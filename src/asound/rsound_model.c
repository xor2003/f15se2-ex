/*
 * rsound_model.c - faithful port of the RSOUND.EXE Roland MT-32 sequencer.
 *
 * Semantics recovered from the F19 (11-21-90) and F15 (3-14-90) drivers;
 * both share the same interpreter, differing only in data. Emits MIDI
 * message words through a sink instead of polling MPU-401 status ports.
 * See rsound_model.h for the stream format.
 *
 * Faithful details kept:
 *  - running-status dedup per emitted status byte (the 0xe20 shadow)
 *  - note-off scheduled by duration - keyoff gap
 *  - nested loop marks: op byte 0 = loop begin, N = loop end (count down)
 *  - f5 is a self-contained chord+duration that ends the channel's fetch
 *    pass via the skip-note flag (0xe22)
 *  - stream-end callback = "load program N on all channels" (the original
 *    cb routine literally re-inits the whole bank); -1 marks intro done
 *  - dispatch arg is a word index (arg>>1 into the handler table)
 *  - drone channel: program 10, note 0x28 vel 0x78, bend=pitch*8+0x2000,
 *    volume=(pitch>>6)+0x3c, tracked per tick while enabled
 */
#include "rsound_model.h"

#include <string.h>

/* ---- MIDI emit ------------------------------------------------------------ */

/* The original dedups the status byte on the MPU wire (the 0xe20 shadow).
 * That changes nothing for the receiving synth's message stream, so we track
 * it for state fidelity but always hand the sink a complete message — the
 * backend (mt32emu playMsg) needs the status byte regardless. */
static void midi_msg(RsoundDriver *drv, int ci, uint8_t status,
                     int ndata, uint8_t a, uint8_t b) {
    uint32_t msg = status | ((uint32_t)a << 8);
    if (ndata > 1) msg |= (uint32_t)b << 16;
    drv->last_status[ci] = status;
    drv->midi(drv->midi_user, msg);
}

static void emit_note_on(RsoundDriver *drv, int ci, uint8_t note, uint8_t vel) {
    midi_msg(drv, ci, (uint8_t)(0x90 | ci), 2, note, vel);
}

static void emit_program(RsoundDriver *drv, int ci, uint8_t prog) {
    midi_msg(drv, ci, (uint8_t)(0xc0 | ci), 1, prog, 0);
}

static void emit_volume(RsoundDriver *drv, int ci, uint8_t v) {
    midi_msg(drv, ci, (uint8_t)(0xb0 | ci), 2, 7, v);
}

static void emit_pan(RsoundDriver *drv, int ci, uint8_t v) {
    midi_msg(drv, ci, (uint8_t)(0xb0 | ci), 2, 0x0a, (uint8_t)(v & 0x7f));
}

static void emit_mod(RsoundDriver *drv, int ci, uint8_t v) {
    midi_msg(drv, ci, (uint8_t)(0xb0 | ci), 2, 1, (uint8_t)(v & 0x7f));
}

static void emit_bend(RsoundDriver *drv, int ci, uint16_t v14) {
    midi_msg(drv, ci, (uint8_t)(0xe0 | ci), 2,
             (uint8_t)(v14 & 0x7f), (uint8_t)((v14 >> 7) & 0x7f));
}

/* ---- held-note note-off (0x686) / note-on --------------------------------- */

static void note_off_all(RsoundDriver *drv, RsoundChannel *c, int ci) {
    int i;
    for (i = 0; i < RSOUND_HELD_NOTES; i++) {
        uint8_t n = c->held[i];
        if (n == 0xff) break;
        emit_note_on(drv, ci, n, 0);
        c->held[i] = 0xff;
    }
}

static void note_on_held(RsoundDriver *drv, RsoundChannel *c, int ci, uint8_t note) {
    int i;
    emit_note_on(drv, ci, note, c->vel);
    for (i = 0; i < RSOUND_HELD_NOTES; i++) {
        if (c->held[i] == 0xff) {
            c->held[i] = note;
            return;
        }
    }
}

/* ---- channel init (0x287): seq_enable masked during reinit ---------------- */

static void channel_load(RsoundDriver *drv, int idx, uint16_t stream, int16_t cb) {
    RsoundChannel *c = &drv->ch[idx];
    uint8_t seq = drv->seq_enable;
    drv->seq_enable = 0;
    memset(c, 0, sizeof(*c));
    c->volsw_r = 0xff;
    c->pansw_r = 0xff;
    c->vol = 0x64;
    c->bend14 = 0x2000;
    c->pan = 0x40;
    c->base = c->pos = c->mark[0] = c->mark[1] = stream;
    c->wait = 1;
    c->cb = cb;
    memset(c->held, 0xff, sizeof(c->held));
    drv->seq_enable = seq;
}

/* load program p onto all channels (the stream-end callback routine) */
static void start_program(RsoundDriver *drv, int p) {
    int i, nch = drv->n_music;
    for (i = 0; i < nch; i++) {
        const RsoundStreamRef *r = &drv->data->music[p * nch + i];
        channel_load(drv, i, r->stream, r->cb);
    }
    drv->music_prog = p;
}

/* ---- per-tick channel effects (0xe71) -------------------------------------- */

static void fx_tick(RsoundDriver *drv, RsoundChannel *c, int ci) {
    if (c->volsw_step) {                        /* f8 volume sweep */
        if (--c->volsw_t == 0) {
            c->volsw_t = c->volsw_r;
            c->vol = (uint8_t)(c->vol + c->volsw_step);
            emit_volume(drv, ci, c->vol);
        }
    }
    if (c->bend_del) {                          /* fa pitch sweep */
        c->bend_del--;
    } else if (c->bend_step) {
        c->bend14 = (uint16_t)(c->bend14 + ((int16_t)c->bend_step << 8));
        emit_bend(drv, ci, c->bend14);
        if (--c->bend_cnt == 0) c->bend_step = 0;
    }
    if (c->pansw_step) {                        /* f3 pan sweep */
        if (--c->pansw_t == 0) {
            c->pansw_t = c->pansw_r;
            c->pan = (uint8_t)(c->pan + c->pansw_step);
            emit_pan(drv, ci, c->pan);
        }
    }
}

/* ---- stream step (0x970 fetch loop) ---------------------------------------- */

static void step_channel(RsoundDriver *drv, RsoundChannel *c, int ci) {
    const uint8_t *blob = drv->data->blob;
    uint8_t skipnote = 0;

    if (!c->wait) goto tail;
    if (c->noteoff && --c->noteoff == 0) note_off_all(drv, c, ci);
    if (--c->wait > 0) goto tail;

    for (;;) {
        uint8_t op = blob[c->pos++];
        if (!(op & 0x80)) {                     /* note+duration */
            uint8_t note = op, dur = blob[c->pos++];
            c->note = note;
            c->wait = dur;
            if (note == 0 || dur == 0) {
                note_off_all(drv, c, ci);
            } else {
                int g = dur - c->gap;
                c->noteoff = (uint8_t)(g > 0 ? g : 1);
                note_on_held(drv, c, ci, note);
            }
            goto tail;
        }
        switch (op) {
        case 0xf2: emit_mod(drv, ci, blob[c->pos++]); break;
        case 0xf3: {                            /* pan sweep r,s */
            uint8_t r = blob[c->pos], s = blob[c->pos + 1];
            c->pansw_r = r;
            c->pansw_step = (int8_t)s;
            c->pos += 2;
            if (s == 0) {
                c->pan = 0x40;
                emit_pan(drv, ci, 0x40);
            }
            c->pansw_t = 1;
            break;
        }
        case 0xf4:
            c->pan = blob[c->pos];
            emit_pan(drv, ci, blob[c->pos++]);
            break;
        case 0xf5: {                            /* chord of n + duration */
            uint8_t n = blob[c->pos++];
            int i;
            for (i = 0; i < n && i < RSOUND_HELD_NOTES; i++) {
                c->held[i] = blob[c->pos++];
                emit_note_on(drv, ci, c->held[i], c->vel);
            }
            for (; i < n; i++) c->pos++;        /* >4 notes: still consumed */
            c->wait = blob[c->pos++];
            {
                int g = c->wait - c->gap;
                c->noteoff = (uint8_t)(g > 0 ? g : 1);
            }
            skipnote = 1;
            goto tail;
        }
        case 0xf6:
            c->vol = blob[c->pos];
            emit_volume(drv, ci, blob[c->pos++]);
            break;
        case 0xf7: {
            uint8_t b = blob[c->pos++];
            c->bend14 = (uint16_t)((uint16_t)b << 8);
            emit_bend(drv, ci, c->bend14);
            break;
        }
        case 0xf8: {                            /* volume sweep r,s */
            uint8_t r = blob[c->pos], s = blob[c->pos + 1];
            c->volsw_r = r;
            c->volsw_step = (int8_t)s;
            c->pos += 2;
            c->volsw_t = 1;
            break;
        }
        case 0xf9: c->vel = blob[c->pos++]; break;
        case 0xfa: {                            /* pitch sweep d,s,l */
            c->bend14 = 0x2000;
            c->bend_del = blob[c->pos];
            c->bend_step = (int8_t)blob[c->pos + 1];
            c->bend_cnt = blob[c->pos + 2];
            c->pos += 3;
            c->bflag = 1;
            break;
        }
        case 0xfb: c->gap = blob[c->pos++]; break;
        case 0xfc: emit_program(drv, ci, blob[c->pos++]); break;
        case 0xfd:                              /* stream end */
            if (c->cb == -2) {                  /* no cb: loop to base */
                c->pos = c->base;
                break;
            }
            c->wait = 0;                        /* channel done */
            if (c->cb == -1) {
                drv->intro_done = 0xff;
            } else {
                drv->pending_prog = c->cb;      /* cb: load program N */
            }
            goto tail;
        case 0xfe:
        case 0xff: {                            /* nested loops */
            int k = op - 0xfe;
            uint8_t cnt = blob[c->pos];
            if (c->loop[k] == 0) {
                if (cnt == 0) {                 /* begin marker */
                    c->mark[k] = (uint16_t)(c->pos + 1);
                } else {                        /* loop end: count it */
                    c->loop[k] = cnt;
                    c->mark[k] = (uint16_t)(c->pos + 1);
                }
            } else if (--c->loop[k] != 0) {
                c->pos = c->mark[k];
            }
            c->pos++;
            break;
        }
        default: break;                         /* 0x80-0xf1 skipped */
        }
        if (skipnote) goto tail;
    }

tail:
    fx_tick(drv, c, ci);
}

/* ---- drone channel (0xf44): jet noise on ch after music -------------------- */

static void drone_tick(RsoundDriver *drv, int ci) {
    if (drv->drone_on != drv->drone_prev) {
        if (drv->drone_on) {
            emit_program(drv, ci, 10);
            emit_pan(drv, ci, 0x40);
            emit_bend(drv, ci, (uint16_t)(drv->drone_pitch * 8 + 0x2000));
            emit_volume(drv, ci, (uint8_t)((drv->drone_pitch >> 6) + 0x3c));
            emit_note_on(drv, ci, 0x28, 0x78);
            drv->drone_held = 0x28;
        } else {
            emit_note_on(drv, ci, drv->drone_held, 0);
            drv->drone_held = 0;
            emit_bend(drv, ci, 0x2000);
        }
        drv->drone_prev = drv->drone_on;
        drv->drone_last = drv->drone_pitch;
    } else if (drv->drone_on && drv->drone_pitch != drv->drone_last) {
        emit_bend(drv, ci, (uint16_t)(drv->drone_pitch * 8 + 0x2000));
        emit_volume(drv, ci, (uint8_t)((drv->drone_pitch >> 6) + 0x3c));
        drv->drone_last = drv->drone_pitch;
    }
}

/* ---- public API ------------------------------------------------------------- */

void rsound_init(RsoundDriver *drv, const RsoundData *data,
                 RsoundMidiSink midi, RsoundSysexSink sysex, void *user) {
    int i;
    memset(drv, 0, sizeof(*drv));
    drv->data = data;
    drv->midi = midi;
    drv->sysex = sysex;
    drv->midi_user = user;
    drv->n_music = (uint8_t)(data->n_channels ? data->n_channels
                                             : RSOUND_MUSIC_CHANNELS);
    drv->drone_max = 0x02bc;      /* [0xe18] pitch clamp */
    drv->rng = 0x04d2;
    drv->rng_add = 0x9248;        /* [0xe1e] scramble const */
    drv->throttle = 0;
    drv->intro_done = 0;
    drv->pending_prog = -1;
    drv->music_prog = -1;
    for (i = 0; i < drv->n_music; i++)
        memset(drv->ch[i].held, 0xff, sizeof(drv->ch[i].held));
    drv->seq_enable = 0;
    /* setup: MPU reset, UART mode, then the embedded timbre dump. The
     * original sends f0 41 10 16 12 + addr3 + data + checksum + f7. */
    if (sysex) {
        static const uint8_t hdr[5] = {0xf0, 0x41, 0x10, 0x16, 0x12};
        for (i = 0; i < data->init_count; i++) {
            const RsoundSysex *r = &data->init[i];
            uint8_t frame[300];
            size_t n = 0;
            uint16_t j;
            uint8_t chk = 0;
            memcpy(frame, hdr, 5);
            n = 5;
            frame[n++] = (uint8_t)(r->addr >> 16);
            frame[n++] = (uint8_t)(r->addr >> 8);
            frame[n++] = (uint8_t)(r->addr);
            chk = (uint8_t)(frame[n - 3] + frame[n - 2] + frame[n - 1]);
            for (j = 0; j < r->len; j++) {
                frame[n] = r->data[j];
                chk += r->data[j];
                n++;
            }
            frame[n++] = (uint8_t)(-(int)chk & 0x7f);
            frame[n++] = 0xf7;
            sysex(user, frame, n);
        }
    }
    drv->seq_enable = 1;
}

void rsound_shutdown(RsoundDriver *drv) {
    int i;
    for (i = 0; i < drv->n_music; i++) {
        if (drv->ch[i].wait) note_off_all(drv, &drv->ch[i], i);
    }
    if (drv->drone_prev) {
        emit_note_on(drv, drv->n_music, drv->drone_held, 0);
    }
    drv->seq_enable = 0;
    drv->drone_on = 0;
    drv->drone_prev = 0;
}

int rsound_dispatch_sound(RsoundDriver *drv, int sound_id) {
    const RsoundSound *s;
    int idx = sound_id >> 1;             /* slot arg is a word index */
    if (idx < 0 || idx >= drv->data->sound_count) return 0;
    s = &drv->data->sounds[idx];
    if (s->channel < 0 || s->count == 0) return 1;
    if (s->if_idle && drv->ch[s->channel].wait) return 1;
    {
        int pick = s->count > 1 ? (int)((drv->rng >> 3) & 3) % s->count : 0;
        channel_load(drv, s->channel, s->streams[pick], 0);
    }
    return 1;
}

void rsound_start_intro(RsoundDriver *drv) {
    /* the original ignores the slot while an intro is still playing but
     * restarts freely once every channel has run dry */
    if (drv->seq_enable != 1 || rsound_intro_active(drv)) return;
    drv->intro_done = 0;
    drv->pending_prog = -1;
    start_program(drv, 0);
}

void rsound_drone_on(RsoundDriver *drv) { drv->drone_on = 1; }
void rsound_drone_off(RsoundDriver *drv) { drv->drone_on = 0; }

void rsound_set_drone_pitch(RsoundDriver *drv, uint16_t pitch) {
    if (pitch > drv->drone_max) pitch = drv->drone_max;
    drv->drone_pitch = pitch;
}

uint16_t rsound_tick(RsoundDriver *drv) {
    int i;
    if (--drv->throttle == 0) {
        drv->throttle = 7;
        /* [0x23c] skip gate: set by setup variants; default clear */
    }
    drv->rng = (uint16_t)(((drv->rng + drv->rng_add) >> 3) |
                          (((drv->rng + drv->rng_add) & 7) << 13));
    if (drv->seq_enable) {
        for (i = 0; i < drv->n_music; i++) {
            if (drv->ch[i].wait) step_channel(drv, &drv->ch[i], i);
        }
        drone_tick(drv, drv->n_music);
    }
    if (drv->pending_prog >= 0) {
        start_program(drv, drv->pending_prog);
        drv->pending_prog = -1;
    }
    {
        uint16_t st = drv->pending_status;
        drv->pending_status = 0;
        return st;
    }
}

int rsound_intro_active(const RsoundDriver *drv) {
    int i;
    if (drv->intro_done) return 0;
    for (i = 0; i < drv->n_music; i++)
        if (drv->ch[i].wait) return 1;
    return 0;
}

void rsound_intro_skip(RsoundDriver *drv) {
    if (drv->intro_done || drv->music_prog < 0) return;
    /* keypress path: jump straight to the final program (0x404 call) */
    start_program(drv, drv->data->music_programs - 1);
}
