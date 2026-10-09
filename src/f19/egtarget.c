#include "f19eg.h"
#include "f19egvars.h"

/* egtarget.c — target bearing/label routines (F19) */

void setDrawColor(int16 c);                        /* sub_18F9C */
void drawTargetBox(int16 x, int16 y, int16 size, int16 a); /* sub_1C4FC */
void drawStringActivePage(const char *text, int16 x, int16 y, int16 color); /* sub_191E5 */
int16 computeBearing(int16 dx, int16 dy);          /* sub_1D29D */
int16 rangeApprox(int16 dx, int16 dy);             /* sub_1D23B (egmath.c) */
void drawStringBothPages(const char *text, int16 x, int16 y, int16 color);


/* vtxScratch.vproj.x.lo / y.lo */

struct MapTarget {                 /* F19 layout, 16 bytes */
    int16 objType;                 /* +0 */
    uint16 mapX;                   /* +2 */
    uint16 mapY;                   /* +4 */
    int16 nameIndex;               /* +6 (egcombat calls this "active") */
    int16 flags;                   /* +8 */
    int16 alertLevel;              /* +A */
    int16 threatTimer;             /* +C */
    int16 symbol;                  /* +E */
};
struct PlaneTable { int16 lead[3]; struct MapTarget planes[74]; };
#undef g_planeTable
#define g_planeTable (*(struct PlaneTable *)((uint8 *)f19_egSpace.m_g_camRotMatrix + 12))

/* ==== seg000:0xb2ca ==== */
int16 isqrt(int16 value);                                /* sub_13387 */
void computeAimProjection(int16 wx, int16 wy, int16 wz); /* sub_1C793 */
void drawViewportLine(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18D9A */
void drawHudViewLine(int16 x1, int16 y1, int16 x2, int16 y2);  /* sub_18F38 */
void drawTargetBox(int16 x, int16 y, int16 size, int16 a);     /* sub_1C4FC */
void drawLockReticle(void);                              /* sub_1C60C */
void drawTargetLabel(const char *text, int16 color, int16 size); /* sub_1C681 */
void buildRangeString(int16 rangeRaw);                   /* sub_1C719 */
int16 findStoreAtGrid(int16 gridX, int16 gridY);         /* sub_1C9B2 */
int16 bearingToStore(int16 i);                           /* sub_1CA74 */
int16 hudPitchScale(void);                               /* sub_1CAF2 */
int16 getStoreMapCode(int16 idx);                        /* sub_1CB1A */
void drawTargetView(int16 shapeId, int16 worldX, int16 worldY,
                    int16 altitude, int16 objYaw, int16 objPitch,
                    int16 objRoll, int16 mode, int16 shift);     /* sub_1CDDE */
int16 clampRange(int16 v, int16 lo, int16 hi);           /* sub_1D1FA */
int16 rangeApprox(int16 dx, int16 dy);                   /* sub_1D23B */
int16 sinMul(int16 angle, int16 mult);                   /* sub_1D3EC */
int16 cosMul(int16 angle, int16 mult);                   /* sub_1D404 */
int16 signOf(int16 value);                               /* sub_1D436 */
int16 randomRange(int16 range);                          /* sub_1D46B */
int16 readAxisInput(int16 axis);                         /* sub_1D484 */
void makeSound(int16 a, int16 b);                        /* sub_1DEF2 */
void destroySimObject(int16 idx);                        /* sub_17800 */
void destroyGroundTarget(int16 planeIdx);                /* sub_1790E */
int16 markTargetReached(int16 n);                        /* sub_17AAF */
void bombTarget(void);                                   /* sub_17B46 */
void hudMessage(const char *s);                          /* sub_192B1 */
int16 getWeaponStat(int16 statIdx, int16 target);        /* sub_192CD */
void blitSprite(int16 destX, int16 destY, int16 srcX, int16 srcY,
                int16 width, int16 height, int16 transparent);   /* sub_19912 */
void drawTargetInfoPanel(void);                          /* sub_1A731 */
void loadColorPalette(int16 idx);                        /* sub_10504 */
void buildStoreName(int16 i);                            /* sub_14D03 */
void recordFrame(uint8 a, uint8 b);                      /* sub_14CAF */
void far gfx_copyRect(int16 src, int16 sx, int16 sy, int16 dst,
                      int16 dx, int16 dy, int16 w, int16 h);     /* sub_2F0FC */
int16 FAR fillSpanRect(const int16 *, int16, int16, int16, int16); /* sub_21A58 */
void fillPanelBox(int16 panelId, int16 color);           /* sub_190E8 */
void drawFullscreenLine(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18D71 */


struct BulletTrack { int16 posX, posY, alt, velX, velY, velZ; };
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
};
struct ObjType { char name[0x12]; int16 maxSpeed; int16 range; int16 maneuverability;
                 int16 modelId; int16 pad1A, pad1C; int16 kills; };
struct MissileSpec { int16 weaponIdx; int16 ammo; };
struct Missile { char shortName[10]; char longName[12]; int16 specIndex; int16 weaponCategory; };
struct Sam { char name[8]; int16 lockRange, maxSpeed, weaponClass, turnRate, modelId; };
struct TargetSlot { int16 state, planeIndex, viewIndex, flags, seedNoise, pad[4]; };
struct TileObject { int16 id; int16 dist; int32 x; int32 y; int16 entry;
                    uint8 lod, subIndex, tileX, tileY; int16 shapeOff; };

/* HUD world overlay: tracer-bullet tracks, ground hit effects, weapon
 * lock corridors, threat labels, target camera view and reticles.
 * Called once per frame from render3DView. */
void drawHudWorldOverlay(void) {
    int16 p, prevDepth, lockFlag, wpEntry, z, tmpi, hitFlag;
    int16 specD, padz, pitchw, loftDistf, pad3w, specd, pad4x;
    int16 idxj, objIdxo, radiusg, pointXc, pointYb, wpIdxc, distk;
    int16 prevXj, gunRadiusa, compatk, prevYi;

    g_prevKillMarker = g_targetInHudFlag;
    g_targetInHudFlag = 0;
    gunRadiusa = 0x17a / isqrt(g_frameRateScaling * 4 + 8);

    for (idxj = 0; idxj < g_bulletTrackCount + 4; idxj++) {
        if (bulletTracks[idxj].posX != 0) {
            computeAimProjection(bulletTracks[idxj].posX, bulletTracks[idxj].posY,
                                 bulletTracks[idxj].alt);
            prevXj = g_vprojXlo;
            prevYi = g_vprojYlo;
            prevDepth = g_projDepth;
            computeAimProjection((bulletTracks[idxj].velX >> 1) + bulletTracks[idxj].posX,
                                 (bulletTracks[idxj].velY >> 1) + bulletTracks[idxj].posY,
                                 (bulletTracks[idxj].velZ >> 1) + bulletTracks[idxj].alt);
            if (g_vprojXlo != -1) {
                if (prevXj != -1) {
                    distk = ((frameTick >> 1) - idxj) & 7;
                    setDrawColor(idxj < g_bulletTrackCount ? 0x0d : 0x0c);
                    drawViewportLine(g_vprojXlo, g_vprojYlo, prevXj, prevYi);
                    hitFlag = 0;
                    if (idxj < g_bulletTrackCount) {
                        for (objIdxo = 0; objIdxo < g_groundUnitCount; objIdxo++) {
                            if ((g_simObjects[objIdxo].flags.b[0] & 2) != 0) {
                                distk = (abs(bulletTracks[idxj].alt -
                                             g_simObjects[objIdxo].alt) >> 5) +
                                        abs(bulletTracks[idxj].posX -
                                            g_simObjects[objIdxo].posX) +
                                        abs(bulletTracks[idxj].posY -
                                            g_simObjects[objIdxo].posY);
                                distk = abs(distk);
                                if (gunRadiusa / (g_missionStatus + 1) > distk) {
                                    hitFlag = 1;
                                    g_simObjects[objIdxo].flags.b[0] |= 0x10;
                                    destroySimObject(objIdxo);
                                    strcat(strBuf, " destroyed by gunfire");
                                    hudMessage(strBuf);
                                    g_hitEffectTimer = 8;
                                }
                            }
                        }
                    } else {
                        distk = (abs(bulletTracks[idxj].alt - g_viewZ) >> 5) +
                                abs(bulletTracks[idxj].posX - g_viewX_) +
                                abs(bulletTracks[idxj].posY - g_viewY_);
                        distk = abs(distk);
                        if (distk < 0x20) {
                            hitFlag = 1;
                            hudMessage("Hit by gunfire");
                            if (0x20 / (4 - g_missionStatus) > distk) {
                                bombTarget();
                            }
                        }
                    }
                    if (hitFlag != 0) {
                        g_hitMapX = bulletTracks[idxj].posX;
                        g_hitMapY = bulletTracks[idxj].posY;
                        g_hitAlt = bulletTracks[idxj].alt;
                        g_hitEffectTimer = -1;
                        bulletTracks[idxj].posX = 0;
                    }
                    if (bulletTracks[idxj].alt < 0) {
                        if (g_hitEffectTimer <= 0) {
                            g_hitMapX = bulletTracks[idxj].posX;
                            g_hitMapY = bulletTracks[idxj].posY;
                            g_hitAlt = bulletTracks[idxj].alt;
                            g_hitEffectTimer = -1;
                        }
                        bulletTracks[idxj].posX = 0;
                        wpEntry = findStoreAtGrid(g_hitMapX, g_hitMapY);
                        if (wpEntry != -1 && !(g_planeTable.planes[wpEntry].flags & 0x80)) {
                            pointXc = (int16)(g_nearestTileObj->x >> 5);
                            pointYb = -((int16)(g_nearestTileObj->y >> 5) - 0x8000);
                            if ((getWeaponStat(0x12, wpEntry) * 6) / (g_missionStatus + 2) >
                                rangeApprox(g_hitMapX - pointXc, g_hitMapY - pointYb)) {
                                destroyGroundTarget(wpEntry);
                                strcat(strBuf, " destroyed by gunfire");
                                hudMessage(strBuf);
                                g_hitEffectTimer = 8;
                                g_hitAlt = 0;
                            }
                        }
                    }
                }
            }
        }
    }
    if (g_hitEffectTimer != 0) {
        computeAimProjection(g_hitMapX, g_hitMapY, g_hitAlt);
        if (g_vprojXlo != -1) {
            radiusg = abs(0x100 / g_projDepth);
            for (idxj = 0; idxj < 8; idxj++) {
                setDrawColor(randomRange(4) + 0x0c);
                if (g_hitAlt > 0) {
                    drawViewportLine(g_vprojXlo, g_vprojYlo,
                                     randomRange(radiusg << 1) - radiusg + g_vprojXlo,
                                     randomRange(radiusg << 1) - radiusg + g_vprojYlo);
                } else {
                    tmpi = randomRange(0x6000) - 0x3000;
                    if (g_hudVisible != 0) {
                        tmpi -= g_ourRoll;
                    }
                    prevDepth = randomRange(radiusg);
                    prevXj = sinMul(tmpi, prevDepth) + g_vprojXlo;
                    prevYi = g_vprojYlo - cosMul(tmpi, prevDepth);
                    drawViewportLine(g_vprojXlo, g_vprojYlo, prevXj, prevYi);
                }
            }
        }
        g_hitEffectTimer -= signOf(g_hitEffectTimer);
    } else {
        g_lockedTargetKilled = 0;
    }
    if (g_hudVisible == 0) {
        return;
    }
    if (g_lockMark != 0) {
        drawTargetInfoPanel();
        g_lockMark = 0;
    }
    loadColorPalette(g_nightMode != 0 ? 2 : g_nightMode);
    setDrawColor(0xf);
    drawFullscreenLine(0x13f, 0xc7, 0x13f, 0xc7);
    g_targetLock = 0;
    if (g_currentWeaponType == 2 && g_viewMode == 0 && g_groundTargetLock >= 0) {
        computeAimProjection(g_planeTable.planes[g_groundTargetLock].mapX,
                             g_planeTable.planes[g_groundTargetLock].mapY, 0);
        specd = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;
        if (specd == 0x1c &&
            bearingToStore(g_groundTargetLock) - ((uint16)g_viewZ >> 5) * 5 < 0 &&
            g_projDepth < 0) {
            g_targetLock = 1;
        }
        if (g_vprojXlo != -1) {
            setDrawColor(0xf);
            lockFlag = 0;
            compatk = getWeaponStat(missleSpec[missileSpecIndex].weaponIdx, g_groundTargetLock);
            if (compatk != 0) {
                if (missleSpec[missileSpecIndex].ammo != 0 &&
                    (rangeApprox(g_vprojXlo - 0xa0, g_vprojYlo - 0x38) < 0x30 || g_targetLock != 0) &&
                    -g_projDepth / 7 < sams[specd].lockRange &&
                    sams[specd].weaponClass != 7 &&
                    (sams[specd].weaponClass != 0x1c || g_targetLock != 0)) {
                    g_targetLock = 1;
                    lockFlag = 1;
                    if (sams[specd].lockRange > (-g_projDepth >> 1 >> 1)) {
                        setDrawColor(0xc);
                    }
                }
            } else {
                if (specd != -1) {
                    setDrawColor(g_nightMode != 0 ? 6 : 0);
                }
                g_targetLock = 0;
            }
            drawTargetBox(g_vprojXlo, g_vprojYlo, compatk != 0 ? compatk + 5 : 9, lockFlag);
        }
    }
    if (g_scopeSweepTimer > 0 && g_threatLabelTarget >= 0) {
        computeAimProjection(g_planeTable.planes[g_threatLabelTarget].mapX,
                             g_planeTable.planes[g_threatLabelTarget].mapY, 0);
        drawTargetLabel(g_nameTab[g_planeTable.planes[g_threatLabelTarget].objType],
                        g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }
    g_playerPlaneFlags &= ~0x200;
    g_pageFront[1] = 4;
    g_pageBack[1] = 4;
    if (g_curPanelMode == 0x13 && (g_currentWeaponType == 2 || g_currentWeaponType == 0)) {
        if ((g_playerPlaneFlags & 0x100) != 0) {
            pitchw = g_ourPitch;
            wpEntry = -1;
            if (pitchw < 0) {
                pointXc = cosMul(pitchw, g_viewZ) / (sinMul(-pitchw, 0x20) + 1);
            } else {
                pointXc = 0x280;
            }
            pointYb = g_viewY_ - cosMul(g_ourHead, pointXc);
            pointXc = sinMul(g_ourHead, pointXc) + g_viewX_;
            wpEntry = findStoreAtGrid(pointXc, pointYb);
            if (wpEntry != -1) {
                g_groundTargetLock = wpEntry;
                if (g_smokeSourceIdx == 0) {
                    g_smokeSourceIdx = -1;
                }
                g_playerPlaneFlags &= ~0x100;
            }
        }
        if (g_lgbTimer != 0) {
            --g_lgbTimer;
        }
        if (g_groundTargetLock != -1) {
            wpIdxc = g_groundTargetLock & 0x7f;
            if (missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex == -1 &&
                (g_playerPlaneFlags & 4) != 0) {
                g_playerPlaneFlags |= 0x200;
                g_targetInHudFlag = 1;
                if (g_lgbTimer == 0 && g_drawPage != 0 &&
                    (readAxisInput(1) != 0 || g_axisInput1 != 0)) {
                    g_lgbTimer = 4;
                    setDrawColor(0);
                    fillSpanRect(g_pageBack, 0xb0, 0x7c, 0x118, 0xc4);
                    strcpy(strBuf, "Frame ");
                    strcat(strBuf, itoa(++g_lgbCount, g_itoaScratch, 10));
                    drawStringActivePage(strBuf, 0xd8, 0x9c, 0xf);
                    makeSound(0x1e, 2);
                }
                if (g_lgbTimer == 0) {
                    g_extViewPitch = g_ourPitch - 0x6ef;
                    drawTargetView(getStoreMapCode(wpIdxc),
                                   g_planeTable.planes[wpIdxc].mapX,
                                   g_planeTable.planes[wpIdxc].mapY, 0, 0, 0, 0, 3, 0);
                }
                if (g_lgbTimer == 3) {
                    gfx_copyRect(g_pageFront[0], 0xb0, 0x7c, g_pageBack[0], 0xb0, 0x7c, 0x68, 0x48);
                    for (idxj = 0; idxj < 2; idxj++) {
                        if (g_targetSlots[idxj].state == 1 &&
                            g_targetSlots[idxj].planeIndex == wpIdxc &&
                            !(g_planeTable.planes[wpIdxc].flags & 0x80)) {
                            loftDistf = bearingToStore(wpIdxc);
                            if ((abs(g_targetBearing - g_ourHead) >> 4) +
                                (abs(computeBearing(-((uint16)g_viewZ >> 5), loftDistf) - g_ourPitch + 0x6ef) >> 4) +
                                loftDistf <
                                -(g_missionStatus * 2 - 8) << 6) {
                                hudMessage("Target photographed.");
                                if ((g_playerPlaneFlags & (0x4000 >> idxj)) == 0) {
                                    markTargetReached(idxj);
                                    recordFrame((idxj != 0 ? 0x40 : 0x80) + 10, wpIdxc);
                                }
                            }
                        }
                    }
                }
            } else {
                drawTargetView(getStoreMapCode(wpIdxc),
                               g_planeTable.planes[wpIdxc].mapX,
                               g_planeTable.planes[wpIdxc].mapY, 0, 0, 0, 0, 1, -1);
                drawLockReticle();
                buildRangeString(bearingToStore(wpIdxc));
                drawStringActivePage(strBuf, 0xbc, 0xbc, 0xf);
                buildStoreName(wpIdxc);
                drawStringActivePage(strBuf, -((int16)strlen(strBuf) * 2 - 0xe4), 0x82, 0xf);
                if (g_currentWeaponType == 0) {
                    computeAimProjection(g_planeTable.planes[g_groundTargetLock].mapX,
                                         g_planeTable.planes[g_groundTargetLock].mapY, 0);
                    setDrawColor(0xf);
                    drawTargetBox(g_vprojXlo, g_vprojYlo, 8, 0);
                } else if (g_targetSlots[0].planeIndex == g_groundTargetLock &&
                           g_targetSlots[0].state < 5) {
                    drawStringActivePage("Primary Target", 0xc8, 0x88, 0xf);
                } else if (g_targetSlots[1].planeIndex == g_groundTargetLock) {
                    drawStringActivePage("Secondary Target", 0xc4, 0x88, 0xf);
                } else if (!(frameTick & 1) &&
                           ((g_difficultyTier < 2 &&
                             (g_shapeTargetCategory[g_planeTable.planes[wpIdxc].symbol & 0x7f] & 0xc0) != 0) ||
                            (g_planeTable.planes[wpIdxc].flags & 0x500) != 0 ||
                            (g_mapCellFlags[(g_planeTable.planes[wpIdxc].mapX >> 0xb) +
                                            ((g_planeTable.planes[wpIdxc].mapY >> 0xb) << 4)] & 1) != 0)) {
                    drawStringActivePage("Civilian or Friendly", 0xbc, 0x88, 0xf);
                }
            }
        }
    }
    g_axisInput1 = readAxisInput(1);
    if (g_currentWeaponType == 1 && g_viewMode == 0 && !(g_airTargetLock & 0x80)) {
        computeAimProjection(g_simObjects[g_airTargetLock].posX,
                             g_simObjects[g_airTargetLock].posY,
                             g_simObjects[g_airTargetLock].alt);
        if (g_vprojXlo != -1) {
            setDrawColor(g_nightMode != 0 ? 6 : 0);
            lockFlag = 0;
            specd = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;
            if (missleSpec[missileSpecIndex].ammo != 0 && sams[specd].weaponClass == 7) {
                setDrawColor(0xf);
                if (rangeApprox(g_vprojXlo - 0xa0, g_vprojYlo - 0x38) < 0x30 &&
                    (-g_projDepth >> 3) < sams[specd].lockRange) {
                    g_targetLock = 1;
                    lockFlag = 1;
                    if (((-g_projDepth) >> 1 >> 1) < sams[specd].lockRange) {
                        setDrawColor(0xc);
                    }
                }
            }
            drawTargetBox(g_vprojXlo, g_vprojYlo, 9, lockFlag);
        }
    }
    if (g_curPanelMode == 0x13 && g_currentWeaponType == 1 && g_airTargetLock != -1) {
        wpIdxc = g_airTargetLock & 0x7f;
        drawTargetView(g_objTypes[g_simObjects[wpIdxc].spec].pad1A,
                       g_simObjects[wpIdxc].posX, g_simObjects[wpIdxc].posY,
                       g_simObjects[wpIdxc].alt, g_simObjects[wpIdxc].heading.w,
                       g_simObjects[wpIdxc].pitch, g_simObjects[wpIdxc].bank.w, 1, 1);
        drawLockReticle();
        buildRangeString(rangeApprox(g_viewX_ - g_simObjects[wpIdxc].posX,
                                     g_viewY_ - g_simObjects[wpIdxc].posY));
        drawStringActivePage(strBuf, 0xbc, 0xbc, 0xf);
        idxj = g_simObjects[wpIdxc].spec;
        strcpy(strBuf, g_objTypes[idxj].name);
        strcat(strBuf, g_objTypes[idxj].name + 7);
        drawStringActivePage(strBuf, 0xc8, 0x82, 0xf);
        if (wpIdxc == 0 && g_targetSlots[0].state >= 5) {
            drawStringActivePage("Primary Target", 0xc8, 0x88, 0xf);
        }
        if (g_detailLevel != 0) {
            if ((frameTick & 1) != 0) {
                g_aamLeadDist = (int16)(((uint32)(uint16)(0x8000 - g_simObjects[wpIdxc].pitch) *
                                       (int32)g_simObjects[wpIdxc].speed) >> 15);
                g_aamLeadDist -= abs(sinMul(g_simObjects[wpIdxc].bank.w, g_aamLeadDist)) >> 1;
            }
            strcpy(strBuf, "Speed ");
            strcat(strBuf, itoa(g_aamLeadDist, g_itoaScratch, 10));
            strcat(strBuf, " KMH");
            drawStringActivePage(strBuf, 0xc7, 0xb6, 0xf);
        }
    }
    g_pageFront[1] = 2;
    g_pageBack[1] = 2;
    if (g_scopeSweepTimer > 0 && g_threatLabelTarget < 0) {
        idxj = -1 - g_threatLabelTarget;
        computeAimProjection(g_simObjects[idxj].posX, g_simObjects[idxj].posY,
                             g_simObjects[idxj].alt);
        drawTargetLabel(g_objTypes[g_simObjects[idxj].spec].name,
                        g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }
    if (g_currentWeaponType == 2 && g_viewMode == 0) {
        specD = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;
        if (specD == 0x1e && abs(g_ourRoll) < 0x2000) {
            tmpi = hudPitchScale();
            loftDistf = cosMul(tmpi, g_viewZ) / (sinMul(-tmpi, 0x20) + 1);
            pointXc = sinMul(g_ourHead, loftDistf) + g_viewX_;
            pointYb = g_viewY_ - cosMul(g_ourHead, loftDistf);
            computeAimProjection(pointXc, pointYb, 0);
            if (g_vprojXlo == -1) {
                g_vprojXlo = (sinMul(g_ourRoll, 0x6c - g_flightPathMarkerY) << 2) / 3 + 0xa0;
                g_vprojYlo = 0x6c;
            } else {
                setDrawColor(0xc);
                drawTargetBox(g_vprojXlo, g_vprojYlo, 5, 1);
            }
            setDrawColor(0xf);
            drawHudViewLine(0xa0, g_flightPathMarkerY, g_vprojXlo, g_vprojYlo);
        }
        if ((specD == 0x1e || specD == 0x1d) && g_groundTargetLock >= 0) {
            computeAimProjection(g_planeTable.planes[g_groundTargetLock].mapX + sinMul(g_ourHead, 0x80),
                                 g_planeTable.planes[g_groundTargetLock].mapY - cosMul(g_ourHead, 0x80),
                                 g_viewZ);
            if (g_vprojXlo != -1) {
                p = 0;
                if (specD == 0x1e) {
                    g_projDepth = clampRange(rangeApprox(pointXc - g_planeTable.planes[g_groundTargetLock].mapX,
                                                         pointYb - g_planeTable.planes[g_groundTargetLock].mapY) >> 3,
                                             0, 0x40);
                    if ((uint16)-(g_knots * 2 - 0xbb8) > (uint16)g_viewZ) {
                        p = 1;
                    }
                } else {
                    g_projDepth = clampRange(bearingToStore(g_groundTargetLock) >> 3, 0, 0x40);
                    if (g_viewZ < g_knots) {
                        p = 1;
                    }
                }
                if (p == 0 || (frameTick & 1) != 0) {
                    setDrawColor(0xc);
                    drawViewportLine(0x9f - g_projDepth, 0x21, 0x9f - g_projDepth, 0x1e);
                    drawViewportLine(g_projDepth + 0xa0, 0x21, g_projDepth + 0xa0, 0x1e);
                    drawViewportLine(0x9f - g_projDepth, 0x1e, g_projDepth + 0xa0, 0x1e);
                }
                setDrawColor(0xf);
                drawHudViewLine(g_vprojXlo - 4, g_vprojYlo, g_vprojXlo, g_vprojYlo - 4);
                drawHudViewLine(g_vprojXlo, g_vprojYlo - 4, g_vprojXlo + 4, g_vprojYlo);
                drawHudViewLine(g_vprojXlo + 4, g_vprojYlo, g_vprojXlo, g_vprojYlo + 4);
                drawHudViewLine(g_vprojXlo, g_vprojYlo + 4, g_vprojXlo - 4, g_vprojYlo);
            }
        }
    }
    idxj = 0;
    do {
        if ((g_targetSlots[idxj].flags & 0x10) != 0 && g_missionTick < g_missionTimeLimit) {
            objIdxo = g_targetSlots[idxj].planeIndex;
            if (bearingToStore(objIdxo) < 0xc80) {
                computeAimProjection(g_planeTable.planes[objIdxo].mapX,
                                     g_planeTable.planes[objIdxo].mapY, 0);
                g_scopeArcColor = 10;
                drawTargetLabel("Radio Beacon", 10, frameTick % g_frameRateScaling);
                if (frameTick % (g_frameRateScaling << 3) == 0) {
                    strcpy(strBuf, "Radio beacon bearing ");
                    strcat(strBuf, itoa((uint16)computeBearing(g_planeTable.planes[objIdxo].mapX - g_viewX_,
                                                               g_viewY_ - g_planeTable.planes[objIdxo].mapY) / 0xb6,
                                        g_itoaScratch, 10));
                    hudMessage(strBuf);
                    if (g_vprojXlo != -1 && g_projDepth > -0x10 && missleSpec[0].ammo != 0) {
                        hudMessage("Drop supplies now");
                    }
                }
            }
        }
        idxj++;
    } while (idxj < 2);
    if (g_hitEffectTimer != 0 && g_curPanelMode == 0x13 &&
        g_lockedTargetKilled != 0 && g_targetInHudFlag != 0) {
        blitSprite(0xd4, 0x90, (abs(g_hitEffectTimer) - 8) * -0x20, 0x48, 0x20, 0x20, 0);
    }
    if (g_curPanelMode == 0x13 && g_prevKillMarker != 0 && g_targetInHudFlag == 0) {
        fillPanelBox(2, 3);
    }
}

/* ==== seg000:0xc4fc ==== */
void drawHudViewLine(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18F38 */

void drawTargetBox(int16 centerX, int16 centerY, int16 size, int16 mode) {
    int16 halfHeight, left, top, right, bottom;
    if (g_hudVisible == 0) {
        return;
    }
    if (g_halfScaleRender != 0) {
        size >>= 1;
    }
    halfHeight = size - (size >> 2);
    right = centerX + size;
    left = centerX - size;
    bottom = centerY + halfHeight;
    top = centerY - halfHeight;
    if (mode == 0) {
        drawHudViewLine(left, top, left, bottom);
        drawHudViewLine(left, bottom, right, bottom);
        drawHudViewLine(right, bottom, right, top);
        drawHudViewLine(right, top, left, top);
    } else {
        drawHudViewLine(centerX, top, right, centerY - (halfHeight >> 1));
        drawHudViewLine(right, centerY - (halfHeight >> 1), right, centerY + (halfHeight >> 1));
        drawHudViewLine(right, centerY + (halfHeight >> 1), centerX, bottom);
        drawHudViewLine(centerX, bottom, left, centerY + (halfHeight >> 1));
        drawHudViewLine(left, centerY + (halfHeight >> 1), left, centerY - (halfHeight >> 1));
        drawHudViewLine(left, centerY - (halfHeight >> 1), centerX, top);
    }
}

/* ==== seg000:0xc60c ==== */
void drawFullscreenLine(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18D71 (egtacmap.c) */

/* Draw the target-lock reticle: "Missile Lock" label plus a + crosshair at
 * (0xE4,0xA0).  cx/cy are stored then constant-folded into the line args. */
void drawLockReticle(void) {
    int16 cx, cy;
    if (g_targetLock != 0 && g_hudVisible != 0) {
        if (g_drawPage != 0) {
            drawStringActivePage("Missile Lock", 0xCE, 0x8E, 0x0E);
        }
        setDrawColor(0x0E);
        cx = 0xE4;
        cy = 0xA0;
        drawFullscreenLine(cx - 0x0A, cy, cx + 0x0A, cy);
        drawFullscreenLine(cx, cy - 0x08, cx, cy + 0x08);
    }
}

/* ==== seg000:0xc681 ==== */
void drawTargetLabel(const char *text, int16 color, int16 size) {
    if (g_vprojXlo == -1) {
        return;
    }
    setDrawColor(color);
    if (size < g_vprojXlo && 319 - size > g_vprojXlo &&
        size < g_vprojYlo && 110 - size > g_vprojYlo) {
        drawTargetBox(g_vprojXlo, g_vprojYlo, size, 1);
    }
    if (g_vprojXlo > 20 && g_vprojXlo < 280 &&
        g_vprojYlo > 0 && g_vprojYlo < 94) {
        drawStringActivePage(text, g_vprojXlo - (int16)strlen(text) * 2, g_vprojYlo + 5, g_scopeArcColor);
    }
}

/* ==== seg000:0xc719 ==== */
void buildRangeString(int16 rangeRaw) {
    int16 p, a, b, c, d;

    strcpy(strBuf, "Range ");
    strcat(strBuf, itoa(rangeRaw >> 6, g_itoaScratch, 10));
    strcat(strBuf, ".");
    strcat(strBuf, itoa((rangeRaw & 0x3f) * 2 / 13, g_itoaScratch, 10));
    strcat(strBuf, " km");
}

/* ==== seg000:0xc793 ==== */
int32 matVecDotAxis(int16 axis, int16 x, int16 y, int16 z); /* sub_1C95F (egmath.c) */

/* Project a world point through the view rotation and store the screen
 * position of the aim marker: X into g_vprojXlo, Y into g_vprojYlo,
 * depth into g_projDepth.  Marks g_vprojXlo = -1 when the point is
 * behind the camera or off the frustum edge. */
void computeAimProjection(int16 wx, int16 wy, int16 wz) {
    int16 x, y, z;
    int32 y0, y1, y2;

    x = g_viewX_ - wx;
    y = wy - g_viewY_;
    z = (wz - g_viewZ) >> 5;          /* EN: g_viewZ int16 -> sar */
    if (g_camExtFlag & 0x80) {
        x -= (int16)((g_ViewX - g_camEyeX) >> 5);
        y -= (int16)((g_ViewY - g_camEyeY) >> 5);
        z -= (int16)(-((int32)(uint16)g_viewZ - (int32)g_camEyeZ) >> 5);
    }
    y0 = matVecDotAxis(0, x, y, z);
    y1 = matVecDotAxis(1, x, y, z);
    y2 = matVecDotAxis(2, x, y, z);
    if (y2 < 0) {
        if (g_halfScaleRender != 0) {
            y0 >>= 1;
            y1 >>= 1;
        }
        if (-y2 >= y0) {
            if (y0 < y2) goto fail;
            g_vprojXlo = (int16)((y0 << 8) / y2) + 0xA0;
            g_vprojYlo = (int16)((y1 << 8) / y2);
            g_vprojYlo -= g_vprojYlo >> 2;
            g_vprojYlo += g_hudVisible ? (g_halfScaleRender ? 0x52 : 0x38) : 0x64;
            g_projDepth = (int16)(y2 >> 3);
            if (g_vprojXlo < 0 || g_vprojXlo > 0x13F) {
                g_aimClipSave = g_vprojXlo;
                g_vprojXlo = -1;
            }
            if (g_vprojYlo < 0 || g_vprojYlo > (g_hudVisible ? 0x70 : 0xC7)) {
                g_aimClipSave = g_vprojXlo;
                goto fail;
            }
            return;
        }
    }
fail:
    g_vprojXlo = -1;
}

/* ==== seg000:0xc9b2 ==== */
struct StoreDef { int16 subIdx; uint16 coordX; uint16 coordY; int16 f6; int8 flags; int8 f9; int16 padA; int16 padC; int16 nameIdx; };
struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY); /* sub_11092 (eg3dmap.c) */

/* Map a grid coordinate to its g_storeDefs index: convert to world coords,
 * find the nearest tile object, recover its grid coords, then linear-search
 * the store table.  Returns the store index, 0 on a miss (caching the coords
 * and tile id), or -1 when no tile object exists at the position. */
int16 findStoreAtGrid(int16 gridX, int16 gridY) {
    int16 i;
    g_nearestTileObj = findNearestTileObject((uint32)gridX << 5, ((uint32)0x8000 - gridY) << 5);
    if (g_nearestTileObj != 0) {
        gridX = (int16)(g_nearestTileObj->x >> 5);
        gridY = (int16)(0x8000 - (g_nearestTileObj->y >> 5));
        for (i = 1; i < g_storeDefCount; i++) {
            if (g_storeDefs[i].coordX == gridX && g_storeDefs[i].coordY == gridY) {
                return i;
            }
        }
        g_storeDefs[0].coordX = gridX;
        g_storeDefs[0].coordY = gridY;
        g_storeDefs[0].nameIdx = g_nearestTileObj->id + 0x100;
        if (g_selStoreState == 0) {
            g_selStoreState = -1;
        }
        return 0;
    }
    return -1;
}

/* ==== seg000:0xca74 ==== */
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing); /* sub_1CAB4 */
int16 bearingToStore(int16 i) {
    return computeTargetBearing(g_storeDefs[i].coordX, g_storeDefs[i].coordY, 1);
}

/* ==== seg000:0xca94 ==== */
int16 bearingToSimObject(int16 i) {
    return computeTargetBearing(g_simObjects[i].posX, g_simObjects[i].posY, 0);
}

/* ==== seg000:0xcab4 ==== */
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing) {
    int16 dx, dy;
    dx = g_viewX_ - targetX;
    dy = g_viewY_ - targetY;
    if (wantBearing != 0) {
        g_targetBearing = computeBearing(-dx, dy);
    }
    g_targetRange = rangeApprox(dx, dy);
    return g_targetRange;
}

/* ==== seg000:0xcaf2 ==== */
int16 abs(int16);
int16 hudPitchScale(void) {
    return (int16)(((int32)(0x4000 - abs(g_ourPitch)) << 12) / (uint32)(uint16)(g_viewZ + 0x1000) - 0x4000);
}

/* ==== seg000:0xcb1a ==== */
int16 isTargetOverWater(int16 wpIdx);   /* sub_1CB53 */
int16 getStoreMapCode(int16 idx) {
    if (g_storeDefs[idx].flags & 0x80) {
        return (isTargetOverWater(idx) != 0 ? g_airTargetMark : g_gndTargetMark) + 0x100;
    }
    return g_storeDefs[idx].nameIdx;
}

/* ==== seg000:0xcb53 ==== */
int16 isTargetOverWater(int16 wpIdx) {
    int16 category;

    category = ((char *)g_shapeTargetCategory)[g_planeTable.planes[wpIdx].nameIndex & 0x7f] & 0x0f;
    return (category == 12 || category == 9 || category == 11) ? 1 : 0;
}

/* ==== seg000:0xd1c8 ==== */

int16 shapeDataOffset(int16 shapeId) {
    if (shapeId & 0x100) {
        return buf3d3[shapeId & 0x7f];
    }
    return (int16)((char *)&g_aircraftModels[flt15_buf1[shapeId]] - g_world3dData);
}

/* ==== seg000:0xcdde ==== */

int16 cosMul(int16, int16);                    /* sub_1D404 */
int16 sinMul(int16, int16);                    /* sub_1D3EC */
int16 FAR fillSpanRect(const int16 *, int16, int16, int16, int16); /* sub_21A58 */
void setup3DTransform(const int16 *p, int16 a, int16 b, int16 c, int16 d, int16 e, int16 f, int16 g); /* sub_119EA */
void rasterize3DWorld(void);                   /* sub_11A64 */
void far projectSceneObject(char far *model, int yaw, int pitch, int roll,
                           int relX, int relY, int flag); /* sub_20716 */

void drawTargetView(int16 shapeId, int16 worldX, int16 worldY, int16 altitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 mode, int16 shift) {
    int16 unusda, horize, brgg, rngl, pdelo, catgf, ptchg, doffh, cidx, radb, bdelc, relX, relY, relZ;
    char catLowd;

    g_targetInHudFlag = 1;
    if (mode == 1 && g_detailLevel == 0 && g_gfxModeUnset != 0 && (frameTick & 3) != 0) {
        return;
    }

    doffh = shapeDataOffset(shapeId);
    if (g_drawPage == 0) {
        *g_targetViewParams = 0;
    } else {
        *g_targetViewParams = 1;
    }

    if (mode < 2) {
        g_trkRoll = 0;
        relX = worldX - g_viewX_;
        relY = worldY - g_viewY_;
        relZ = (altitude - g_viewZ) >> 5;
        brgg = computeBearing(relX, -relY);
        ptchg = computeBearing(relZ, rangeApprox(relX, relY));
        rngl = rangeApprox(relZ, rangeApprox(relX, relY));

        if (mode == 1) {
            g_trkRange = rngl;
            g_trkSize = (rngl >> 4) + 400;
            g_trkScale = (g_trkSize << 5) / (rngl + 1);
            rngl = g_trkSize << 2;
            g_trkBearing = brgg;
            g_trkPitch = ptchg;
        } else {
            g_trkScale = (g_trkRange << 5) / (rngl + 1);
            if (g_trkScale > 0x100) {
                g_trkScale = 0x100;
            }
            if (g_trkScale < 4) {
                g_trkScale = 4;
            }
            bdelc = ((brgg - g_trkBearing) >> 5) * g_trkScale;
            pdelo = ((ptchg - g_trkPitch) >> 5) * g_trkScale;
            if (abs(bdelc) > 0x1000) {
                return;
            }
            if (abs(pdelo) > 0x1000) {
                return;
            }
            brgg = (bdelc << 2) + g_trkBearing;
            ptchg = (pdelo << 2) + g_trkPitch;
            rngl = (g_trkSize << 5) / g_trkScale << 2;
        }

        radb = cosMul(ptchg, rngl);
        g_extraScaleShift = 2;
        if (shift < 0) {
            g_extraScaleShift = (uint8)(shift + 2);
            shift = 0;
        }
        relX = sinMul(brgg, radb) >> (char)shift;
        relY = -(cosMul(brgg, radb)) >> (char)shift;
        relZ = sinMul(ptchg, rngl) >> (char)shift;
    } else {
        relX = (worldX - g_viewX_) << 4;
        relY = (worldY - g_viewY_) << 4;
        relZ = (altitude - g_viewZ) >> 1;
        g_trkBearing = g_ourHead;
        g_trkPitch = g_extViewPitch;
        g_trkRoll = g_ourRoll;
        g_trkScale = 0x20;
        g_extraScaleShift = 2;
    }

    if (mode == 1 || mode == 3) {
        horize = (int16)((int32)g_trkScale * (int32)(g_trkPitch >> 2) >> 5) + 0xA0;
        if (horize < 0x7C || g_trkPitch < (int16)0xE800) {
            horize = 0x7C;
        }
        if (horize > 0xC4 || g_trkPitch > 0x1800) {
            horize = 0xC4;
        }
        g_targetViewParams[2] = colorLut[3];
        if (horize != 0x7C) {
            fillSpanRect(g_targetViewParams, 0xB0, 0x7C, 0x118, horize);
        }
        cidx = (g_viewParamsFar[0x1C] > 1) ? 2 : 6;
        catgf = (int16)(signed char)g_shapeTargetCategory[shapeId & 0x7f];
        if (catgf & 0x10) {
            cidx = 8;
        }
        catLowd = (char)(catgf & 0xf);
        if (catLowd == 12 || catLowd == 9 || catLowd == 11) {
            cidx = 1;
        }
        g_targetViewParams[2] = colorLut[cidx];
        if (horize != 0xC4) {
            fillSpanRect(g_targetViewParams, 0xB0, horize, 0x118, 0xC4);
        }
    }

    g_offscreenRender = 1;
    setup3DTransform(g_targetViewParams, -g_trkBearing, g_trkPitch, g_trkRoll, 0, 0, 0, 0);
    projectSceneObject((char *)(g_world3dData + (uint16)doffh), -objYaw, objPitch, objRoll, relX, -relY, relZ);
    rasterize3DWorld();
    g_offscreenRender = 0;

    if (mode == 1) {
        strcpy(strBuf, "BRG ");
        strcat(strBuf, itoa((uint16)g_trkBearing / 0xB6, g_itoaScratch, 10));
        drawStringActivePage(strBuf, 0xF6, 0xBC, 0xF);
    }
    g_extraScaleShift = 0;
}
