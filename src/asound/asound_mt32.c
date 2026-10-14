/*
 * asound_mt32.c - Roland MT-32 backend: rsound_model sequencer + libmt32emu.
 *
 * Replaces RSOUND.EXE's MPU-401 UART writes with direct mt32emu message calls.
 * The original driver's embedded timbre dump (the "F19 Stealth Fighter"/
 * "F15 Strike Eagle II" SysEx stream inside RSOUND.EXE) is replayed verbatim
 * through mt32emu_play_sysex_now at setup, so the custom patches match the
 * original hardware configuration exactly.
 *
 * MT-32 needs copyrighted ROMs (MT32_CONTROL.ROM + MT32_PCM.ROM, or the
 * CM-32L set) which we do not ship. Discovery order:
 *   1. $F15_MT32_ROM_DIR (a directory)
 *   2. <gamedir>/roms, <gamedir>/mt32
 *   3. <exedir>/roms, <exedir>/mt32 (the install/bundle dir)
 *   4. ./roms, ./mt32
 *   5. $XDG_DATA_HOME/mt32-roms, ~/.local/share/mt32-roms,
 *      ~/.config/retroarch/system, ~/.local/share/retroarch/system,
 *      /usr/share/mt32-rom-data, /usr/local/share/mt32-rom-data
 * Each candidate dir is first probed for the usual file names; if the synth
 * still cannot open, every plausible-size file in the dirs is offered to
 * mt32emu_add_rom_file, which validates ROM images by content hash - so
 * MAME/RetroArch names, interleaved dumps and LAPC-I images all work.
 *
 * Runtime selection: F15_AUDIO_BACKEND=mt32|opl|auto (default auto = MT-32
 * when ROMs + lib are present, else the AdLib/OPL path). Without the lib
 * compiled in (F15_HAVE_MT32EMU unset) everything compiles to a stub.
 */

#include "asound_mt32.h"

#if F15_HAVE_MT32EMU

#include <SDL3/SDL.h>
/* The flat C interface: real symbols in libmt32emu regardless of the build's
 * MT32EMU_EXPORTS_TYPE, and immune to the mt32emu.h API_TYPE macro rewrite. */
#include <mt32emu/c_interface/c_interface.h>

#include <stdio.h>
#include <string.h>

#include "../log.h"
#include "../shared/common.h" /* getGamePath */

static mt32emu_context g_ctx;
static int g_mt32Open;

/* ---- ROM discovery --------------------------------------------------------- */

static const char *const kRomNames[] = {
    "MT32_CONTROL.ROM", "MT32_PCM.ROM",
    "CM32L_CONTROL.ROM", "CM32L_PCM.ROM",
    "mt32_control.rom", "mt32_pcm.rom",
    "cm32l_control.rom", "cm32l_pcm.rom",
};

static int mt32_try_dir(const char *dir) {
    int added = 0;
    char path[1024];
    for (size_t i = 0; i < sizeof(kRomNames) / sizeof(kRomNames[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, kRomNames[i]);
        SDL_IOStream *io = SDL_IOFromFile(path, "rb");
        if (!io) continue;
        SDL_CloseIO(io);
        if (mt32emu_add_rom_file(g_ctx, path) == MT32EMU_RC_OK) {
            LogInfo(("mt32: added ROM %s", path));
            added++;
        }
    }
    return added;
}

/* Offer every plausible-size file in the dir to mt32emu: it validates ROM
 * images by content hash, so renamed/interleaved/MAME-style dumps work. */
static int mt32_scan_dir(const char *dir) {
    int count = 0, added = 0;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &count);
    if (!names) return 0;
    for (int i = 0; i < count; i++) {
        char path[1024];
        SDL_PathInfo pi;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (!SDL_GetPathInfo(path, &pi) || pi.type != SDL_PATHTYPE_FILE ||
            pi.size < 8192 || pi.size > 4 * 1024 * 1024) {
            continue;
        }
        if (mt32emu_add_rom_file(g_ctx, path) == MT32EMU_RC_OK) {
            LogInfo(("mt32: added ROM %s", path));
            added++;
        }
    }
    SDL_free(names);
    return added;
}

static char g_romDirs[16][1024];
static int g_nRomDirs;

static void mt32_collect_dirs(void) {
    char (*dirs)[1024] = g_romDirs;
    int ndirs = 0;
    const char *env = SDL_getenv("F15_MT32_ROM_DIR");
    if (env && *env) {
        snprintf(dirs[ndirs], sizeof(dirs[0]), "%s", env);
        ndirs++;
    }
    const char *gdir = getGamePath();
    snprintf(dirs[ndirs], sizeof(dirs[0]), "%s/roms", gdir); ndirs++;
    snprintf(dirs[ndirs], sizeof(dirs[0]), "%s/mt32", gdir); ndirs++;
    const char *base = SDL_GetBasePath();
    if (base && ndirs + 2 <= 16) {
        snprintf(dirs[ndirs], sizeof(dirs[0]), "%sroms", base); ndirs++;
        snprintf(dirs[ndirs], sizeof(dirs[0]), "%smt32", base); ndirs++;
    }
    snprintf(dirs[ndirs], sizeof(dirs[0]), "roms"); ndirs++;
    snprintf(dirs[ndirs], sizeof(dirs[0]), "mt32"); ndirs++;
    const char *xdg = SDL_getenv("XDG_DATA_HOME");
    if (xdg && *xdg && ndirs < 16) {
        snprintf(dirs[ndirs], sizeof(dirs[0]), "%s/mt32-roms", xdg); ndirs++;
    }
    const char *home = SDL_getenv("HOME");
    if (home && *home && ndirs + 3 <= 16) {
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "%s/.local/share/mt32-roms", home); ndirs++;
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "%s/.config/retroarch/system", home); ndirs++;
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "%s/.local/share/retroarch/system", home); ndirs++;
    }
    if (ndirs + 2 <= 16) {
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "/usr/share/mt32-rom-data"); ndirs++;
        snprintf(dirs[ndirs], sizeof(dirs[0]),
                 "/usr/local/share/mt32-rom-data"); ndirs++;
    }
    g_nRomDirs = ndirs;
}

/* Fast path: only the conventional file names. */
static void mt32_load_named(void) {
    for (int i = 0; i < g_nRomDirs; i++) {
        mt32_try_dir(g_romDirs[i]); /* more dirs may fill a missing half */
    }
}

/* Slow path: content-hash every plausible file in the same dirs. */
static void mt32_scan_dirs(void) {
    for (int i = 0; i < g_nRomDirs; i++) {
        mt32_scan_dir(g_romDirs[i]);
    }
}

/* ---- public API ------------------------------------------------------------ */

int asound_mt32_open(unsigned rate) {
    mt32emu_report_handler_i rh;
    if (g_mt32Open) return 1;
    if (g_ctx) { mt32emu_free_context(g_ctx); g_ctx = NULL; }
    memset(&rh, 0, sizeof(rh)); /* no report callbacks */
    g_ctx = mt32emu_create_context(rh, NULL);
    if (!g_ctx) return 0;
    mt32_collect_dirs();
    mt32_load_named();
    mt32emu_set_stereo_output_samplerate(g_ctx, (double)rate);
    mt32emu_set_analog_output_mode(g_ctx, MT32EMU_AOM_ACCURATE);
    if (mt32emu_open_synth(g_ctx) != MT32EMU_RC_OK) {
        mt32_scan_dirs(); /* second chance: any file, identified by content */
    }
    if (mt32emu_open_synth(g_ctx) != MT32EMU_RC_OK) {
        LogWarn(("mt32: open_synth failed (control/pcm ROMs not found?); "
                 "set F15_MT32_ROM_DIR"));
        mt32emu_free_context(g_ctx);
        g_ctx = NULL;
        return 0;
    }
    g_mt32Open = 1;
    LogInfo(("mt32: synth open at %u Hz", rate));
    return 1;
}

void asound_mt32_close(void) {
    if (g_ctx) {
        if (g_mt32Open) mt32emu_close_synth(g_ctx);
        mt32emu_free_context(g_ctx);
    }
    g_ctx = NULL;
    g_mt32Open = 0;
}

int asound_mt32_is_open(void) { return g_mt32Open; }

void asound_mt32_midi(void *user, uint32_t msg) {
    (void)user;
    if (g_mt32Open) mt32emu_play_msg_now(g_ctx, msg);
}

void asound_mt32_sysex(void *user, const uint8_t *frame, size_t len) {
    (void)user;
    if (g_mt32Open) {
        mt32emu_play_sysex_now(g_ctx, frame, (mt32emu_bit32u)len);
    }
}

void asound_mt32_render(int16_t *buf, int frames) {
    if (!g_mt32Open || frames <= 0) {
        if (buf && frames > 0) memset(buf, 0, (size_t)frames * 4);
        return;
    }
    mt32emu_render_bit16s(g_ctx, (mt32emu_bit16s *)buf, (mt32emu_bit32u)frames);
}

#else /* !F15_HAVE_MT32EMU */

int asound_mt32_open(unsigned rate) { (void)rate; return 0; }
void asound_mt32_close(void) {}
int asound_mt32_is_open(void) { return 0; }
void asound_mt32_midi(void *user, uint32_t msg) { (void)user; (void)msg; }
void asound_mt32_sysex(void *user, const uint8_t *frame, size_t len) {
    (void)user; (void)frame; (void)len;
}
void asound_mt32_render(int16_t *buf, int frames) {
    if (buf && frames > 0) memset(buf, 0, (size_t)frames * 4);
}

#endif
