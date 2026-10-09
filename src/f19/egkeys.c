#include "f19eg.h"

/* egkeys.c — sound / timescale routines (F19) */

/* ==== seg000:0xdef2 ==== */
void FAR audio_playSound(int16 id);   /* sub_2F228 thunk */
void updateEngineSound(void);         /* sub_1DF1A */

void makeSound(int16 soundId, int16 priority) {
    if (priority >= g_soundPriorityFloor) {
        if (g_ejectState == 0 || priority > 1) {
            audio_playSound(soundId);
        }
    }
    updateEngineSound();
}

/* ==== seg000:0xdf1a ==== */
void FAR audio_engineDroneOn(void);   /* sub_2F232 thunk */
void FAR audio_engineDroneOff(void);  /* sub_2F237 thunk */
void updateEngineSound(void) {
    if (g_soundPriorityFloor == 0 && g_engineThrust != 0 && g_ejectState == 0)
        goto droneOn;
    audio_engineDroneOff();
    return;
droneOn:
    audio_engineDroneOn();
}

/* ==== seg000:0xdf3c ==== */
int16 clampRange(int16 v, int16 lo, int16 hi);

void recalcTimeScale(void) {
    if (g_frameRateScaling > 15) {
        g_frameSyncWait = clampRange((-(120 / g_frameRateScaling - 9)) >> 1, 1, 4);
    } else {
        g_frameSyncWait = 0;
    }
    g_frameRateScaling = clampRange(g_frameRateScaling, 4 - g_timeAccelMode, 15);
    g_bulletTrackCount = clampRange(g_frameRateScaling << 1, 3, 16);
    g_threatTimerInit = 250 * g_frameRateScaling;
    g_threatDisplayTtl = 200 * g_frameRateScaling;
}

/* ==== seg000:0xdfb4 ==== */

void setupLodDistances(void) {
    int16 lod;
    for (lod = 0; lod < 6; lod++) {
        ((int16 *)(colorLut + 0x10))[lod] = 0x20 << ((char)lod + (char)g_detailLevel);
    }
    g_lodDistNear = g_lodDistScale + g_lodDistBase;
    g_lodDistFar = clampRange(g_lodDistScale << 1, 0x1000, 9999);
    *(int16 *)(colorLut + 0x20) = g_detailLevel * 0xD05 + 0xD05;
}

/* ==== seg000:0xe010 ==== */
void exitTimeAccel(void) {
    if (g_timeAccelMode == 2) {
        g_timeAccelMode = 1;
        g_frameRateScaling <<= 1;
        recalcTimeScale();
    }
}

/* ==== seg000:0xe026 ==== */
struct StoreDef { int16 subIdx; uint16 coordX; uint16 coordY; int16 f6; int8 flags; int8 f9; int16 padA; int16 padC; int16 nameIdx; };

void copyStoreToWaypoint(int16 dst, int16 src) {
    waypoints[dst * 2] = g_storeDefs[src].coordX;
    waypoints[dst * 2 + 1] = g_storeDefs[src].coordY;
}

/* ==== seg000:0xd4c6 ==== */
struct CommData { int8 pad26[0x26]; int16 landingType; int8 pad2[8]; int8 trainingFlag;
                  int8 pad3[0x41]; int16 setupUseJoy; };
struct TargetSlot { int16 state, planeIndex, viewIndex, flags, seedNoise, pad[4]; };

void initFlightParams(void);            /* sub_14BF8 */
void countermeasures(int16);            /* sub_147F1 */
void zoomIn(void);                      /* sub_18A1B */
void zoomOut(void);                     /* sub_18A54 */
void drawStatusItem(int16, int16);      /* sub_19007 */
void exitTimeAccel(void);               /* sub_1E010 */
void setupLodDistances(void);           /* sub_1DFB4 */
void updateEngineSound(void);           /* sub_1DF1A */
void recalcTimeScale(void);             /* sub_1DF3C */
void hudMessage(const char *);          /* sub_192B1 */
void initWeaponLoadout(void);           /* sub_14C05 */
void updateStatusPanel(int16);          /* sub_186FC */
void drawPanelModeText(int16);          /* sub_18651 */
void updatePanelMode(int16);            /* sub_18610 */
int16  readAxisInput(int16);            /* sub_1D484 */
void fireMissile(void);                 /* sub_17BA0 */
void drawLoadoutPanel(void);            /* sub_19E4F */
void makeSound(int16, int16);           /* sub_1DEF2 */
int16  randomRange(int16);              /* sub_1D46B */
void commitCommSnapshot(int16);         /* sub_14C3C */
void drawAirTargetInfoPanel(void);      /* sub_1A8BB */
void drawTargetInfoPanel(void);         /* sub_1A731 */
void drawWaypointPanel(void);           /* sub_19979 */
void copyStoreToWaypoint(int16, int16); /* sub_1E026 */
int16  clampRange(int16, int16, int16); /* sub_1D1FA */
int16  abs(int16);
char  *strcpy(char *, const char *);
char  *strcat(char *, const char *);
char  *itoa(int16, char *, int16);

void keyDispatch(uint16 scanCode) {
    int16 wpStep, u2;

    g_axisInputAccum[0] = g_axisInputAccum[1] = 0;
    if (scanCode == 0) goto endDispatch;
    switch (scanCode) {
    case 0x316E:                                    /* 'n' */
        ((int8 *)&g_playerPlaneFlags)[1] ^= 1;
        g_scanDir = 0;
        if (g_curPanelMode == 0x13) goto scanApply;
        break;
    case 0x2C7A:                                    /* 'z' */
        zoomIn();
        break;
    case 0x2D78:                                    /* 'x' */
        zoomOut();
        break;
    case 0x7D:
        initFlightParams();
        break;
    case 0x534:                                     /* '4' */
        *(int8 *)&g_playerPlaneFlags ^= 0x10;
        drawStatusItem(5, (*(int8 *)&g_playerPlaneFlags & 0x10) ? 0xA : 0);
        break;
    case 0x433:                                     /* '3' */
        *(int8 *)&g_playerPlaneFlags ^= 0x20;
        break;
    case 0x231:                                     /* '1' */
        countermeasures(1);
        break;
    case 0x332:                                     /* '2' */
        countermeasures(2);
        break;
    case 0x635:                                     /* '5' */
        countermeasures(3);
        break;
    case 0x736:                                     /* '6' — landing gear */
        if (g_viewZ != g_groundAltitude)
            *(int8 *)&g_playerPlaneFlags ^= 1;
        if (!(*(int8 *)&g_playerPlaneFlags & 1))
            goto exitAccel;
        break;
    case 0x2D58:                                    /* 'X' */
    exitAccel:
        exitTimeAccel();
        break;
    case 0xA39:                                     /* '9' */
        *(int8 *)&g_playerPlaneFlags ^= 2;
        break;
    case 0x938:                                     /* '8' */
        *(int8 *)&g_playerPlaneFlags ^= 4;
        drawStatusItem(2, (*(int8 *)&g_playerPlaneFlags & 4) ? 0xE : 0);
        break;
    case 0x2000:                                    /* Alt-D — detail level */
        if (--g_detailLevel < 0)
            g_detailLevel = 2;
        strcpy(strBuf, "Detail Level ");
        strcat(strBuf, itoa(g_detailLevel, g_itoaScratch, 10));
        hudMessage(strBuf);
        setupLodDistances();
        break;
    case 0x5200:                                    /* sensitivity */
        if (++g_joySensitivity > 2)
            g_joySensitivity = 0;
        strcpy(strBuf, "Kybd Sensitivity");
        strcat(strBuf, itoa(g_joySensitivity + 1, g_itoaScratch, 10));
        hudMessage(strBuf);
        break;
    case 0x2C5A:                                    /* 'Z' — time accel on */
        if (g_timeAccelMode == 1) {
            g_timeAccelMode = 2;
            g_frameRateScaling = g_frameRateScaling / 2;
            recalcTimeScale();
        }
        break;
    case 0x2F00:                                    /* Alt-V — sound level */
        g_soundPriorityFloor = (g_soundPriorityFloor + 1) & 3;
        strcpy(strBuf, "Sounds ");
        strcat(strBuf, itoa(3 - g_soundPriorityFloor, g_itoaScratch, 10));
        hudMessage(strBuf);
        updateEngineSound();
        break;
    case 0x1400:                                    /* Alt-T — training mode */
        ((int8 *)&g_playerPlaneFlags)[1] ^= 0x10;
        if (g_playerPlaneFlags & 0x1000)
            commData->trainingFlag |= 1;
        break;
    case 0x3920:                                    /* space — weapon spec cycle */
        missileSpecIndex = (missileSpecIndex + 1) & 3;
        if (g_curPanelMode == 0x15)
            drawLoadoutPanel();
        break;
    case 0x837:                                     /* '7' — autopilot toggle */
        if (g_autopilotAltitude != 0) {
            g_autopilotAltitude = 0;
        } else {
            g_autopilotAltitude = g_viewZ < 0x1F4 ? 0x1F4 : g_viewZ;
        }
        break;
    case 0x3062:                                    /* 'b' — lock both targets */
        *(int8 *)&g_groundTargetLock |= 0x80;
        *(int8 *)&g_airTargetLock   |= 0x80;
        break;
    case 0xE08:                                     /* backspace */
        g_axisInputAccum[0] = 1;
        break;
    case 0x1C0D:                                    /* enter */
        g_axisInputAccum[1] = 1;
        break;
    case 0x3B00:                                    /* F1 — cockpit view */
        g_viewMode = 0;
        break;
    case 0x353F:                                    /* '?' */
        g_viewMode = 0x44;
        break;
    case 0x343E:                                    /* '>' */
        g_viewMode = 0x41;
        break;
    case 0x333C:                                    /* '<' */
        g_viewMode = 0x43;
        break;
    case 0x324D:                                    /* 'M' */
        g_viewMode = 0x42;
        break;
    case 0x326D:                                    /* 'm' */
        g_scanDir = 0xC000;
        goto scanApply;
    case 0x352F:                                    /* '/' */
        g_scanDir = 0;
        goto scanApply;
    case 0x332C:                                    /* ',' */
        g_scanDir = 0x4000;
        goto scanApply;
    case 0x342E:                                    /* '.' */
        g_scanDir = 0x8000;
    scanApply:
        if (g_curPanelMode != 0x19)
            g_groundTargetLock = g_airTargetLock = -1;
        drawPanelModeText(0x13);
        break;
    case 0x5500:
        g_viewMode = 0x84;
        break;
    case 0x5600:
        g_viewMode = 0x85;
        break;
    case 0x5400:
        g_viewMode = 0x87;
        break;
    case 0x5700:
        g_viewMode = 0x89;
        break;
    case 0x5800:
        g_viewMode = 0x88;
        break;
    case 0x5900:
        g_viewMode = 0x8B;
        break;
    case 0x5D00:                                    /* eject */
        if (g_ejectState == 0) {
            makeSound(2, 2);
            if ((abs(g_ourRoll) >> 5) + (abs(g_ourPitch) >> 5) + g_knots >
                randomRange(300) + 500) {
                commitCommSnapshot(6);
            } else {
                commData->landingType = 2;
            }
            g_ejectState = 1;
            g_crashCamX = g_viewX_;
            g_crashCamY = g_viewY_;
            g_crashCamZ = g_viewZ + 8;
        }
        break;
    case 0x3C00:                                    /* F2 — weapon type cycle */
        switch (g_currentWeaponType) {
        case 2:
            g_currentWeaponType = 0;
            break;
        case 0:
            g_currentWeaponType = 1;
            g_groundTargetLock = g_airTargetLock = -1;
            break;
        case 1:
            g_currentWeaponType = 2;
            g_groundTargetLock = -1;
            break;
        }
        break;
    case 0x3D00:                                    /* F3 */
        if (g_mapMode == 1)
            updatePanelMode(0);
        else
            updatePanelMode(1);
        break;
    case 0x3E00:                                    /* F4 — target panel */
        g_curPanelMode = 0x19;
        if (g_currentWeaponType == 1) {
            drawAirTargetInfoPanel();
        } else {
            drawTargetInfoPanel();
        }
        break;
    case 0x3F00:                                    /* F5 */
        drawPanelModeText(0x15);
        break;
    case 0x4000:                                    /* F6 */
        drawPanelModeText(0x16);
        break;
    case 0x4100:                                    /* F7 — waypoint select mode */
        g_wpPanelMode = 1;
        drawPanelModeText(0x14);
        break;
    case 0x4200:                                    /* F8 — waypoint change mode */
        g_wpPanelMode = 0;
        g_wpSelectIdx = waypointIndex;
        drawPanelModeText(0x14);
        break;
    case 0x5B00:
        copyStoreToWaypoint(0, g_targetSlots[0].viewIndex);
        copyStoreToWaypoint(1, g_targetSlots[0].planeIndex);
        copyStoreToWaypoint(2, g_targetSlots[1].planeIndex);
        copyStoreToWaypoint(3, g_targetSlots[1].viewIndex);
        waypoints[0] = (uint16)(waypoints[0] + waypoints[2]) >> 1;
        waypoints[1] = (uint16)(waypoints[1] + waypoints[3]) >> 1;
        if (g_curPanelMode == 0x14)
            drawWaypointPanel();
        break;
    case 0x4300:                                    /* F9 */
        ((int8 *)&g_playerPlaneFlags)[1] ^= 4;
        break;
    case 0x4400:                                    /* F10 */
        drawPanelModeText(0x18);
        break;
    }
    if (g_playerPlaneFlags & 0x1000) {
        switch (scanCode) {
        case 0x3100:
            *(int8 *)&g_nightMode ^= 1;
            break;
        case 0x1300:
            initWeaponLoadout();
            updateStatusPanel(0x16);
            break;
        case 0x1700:
            g_ViewY += 0x20000L >> g_mapZoomLevel;
            break;
        case 0x2500:
            g_ViewY -= 0x20000L >> g_mapZoomLevel;
            break;
        case 0x2400:
            g_ViewX -= 0x20000L >> g_mapZoomLevel;
            break;
        case 0x2600:
            g_ViewX += 0x20000L >> g_mapZoomLevel;
            break;
        }
    }
    if (g_curPanelMode == 0x14) {
        wpStep = (g_wpPanelMode != 0) ? 0 : 0x1000;
        switch (scanCode) {
        case 0x4800:
            if (commData->setupUseJoy == 0) goto wpDone;
            goto wpUp;
        case 0x4838:
        wpUp:
            waypoints[g_wpSelectIdx * 2 + 1] -= wpStep >> g_mapZoomLevel;
        wpRedraw:
            drawPanelModeText(0x14);
            goto wpDone;
        case 0x5000:
            if (commData->setupUseJoy == 0) goto wpDone;
        case 0x5032:
            waypoints[g_wpSelectIdx * 2 + 1] += wpStep >> g_mapZoomLevel;
            goto wpRedraw;
        case 0x4B00:
            if (commData->setupUseJoy == 0) goto wpDone;
        case 0x4B34:
            waypoints[g_wpSelectIdx * 2] -= wpStep >> g_mapZoomLevel;
            goto wpRedraw;
        case 0x4D00:
            if (commData->setupUseJoy == 0) goto wpDone;
        case 0x4D36:
            waypoints[g_wpSelectIdx * 2] += wpStep >> g_mapZoomLevel;
            goto wpRedraw;
        case 0x4900:
            if (commData->setupUseJoy == 0) goto wpDone;
        case 0x4939:
            if (g_wpPanelMode != 0) {
                --waypointIndex;
        wpIdxWrap:
                waypointIndex = (int16)waypointIndex & 3;
                goto wpRedraw;
            } else {
                --g_wpSelectIdx;
        wpSelWrap:
                g_wpSelectIdx = (int16)g_wpSelectIdx & 3;
                goto wpRedraw;
            }
        case 0x5100:
            if (commData->setupUseJoy == 0) goto wpDone;
        case 0x5133:
            if (g_wpPanelMode != 0) {
                ++waypointIndex;
                goto wpIdxWrap;
            } else {
                ++g_wpSelectIdx;
                goto wpSelWrap;
            }
        case 0x4F00:
            g_missionTick += 0x3C;
            goto wpDone;
        case 0x4700:
            g_missionTick -= 0x3C;
            goto wpDone;
        default:
            goto wpDone;
        }
    wpDone:
        waypoints[g_wpSelectIdx * 2] =
            clampRange(waypoints[g_wpSelectIdx * 2], 0, 0x7FFF);
        waypoints[g_wpSelectIdx * 2 + 1] =
            clampRange(waypoints[g_wpSelectIdx * 2 + 1], 0, 0x7FFF);
    }
    if (g_ejectState != 0)
        g_viewMode = 0x8C;
endDispatch:
    if (g_fireCooldown > 0)
        g_fireCooldown--;
    if (readAxisInput(1) != 0 && g_fireCooldown == 0) {
        fireMissile();
        if (g_curPanelMode == 0x15 && g_hudVisible != 0)
            drawLoadoutPanel();
        g_fireCooldown = 4;
    }
    if (g_weaponMask & 0x40) {
        *(int8 *)&g_playerPlaneFlags &= 0xCF;
        drawStatusItem(5, 0);
        drawStatusItem(6, 0);
    }
    if (g_playerPlaneFlags & 0x20) {
        if (g_frameRateScaling * 0x14 < ++g_smokeTimer)
            *(int8 *)&g_playerPlaneFlags &= 0xDF;
    } else if (g_smokeTimer > 0) {
        g_smokeTimer--;
    }
    drawStatusItem(6, (g_playerPlaneFlags & 0x20) ? 0xA : (g_smokeTimer != 0) ? 4 : 0);
    if (g_weaponMask & 0x10)
        *(int8 *)&g_playerPlaneFlags |= 4;
    if (g_weaponMask & 8)
        g_autopilotAltitude = 0;
    if (g_curPanelMode == 0x14 && !(*(int8 *)&frameTick & 0x1F) && g_hudVisible != 0)
        updateStatusPanel(0x14);
    drawStatusItem(2, (*(int8 *)&g_playerPlaneFlags & 4) &&
                      (*(int8 *)&frameTick & 1) ? 0xE : 0);
    drawStatusItem(3, ((*(int8 *)&g_playerPlaneFlags & 1) == 0 &&
                      (g_knots < 0xFA || (*(int8 *)&frameTick & 1))) ? 0xA : 0);
}
