#ifndef RSOUND_MODEL_H
#define RSOUND_MODEL_H

/*
 * rsound_model - native port of the MicroProse RSOUND.EXE Roland MT-32 driver.
 *
 * The original overlay speaks MPU-401 UART (intelligent mode): a SysEx timbre
 * dump on setup, then a bytecode sequencer emitting standard MIDI channel
 * messages (9x/8x handled as 9x vel0, Bx CC1/CC7/CC10, Cx, Ex) with running-
 * status dedup. This model keeps layer-1 semantics identical to the DOS
 * driver but emits MIDI message words to a sink instead of touching ports.
 *
 * Stream format (recovered by disassembly; see tools/rsound_extract.py):
 *   00-7f,dd   note dd ticks (note==0 or dd==0 -> all-notes-off; dd==0 kills
 *              the channel)
 *   f2 p       CC1 modulation        f8 r,s   volume sweep (rate, signed step)
 *   f3 r,s     pan sweep (s==0=64)   f9 v     velocity
 *   f4 v       CC10 pan              fa d,s,l pitch sweep (delay,step<<8,len)
 *   f5 n,n1..  poly note-on          fb g     keyoff gap
 *   f6 v       CC7 volume            fc p     program change
 *   f7 b       pitch bend b<<8       fd       end: callback else loop-to-base
 *   fe/ff k    nested loop end; count byte 0 = loop begin marker
 */

#include <stddef.h>
#include <stdint.h>

#include "asound_model.h" /* AsoundU8/U16 compatibility typedefs */

#ifdef __cplusplus
extern "C" {
#endif

#define RSOUND_MAX_CHANNELS 8 /* F19: 6 music + drone, F15: 7 + drone */
#define RSOUND_MUSIC_CHANNELS 6
#define RSOUND_HELD_NOTES 4

/* ---- extracted table types (see rsound_data_<game>.inc) ------------------ */

typedef struct RsoundSysex {
    uint32_t addr;
    uint8_t data[256];
    uint16_t len;
} RsoundSysex;

/* One 6-channel music program entry; cb = index of the program the stream-end
 * callback starts, or -1 when the intro chain ends there. */
typedef struct RsoundStreamRef {
    uint16_t stream; /* offset into the blob, relative to BLOB_BASE */
    int16_t cb;
} RsoundStreamRef;

typedef struct RsoundSound {
    int8_t channel; /* -1 = no-op */
    uint8_t if_idle; /* 1 = load only when the channel is idle (ambient) */
    uint8_t count;  /* variant count (1 or 4) */
    uint16_t streams[6];
} RsoundSound;

typedef struct RsoundData {
    const uint8_t *blob;
    uint16_t blob_base;
    uint16_t blob_len;
    const RsoundSysex *init;
    uint8_t init_count;
    const RsoundStreamRef *music; /* programs * n_channels channel entries */
    uint8_t music_programs;
    uint8_t n_channels;  /* 6 for F19, 7 for F15 */
    const RsoundSound *sounds;
    uint8_t sound_count;
} RsoundData;

extern const RsoundData rsound_data_f19;
extern const RsoundData rsound_data_f15;

/* ---- driver state --------------------------------------------------------- */

typedef struct RsoundChannel {
    uint8_t wait;         /* [0]  ticks until next event; 0 = dead */
    int8_t bend_step;     /* [1]  pitch sweep step (<<8 applied) */
    int8_t volsw_step;    /* [2]  volume sweep step */
    int8_t pansw_step;    /* [3]  pan sweep step */
    uint8_t note;         /* [4]  last note */
    uint8_t pad5;
    uint8_t vel;          /* [6]  velocity */
    uint8_t gap;          /* [7]  keyoff gap */
    uint8_t noteoff;      /* [8]  ticks until note-off */
    uint8_t volsw_t;      /* [9]  volume sweep tick countdown */
    uint8_t bflag;        /* [0xa] pitch sweep active */
    uint8_t pansw_t;      /* [0xb] pan sweep tick countdown */
    uint8_t vol;          /* [0xc] CC7 */
    uint8_t pan;          /* [0xd] CC10 */
    uint8_t volsw_r;      /* [0xe] volume sweep rate */
    uint8_t pansw_r;      /* [0xf] pan sweep rate */
    uint8_t bend_cnt;     /* [0x10] pitch sweep length */
    uint8_t bend_del;     /* [0x11] pitch sweep delay */
    uint16_t base;        /* [0x12] stream base (loop target) */
    uint16_t pos;         /* [0x14] stream pos */
    uint16_t mark[2];     /* [0x16]/[0x18] loop marks */
    uint16_t loop[2];     /* [0x1a]/[0x1c] loop counts */
    uint16_t bend14;      /* [0x1e] 14-bit bend value */
    int16_t cb;           /* [0x20] end callback: next program index, -1 none */
    uint8_t held[RSOUND_HELD_NOTES]; /* [0x1980+ch*4] held notes for note-off */
} RsoundChannel;          /* layout mirrors the original 0x22-byte record */

/* MIDI message sink: msg = status | data0<<8 | data1<<16. SysEx: full Roland
 * frame (f0 41 10 16 12 addr3 data chk f7) delivered as a byte span. */
typedef void (*RsoundMidiSink)(void *user, uint32_t msg);
typedef void (*RsoundSysexSink)(void *user, const uint8_t *frame, size_t len);

typedef struct RsoundDriver {
    const RsoundData *data;
    RsoundMidiSink midi;
    RsoundSysexSink sysex;
    void *midi_user;
    RsoundChannel ch[RSOUND_MAX_CHANNELS];
    uint8_t n_music;      /* music channel count (drone runs on n_music) */
    uint8_t seq_enable;   /* [0x18b0] play_intro guard / sequencer on */
    uint8_t drone_on;     /* [0xe1a] */
    uint8_t drone_prev;   /* [0xe1b] */
    uint8_t drone_held;   /* note currently sounding on the drone channel */
    uint16_t drone_pitch; /* [0xe12] engine-noise intensity */
    uint16_t drone_max;   /* [0xe18] clamp */
    uint16_t drone_last;  /* [0xe14] last rendered pitch */
    uint16_t rng;         /* [0xe1c] PRNG for variant pick */
    uint16_t rng_add;     /* [0xe1e] scramble constant (0x9248) */
    uint8_t last_status[RSOUND_MAX_CHANNELS + 1]; /* running-status dedup */
    uint8_t throttle;     /* [0x23d] tick throttle counter */
    uint16_t pending_status; /* [0x240] status returned by the tick slot */
    int16_t intro_done;   /* [0x244] 0xff when intro chain finished */
    int music_prog;       /* program currently loaded (-1 = none) */
    int pending_prog;     /* program an fd callback asked to load */
} RsoundDriver;

void rsound_init(RsoundDriver *drv, const RsoundData *data,
                 RsoundMidiSink midi, RsoundSysexSink sysex, void *user);
void rsound_shutdown(RsoundDriver *drv);
int rsound_dispatch_sound(RsoundDriver *drv, int sound_id);
void rsound_start_intro(RsoundDriver *drv);
void rsound_drone_on(RsoundDriver *drv);
void rsound_drone_off(RsoundDriver *drv);
void rsound_set_drone_pitch(RsoundDriver *drv, uint16_t pitch);
/* One game tick: steps the sequencer + drone channel, emits through the sink.
 * Returns the original tick-slot status word (drone_pitch while the drone
 * channel is active, else 0). */
uint16_t rsound_tick(RsoundDriver *drv);
/* Intro still playing? (any music channel alive and not past last program) */
int rsound_intro_active(const RsoundDriver *drv);
/* Skip to the final intro program (keypress during intro) */
void rsound_intro_skip(RsoundDriver *drv);

#ifdef __cplusplus
}
#endif
#endif /* RSOUND_MODEL_H */
