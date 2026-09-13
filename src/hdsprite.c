#include "hdsprite.h"
#include "r2d.h"
#include "log.h"
#include "inttype.h"
#include "shared/common.h"
#include <SDL3/SDL.h>

/* The radar ownship's on-scope footprint (320-space), matching the original 7x7
 * gauge icon (blitGaugeSprite). Raise for a larger ownship marker. */
#define RADAR_OWNSHIP_FOOTPRINT 7

/* Load a PNG as an owned RGBA image, or NULL (missing/failed). GPU-gated by callers;
 * a missing asset is normal (the game ships with only the sprites drawn so far). */
static R2DImage *loadHdPng(const char *path) {
    char resolvedPath[1024];
    const int bundledAsset = SDL_strncmp(path, "assets/", 7) == 0;
    SDL_Surface *raw;
    SDL_Surface *rgba;
    if (bundledAsset && findReplacementAssetPath(path + 7, ".png",
                                                resolvedPath, sizeof(resolvedPath))) {
        path = resolvedPath;
    }
    raw = SDL_LoadPNG(path);
    if (!raw && bundledAsset && path != resolvedPath) {
        const char *basePath = SDL_GetBasePath();
        if (basePath) {
            const int length = SDL_snprintf(resolvedPath, sizeof(resolvedPath), "%s%s", basePath, path);
            if (length > 0 && (size_t)length < sizeof(resolvedPath)) {
                path = resolvedPath;
                raw = SDL_LoadPNG(path);
            }
        }
    }
    if (!raw) {
        LogInfo(("hdsprite: %s not loaded (%s); using legacy art", path, SDL_GetError()));
        return NULL;
    }
    rgba = (raw->format == SDL_PIXELFORMAT_RGBA32)
               ? raw
               : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    if (rgba != raw) SDL_DestroySurface(raw);
    if (!rgba) return NULL;
    LogInfo(("hdsprite: loaded %s (%dx%d)", path, rgba->w, rgba->h));
    R2DImage *image = r2d_imageFromSurface(rgba);
    if (!image) SDL_DestroySurface(rgba);
    return image;
}

static R2DImage *loadHdReplacementOrPng(const char *legacyName, const char *fallbackPath) {
    char replacementPath[512];

    /* Campaign/scenario PNG replacements are the editable source for high-res
     * maps. Built-in assets/ paths remain a fallback for repo-provided HD art. */
    if (legacyName && findReplacementAssetPath(legacyName, ".png", replacementPath, sizeof(replacementPath))) {
        return loadHdPng(replacementPath);
    }
    return loadHdPng(fallbackPath);
}

/* Lazily loaded once; NULL means "no HD asset, use legacy". */
static R2DImage *radarOwnship(void) {
    static R2DImage *img;
    static int tried;
    if (tried) return img;
    tried = 1;
    if (!r2d_hasNativeOverlay()) return NULL; /* software: HD is GPU-only */
    img = loadHdPng("assets/flight/radar/self.png");
    return img;
}

/* Debrief theatre map: one HD PNG per theatre (parallel to endata's
 * theaterSprFiles). A missing file leaves the slot NULL → legacy SPR. */
static const char *const debriefMapPaths[8] = {
    "assets/end/map/lb.png", /* Libya */
    "assets/end/map/pg.png", /* Persian Gulf */
    "assets/end/map/vn.png", /* Vietnam */
    "assets/end/map/me.png", /* Middle East */
    "assets/end/map/nc.png", /* North Cape */
    "assets/end/map/ce.png", /* Central Europe */
    "assets/end/map/jp.png", /* Japan */
    "assets/end/map/na.png", /* North Africa */
};

static const char *const debriefMapLegacyNames[8] = {
    "libya.spr",
    "persian.spr",
    "vn.spr",
    "me.spr",
    "ncape.spr",
    "ceurope.spr",
    "jp.spr",
    "na.spr",
};

static R2DImage *debriefMaps[8];
static int debriefMapsTried[8];

static R2DImage *debriefMap(int theatre) {
    if (theatre < 0 || theatre >= 8 || !r2d_hasNativeOverlay()) return NULL;
    if (!debriefMapsTried[theatre]) {
        debriefMapsTried[theatre] = 1;
        debriefMaps[theatre] = loadHdReplacementOrPng(debriefMapLegacyNames[theatre],
                                                       debriefMapPaths[theatre]);
    }
    return debriefMaps[theatre];
}

int hdsprite_drawDebriefTheatreMap(int theatre) {
    R2DImage *hd;
    SDL_Surface *s;
    int sourceWidth;
    int sourceHeight;

    if (theatre < 0 || theatre >= 8 || !r2d_vectorActive()) return 0;
    hd = debriefMap(theatre);
    if (!hd) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;

    sourceWidth = s->w;
    sourceHeight = s->h;
    if ((long)s->w * 200 == (long)s->h * 320) {
        /* Legacy theatre images are 320x200 sprite sheets.  The geographic
         * map occupies the upper-left 224x168; the rest stores map markers. */
        sourceWidth = s->w * 224 / 320;
        sourceHeight = s->h * 168 / 200;
    }

    /* Scale the whole HD image into the legacy map footprint (spriteMapAreaDef:
     * origin 8,10; 224×168 in 320-space) so it shares the overlay's 4:3-corrected
     * coordinate space — the flight-path lines and event markers are plotted in
     * that same space and must line up with the terrain. */
    r2d_submitImageScaled(hd, 0, 0, sourceWidth, sourceHeight, 8, 10, 224, 168,
                         R2D_IMAGE_ATLAS_OPAQUE);
    return 1;
}

int hdsprite_drawTacticalMapBackground(int theatre, int centerX, int centerY, int zoomLevel) {
    R2DImage *hd;
    SDL_Surface *surface;
    int sourceWidth;
    int sourceHeight;
    int cropWidth;
    int cropHeight;
    int cropX;
    int cropY;
    int coordinateShift;

    if (!r2d_vectorActive()) return 0;
    hd = debriefMap(theatre);
    if (!hd) return 0;
    surface = r2d_imageSurface(hd);
    if (!surface) return 0;

    sourceWidth = surface->w;
    sourceHeight = surface->h;
    if ((long)surface->w * 200 == (long)surface->h * 320) {
        sourceWidth = surface->w * 224 / 320;
        sourceHeight = surface->h * 168 / 200;
    }
    if (sourceWidth <= 0 || sourceHeight <= 0 || zoomLevel < 0 || zoomLevel > 10) return 0;
    coordinateShift = 10 - zoomLevel;
    cropWidth = (int)((int64)sourceWidth * (72L << coordinateShift) / 0x8000L);
    cropHeight = (int)((int64)sourceHeight * (75L << coordinateShift) / 0x8000L);
    if (cropWidth < 1) cropWidth = 1;
    if (cropHeight < 1) cropHeight = 1;
    if (cropWidth > sourceWidth) cropWidth = sourceWidth;
    if (cropHeight > sourceHeight) cropHeight = sourceHeight;
    cropX = (int)((int64)sourceWidth * centerX / 0x8000L) - cropWidth / 2;
    cropY = (int)((int64)sourceHeight * centerY / 0x8000L) - cropHeight / 2;
    if (cropX < 0) cropX = 0;
    if (cropY < 0) cropY = 0;
    if (cropX + cropWidth > sourceWidth) cropX = sourceWidth - cropWidth;
    if (cropY + cropHeight > sourceHeight) cropY = sourceHeight - cropHeight;
    r2d_submitImageScaled(hd, cropX, cropY, cropWidth, cropHeight,
                         24, 112, 72, 56, R2D_IMAGE_ATLAS_OPAQUE);
    return 1;
}

/* Pre-mission briefing (START mission-select) HD art: a window-filling widescreen
 * room/officer backdrop plus 7 pointer-arm poses. The arm poses are authored as
 * full-frame cels the same size as the wall (transparent except the forearm), so
 * they register over the wall with no per-frame placement — see hdsprite.h. */
#define BRIEFING_ARM_FRAMES 7

static R2DImage *briefingPersonRoom;
static R2DImage *briefingPeople[BRIEFING_ARM_FRAMES];

/* Load one complete set from one directory; never combine poses from different
 * packs or show a bodyless room when only some poses are available. */
static int briefingPeopleAvailable(void) {
    static int tried = 0;
    char roomPath[512] = "assets/start/menu/person/room.png";
    char directory[512] = {0};
    char *separator = NULL;
    SDL_Surface *roomSurface = NULL;
    int frame = 0;

    if (!r2d_hasNativeOverlay()) return 0;
    if (tried) return briefingPersonRoom != NULL;
    tried = 1;
    if (!findReplacementAssetPath("start/menu/person/room", ".png",
                                  directory, sizeof(directory))) {
        SDL_strlcpy(directory, roomPath, sizeof(directory));
    }
    briefingPersonRoom = loadHdPng(directory);
    if (!briefingPersonRoom) return 0;
    roomSurface = r2d_imageSurface(briefingPersonRoom);
    if (!roomSurface) goto incomplete;
    separator = SDL_strrchr(directory, '/');
    {
        char *backslash = SDL_strrchr(directory, '\\');
        if (backslash && (!separator || backslash > separator)) separator = backslash;
    }
    if (separator) *separator = '\0';
    else SDL_strlcpy(directory, ".", sizeof(directory));

    for (frame = 0; frame < BRIEFING_ARM_FRAMES; frame++) {
        char path[512] = {0};
        SDL_Surface *poseSurface = NULL;
        int length = SDL_snprintf(path, sizeof(path), "%s/%d.png", directory, frame);
        if (length < 0 || (size_t)length >= sizeof(path)) goto incomplete;
        briefingPeople[frame] = loadHdPng(path);
        poseSurface = r2d_imageSurface(briefingPeople[frame]);
        if (!poseSurface || poseSurface->w != roomSurface->w ||
            poseSurface->h != roomSurface->h) goto incomplete;
    }
    return 1;

incomplete:
    LogWarn(("hdsprite: complete-person briefing needs room.png and 0..6.png "
             "with matching dimensions; using legacy body and arms"));
    for (frame = 0; frame < BRIEFING_ARM_FRAMES; frame++) {
        r2d_releaseImage(briefingPeople[frame]);
        briefingPeople[frame] = NULL;
    }
    r2d_releaseImage(briefingPersonRoom);
    briefingPersonRoom = NULL;
    return 0;
}

static R2DImage *briefingWall(void) {
    static R2DImage *img;
    static int tried;
    if (tried) return img;
    tried = 1;
    if (!r2d_hasNativeOverlay()) return NULL;
    img = loadHdReplacementOrPng("WALL.PIC", "assets/start/menu/wall.png");
    return img;
}

int hdsprite_hasBriefingWall(void) {
    return briefingPeopleAvailable() || briefingWall() != NULL;
}

/* 320-space x for the LEFT edge of the arm cels within the 4:3 menu box. The legacy
 * arm sits at [60,174]; the HD cel, scaled to the room height, is wider (~173 units
 * at the reference window), so left-aligning to the box (0) lands the pointer near the
 * legacy 174 with the extra width tucked behind the officer. Raise toward 60 to match
 * the legacy left edge instead (pointer then overshoots to the right). */
#define BRIEFING_ARM_BOX_LEFT_X 0.0f

void hdsprite_drawBriefingWall(void) {
    R2DImage *w = briefingPeopleAvailable() ? briefingPersonRoom : briefingWall();
    if (w && r2d_vectorActive()) r2d_submitImageWindow(w); /* room centred */
}

/* Whole-person poses share the room transform, including widescreen placement. */
int hdsprite_drawBriefingPerson(int frame) {
    if (!r2d_vectorActive() || !briefingPeopleAvailable()) return 0;
    /* Keep the resting person visible between menu animation states. */
    if (frame < 0 || frame >= BRIEFING_ARM_FRAMES) frame = BRIEFING_ARM_FRAMES - 1;
    r2d_submitImageWindow(briefingPeople[frame]);
    return 1;
}

void hdsprite_drawBriefingArm(int frame) {
    static R2DImage *img[BRIEFING_ARM_FRAMES];
    static int tried[BRIEFING_ARM_FRAMES];
    R2DImage *a;
    if (frame < 0 || frame >= BRIEFING_ARM_FRAMES) return;
    if (!tried[frame]) {
        tried[frame] = 1;
        if (r2d_hasNativeOverlay()) {
            char legacyPath[48];
            char path[48];
            SDL_snprintf(legacyPath, sizeof(legacyPath), "start/menu/arm/%d", frame);
            SDL_snprintf(path, sizeof(path), "assets/start/menu/arm/%d.png", frame);
            img[frame] = loadHdReplacementOrPng(legacyPath, path);
        }
    }
    a = img[frame];
    /* Arm cels are the officer's forearm; draw at the room's height scale with the
     * cel left-aligned in the 4:3 menu box, so the pointer reaches across the box to
     * the menu rows (see BRIEFING_ARM_BOX_LEFT_X). */
    if (a && r2d_vectorActive()) r2d_submitImageWindowBoxX(a, BRIEFING_ARM_BOX_LEFT_X);
}

/* These lights are recoloured in-place in the original indexed cockpit. A
 * truecolor redraw has no such palette-index mask, so draw their live labels. */
void hdsprite_drawCockpitIndicator(int indicator, int x, int y, int color) {
    static const unsigned char glyphs[4][7] = {
        {30, 17, 17, 30, 20, 18, 17}, /* R: radar warning */
        {31, 4, 4, 4, 4, 4, 31},    /* I: infrared warning */
        {30, 17, 17, 30, 17, 17, 30}, /* B: air brake */
        {16, 16, 16, 16, 16, 16, 31} /* L: landing gear */
    };
    if (indicator < 0 || indicator >= 4 || !r2d_vectorActive()) return;
    r2d_submitRect(x, y, x + 6, y + 6, 0);
    for (int row = 0; row < 7; row++) {
        for (int column = 0; column < 5; column++) {
            if (glyphs[indicator][row] & (1 << (4 - column)))
                r2d_submitPoint(x + column + 1, y + row, color);
        }
    }
}

int hdsprite_drawRadarOwnship(float destX, float destY) {
    R2DImage *hd = radarOwnship();
    SDL_Surface *s;
    int f = RADAR_OWNSHIP_FOOTPRINT;
    if (!hd || !r2d_vectorActive()) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    /* Centre the footprint on the sub-pixel (destX,destY) so the ownship marker
     * glides with the scope grid, matching the legacy icon's destX-3 centring. */
    return r2d_submitImageF(hd, 0, 0, s->w, s->h,
                            destX - f / 2.0f, destY - f / 2.0f, f, f, 0);
}

/* Radar scope contacts (middle-MFD gauge bank), drawn into the 7x7 footprint centred
 * on the sub-pixel blip (cx,cy). Each returns 1 if it submitted the HD art (caller
 * skips the atlas), 0 to fall back. GPU-only; always 0 on the software backend. */

/* Enemy aircraft, spun to the contact's relative heading (authored once nose-up).
 * altBand: 0 = co-altitude, 1 = below you, 2 = above you (egui.c). */
int hdsprite_drawRadarContact(int altBand, float cx, float cy, int angle16) {
    static R2DImage *img[3];
    static int tried[3];
    static const char *const paths[3] = {
        "assets/flight/radar/plane-level.png", /* co-altitude */
        "assets/flight/radar/plane-low.png",   /* below you */
        "assets/flight/radar/plane-high.png",  /* above you */
    };
    R2DImage *hd;
    SDL_Surface *s;
    float rad;
    if (altBand < 0 || altBand > 2 || !r2d_vectorActive()) return 0;
    if (!tried[altBand]) {
        tried[altBand] = 1;
        if (r2d_hasNativeOverlay()) img[altBand] = loadHdPng(paths[altBand]);
    }
    hd = img[altBand];
    if (!hd) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    rad = (float)(int16)angle16 * (float)(6.283185307179586 / 65536.0);
    return r2d_submitImageRot(hd, 0, 0, s->w, s->h, cx, cy,
                              (float)RADAR_OWNSHIP_FOOTPRINT, (float)RADAR_OWNSHIP_FOOTPRINT, rad, 0);
}

int hdsprite_drawRadarAtlasIcon(int column, float cx, float cy, int angle16) {
    enum { RADAR_ATLAS_COLUMNS = 16 };
    static R2DImage *images[RADAR_ATLAS_COLUMNS] = {};
    static bool tried[RADAR_ATLAS_COLUMNS] = {};
    // Column zero is ownship, which has its separate aircraft replacement.
    if (column <= 0 || column >= RADAR_ATLAS_COLUMNS || !r2d_vectorActive()) return 0;
    if (!tried[column]) {
        tried[column] = true;
        char name[64];
        char path[1024];
        SDL_snprintf(name, sizeof(name), "flight/radar/atlas_%02d", column);
        if (r2d_hasNativeOverlay() && findReplacementAssetPath(name, ".png", path, sizeof(path)))
            images[column] = loadHdPng(path);
    }
    R2DImage *image = images[column];
    if (!image) return 0;
    SDL_Surface *surface = r2d_imageSurface(image);
    if (!surface) return 0;
    const float radians = (float)(int16)angle16 * (float)(6.283185307179586 / 65536.0);
    return r2d_submitImageRot(image, 0, 0, surface->w, surface->h, cx, cy,
                             RADAR_OWNSHIP_FOOTPRINT, RADAR_OWNSHIP_FOOTPRINT, radians, 0);
}

/* HUD reticles: lazily-loaded HD PNGs drawn into the legacy sprite footprint. The
 * footprint (320-space) equals the original blitSprite width/height so placement is
 * identical; the 1.2 present aspect then stretches it back toward square on screen. */
#define HUD_GUNRETICLE_W 11 /* legacy blitSprite(154, y, 0x94, 21, 11, 7, ...) */
#define HUD_GUNRETICLE_H 7
#define HUD_AAMSEEKER_W 13 /* legacy blitSprite(x, y, 0x91, 4, 13, 11, ...) */
#define HUD_AAMSEEKER_H 11

static R2DImage *loadHudReticle(const char *path) {
    if (!r2d_hasNativeOverlay()) return NULL; /* software: HD is GPU-only */
    return loadHdPng(path);
}

/* Fractional 320-space destination (destX,destY is the footprint's top-left) so the
 * reticle glides sub-pixel with the interpolated player state instead of snapping to
 * the 320x200 grid — the same native-res path the radar ownship and target boxes use. */
static int drawHudReticle(R2DImage *hd, float destX, float destY, int w, int h) {
    SDL_Surface *s;
    if (!hd || !r2d_vectorActive()) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    return r2d_submitImageF(hd, 0, 0, s->w, s->h, destX, destY, (float)w, (float)h, 0);
}

int hdsprite_drawHudGunReticle(float destX, float destY) {
    static R2DImage *img;
    static int tried;
    if (!tried) { tried = 1; img = loadHudReticle("assets/flight/hud/gun-reticle.png"); }
    return drawHudReticle(img, destX, destY, HUD_GUNRETICLE_W, HUD_GUNRETICLE_H);
}

int hdsprite_drawHudAamSeeker(float destX, float destY) {
    static R2DImage *img;
    static int tried;
    if (!tried) { tried = 1; img = loadHudReticle("assets/flight/hud/aam-seeker.png"); }
    return drawHudReticle(img, destX, destY, HUD_AAMSEEKER_W, HUD_AAMSEEKER_H);
}
