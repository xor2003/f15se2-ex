#include "f19eg.h"

int16 computeThreatRangeBearing(int16, int16, int16, int16, int16 *, int16 *);
/* egcombat.c — threat/target combat routines (F19) */

int16 clampRange(int16 v, int16 lo, int16 hi);   /* sub_1D1FA */
void appendMapEvent(int16 type, int16 arg);      /* sub_14CAF */
int16 randomRange(int16 n);                      /* sub_1D46B */
void refreshActivePanel(int16 id);               /* sub_186FC */
void makeSound(int16 a, int16 b);                /* sub_1DEF2 */
int16 rangeApprox(int16 dx, int16 dy);           /* sub_1D23B */
int16 computeBearing(int16 dx, int16 dy);        /* sub_1D29D */
int16 abs(int16 v);

struct Projectile { int16 mapX, mapY, alt, speed, worldX, worldY, worldZ, ttl, specIdx, weaponIdx, targetLock, targetRef; };
struct Weapon { int8 name[8]; int16 lethality, dangerTier, flags; };   /* 14-byte @0x4894 */
void   exitTimeAccel(void);         /* sub_1E010 */
struct MissileSpec { int16 weaponIdx; int16 ammo; };
struct Missile { char shortName[10]; char longName[12]; int16 specIndex; int16 weaponCategory; };
struct Sam { char name[8]; int16 lockRange, maxSpeed, weaponClass, turnRate, modelId; };
int16 computeLoftAngle(void);       /* sub_1CAF2 */
void exitTimeAccel(void);           /* sub_1E010 */
void hwPortWrite(int16 cmd);        /* sub_14CAC */
void drawWaypointPanel(void);            /* sub_19979 */
void hudMessage(const char *s);     /* sub_192B1 */

struct MapEvent {                  /* 12-byte marker record */
    int16 mapX, mapY, unused4, type, ttl, unusedA;
};

struct MapTarget {                 /* F19 layout, 16 bytes @0x80C8 */
    int16 objType;                 /* +0 */
    uint16 mapX;                   /* +2 */
    uint16 mapY;                   /* +4 */
    int16 active;                  /* +6 */
    int16 flags;                   /* +8  (was field02) */
    int16 alertLevel;              /* +A */
    int16 threatTimer;             /* +C */
    int16 symbol;                  /* +E */
};

struct TargetSlot { int16 state; int16 planeIndex; int16 pad[7]; };  /* 0x12 bytes, state@0 */

struct TileObject {
    int16 id;                      /* +0x00 */
    int16 dist;                    /* +0x02 */
    int32 x;                       /* +0x04 */
    int32 y;                       /* +0x08 */
    int16 entry;                   /* +0x0C */
    uint8 lod;                     /* +0x0E */
    uint8 subIndex;                /* +0x0F */
    uint8 tileX;                   /* +0x10 */
    uint8 tileY;                   /* +0x11 */
    int16 shapeOff;                /* +0x12 */
    uint8 flag;                    /* +0x14 */
    uint8 pad15;                   /* +0x15 */
};
struct TileObject *findNearestTileObject(uint32 wx, uint32 wy);  /* sub_11092 */
int16 placeString(int16 idx);                 /* sub_14D03 */
int16 getStoreMapCode(int16 idx);             /* sub_1CB1A */
int16 isTargetOverWater(int16 idx);           /* sub_1CB53 */
int16 shapeDataOffset(int16 shapeId);         /* sub_1D1C8 */
void addTileEntry(struct TileObject *rec, int16 value, char tag);  /* sub_112DC */

struct SimObject {
    int16 objType;      /* +0x00 */
    uint16 posX;        /* +0x02 */
    int16 posY;         /* +0x04 */
    int16  alt;         /* +0x06 */
    int32  worldX;      /* +0x08 */
    int32  worldY;      /* +0x0C */
    union { int16 w; uint8 b[2]; } heading; /* +0x10 */
    int16  pitch;       /* +0x12 */
    union { int16 w; uint8 b[2]; } bank;    /* +0x14 */
    int16  spec;        /* +0x16 */
    union { uint16 w; uint8 b[2]; } flags;  /* +0x18 */
    int16  speed;       /* +0x1A */
    int16  timer;       /* +0x1C */
    int16  weaponType;  /* +0x1E */
    int16  terrainColor;/* +0x20 */
    int16  damage;      /* +0x22 */
};                                    /* 36 bytes */
struct ObjType { char name[0x12]; int16 maxSpeed; int16 range; int16 maneuverability; int16 modelId; int16 pad1A, pad1C; int16 kills; };  /* 32 bytes */
void notifyViewObj(int16 idx);      /* sub_14C98 */
int16 markTargetReached(int16 n);   /* sub_17AAF */
void redrawTacMap(int16 x, int16 y);/* sub_187EC */

struct CommData { int8 pad1[0x2C]; int16 restartFlag; int8 pad2[0x4A]; int16 gfxModeNum; };
void restoreScopePanel(void);           /* sub_1A26C */
void captureScopePanel(void);           /* sub_1A2C5 */
int16 plotMapObject(int16 x, int16 y, int16 color, int16 big);  /* sub_18B41 */
void drawMapArc(int16 cx, int16 cy, int16 r, int16 color, int16 lines, int16 a0, int16 a1); /* sub_18C76 */
void drawGaugeBar(int16 val, int16 color, int16 x1, int16 x2);  /* sub_1877F */
void cacheScopePanel(void);             /* sub_1A23F */
void updateThreatAlert(void);           /* sub_157DB — defined below */
void fireGroundThreat(int16 idx);       /* sub_15311 */
int16 sinMul(int16 angle, int16 val);   /* sub_1D3EC */
int16 cosMul(int16 angle, int16 val);   /* sub_1D404 */
void  testWorldPosVisible(int16 x, int16 y, int16 z); /* sub_17E29 */
void  fireAirThreat(int16 idx);         /* sub_1673D — defined below */
void  spawnEnemyAircraft(int16 slot, int16 objType); /* sub_16AD2 — defined below */
struct BulletTrack { int16 posX, posY, alt, velX, velY, velZ; }; /* stride 0xC */
struct Particle { int16 posX, posY, alt, spin; };              /* stride 8 */
int16 readMapPixelColor(int16 x, int16 y);  /* sub_18BEA (egtacmap) */
void drawStatusItem(int16 item, int16 attr);            /* sub_19007 (egui) */
int16 getWeaponStat(int16 statIdx, int16 target);       /* sub_192CD */
int16 findStoreAtGrid(int16 x, int16 y);                /* sub_1C9B2 */
int16 signOf(int16 v);                                  /* sub_1D436 (egmath) */
int16 samCanAcquireTarget(int16 slot, int16 targetX, int16 targetY,
                          int16 targetAlt, int16 mode); /* defined below */
void destroySimObject(int16 idx);                       /* defined below */
void destroyGroundTarget(int16 planeIdx);               /* defined below */
void bombTarget(void);                                  /* defined below */

/* ==== seg000:0x505a ==== */
void updateThreatSites(void) {
    int16 p, arc, rad, x, siteIdx, arcRadius;

    if ((g_scopeSweepTimer == 0 || g_prevScopeRange != g_threatScopeRange) &&
        g_hudVisible != 0) {
        g_prevScopeRange = g_threatScopeRange;
        drawGaugeBar(-clampRange(0x64 - g_threatScopeRange, 0, 0x63), 1, 0, 0xB);
        drawGaugeBar(clampRange(g_threatScopeRange, 0, 0x63), 4, 0, 0xB);
        if (g_scopeSweepTimer == 0 && g_mapMode == 0) {
            restoreScopePanel();
            if (g_curPanelMode == 0x14)
                drawWaypointPanel();
            g_scopeArcStart = 0;
            g_scopeArcEnd = 0x100;
        }
    }

    for (siteIdx = 0; siteIdx < g_targetEntityCount; siteIdx++) {
        if (g_planeTable[siteIdx].active != 0 &&
            !(*(uint8 *)&g_planeTable[siteIdx].flags & 0x80) &&
            (((uint8)(siteIdx * (frameTick >> 10) * 7) & 7) <=
                 (g_unusedEventHist1 < 0x80 ? g_difficultyTier * 2 + 1 : g_difficultyTier * 2 + 3) ||
             g_planeTable[siteIdx].alertLevel != 0 ||
             (g_planeTable[siteIdx].flags & 0x100) != 0)) {
            g_planeTable[siteIdx].threatTimer--;
            if (g_planeTable[siteIdx].threatTimer <= 0)
                g_planeTable[siteIdx].threatTimer =
                    ((int16)(int8)g_frameRateScaling << 8) /
                    ((g_planeTable[siteIdx].alertLevel >> 2) + 0x10) + siteIdx;
            if (g_planeTable[siteIdx].threatTimer == 4 && g_scopeSweepTimer < 0) {
                fireGroundThreat(siteIdx);
                *(uint8 *)&g_planeTable[siteIdx].flags |= 2;
            }
        } else {
            *(uint8 *)&g_planeTable[siteIdx].flags &= ~2;
        }
    }

    if (commData->gfxModeNum == 0)
        g_scopeArcColor = 0;
    if (g_mapMode == 0 && g_scopeSweepTimer > 0 && g_hudVisible != 0 && g_scopeArcRange > 1) {
        if (g_detailLevel != 0 && commData->gfxModeNum != 0) {
            captureScopePanel();
            arc = (int16)((int32)clampRange(g_frameRateScaling - g_scopeSweepTimer, 1, g_frameRateScaling) *
                          g_scopeArcRange / g_frameRateScaling) << 6;
        } else {
            arc = g_scopeArcRange << 6;
            g_scopeArcRange = 0;
        }
        if (g_threatLabelTarget >= 0) {
            plotMapObject(g_planeTable[g_threatLabelTarget].mapX, g_planeTable[g_threatLabelTarget].mapY,
                          (g_scopeSweepTimer & 1) ? 0xF : 0, 1);
            drawMapArc(g_planeTable[g_threatLabelTarget].mapX, g_planeTable[g_threatLabelTarget].mapY,
                       arc, g_scopeArcColor, g_threatRadarFlag, g_scopeArcStart, g_scopeArcEnd);
        } else {
            plotMapObject(g_simObjects[-1 - g_threatLabelTarget].posX, g_simObjects[-1 - g_threatLabelTarget].posY,
                          (g_scopeSweepTimer & 1) ? 0xF : 0, 1);
            drawMapArc(g_simObjects[-1 - g_threatLabelTarget].posX, g_simObjects[-1 - g_threatLabelTarget].posY,
                       arc, g_scopeArcColor, g_threatRadarFlag, g_scopeArcStart, g_scopeArcEnd);
        }
    }
    g_scopeSweepTimer--;
}

/* ==== seg000:0x5311 ==== */
void fireGroundThreat(int16 siteIdx) {
    int16 p[11];                 /* bearing buffer — only p[0] (bp-16) live */
    uint16 r[4];                 /* range buffer — only r[0] (bp-1E) live */
    int16 d, e, rad, x, i, score;

    e = g_planeTable[siteIdx].active;
    score = computeThreatRangeBearing(g_planeTable[siteIdx].mapX,
                                      g_planeTable[siteIdx].mapY, 0, e,
                                      p, (int16 *)r);
    g_threatToneLevel = 0;
    if (score > 0) {
        d = score;
        if (d > 0x63)
            d = 0x63;
        g_threatToneLevel = 4;
        if (score + g_threatScopeRange > 0x32)
            g_threatToneLevel = 0xC;
        if (score + g_threatScopeRange > 0x64)
            g_threatToneLevel = 0xE;
        drawGaugeBar(-d, g_threatToneLevel, 1, 4);
        g_scopeArcRange = (uint16)((int32)(score + g_threatScopeRange) * r[0] / 100);
        g_scopeSweepTimer = g_frameRateScaling;
        g_threatLabelTarget = siteIdx;
        g_threatRadarFlag = g_samSpecs[e].flags & 1;
        if (g_planeTable[siteIdx].alertLevel != 0) {
            g_scopeArcStart = (p[0] >> 8) - 0x20;
            g_scopeArcEnd = (p[0] >> 8) + 0x20;
        }
        g_scopeArcColor = g_threatToneLevel;
        if (!(*(uint8 *)&g_planeTable[siteIdx].flags & 4)) {
            if (g_mapMode == 0 && g_hudVisible != 0) {
                restoreScopePanel();
                plotMapObject(g_planeTable[siteIdx].mapX, g_planeTable[siteIdx].mapY, 0, 1);
                cacheScopePanel();
            }
            *(uint8 *)&g_planeTable[siteIdx].flags |= 4;
            placeString(siteIdx);
            strcat(strBuf, " detected");
            hudMessage(strBuf);
        }
    }
    if (score + g_threatScopeRange > 0x64) {
        if (!(*(uint8 *)&g_planeTable[siteIdx].flags & 0x10))
            makeSound(8, 1);
        g_planeTable[siteIdx].alertLevel +=
            (((g_difficultyTier + g_missionStatus) << 4) + 0x10) >>
            ((g_playerPlaneFlags & 0x10) != 0);
        if (g_planeTable[siteIdx].alertLevel > 0xFF)
            g_planeTable[siteIdx].alertLevel = 0xFF;
        if (!(g_planeTable[siteIdx].flags & 0x100) && mapEvents[0].ttl == 0 &&
            g_planeTable[siteIdx].alertLevel > 0x7F)
            updateThreatAlert();
        if (g_enemyThreatCount <= g_missionStatus) {
            if (g_planeTable[siteIdx].alertLevel > 0xC0) {
                if (e != 0x15) {
                    if (g_nearestThreatRange > 0x500) {
                        g_enemyAlertFlag++;
                        if (!(g_planeTable[siteIdx].flags & 0x1000) &&
                            g_planeTable[siteIdx].alertLevel >= 0xFA) {
                            g_planeTable[siteIdx].flags |= 0x1000;
                            g_enemyGroundRemaining++;
                            placeString(siteIdx);
                            strcat(strBuf, " - Radar ID");
                            hudMessage(strBuf);
                            makeSound(8, 1);
                            appendMapEvent(7, siteIdx);
                        } else {
                            rad = siteIdx & 7;
                            if (g_projectiles[rad].ttl == 0 &&
                                sams[e].lockRange > r[0]) {
                                g_projectiles[rad].mapX = g_planeTable[siteIdx].mapX + 8;
                                g_projectiles[rad].mapY = g_planeTable[siteIdx].mapY;
                                g_projectiles[rad].alt = 0;
                                g_projectiles[rad].speed = sams[e].maxSpeed >> 6;
                                g_projectiles[rad].worldX = p[0];
                                g_projectiles[rad].worldY = 0x4000;
                                g_projectiles[rad].ttl = ((int32)sams[e].lockRange << 4) *
                                                         g_frameRateScaling / g_projectiles[rad].speed;
                                g_projectiles[rad].specIdx = e;
                                g_projectiles[rad].targetRef = siteIdx;
                                placeString(siteIdx);
                                strcat(strBuf, " firing ");
                                strcat(strBuf, sams[e].name);
                                hudMessage(strBuf);
                                notifyViewObj(siteIdx + 0x40);
                                commData->restartFlag++;
                            }
                        }
                    }
                }
            }
        }
        *(uint8 *)&g_planeTable[siteIdx].flags |= 0x10;
    } else {
        *(uint8 *)&g_planeTable[siteIdx].flags &= ~0x10;
        g_planeTable[siteIdx].alertLevel -= 0x10;
        if (g_planeTable[siteIdx].alertLevel < 0)
            g_planeTable[siteIdx].alertLevel = 0;
    }
}

/* ==== seg000:0x5689 ==== */
int16 computeThreatRangeBearing(int16 threatX, int16 threatY, int16 threatAlt,
                                 int16 threatType, int16 *outBearing, int16 *outRange) {
    int16 bearingErr, isRadar, bearing, dx, dy, result;
    uint16 distance;

    if (threatType == 0 || threatType == -1) return 0;
    dx = g_viewX_ - threatX;
    dy = g_viewY_ - threatY;
    distance = (uint16)rangeApprox(dx, dy) >> 6;
    result = ((int32)(g_mapCellFlags[(g_viewY_ >> 0xB) * 0x10 + (g_viewX_ >> 0xB)] & 0xC) *
              (int32)((int32)g_samSpecs[threatType].lethality - distance) *
              (int32)(g_samSpecs[threatType].dangerTier + g_missionStatus * 2 + 1))
             / g_samSpecs[threatType].lethality;
    bearing = computeBearing(dx, -dy);
    bearingErr = abs(bearing - g_ourHead) >> 8;
    if (bearingErr > 0x40) bearingErr = 0x80 - bearingErr;
    isRadar = 0;
    if (g_samSpecs[threatType].flags & 1) {
        bearingErr = (uint16)(0x60 - bearingErr) * ((uint16)g_startRange >> 5) >> 9;
        isRadar = 1;
    }
    result = ((bearingErr + 0x20) >> 1) * (result >> 1) >> 4;
    if ((uint16)(abs(threatAlt - g_viewZ) >> 0xA) > (uint16)distance) result = 0;
    if (result + g_threatScopeRange > 0x64) exitTimeAccel();
    *outBearing = bearing;
    *outRange = distance;
    return result;
}

/* ==== seg000:0x57db ==== */
void updateThreatAlert(void) {
    int16 planeIdx;
    g_threatActiveTimer = g_threatTimerInit;
    if (mapEvents[0].ttl != 0) {
        g_threatRefX = mapEvents[0].mapX;
        g_threatRefY = mapEvents[0].mapY;
    } else {
        g_threatRefX = g_viewX_;
        g_threatRefY = g_viewY_;
    }
    g_threatRefZ = g_viewZ;
    g_threatRefHead = g_ourHead;
    g_unusedEventHist0 = 0xFF;
    for (planeIdx = 0; planeIdx < g_planeScanCount; planeIdx++) {
        if (g_planeTable[planeIdx].active != 0) {
            g_planeTable[planeIdx].alertLevel = clampRange(g_planeTable[planeIdx].alertLevel, ((g_missionStatus + g_difficultyTier) << 4) - 16, 0xFF);
        }
    }
}

/* ==== seg000:0x585c (EN 0x56ec) ====
 * EN indexes g_projectiles[slot] directly — MSC dedicates si = slot*24 across
 * the body (no 'off' variable, no slot), which leaves di free for the
 * transient map index in guard2.  RU needed a phantom register param
 * ('mov si,[bp+4]' dead load) to approximate this. */
void spawnSamThreat(void) {
    int16 dy, dx, slot, specIdx;
    register int16 off;

    if ((g_missionTick & 0xF) != 0)
        return;
    if (!(g_mapCellFlags[(g_viewX_ >> 11) + ((g_viewY_ >> 11) << 4)] & 0x10))
        return;
    if (g_nightMode != 0)
        return;
    slot = (g_missionTick >> 4) & 7;
    specIdx = 0x21;
    off = slot * 24;
    if (*(int16 *)((char *)g_projectiles + off + 14) != 0)
        return;
    *(int16 *)((char *)g_projectiles + off) = randomRange(0x800) + (g_viewX_ & 0xF800);
    *(int16 *)((char *)g_projectiles + off + 2) = randomRange(0x800) + (g_viewY_ & 0xF800);
    dx = g_viewX_ - *(int16 *)((char *)g_projectiles + off);
    dy = g_viewY_ - *(int16 *)((char *)g_projectiles + off + 2);
    if ((uint16)((4 - g_missionStatus) << 8) >= (uint16)rangeApprox(dx, dy))
        return;
    *(int16 *)((char *)g_projectiles + off + 4) = 0;
    *(int16 *)((char *)g_projectiles + off + 6) = g_samSpeed >> 6;
    *(int16 *)((char *)g_projectiles + off + 8) = computeBearing(dx, -dy);
    *(int16 *)((char *)g_projectiles + off + 10) = 0x2000;
    *(int16 *)((char *)g_projectiles + off + 14) = (int16)(((int32)g_frameRateScaling * ((int32)g_samRange << 4)) / (int32)*(int16 *)((char *)g_projectiles + off + 6));
    *(int16 *)((char *)g_projectiles + off + 16) = 0x21;
    *(int16 *)((char *)g_projectiles + off + 22) = 1;
    strcpy(strBuf, "SA-14");
    strcat(strBuf, " fired");
    hudMessage(strBuf);
    updateThreatAlert();
}

/* ==== seg000:0x59a4 ==== */
void updateObjects(void) {
    int16 candBear, pitchReq, viewBrgK, aggr, toTgt, offBoresight, selTgt, maxRange,
          hdgA, asp, e0, u0, best8, distToTgt, move, mode2, fireSlot, objIdx8, bVel,
          scnIdx, pDelta, trackSlot3, tgtX4, deltaX9, deltaY9, rollC, horizMoveJ, tY,
          smokeSlotM, tZ, color;

    if (!(frameTick & 1) && g_smokeSourceIdx == -1)
        g_particles[(frameTick >> 1) & 7].posX = 0;
    bulletTracks[((frameTick >> 2) & 3) + g_bulletTrackCount].posX = 0;
    g_enemyThreatCount = g_activeThreatCount;
    g_activeThreatCount = 0;

    for (objIdx8 = 0; objIdx8 < g_groundUnitCount; objIdx8++) {
        if (g_simObjects[objIdx8].flags.b[0] & 1) {
            g_threatSpec = g_simObjects[objIdx8].spec;
            if ((g_simObjects[objIdx8].flags.b[0] & 2) && g_simObjects[objIdx8].speed != 0) {
                if (g_detailLevel != 0)
                    plotMapObject(g_simObjects[objIdx8].posX, g_simObjects[objIdx8].posY,
                                  g_simObjects[objIdx8].terrainColor, 1);
                mode2 = 0;
                if (!((uint8)*(int16 *)&g_simObjects[objIdx8].flags & 4)) {
                    if (g_threatActiveTimer != 0) {
                        if (!(*(int16 *)&g_simObjects[objIdx8].flags & 0x140) ||
                            g_threatActiveTimer > g_threatDisplayTtl) {
                        tgtX4 = g_threatRefX;
                        tY = g_threatRefY;
                        tZ = g_threatRefZ;
                        mode2 = 1;
                        if (mapEvents[0].ttl != 0)
                            goto padlock_target;
                        goto got_target;
                        }
                    }
                    mode2 = 3;
                    if (g_simObjects[objIdx8].flags.w & 0x100) {
                        if (g_selSimObj != -1) {
                            tgtX4 = sinMul((objIdx8 & 7) * 0x800 + g_simObjects[g_selSimObj].heading.w - 0x1800,
                                          g_simObjects[g_selSimObj].speed) +
                                   g_simObjects[g_selSimObj].posX;
                            tY = g_simObjects[g_selSimObj].posY -
                                   cosMul((objIdx8 & 7) * 0x800 + g_simObjects[g_selSimObj].heading.w - 0x1800,
                                          g_simObjects[g_selSimObj].speed);
                            tZ = g_simObjects[g_selSimObj].alt + (objIdx8 & 7) * 0x40;
                            goto set_target_alt;
                        }
                    }
                    if (((uint8)(objIdx8 * 8) + (uint8)g_missionTick) & 0xbf)
                        goto after_retarget;
                    if (!(g_simObjects[objIdx8].flags.b[0] & 0x40)) {
                        best8 = 0x7fff;
                        viewBrgK = computeBearing(g_viewX_ - g_simObjects[objIdx8].posX,
                                                     g_simObjects[objIdx8].posY - g_viewY_);
                        for (scnIdx = 0; scnIdx < 8; scnIdx++) {
                            selTgt = randomRange(g_storeDefCount) + 1;
                            if (!(g_planeTable[selTgt].flags & 0x400)) {
                                candBear = computeBearing(g_planeTable[selTgt].mapX - g_simObjects[objIdx8].posX,
                                                             g_simObjects[objIdx8].posY - g_planeTable[selTgt].mapY);
                                if (abs(viewBrgK - candBear) < best8) {
                                    best8 = abs(viewBrgK - candBear);
                                    g_simObjects[objIdx8].objType = selTgt;
                                    if (-(g_missionStatus * 0x1000 - 0x4000) > best8)
                                        break;
                                }
                            }
                        }
                        if ((uint16)rangeApprox(g_viewX_ - g_simObjects[objIdx8].posX,
                                                g_viewY_ - g_simObjects[objIdx8].posY) >> 6 > 0x15e &&
                            objIdx8 != 0) {
                            g_simObjects[objIdx8].flags.w &= 0x1c1;
                            g_simObjects[objIdx8].timer = 0;
                        }
                    }
                after_retarget:
                    selTgt = g_simObjects[objIdx8].objType;
                    tgtX4 = g_planeTable[selTgt].mapX;
                    tY = g_planeTable[selTgt].mapY;
                    tZ = clampRange(g_viewZ + 1000, 5000, 20000);
                set_target_alt:
                    tZ = tZ;
                    goto got_target;
                padlock_target:
                    tgtX4 = mapEvents[0].mapX;
                    tY = mapEvents[0].mapY;
                    tZ = clampRange(g_viewZ, 1000, 30000);
                    goto got_target;
                }
                tgtX4 = g_planeTable[g_simObjects[objIdx8].objType].mapX;
                if (g_simObjects[objIdx8].flags.w & 0x200) {
                    tZ = g_simObjects[objIdx8].posX - tgtX4;
                    tY = g_planeTable[g_simObjects[objIdx8].objType].mapY;
                    tgtX4 = tgtX4 - tZ * 2;
                    tZ = ((g_planeTable[g_simObjects[objIdx8].objType].flags + abs(tZ)) & 0x200)
                               ? 140 : 12;
                } else {
                    tY = g_planeTable[g_simObjects[objIdx8].objType].mapY + g_northSouthSign * 0x280;
                    tZ = rangeApprox(g_simObjects[objIdx8].posX - tgtX4,
                                       g_simObjects[objIdx8].posY - tY) + 2000;
                }
                mode2 = 2;
            got_target:
                if (mode2 == 3 && (g_simObjects[objIdx8].flags.b[0] & 8)) {
                    tgtX4 = g_viewX_;
                    tY = g_viewY_;
                    tZ = g_simObjects[objIdx8].alt;
                }
                deltaX9 = tgtX4 - g_simObjects[objIdx8].posX;
                deltaY9 = tY - g_simObjects[objIdx8].posY;
                toTgt = computeBearing(deltaX9, -deltaY9);
                distToTgt = rangeApprox(deltaX9, deltaY9);
                pitchReq = computeBearing((tZ - g_simObjects[objIdx8].alt) >> 5, distToTgt);
                pitchReq = clampRange(pitchReq, -0x2000, 0x1000);
                if (mode2 == 1 && (uint16)distToTgt < 0x400) {
                    g_activeThreatCount++;
                    if ((uint16)distToTgt < 0x200 && !(frameTick & 3) &&
                        g_threatActiveTimer > g_threatDisplayTtl &&
                        abs(g_simObjects[objIdx8].heading.w - toTgt) < 0x400 &&
                        abs(g_simObjects[objIdx8].pitch - pitchReq) < 0x400) {
                    trackSlot3 = ((frameTick >> 2) & 3) + g_bulletTrackCount;
                    bVel = 0xe0 / g_frameRateScaling;
                    bulletTracks[trackSlot3].velZ = sinMul(-g_simObjects[objIdx8].pitch, bVel) << 5;
                    bVel = cosMul(g_simObjects[objIdx8].pitch, bVel);
                    bulletTracks[trackSlot3].velX = sinMul(g_simObjects[objIdx8].heading.w, bVel);
                    bulletTracks[trackSlot3].velY = -cosMul(g_simObjects[objIdx8].heading.w, bVel);
                    bulletTracks[trackSlot3].posX = g_simObjects[objIdx8].posX;
                    bulletTracks[trackSlot3].posY = g_simObjects[objIdx8].posY;
                    bulletTracks[trackSlot3].alt = g_simObjects[objIdx8].alt;
                    }
                after_missile_table:
                    aggr = clampRange((objIdx8 & 3) + g_missionStatus, 0, 2);
                    if (objIdx8 == 0)
                        aggr = 1;
                    hdgA = g_simObjects[objIdx8].heading.w;
                    if (abs(g_simObjects[objIdx8].bank.w) < 0x4000)
                        hdgA += g_simObjects[objIdx8].bank.w >> 2;
                    offBoresight = (toTgt - hdgA) >> 13 & 7;
                    hdgA = g_ourHead;
                    if (abs(g_ourRoll) < 0x4000)
                        hdgA += g_ourRoll >> 1;
                    asp = (((g_simObjects[objIdx8].heading.w - hdgA) >> 13) + 4) & 7;
                    {
                        register int16 maneuver;
                        maneuver = g_maneuverTable[aggr][offBoresight][asp];
                        rollC = (maneuver & 0xf) << 12;
                        if (maneuver == 0x100) {
                            pitchReq = 0x6000;
                            rollC = ((frameTick >> 8) & 8) * 0x1000 - 0x4000;
                        }
                    }
                    if (g_maneuverTable[aggr][offBoresight][asp] == 0x200) {
                        pitchReq = (int16)0xa000;
                        rollC = (((frameTick >> 8) & 8) - 4) * -0x1000;
                    }
                    if (pitchReq == (int16)0xa000) {
                        if (-((g_simObjects[objIdx8].pitch >> 3) - 3000) > g_simObjects[objIdx8].alt)
                            pitchReq = g_simObjects[objIdx8].pitch + 0x1000;
                    }
                    if (abs(g_simObjects[objIdx8].bank.w) > 0x4000) {
                        pitchReq = rollC = 0;
                    }
                    goto after_accel;
                }
                rollC = clampRange(toTgt - g_simObjects[objIdx8].heading.w, -0x2000, 0x2000) << 1;
                if (mode2 == 1 && g_missionStatus * 2 < g_enemyThreatCount)
                    rollC = 0x3000;
            after_accel:
                if (mode2 == 1 && (g_planeTable[g_closestThreatIndex].flags & 0x400) &&
                    g_nearestThreatRange < 0x780)
                    rollC = 0x3000;
                rollC = clampRange(rollC, -g_objTypes[g_threatSpec].maneuverability * 0x1000,
                                     g_objTypes[g_threatSpec].maneuverability * 0x1000);
                rollC = clampRange(rollC - g_simObjects[objIdx8].bank.w,
                                     -g_objTypes[g_threatSpec].maneuverability * 256,
                                     g_objTypes[g_threatSpec].maneuverability * 256);
                if (g_simObjects[objIdx8].flags.w & 0x400) {
                    if (g_simObjects[objIdx8].speed < 150) {
                        g_simObjects[objIdx8].pitch = 0;
                    } else {
                        g_simObjects[objIdx8].pitch += 0x100;
                    }
                    rollC = 0;
                    if (g_simObjects[objIdx8].speed < g_objTypes[g_threatSpec].maxSpeed) {
                        g_simObjects[objIdx8].speed += 40 / g_frameRateScaling;
                    } else if (g_simObjects[objIdx8].alt > 300) {
                        g_simObjects[objIdx8].flags.b[1] &= 0xfb;
                    }
                }
                if (g_simObjects[objIdx8].flags.b[0] & 0x30)
                    rollC = 0x400;
                if (((uint8)objIdx8 & 3) == ((uint8)frameTick & 3)) {
                    testWorldPosVisible(g_simObjects[objIdx8].posX,
                                        g_simObjects[objIdx8].posY,
                                        g_simObjects[objIdx8].alt);
                    if (g_projClipFlag != 0) {
                        g_simObjects[objIdx8].flags.b[1] |= 0x20;
                    } else {
                        g_simObjects[objIdx8].flags.b[1] &= 0xdf;
                    }
                }
                if (g_simObjects[objIdx8].flags.w & 0x2000)
                    pitchReq = 0x3000;
                g_simObjects[objIdx8].bank.w += (rollC << 2) / g_frameRateScaling;
                g_simObjects[objIdx8].heading.w += (g_simObjects[objIdx8].bank.w >> 3) / g_frameRateScaling;
                pDelta = pitchReq - g_simObjects[objIdx8].pitch;
                if (g_simObjects[objIdx8].flags.b[0] & 0x20) {
                    pDelta = -0x200;
                    if (!(frameTick & 3)) {
                        smokeSlotM = (frameTick >> 1) & 7;
                        g_particles[smokeSlotM].posX = g_simObjects[objIdx8].posX;
                        g_particles[smokeSlotM].posY = g_simObjects[objIdx8].posY;
                        g_particles[smokeSlotM].alt = g_simObjects[objIdx8].alt;
                        g_particles[smokeSlotM].spin = randomRange(32) << 11;
                        g_smokeParticleSlot = smokeSlotM;
                    }
                }
            no_smoke:
                if (g_simObjects[objIdx8].pitch < 0 &&
                    -(sinMul(g_simObjects[objIdx8].pitch, 2000) - 200) > g_simObjects[objIdx8].alt &&
                    (g_simObjects[objIdx8].flags.w & 0x220) == 0)
                    pDelta = 0x400;
                pDelta = clampRange(pDelta, -0x400, 0x400);
                g_simObjects[objIdx8].pitch += (pDelta << 2) / g_frameRateScaling;
                if (abs(g_simObjects[objIdx8].pitch) > 0x4000) {
                    g_simObjects[objIdx8].heading.b[1] += (char)0x80;
                    g_simObjects[objIdx8].bank.b[1] += (char)0x80;
                    g_simObjects[objIdx8].pitch = (int16)0x8000 - g_simObjects[objIdx8].pitch;
                }
                g_simObjects[objIdx8].flags.b[0] &= 0xef;
                move = (int16)((uint32)((uint16)(0x8000 - g_simObjects[objIdx8].pitch) *
                                           (int32)g_simObjects[objIdx8].speed) >> 15);
                move -= abs(sinMul(g_simObjects[objIdx8].bank.w, move)) >> 1;
                if (mode2 == 1 && g_enemyThreatCount <= g_missionStatus)
                    move += move >> 1;
                move = move * 4 / g_frameRateScaling;
                move >>= 2;
                horizMoveJ = cosMul(g_simObjects[objIdx8].pitch, move);
                g_simObjects[objIdx8].worldX += sinMul(g_simObjects[objIdx8].heading.w, horizMoveJ);
                g_simObjects[objIdx8].worldY -= cosMul(g_simObjects[objIdx8].heading.w, horizMoveJ);
                g_simObjects[objIdx8].alt += sinMul(g_simObjects[objIdx8].pitch, move);
                g_simObjects[objIdx8].posX = (int16)(g_simObjects[objIdx8].worldX >> 5);
                g_simObjects[objIdx8].posY = (int16)(g_simObjects[objIdx8].worldY >> 5);
                if (g_simObjects[objIdx8].alt > 30000)
                    g_simObjects[objIdx8].pitch = 0;
            alt_ok:
                if (g_simObjects[objIdx8].alt < 0) {
                    g_simObjects[objIdx8].flags.w &= (objIdx8 != 0) ? 0x1c1 : 0;
                    g_hitMapX = g_simObjects[objIdx8].posX;
                    g_hitMapY = g_simObjects[objIdx8].posY;
                    g_hitAlt = g_simObjects[objIdx8].alt;
                    g_hitEffectTimer = -8;
                    if (objIdx8 == g_airTargetLock)
                        g_airTargetLock = -1;
                }
                if ((uint16)distToTgt < 0x10 && mode2 == 2) {
                    placeString(g_simObjects[objIdx8].objType);
                    strcat(strBuf, "- ");
                    strcat(strBuf, g_objTypes[g_threatSpec].name);
                    if (g_simObjects[objIdx8].flags.w & 0x200) {
                        g_simObjects[objIdx8].flags.b[1] |= 0x10;
                        strcat(strBuf, " landing");
                    } else {
                        g_simObjects[objIdx8].flags.b[1] |= 2;
                        strcat(strBuf, " taking off");
                    }
                    if (objIdx8 < g_groundUnitCount - 4)
                        hudMessage(strBuf);
                }
                if (g_simObjects[objIdx8].flags.w & 0x1000) {
                    g_simObjects[objIdx8].bank.w = g_simObjects[objIdx8].pitch = 0;
                    g_simObjects[objIdx8].heading.w = (g_northSouthSign == 1) ? 0 : (int16)0x8000;
                    g_simObjects[objIdx8].alt = (g_planeTable[g_closestThreatIndex].flags & 0x200) ? 140 : 12;
                    if (g_simObjects[objIdx8].speed > 0) {
                        g_simObjects[objIdx8].speed -= 60 / g_frameRateScaling;
                    } else {
                        g_simObjects[objIdx8].flags.w &= 0x1c1;
                        if (objIdx8 == 0 && g_missionStage >= 5)
                            g_simObjects[objIdx8].flags.w = 0;
                    }
                    if (objIdx8 >= g_groundUnitCount - 4 && g_simObjects[objIdx8].speed < 100) {
                        g_simObjects[objIdx8].flags.w &= 0x1c1;
                        g_simObjects[objIdx8].flags.w |= 0x406;
                    }
                }
                if (--g_simObjects[objIdx8].timer == 0) {
                    g_simObjects[objIdx8].flags.b[0] |= 4;
                    best8 = 0x7fff;
                    for (scnIdx = 3; scnIdx < g_planeScanCount; scnIdx++) {
                        if ((g_planeTable[scnIdx].flags & 0x101) == 1) {
                            smokeSlotM = rangeApprox(g_simObjects[objIdx8].posX - g_planeTable[scnIdx].mapX,
                                                    g_simObjects[objIdx8].posY - g_planeTable[scnIdx].mapY);
                            if (smokeSlotM < best8) {
                                g_simObjects[objIdx8].objType = scnIdx;
                                best8 = smokeSlotM;
                            }
                        }
                    }
                }
                g_simObjects[objIdx8].terrainColor =
                    readMapPixelColor(g_simObjects[objIdx8].posX, g_simObjects[objIdx8].posY);
                if (g_simObjects[objIdx8].flags.b[0] & 2) {
                    color = (commData->gfxModeNum == 0 && (frameTick & 1)) ? 0 : 0xc;
                    if (objIdx8 == 0 && g_missionStage >= 5 && (frameTick & 1))
                        color = 0;
                    if (g_detailLevel != 0 || objIdx8 == 0)
                        plotMapObject(g_simObjects[objIdx8].posX, g_simObjects[objIdx8].posY,
                                      color, 1);
                    fireSlot = (((uint8)objIdx8 & 8) >> 3) + (objIdx8 & 7) * 2;
                    if (frameTick % (g_frameRateScaling << 4) == fireSlot * g_frameRateScaling &&
                        !(g_simObjects[objIdx8].flags.b[0] & 0x20))
                        fireAirThreat(objIdx8);
                }
            } else {
                if (((uint8)(g_missionTick >> ((uint8)g_difficultyTier + 4)) & 7) == ((uint8)objIdx8 & 7)) {
                    if (objIdx8 < g_groundUnitCount - 4) {
                        if (objIdx8 != 0) {
                            selTgt = randomRange(g_planeScanCount);
                            if (g_threatActiveTimer > g_threatDisplayTtl ||
                                (g_simObjects[objIdx8].flags.b[0] & 0x80)) {
                                if ((g_planeTable[selTgt].flags & 0x181) == 1) {
                                    if (g_simObjects[objIdx8].spec == g_planeTable[selTgt].alertLevel) {
                                        deltaX9 = g_threatRefX - g_planeTable[selTgt].mapX;
                                        deltaY9 = g_threatRefY - g_planeTable[selTgt].mapY;
                                        distToTgt = (uint16)rangeApprox(deltaX9, deltaY9) >> 6;
                                        maxRange = g_objTypes[g_threatSpec].range;
                                        if ((uint16)maxRange > (uint16)distToTgt) {
                                            spawnEnemyAircraft(objIdx8, selTgt);
                                            notifyViewObj(selTgt + 0x40);
                                            frameTick += 0x20;
                                            notifyViewObj(objIdx8 + 0x20);
                                            frameTick -= 0x20;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ==== seg000:0x673d ==== */
void fireAirThreat(int16 objIdx) {
    int16 p, a, b, c, bearing, e, f;
    uint16 acqRange;
    int16 gauge, j, k, l, nn, range, n, wofs;

    j = g_objTypes[g_threatSpec].modelId;
    range = computeThreatRangeBearing(g_simObjects[objIdx].posX,
                                      g_simObjects[objIdx].posY,
                                      g_simObjects[objIdx].alt, j,
                                      &bearing, (int16 *)&acqRange);
    g_threatToneLevel = 4;
    if (range > 0) {
        g_scopeArcRange = (uint16)((int32)(range + g_threatScopeRange) * acqRange / 100);
        if (!(*(uint8 *)&g_samSpecs[j].flags & 2)) {
            g_scopeArcStart = (g_simObjects[objIdx].heading.w >> 8) - 0x30;
            g_scopeArcEnd = (g_simObjects[objIdx].heading.w >> 8) + 0x30;
            if (abs(bearing - g_simObjects[objIdx].heading.w) >> 8 > 0x30)
                range = 0;
        }
        if (g_simObjects[objIdx].flags.b[0] & 8) {
            g_scopeArcStart = (bearing >> 8) - 0x20;
            g_scopeArcEnd = (bearing >> 8) + 0x20;
        }
        g_threatToneLevel = (range + g_threatScopeRange > 0x64) ? 0xF : 0xB;
        gauge = range;
        if (gauge > 0x63)
            gauge = 0x63;
        drawGaugeBar(-gauge, g_threatToneLevel, 7, 0xA);
        g_scopeSweepTimer = g_frameRateScaling;
        g_threatLabelTarget = -objIdx - 1;
        g_scopeArcColor = g_threatToneLevel;
        g_threatRadarFlag = g_samSpecs[j].flags & 1;
    }
    if (range + g_threatScopeRange > 0x64) {
        g_simObjects[objIdx].damage += (((g_difficultyTier + g_missionStatus) << 4) + 0x20) >>
                                       ((g_playerPlaneFlags & 0x10) != 0);
        if (g_simObjects[objIdx].damage > 0xC0) {
            g_enemyAlertFlag++;
            g_simObjects[objIdx].flags.b[1] |= 0x40;
            updateThreatAlert();
            if (!(g_simObjects[objIdx].flags.w & 0x800)) {
                if ((8 >> g_nightMode) > acqRange) {
                    g_simObjects[objIdx].flags.b[1] |= 8;
                    g_liveObjCount++;
                    strcpy(strBuf, "Visual ID - ");
                    strcat(strBuf, g_objTypes[g_threatSpec].name);
                    hudMessage(strBuf);
                    makeSound(6, 1);
                    appendMapEvent(6, g_threatSpec);
                }
            }
            k = objIdx & 7;
            if (g_missionStatus * 2 >= g_enemyThreatCount &&
                g_projectiles[k].ttl == 0 &&
                abs(bearing - g_simObjects[objIdx].heading.w) < 0x1800) {
                j = g_simObjects[objIdx].weaponType;
                wofs = j * 0x12;
                if (sams[j].lockRange > acqRange &&
                    (uint16)(-(g_missionStatus * 2 - 8)) < acqRange && j != 0) {
                    g_projectiles[k].mapX = g_simObjects[objIdx].posX;
                    g_projectiles[k].mapY = g_simObjects[objIdx].posY;
                    g_projectiles[k].alt = g_simObjects[objIdx].alt - 0x19;
                    g_projectiles[k].speed = sams[j].maxSpeed >> 6;
                    g_projectiles[k].worldX = g_simObjects[objIdx].heading.w;
                    g_projectiles[k].worldY = g_simObjects[objIdx].pitch;
                    g_projectiles[k].worldZ = g_simObjects[objIdx].bank.w;
                    g_projectiles[k].ttl = (int16)(((int32)sams[j].lockRange << 5) *
                        g_frameRateScaling / g_projectiles[k].speed);
                    g_projectiles[k].specIdx = j;
                    g_projectiles[k].targetRef = -objIdx;
                    strcpy(strBuf, sams[j].name);
                    strcat(strBuf, " fired by ");
                    strcat(strBuf, g_objTypes[g_threatSpec].name);
                    hudMessage(strBuf);
                    commData->restartFlag++;
                    notifyViewObj(objIdx + 0x20);
                }
            }
        }
        if (!(g_simObjects[objIdx].flags.b[0] & 8))
            makeSound(6, 1);
        g_simObjects[objIdx].flags.b[0] |= 8;
    } else {
        g_simObjects[objIdx].flags.b[0] &= 0xF7;
        g_simObjects[objIdx].damage -= 0x20;
    }
    g_simObjects[objIdx].damage = clampRange(g_simObjects[objIdx].damage, 0, 0xFF);
}

/* ==== seg000:0x6ad2 ==== */
void spawnEnemyAircraft(int16 slot, int16 objType) {
    int16 spec;

    spec = g_simObjects[slot].spec;
    g_simObjects[slot].heading.w = (g_northSouthSign == 1) ? 0 : (int16)0x8000;
    if (g_planeTable[objType].flags & 0x200) {
        g_simObjects[slot].posX = g_northSouthSign * 3 + g_planeTable[objType].mapX;
        g_simObjects[slot].posY = g_planeTable[objType].mapY - g_northSouthSign * 12;
        g_simObjects[slot].alt = 140;
        g_simObjects[slot].speed = 100;
        g_simObjects[slot].heading.b[1] += 0xfc;
    } else {
        g_simObjects[slot].posX = g_planeTable[objType].mapX;
        g_simObjects[slot].posY = 30 * g_northSouthSign + g_planeTable[objType].mapY;
        g_simObjects[slot].alt = 12;
        g_simObjects[slot].speed = 10;
    }
    g_simObjects[slot].worldX = (int32)(uint16)g_simObjects[slot].posX << 5;
    g_simObjects[slot].worldY = (int32)(uint16)g_simObjects[slot].posY << 5;
    g_simObjects[slot].pitch = 0;
    g_simObjects[slot].bank.w = 0;
    g_simObjects[slot].flags.w |= 0x403;
    g_simObjects[slot].objType = objType;
    g_simObjects[slot].timer = (int16)(((int32)g_objTypes[spec].range << 11) * g_frameRateScaling / g_objTypes[spec].maxSpeed);
    g_simObjects[slot].terrainColor = readMapPixelColor(g_planeTable[objType].mapX, g_planeTable[objType].mapY);
    if (g_selSimObj == -1) {
        g_simObjects[slot].flags.b[1] &= 0xfe;
    }
    placeString(objType);
    strcat(strBuf, "- ");
    strcat(strBuf, g_objTypes[g_simObjects[slot].spec].name);
    strcat(strBuf, " taking off");
    if (slot < g_groundUnitCount - 4) {
        hudMessage(strBuf);
    }
}

/* ==== seg000:0x6c66 ==== */
void updateThreatTargeting(void) {
    int16 lockedn, acqk, wpXb, aimYa, ringb, wpYb;
    uint16 beste, disto;
    int16 bestIdxa, speci, wpn, stepi, slotd, deltal, scanb, foundk,
          bearn, modec, e0d, tileXc, tileYc, viewXg, viewYg, u0f,
          storeIdxj, alt0k;

    drawStatusItem(0, 0);
    drawStatusItem(1, 0);
    drawStatusItem(8, (g_enemyAlertFlag != 0 && (frameTick & 1)) ? 0xE : 3);
    if (mapEvents[0].ttl != 0) {
        viewXg = mapEvents[0].mapX;
        viewYg = mapEvents[0].mapY;
    } else {
        viewXg = g_viewX_;
        viewYg = g_viewY_;
    }

    for (slotd = 0; slotd < 12; slotd++) {
        if (g_projectiles[slotd].ttl != 0) {
            speci = g_projectiles[slotd].specIdx;
            lockedn = 0;
            aimYa = 0;
            modec = sams[speci].weaponClass;

            if (slotd < 8) {
                plotMapObject(g_projectiles[slotd].mapX, g_projectiles[slotd].mapY,
                              g_projectiles[slotd].targetLock, 0);
                alt0k = g_viewZ;
                lockedn = samCanAcquireTarget(slotd, viewXg, viewYg, g_viewZ, modec);
                beste = g_acqRange;
                aimYa = g_acqAimY;
                if ((g_playerPlaneFlags & 0x10) && modec > 0)
                    goto lock_eval;
                if ((g_playerPlaneFlags & 0x20) && modec <= 0)
                    goto lock_eval;
                if (g_threatActiveTimer < g_threatDisplayTtl &&
                    g_projectiles[slotd].specIdx != 0x21) {
lock_eval:
                    if ((g_missionStatus << 8) + 0x180 < beste ||
                        modec == 1 || modec == -1) {
                        aimYa = g_projectiles[slotd].worldX;
                        lockedn = 0;
                    }
                }
                scanb = 1;
                do {
                    if ((mapEvents[scanb].type == 1 && modec <= 0) ||
                        (mapEvents[scanb].type == 2 &&
                         (modec == 1 || modec == 2 ||
                          (modec == 3 &&
                           -(g_missionStatus * 12 - 0x40) >
                               abs(abs((aimYa - g_ourHead) >> 8) - 0x40))))) {
                        acqk = samCanAcquireTarget(slotd, mapEvents[scanb].mapX,
                                                   mapEvents[scanb].mapY, g_viewZ, modec);
                        if (acqk != 0) {
                            aimYa = acqk;
                            lockedn = 0;
                        }
                    }
                    scanb++;
                } while (scanb < 4);

                if (beste > 0x200) {
                    if (g_projectiles[slotd].targetRef > 2 &&
                        !(*(uint8 *)&g_planeTable[g_projectiles[slotd].targetRef].flags & 0x10))
                        lockedn = 0;
                    if (g_projectiles[slotd].targetRef <= 0 &&
                        !(g_simObjects[-g_projectiles[slotd].targetRef].flags.b[0] & 8))
                        lockedn = 0;
                }
            } else {
                beste = 0x7fff;
                if (modec == 7) {
                    for (scanb = 0; scanb < g_groundUnitCount; scanb++) {
                        if ((g_simObjects[scanb].flags.b[0] & 2) &&
                            g_simObjects[scanb].speed != 0) {
                            acqk = samCanAcquireTarget(slotd, g_simObjects[scanb].posX,
                                                       g_simObjects[scanb].posY,
                                                       g_simObjects[scanb].alt, modec);
                            if (g_acqRange < beste && acqk != 0) {
                                aimYa = g_acqAimY;
                                beste = g_acqRange;
                                bestIdxa = scanb;
                                alt0k = g_simObjects[scanb].alt;
                                lockedn = 1;
                                if (beste < 0x180)
                                    g_simObjects[scanb].flags.b[0] |= 0x10;
                            }
                        }
                    }
                }
                if (g_projectiles[slotd].speed < (sams[speci].maxSpeed >> 6) &&
                    (frameTick & 1)) {
                    g_projectiles[slotd].speed++;
                    aimYa = g_projectiles[slotd].worldX;
                }
                if (modec == 4 || modec == 6 || modec == 5 || modec == 0x1C) {
                    if (g_projectiles[slotd].targetLock == -1) {
                        for (scanb = 0; scanb < g_storeDefCount; scanb++) {
                            if (modec == 4 && g_planeTable[scanb].active == 0)
                                continue;
                            if (((modec == 5 || modec == 6) &&
                                 (g_shapeTargetCategory[g_planeTable[scanb].symbol] & 0xC)) ||
                                (modec != 5 &&
                                 !(g_shapeTargetCategory[g_planeTable[scanb].symbol] & 0xC))) {
                                acqk = samCanAcquireTarget(slotd, g_planeTable[scanb].mapX,
                                                           g_planeTable[scanb].mapY, 0, modec);
                                if (g_acqRange < beste && acqk != 0) {
                                    aimYa = g_acqAimY;
                                    beste = g_acqRange;
                                    bestIdxa = scanb;
                                    alt0k = 0;
                                    lockedn = 1;
                                }
                            }
                        }
                    } else {
                        scanb = g_projectiles[slotd].targetLock;
                        acqk = samCanAcquireTarget(slotd, g_planeTable[scanb].mapX,
                                                   g_planeTable[scanb].mapY, 0, modec);
                        if (acqk != 0) {
                            aimYa = g_acqAimY;
                            beste = g_acqRange;
                            bestIdxa = scanb;
                            alt0k = 0;
                            lockedn = 1;
                        }
                    }
                }
            }

            if (lockedn != 0 && slotd < 8 &&
                abs(g_acqAimY - g_projectiles[slotd].worldX) < 0x1000 &&
                !(g_bombDamageMask & 1) && mapEvents[0].ttl == 0) {
                if (modec <= 0 && (frameTick & 2))
                    drawStatusItem(1, g_gfxModeUnset != 0 ? 0x7 : 0xC);
                if (modec != 0 && !(frameTick & 2))
                    drawStatusItem(0, 0xE);
                if (!(frameTick & 3) &&
                    (uint16)(g_projectiles[slotd].speed << 4) > beste)
                    makeSound(0xA, 1);
            }

            if (aimYa != 0 && lockedn != 0) {
                deltal = aimYa - g_projectiles[slotd].worldX;
                if (slotd < 8)
                    deltal = clampRange(deltal, -(g_missionStatus + 1) << 8,
                                        (g_missionStatus + 1) << 8);
                deltal = clampRange(deltal, -(sams[speci].turnRate << 7),
                                    sams[speci].turnRate << 7);
                g_projectiles[slotd].worldX += (deltal << 2) / g_frameRateScaling;
                g_projectiles[slotd].worldZ = deltal << 1;
                aimYa = computeBearing(((alt0k - g_projectiles[slotd].alt) >> 5) +
                                       (abs((int16)beste) >> 3), abs((int16)beste));
                bearn = aimYa - g_projectiles[slotd].worldY;
                if (slotd < 8)
                    bearn = clampRange(bearn, -(g_missionStatus + 1) << 9,
                                       (g_missionStatus + 1) << 9);
                bearn = clampRange(bearn, -(sams[speci].turnRate << 8),
                                   sams[speci].turnRate << 8);
                g_projectiles[slotd].worldY += (bearn << 2) / g_frameRateScaling;
            } else {
                if (g_projectiles[slotd].worldY > 0 && modec != 0x1E)
                    g_projectiles[slotd].worldY -=
                        (signOf(g_projectiles[slotd].worldY) << 0xB) / g_frameRateScaling;
            }

            if (modec == 0x1C && g_projectiles[slotd].worldY > -0x800)
                g_projectiles[slotd].worldY = -0x800;
            if (modec == 0x1E || g_projectiles[slotd].alt == 1) {
                g_projectiles[slotd].worldY -= 0x800 / g_frameRateScaling;
                if (g_projectiles[slotd].worldY < g_projectiles[slotd].targetRef)
                    g_projectiles[slotd].worldY = g_projectiles[slotd].targetRef;
            }

            stepi = (cosMul(g_projectiles[slotd].worldY, g_projectiles[slotd].speed) << 2) /
                    g_frameRateScaling;
            g_projectiles[slotd].mapX += sinMul(g_projectiles[slotd].worldX, stepi);
            g_projectiles[slotd].mapY -= cosMul(g_projectiles[slotd].worldX, stepi);
            g_projectiles[slotd].alt += sinMul(g_projectiles[slotd].worldY,
                                               (g_projectiles[slotd].speed << 7) /
                                                   g_frameRateScaling);
            g_projectiles[slotd].ttl--;
            g_projClipFlag = 0;
            if ((slotd & 3) == (frameTick & 3))
                testWorldPosVisible(g_projectiles[slotd].mapX, g_projectiles[slotd].mapY,
                                    g_projectiles[slotd].alt);

            if (g_projectiles[slotd].alt < 0 || g_projClipFlag != 0) {
                g_hitMapX = g_projectiles[slotd].mapX;
                g_hitMapY = g_projectiles[slotd].mapY;
                g_hitAlt = g_projectiles[slotd].alt;
                g_hitEffectTimer = -3;
                g_projectiles[slotd].ttl = 0;
                strcpy(strBuf, missiles[g_projectiles[slotd].weaponIdx].longName);
                if (modec == 0x1E || modec == 0x1D || modec == 0x1C) {
                    {   /* pjbase is a codegen device: `register` commits si to the
                         * slotd*24 byte offset region-wide, so the original's
                         * [si+543x] index-form field accesses fall out instead of
                         * a folded &projectiles[slotd].weaponIdx pointer. */
                    register int16 pjbase = slotd * (int16)sizeof(struct Projectile);
                    if (*(int16 *)((char *)g_projectiles + pjbase + 16) == 0x26) {
                        goto cluster_imp;
                    } else {
                        makeSound(2, 2);
                        strcat(strBuf, " misses ");
                        storeIdxj = findStoreAtGrid(g_hitMapX, g_hitMapY);
                        if (storeIdxj != -1 &&
                            !(g_planeTable[storeIdxj].flags & 0x80)) {
                            tileXc = (int16)(g_nearestTileObj->x >> 5);
                            tileYc = -((int16)(g_nearestTileObj->y >> 5) - 0x8000);
                            disto = rangeApprox(g_hitMapX - tileXc, g_hitMapY - tileYc);
                            if (disto < (uint16)((getWeaponStat(
                                                     *(int16 *)((char *)g_projectiles + pjbase + 18),
                                                     storeIdxj) << 6) /
                                                 (g_missionStatus + 2))) {
                                destroyGroundTarget(storeIdxj);
                                strcat(strBuf, " destroyed by ");
                                strcat(strBuf,
                                       missiles[*(int16 *)((char *)g_projectiles + pjbase + 18)].longName);
                                g_hitEffectTimer = 8;
                                g_hitAlt = 0;
                            }
                        }
                        hudMessage(strBuf);
                        pjbase = slotd * (int16)sizeof(struct Projectile);
                        disto = rangeApprox(g_viewX_ - *(int16 *)((char *)g_projectiles + pjbase),
                                            g_viewY_ - *(int16 *)((char *)g_projectiles + pjbase + 2)) +
                                ((uint16)g_viewZ >> 5);
                        if ((uint16)(g_missionStatus << 5) > disto) {
                            bombTarget();
                            strcpy(strBuf, "Blast damage");
                        }
                        goto impact_done;
                    }
                    }
cluster_imp:
                    foundk = 0;
                    scanb = 0;
                    do {
                        disto = g_targetSlots[scanb].planeIndex;
                        if ((uint16)rangeApprox(g_projectiles[slotd].mapX -
                                                g_planeTable[disto].mapX,
                                                g_projectiles[slotd].mapY -
                                                g_planeTable[disto].mapY) < 0x100 &&
                            g_targetSlots[scanb].state == 3 &&
                            g_missionTick < g_missionTimeLimit) {
                            hudMessage("Equipment Delivered");
                            markTargetReached(scanb);
                            foundk = 1;
                        }
                    } while (++scanb < 2);
                    if (foundk == 0)
                        hudMessage("Bad Drop");
                } else if (slotd >= 8 &&
                           g_projectiles[slotd].ttl > g_frameRateScaling * 2) {
                    strcat(strBuf, " ground impact");
                    hudMessage(strBuf);
                }
            }
impact_done:

            if ((uint16)((abs(alt0k - g_projectiles[slotd].alt) >> 5) + beste) <
                    (uint16)((g_projectiles[slotd].speed << 3) / g_frameRateScaling) &&
                lockedn != 0 && g_projectiles[slotd].ttl > g_frameRateScaling * 4) {
                g_hitMapX = g_projectiles[slotd].mapX;
                g_hitMapY = g_projectiles[slotd].mapY;
                g_hitAlt = g_projectiles[slotd].alt;
                g_hitEffectTimer = 8;
                g_projectiles[slotd].ttl = g_frameRateScaling * 2;
                if (slotd < 8) {
                    if (mapEvents[0].ttl != 0)
                        goto hit_done;
                    strcpy(strBuf, "Hit by ");
                    strcat(strBuf, sams[speci].name);
                    hudMessage(strBuf);
                    bombTarget();
                    if (!(g_playerPlaneFlags & 0x1000))
                        appendMapEvent(5, speci);
                    notifyViewObj(slotd);
                } else {
                    if (modec == 7) {
                        destroySimObject(bestIdxa);
                    } else {
                        g_projectiles[slotd].ttl = 0;
                        if (getWeaponStat(g_projectiles[slotd].weaponIdx, bestIdxa) >
                            randomRange(4))
                            destroyGroundTarget(bestIdxa);
                        else
                            strcpy(strBuf, "Ineffective");
                        g_threatActiveTimer = g_threatTimerInit;
                        g_threatRefX = g_hitMapX;
                        g_threatRefY = g_hitMapY;
                        g_threatRefZ = 0xBB8;
                    }
                    strcat(strBuf, " hit by ");
                    strcat(strBuf, sams[speci].name);
                    hudMessage(strBuf);
                }
            }
hit_done:

            if (slotd < 8 && g_projectiles[slotd].ttl != 0) {
                g_projectiles[slotd].targetLock =
                    readMapPixelColor(g_projectiles[slotd].mapX,
                                      g_projectiles[slotd].mapY);
                if (frameTick & 1)
                    plotMapObject(g_projectiles[slotd].mapX, g_projectiles[slotd].mapY,
                                  0xE, 0);
            }
        }
    }
}

/* ==== seg000:0x7757 ==== */
int16 samCanAcquireTarget(int16 slot, int16 targetX, int16 targetY, int16 targetAlt, int16 mode) {
    int16 dx, dy, rng, bd;

    dx = targetX - g_projectiles[slot].mapX;
    dy = targetY - g_projectiles[slot].mapY;
    rng = rangeApprox(dx, dy);
    g_acqAimY = computeBearing(dx, -dy);
    bd = abs(g_acqAimY - g_projectiles[slot].worldX);
    if (bd > 0x1000 && mode != 3) {
        if (bd > 0x6000 && slot < 8 && g_projectiles[slot].speed < rng) {
            g_projectiles[slot].ttl = 0;
        }
        return 0;
    }
    if (mode == 0 && abs(g_projectiles[slot].worldX - g_ourHead) > 0x2000) {
        return 0;
    }
    g_acqRange = rng;
    return 1;
}

/* ==== seg000:0x7800 ==== */
void destroySimObject(int16 idx) {
    int16 evt;
    if (!(g_simObjects[idx].flags.b[0] & 0x20)) {
        g_objTypes[g_simObjects[idx].spec].kills++;
        if (g_simObjects[idx].flags.w & 0x800)
            --g_liveObjCount;
        notifyViewObj(idx + 0x20);
        if (g_selSimObj == idx)
            g_selSimObj = -1;
        g_simObjects[idx].flags.b[0] |= 0x20;
        g_smokeSourceIdx = -1;
        g_wreckX = g_simObjects[idx].posX;
        g_wreckY = g_simObjects[idx].posY;
        g_wreckAlt = g_simObjects[idx].alt;
        g_wreckFallVel = 0x80;
        evt = 3;
        if (g_missionStage >= 5 && idx == 0) {
            markTargetReached(0);
            evt |= 0x80;
        }
        appendMapEvent(evt, g_simObjects[idx].spec + (g_simObjects[idx].flags.w & 0x4000 ? 0x80 : 0));
        if (g_simObjects[idx].speed == 0)
            g_simObjects[idx].flags.w &= 0x1C1;
    }
    strcpy(strBuf, g_objTypes[g_simObjects[idx].spec].name);
    makeSound(2, 2);
    if (g_currentWeaponType == 1 && idx == g_airTargetLock)
        g_lockedTargetKilled = 1;
}

/* ==== seg000:0x790e ==== */
void destroyGroundTarget(int16 planeIdx) {
    int16 eventType;

    placeString(planeIdx);
    eventType = 1;
    {
        int16 slot;
        int16 symbol;
        if ((g_planeTable[planeIdx].flags & 0x80) == 0) {
        if (g_planeTable[planeIdx].flags & 0x1000)
            --g_enemyGroundRemaining;
        g_nearestTileObj = findNearestTileObject(
            (int32)(uint16)g_planeTable[planeIdx].mapX << 5,
            ((int32)0x8000 - (uint16)g_planeTable[planeIdx].mapY) << 5);
        notifyViewObj(planeIdx + 0x40);
        if (planeIdx != 0) {
            if (g_planeTable[planeIdx].active == 0)
            eventType = 12;
        *(uint8 *)&g_planeTable[planeIdx].flags |= 0x80;
        g_planeTable[planeIdx].active = 0;
        for (slot = 0; slot < 2; slot++) {
            if (g_targetSlots[slot].state == 2 && g_targetSlots[slot].planeIndex == planeIdx) {
                markTargetReached(slot);
                eventType |= (slot != 0 ? 0x40 : 0x80);
                makeSound(0, 2);
            }
        }
            appendMapEvent(eventType, planeIdx);
            symbol = getStoreMapCode(planeIdx);
        } else {
            symbol = isTargetOverWater(planeIdx) ? g_airTargetMark : g_gndTargetMark;
        if (symbol != g_nearestTileObj->id) {
            g_tileKillTally[g_nearestTileObj->id]++;
            appendMapEvent(2, g_nearestTileObj->id);
        }
            symbol |= 0x100;
            g_planeTable[planeIdx].symbol = symbol;
        }
            if (g_nearestTileObj != 0)
                addTileEntry(g_nearestTileObj, shapeDataOffset(symbol), symbol);
        }
        g_smokeSourceIdx = planeIdx;
    }
    makeSound(2, 2);
    if (g_currentWeaponType == 2 && planeIdx == g_groundTargetLock)
        g_lockedTargetKilled = 1;
    if (g_mapMode == 0)
        redrawTacMap(g_viewX_, g_viewY_);
    if (g_missionStatus < 2)
        updateThreatAlert();
}

/* ==== seg000:0x7aaf ==== */
int16 markTargetReached(int16 targetIdx) {
    if (g_playerPlaneFlags & (0x4000 >> targetIdx)) {
        return 0;
    }
    if (g_targetSlots[targetIdx].state == 4 || g_targetSlots[targetIdx].state == 3) {
        appendMapEvent((targetIdx != 0 ? 0x40 : 0x80) + 0x0b, 0);
    }
    if (targetIdx != 0) {
        strcpy(strBuf, "Second. target");
        waypointIndex = 1;
        g_playerPlaneFlags |= 0x2000;
    } else {
        strcpy(strBuf, "Primary target");
        waypointIndex = 2;
        g_playerPlaneFlags |= 0x4000;
    }
    if ((g_playerPlaneFlags & 0x6000) == 0x6000) {
        waypointIndex = 3;
    }
    return 1;
}

/* ==== seg000:0x7b46 ==== */
void bombTarget(void) {
    int16 hit;
    if (!(g_playerPlaneFlags & 0x1000)) {
        hit = 0;
        goto check;
        do {
            g_bombDamageMask |= (1 << randomRange(8));
            g_gunHits++;
            hit++;
        check:;
        } while (hit <= g_missionStatus);
        refreshActivePanel(0x16);
        g_damageTakenFlag = 1;
        makeSound(0, 2);
    }
}

/* ==== seg000:0x7ba0 ==== */
void fireMissile(void) {
    int16 spec, tmp, weaponIdx, slot;

    if (abs((int16)g_ourRoll) > 0x3000) return;
    if (missleSpec[missileSpecIndex].ammo == 0) return;
    weaponIdx = missleSpec[missileSpecIndex].weaponIdx;
    spec = missiles[weaponIdx].specIndex;
    if (spec == 0) return;
    if (spec == -1) return;
    if (!(g_playerPlaneFlags & 4) && spec != -2) {
        hudMessage("Bay closed");
        return;
    }
    missleSpec[missileSpecIndex].ammo--;
    if (spec == -2) {
        g_fuelRemaining += 0x76C;
        hudMessage("Fuel transfer");
        if (g_curPanelMode == 0x14)
            drawWaypointPanel();
        return;
    }
    appendMapEvent(4, missileSpecIndex);
    exitTimeAccel();
    slot = -1;
    tmp = 8;
    do {
        if (g_projectiles[tmp].ttl == 0)
            slot = tmp;
        tmp++;
    } while (tmp < 12);
    if (slot == -1) goto check_end;
    g_projectiles[slot].mapX = g_viewX_;
    g_projectiles[slot].mapY = g_viewY_;
    g_projectiles[slot].alt = g_viewZ - 20;
    g_projectiles[slot].speed = (uint16)g_startRange >> 11;
    g_projectiles[slot].worldX = g_ourHead;
    g_projectiles[slot].worldY = g_ourPitch;
    g_projectiles[slot].worldZ = g_ourRoll;
    g_projectiles[slot].ttl = (int16)(((int32)sams[spec].lockRange << 5) * (int32)g_frameRateScaling / (int32)((sams[spec].maxSpeed >> 6) + 1)) + 6;
    if (g_projectiles[slot].ttl <= 6)
        g_projectiles[slot].ttl = 999;
    g_projectiles[slot].specIdx = spec;
    g_projectiles[slot].weaponIdx = weaponIdx;
    g_projectiles[slot].targetLock = -1;
    if (spec != 30)
        g_projectiles[slot].worldY -= 0x1000;
    else
        g_projectiles[slot].targetRef = computeLoftAngle() - 0x400;
    if (g_groundTargetLock >= 0 && sams[spec].weaponClass == 6)
        g_projectiles[slot].targetLock = g_groundTargetLock;
    if (g_groundTargetLock >= 0 && sams[spec].weaponClass == 5 && (g_planeTable[g_groundTargetLock].flags & 8))
        g_projectiles[slot].targetLock = g_groundTargetLock;
    if (spec == 29) {
        g_projectiles[slot].worldY = 0xC000;
        g_projectiles[slot].speed = 1;
    }
    g_lastMissileSlot = slot;
    strcpy(strBuf, missiles[weaponIdx].longName);
    strcat(strBuf, " fired");
    hudMessage(strBuf);
    makeSound(sams[spec].lockRange != 0 ? 18 : 24, 2);
check_end:
    hwPortWrite(0xD6);
    if (g_curPanelMode == 0x15)
        refreshActivePanel(0x15);
}
