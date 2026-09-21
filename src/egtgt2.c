#include "math/legacy_horizontal.hpp"
using f15::math::legacy::fineUnits;
/* egtgt2.c — world->HUD projection + range/bearing helpers (reads g_viewZ
   as int16). Split from egtarget.c at the projectWorldToHud boundary. */
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleMagnitude;
#include "egflight.h"
#include "egframe.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "const.h"

#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* Private helpers for this translation unit. */
void drawTargetBox(int16, int16, int16, int16);
void drawMissileLock(void);
void drawTargetLabel(const char *, int16, int16);
void buildRangeString(int16 rangeRaw);
void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

// ==== seg000:0xc488 ====
void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ) {
    int16 relX;
    int32 camX;
    int16 relY;
    int32 camY;
    int16 relZ;
    int32 camDepth;

    relX = g_viewX_ - worldX;
    relY = worldY - g_viewY_;
    relZ = (worldZ - g_viewZ) >> 5;

    if (g_viewMode & 0x80) {
        relX -= (int16)((fineUnits(g_ViewX) - g_camEyeX) >> 5);
        relY -= (int16)((fineUnits(g_ViewY) - g_camEyeY) >> 5);
        relZ -= (int16)((-((int32)(uint16)g_viewZ - (int32)g_camEyeZ)) >> 5);
    }

    camX = rotateVectorComponent(0, relX, relY, relZ);
    camY = rotateVectorComponent(1, relX, relY, relZ);
    camDepth = rotateVectorComponent(2, relX, relY, relZ);

    if (camDepth >= 0) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    if (g_halfScaleRender) {
        camX >>= 1;
        camY >>= 1;
    }

    if (-camDepth < camX || camX < camDepth) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    vtxScratch.vproj.x.lo = (int16)((camX << 8) / camDepth) + 160;
    vtxScratch.vproj.y.lo = (int16)((camY << 8) / camDepth);
    vtxScratch.vproj.y.lo -= vtxScratch.vproj.y.lo >> 1 >> 1;
    vtxScratch.vproj.y.lo += (g_pageFront[8] == 199) ? 100 : 56;

    g_projDepth = (int16)(camDepth >> 3);

    if (vtxScratch.vproj.x.lo < 0 || vtxScratch.vproj.x.lo > 319) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
    if (vtxScratch.vproj.y.lo < 0 || g_pageFront[8] < vtxScratch.vproj.y.lo) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
}

/* Fine-precision projectWorldToHud. Takes the object's *fine* world position
 * (mapX<<5 scale on X/Y, altitude on Z — the same integrated coords the 3D model
 * is drawn from) rather than the coarse posX/posY (÷32) the reticle used to read.
 * The coarse path rounds the viewer and the object to the ÷32 grid independently,
 * so under the render/sim decouple the two roundings beat ±1 against each other
 * between sim ticks and the target box jumped around — worst up close, where one
 * grid cell spans many screen pixels. Differencing the interpolated fine coords
 * once and rotating at the finest power-of-two scale whose components still fit the
 * int16 rotate inputs keeps sub-grid precision through the perspective divide (the
 * projected ratio is scale-invariant), so the box glides with the model. */
void projectWorldToHudFine(int32 fineX, int32 fineY, int fineZ) {
    long relX, relY, relZ, am, camX, camY, camDepth;
    int rx, ry, rz, sh;

    relX = fineUnits(g_ViewX) - fineX;
    relY = fineY + fineUnits(g_ViewY) - 0x100000L; /* (relY >> 5) == object posY - g_viewY_ */
    relZ = (long)fineZ - (long)g_viewZ;

    if (g_viewMode & 0x80) {
        relX -= fineUnits(g_ViewX) - g_camEyeX;
        relY -= fineUnits(g_ViewY) - g_camEyeY;
        relZ -= -((long)(unsigned)g_viewZ - (long)g_camEyeZ);
    }

    am = labs(relX) | labs(relY) | labs(relZ);
    sh = 0;
    while ((am >> sh) > 0x3fff)
        sh++;
    rx = (int)(relX >> sh);
    ry = (int)(relY >> sh);
    rz = (int)(relZ >> sh);

    camX = rotateVectorComponent(0, rx, ry, rz);
    camY = rotateVectorComponent(1, rx, ry, rz);
    camDepth = rotateVectorComponent(2, rx, ry, rz);

    if (camDepth >= 0) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    if (g_halfScaleRender) {
        camX >>= 1;
        camY >>= 1;
    }

    if (-camDepth < camX || camX < camDepth) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    vtxScratch.vproj.x.lo = (int)((camX << 8) / camDepth) + 160;
    vtxScratch.vproj.y.lo = (int)((camY << 8) / camDepth);
    vtxScratch.vproj.y.lo -= vtxScratch.vproj.y.lo >> 1 >> 1;
    vtxScratch.vproj.y.lo += (g_pageFront[8] == 199) ? 100 : 56;

    /* Same perspective divide in float: the box draws from these on the GL overlay
     * so its centre isn't quantized to the 320x200 grid (worst up close). The GL 3D
     * pass projects with a 5/6 vertical grid-aspect (buildProjection) — 10/9 taller
     * than the software raster's 3/4 — so the box's fractional Y uses 5/6 too, or it
     * would drift vertically from the 3D target proportional to screen Y. The integer
     * vproj.y.lo keeps 3/4 for the software backend and the reticle range tests. */
    g_hudProjXf = (float)(camX << 8) / (float)camDepth + 160.0f;
    g_hudProjYf = (float)(camY << 8) / (float)camDepth * (5.0f / 6.0f) + ((g_pageFront[8] == 199) ? 100.0f : 56.0f);

    /* camDepth is in 2^sh-fine units; the coarse depth is camDepth * 2^(sh-5) and
     * g_projDepth is that >> 3, i.e. camDepth >> (8 - sh). */
    g_projDepth = (sh <= 8) ? (int)(camDepth >> (8 - sh)) : (int)(camDepth << (sh - 8));

    if (vtxScratch.vproj.x.lo < 0 || vtxScratch.vproj.x.lo > 319) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
    if (vtxScratch.vproj.y.lo < 0 || g_pageFront[8] < vtxScratch.vproj.y.lo) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
}

// ==== seg000:0xc661 ====
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ) {
    int32 sum;

    sum = (int32)fixedMulQ14(g_camRotMatrix[axis], vecX);
    sum += (int32)fixedMulQ14(g_camRotMatrix[3 + axis], vecZ);
    sum += (int32)fixedMulQ14(g_camRotMatrix[6 + axis], vecY);
    return sum;
}

int16 findWaypointEntry(int16 mapX, int16 mapY) {
    int16 idx;

    if ((g_nearestTileObj = findNearestTileObject((int32)mapX << 5, (0x8000L - (int32)mapY) << 5))) {
        mapX = g_nearestTileObj->x >> 5;
        mapY = -((int16)(g_nearestTileObj->y >> 5) - 0x8000);
        for (idx = 1; idx < g_planeCount; idx++) {
            if (g_planeTable.planes[idx].mapX == mapX && g_planeTable.planes[idx].mapY == mapY) {
                return idx;
            }
        }
        g_planeTable.planes[0].mapX = mapX;
        g_planeTable.planes[0].mapY = mapY;
        g_planeTable.planes[0].nameIndex = g_nearestTileObj->id + 0x100;
        if (g_smokeSourceIdx == 0) {
            g_smokeSourceIdx = -1;
        }
        return 0;
    } else {
        return -1;
    }
}

// ==== seg000:0xc7a2 ====
int16 computeMapTargetRange(int16 targetIdx) {
    return computeTargetBearing(g_planeTable.planes[targetIdx].mapX, g_planeTable.planes[targetIdx].mapY, 1);
}

// ==== seg000:0xc7c6 ====
int16 computeSimObjectRange(int16 objIdx) {
    return computeTargetBearing(g_simObjects[objIdx].posX, g_simObjects[objIdx].posY, 0);
}

// ==== seg000:0xc7ea ====
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing) {
    int16 dx, dy;
    /* Player side comes from the typed map position; the word extraction is
     * exact because targetX/targetY are already coarse words. */
    const auto pos = flightMapPosition();
    dx = f15::math::legacy::mapWordX(pos) - targetX;
    dy = f15::math::legacy::mapWordY(pos) - targetY;
    if (wantBearing != 0) {
        g_targetBearing = computeBearing(-dx, dy);
    }
    g_targetRange = rangeApprox(dx, dy);
    return g_targetRange;
}

// ==== seg000:0xc82d ====
int16 computeLoftAngle() {
    /* Divisor: scene height + margin. Fixed keeps the unsigned-word wrap;
     * modern uses the unwrapped height so the loft cue stays meaningful above
     * the word range. */
#ifdef F15_MODERN_MATH
    const uint32 scenePlusMargin = (uint32)((int)f15::math::legacy::Altitudes::render(flightSceneHeight()) + 0x1000);
#else
    const uint32 scenePlusMargin = (uint16)(g_viewZ + 0x1000);
#endif
    return (int16)((uint32)((int32)(0x4000 - angleMagnitude(g_ourPitch)) << 12) / scenePlusMargin) - 0x4000;
}

// ==== seg000:0xc864 ====
int16 getTargetSymbol(int16 wpIdx) {
    if (g_planeTable.planes[wpIdx].flags & 0x80) {
        return (isTargetOverWater(wpIdx) ? (int16)(char)g_waterTargetId[0] : (int16)(char)g_landTargetId[0]) + 0x100;
    }
    return g_planeTable.planes[wpIdx].nameIndex;
}

// ==== seg000:0xc8a4 ====
int16 isTargetOverWater(int16 wpIdx) {
    int16 category;

    category = ((char *)g_shapeTargetCategory)[g_planeTable.planes[wpIdx].nameIndex & 0x7f] & 0x0f;
    return (category == 12 || category == 9 || category == 11) ? 1 : 0;
}
