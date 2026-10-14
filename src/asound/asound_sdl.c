/*
 * asound_sdl.c - SDL3 audio backend for the ported ASOUND AdLib driver.
 *
 * Replaces the DOS glue (asound_/asound_model/asdrv51.*): instead of busy-waited
 * writes to OPL ports 388h/389h, the decoded driver/asopl shadow state is
 * flushed into a Nuked-OPL3 (YM3812/OPL2) emulator and rendered to PCM on an SDL
 * audio thread. Digitized voice cues (F15DGTL.BIN) are mixed in as raw PCM.
 * On DOS (FM_EMULATION=OFF) the shadow is written to the real OPL at 388h
 * instead and only the voice cues are rendered to PCM.
 *
 * The game-facing audio_* slot ABI (slot.h) maps by name onto the model's
 * sound_driver_* entry points. The audio thread runs the sequencer at 60 Hz (the
 * game tick rate); a single mutex guards the driver/asopl state it shares with
 * the game thread.
 */

#include <SDL3/SDL.h>

#include "../shared/asset_compare.h"
#include "asound_model.h"
#include "asound_music_replacement.h"
#include "asound_replacements.h"
#include "asopl.h"
#include "compressed_audio.h"
#if F15_FM_EMULATION
/* opl3.h has no extern "C" guard of its own; this TU compiles as C++ but opl3.c
 * is built as C, so the declarations must use C linkage to match. */
extern "C" {
#include "opl3.h"
}
#else
#include "asound_dos_opl.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "inttype.h"
#include <dos.h>
#include "slot.h"
#include "common.h" /* openFile */
#include "input.h"  /* input_keyWaiting / input_setMode */
#include "log.h"
#include "shared/blackbox.h"
#include "shared/blackbox_wait.h"

#ifdef __DJGPP__
#include "asound_dos_opl.h"
#endif

#define ASND_OUT_RATE 44100 /* SDL output sample rate (Hz) */
#define ASND_TICK_HZ 60     /* sequencer tick = game tick rate */
#define ASND_CHUNK 512      /* max frames generated per inner loop pass */

static AsoplState g_opl; /* OPL register shadow (event -> regs) */
#if F15_FM_EMULATION
static opl3_chip g_chip; /* Nuked-OPL3 synthesis state */
#endif
static SDL_AudioStream *g_stream;
static SDL_Mutex *g_lock;
static AsoundU8 g_hwShadow[ASOPL_REGISTER_COUNT]; /* last value written to chip */
static bool g_ready;                              /* device opened */

/* Digitized-sample blob + active playback cursor (audio-thread-only state). */
static AsoundU8 *g_blob;
static int g_blobSize;
static bool g_smpActive;
static const AsoundU8 *g_smpData{};
static double g_smpPos; /* fractional read position (bytes) */
static int g_smpSize{};
static double g_smpStep = (double)ASND_SAMPLE_HZ / (double)ASND_OUT_RATE;

static CompressedAudio g_compressedIntro;
static double g_compressedIntroPos;
static bool g_compressedIntroActive;
static bool g_compressedIntroTried;

/* Fractional countdown (in output frames) to the next sequencer tick. */
static double g_tickAccum;


static AsoundU8 *readEntireIo(SDL_IOStream *io, Sint64 *outSize) {
    Sint64 size;
    AsoundU8 *data;
    size_t got;

    if (outSize) *outSize = 0;
    if (!io) return NULL;
    size = SDL_GetIOSize(io);
    if (size <= 0) return NULL;

    data = (AsoundU8 *)SDL_malloc((size_t)size);
    if (!data) return NULL;
    got = SDL_ReadIO(io, data, (size_t)size);
    if (got != (size_t)size) {
        SDL_free(data);
        return NULL;
    }
    if (outSize) *outSize = size;
    return data;
}


static AsoundU8 *loadLegacyDigitizedBlob(int *outSize) {
    SDL_IOStream *io = openFile("F15DGTL.BIN", 0);
    Sint64 size = 0;
    AsoundU8 *data;
    if (outSize) *outSize = 0;
    if (!io) return NULL;
    data = readEntireIo(io, &size);
    SDL_CloseIO(io);
    if (!data) return NULL;
    if (outSize) *outSize = (int)size;
    return data;
}

static int loadCompressedIntro(void) {
    char path[512];
    if (g_compressedIntroTried) return g_compressedIntro.samples != NULL;
    g_compressedIntroTried = true;
    if (!findReplacementAssetPath("sounds/intro_music", ".ogg", path, sizeof(path)) &&
        !findReplacementAssetPath("sounds/intro_music", ".mp3", path, sizeof(path))) {
        return 0;
    }
    if (!compressedAudioLoad(path, &g_compressedIntro)) {
        LogWarn(("asset replacement: failed to decode intro music %s; using ASOUND", path));
        return 0;
    }
    LogInfo(("asset replacement: loaded compressed intro music from %s", path));
    return 1;
}

static const double g_samplesPerTick = (double)ASND_OUT_RATE / (double)ASND_TICK_HZ;

/* ---- OPL shadow -> chip --------------------------------------------------- */

static void asnd_resetChip(void) {
#if F15_FM_EMULATION
    OPL3_Reset(&g_chip, ASND_OUT_RATE);
#endif
}

static void asnd_invalidateHwShadow(void) {
    for (int r = 0; r < ASOPL_REGISTER_COUNT; r++) g_hwShadow[r] = 0xff;
}

/* Push every changed shadow register into the emulator (mirrors asdrv_sync_backend). */
static void asnd_syncChip(void) {
    for (int r = 0; r < ASOPL_REGISTER_COUNT; r++) {
        AsoundU8 v = asopl_get_register(&g_opl, (AsoundU8)r);
        if (g_hwShadow[r] == v) continue;
#if F15_FM_EMULATION
        OPL3_WriteReg(&g_chip, (uint16_t)r, v);
#else
        asnd_writeDosOpl((unsigned char)r, v);
#endif
        g_hwShadow[r] = v;
    }
}

/* ---- sequencer tick (audio thread, under g_lock) -------------------------- */

static void asnd_startSample(AsoundU16 start, AsoundU16 end) {
    AsoundReplacementCue cue{};
    if (asound_find_replacement_cue(start, end, &cue)) {
        g_smpData = cue.samples;
        g_smpSize = cue.sample_count;
        g_smpPos = 0;
        g_smpStep = (double)cue.sample_rate / (double)ASND_OUT_RATE;
        g_smpActive = true;
        return;
    }
    if (!g_blob || g_blobSize <= 0) return;
    int s = start, e = (int)end + 1;
    if (e > g_blobSize) e = g_blobSize;
    if (s < 0 || s >= e) return;
    g_smpData = g_blob + s;
    g_smpSize = e - s;
    g_smpPos = 0;
    g_smpStep = (double)ASND_SAMPLE_HZ / (double)ASND_OUT_RATE;
    g_smpActive = true;
}

/* One 60 Hz service tick: advance streams, apply events to the OPL shadow, run
 * the per-tick pitch-slide/noise/drone updates, then flush to the chip. This is
 * asdrv_timer + asdrv_noise from the DOS glue, minus the hardware I/O. */
static void asnd_tick(void) {
    AsoundDriver *drv = sound_driver_state();
    AsoundEvent events[32];
    size_t count = asound_driver_tick_events(drv, events, 32);
    for (size_t i = 0; i < count; i++) {
        if (events[i].type == ASOUND_EVENT_SAMPLE_RANGE) {
            asnd_startSample(events[i].a, events[i].b);
        } else {
            asopl_apply_event(&g_opl, drv, &events[i]);
        }
    }
    asopl_service_tick(&g_opl, drv);
    asopl_noise_tick(&g_opl);
    asnd_syncChip();
}

/* ---- PCM rendering (audio thread, lock-free) ------------------------------ */

static void asnd_mixSample(Sint16 *buf, int frames) {
    if (!g_smpActive) return;
    for (int i = 0; i < frames; i++) {
        int idx = (int)g_smpPos;
        if (idx >= g_smpSize) {
            g_smpActive = false;
            break;
        }
        /* 8-bit unsigned -> signed, scaled below full-scale to leave OPL headroom */
        int s = ((int)g_smpData[idx] - 128) * 200;
        int l = buf[2 * i] + s;
        int r = buf[2 * i + 1] + s;
        if (l > 32767)
            l = 32767;
        else if (l < -32768)
            l = -32768;
        if (r > 32767)
            r = 32767;
        else if (r < -32768)
            r = -32768;
        buf[2 * i] = (Sint16)l;
        buf[2 * i + 1] = (Sint16)r;
        g_smpPos += g_smpStep;
    }
}

static void asnd_mixCompressedIntro(Sint16 *buf, int frames) {
    const double step = g_compressedIntro.sampleRate > 0
        ? (double)g_compressedIntro.sampleRate / (double)ASND_OUT_RATE : 1.0;
    if (!g_compressedIntroActive) return;
    for (int frame = 0; frame < frames; frame++) {
        uint64_t sourceFrame = (uint64_t)g_compressedIntroPos;
        if (sourceFrame >= g_compressedIntro.frames) {
            g_compressedIntroActive = false;
            break;
        }
        for (int outputChannel = 0; outputChannel < 2; outputChannel++) {
            int sourceChannel = g_compressedIntro.channels == 1 ? 0 : outputChannel;
            int sample = g_compressedIntro.samples[sourceFrame * g_compressedIntro.channels + sourceChannel];
            int mixed = buf[frame * 2 + outputChannel] + sample * 3 / 4;
            buf[frame * 2 + outputChannel] = (Sint16)(mixed < -32768 ? -32768 : mixed > 32767 ? 32767 : mixed);
        }
        g_compressedIntroPos += step;
    }
}

static bool asnd_compressedIntroIsActive(void) {
    bool active;

    SDL_LockMutex(g_lock);
    active = g_compressedIntroActive;
    SDL_UnlockMutex(g_lock);
    return active;
}

static void SDLCALL asnd_callback(void *user, SDL_AudioStream *stream,
                                  int additional, int total) {
    (void)user;
    (void)total;
    Sint16 buf[ASND_CHUNK * 2];
    int framesNeeded = additional / (int)sizeof(Sint16) / 2;

    while (framesNeeded > 0) {
        /* The OPL chip and driver state are shared with the game thread (which
         * calls OPL3_Reset/register writes via audio_setup/shutdown), so the
         * tick + generation must be serialized with it. Lock per chunk (<=512
         * frames) so game-thread audio_* calls only ever block briefly. */
        SDL_LockMutex(g_lock);
        while (g_tickAccum < 1.0) {
            asnd_tick();
            g_tickAccum += g_samplesPerTick;
        }
        int chunk = framesNeeded;
        if (chunk > (int)g_tickAccum) chunk = (int)g_tickAccum;
        if (chunk > ASND_CHUNK) chunk = ASND_CHUNK;

#if F15_FM_EMULATION
        OPL3_GenerateStream(&g_chip, buf, (uint32_t)chunk);
#else
        /* The chip produces the FM itself; only digitized speech needs PCM. */
        SDL_memset(buf, 0, chunk * 2 * sizeof(Sint16));
#endif
        asnd_mixSample(buf, chunk);
        asnd_mixCompressedIntro(buf, chunk);
        g_tickAccum -= chunk;
        SDL_UnlockMutex(g_lock);

        SDL_PutAudioStreamData(stream, buf, chunk * 2 * (int)sizeof(Sint16));
        framesNeeded -= chunk;
    }
}

/* ---- device lifecycle ----------------------------------------------------- */

static void asnd_openDevice(void) {
    const char *audio = SDL_getenv("F15_AUDIO");
    if (g_ready) return;
    if (audio && (!SDL_strcasecmp(audio, "off") || !SDL_strcmp(audio, "0"))) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogError(("asound: SDL_INIT_AUDIO failed: %s", SDL_GetError()));
        return;
    }
    g_lock = SDL_CreateMutex();
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = ASND_OUT_RATE;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                         asnd_callback, NULL);
    if (!g_stream) {
        LogError(("asound: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError()));
        SDL_DestroyMutex(g_lock);
        g_lock = NULL;
        return;
    }
    /* Make the synth valid before the callback can fire: a zeroed opl3_chip has
     * rateratio 0 and OPL3_GenerateStream would divide by zero. */
    asopl_init(&g_opl);
    asnd_resetChip();
    asnd_invalidateHwShadow();
    g_tickAccum = g_samplesPerTick;
    SDL_ResumeAudioStreamDevice(g_stream);
    g_ready = true;
}

/* Reset the synth to silence (used by setup and shutdown). Caller holds g_lock
 * if the device is already running. */
static void asnd_resetSynth(AsoundU16 setupValue) {
    sound_driver_setup(setupValue, /*driver_segment=*/1); /* nonzero: enables intro */
    asopl_init(&g_opl);
    asopl_set_drone_pitch(&g_opl, 0);
    asopl_set_drone_enable(&g_opl, 0);
    asnd_resetChip();
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
    g_compressedIntroActive = false;
}

/* ---- game-facing slot ABI (slot.h) --------------------------------------- */

int FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel) {
    (void)sampleDataSeg; /* segments are meaningless natively; blob is loaded by loadF15DgtlBin */
    asnd_openDevice();
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    asnd_resetSynth((AsoundU16)variantSel);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_shutdown(void) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_shutdown();
    asopl_reset(&g_opl);
    asnd_resetChip();
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_playSound(int soundId) {
    /* Fast-forward advances many simulated seconds per wall-clock second. Do
     * not serialize every historical sound update against the real-time audio
     * callback: audio is not simulation state, and fresh updates resume at the
     * target tick. This also prevents a backlog of obsolete sounds. */
    if (!g_ready || blackbox_fastForwarding()) return 0;
    SDL_LockMutex(g_lock);
    int r = sound_driver_dispatch_sound((AsoundU16)soundId);
    SDL_UnlockMutex(g_lock);
    return r;
}

/* Voices 0-2 carry the intro melody; all three idle (stream_ptr cleared) once a
 * set of intro/release streams has played out. */
static bool asnd_introVoicesActive(void) {
    AsoundDriver *drv = sound_driver_state();
    bool active;
    SDL_LockMutex(g_lock);
    active = drv->streams[0].stream_ptr || drv->streams[1].stream_ptr ||
             drv->streams[2].stream_ptr;
    SDL_UnlockMutex(g_lock);
    return active;
}

/* Faithful port of adlib_play_intro_until_key: play the title jingle, then peek
 * the keyboard without consuming it (so the caller's getKey still sees the
 * press), play the short release tail, and silence the chip. Blocking, like the
 * original - the title screen is a blocking wait regardless. Deadlines are
 * defensive caps so a stuck stream can never hang the title. */
int FAR CDECL audio_playIntro(void) {
    asnd_openDevice();
    if (!g_ready) {
        LogWarn(("asound: intro music skipped because no audio device is ready"));
        return 0;
    }

    if (loadCompressedIntro()) {
        LogInfo(("asound: playing compressed intro music"));
        SDL_LockMutex(g_lock);
        g_compressedIntroPos = 0;
        g_compressedIntroActive = true;
        SDL_UnlockMutex(g_lock);
        input_setMode(INPUT_MODE_MENU);
        while (asnd_compressedIntroIsActive() && !input_keyWaiting()) {
            SDL_DelayNS(2 * SDL_NS_PER_MS);
        }
        SDL_LockMutex(g_lock);
        g_compressedIntroActive = false;
        SDL_UnlockMutex(g_lock);
        LogInfo(("asound: compressed intro music finished"));
        return 0;
    }

    LogInfo(("asound: playing intro music"));
    SDL_LockMutex(g_lock);
    if (!asound_start_replacement_music(sound_driver_state(), 0)) {
        sound_driver_play_intro();
    }
    SDL_UnlockMutex(g_lock);

    input_setMode(INPUT_MODE_MENU);
    if (blackbox_virtualWait(15u * 60u, 1, input_keyWaiting)) {
        SDL_LockMutex(g_lock);
        asound_driver_start_intro_rel(sound_driver_state());
        SDL_UnlockMutex(g_lock);
        blackbox_virtualWait(2u * 60u, 0, input_keyWaiting);
    } else {
        Uint64 deadline = SDL_GetTicksNS() + 15 * SDL_NS_PER_SECOND;
        while (asnd_introVoicesActive() && !input_keyWaiting() &&
               SDL_GetTicksNS() < deadline) {
            SDL_DelayNS(2 * SDL_NS_PER_MS); /* input_keyWaiting() pumps the clock */
        }

        /* release tail (adlib_start_intro_release), then let it decay out */
        SDL_LockMutex(g_lock);
        if (!asound_start_replacement_music(sound_driver_state(), 1)) {
            asound_driver_start_intro_rel(sound_driver_state());
        }
        SDL_UnlockMutex(g_lock);
        deadline = SDL_GetTicksNS() + 2 * SDL_NS_PER_SECOND;
        while (asnd_introVoicesActive() && SDL_GetTicksNS() < deadline) {
            SDL_DelayNS(2 * SDL_NS_PER_MS);
            input_keyWaiting();
        }
    }

    /* silence the chip (adlib_reset_state) */
    SDL_LockMutex(g_lock);
    asopl_reset(&g_opl);
    asnd_resetChip();
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
    SDL_UnlockMutex(g_lock);
    LogInfo(("asound: intro music finished"));
    return 0;
}

int FAR CDECL audio_engineDroneOn(void) {
    if (!g_ready || blackbox_fastForwarding()) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_enable_drone();
    asopl_set_drone_enable(&g_opl, 1);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_engineDroneOff(void) {
    if (!g_ready || blackbox_fastForwarding()) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_disable_drone();
    asopl_set_drone_enable(&g_opl, 0);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_setEnginePitch(int knots, int thrust) {
    (void)thrust; /* the original ASOUND set_drone_pitch slot used only the first arg */
    if (!g_ready || blackbox_fastForwarding()) return 0;
    AsoundU16 pitch = (knots < 0) ? 0 : (knots > 0x02bc ? 0x02bc : (AsoundU16)knots);
    SDL_LockMutex(g_lock);
    sound_driver_set_drone_pitch(pitch);
    asopl_set_drone_pitch(&g_opl, pitch);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_playSample(int sampleIdx) {
    if (!g_ready || blackbox_fastForwarding()) return 0;
    SDL_LockMutex(g_lock);
    int r = sound_driver_play_sample((AsoundU16)sampleIdx);
    SDL_UnlockMutex(g_lock);
    return r;
}

/* The sequencer is driven from the audio callback, so the game's per-tick audio
 * hooks (originally the PIT IRQ path) are no-ops here. */
int FAR CDECL audio_timerTick(void) { return 0; }
int FAR CDECL audio_noiseTick(void) { return 0; }

/* ---- digitized sample blob ----------------------------------------------- */

/* Load F15DGTL.BIN (the digitized voice/effect blob) and any modern cue WAVs.
 * The legacy blob size is still returned because game code stores it in
 * f15DgtlResult and passes it to audio_setup as the sample-variant selector.
 * If the blob is missing but cue WAVs exist, return the known ASOUND span so
 * those modern replacements can play without requiring JSON sidecars. */
int loadF15DgtlBin(void) {
    AsoundU8 *data;
    int size = 0;
    int replacementSpan;

    data = loadLegacyDigitizedBlob(&size);
    asound_load_replacement_cues(data, size);
    replacementSpan = asound_replacement_logical_span();
    if ((!data || size <= 0) && replacementSpan <= 0) {
        LogError(("asound: cannot open F15DGTL.BIN or replacement cue WAVs"));
        return 0;
    }

    if (g_lock) SDL_LockMutex(g_lock);
    SDL_free(g_blob);
    g_blob = data;
    g_blobSize = size;
    if (g_lock) SDL_UnlockMutex(g_lock);

    /* f15DgtlResult doubles as the variant selector, so report the known ASOUND
     * span — not just the loaded-cue coverage — when the legacy blob is absent. */
    if (size > 0) return size;
    return replacementSpan ? 0x7d9e : 0;
}

#ifdef DEBUG
int asound_testCompareReplacementCues(void) {
    AsoundU8 *legacyBlob = NULL;
    int legacySize = 0;
    int loaded;
    int i;

    legacyBlob = loadLegacyDigitizedBlob(&legacySize);
    if (!legacyBlob || legacySize <= 0) {
        SDL_free(legacyBlob);
        return 0;
    }
    loaded = asound_load_replacement_cues(legacyBlob, legacySize);
    if (loaded != asound_replacement_cue_count()) {
        SDL_free(legacyBlob);
        return 0;
    }
    for (i = 0; i < asound_replacement_cue_count(); i++) {
        AsoundReplacementCue cue;
        AsoundU16 start = 0, endInclusive = 0;
        int expectedSize;
        if (!asound_replacement_cue_at(i, &start, &endInclusive, &cue)) {
            SDL_free(legacyBlob);
            return 0;
        }
        expectedSize = (int)endInclusive + 1 - (int)start;
        if (!cue.samples || cue.sample_rate != ASND_SAMPLE_HZ ||
            cue.sample_count != expectedSize || endInclusive + 1 > legacySize ||
            memcmp(cue.samples, legacyBlob + start, (size_t)expectedSize) != 0) {
            SDL_free(legacyBlob);
            return 0;
        }
    }
    SDL_free(legacyBlob);
    return 1;
}

int asound_testCompareReplacementIntroMusic(void) {
    static const AsoundU8 *const legacyIntro[ASOUND_STREAM_COUNT] = {
        asound_intro_voice0, asound_intro_voice1, asound_intro_voice2,
        asound_intro_voice3, asound_intro_voice4, asound_intro_voice5
    };
    static const AsoundU8 *const legacyRelease[ASOUND_STREAM_COUNT] = {
        asound_release_voice0, asound_release_voice1, asound_release_voice2,
        asound_release_voice3, asound_release_voice4, asound_release_voice5
    };
    int voice;

    if (!asound_reload_replacement_music()) return 0;
    for (voice = 0; voice < ASOUND_STREAM_COUNT; voice++) {
        const AsoundU8 *introData = NULL, *releaseData = NULL;
        size_t introSize = 0, releaseSize = 0;
        size_t introLen = asound_intro_voice_length(voice);
        size_t releaseLen = asound_release_voice_length(voice);
        if (introLen == 0 || releaseLen == 0) return 0;
        if (!asound_replacement_music_stream(0, voice, &introData, &introSize)) return 0;
        if (!asound_replacement_music_stream(1, voice, &releaseData, &releaseSize)) return 0;
        if (introSize != introLen || releaseSize != releaseLen) return 0;
        if (memcmp(introData, legacyIntro[voice], introLen) != 0) return 0;
        if (memcmp(releaseData, legacyRelease[voice], releaseLen) != 0) return 0;
    }
    return 1;
}
#endif
