/*
 * asound_sf2.c - SoundFont backend: rsound_model sequencer + FluidSynth.
 *
 * Legal, ROM-free path for Roland-scored music: the authentic RSOUND
 * event stream (notes, bends, sweeps, program changes) is played through
 * any GM/GS SoundFont. MT-32 program numbers are remapped to the closest
 * GM instrument (the MT-32's 128 default timbres vs GM's layout). SysEx
 * timbre uploads cannot be reproduced by a SoundFont and are skipped, so
 * game custom patches (Jet Engine, Explosion...) degrade to their mapped
 * GM program - still far closer than silence, and the fallback order
 * auto: MT-32 > SF2 > OPL keeps real MT-32 preferred whenever ROMs exist.
 *
 * SoundFont discovery:
 *   1. $F15_SF2_PATH (a .sf2/.sf3 file)
 *   2. *.sf2/*.sf3 in <gamedir>/roms, <exedir>/roms, ~/.local/share/mt32-roms
 *   3. system fonts: /usr/share/sounds/sf2 (FluidR3_GM.sf2, default-GM.sf2,
 *      TimGM6mb.sf2), /usr/share/soundfonts
 *
 * Selection: F15_AUDIO_BACKEND=sf2|fluidsynth forces this backend; auto
 * uses it when MT-32 cannot open; opl|adlib|off skips it. Without the lib
 * compiled in (F15_HAVE_FLUIDSYNTH unset) everything compiles to a stub.
 */

#include "asound_sf2.h"

#if F15_HAVE_FLUIDSYNTH

#include <SDL3/SDL.h>
#include <fluidsynth.h>

#include <stdio.h>
#include <string.h>

#include "../log.h"
#include "../shared/common.h" /* getGamePath */

static fluid_settings_t *g_settings;
static fluid_synth_t *g_synth;
static int g_sf2Open;

/* MT-32 default timbre -> closest GM program. Built from the documented
 * MT-32 patch list; matches the ScummVM-style translation approach. */
static const uint8_t kMt32ToGm[128] = {
    /*  0- 7 pianos      */ 0, 0, 1, 4, 4, 5, 5, 3,
    /*  8-15 organs/acc  */ 16, 17, 17, 18, 19, 19, 20, 21,
    /* 16-23 harpsi/clav/celesta */ 6, 6, 6, 7, 7, 7, 8, 8,
    /* 24-31 syn brass/bass */ 62, 63, 62, 63, 38, 39, 38, 39,
    /* 32-39 fantasy..warm bell */ 88, 89, 52, 92, 97, 99, 89, 54,
    /* 40-47 echo bell..square */ 102, 96, 68, 102, 80, 54, 91, 80,
    /* 48-55 strings     */ 48, 49, 48, 45, 40, 40, 42, 42,
    /* 56-63 cbass..sitar*/ 43, 46, 46, 24, 25, 27, 28, 104,
    /* 64-71 basses      */ 32, 32, 33, 34, 36, 37, 35, 35,
    /* 72-79 woodwind    */ 73, 73, 72, 72, 74, 75, 65, 65,
    /* 80-87 reeds       */ 66, 67, 71, 71, 68, 69, 70, 22,
    /* 88-95 brass       */ 56, 56, 57, 57, 60, 60, 58, 61,
    /* 96-103 mallets    */ 61, 11, 11, 98, 112, 9, 14, 13,
    /* 104-111 world     */ 12, 107, 109, 77, 78, 78, 76, 121,
    /* 112-119 perc/fx   */ 47, 117, 118, 118, 118, 116, 115, 119,
    /* 120-127 fx        */ 115, 112, 55, 124, 123, 125, 98, 103,
};

/* ---- SoundFont discovery --------------------------------------------------- */

static int sf2_try_file(const char *path) {
    return fluid_synth_sfload(g_synth, path, 1) >= 0;
}

static int sf2_glob_dirs(void) {
    char dirs[8][1024];
    int ndirs = 0;
    const char *gdir = getGamePath();
    snprintf(dirs[ndirs], sizeof(dirs[0]), "%s/roms", gdir); ndirs++;
    const char *base = SDL_GetBasePath();
    if (base && ndirs < 8) {
        snprintf(dirs[ndirs], sizeof(dirs[0]), "%sroms", base); ndirs++;
    }
    const char *home = SDL_getenv("HOME");
    if (home && *home && ndirs < 8) {
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "%s/.local/share/mt32-roms", home); ndirs++;
    }
    for (int i = 0; i < ndirs; i++) {
        int count = 0;
        char **names = SDL_GlobDirectory(dirs[i], "*.sf*", 0, &count);
        if (!names) continue;
        for (int j = 0; j < count; j++) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dirs[i], names[j]);
            if (sf2_try_file(path)) {
                LogInfo(("sf2: loaded SoundFont %s", path));
                SDL_free(names);
                return 1;
            }
        }
        SDL_free(names);
    }
    return 0;
}

static int sf2_load_any(void) {
    static const char *const kSysFonts[] = {
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GS.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/default-GM.sf2",
    };
    const char *env = SDL_getenv("F15_SF2_PATH");
    if (env && *env) {
        if (sf2_try_file(env)) {
            LogInfo(("sf2: loaded SoundFont %s", env));
            return 1;
        }
        LogWarn(("sf2: F15_SF2_PATH=%s failed to load", env));
    }
    if (sf2_glob_dirs()) return 1;
    for (size_t i = 0; i < sizeof(kSysFonts) / sizeof(kSysFonts[0]); i++) {
        if (sf2_try_file(kSysFonts[i])) {
            LogInfo(("sf2: loaded SoundFont %s", kSysFonts[i]));
            return 1;
        }
    }
    return 0;
}

/* ---- public API ------------------------------------------------------------ */

int asound_sf2_open(unsigned rate) {
    if (g_sf2Open) return 1;
    g_settings = new_fluid_settings();
    if (!g_settings) return 0;
    fluid_settings_setnum(g_settings, "synth.sample-rate", (double)rate);
    g_synth = new_fluid_synth(g_settings);
    if (!g_synth) {
        delete_fluid_settings(g_settings);
        g_settings = NULL;
        return 0;
    }
    if (!sf2_load_any()) {
        LogWarn(("sf2: no SoundFont found; set F15_SF2_PATH"));
        delete_fluid_synth(g_synth);
        delete_fluid_settings(g_settings);
        g_synth = NULL;
        g_settings = NULL;
        return 0;
    }
    g_sf2Open = 1;
    LogInfo(("sf2: FluidSynth backend active at %u Hz", rate));
    return 1;
}

void asound_sf2_close(void) {
    if (g_synth) delete_fluid_synth(g_synth);
    if (g_settings) delete_fluid_settings(g_settings);
    g_synth = NULL;
    g_settings = NULL;
    g_sf2Open = 0;
}

int asound_sf2_is_open(void) { return g_sf2Open; }

void asound_sf2_midi(void *user, uint32_t msg) {
    (void)user;
    if (!g_sf2Open) return;
    int ch = msg & 0x0f;
    int d1 = (msg >> 8) & 0x7f, d2 = (msg >> 16) & 0x7f;
    switch (msg & 0xf0) {
    case 0x80: fluid_synth_noteoff(g_synth, ch, d1); break;
    case 0x90:
        if (d2) fluid_synth_noteon(g_synth, ch, d1, d2);
        else    fluid_synth_noteoff(g_synth, ch, d1);
        break;
    case 0xb0: fluid_synth_cc(g_synth, ch, d1, d2); break;
    case 0xc0:
        fluid_synth_program_change(g_synth, ch, kMt32ToGm[d1 & 0x7f]);
        break;
    case 0xe0: fluid_synth_pitch_bend(g_synth, ch, d1 | (d2 << 7)); break;
    default: break;
    }
}

void asound_sf2_sysex(void *user, const uint8_t *frame, size_t len) {
    /* MT-32 timbre uploads can't map onto a SoundFont - ignore them; the
     * program changes that follow still land on the GM-mapped presets. */
    (void)user; (void)frame; (void)len;
}

void asound_sf2_render(int16_t *buf, int frames) {
    if (!g_sf2Open || frames <= 0) {
        if (buf && frames > 0) memset(buf, 0, (size_t)frames * 4);
        return;
    }
    fluid_synth_write_s16(g_synth, frames, buf, 0, 2, buf, 1, 2);
}

#else /* !F15_HAVE_FLUIDSYNTH */

int asound_sf2_open(unsigned rate) { (void)rate; return 0; }
void asound_sf2_close(void) {}
int asound_sf2_is_open(void) { return 0; }
void asound_sf2_midi(void *user, uint32_t msg) { (void)user; (void)msg; }
void asound_sf2_sysex(void *user, const uint8_t *frame, size_t len) {
    (void)user; (void)frame; (void)len;
}
void asound_sf2_render(int16_t *buf, int frames) {
    if (buf && frames > 0) memset(buf, 0, (size_t)frames * 4);
}

#endif
