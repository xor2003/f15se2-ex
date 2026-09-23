/*
 * debugframe.c - env-gated PPM frame dumps (software + GL backends).
 *
 * Written for netcode/visual verification runs: point F15_DUMP_FRAME at a
 * path, optionally F15_DUMP_AT / F15_DUMP_EVERY, and the renderer writes P6
 * PPMs of what the player actually sees. Both backends share the trigger
 * logic in dumpTrigger() below; only the pixel source differs.
 *
 * See debugframe.h for the env contract.
 */
#include "egtypes.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debugframe.h"
#include "egdata.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "r3d_gl.h"

#define DUMP_DEFAULT_AT 120 /* ~8s at the 15Hz tick: past join/loading screens */

/* One trigger state shared by both backends: whichever renderer is active
 * calls it once per presented frame, so a given tick dumps exactly once. */
static int s_done, s_seq, s_lastTick = -1;

/* Returns the resolved output path when this frame should be dumped, else
 * NULL. With F15_DUMP_EVERY the path gains a _NNNNN sequence suffix before
 * the .ppm extension. */
static const char *dumpTrigger(void) {
    static char seqPath[1024];
    const char *path = SDL_getenv("F15_DUMP_FRAME");
    const char *at;
    int every;
    if (!path || !*path)
        return NULL;
    at = SDL_getenv("F15_DUMP_AT");
    if (frameTick < (at ? atoi(at) : DUMP_DEFAULT_AT))
        return NULL;
    at = SDL_getenv("F15_DUMP_EVERY");
    every = at ? atoi(at) : 0;
    if (s_done && (every <= 0 || frameTick - s_lastTick < every))
        return NULL;
    s_done = 1;
    s_lastTick = frameTick;
    if (every > 0) {
        size_t plen = strlen(path);
        if (plen > 4 && !strcmp(path + plen - 4, ".ppm"))
            plen -= 4;
        snprintf(seqPath, sizeof(seqPath), "%.*s_%05d.ppm",
                 (int)plen, path, s_seq++);
        path = seqPath;
    }
    return path;
}

/* Software backend: the front page surface holds the composited 8-bit frame;
 * expand each pixel through the DAC palette. Under GL the page only carries
 * cockpit chrome (world + HUD draw straight to GL), so skip there. */
void debugDumpFrame(void) {
    const char *path;
    struct SDL_Surface *surf;
    int x, y;
    FILE *f;
    if (r3dgl_active())
        return;
    if (!(path = dumpTrigger()))
        return;
    surf = gfx_getPageSurface(g_pageFront[0]);
    f = surf ? fopen(path, "wb") : 0;
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", surf->w, surf->h);
    for (y = 0; y < surf->h; y++)
        for (x = 0; x < surf->w; x++) {
            uint8_t r, g, b;
            gfx_paletteRGB(((uint8_t *)surf->pixels)[y * surf->pitch + x],
                           &r, &g, &b);
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        }
    fclose(f);
    fprintf(stderr, "f15: dumped %dx%d frame to %s\n", surf->w, surf->h, path);
}

/* GL backend: called from r3dgl_present() just before SwapWindow - the back
 * buffer still holds the just-drawn frame (post-swap contents are undefined).
 * glReadPixels returns rows bottom-up; the PPM is written top-down. */
void debugDumpFrameGL(void) {
    static uint8 *rowbuf;
    const char *path;
    int w = 0, h = 0, y;
    FILE *f;
    /* env pre-check, then size: a transient size failure must not consume
     * this tick's trigger */
    if (!(path = SDL_getenv("F15_DUMP_FRAME")) || !*path)
        return;
    if (!SDL_GetWindowSizeInPixels(r3dgl_window(), &w, &h) || w <= 0 || h <= 0)
        return;
    if (!(path = dumpTrigger()))
        return;
    f = fopen(path, "wb");
    if (!f)
        return;
    rowbuf = (uint8 *)realloc(rowbuf, (size_t)w * 3);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    glReadBuffer(GL_BACK);
    for (y = h - 1; y >= 0; y--) {
        glReadPixels(0, y, w, 1, GL_RGB, GL_UNSIGNED_BYTE, rowbuf);
        fwrite(rowbuf, 1, (size_t)w * 3, f);
    }
    fclose(f);
    fprintf(stderr, "f15: dumped %dx%d GL frame to %s\n", w, h, path);
}
