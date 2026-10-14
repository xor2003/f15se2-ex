/*
 * png_asset.c - Decode self-contained PNGs into legacy 8-bit surfaces.
 */

#include "png_asset.h"

#include "asset_path.h"
#include "common.h"
#include "../gfx.h"
#include "../gfx_impl.h"
#include "../log.h"
#include "../inttype.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

enum { INDEX8_COLOR_COUNT = 256, RGBA_PIXEL_BYTES = 4 };


/* Scale indexed replacement pixels into legacy logical dimensions without changing indices. */
static bool copyScaledIndices(SDL_Surface *source, SDL_Surface *destination) {
    if (source->w <= 0 || source->h <= 0
        || destination->w <= 0 || destination->h <= 0
        || destination->format != SDL_PIXELFORMAT_INDEX8) {
        return false;
    }

    const bool lockSource = SDL_MUSTLOCK(source);
    const bool lockDestination = SDL_MUSTLOCK(destination);
    if (lockSource && !SDL_LockSurface(source)) return false;
    if (lockDestination && !SDL_LockSurface(destination)) {
        if (lockSource) SDL_UnlockSurface(source);
        return false;
    }

    for (int y = 0; y < destination->h; ++y) {
        const int sourceY = (int)(((int64)y * source->h) / destination->h);
        const uint8 *sourceRow =
            (const uint8 *)source->pixels + (size_t)sourceY * source->pitch;
        uint8 *destinationRow =
            (uint8 *)destination->pixels + (size_t)y * destination->pitch;
        for (int x = 0; x < destination->w; ++x) {
            const int sourceX = (int)(((int64)x * source->w) / destination->w);
            destinationRow[x] = sourceRow[sourceX];
        }
    }

    if (lockDestination) SDL_UnlockSurface(destination);
    if (lockSource) SDL_UnlockSurface(source);
    return true;
}

/* Map an RGB replacement color to the nearest entry in the active game palette. */
static uint8 nearestPaletteIndex(const SDL_Palette *palette, uint8 r, uint8 g, uint8 b) {
    int bestIndex = 0;
    unsigned int bestDistance = ~0U;
    for (int i = 0; i < palette->ncolors && i < INDEX8_COLOR_COUNT; ++i) {
        const int dr = (int)r - palette->colors[i].r;
        const int dg = (int)g - palette->colors[i].g;
        const int db = (int)b - palette->colors[i].b;
        const unsigned int distance = (unsigned int)(dr * dr + dg * dg + db * db);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if (!distance) break;
        }
    }
    return (uint8)bestIndex;
}

/* Scale truecolor replacement pixels and quantize them into the active game palette. */
static bool copyScaledTruecolor(SDL_Surface *source, SDL_Surface *destination) {
    SDL_Palette *palette = gfx_getPalette();
    SDL_Surface *rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    if (destination->format != SDL_PIXELFORMAT_INDEX8
        || !palette || palette->ncolors <= 0 || !rgba) {
        if (rgba) SDL_DestroySurface(rgba);
        return false;
    }

    const bool lockSource = SDL_MUSTLOCK(rgba);
    const bool lockDestination = SDL_MUSTLOCK(destination);
    if (lockSource && !SDL_LockSurface(rgba)) {
        SDL_DestroySurface(rgba);
        return false;
    }
    if (lockDestination && !SDL_LockSurface(destination)) {
        if (lockSource) SDL_UnlockSurface(rgba);
        SDL_DestroySurface(rgba);
        return false;
    }

    for (int y = 0; y < destination->h; ++y) {
        const int sourceY = (int)(((int64)y * rgba->h) / destination->h);
        const uint8 *sourceRow =
            (const uint8 *)rgba->pixels + (size_t)sourceY * rgba->pitch;
        uint8 *destinationRow =
            (uint8 *)destination->pixels + (size_t)y * destination->pitch;
        for (int x = 0; x < destination->w; ++x) {
            const int sourceX = (int)(((int64)x * rgba->w) / destination->w);
            const uint8 *pixel = sourceRow + sourceX * RGBA_PIXEL_BYTES;
            destinationRow[x] =
                nearestPaletteIndex(palette, pixel[0], pixel[1], pixel[2]);
        }
    }

    if (lockDestination) SDL_UnlockSurface(destination);
    if (lockSource) SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    return true;
}

/* Load a PNG replacement, preserving indexed pixels when possible and scaling
 * to logical dimensions. The lookup goes through findReplacementAssetPath so
 * per-game filename aliases and all replacement roots (F15_REPLACEMENT_ROOT,
 * its converted_assets_all subtree, game-dir packs) resolve like every other
 * modern asset. */
static int loadReplacementPngResolved(const char *legacyFilename,
                                      SDL_Surface *destination,
                                      char *replacementPath, size_t pathSize,
                                      int pageBackground) {
    if (!destination) return 0;
    if (!findReplacementAssetPath(legacyFilename, ".png", replacementPath, pathSize)) {
        return 0;
    }

    SDL_Surface *source = SDL_LoadPNG(replacementPath);
    if (!source) {
        LogWarn(("asset replacement: cannot load %s (%s); using legacy image",
                 replacementPath, SDL_GetError()));
        return 0;
    }

    SDL_Palette *palette = SDL_GetSurfacePalette(source);
    int loaded{};
    if (source->format == SDL_PIXELFORMAT_INDEX8) {
        if (!palette || palette->ncolors <= 0) {
            LogWarn(("asset replacement: indexed PNG %s has no embedded palette; "
                     "using legacy image", replacementPath));
            SDL_DestroySurface(source);
            return 0;
        }
        /* An indexed replacement means no truecolor page background. */
        if (pageBackground) gfx_setPageReplacementSurface(NULL);
        loaded = copyScaledIndices(source, destination);
        /*
         * PIC/SPR data contains palette indices only. The original callers
         * select the active DAC separately, so the PNG's embedded palette is
         * editor metadata and must not recolor the running game.
         */
    } else {
        /* The legacy page remains indexed. Quantize true-color replacements
         * against its current DAC palette; a future native-color renderer can
         * retain the source surface without changing this compatibility path. */
        loaded = copyScaledTruecolor(source, destination);
        if (loaded && pageBackground) {
            /* Legacy cockpit save-unders still operate on the indexed page.
             * Populate it without replacing the VGA palette, while retaining
             * the source-resolution truecolor image for presentation. */
            SDL_Surface *pageTruecolor =
                SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
            if (pageTruecolor) {
                gfx_setPageReplacementSurface(pageTruecolor);
                gfx_setPageReplacementIndexedBase(destination);
            } else {
                loaded = 0;
            }
        }
    }
    if (loaded) {
        LogInfo(("asset replacement: loaded %s for %s (%dx%d)",
                 replacementPath, legacyFilename, source->w, source->h));
    } else {
        LogWarn(("asset replacement: cannot copy %s into the indexed target; "
                 "using legacy image", replacementPath));
    }
    SDL_DestroySurface(source);
    return loaded;
}

int loadReplacementPng(const char *legacyFilename, SDL_Surface *destination) {
    char replacementPath[1024]{};
    return loadReplacementPngResolved(legacyFilename, destination,
                                      replacementPath, sizeof(replacementPath), 0);
}

/* Load a replacement PNG directly into a legacy full-page image buffer. */
int loadReplacementPngToPage(const char *legacyFilename, int page) {
    (void)page; /* The port uses one page backbuffer. */
    char replacementPath[1024]{};
    const int loaded = loadReplacementPngResolved(legacyFilename, gfx_getCurPageSurface(),
                                                  replacementPath, sizeof(replacementPath), 1);
    /* Match showPicFile: replacing a page also replaces its retained text.
     * Failed replacement attempts must leave the current screen untouched. */
    if (loaded) gfx_clearTtfTextOverlay();
    return loaded;
}

/* Load a replacement PNG into a sprite buffer while preserving the sprite header contract. */
int loadReplacementPngToSprite(const char *legacyFilename, int segment) {
    SDL_Surface *destination = gfx_getSpriteSurface(segment);
    char replacementPath[1024]{};
    const int loaded = loadReplacementPngResolved(legacyFilename, destination,
                                                  replacementPath, sizeof(replacementPath), 0);
    if (loaded) gfx_setSpriteReplacementPng(segment, replacementPath);
    return loaded;
}

/* Load and scale a replacement for the split-field high-resolution title image. */
int loadReplacementPngToHiResTitle(const char *legacyFilename) {
    char replacementPath[1024]{};
    const int loaded = loadReplacementPngResolved(legacyFilename, gfx_getHiResSurface(),
                                                  replacementPath, sizeof(replacementPath), 0);
    if (loaded) gfx_presentHiRes();
    else gfx_setHiResReplacementSurface(NULL);
    return loaded;
}
