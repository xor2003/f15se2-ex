// seg000 debug code (/Zi) - split from egmain.c
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "egflight.h"
#include "egframe.h"
#include "worldxfer.h"
#include "egkeys.h"
#include "egmath.h"
#include "egplayer.h"
#include "egtacmap.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "const.h"
#include "comm.h"
#include "net/protocol.h" /* F15_MAX_MAP_EVENTS: decoy pool bound */

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void updateFrame(void);
void dispatchKeyScancode();
void tickMessageTimers();
void moveBullets();
void tryPlayerFire();
void updateTracerParticles();
void applyGravityFall();
void initFrameRandom();
void generateRandomRadioMessage();
void findWaypointFeatures();
void moveStuff();
void moveNearFar(void *nearPtr, int16 count);
int16 setCommWorldbufPtr();
/* updateFrame is split into ordered segments (egframeseg.c, decls in
 * egframe.h): the P-segments only touch player-scoped globals, the W-segments
 * world state. Single-player calls them interleaved exactly as before; the
 * net server runs the W pass once and the P pass per PlayerSim context.
 * frameThreatScan/frameThreatEscort are defined in f15world.c — the scan's
 * world-mutating half was split out so the server can drive it with a
 * deterministic threat target. */

// ==== seg000:0x0720 ====
/* Original order, preserved for single-player:
 *   P-pre W-sims P-fire W-tracers P-gravity P-tacmap P-timers W-threat
 *   P-mission W-tick P-keys
 * The server calls updateWorldFrame() once + updatePlayerFrame() per ctx. */
void updateFrame(void) {
    framePlayerPre();
    updateThreatSites();
    updateObjects();
    updateThreatTargeting();
    tickMessageTimers();
    moveBullets();
    tryPlayerFire();
    updateTracerParticles();
    applyGravityFall();
    frameTacmapBlip();
    framePlayerTimers();
    frameThreatScan();
    frameThreatEscort(g_closestThreatIndex,
                      g_prevThreatIndex != g_closestThreatIndex);
    framePlayerMission();
    frameWorldTick();
    dispatchKeyScancode();
    /* gun tracer hit tests + damage: was inside drawWorldEffects at render
     * time; combat now resolves in the sim step regardless of rendering */
    simBulletHits();
    /* air/ground target acquisition: was inside updateTargetLock at render
     * time; locks now resolve in the sim step */
    simTargetLock();
}

/* The server's world-only/per-player compositions of these segments live in
 * f15world.c (updateWorldFrame/updatePlayerFrame); the segment bodies moved
 * to egframeseg.c so this file stays close to upstream. */

// ==== seg000:0x14e8 ====
void dispatchKeyScancode(void) {
    int16 unused0, unused1, unused2, unused3, unused4, unused5, unused6, unused7;
    keyDispatch(keyScancode);
}

// ==== seg000:0x14fc ====
void countermeasures(int16 eventType) {
    const char *name;
    int16 i, slot;

    slot = -1;
    if (g_eventTimers[eventType] <= 0) {
        g_eventTimers[eventType] = 0;
        hudMessage("Stores exhausted");
    } else {
        /* shared decoy pool: find space BEFORE spending stores - a full
         * pool rejects the release without consuming inventory (the old
         * order burned one store and deployed nothing) */
        for (i = 1; i < F15_MAX_MAP_EVENTS; i++) {
            if (mapEvents[i].ttl == 0)
                slot = i;
        }
        if (slot != -1) {
            g_eventTimers[eventType]--;
            mapEvents[slot].mapX = g_viewX_;
            mapEvents[slot].mapY = g_viewY_;
            mapEvents[slot].type = eventType;
            mapEvents[slot].ttl =
                -(g_missionStatus * 3 - 15) * g_frameRateScaling;
            switch (eventType) {
            case 1:
                name = "Flare";
                break;
            case 2:
                name = "Chaff";
                break;
            }
            strcpy(strBuf, name);
            strcat(strBuf, " released");
            hudMessage(strBuf);
            strcpy(strBuf, name);
            strcat(strBuf, ":");
            strcat(strBuf, itoa(g_eventTimers[eventType], g_itoaScratch, 10));
            setTimedMessage(strBuf);
            makeSound(22, 2);
        } else {
            hudMessage("Decoy slots full");
        }
    }
}

// ==== seg000:0x1636 ====
void tickMessageTimers(void) {
    int16 i;
    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        if (mapEvents[i].ttl != 0) {
            (mapEvents[i].ttl)--;
            if (mapEvents[i].ttl == 0) {
                mapEvents[i].type = 0;
            }
        }
    }
}

/* moveBullets/tryPlayerFire moved to egframeseg.c with the other updateFrame
 * segments (the round-slot allocation policy comments live there too). */

// ==== seg000:0x1841 ====
void updateTracerParticles() {
    int16 i, slot;

    if (g_smokeSourceIdx != -1) {
        for (i = 0; i < 8; i++) {
            g_particles[i].alt += 10;
            g_particles[i].posY += g_particles[i].alt >> 9;
            *(((char *)&g_particles[i].spin) + 1) += 6;
        }
        if (!((char)frameTick & 0x0f)) {
            slot = (frameTick >> 4) & 7;
            g_particles[slot].posX = g_planeTable.planes[g_smokeSourceIdx].mapX;
            g_particles[slot].posY = g_planeTable.planes[g_smokeSourceIdx].mapY;
            g_particles[slot].alt = 0x80;
            g_particles[slot].spin = randomRange(0x100) << 8;
            g_smokeParticleSlot = slot;
        }
    }
}

// ==== seg000:0x18d5 ====
void applyGravityFall() {
    if (g_wreckAlt > 0) {
        if (g_wreckFallVel > -16) {
            g_wreckFallVel -= 12;
        }
        g_wreckAlt += g_wreckFallVel;
    }
}

// ==== seg000:0x18f6 ====
void initFrameRandom(void) {
    int16 seedSum, unused0, unused1, unused2;

    seedRng();
    clearStatusPanel();
    frameTick = randomRange(0x1000) & 0x7ff8;
    seedSum = g_targetSlots[0].seedNoise + g_targetSlots[1].seedNoise;
    g_nightMode = (gameData->theater == 6 ? 5 : 9) < randomRange(0x10);
    if (g_nightMode && g_dacSupported) {
        setupDac();
    }
    g_unusedFrameVal = (seedSum & 0xF) << 8;
    g_missionTick = 0;
}

// ==== seg000:0x1971 ====
void resetSimObjectLocks() {
    g_trackedEnemyIdx = -1;
}

// ==== seg000:0x19a3 ====
void initWeaponLoadout() {
    int16 i;

    i = g_gunHits = g_bombDamageMask = 0;
    for (; i < 3; i++) {
        missleSpec[i].weaponIdx = commData->weaponType[i];
        missleSpec[i].ammo = commData->weaponCount[i];
    }
    g_gunAmmo = 1000;
    g_fuelRemaining = 10000;
    g_eventTimers[2] = 18;
    g_eventTimers[1] = 12;
    drawWeaponAmmo();
    drawFuelGauge();
    UpdateThrottleState();
}

// ==== seg000:0x1a18 routine_131 ====
void drawWeaponAmmo() {
    int16 x, i;

    if (g_hudVisible == 0) {
        return;
    }
    for (i = 0; i < 3; i++) {
        setDrawColor(COLOR_BLACK);
        x = g_tacmapIndicators[i];
        fillRectBoth(x - 1, 190, x + 2, 194);
        drawNumber(missleSpec[i].ammo, x, 190, 0x0c);
    }
}

// ==== seg000:0x1a88 ====
void drawWeaponSelectMarker(int16 weaponIdx) {
    if (g_hudVisible == 0) return;
    g_pageFront[2] = 0;
    drawFullscreenLine(g_weaponMarkerBoxX[g_weaponMarkerSel], 196, g_weaponMarkerBoxX[g_weaponMarkerSel] + 6, 196);
    g_pageFront[2] = 7;
    drawFullscreenLine(g_weaponMarkerBoxX[g_weaponMarkerSel], 197, g_weaponMarkerBoxX[g_weaponMarkerSel] + 6, 197);
    g_pageFront[2] = 0x0c;
    drawFullscreenLine(g_weaponMarkerBoxX[weaponIdx], 196, g_weaponMarkerBoxX[weaponIdx] + 6, 196);
    g_pageFront[2] = 4;
    drawFullscreenLine(g_weaponMarkerBoxX[weaponIdx], 197, g_weaponMarkerBoxX[weaponIdx] + 6, 197);
    g_weaponMarkerSel = weaponIdx;
}

// ==== seg000:0x1b37 routine_148 ====
void finalizeMission(int outcome) {
    if (g_ejectState != 0 && outcome != 0) {
        return;
    }
    g_missionEndedFlag[0] = 1;
    commData->bailoutSurvived = outcome;
    /* Landing type the debrief reads (1=crashed, 2=ejected, 3=landed). START
     * defaults it to 3, so crash/eject must be set or the debrief reports a landing. */
    if (g_ejectState != 0) {
        commData->landingType = 2;
    } else if (outcome == 0) {
        commData->landingType = 3;
    } else {
        commData->landingType = 1;
    }
    /* Debrief handoff. enbrief.c derives the map grid from worldX/worldY. */
    commData->worldX = g_viewX_;
    commData->worldY = g_viewY_;
    commData->weaponCount[0] = g_finalThreatScore;
    commData->weaponCount[1] = g_resupplyCount;
    commData->gunHits = g_gunHits;
    appendMapEvent(8, 0);
}

// ==== seg000:0x1bc3 ====
void scheduleEventCheck(int16 eventObjIdx, uint16 priority) {
    if (priority > (uint16)g_directorMode) return;
    if (g_directorEventDeadline != -1) return;
    g_viewTargetObj = eventObjIdx;
    scheduleTimedEvent(VIEW_MISSILE, g_directorMode == 1 ? 3 : 4);
}

// ==== seg000:0x1bfd scheduleTimedEvent ====
void scheduleTimedEvent(ViewMode viewMode, int16 delay) {
    if (g_directorMode == 0) {
        return;
    }
    g_viewMode = viewMode;
    g_directorEventDeadline = delay * g_frameRateScaling + frameTick;
}

// ==== seg000:0x1c21 routine_180 ====
void generateRandomRadioMessage(void) {
    int16 idx;

    if (g_directorEventDeadline != -1) {
        return;
    }
    g_autopilotAltitude = 500;
    g_directorMode = 2;
    switch (randomRange(3)) {
    case 0:
        idx = randomRange(g_planeCount - 3) + 3;
        g_viewTargetObj = idx + 0x40;
        g_viewMode = VIEW_MISSILE;
        placeString(idx);
        hudMessage(strBuf);
        break;
    case 1:
        do {
            idx = randomRange(g_groundUnitCount);
        } while (g_simObjects[idx].speed == 0);
        g_viewTargetObj = idx + 0x20;
        g_viewMode = VIEW_MISSILE;
        strcpy(strBuf, simObjectTypeName(idx));
        strcat(strBuf, " on patrol");
        hudMessage(strBuf);
        break;
    case 2:
        g_viewMode = VIEW_EXT_FOLLOW;
        hudMessage("F15 Strike Eagle");
        break;
    }
}

// ==== seg000:0x1d10 ====
void appendMapEvent(int16 eventType, int16 eventArg) {
    simEventsMapEvent(eventType, eventArg); /* net: forward as NE_MAP_EVENT */
    if (g_eventLogCount >= 255) {
        return;
    }
    g_replayLog.events[g_eventLogCount].coord = g_missionTick;
    g_replayLog.events[g_eventLogCount].screenX = (uint16)g_viewX_ >> 7;
    g_replayLog.events[g_eventLogCount].screenY = (uint16)g_viewY_ >> 7;
    g_replayLog.events[g_eventLogCount].type = eventType;
    g_replayLog.events[g_eventLogCount].arg = eventArg;
    g_eventLogCount++;
    g_replayLog.events[g_eventLogCount].type = 0;
}

// ==== seg000:0x1d6e placeString ====
void placeString(int16 waypointIdx) {
    strcpy(strBuf, g_targetNameTable[(g_planeTable.planes[waypointIdx].nameIndex) & 0x7f]);
    if (strlen(g_targetNameTable[((int16 *)&g_planeTable)[waypointIdx * 8]])) {
        if (strlen(g_targetNameTable[(g_planeTable.planes[waypointIdx].nameIndex) & 0x7f])) {
            strcat(strBuf, " at ");
        }
        strcat(strBuf, g_targetNameTable[((int16 *)&g_planeTable)[waypointIdx * 8]]);
    }
    if ((int16)strlen(strBuf) > 25) {
        g_strTruncDot = '.';
        g_strTruncTerm[0] = 0;
    }
}

// ==== seg000:0x1e0e ====
void initMissionStrings() {
    int16 nameIdx, i;
    /* Net clients receive the world tables in MISSION_SETUP instead of
     * importing from the local START state. */
    if (!g_netClientMode)
        worldImportToEgame();
    g_targetNameTable[0] = g_stringPool;
    nameIdx = 1;
    for (i = 0; i < 750; ++i) {
        if (g_stringPool[i] == 0 && nameIdx < 100) {
            g_targetNameTable[nameIdx++] = &g_stringPool[i + 1];
        }
    }
    if (gameData->difficulty != 0) { // 1e6c
        g_ViewX = ((int32)(g_planeTable.planes[g_targetSlots[0].viewIndex].mapX) << 5) + 2;
        g_ViewY = (0x8000 - (int32)(g_planeTable.planes[g_targetSlots[0].viewIndex].mapY)) << 5;
    } else {
        g_ViewX = ((int32)waypoints[0].mapX << 5) + 2;
        g_ViewY = (0x8000 - (int32)waypoints[0].mapY) << 5;
    }
    g_viewX_ = (g_ViewX + 0x10) >> 5;
    g_viewY_ = 0x8000 - ((g_ViewY + 0x10) >> 5);
}

// ==== seg000:0x1f3e ====
void findWaypointFeatures() {
    int16 nameIdx, slot;

    nameIdx = size3d3;
    for (slot = 0; slot < 2; slot++) {
        if (g_targetSlots[slot].flags >> 8 != 0) {
            g_nearestTileObj = findNearestTileObject(
                (uint32)(uint16)g_planeTable.planes[g_targetSlots[slot].planeIndex].mapX << 5,
                (0x8000L - (uint32)(uint16)g_planeTable.planes[g_targetSlots[slot].planeIndex].mapY) << 5);
            if (g_nearestTileObj != 0) {
                g_shapeTargetCategory[nameIdx] = g_shapeTargetCategory[g_nearestTileObj->id];
                /* src may alias dst when the tile maps to this very name
                 * slot - strcpy(x,x) is UB (ASan abort; seen on the server) */
                if (g_targetNameTable[nameIdx] != g_targetNameTable[g_nearestTileObj->id])
                    strcpy(g_targetNameTable[nameIdx], g_targetNameTable[g_nearestTileObj->id]);
                g_targetNameTable[nameIdx + 1] = g_targetNameTable[nameIdx] + strlen(g_targetNameTable[nameIdx]) + 1;
                addTileEntry(g_nearestTileObj, shapeDataOffset(nameIdx + 0x100), nameIdx + 0x100);
            }
            g_planeTable.planes[g_targetSlots[slot].planeIndex].nameIndex = nameIdx + 0x100;
            nameIdx++;
        }
    }
    g_render3DTiles = 0;
}
