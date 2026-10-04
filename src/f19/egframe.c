#include "f19eg.h"

int16 randomRange(int16);
/* egframe.c — frame/state transfer routines (F19) */

void moveNearFar(void *nearPtr, int16 count);   /* sub_15001 */
int16 setCommWorldbufPtr(void);                  /* sub_15045 */
void moveStuff(void);                            /* sub_14F18 */


/* ==== seg000:0x4ef7 ==== */
struct FrameRec { int16 tick; int8 y, x, a, b; };      /* packed 6-byte record */
struct ReplayLog { struct FrameRec events[0x100]; };
#undef g_replayLog
#define g_replayLog (*(struct ReplayLog *)(f19_dseg + 0x8E64))

void moveDataFar() {
    int16 unused1, unused2;
    setCommWorldbufPtr();
    flagFarToNear = 0;
    moveStuff();
    moveNearFar(g_replayLog.events, sizeof(g_replayLog.events));
}

/* ==== seg000:0x4f18 ==== */
struct TargetSlot {
    int16 state;      /* +0x0 */
    int16 planeIndex; /* +0x2  index into g_storeDefs */
    int16 viewIndex;  /* +0x4 */
    int16 flags;      /* +0x6 */
    int16 seedNoise;  /* +0x8 */
    int16 pad[4];     /* +0xA..+0x12 (18-byte stride) */
};

struct MapTarget {                 /* F19 layout, 16 bytes @0x80C8 */
    int16 objType;                 /* +0 */
    uint16 mapX;                   /* +2 */
    int16  mapY;                   /* +4 */
    int16  active;                 /* +6 */
    int16  flags;                  /* +8 */
    int16  alertLevel;             /* +A */
    int16  threatTimer;            /* +C */
    int16  symbol;                 /* +E */
};
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
};                                    /* 36 bytes */

/* ==== seg000:0x3dc2 ==== */
struct CommData;
struct MissileSpec { int16 weaponIdx; int16 ammo; };

void findWaypointFeatures(void);
void initFrameRandom(void);
void hwPortWrite(int16 cmd);
void recordFrame(uint8 a, uint8 b);
void initTacMapView(void);               /* sub_18751 */
void drawStatusItem(int16 idx, int16 val); /* sub_19007 */
void drawMissionObjectives(void);        /* sub_1A300 */
void setupLodDistances(void);            /* sub_1DFB4 */
void recalcTimeScale(void);
int16 clampRange(int16 v, int16 lo, int16 hi);
int16 plotMapObject(int16 x, int16 y, int16 color, int16 big);  /* sub_18B41 */
int16 readMapPixelColor(int16 x, int16 y);/* sub_18BEA */
void redrawTacMap(int16 x, int16 y);     /* sub_187EC */
void updateThreatSites(void);
void updateObjects(void);
void updateThreatTargeting(void);
void spawnSamThreat();                   /* sub_1585C */
void tickWeaponSlots(void);
void updateBulletsAndFire(void);
void updateTracerParticles(void);
void applyGravityFall(void);
int16 rangeApprox(int16 dx, int16 dy);   /* sub_1D23B */
void spawnEnemyAircraft(int16 slot, int16 objType); /* sub_16AD2 */
int16 markTargetReached(int16 n);        /* sub_17AAF */
void hudMessage(const char *s);          /* sub_192B1 */
void makeSound(int16 id, int16 pri);     /* sub_1DEF2 */
void setDrawColor(int16 color);          /* sub_18E7D */
void fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2); /* sub_18E93 */
void far gfx_flipPage(void);        /* sub_2F17E */
void far gfx_waitRetrace(void);          /* sub_2F183 */
#define gfx_waitRetrace2 gfx_waitRetrace
void far gfx_waitRetrace(void);         /* sub_2F188 */
void waitFrameSync(int16 ticks);         /* sub_104E2 */
void commitCommSnapshot(int16 arg);
void updateHudGauge(void);
struct TileObject *findNearestTileObject(uint32 wx, uint32 wy);  /* sub_11092 */
int16 shapeDataOffset(int16 shapeId);    /* sub_1D1C8 */
void addTileEntry(struct TileObject *rec, int16 value, char tag); /* sub_112DC */
struct TileObject;
int16 abs(int16 v);

void updateFrame(void) {
    int16 t2, pad;
    uint16 v;
    int16 i, obj;
    uint16 FAR *fpw;

    g_viewX_ = (int16)((g_worldX + 0x10) >> 5);
    g_viewY_ = -((int16)((g_worldY + 0x10) >> 5) - 0x8000);

    if (g_initPhase == 1) {
        findWaypointFeatures();
        g_threatActiveTimer = 0;
        g_scopeSweepTimer = 1;
        g_airTargetLock = g_groundTargetLock = -1;
        g_fireCooldown = g_playerPlaneFlags = g_weaponMask = missileSpecIndex =
            g_autopilotAltitude = waypointIndex = g_wpSelectIdx = 0;
        g_closestThreatIndex = g_unusedEventHist0 = g_unusedEventHist1 =
            g_unusedEventHist2 = (int8)(g_halfScaleRender = 0);
        g_threatRefX = g_threatRefY = g_threatRefZ = 0;
        g_prevThreatIndex = g_smokeSourceIdx = -1;
        g_fuelRemaining = 10000;
        g_gunHits = 2;
        g_currentWeaponType = 0;
        g_frameTimingAccum = 0xC;
        g_frameRateScaling = 4;
        recalcTimeScale();
        g_mapZoomLevel = 1;
        g_radarScopeRange = 1;
        g_northSouthSign = (((int8 FAR *)g_viewParamsFar)[0x38] & 1) ? 1 : -1;
        if ((g_planeTable[g_selStoreIdx].flags & 0x200) != 0) {
            g_worldX -= (int32)(g_northSouthSign * 0x80);
            *(int8 *)&g_playerPlaneFlags |= 8;
        } else {
            g_worldY -= (int32)(0x708 * g_northSouthSign);
        }
        initFrameRandom();
        hwPortWrite(0xD6);
        recordFrame(8, 0);
        initTacMapView();
        drawStatusItem(3, 0xA);
        drawMissionObjectives();
        g_isCampaignMission = g_viewParamsFar[0x1D];
        g_missionStatus = g_viewParamsFar[0x1F];
        g_detailLevel = ((uint16 FAR *)commData)[0x19];
        setupLodDistances();
        ((uint16 FAR *)commData)[0x13] = 1;
        if (((int8 FAR *)g_viewParamsFar)[0x3C] & 2) {
            ((int8 *)&g_playerPlaneFlags)[1] |= 0x10;
        }
        for (i = 0; i < 4; i++) {
            fpw = (uint16 FAR *)commData + i;
            missleSpec[i].weaponIdx = fpw[0x1C];
            missleSpec[i].ammo = fpw[0x20];
        }
        g_initPhase = 2;
        gfx_flipPage();
        gfx_waitRetrace2();
    }

    v = clampRange(g_viewX_, 0x100, 0x7E00);
    if (v != g_viewX_) {
        g_viewX_ = v;
        g_worldX = (int32)v << 5;
    }
    v = clampRange(g_viewY_, 0x200, 0x7D00);
    if (v != g_viewY_) {
        g_viewY_ = v;
        g_worldY = (int32)(uint16)(0x8000 - g_viewY_) << 5;
    }

    plotMapObject(g_markerPosX, g_markerPosY, g_trackedEnemyIdx, 1);
    g_trackedEnemyIdx = readMapPixelColor(g_viewX_, g_viewY_);
    if (plotMapObject(g_viewX_, g_viewY_, 0xF, 1) != 0) {
        redrawTacMap(g_viewX_, g_viewY_);
    }
    g_markerPosX = g_viewX_;
    g_markerPosY = g_viewY_;

    updateThreatSites();
    updateObjects();
    updateThreatTargeting();
    spawnSamThreat();
    tickWeaponSlots();
    updateBulletsAndFire();
    updateTracerParticles();
    applyGravityFall();

    if (g_threatActiveTimer != 0) {
        g_threatActiveTimer--;
    }

    if ((*(int8 *)&frameTick & 7) == 0) {
        g_prevThreatIndex = g_closestThreatIndex;
        g_nearestThreatRange = 0x7FFF;
        for (i = 0; i < g_storeDefCount; i++) {
            if ((g_planeTable[i].flags & 0x201) != 0 &&
                (g_planeTable[i].flags & 0x500) != 0) {
                t2 = rangeApprox(g_viewX_ - g_planeTable[i].mapX,
                                 g_viewY_ - g_planeTable[i].mapY);
                if (t2 < g_nearestThreatRange) {
                    g_nearestThreatRange = t2;
                    g_closestThreatIndex = i;
                }
            }
        }
        if (g_prevThreatIndex != g_closestThreatIndex &&
            (g_planeTable[g_closestThreatIndex].flags & 0x800) == 0) {
            for (i = 1; i <= 2; i++) {
                g_simObjects[g_groundUnitCount - i].flags.b[0] &= ~2;
                g_simObjects[g_groundUnitCount - i].spec =
                    (g_planeTable[g_closestThreatIndex].flags & 0x400) ? 8 : 0;
                if (g_planeTable[g_closestThreatIndex].flags & 0x100) {
                    g_simObjects[g_groundUnitCount - i].spec = 0x12;
                }
                g_simObjects[g_groundUnitCount - i].objType = g_closestThreatIndex;
            }
            for (i = 3; i <= 4; i++) {
                obj = g_groundUnitCount - i;
                g_simObjects[obj].flags.b[0] |= 2;
                g_simObjects[obj].posX = g_planeTable[g_closestThreatIndex].mapX;
                g_simObjects[obj].posY = g_planeTable[g_closestThreatIndex].mapY;
                if ((g_planeTable[g_closestThreatIndex].flags & 0x200) != 0) {
                    g_simObjects[obj].posX += g_northSouthSign * 5;
                    g_simObjects[obj].posY += (i & 1) * g_northSouthSign * 0x10;
                    g_simObjects[obj].alt = 0x84;
                } else {
                    g_simObjects[obj].posX += 10;
                    g_simObjects[obj].posY += ((i + g_closestThreatIndex) & 3) * 0x10;
                    g_simObjects[obj].alt = 4;
                }
                g_simObjects[obj].worldX = (int32)g_simObjects[obj].posX << 5;
                g_simObjects[obj].worldY = (int32)(uint16)g_simObjects[obj].posY << 5;
                g_simObjects[obj].heading.w = -randomRange(0x4000);
                g_simObjects[obj].spec =
                    (g_planeTable[g_closestThreatIndex].flags & 0x400) ? 8 : 0xB;
                if (g_planeTable[g_closestThreatIndex].flags & 0x100) {
                    g_simObjects[obj].spec = 9;
                }
            }
        }
        if ((*(int8 *)&frameTick & 0x7F) == 0) {
            if ((g_planeTable[g_closestThreatIndex].flags & 0x800) == 0) {
                obj = (*(int8 *)&frameTick & 0x80) ? g_groundUnitCount - 1
                                                 : g_groundUnitCount - 2;
                if ((g_simObjects[obj].flags.b[0] & 2) == 0) {
                    spawnEnemyAircraft(obj, g_closestThreatIndex);
                    g_simObjects[obj].flags.w = 0x207;
                    g_simObjects[obj].alt = 0x3E8;
                    g_simObjects[obj].speed = 0xFA;
                    g_simObjects[obj].worldY += (int32)(g_northSouthSign * 0x3000);
                }
            }
            g_unusedEventHist2 = g_unusedEventHist1;
            g_unusedEventHist1 = g_unusedEventHist0;
            g_unusedEventHist0 = 0;
        }
    }

    if (g_nearestThreatRange < 0x200 || g_groundAltitude == g_viewZ) {
        g_groundAltitude = 0;
        g_threatProxX = 0xA0;
        g_threatProxY = 0x800;
        if ((g_planeTable[g_closestThreatIndex].flags & 0x800) != 0) {
            g_threatProxY = 0x400;
        }
        if ((g_planeTable[g_closestThreatIndex].flags & 0x200) != 0) {
            g_groundAltitude = 0x80;
            g_threatProxX = 0x100;
            g_threatProxY = 0x3C0;
            if (g_viewZ == 0x80 && g_knots > 0x50) {
                if ((uint16)(g_viewY_ - g_planeTable[g_closestThreatIndex].mapY) * g_northSouthSign >= 0x10 &&
                    (uint16)(g_viewY_ - g_planeTable[g_closestThreatIndex].mapY) * g_northSouthSign <= 0x14 &&
                    abs(g_ourHead - ((1 - g_northSouthSign) << 0xE)) < 0x2000) {
                    g_autoCrashDive = 1;
                    makeSound(0x16, 2);
                }
            }
        }
        if (g_viewParamsFar[0x20] == 1) {
            g_threatProxX += 0x80;
            g_threatProxY += 0x100;
        }
        if (abs(g_viewX_ - g_planeTable[g_closestThreatIndex].mapX) > (g_threatProxX >> 5) ||
            abs(g_viewY_ - g_planeTable[g_closestThreatIndex].mapY) > (g_threatProxY >> 5)) {
            g_groundAltitude = 0;
            g_inLandingCorridor = 0;
        } else {
            g_inLandingCorridor = 1;
            if (g_knots <= 1 && (*(int8 *)&frameTick & 0xF) == 0) {
                for (i = 0; i < 2; i++) {
                    if (g_targetSlots[i].planeIndex == g_closestThreatIndex &&
                        g_targetSlots[i].state == 4 &&
                        g_missionTick < g_missionTimeLimit) {
                        markTargetReached(i);
                        hudMessage("Cargo delivered");
                        missleSpec[i].ammo = 0;
                    }
                }
                if ((g_planeTable[g_closestThreatIndex].flags & 0x500) != 0 &&
                    g_landingTimer != 0 &&
                    (g_planeTable[g_closestThreatIndex].flags & 0x800) == 0) {
                    if (g_landingTimer++ == 1) {
                        hudMessage("Safe Landing");
                    }
                    if (g_landingTimer > g_frameRateScaling) {
                        commitCommSnapshot(0);
                    }
                }
            }
        }
    } else {
        g_inLandingCorridor = 0;
    }

    if (g_inLandingCorridor == 0) {
        if (g_viewZ == 0) {
            if ((g_viewParamsFar[0x20] != 0 || g_gunHits > 4 || g_fuelRemaining == 0) &&
                g_ejectState == 0 && g_knots > 0x32) {
                makeSound(0, 2);
                setDrawColor(0);
                fillRectBoth(0, 0, 0x13F, 0xC7);
                waitFrameSync(0x78);
                commitCommSnapshot(1);
            }
        } else {
            g_landingTimer = 1;
        }
    }

    if (g_savedPosVisible != 0 && (g_viewMode & 0x80) == 0) {
        if (g_viewParamsFar[0x20] != 0 && g_altitude != 0) {
            makeSound(0, 2);
            gfx_waitRetrace();
            waitFrameSync(0x78);
            commitCommSnapshot(2);
        } else {
            g_altitude += 0x1F4;
            g_autopilotAltitude = 0;
        }
    }

    g_targetLeadAngle =
        ((g_planeTable[g_closestThreatIndex].flags & 0x200) != 0 &&
         g_nearestThreatRange < 0x500)
            ? ((g_northSouthSign << 8) / g_frameRateScaling + g_targetLeadAngle) & 0xFFF
            : 0;

    frameTick++;
    if (frameTick % g_frameRateScaling == 0) {
        g_missionTick++;
        for (i = 0; i < 2; i++) {
            if ((*(int8 *)&g_targetSlots[i].flags & 0x20) != 0 &&
                g_missionTick >= g_missionTimeLimit &&
                g_groundAltitude != g_viewZ &&
                !(*(int8 *)&g_planeTable[g_targetSlots[i].planeIndex].flags & 0x80)) {
                g_nearestTileObj = findNearestTileObject(
                    (uint32)(uint16)g_planeTable[g_targetSlots[i].planeIndex].mapX << 5,
                    -((uint32)(uint16)g_planeTable[g_targetSlots[i].planeIndex].mapY - 0x8000) << 5);
                if (g_nearestTileObj != 0) {
                    addTileEntry(g_nearestTileObj,
                                 shapeDataOffset(g_gndTargetMark + 0x100),
                                 g_gndTargetMark + 0x100);
                }
                *(int8 *)&g_planeTable[g_targetSlots[i].planeIndex].flags |= 0x80;
            }
        }
        if ((*(int8 *)&g_missionTick & 0x1F) == 0) {
            recordFrame(9, 0);
        }
        if (g_missionTick == 2) {
            makeSound(0x20, 2);
        }
    }

    if (++g_frameRateAccum >= (uint16)(g_frameRateScaling * 4)) {
        g_frameTimingAccum -= (g_frameSyncWait * 2 - 1) * g_frameRateScaling * 2;
        if (g_frameTimingAccum < 4) {
            g_frameTimingAccum = 4;
        }
        v = clampRange((int16)((uint16)(0x3C0 * g_frameRateScaling) /
                               (g_frameTimingAccum * g_timeAccelMode)), 1, 0xFF);
        g_frameRateAccum = g_frameTimingAccum = 0;
        if (abs(g_frameRateScaling * 4 - (int16)v) > 3) {
            g_frameRateScaling = (int16)(v + 2) >> 2;
            recalcTimeScale();
        }
        g_enemyAlertFlag = 0;
        for (i = 3; i < g_targetEntityCount; i++) {
            if (g_planeTable[i].alertLevel > 0xC0 &&
                !(*(int8 *)&g_planeTable[i].flags & 0x80)) {
                g_enemyAlertFlag++;
                break;
            }
        }
        for (i = 0; i < g_groundUnitCount; i++) {
            if (g_simObjects[i].damage > 0xC0 &&
                (g_simObjects[i].flags.b[0] & 2) != 0) {
                g_enemyAlertFlag++;
                break;
            }
        }
    }

    updateHudGauge();
}

/* ==== seg000:0x4769 ==== */
int16 dispatchKeyCmd(int16 key);         /* sub_1D4C6 */
int16 signExtendByte(int16 v);           /* sub_1D419 */
int16 computeBearing(int16 dx, int16 dy);/* sub_1D29D */
int16 abs(int16 v);
void updateHudGauge(void) {
    int16 acc, mag, dev, val, x, t, dd, ee;

    dispatchKeyCmd(g_keyCode);
    val = abs(signExtendByte(g_ourRoll >> 8));
    if (val >= 0x40)
        val = 0x80 - val;
    g_threatScopeRange = computeBearing((uint16)g_viewZ / 3 * (g_missionStatus + 1), 0x800) >> 8;
    g_threatScopeRange += (val >> 1) + ((uint16)g_startRange >> 10);
    if (g_playerPlaneFlags & 4)
        g_threatScopeRange += 0x18;
    if (g_playerPlaneFlags & 0x10)
        g_threatScopeRange += 0x10;
}

/* ==== seg000:0x47f1 ==== */
struct FireRec { int16 viewX, viewY, unk4, type, timer, unkA; };  /* stride 0xC @0x5230 */
void hudMessage(const char *s);          /* sub_192B1 */
void makeSound(int16 id, int16 pri);     /* sub_1DEF2 */
void countermeasures(int16 type) {
    int16 n, i;
    n = -1;
    if (g_eventTimers[type]-- <= 0) {
        g_eventTimers[type] = 0;
        hudMessage("Stores exhausted");
        return;
    }
    if (type == 3) {
        if (g_fireRecs[0].timer == 0 && !(g_weaponMask & 0x40))
            n = 0;
    } else {
        for (i = 1; i < 4; i++)
            if (g_fireRecs[i].timer == 0)
                n = i;
    }
    if (n != -1) {
        g_fireRecs[n].viewX = g_viewX_;
        g_fireRecs[n].viewY = g_viewY_;
        g_fireRecs[n].type = type;
        g_fireRecs[n].timer = (9 - 2 * g_missionStatus) * g_frameRateScaling;
        switch (type) {
        case 1:
            strcpy(g_nameBuf, "Flare");
            break;
        case 2:
            strcpy(g_nameBuf, "Chaff");
            break;
        case 3:
            strcpy(g_nameBuf, "Decoy");
            g_fireRecs[n].timer <<= 3;
            break;
        }
        strcat(g_nameBuf, " released");
        hudMessage(g_nameBuf);
    }
    makeSound(0x16, 2);
}

/* ==== seg000:0x4905 ==== */
struct WSlot { int16 state, timer, pad[4]; };      /* sizeof = 0xC */
void drawStatusItem(int16 idx, int16 val);         /* sub_19007 */
void tickWeaponSlots(void) {
    int16 i;
    for (i = 0; i < 4; i++)
        if (g_wpnSlots[i].timer != 0) {
            g_wpnSlots[i].timer--;
            if (g_wpnSlots[i].state == 3)
                drawStatusItem(7, g_wpnSlots[i].timer != 0 ? 0xC : 0);
            return;
        }
}

/* ==== seg000:0x4956 ==== */
struct BulletTrack { int16 posX, posY, alt, velX, velY, velZ; };  /* stride 0xC @0x9BA6 */
int16 readAxisInput(int16 axis);      /* sub_1D484 */
int16 sinMul(int16 angle, int16 val); /* sub_1D3EC */
int16 cosMul(int16 angle, int16 val); /* sub_1D404 */
int16 clampRange(int16 v, int16 lo, int16 hi);  /* sub_1D1FA */
void updateBulletsAndFire(void) {
    int16 tmp, sel, i, j, n;

    for (i = 0; i < g_bulletTrackCount + 4; i++) {
        if (bulletTracks[i].posX != 0) {
            bulletTracks[i].posX += bulletTracks[i].velX;
            bulletTracks[i].posY += bulletTracks[i].velY;
            bulletTracks[i].alt += bulletTracks[i].velZ;
        }
    }
    if (!(frameTick & 1)) {
        return;
    }
    n = (frameTick >> 1) % g_bulletTrackCount;
    if (!readAxisInput(0)) goto no_fire;
    if (g_gunAmmo <= 0) goto no_fire;
    if (g_ejectState != 0) goto no_fire;
    g_gunAmmo = clampRange(g_gunAmmo - 40 / g_frameRateScaling, 0, 1000);
    makeSound(4, 2);
    j = 0x70 / g_frameRateScaling;
    bulletTracks[n].velZ = sinMul(g_ourPitch, j) << 5;
    j = cosMul(g_ourPitch, j);
    bulletTracks[n].velX = sinMul(g_ourHead, j);
    bulletTracks[n].velY = -cosMul(g_ourHead, j);
    bulletTracks[n].posX = bulletTracks[n].velX + g_viewX_;
    bulletTracks[n].posY = bulletTracks[n].velY + g_viewY_;
    bulletTracks[n].alt = bulletTracks[n].velZ + g_viewZ - 2;
    g_gunFiredFlag = 1;
    return;
no_fire:
    bulletTracks[n].posX = 0;
    g_gunFiredFlag = 0;
}

/* ==== seg000:0x4aa8 ==== */
struct StoreDef {
    int16  subIdx;   /* +0x0 */
    uint16 coordX;   /* +0x2 */
    uint16 coordY;   /* +0x4 */
    int8   pad[8];   /* +0x6 */
    int16  nameIdx;  /* +0xE */
};                                              /* stride 0x10 */
struct Particle { int16 posX, posY, alt, spin; };   /* stride 8 */
int16 randomRange(int16 range);         /* sub_1D46B */
void updateTracerParticles(void) {
    int16 i, slot;
    if (g_smokeSourceIdx != -1) {
        for (i = 0; i < 8; i++) {
            g_particles[i].alt += 10;
            g_particles[i].posY += g_particles[i].alt >> 9;
            *(((char *)&g_particles[i].spin) + 1) += 6;
        }
        if (!((char)frameTick & 0x0f)) {
            slot = (frameTick >> 4) & 7;
            g_particles[slot].posX = g_storeDefs[g_smokeSourceIdx].coordX;
            g_particles[slot].posY = g_storeDefs[g_smokeSourceIdx].coordY;
            g_particles[slot].alt = 0x80;
            g_particles[slot].spin = randomRange(0x100) << 8;
            g_smokeParticleSlot = slot;
        }
    }
}

/* ==== seg000:0x4b38 ==== */
void applyGravityFall(void) {
    if (g_wreckAlt > 0) {
        if (g_wreckFallVel > -16) {
            g_wreckFallVel -= 12;
        }
        g_wreckAlt += g_wreckFallVel;
    }
}

/* ==== seg000:0x4b53 ==== */
void seedRng(void);
void clearStatusPanel(void);
int16 randomRange(int16 range);
void initFrameRandom(void) {
    int16 seedSum, unused0, unused1, unused2;
    seedRng();
    clearStatusPanel();
    frameTick = randomRange(0x1000) & 0x7ff8;
    seedSum = g_setupSlots[4] + g_setupSlots[0xD];
    g_nightMode = ((seedSum & 3) == 0);
    if (g_setupSlots[0] == 1 || g_setupSlots[9] == 1) {
        g_nightMode = 0;
    }
    if (g_setupSlots[0] == 4 || g_setupSlots[9] == 4) {
        g_nightMode = 1;
    }
    g_unusedFrameVal = (seedSum & 0xF) << 8;
    g_missionTick = 0;
}

/* ==== seg000:0x4bc8 ==== */
void resetSimObjectLocks(void) {
    int16 i;
    for (i = 0; i < g_groundUnitCount; i++) {
        ((struct SimObject *)g_simObjects)[i].terrainColor = -1;
    }
    g_trackedEnemyIdx = -1;
}

/* ==== seg000:0x4c05 ==== */
void initWeaponLoadout(void) {
    int16 i;
    i = g_gunHits = g_bombDamageMask = 0;
    do {
        g_stores[i][0] = 9;
        i++;
    } while (i < 4);
    g_gunAmmo = 0x3E8;
    g_fuelRemaining = 0x1388;
}

/* ==== seg000:0x4c3c ==== */
struct CommSnap {
    int8  pad_0[0x26];
    int16 type;                     /* +0x26 */
    int16 arg;                      /* +0x28 */
    int8  pad_2a[0x0A];
    int16 gunHits;                  /* +0x34 */
    int16 dmgMask;                  /* +0x36 */
    int8  pad_38[0x3C];
    int16 viewY;                    /* +0x74 */
    int16 viewX;                    /* +0x76 */
};
struct CommData;
void recordFrame(uint8 a, uint8 b);
void commitCommSnapshot(int16 arg) {
    if (g_ejectState == 0 || arg == 0) {
        g_commEventFlag = 1;
        ((struct CommSnap FAR *)commData)->arg = arg;
        if (arg == 0 && g_ejectState == 0)
            ((struct CommSnap FAR *)commData)->type = 3;
        ((struct CommSnap FAR *)commData)->viewY = g_viewY_;
        ((struct CommSnap FAR *)commData)->viewX = g_viewX_;
        ((struct CommSnap FAR *)commData)->gunHits = g_gunHits;
        ((struct CommSnap FAR *)commData)->dmgMask = g_bombDamageMask;
        recordFrame(8, 0);
    }
}

/* ==== seg000:0x4c98 ==== */
void hwPortWrite(int16 cmd);           /* sub_14CAC (noop hw thunk) */
void sendSoundCmd(uint8 v) {
    hwPortWrite((v << 8) + 0xDB);
}

/* ==== seg000:0x4caf ==== */
void recordFrame(uint8 a, uint8 b) {
    if (g_replayCount < 0xFF) {
        g_replayLog.events[g_replayCount].tick = g_missionTick;
        g_replayLog.events[g_replayCount].y = (uint16)g_viewY_ >> 7;
        g_replayLog.events[g_replayCount].x = (uint16)g_viewX_ >> 7;
        g_replayLog.events[g_replayCount].a = a;
        g_replayLog.events[g_replayCount].b = b;
        g_replayLog.events[g_replayCount += 1].a = 0;
    }
}

/* ==== seg000:0x4d03 ==== */
void buildStoreName(int16 i) {
    strcpy(g_nameBuf, g_nameTab[g_storeDefs[i].nameIdx & 0x7F]);
    if (strlen(g_nameTab[g_storeDefs[i].subIdx]) != 0) {
        strcat(g_nameBuf, " ");
        strcat(g_nameBuf, g_nameTab[g_storeDefs[i].subIdx]);
    }
    if ((int16)strlen(g_nameBuf) > 0x19) {
        g_nameBuf[0x18] = '.';
        g_nameBuf[0x19] = 0;
    }
}

/* ==== seg000:0x4d77 ==== */
void initStoreData(void) {
    int16 n, i;
    setCommWorldbufPtr();
    flagFarToNear = 1;
    moveStuff();
    g_nameTab[0] = g_strpool;
    n = 1;
    for (i = 0; i < 0x2EE; i++) {
        if (g_strpool[i] == 0 && n < 0x64)
            g_nameTab[n++] = &g_strpool[i + 1];
    }
    g_worldX = ((uint32)g_storeDefs[g_selStoreIdx].coordX << 5) + 2;
    g_worldY = ((int32)0x8000 - g_storeDefs[g_selStoreIdx].coordY) << 5;
}

/* ==== seg000:0x4e09 ==== */
#pragma pack(1)
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
#pragma pack()
struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY);
int16 shapeDataOffset(int16 shapeId);            /* sub_1D1C8 */
void addTileEntry(struct TileObject *rec, int16 value, char tag);   /* sub_112DC */

void findWaypointFeatures(void) {
    int16 nameIdx, slot;
    nameIdx = g_waypointNameBase;
    for (slot = 0; slot < 2; slot++) {
        if (g_targetSlots[slot].flags >> 8 != 0) {
            g_nearestTileObj = findNearestTileObject(
                (uint32)(uint16)g_storeDefs[g_targetSlots[slot].planeIndex].coordX << 5,
                ((int32)0x8000 - g_storeDefs[g_targetSlots[slot].planeIndex].coordY) << 5);
            if (g_nearestTileObj != 0) {
                g_shapeTargetCategory[nameIdx] = g_shapeTargetCategory[g_nearestTileObj->id];
                strcpy(g_nameTab[nameIdx], g_nameTab[g_nearestTileObj->id]);
                g_nameTab[nameIdx + 1] = g_nameTab[nameIdx] + strlen(g_nameTab[nameIdx]) + 1;
                addTileEntry(g_nearestTileObj, shapeDataOffset(nameIdx + 0x100), nameIdx + 0x100);
            }
            g_storeDefs[g_targetSlots[slot].planeIndex].nameIdx = nameIdx + 0x100;
            nameIdx++;
        }
    }
    g_render3DTiles = 0;
}

void moveStuff() {
    moveNearFar(g_landTargetId, 1);
    moveNearFar(g_waterTargetId, 1);
    moveNearFar(&g_planeCount, 2);
    moveNearFar(&g_targetEntityCount, 2);
    moveNearFar(&g_planeScanCount, 2);
    moveNearFar(g_planeTable, g_planeCount * (int16)sizeof(struct MapTarget));
    moveNearFar(&g_groundUnitCount, 2);
    moveNearFar(g_simObjects, g_groundUnitCount * (int16)sizeof(struct SimObject));
    moveNearFar(g_shapeTargetCategory, 0x64);
    moveNearFar(g_tileKillTally, 0x64);
    moveNearFar(g_stringPool, 0x2EE);
    moveNearFar(g_mapCellFlags, 0x100);
    moveNearFar(&g_unusedSavedWord, 2);
    moveNearFar(&g_padlockAircraft, 2);
    moveNearFar(waypoints, 0x10);
    moveNearFar(g_targetSlots, 0x24);
}

/* ==== seg000:0x5001 ==== */

void moveNearFar(void *nearPtr, int16 count) {
    void FAR *farPtr = nearPtr;
    if (flagFarToNear != 0) {
        movedata(FP_SEG(farPointer), FP_OFF(farPointer), FP_SEG(farPtr), FP_OFF(farPtr), count);
    } else {
        movedata(FP_SEG(farPtr), FP_OFF(farPtr), FP_SEG(farPointer), FP_OFF(farPointer), count);
    }
    farPointer += count;
}

/* ==== seg000:0x5045 ==== */
struct CommData { int8 pad[0x7A]; int8 worldBuf[1]; };

int16 setCommWorldbufPtr() {
    farPointer = (uint8 FAR *)&commData->worldBuf;
    return 0;
}

/* ==== seg000:0x4bf8 ==== */
void initFlightParams(void) {
    g_altitude = 0x3E8;
    g_startRange = 0x1900;
}
