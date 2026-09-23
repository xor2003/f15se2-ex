/*
 * f15world.c - authoritative-server frame orchestration.
 *
 * updateFrame() in egframe.c is split into ordered segments: P-segments only
 * touch player-scoped globals, W-segments world state. Single-player calls
 * them interleaved exactly as before (egframe.c). The server composes them
 * differently here:
 *
 *   - updateWorldFrame()  runs once per tick: world entities advance once.
 *   - updatePlayerFrame() runs per PlayerSim ctx: player-relative work
 *     (threat guidance, nearest-threat scan, firing, timers, mission, keys)
 *     lands on the correct player. Each in-flight projectile carries an
 *     explicit owner (targetPlayer), so guidance/damage resolve under the
 *     victim's or shooter's own ctx instead of whichever ctx was resident.
 *   - frameThreatEscort() is the world-mutating half of the original threat
 *     scan (escort/interceptor spawns + waypoint 3 publish). The server calls
 *     it once with a deterministic target rather than the resident ctx's.
 *
 * frameThreatScan()/frameThreatEscort() are called by updateFrame() too, so
 * this file lives in the core lib; the server-only helpers (threat victim
 * selection, ctx juggling) stay in server/f15server.cpp.
 */
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "egframe.h"
#include "egplayer.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "struct.h"

/* egframe.c segment functions shared with the single-player updateFrame()
 * (non-static but not declared in a header - same convention as egframe.c). */
void framePlayerPre(void);
void framePlayerTimers(void);
void framePlayerMission(void);
void frameWorldTick(void);
void tickMessageTimers(void);
void moveBullets(void);
void tryPlayerFire(void);
void updateTracerParticles(void);
void applyGravityFall(void);
void dispatchKeyScancode(void);
void framePlayerAlertScan(void);

/* World-only pass: runs once per tick while a valid player ctx is resident.
 * Player-relative work moved to updatePlayerFrame(): updateThreatTargeting
 * (each shot runs under its owner's ctx) and frameThreatScan (per-player
 * nearest-threat bookkeeping). The world-side escort spawn is invoked
 * separately via frameThreatEscort() so the server picks the target. */
void updateWorldFrame(void) {
    updateThreatSites();
    updateObjects();
    tickMessageTimers();
    moveBullets();
    updateTracerParticles();
    frameWorldTick();
}

/* Per-player pass: runs once per tick per PlayerSim ctx. */
void updatePlayerFrame(void) {
    framePlayerPre();
    updateThreatTargeting();
    tryPlayerFire();
    applyGravityFall();
    if (!g_headlessSim)
        frameTacmapBlip(); /* presentation-only: no renderer on the server */
    framePlayerTimers();
    frameThreatScan();
    framePlayerAlertScan();
    framePlayerMission();
    dispatchKeyScancode();
    /* gun tracer hit tests under the shooter's/victim's own ctx (was
     * render-side inside drawWorldEffects) */
    simBulletHits();
    /* authoritative air/ground target acquisition under this ctx (was
     * render-side inside updateTargetLock) */
    simTargetLock();
}

/* Player-relative part of the threat scan: nearest-threat bookkeeping for
 * the resident ctx (closestThreatIndex/nearestThreatRange drive waypoint 3,
 * the padlock and warning cues). Runs per player ctx on the server. */
void frameThreatScan(void) {
    int16 tmp;
    int16 i;

    /* RWR sweep debounce: a per-player cockpit timer (set by
     * fireGroundThreat under the victim's ctx), so it ticks down in the
     * player pass where every ctx's timers advance exactly once. */
    g_scopeSweepTimer--;

    if ((frameTick & 7) != 0) goto skip_target_section;

    g_prevThreatIndex = g_closestThreatIndex;
    g_nearestThreatRange = 0x7fff;
    for (i = 0; i < g_planeCount; i++) {
        if ((g_planeTable.planes[i].flags & 0x201) != 0 &&
            (g_planeTable.planes[i].flags & 0x500) != 0 &&
            (g_planeTable.planes[i].flags & 0x800) == 0) {
            tmp = rangeApprox(g_viewX_ - g_planeTable.planes[i].mapX, g_viewY_ - g_planeTable.planes[i].mapY);
            if (tmp < g_nearestThreatRange) {
                g_nearestThreatRange = tmp;
                g_closestThreatIndex = i;
            }
        }
    }
    if (g_prevThreatIndex != g_closestThreatIndex)
        g_targetSlots[1].viewIndex = g_closestThreatIndex;

    if ((frameTick & 0x7f) == 0) {
        g_unusedEventHist2 = g_unusedEventHist1;
        g_unusedEventHist1 = g_unusedEventHist0;
        g_unusedEventHist0 = 0;
    }

skip_target_section:;
}

/* World-mutating part of the threat scan: waypoint-3 publish plus the
 * escort/interceptor spawn block. threatIdx/threatChanged come from the
 * caller: single-player passes its own ctx result (egframe.c updateFrame);
 * the server feeds the most-threatened player's scan result so the spawn
 * decision is deterministic and not bound to the resident ctx. */
void frameThreatEscort(int16 threatIdx, int16 threatChanged) {
    int16 i, objIdx;

    if ((frameTick & 7) != 0) goto skip_escort_section;

    if (threatChanged) {
        waypoints[3].mapX = g_planeTable.planes[threatIdx].mapX;
        waypoints[3].mapY = g_planeTable.planes[threatIdx].mapY;
    }

    if (threatChanged && (g_planeTable.planes[threatIdx].flags & 0x800) == 0) {
        for (i = 1; i <= 2; i++) {
            g_simObjects[g_groundUnitCount - i].flags.b[0] &= ~2;
            g_simObjects[g_groundUnitCount - i].spec = g_planeTable.planes[threatIdx].flags & 0x400 ? 13 : 0;
            if (g_planeTable.planes[threatIdx].flags & 0x100) {
                g_simObjects[g_groundUnitCount - i].spec = 18;
            }
            g_simObjects[g_groundUnitCount - i].objType = threatIdx;
        }
        for (i = 3; i <= 4; i++) {
            objIdx = g_groundUnitCount - i;
            g_simObjects[objIdx].flags.b[0] |= 2;
            g_simObjects[objIdx].posX = g_planeTable.planes[threatIdx].mapX;
            g_simObjects[objIdx].posY = g_planeTable.planes[threatIdx].mapY;
            if ((g_planeTable.planes[threatIdx].flags & 0x200) != 0) {
                g_simObjects[objIdx].posX += g_northSouthSign * 5;
                g_simObjects[objIdx].posY += (i & 1) * g_northSouthSign * 0x10;
                g_simObjects[objIdx].alt = 132;
            } else {
                g_simObjects[objIdx].posX += 10;
                g_simObjects[objIdx].posY += ((i + threatIdx) & 3) * 0x10;
                g_simObjects[objIdx].alt = 4;
            }
            g_simObjects[objIdx].worldX = (int32)g_simObjects[objIdx].posX << 5;
            g_simObjects[objIdx].worldY = (int32)g_simObjects[objIdx].posY << 5;
            g_simObjects[objIdx].heading.w = -randomRange(0x4000);
            g_simObjects[objIdx].spec = g_planeTable.planes[threatIdx].flags & 0x400 ? 8 : 11;
            if (g_planeTable.planes[threatIdx].flags & 0x100) {
                g_simObjects[objIdx].spec = 9;
            }
        }
    }

    if ((frameTick & 0x7f) == 0) {
        if ((g_planeTable.planes[threatIdx].flags & 0x800) == 0) {
            objIdx = frameTick & 0x80 ? g_groundUnitCount - 1 : g_groundUnitCount - 2;
            if ((g_simObjects[objIdx].flags.b[0] & 2) == 0) {
                spawnEnemyAircraft(objIdx, threatIdx);
                g_simObjects[objIdx].flags.w = 0x207;
                g_simObjects[objIdx].alt = 1000;
                g_simObjects[objIdx].speed = 250;
                g_simObjects[objIdx].worldY += g_northSouthSign * 0x3000;
            }
        }
    }

skip_escort_section:;
}

/* Per-player alert-flag refresh: frameWorldTick resets the cadence counter
 * (world state, once); the flagged-enemy scan is a pure world read but its
 * result lands in a ctx field, so each player's pass recomputes it on the
 * tick after the counter was reset. */
void framePlayerAlertScan(void) {
    int16 i;

    if (g_frameRateAccum != 0)
        return;
    g_enemyAlertFlag = 0;
    for (i = 3; i < g_targetEntityCount; i++) {
        if (g_planeTable.planes[i].alertLevel > 0xc0 &&
            (g_planeTable.planes[i].flags & 0x80) == 0) {
            g_enemyAlertFlag++;
            break;
        }
    }
    for (i = 0; i < g_groundUnitCount; i++) {
        if (g_simObjects[i].damage > 0xc0 &&
            (g_simObjects[i].flags.b[0] & 2) != 0) {
            g_enemyAlertFlag++;
            break;
        }
    }
}
