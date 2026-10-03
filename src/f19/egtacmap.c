#include "f19eg.h"

int16 sinMul(int16, int16);
int16 cosMul(int16, int16);
void loadColorPalette(int16);
void renderMapTerrain(const int16 *, int16, int16, int16);
int16 plotMapObject(int16, int16, int16, int16);
/* egtacmap.c — tactical map / screen drawing routines (F19) */

int16 mapXToScreen(int16 x);              /* sub_18B06 */
int16 mapYToScreen(int16 y);              /* sub_18B1D */
void drawClippedLineRegion(int16 sx1, int16 sy1, int16 sx2, int16 sy2,
                           int16 clipL, int16 clipR, int16 clipT, int16 clipB, int16 both); /* sub_18E12 */
int16 clampRange(int16 v, int16 lo, int16 hi);   /* sub_1D1FA */
int16 readScreenPixel(int16 screenX, int16 screenY); /* sub_19282 */
void FAR fillSpanRect(const int16 *dst, int16 left, int16 top, int16 right, int16 bottom); /* sub_21A58 */
void drawStringCentered(int16 *page, const char *text, int16 x, int16 y, int16 color); /* sub_19219 */
void drawViewportLine(int16 x1, int16 y1, int16 x2, int16 y2);   /* sub_18D9A */
void FAR gfx_drawString(int16 *page, const char *str, int16 len); /* sub_2F043 */
void fillPanelBox(int16 panelId, int16 color);               /* sub_190E8 */
void drawCenteredLabelBox(int16 panelId, const char *text);  /* sub_19123 */
void drawPanelText(int16 panelId, const char *text, int16 color); /* sub_190CB */
void drawStringBothPages(const char *text, int16 x, int16 y, int16 color); /* sub_191B4 */



/* ==== seg000:0x7e29 ==== */
void projectVertex(int32 vx, int32 vy, int32 vz);  /* sub_11372 */
void projectWorldPoint(int16 x, int16 y, int16 z) {
    g_projClipFlag = 0;
    projectVertex((int32)x << 5, ((int32)0x8000 - y) << 5, (int32)z);
}

/* ==== seg000:0x7e62 ==== */
void clearStatusPanel(void) {
    drawPanelText(2, "", 0);
}

/* ==== seg000:0x7e74 ==== */
struct CommData { int8 pad72[0x72]; int16 setupUseJoy; int8 pad74[4]; int16 gfxModeNum; };
struct MissileSpec { int16 weaponIdx; int16 ammo; };
struct Missile { char shortName[10]; char longName[12]; int16 specIndex; int16 weaponCategory; };
struct Sam { char name[8]; int16 lockRange, maxSpeed, weaponClass, turnRate, modelId; };
struct MapTarget { int16 objType; uint16 mapX; uint16 mapY; int16 active; int16 flags;
                   int16 alertLevel; int16 threatTimer; int16 symbol; };
uint8 far gfx_getDrawPage(void);         /* sub_2F10B */
void  far gfx_setDacAnimCount(int16 n);  /* sub_2F1B5 */
void setDrawColor(int16 color);                              /* sub_18F9C */
void fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2);   /* sub_18FB2 */
void drawStatusItem(int16 idx, int16 color);                 /* sub_19007 */
void drawStringActivePage(const char *t, int16 x, int16 y, int16 c); /* sub_191E5 */
void blitSprite(int16 dx, int16 dy, int16 sx, int16 sy, int16 w, int16 h, int16 t); /* sub_19912 */
int16 computeBearing(int16 dx, int16 dy);  /* sub_1D29D */
int16 rangeApprox(int16 dx, int16 dy);     /* sub_1D23B */
void drawNumber(int16 v, int16 x, int16 y, int16 color);   /* sub_19257 */
void hudMessage(const char *s);            /* sub_192B1 */
void drawThreatIndicator(void);            /* sub_1A5C8 */
void drawTacticalMap(int8 page);           /* sub_192FA */

void renderHudFrame(void) {
    int16 climbMarkerY, angleFixed, waypointMarkerX, circleX, angle,
          circleY, prevX, speedBarLen, prevY, markerX, deltaX, markerY, deltaY;
    int8 seekerShift;

    g_drawPage = gfx_getDrawPage();
    deltaX = waypoints[waypointIndex * 2] - g_viewX_;
    deltaY = waypoints[waypointIndex * 2 + 1] - g_viewY_;
    g_waypointBearing = computeBearing(deltaX, -deltaY);
    if (g_hudVisible == 0)
        goto HUDMSG;
    if (g_damageTakenFlag != 0) {
        g_damageTakenFlag = 0;
        if (!(g_viewMode & 0x80)) {
            setDrawColor(0xD);
            fillRectBoth(0, 0, 0x13F, 0x6B);
            gfx_setDacAnimCount(0x3C);
        }
    }
    g_hudDrawnFlag = 1;
    if (g_viewMode != 0)
        goto MAPCHECK;
    if (g_halfScaleRender != 0)
        goto MAPCHECK;
    setDrawColor(0xF);
    drawViewportLine(0x18, 0x6C, 0x127, 0x6C);
    if (commData->setupUseJoy == 0) {
        setDrawColor(0);
        drawViewportLine(0x100, 0x5E, 0x110, 0x5E);
        drawViewportLine(0x110, 0x5E, 0x110, 0x6A);
        drawViewportLine(0x110, 0x6A, 0x100, 0x6A);
        drawViewportLine(0x100, 0x6A, 0x100, 0x5E);
        drawViewportLine(0x108, 0x64, 0x108, 0x64);
        setDrawColor(0xF);
        markerX = ((int16)(joyAxes[0] - 0x78) >> 4) + 0x108;
        markerY = (((int16)joyAxes[1] * 3 - 0x168) >> 6) + 0x64;
        drawViewportLine(markerX - 1, markerY, markerX + 1, markerY);
        drawViewportLine(markerX, markerY + 1, markerX, markerY - 1);
    }
    if (g_playerPlaneFlags & 0x200) {
        setDrawColor(0xF);
        drawViewportLine(0x9C, 0x59, 0xA4, 0x59);
        drawViewportLine(0xA0, 0x56, 0xA0, 0x5C);
    }
    setDrawColor(g_nightMode != 0 ? 4 : 0);
    speedBarLen = clampRange((g_cornerSpeed - g_knots) * 2 / 5 + 0x1D, 0, 0x3D);
    if (speedBarLen != 0)
        drawViewportLine(0x48, 0x55 - speedBarLen, 0x48, 0x55);
    drawViewportLine(0xF7, 0x38, 0xF7,
                     clampRange(-((g_climbRate >> 4) - 0x38), 0x14, 0x55));
    if (!(g_playerPlaneFlags & 1) && (frameTick & 1) &&
        g_viewParamsFar[0x20] != 0 && g_climbRate < 0) {
        climbMarkerY = ((uint16)(g_planeTable[g_closestThreatIndex].flags & 0x200
                                  ? 0x100 : 0x80) / g_viewParamsFar[0x20] >> 4) + 0x38;
        setDrawColor(0xF);
        drawViewportLine(0xF2, climbMarkerY - 2, 0xF4, climbMarkerY);
        drawViewportLine(0xF2, climbMarkerY + 2, 0xF4, climbMarkerY);
    }
    if (g_knots < g_cornerSpeed && g_groundAltitude != g_viewZ &&
        (frameTick & 1))
        drawStringActivePage("stall warning", 0x84, 0x1E, 0xF);
    strcpy(g_nameBuf, "");
    strcat(g_nameBuf, itoa(missleSpec[missileSpecIndex].ammo, g_itoaScratch, 10));
    strcat(g_nameBuf, " ");
    strcat(g_nameBuf, missiles[missleSpec[missileSpecIndex].weaponIdx].longName);
    drawStringBothPages(g_nameBuf, 0x38, 0x60, 0xF);
    strcpy(g_nameBuf, "GUN ");
    strcat(g_nameBuf, itoa(g_gunAmmo, g_itoaScratch, 10));
    drawStringBothPages(g_nameBuf, 0x38, 0x66, 0xF);
    if (g_playerPlaneFlags & 0x400)
        drawThreatIndicator();
    if (g_currentWeaponType == 0 || g_currentWeaponType == 2) {
        setDrawColor(7);
        g_flightPathMarkerY = (g_rollPitchTrim >> 6) + 0x38;
        if (g_flightPathMarkerY > 0xA && g_flightPathMarkerY < 0x6F)
            blitSprite(0x9A, g_flightPathMarkerY - 4, 0x94, 0x15, 0xB, 7, 0xF);
        if (g_currentWeaponType == 2)
            drawStringBothPages("AIR-GROUND", 0x88, 0x66, 0xF);
        else
            drawStringBothPages("NAV", 0x98, 0x66, 0xF);
    }
    if (g_currentWeaponType == 1) {
        seekerShift = g_halfScaleRender + 4;
        markerX = (g_aamSeekerX >> seekerShift) + 0x9F;
        markerY = (g_aamSeekerY >> seekerShift) + 0x38;
        if (markerX > 0xA && markerX < 0x135 && markerY > 8 && markerY < 0x68)
            blitSprite(markerX - 6, markerY - 5, 0x91, 4, 0xD, 0xB, 0xE);
        if (sams[missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex].weaponClass == 7) {
            setDrawColor(7);
            angle = 0;
            do {
                angleFixed = angle << 8;
                circleX = sinMul(angleFixed, 0x28) + 0x9F;
                circleY = -(cosMul(angleFixed, 0x23) - 0x38);
                if (angle != 0)
                    drawViewportLine(circleX, circleY, prevX, prevY);
                prevX = circleX;
                prevY = circleY;
                angle += 0x10;
            } while (angle <= 0x100);
        }
        drawStringBothPages("AIR-AIR", 0x8C, 0x66, 0xF);
    }
    drawNumber(g_knots, 0x50, 0x36, 0xF);
    if (g_altitude <= 0x4E20)
        drawNumber(g_altitude < 0x64 ? g_altitude : g_altitude / 5 * 5,
                   0xE4, 0x36, 0xF);
    if (g_playerPlaneFlags & 2)
        drawStringBothPages("FLAPS", 0xFC, 0x66, 0xF);
    if (g_playerPlaneFlags & 8)
        drawStringBothPages("BRAKE", 0xE4, 0x66, 0xF);
    if (g_timeAccelMode > 1)
        drawStringBothPages("ACCEL", 0xE4, 0x60, 0xF);
    if (g_playerPlaneFlags & 0x1000)
        drawStringBothPages("TRAINING", 0xE4, 0x5A, 0xF);
    if (!(g_bombDamageMask & 8)) {
        deltaX = waypoints[waypointIndex * 2] - g_viewX_;
        deltaY = waypoints[waypointIndex * 2 + 1] - g_viewY_;
        if (rangeApprox(deltaX, deltaY) < 0x200 && waypointIndex < 3) {
            waypointIndex++;
            strcpy(g_nameBuf, "Waypoint ");
            strcat(g_nameBuf, itoa(waypointIndex, g_itoaScratch, 10));
            strcat(g_nameBuf, " reached");
            hudMessage(g_nameBuf);
        }
        waypointMarkerX = clampRange(((g_waypointBearing - g_ourHead) >> 6) / 3 + 0x9F,
                                     0x59, 0xE5);
        setDrawColor(0xB);
        drawViewportLine(waypointMarkerX - 2, 0xF, waypointMarkerX, 0x11);
        drawViewportLine(waypointMarkerX, 0x11, waypointMarkerX + 2, 0xF);
        drawViewportLine(waypointMarkerX - 2, 0xF, waypointMarkerX + 2, 0xF);
    }
    /* The drawStatusItem(9,3) arm is reached by two gotos and ends in
     * `goto MAPCHECK` just like the 0xE arm: MSC tail-merges the two
     * calls into a shared push/call tail and emits ARM3 as a deferred
     * block that jumps back into it (the original's "mov ax,3; jmp"
     * cold arm).  Plain if/else or ?: would instead emit the 3-arm
     * inline next to the merge. */
    if (frameTick & 1) {
        if ((rangeApprox(g_viewX_ - g_planeTable[g_homeBaseIdx].mapX,
                         g_viewY_ - g_planeTable[g_homeBaseIdx].mapY) >> 4)
            <= g_fuelRemaining)
            goto ARM3;
        drawStatusItem(9, 0xE);
        goto MAPCHECK;
    }
ARM3:
    drawStatusItem(9, 3);
    goto MAPCHECK;
MAPCHECK:
    if (g_mapMode == 1)
        drawTacticalMap(g_drawPage);
HUDMSG:
    if (g_hudMsgTimer != 0 &&
        ((g_viewMode == 0 && g_halfScaleRender == 0) || g_inputDisabled != 0)) {
        drawStringActivePage(g_hudMessageBuf,
            -(((int16)strlen(g_hudMessageBuf) >> 1) - 0x28) * 4, 0x18, 0xF);
        g_hudMsgTimer--;
    }
}

/* ==== seg000:0x8610 ==== */
void redrawTacMap(int16 x, int16 y);            /* sub_187EC */
void updatePanelMode(int16 mode) {
    if (g_panelLabelOn != 0) {
        switch (mode) {
        case 0:
            redrawTacMap(g_viewX_, g_viewY_);
            break;
        case 1:
            drawPanelText(1, "Tac", 0);
            break;
        }
        g_mapMode = mode;
    }
}

/* ==== seg000:0x8651 ==== */
void drawWaypointPanel(void); void drawLoadoutPanel(void); void drawWeaponsPanel(void);
void drawMissionObjectives(void); void nullsub_3(void);
void drawPanelModeText(int16 mode) {
    int16 unused[10];
    if (g_panelLabelOn == 0)
        return;
    switch (mode) {
    case 0x13:
        strcpy(g_nameBuf, "TrackCam ");
        switch (g_scanDir) {
        case 0x8000: strcat(g_nameBuf, "Rear"); break;
        case 0xC000: strcat(g_nameBuf, "Left"); break;
        case 0:      strcat(g_nameBuf, "Ahead"); break;
        case 0x4000: strcat(g_nameBuf, "Right"); break;
        }
        drawPanelText(2, g_nameBuf, 3);
        break;
    case 0x14: drawWaypointPanel(); break;
    case 0x15: drawLoadoutPanel(); break;
    case 0x16: drawWeaponsPanel(); break;
    case 0x18: drawMissionObjectives(); break;
    case 0x19: nullsub_3(); break;
    }
    g_curPanelMode = mode;
}

/* ==== seg000:0x86fc ==== */
void drawStatusItem(int16 idx, int16 color);   /* sub_19007 */
void drawPanelModeText(int16 mode);            /* sub_18651 */
void updateStatusPanel(int16 arg) {
    int16 i;
    if (arg == 0x16) {
        for (i = 0; i < 7; i++)
            drawStatusItem(i + 0xA, (g_weaponMask & (1 << i)) ? 0xC : 0xA);
    }
    if (arg == g_curPanelMode)
        drawPanelModeText(arg);
}

/* ==== seg000:0x8751 ==== */
void zoomIn(void);                              /* sub_18A1B */
void initTacMapView(void) {
    g_mapMode = 0;
    g_scopeClipLeft = 0x28;
    g_scopeClipRight = 0x90;
    g_scopeClipTop = 0x7C;
    g_scopeClipBottom = 0xC4;
    g_scopeCenterX = 0x68;
    g_scopeCenterY = 0x48;
    zoomIn();
}

/* ==== seg000:0x877f ==== */
void setDrawColor(int16 color);                              /* sub_18F9C */
void fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2);   /* sub_18FB2 */
void drawGaugeBar(int16 val, int16 color, int16 x1, int16 x2) {
    if (g_panelLabelOn == 0)
        return;
    setDrawColor(color);
    if (val < 0)
        fillRectBoth(x1 + 0x9A, 0x7B, x2 + 0x9A, 0x7B - val / 2);
    if (val > 0)
        fillRectBoth(x1 + 0x9A, 0xAD - val / 2, x2 + 0x9A, 0xAD);
}

/* ==== seg000:0x87ec ==== */
struct TargetSlot { int16 state; int16 planeIndex; int16 viewIndex; int16 flags;
                    int16 seedNoise; int16 pad[4]; };      /* 0x12 bytes */
void far gfx_setFadeSteps(int16 n);   /* sub_2F15B */
void resetSimObjectLocks(void);       /* sub_14BC8 */
void cacheScopePanel(void);           /* sub_1A23F */
void restoreScopePanel(void);         /* sub_1A26C */
void far gfx_copyRect(int16 src, int16 sx, int16 sy, int16 dst, int16 dx, int16 dy, int16 w, int16 h); /* sub_2F0FC */
void redrawTacMap(int16 centerX, int16 centerY) {
    int16 i, j, sx, sy, n, modeFlg;
    g_mapMode = 0;
    if (g_hudVisible == 0)
        return;
    drawPanelText(1, "Map", 0);
    i = 0x68 << (9 - g_mapZoomLevel);
    g_mapCenterX = clampRange(sinMul(g_ourHead, 0x8000 >> g_mapZoomLevel) + centerX, i, 0x7fff - i);
    i = (0x48 << (9 - g_mapZoomLevel)) / 3 * 4;
    g_mapCenterY = clampRange(centerY - cosMul(g_ourHead, 0x8000 >> g_mapZoomLevel), i, 0x7fff - i);
    loadColorPalette(commData->gfxModeNum ? 0 : 3);
    gfx_setFadeSteps(0x13);
    renderMapTerrain(g_mapTerrainMode, g_mapCenterX / 2, -(g_mapCenterY / 2 - 0x4000), 9 - g_mapZoomLevel);
    gfx_setFadeSteps(g_viewParamsFar[0x1C] < 2 ? 0xC : 0x10);
    modeFlg = commData->gfxModeNum == 0;
    for (i = 1; i < g_storeDefCount; i++) {
        if (g_planeTable[i].active != 0 && (*(uint8 *)&g_planeTable[i].flags & 0x84) == 4)
            plotMapObject(g_planeTable[i].mapX, g_planeTable[i].mapY,
                          (*(uint8 *)&g_planeTable[i].flags & 2 || modeFlg) ? 0 : 8, 1);
    }
    for (i = 0; i < 2; i++) {
        if (!(g_playerPlaneFlags & (0x4000 >> i)))
            plotMapObject(g_planeTable[g_targetSlots[i].planeIndex].mapX,
                          g_planeTable[g_targetSlots[i].planeIndex].mapY,
                          modeFlg ? 0 : 0xD, 1);
        plotMapObject(g_planeTable[g_targetSlots[i].viewIndex].mapX,
                      g_planeTable[g_targetSlots[i].viewIndex].mapY, 0xA, 1);
    }
    if ((char)gfx_getDrawPage() == 0)
        cacheScopePanel();
    else
        gfx_copyRect(*g_pageBack, 0x28, 0x7C, *g_pageOffscreen, 0x28, 0x7C, 0x68, 0x48);
    restoreScopePanel();
    resetSimObjectLocks();
}

/* ==== seg000:0x8a1b ==== */
void zoomIn(void) {
    if (g_viewMode & 0x80) {
        g_externalCamDist--;
    } else {
        if (g_mapMode == 0 && g_mapZoomLevel < 9) {
            g_mapZoomLevel++;
            redrawTacMap(g_viewX_, g_viewY_);
        }
        if (g_mapMode == 1) {
            g_radarScopeRange++;
        }
    }
}

/* ==== seg000:0x8a54 ==== */
void zoomOut(void) {
    if (g_viewMode & 0x80) {
        g_externalCamDist++;
    } else {
        if (g_mapMode == 0 && g_mapZoomLevel > 2) {
            g_mapZoomLevel--;
            redrawTacMap(g_viewX_, g_viewY_);
        }
        if (g_mapMode == 1 && g_radarScopeRange != 0) {
            g_radarScopeRange--;
        }
    }
}

/* ==== seg000:0x8b06 ==== */
int16 mapXToScreen(int16 mapX) {
    return ((mapX - g_mapCenterX) >> (10 - (uint8)g_mapZoomLevel)) + 0x5B;
}

/* ==== seg000:0x8b1d ==== */
int16 mapYToScreen(int16 mapY) {
    return (((mapY - g_mapCenterY) >> (10 - (uint8)g_mapZoomLevel)) * 3 >> 1 >> 1) + 0x9F;
}

/* ==== seg000:0x8b41 ==== */
void drawMapPoint(int16 x, int16 y, int16 color);            /* sub_18FE7 */
int16 plotMapObject(int16 mapX, int16 mapY, int16 color, int16 big) {
    int16 screenX, screenY;
    if (g_mapMode != 0 || g_panelLabelOn == 0)
        return 0;
    screenX = mapXToScreen(mapX);
    screenY = mapYToScreen(mapY);
    if (color != -1 && screenX >= g_scopeClipLeft && screenX < g_scopeClipRight - 1 &&
        screenY >= g_scopeClipTop && screenY < g_scopeClipBottom - 1) {
        drawMapPoint(screenX, screenY, color);
        if (big != 0) {
            drawMapPoint(screenX + 1, screenY, color);
            drawMapPoint(screenX, screenY + 1, color);
            drawMapPoint(screenX + 1, screenY + 1, color);
        }
        return 0;
    } else {
        return 1;
    }
}

/* ==== seg000:0x8bea ==== */
int16 readMapPixelColor(int16 mapX, int16 mapY) {
    int16 screenX, screenY, color;
    if (g_mapMode != 0) return 0;
    screenX = mapXToScreen(mapX);
    screenY = mapYToScreen(mapY);
    screenX = clampRange(screenX, g_scopeClipLeft, g_scopeClipRight);
    screenY = clampRange(screenY, g_scopeClipTop, g_scopeClipBottom);
    color = -1;
    if (screenX > g_scopeClipLeft && screenX < g_scopeClipRight && screenY > g_scopeClipTop && screenY < g_scopeClipBottom) {
        color = readScreenPixel(screenX, screenY);
    }
    return color;
}

/* ==== seg000:0x8c76 ==== */
int16 sinMul(int16 angle, int16 value);
int16 cosMul(int16 angle, int16 value);
void drawMapLine(int16 x1, int16 y1, int16 x2, int16 y2);

/* Draw a map arc/circle centred at (cx,cy) with radius r.  Sweeps the angle
 * from startA to endA in 0x10 steps; each point is x=cx+r*sin, y=cy-r*cos.
 * The first point is plotted via plotMapObject; later points connect to the
 * previous with drawMapLine when drawLines is set. */
void drawMapArc(int16 cx, int16 cy, int16 r, int16 color, int16 drawLines, int16 startA, int16 endA) {
    int16 angle, anorm, px, py, prevX, prevY;
    if (endA < startA) {
        startA += 0x100;
    }
    setDrawColor(color);
    angle = startA;
    goto TEST;
PLOT:
    plotMapObject(px, py, color, 0);
UPDATE:
    prevX = px;
    prevY = py;
    angle += 0x10;
TEST:
    if (angle <= endA) {
        anorm = angle << 8;
        px = cx + sinMul(anorm, r);
        py = cy - cosMul(anorm, r);
        if ((uint16)px > 0xC000) px = 0;
        if ((uint16)py > 0xC000) py = 0;
        if (angle == startA || drawLines == 0) {
            goto PLOT;
        }
        drawMapLine(px, py, prevX, prevY);
        goto UPDATE;
    }
}

/* ==== seg000:0x8d2a ==== */
void drawMapLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    drawClippedLineRegion(mapXToScreen(x1), mapYToScreen(y1), mapXToScreen(x2), mapYToScreen(y2), g_scopeClipLeft, g_scopeClipRight, g_scopeClipTop, g_scopeClipBottom, 1);
}

/* ==== seg000:0x8d71 ==== */
void drawFullscreenLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    drawClippedLineRegion(x1, y1, x2, y2, 0, 319, 0, 199, 1);
}

/* ==== seg000:0x8f10 ==== */
void drawScreenLineOnePage(int16 x1, int16 y1, int16 x2, int16 y2) {
    drawClippedLineRegion(x1, y1, x2, y2, 0, 319, 0, 199, 0);
}

/* ==== seg000:0x8f38 ==== */
void drawHudViewLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    if (g_halfScaleRender != 0) {
        if (g_viewParamsFar[0x20] < 2) {
            drawViewportLine(x1, y1, x2, y2);
        } else {
            drawClippedLineRegion(x1, y1, x2, y2, 104, 216, 62, 108, 0);
        }
    } else {
        drawClippedLineRegion(x1, y1, x2, y2, 48, 271, 15, 108, 0);
    }
}

/* ==== seg000:0x8f9c ==== */
void setDrawColor(int16 color) {
    g_pageFront[2] = color;
    g_pageBack[2] = color;
}

/* ==== seg000:0x8fb2 ==== */
void fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2) {
    fillSpanRect(g_pageFront, x1, y1, x2, y2);
    fillSpanRect(g_pageBack, x1, y1, x2, y2);
}

/* ==== seg000:0x8fe7 ==== */
void drawMapPoint(int16 x, int16 y, int16 color) {
    setDrawColor(color);
    drawFullscreenLine(x, y, x, y);
}

/* ==== seg000:0x9007 ==== */
struct StatCell { int16 x1, y1, x2, y2, val; };
void FAR gfx_drawStatusBox(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2, int16 old, int16 val); /* sub_2F0F7 */
void drawStatusItem(int16 idx, int16 val) {
    if (g_panelLabelOn == 0)
        return;
    if (g_statCells[idx].val != val) {
        gfx_drawStatusBox(g_pageFront, g_statCells[idx].x1, g_statCells[idx].y1,
                          g_statCells[idx].x2, g_statCells[idx].y2, g_statCells[idx].val, val);
        gfx_drawStatusBox(g_pageBack,  g_statCells[idx].x1, g_statCells[idx].y1,
                          g_statCells[idx].x2, g_statCells[idx].y2, g_statCells[idx].val, val);
        g_statCells[idx].val = val;
    }
}

/* ==== seg000:0x9071 ==== */
void drawPanelGridText(int16 panel, int16 col, int16 row, const char *text, int16 color) {
    if (g_panelLabelOn == 0)
        return;
    if (panel == 1)
        drawStringBothPages(text, col * 4 + 0x28, row * 6 + 0x7C, color);
    if (panel == 2)
        drawStringBothPages(text, col * 4 + 0xB0, row * 6 + 0x7C, color);
}

/* ==== seg000:0x90cb ==== */
void drawPanelText(int16 panelId, const char *text, int16 color) {
    fillPanelBox(panelId, color);
    drawCenteredLabelBox(panelId, text);
}

/* ==== seg000:0x90e8 ==== */
void fillPanelBox(int16 panelId, int16 color) {
    setDrawColor(color);
    if (panelId == 1) {
        fillRectBoth(0x28, 0x7C, 0x8F, 0xC3);
    } else {
        fillRectBoth(0xB0, 0x7C, 0x118, 0xC4);
    }
}

/* ==== seg000:0x9123 ==== */
void drawCenteredLabelBox(int16 panelId, const char *text) {
    int16 xl, y, xr;
    if (strlen(text) == 0 || g_panelLabelOn == 0) {
        return;
    }
    if (panelId == 1) {
        xl = 0x28;
        xr = 0x8F;
    } else {
        xl = 0xB0;
        xr = 0x118;
    }
    y = 0x7C;
    y -= 8;
    setDrawColor(8);
    fillRectBoth(xl + 5, y, xr - 5, y + 4);
    drawStringBothPages(text, ((xl + xr) >> 1) - (strlen(text) << 1), y, 0xB);
}

/* ==== seg000:0x91b4 ==== */
void drawStringBothPages(const char *text, int16 screenX, int16 screenY, int16 color) {
    drawStringCentered(g_pageFront, text, screenX, screenY, color);
    drawStringCentered(g_pageBack, text, screenX, screenY, color);
}

/* ==== seg000:0x91e5 ==== */
void drawStringActivePage(const char *text, int16 screenX, int16 screenY, int16 color) {
    if (g_drawPage == 0) {
        drawStringCentered(g_pageFront, text, screenX, screenY, color);
    } else {
        drawStringCentered(g_pageBack, text, screenX, screenY, color);
    }
}

/* ==== seg000:0x9219 ==== */
void drawStringCentered(int16 *strStruct, const char *text, int16 screenX, int16 screenY, int16 color) {
    char upbuf[128];      /* DOS strupr mutated the source in place */
    strStruct[6] = 0;
    strStruct[4] = screenX;
    strStruct[5] = screenY;
    strStruct[2] = color;
    strncpy(upbuf, text, sizeof(upbuf) - 1);
    upbuf[sizeof(upbuf) - 1] = 0;
    gfx_drawString(strStruct, strupr(upbuf), strlen(text));
}

/* ==== seg000:0x9257 ==== */
void drawNumber(int16 value, int16 x, int16 y, int16 color) {
    char buf[20];
    itoa(value, buf, 10);
    drawStringBothPages(buf, x, y, color);
}

/* ==== seg000:0x9282 ==== */
int16 readScreenPixel(int16 screenX, int16 screenY) {
    regs.h.ah = 0x0D;
    regs.x.cx = screenX;
    regs.x.dx = screenY;
    regs.h.bh = 0;
    int86(0x10, &regs, &regs);
    return regs.h.al;
}

/* ==== seg000:0x92b1 ==== */
void hudMessage(const char *src) {
    strcpy(g_hudMessageBuf, src);
    g_hudMsgTimer = g_frameRateScaling * 3;
}

/* ==== seg000:0x92cd ==== */
struct StoreDef { int16 subIdx; uint16 coordX; uint16 coordY; int8 pad[8]; int16 nameIdx; };
int16 getWeaponStat(int16 statIdx, int16 sel) {
    return g_statTab[statIdx][ g_classTab[g_storeDefs[sel].nameIdx & 0x7F] & 0xF ];
}

/* ==== seg000:0x92fa ==== */
struct SimObject {
    int16 objType;      /* +0x00 */
    uint16 posX;        /* +0x02 */
    int16 posY;         /* +0x04 */
    int16 alt;          /* +0x06 */
    int32 worldX;       /* +0x08 */
    int32 worldY;       /* +0x0C */
    union { int16 w; uint8 b[2]; } heading; /* +0x10 */
    int16 pitch;        /* +0x12 */
    union { int16 w; uint8 b[2]; } bank;    /* +0x14 */
    int16 spec;         /* +0x16 */
    union { uint16 w; uint8 b[2]; } flags;  /* +0x18 */
    int16 speed;        /* +0x1A */
    int16 timer;        /* +0x1C */
    int16 weaponType;   /* +0x1E */
    int16 terrainColor; /* +0x20 */
    int16 damage;       /* +0x22 */
};                                   /* 36 bytes */
struct Projectile { int16 mapX, mapY, alt, speed, worldX, worldY, worldZ, ttl,
                    specIdx, weaponIdx, targetLock, targetRef; };
struct MapEvent { int16 mapX, mapY, unused4, type, ttl, unusedA; };
void projectMapPoint(int16 mapX, int16 mapY);               /* sub_19810 */
void drawMapMarkerBox(int16 cx, int16 cy, int16 color);     /* sub_1978C */
void blitGaugeSprite(int16 srcCol, int16 srcRow, int16 destX, int16 destY); /* sub_198BC */

void drawTacticalMap(int8 page) {
    int16 startX, code, startY, altBand, altDiff, gridX, i, gridY, radius, gridLo, gridStep;

    radius = g_radarScopeRange + 1;
    setDrawColor(0);
    if (page == 0)
        fillSpanRect(g_pageFront, 0x27, 0x7B, 0x90, 0xC4);
    else
        fillSpanRect(g_pageBack, 0x27, 0x7B, 0x90, 0xC4);
    setDrawColor(8);
    gridStep = 1;
    if (g_radarScopeRange < 2 && g_detailLevel != 0)
        gridStep = (1 << (2 - (uint8)g_radarScopeRange)) + 1;
    gridLo = 1 - gridStep;
    gridX = g_viewX_ & 0xFC00;
    gridY = g_viewY_ & 0xFC00;
    for (i = gridLo; i <= gridStep; i++) {
        projectMapPoint(i * 0x400 + gridX, gridY + 0x1400);
        startX = g_vprojXlo;
        startY = g_vprojYlo;
        projectMapPoint(i * 0x400 + gridX, gridY - 0x1000);
        drawClippedLineRegion(startX, startY, g_vprojXlo, g_vprojYlo, 0x28, 0x8F, 0x7C, 0xC3, 0);
    }
    for (i = gridLo; i <= gridStep; i++) {
        projectMapPoint(gridX + 0x1400, i * 0x400 + gridY);
        startX = g_vprojXlo;
        startY = g_vprojYlo;
        projectMapPoint(gridX - 0x1000, i * 0x400 + gridY);
        drawClippedLineRegion(startX, startY, g_vprojXlo, g_vprojYlo, 0x28, 0x8F, 0x7C, 0xC3, 0);
    }
    for (i = 0; i < g_groundUnitCount; i++) {
        if ((g_simObjects[i].flags.b[0] & 2) && g_simObjects[i].speed != 0) {
            projectMapPoint(g_simObjects[i].posX, g_simObjects[i].posY);
            if (g_projDepth != -1) {
                if (g_currentWeaponType == 1 && i == g_airTargetLock)
                    drawMapMarkerBox(g_vprojXlo, g_vprojYlo, 7);
                if (g_scopeSweepTimer > 0 && i == -1 - g_threatLabelTarget)
                    drawMapMarkerBox(g_vprojXlo, g_vprojYlo, g_scopeArcColor);
                code = g_simObjects[i].heading.w - g_ourHead + 0x800;
                altDiff = g_simObjects[i].alt - g_viewZ;
                altBand = 0;
                if (altDiff < -1000)
                    altBand = 1;
                if (altDiff > 1000)
                    altBand = 2;
                blitGaugeSprite((code >> 0xC) & 0xF, altBand, g_vprojXlo, g_vprojYlo);
            }
        }
    }
    for (i = 0; i < 0xC; i++) {
        if (g_projectiles[i].ttl != 0) {
            projectMapPoint(g_projectiles[i].mapX, g_projectiles[i].mapY);
            if (g_projDepth != -1) {
                setDrawColor(sams[g_projectiles[i].specIdx].weaponClass <= 0 ? 0xC : 0xE);
                if (i >= 8)
                    setDrawColor(0xF);
                code = g_projectiles[i].worldX - g_ourHead;
                drawScreenLineOnePage(g_vprojXlo, g_vprojYlo,
                                      g_vprojXlo - sinMul(code, radius), cosMul(code, radius) + g_vprojYlo);
            }
        }
    }
    for (i = 0; i < g_storeDefCount; i++) {
        if (!(g_planeTable[i].flags & 0x80)) {
            projectMapPoint(g_planeTable[i].mapX, g_planeTable[i].mapY);
            if (g_projDepth != -1) {
                if (g_currentWeaponType == 2 && i == g_groundTargetLock)
                    drawMapMarkerBox(g_vprojXlo, g_vprojYlo, 7);
                if (g_scopeSweepTimer > 0 && i == g_threatLabelTarget)
                    drawMapMarkerBox(g_vprojXlo, g_vprojYlo, g_scopeArcColor);
                code = 5;
                if (g_planeTable[i].flags & 0x201)
                    code = ((-g_ourHead + 0x1000) >> 0xD & 3) + 8;
                if (g_planeTable[i].active != 0)
                    code = 1;
                if (*(uint8 *)&g_planeTable[i].flags & 8)
                    code = 7;
                blitGaugeSprite(code, 3, g_vprojXlo, g_vprojYlo);
            }
        }
    }
    projectMapPoint(g_viewX_, g_viewY_);
    if (g_projDepth != -1)
        blitGaugeSprite(0, 3, g_vprojXlo, g_vprojYlo);
    for (i = 0; i < 4; i++) {
        if (mapEvents[i].ttl != 0) {
            projectMapPoint(mapEvents[i].mapX, mapEvents[i].mapY);
            if (g_projDepth != -1) {
                switch (mapEvents[i].type) {
                case 1:
                    blitGaugeSprite(2, 3, g_vprojXlo, g_vprojYlo);
                    break;
                case 2:
                    blitGaugeSprite(3, 3, g_vprojXlo, g_vprojYlo);
                    break;
                case 3:
                    blitGaugeSprite(6, 3, g_vprojXlo, g_vprojYlo);
                    break;
                }
            }
        }
    }
}

/* ==== seg000:0x9979 ==== */
void drawFuelCell(int16 amount, int16 color);               /* sub_19D5E */
void formatMissionClock(uint16 time);                       /* sub_19DA3 */

void drawWaypointPanel(void) {
    int16 clr, dist, f, burn[4], row, i, clk, sx, sy, ux, uy, o;

    gfx_setFadeSteps(0x11);
    if (g_wpPanelMode != 0)
        drawPanelText(2, "Select Waypoint", 1);
    else
        drawPanelText(2, "Change Waypoint", 4);
    clk = g_missionTick;
    drawPanelGridText(2, 7, 0, "TIME", 0xF);
    formatMissionClock(clk);
    drawPanelGridText(2, 0xD, 0, g_nameBuf, 0xF);
    row = 2;
    for (i = 0; i < 4; i++) {
        if (i <= waypointIndex) {
            clr = 0;
            ux = g_viewX_;
            uy = g_viewY_;
            if (g_wpPanelMode != 0 && i == waypointIndex)
                clr = 0xF;
            if (i == waypointIndex) {
                if (g_wpPanelMode != 0)
                    clr = 0xF;
                else
                    clr = (i & 1) ? 3 : 0xB;
            }
        } else {
            clr = (i & 1) ? 3 : 0xB;
            ux = waypoints[i * 2 - 2];
            uy = waypoints[i * 2 - 1];
        }
        if (g_wpPanelMode == 0 && i == g_wpSelectIdx)
            clr = 0xF;
        strcpy(g_nameBuf, " Waypoint ");
        strcat(g_nameBuf, itoa(i + 1, g_itoaScratch, 10));
        strcat(g_nameBuf, " Dist-");
        dist = rangeApprox(ux - waypoints[i * 2], uy - waypoints[i * 2 + 1]);
        strcat(g_nameBuf, itoa(dist >> 6, g_itoaScratch, 10));
        strcat(g_nameBuf, " Km");
        drawPanelGridText(2, 0, row++, g_nameBuf, clr);
        o = dist / ((uint16)(g_startRange > 0x7D0 ? g_startRange : 0x2A30) >> 6);
        burn[i] = ((g_weaponMask & 0x20 ? g_gunHits : 0) +
                   g_engineThrust * g_engineThrust / 1000) * o * 5;
        strcpy(g_nameBuf, "");
        strcat(g_nameBuf, itoa(o >> 2, g_itoaScratch, 10));
        strcat(g_nameBuf, " MIN.  WRM");
        drawPanelGridText(2, 1, row, g_nameBuf, clr);
        if (i >= waypointIndex)
            clk += o * 8;
        formatMissionClock(clk);
        drawPanelGridText(2, 0xE, row++, g_nameBuf, clr);
    }
    drawFuelCell(10000, 0);
    f = g_fuelRemaining;
    for (i = 0; i < 4; i++) {
        if (i >= waypointIndex) {
            drawFuelCell(f, i == waypointIndex ? 0xF : (i & 1) ? 3 : 0xB);
            f -= burn[i];
        }
    }
    drawFuelCell(f, 2);
    strcpy(g_nameBuf, "Fuel ");
    strcat(g_nameBuf, itoa(g_fuelRemaining, g_itoaScratch, 10));
    strcat(g_nameBuf, " lbs.");
    drawStringBothPages(g_nameBuf, 0xC4, 0xBC, 8);
    gfx_setFadeSteps(g_viewParamsFar[0x1C] < 2 ? 0xC : 0x10);
    if (g_mapMode == 0) {
        restoreScopePanel();
        sx = g_viewX_;
        sy = g_viewY_;
        setDrawColor(0xF);
        for (i = waypointIndex; i < 4; i++) {
            if (waypoints[i * 2] != 0) {
                drawMapLine(sx, sy, waypoints[i * 2], waypoints[i * 2 + 1]);
                sx = waypoints[i * 2];
                sy = waypoints[i * 2 + 1];
                setDrawColor(0xA);
            }
        }
    }
}

/* ==== seg000:0x9e4f ==== */

void drawLoadoutPanel(void) {
    int16 gy, hi, type, sp, lx, ly, v, w, x, i, rx, k, tx, ty, tz;

    if (g_curPanelMode != 0x15)
        drawPanelText(2, "Weapons", 4);
    for (v = 0; v < 4; v++) {
        type = missleSpec[v].weaponIdx;
        lx = (v & 1) * 0xD;
        ly = (v & 2) * 3;
        strcpy(g_nameBuf, "");
        strcat(g_nameBuf, itoa(missleSpec[v].ammo, g_itoaScratch, 10));
        strcat(g_nameBuf, " ");
        strcat(g_nameBuf, missiles[type].shortName);
        tx = lx + 1;
        drawPanelGridText(2, tx, ly, g_nameBuf, v == missileSpecIndex ? 0xF : 7);
        strcpy(g_nameBuf, missiles[type].longName);
        drawPanelGridText(2, tx, ly + 1, g_nameBuf, v == missileSpecIndex ? 0xF : 7);
        ty = lx * 4;
        tz = ly * 6;
        gfx_copyRect(*g_pageOffscreen, g_wpnSpriteX[v], g_wpnSpriteY[v],
                     *g_pageFront, ty + 0xB0, tz + 0x88, 0x33, 0x17);
        setDrawColor(0);
        sp = missiles[type].weaponCategory;
        x = missleSpec[v].ammo;
        if (sp < 4) {
            hi = -(x * 0x15 / sp - 0x15);
            if (hi > 0)
                fillSpanRect((const int16 *)(f19_dseg + *g_pageFront), ty + 0xB1, tz + 0x89, ty + 0xE1, tz + 0x88 + hi);
        } else if (x < 4) {
            for (w = 0; w < 4 - x; w++) {
                rx = (w & 1) * 0x18 + lx * 4 + 0xB1;
                gy = (w & 2) * 5 + ly * 6 + 0x89;
                fillSpanRect((const int16 *)(f19_dseg + *g_pageFront), rx, gy, rx + 0x18, gy + 0xA);
            }
        }
    }
    gfx_copyRect(*g_pageFront, 0xB0, 0x7C, *g_pageBack, 0xB0, 0x7C, 0x68, 0x48);
}

/* ==== seg000:0xa0bd ==== */
struct CellRect { int16 x1, y1, x2, y2; };
void FAR gfx_drawStatusBox(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2, int16 old, int16 val); /* sub_2F0F7 */
void blitSprite(int16 destX, int16 destY, int16 srcX, int16 srcY, int16 width, int16 height, int16 transparent); /* sub_19912 */

void drawWeaponsPanel(void) {
    int16 i, old, val;
    int16 *pageCur, *pageOther;

    if (g_curPanelMode != 0x16) {
        drawPanelText(2, "Damage", 0);
        blitSprite(0xB0, 0x7C, 0xD8, 0, 0x68, 0x48, 0);
    }
    if (g_drawPage != 0) {
        pageCur = g_pageBack;
        pageOther = g_pageFront;
    } else {
        pageCur = g_pageFront;
        pageOther = g_pageBack;
    }
    for (i = 0; i < 7; i++) {
        if (g_weaponMask & (1 << i)) {
            old = 2;
            val = 0xC;
        } else {
            old = 0xC;
            val = 2;
        }
        gfx_drawStatusBox(pageCur, g_weaponCells[i].x1 + 0xB0, g_weaponCells[i].y1 + 0x7C,
                          g_weaponCells[i].x2 + 0xB0, g_weaponCells[i].y2 + 0x7C, old, val);
    }
    strcpy(g_nameBuf, "CHAFF  ");
    strcat(g_nameBuf, itoa(g_chaffCount, g_itoaScratch, 10));
    drawPanelGridText(2, 0xF, 10, g_nameBuf, 0xF);
    strcpy(g_nameBuf, "FLARES ");
    strcat(g_nameBuf, itoa(g_rocketCount, g_itoaScratch, 10));
    drawPanelGridText(2, 0xF, 0xB, g_nameBuf, 0xF);
    gfx_copyRect(*pageCur, 0xB0, 0x7C, *pageOther, 0xB0, 0x7C, 0x68, 0x48);
}

/* ==== seg000:0xa23f / 0xa26c / 0xa2c5 ==== */

void FAR gfx_copyRect(int16 src, int16 sx, int16 sy, int16 dst, int16 dx, int16 dy, int16 w, int16 h); /* sub_2F0FC */

void cacheScopePanel(void) {
    gfx_copyRect(*g_pageFront, 0x28, 0x7C, *g_pageOffscreen, 0x28, 0x7C, 0x69, 0x49);
}

void restoreScopePanel(void) {
    gfx_copyRect(*g_pageOffscreen, 0x28, 0x7C, *g_pageFront, 0x28, 0x7C, 0x69, 0x49);
    gfx_copyRect(*g_pageFront, 0x28, 0x7C, *g_pageBack, 0x28, 0x7C, 0x69, 0x49);
}

void captureScopePanel(void) {
    gfx_copyRect(*g_pageOffscreen, 0x28, 0x7C, g_drawPage ? *g_pageBack : *g_pageFront, 0x28, 0x7C, 0x69, 0x49);
}

/* ==== seg000:0xa300 ==== */
void buildStoreName(int16 i);                              /* sub_14D03 */
void formatMissionClock(uint16 time);                      /* sub_19DA3 */

void drawMissionObjectives(void) {
    int16 n, i;
    char work[24];
    n = 0;
    drawPanelText(2, "Mission", 0);
    drawPanelGridText(2, 5, n++, "Mission Orders", 0xF);
    n++;
    drawPanelGridText(2, 1, n++, "Take off from", 0xA);
    buildStoreName(g_selStoreIdx);
    drawPanelGridText(2, 1, n++, g_nameBuf, 0xA);
    for (i = 0; i < 2; i++) {
        if (g_playerPlaneFlags & (0x4000 >> i)) {
            if (i != 0)
                strcpy(g_nameBuf, "SECONDARY");
            else
                strcpy(g_nameBuf, "PRIMARY");
            strcat(g_nameBuf, " Mission");
            drawPanelGridText(2, 1, n++, g_nameBuf, 0xF);
            drawPanelGridText(2, 1, n++, "Completed", 0xF);
        } else {
            switch (g_targetSlots[i].state) {
                case 1: strcpy(g_nameBuf, "Photograph enemy"); break;
                case 2: strcpy(g_nameBuf, "Destroy enemy"); break;
                case 3: strcpy(g_nameBuf, "Drop supplies over"); break;
                case 4: strcpy(g_nameBuf, "Land supplies at"); break;
                case 5: strcpy(g_nameBuf, "Destroy AN72 flying to"); break;
                case 6: strcpy(g_nameBuf, "Plane returning to"); break;
                case 7: strcpy(g_nameBuf, "Intercept AN72 leaving"); break;
                case 8: strcpy(g_nameBuf, "Destroy plane orbiting"); break;
            }
            drawPanelGridText(2, 1, n++, g_nameBuf, (i != 0) ? 0xC : 0xE);
            buildStoreName(g_targetSlots[i].planeIndex);
            drawPanelGridText(2, 1, n++, g_nameBuf, (i != 0) ? 0xC : 0xE);
            if (g_targetSlots[i].flags & 2) {
                formatMissionClock(g_missionTimeLimit);
                strcpy(work, "");
                strcat(work, g_nameBuf);
                if (g_missionTick > g_missionTimeLimit)
                    strcpy(g_nameBuf, "Time expired");
                drawPanelGridText(2, 1, n++, work, 7);
            }
        }
    }
    drawPanelGridText(2, 1, n++, "Return to", 0xA);
    buildStoreName(g_homeBaseIdx);
    drawPanelGridText(2, 1, n++, g_nameBuf, 0xA);
    strcpy(g_nameBuf, "Sightings  V ");
    strcat(g_nameBuf, itoa(g_liveObjCount, g_itoaScratch, 10));
    strcat(g_nameBuf, "  R ");
    strcat(g_nameBuf, itoa(g_enemyGroundRemaining, g_itoaScratch, 10));
    drawPanelGridText(2, 3, 0xB, g_nameBuf, 7);
}

/* ==== seg000:0xa5c8 ==== */
int16 signOf(int16 v);                 /* sub_1D436 */
int16 abs(int16 v);                    /* libc _abs */

void drawThreatIndicator(void) {
    int16 bearing, deltaX, deltaY;
    if (g_hudVisible == 0) return;
    if (g_halfScaleRender != 0) return;
    if (g_planeTable[g_closestThreatIndex].flags & 0x800) return;
    deltaX = g_planeTable[g_closestThreatIndex].mapX - g_viewX_;
    deltaY = g_planeTable[g_closestThreatIndex].mapY - g_viewY_;
    bearing = computeBearing(deltaX, -deltaY + signOf(deltaY) * abs(deltaX));
    if (deltaY < 0) {
        deltaY = -deltaY;
        deltaX = -deltaX;
    }
    if (deltaY < 0x40) return;
    if (deltaY > 0xA00) return;
    setDrawColor(0xF);
    g_vprojXlo = clampRange(((bearing - g_ourHead) >> 8) + 0x9F, 0x8B, 0xB5);
    drawScreenLineOnePage(g_vprojXlo, 0x27, g_vprojXlo, 0x49);
    drawStringActivePage("ILS", g_vprojXlo - 6, 0x21, 0xF);
    if (g_planeTable[g_closestThreatIndex].flags & 0x200)
        g_vprojYlo = ((uint16)(g_viewZ - 0x80) >> 7) - ((abs(deltaY) - 0x18) >> 5);
    else
        g_vprojYlo = ((uint16)g_viewZ >> 7) - ((abs(deltaY) - 0x38) >> 6);
    g_vprojYlo = clampRange(g_vprojYlo, -0x10, 0x10) + 0x38;
    drawScreenLineOnePage(0x8C, g_vprojYlo, 0xB4, g_vprojYlo);
}

/* ==== seg000:0xa731 ==== */
void drawTargetView(int16 shapeId, int16 worldX, int16 worldY, int16 altitude,
                    int16 objYaw, int16 objPitch, int16 objRoll, int16 mode, int16 shift); /* sub_1CB42 */
void drawWeaponRadarInfo(int16 weaponIdx, int16 row);       /* sub_1AB2D */

void drawTargetInfoPanel(void) {
    int16 specIdx, row;

    if ((uint8)g_groundTargetLock & 0x80)
        return;
    if (g_hudVisible == 0)
        return;
    g_pageFront[1] = 4;
    g_pageBack[1] = 4;
    drawPanelText(2, "Data Base", 1);
    loadColorPalette(0);
    g_camSavedHead = g_ourHead;
    g_camSavedRoll = g_ourRoll;
    g_ourHead = g_ourRoll = 0;
    g_extViewPitch = 0xFF40;
    drawTargetView(g_planeTable[g_groundTargetLock].symbol, g_viewX_, g_viewY_ - 0xC0,
                   g_viewZ, 0x1000, 0x400, 0, 2, 0);
    g_ourHead = g_camSavedHead;
    g_ourRoll = g_camSavedRoll;
    gfx_copyRect(g_drawPage, 0xB0, 0x7C, 1 - g_drawPage, 0xB0, 0x7C, 0x68, 0x48);
    row = 6;
    buildStoreName(g_groundTargetLock);
    drawPanelGridText(2, 1, row++, g_nameBuf, 0xF);
    specIdx = g_planeTable[g_groundTargetLock].active;
    if (specIdx != 0) {
        strcpy(g_nameBuf, "Missile range- ");
        strcat(g_nameBuf, itoa(sams[specIdx].lockRange, g_itoaScratch, 10));
        strcat(g_nameBuf, " km");
        drawPanelGridText(2, 1, row++, g_nameBuf, 0xF);
        drawWeaponRadarInfo(specIdx, row);
    }
    g_pageFront[1] = 2;
    g_pageBack[1] = 2;
}

/* ==== seg000:0xa8bb ==== */
struct ObjType { char name[0x12]; int16 maxSpeed; int16 range; int16 maneuverability;
                 int16 modelId; int16 pad1A, pad1C; int16 kills; };  /* 32 bytes */

void drawAirTargetInfoPanel(void) {
    int16 specIdx, type, row;

    if ((uint8)g_airTargetLock & 0x80)
        return;
    if (g_hudVisible == 0)
        return;
    g_pageFront[1] = 4;
    g_pageBack[1] = 4;
    drawPanelText(2, "Data Base", 1);
    loadColorPalette(0);
    g_camSavedHead = g_ourHead;
    g_camSavedRoll = g_ourRoll;
    g_ourHead = g_ourRoll = 0;
    g_extViewPitch = 0xFF40;
    specIdx = g_simObjects[g_airTargetLock].spec;
    drawTargetView(g_objTypes[specIdx].pad1A, g_viewX_, g_viewY_ - 0x64,
                   g_viewZ, 0x6000, 0, 0x1000, 2, 1);
    gfx_copyRect(g_drawPage, 0xB0, 0x7C, 1 - g_drawPage, 0xB0, 0x7C, 0x68, 0x48);
    g_ourHead = g_camSavedHead;
    g_ourRoll = g_camSavedRoll;
    strcpy(g_nameBuf, g_objTypes[specIdx].name);
    strcat(g_nameBuf, g_objTypes[specIdx].name + 7);
    drawPanelGridText(2, 0xA, 1, g_nameBuf, 0xF);
    row = 6;
    if (g_simObjects[g_airTargetLock].weaponType != 0) {
        strcpy(g_nameBuf, "6 ");
        strcat(g_nameBuf, sams[g_simObjects[g_airTargetLock].weaponType].name);
        strcat(g_nameBuf, " missiles");
        drawPanelGridText(2, 1, row++, g_nameBuf, 0xF);
        strcpy(g_nameBuf, "Missile range- ");
        strcat(g_nameBuf, itoa(sams[g_simObjects[g_airTargetLock].weaponType].lockRange,
                               g_itoaScratch, 10));
        strcat(g_nameBuf, " km");
        drawPanelGridText(2, 1, row++, g_nameBuf, 0xF);
    }
    strcpy(g_nameBuf, "Max Speed- ");
    strcat(g_nameBuf, itoa(g_objTypes[specIdx].maxSpeed, g_itoaScratch, 10));
    strcat(g_nameBuf, " mph");
    drawPanelGridText(2, 1, row++, g_nameBuf, 7);
    type = g_objTypes[specIdx].modelId;
    drawWeaponRadarInfo(type, row);
    g_pageFront[1] = 2;
    g_pageBack[1] = 2;
}

/* ==== seg000:0xac4c ==== */
struct Particle { int16 posX, posY, alt, spin; };   /* stride 8 */
void computeAimProjection(int16 wx, int16 wy, int16 wz);  /* sub_1C793 */
int16 bearingToStore(int16 idx);                 /* sub_1CA74 */
int16 bearingToSimObject(int16 idx);             /* sub_1CA94 */
int16 computeTargetBearing(int16 x, int16 y, int16 f);    /* sub_1CAB4 */
void drawWorldObject(int16 shapeId, int32 worldX, int32 worldY, int16 altitude,
                     int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift);

/* Per-frame targeting HUD pass (called from the main loop at seg000:0x4d1):
 * refresh the ground/air lock candidates inside the frontal cone, then draw
 * every smoke particle, sim object, projectile and the wreck through the
 * 3D symbol pipeline; finish with the HUD targeting reticle. */
void updateTargetingHud(void) {
    int16 i, best, thresh, cur, highAlt, prox, zz;
    int8 fl;

    zz = 0;
    if (g_viewMode == 0x8B) {
        drawWorldObject(6, g_ViewX, 0x1000000L - g_ViewY, g_viewZ + 0x10,
                        g_ourHead, g_ourPitch, g_ourRoll, 2);
    }
    if (g_lockCooldown != 0)
        --g_lockCooldown;
    if ((g_groundTargetLock & 0x80) &&
        (g_curPanelMode == 0x13 || g_curPanelMode == 0x19) &&
        g_lockCooldown == 0 &&
        g_currentWeaponType != 1 &&
        !(*(int8 *)&g_viewMode & 0x80)) {
        best = 0xFFFF;
        thresh = 0x64 << (6 - g_nightMode);
        if (g_groundTargetLock != 0xFFFF) {
            i = g_groundTargetLock - 0x80;
            cur = bearingToStore(i) - 1;
            if (abs(g_ourHead + g_scanDir - g_targetBearing) > 0x2000)
                cur = 0;
        } else
            cur = 0;
        for (i = 0; i < g_storeDefCount; i++) {
            bearingToStore(i);
            if ((abs(g_ourHead + g_scanDir - g_targetBearing) < 0x2000 ||
                 (g_currentWeaponType == 0 && cur > 0)) &&
                i + 0x80 != g_groundTargetLock &&
                (fl = *(int8 *)&g_planeTable[i].flags,
                 !(fl & 0x80) && thresh > g_targetRange) &&
                cur < g_targetRange &&
                (g_currentWeaponType == 2 || (fl & 1))) {
                best = i;
                thresh = g_targetRange;
            }
        }
        if (best & 0x80) {
            if (g_groundTargetLock == 0xFFFF)
                g_lockCooldown = 4;
            else
                g_groundTargetLock = 0xFFFF;
        } else {
            g_groundTargetLock = best;
            if (g_curPanelMode == 0x19)
                g_lockMark = 1;
        }
    }
    for (i = 0; i < 8; i++) {
        if (g_particles[i].posX != 0) {
            computeAimProjection(g_particles[i].posX, g_particles[i].posY,
                                 g_particles[i].alt);
            if (g_projDepth < 0 && g_projDepth > -0x100)
                drawWorldObject((((int8)g_smokeParticleSlot - i) & 7) < 4 ? 3 : 0x11,
                                (int32)(uint16)g_particles[i].posX << 5,
                                (int32)(uint16)g_particles[i].posY << 5,
                                g_particles[i].alt, 0, g_particles[i].spin, 0, 0);
        }
    }
    thresh = 0x4B << (6 - g_nightMode);
    highAlt = (g_hudVisible != 0 && (uint16)(g_nearestThreatRange + g_viewZ) > 0x5DC) ? 1 : 0;
    if ((g_airTargetLock & 0x80) && g_airTargetLock != 0xFFFF) {
        i = g_airTargetLock - 0x80;
        cur = computeTargetBearing(g_simObjects[i].posX,
                                   g_simObjects[i].posY, 1);
        if (abs(g_ourHead + g_scanDir - g_targetBearing) > 0x2000)
            cur = 0;
    } else
        cur = 0;
    best = 0xFFFF;
    for (i = 0; i < g_groundUnitCount; i++) {
        if ((g_simObjects[i].flags.b[0] & 2) &&
            bearingToSimObject(i) < 0x12C0) {
            if (((g_airTargetLock & 0x80) || g_targetRange < 0x100) &&
                thresh > g_targetRange && cur < g_targetRange &&
                !(*(int8 *)&g_viewMode & 0x80) &&
                !(g_simObjects[i].flags.b[0] & 0x20) &&
                g_simObjects[i].speed != 0) {
                computeTargetBearing(g_simObjects[i].posX,
                                     g_simObjects[i].posY, 1);
                if (abs(g_ourHead + g_scanDir - g_targetBearing) < 0x2000) {
                    thresh = g_targetRange;
                    best = i;
                }
            }
            computeAimProjection(g_simObjects[i].posX, g_simObjects[i].posY,
                                 g_simObjects[i].alt);
            if (g_projDepth < 0) {
                g_projDepth >>= highAlt;
                if (g_projDepth > -0x20) {
                    if (g_simObjects[i].alt < 999 && g_nightMode == 0) {
                        prox = 0;
                        if ((g_planeTable[g_closestThreatIndex].flags & 0x200) &&
                            abs(g_simObjects[i].posX -
                                g_planeTable[g_closestThreatIndex].mapX) <
                                (g_threatProxX >> 5) &&
                            abs(g_simObjects[i].posY -
                                g_planeTable[g_closestThreatIndex].mapY) <
                                (g_threatProxY >> 5))
                            prox = 0x80;
                        if (g_viewZ != 0x80 || prox == 0x80)
                            drawWorldObject(5, g_simObjects[i].worldX,
                                            g_simObjects[i].worldY, prox,
                                            g_simObjects[i].heading.w, 0, 0,
                                            2 - signOf(highAlt));
                    }
                    drawWorldObject(((int16 *)&g_objTypes[g_simObjects[i].spec].pad1A)
                                    [g_projDepth > -0x10 ? 0 : 1],
                                    g_simObjects[i].worldX, g_simObjects[i].worldY,
                                    g_simObjects[i].alt, g_simObjects[i].heading.w,
                                    g_simObjects[i].pitch, g_simObjects[i].bank.w,
                                    2 - highAlt);
                } else {
                    setDrawColor(0xF);
                    drawViewportLine(g_vprojXlo, g_vprojYlo, g_vprojXlo, g_vprojYlo);
                }
            }
        }
    }
    if (best != 0xFFFF)
        g_airTargetLock = best;
    if (g_airTargetLock & 0x80)
        g_airTargetLock = 0xFFFF;
    for (i = 0; i < 0xC; i++) {
        if (g_projectiles[i].ttl != 0) {
            computeAimProjection(g_projectiles[i].mapX, g_projectiles[i].mapY,
                                 g_projectiles[i].alt);
            if (g_vprojXlo != -1) {
                if (g_projDepth > -0x20)
                    drawWorldObject(sams[g_projectiles[i].specIdx].modelId,
                                    (int32)(uint16)g_projectiles[i].mapX << 5,
                                    (int32)(uint16)g_projectiles[i].mapY << 5,
                                    g_projectiles[i].alt,
                                    g_projectiles[i].worldX,
                                    g_projectiles[i].worldY,
                                    g_projectiles[i].worldZ + 0x2000,
                                    3 - signOf(highAlt));
                else {
                    if (i < 8)
                        setDrawColor(0xC);
                    else
                        setDrawColor(0xD);
                    drawViewportLine(g_vprojXlo, g_vprojYlo, g_vprojXlo, g_vprojYlo);
                }
            }
        }
    }
    if (g_wreckAlt > 0) {
        computeAimProjection(g_wreckX, g_wreckY, g_wreckAlt);
        if (g_projDepth < 0 && g_projDepth > -0x100)
            drawWorldObject(0xE, (int32)(uint16)g_wreckX << 5, (int32)(uint16)g_wreckY << 5,
                            g_wreckAlt, 0, 0, 0, g_wreckFallVel > 0 ? 4 : 3);
    }
    if ((*(int8 *)&g_viewMode & 0x80) && g_viewMode != 0x8B &&
        (g_viewZ != 0 || g_ejectState == 0)) {
        drawWorldObject(6 + ((g_playerPlaneFlags & 1) == 0), g_ViewX,
                        0x1000000L - g_ViewY, g_viewZ + 0x10,
                        g_ourHead, g_ourPitch, g_ourRoll, 2);
        if ((uint16)g_viewZ < 0x3E8 && g_nightMode == 0)
            drawWorldObject(0x15, g_ViewX, 0x1000000L - g_ViewY, g_groundAltitude,
                            g_ourHead, 0, 0, 2);
    }
}
