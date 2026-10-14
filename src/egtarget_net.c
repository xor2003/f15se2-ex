/*
 * egtarget_net.c - sim-step halves of target/combat code for the
 * authoritative server (split out of egtarget.c to keep that file close to
 * upstream).
 *
 * simTargetLock and simBulletHits run inside the sim update (single-player
 * under F15_NET: updateFrame; server: per player ctx) so locks, hits and
 * damage are authoritative and independent of render rate or screen
 * visibility. updateTargetLock()/drawWorldEffects() in egtarget.c keep the
 * drawing; the shared hit-box helpers (aircraftGunRadiusFine,
 * groundHitRadiusMap, roundToTargetDist2) live there too — non-static so
 * both TUs share them.
 */
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_rotation.hpp"
using f15::math::legacy::angleFromWord;
using f15::math::legacy::angleMagnitude;
using f15::math::legacy::mapWordX;
using f15::math::legacy::mapWordY;
using f15::math::legacy::mapPosition;
using f15::math::legacy::mapRangeDelta;
using f15::math::legacy::terrainFromUnits;
using f15::math::TickDuration;
#include "egflight.h"
#include "egframe.h"
#include "egmath.h"
#include "egplayer.h"
#include "egtacmap.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "r2d.h"
#include "r3dmesh.h"
#include "const.h"
#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* egtgt2.c helpers (typed WordRep convention, same as egtarget.c). */
f15::math::WordRep<f15::math::GameBackend> computeMapTargetRange(int16 targetIdx);
f15::math::WordRep<f15::math::GameBackend> computeSimObjectRange(int16 objIdx);
f15::math::WordRep<f15::math::GameBackend> computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

/* Hit-box helpers shared with egtarget.c (defined there, unconditional). */
int aircraftGunRadiusFine(int spec);
int groundHitRadiusMap(int nameIndex);
long roundToTargetDist2(int idx, int objIdx);

/* Sim-step target acquisition: the AAM/ground-target scan and the air-target
 * select that resolve g_groundTargetLock/g_airTargetLock from world state +
 * the resident ctx. Runs in the sim step (single-player: updateFrame; the
 * server calls it per player ctx) so locks are authoritative and independent
 * of render rate - updateTargetLock() (egtarget.c) is drawing only there. */
void simTargetLock(void) {
    int16 idx, best, airSelect;
    /* range/lockedRange carry computeTargetBearing results — fractional
     * under modern; int16 locals re-narrowed the rep. */
    f15::math::WordRep<f15::math::GameBackend> range, lockedRange;

    if (!g_aamLockCooldown.isZero()) {
        --g_aamLockCooldown;
    }

    if (!(g_groundTargetLock & 0x80)) {
        if (frameTick.phase(16)) goto skip_aam;
        if (g_aamLockActive != 0) goto skip_aam;
    }
    if (g_activePanelMode != 0x13) goto skip_aam;
    if (!g_aamLockCooldown.isZero()) goto skip_aam;
    if (g_currentWeaponType == 1) goto skip_aam;
    if (g_viewMode & 0x80) goto skip_aam;

    if (!(g_groundTargetLock & 0x80)) {
        g_groundTargetLock = best = -1;
    }

    range = 100 << (6 - (uint8)g_nightMode);

    if (g_groundTargetLock != -1) {
        idx = g_groundTargetLock - 0x80;
        lockedRange = computeMapTargetRange(idx) - 1;
        if (g_planeTable.planes[idx].active != 0) {
            lockedRange -= 0x280;
        }
        if (idx < 3) {
            lockedRange -= 0x0a00;
        }
        if (angleMagnitude(g_ourHead + angleFromWord(g_viewHeadingOffset) - g_targetBearing) > 0x2000) {
            lockedRange = -32000;
            goto after_lock;
        }
        g_aamLockActive = 1;
    after_lock:;
    } else {
        g_aamLockActive = 0;
        lockedRange = -32000;
    }

    best = -1;
    for (idx = 1; idx < g_planeCount; idx++) {
        computeMapTargetRange(idx);
        if (angleMagnitude(g_ourHead + angleFromWord(g_viewHeadingOffset) - g_targetBearing) < 0x1800 &&
            idx + 0x80 != g_groundTargetLock && !(g_planeTable.planes[idx].flags & 0x80)) {
            if (g_planeTable.planes[idx].active != 0) {
                g_targetRange -= 0x280;
            }
            if (idx == g_targetSlots[0].planeIndex || idx == g_targetSlots[1].planeIndex) {
                g_targetRange -= 0x0a00;
            }
            if (range > g_targetRange && lockedRange < g_targetRange) {
                best = idx;
                range = g_targetRange;
            }
        }
    }

    if (best & 0x80) {
        if (g_groundTargetLock == -1) {
            g_aamLockCooldown = TickDuration::fromWord(4);
        } else {
            g_groundTargetLock = -1;
        }
    } else {
        g_groundTargetLock = best;
        g_lockedTargetKilled = 0;
    }

skip_aam:

    /* Air-target select. The 0x80 bit means "(re)acquire": either no current
       lock (-1) or the T key just requested the next target. With an A2A missile
       selected the lock is then sticky (T cycles, like the ground-target lock);
       other weapons keep auto-acquiring the nearest contact every frame. */
    range = 0x4b << (6 - (uint8)g_nightMode);
    if (g_airTargetLock & 0x80) {
        airSelect = 1;
        if (g_airTargetLock != -1) {
            idx = g_airTargetLock - 0x80;
            lockedRange = computeTargetBearing(g_simObjects[idx].posX, g_simObjects[idx].posY, 1);
            if (angleMagnitude(g_ourHead + angleFromWord(g_viewHeadingOffset) - g_targetBearing) > 0x2000) {
                lockedRange = 0;
            }
        } else {
            lockedRange = 0;
        }
    } else if (g_currentWeaponType == 1 &&
               (g_simObjects[g_airTargetLock].flags.w & (SIMOBJ_ALIVE | SIMOBJ_DESTROYED)) == SIMOBJ_ALIVE) {
        /* A2A missile selected and the designated target is still a live
           contact: hold the lock (T cycles to the next one). */
        airSelect = 0;
        lockedRange = 0;
    } else {
        airSelect = 1;
        lockedRange = 0;
    }

    best = -1;
    /* The server extends the object span with parked remote-player slots via
     * g_simObjScanBound; a direct test/harness setup (no worldImportToEgame)
     * leaves it 0, so fall back to g_groundUnitCount. */
    {
        const int16 scanBound =
            g_simObjScanBound > 0 ? g_simObjScanBound : g_groundUnitCount;
        for (idx = 0; idx < scanBound; idx++) {
            if (!(g_simObjects[idx].flags.w & SIMOBJ_ALIVE))
                continue;
            /* own parked remote object must never be lockable */
            if ((g_simObjects[idx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) &&
                g_simObjects[idx].objType == g_residentPlayer)
                continue;
            if (computeSimObjectRange(idx) >= 4800 && g_directorMode == 0)
                continue;
            if (airSelect && range > g_targetRange && lockedRange < g_targetRange &&
                !(g_viewMode & 0x80) && !(g_simObjects[idx].flags.w & SIMOBJ_DESTROYED) &&
                g_simObjectSpeed[idx] != 0) {
                computeTargetBearing(g_simObjects[idx].posX, g_simObjects[idx].posY, 1);
                if (angleMagnitude(g_ourHead + angleFromWord(g_viewHeadingOffset) - g_targetBearing) < 0x2000) {
                    range = g_targetRange;
                    best = idx;
                }
            }
        }
    }

    if (best != -1) {
        g_airTargetLock = best;
        g_lockedTargetKilled = 0;
    }
    if (g_airTargetLock & 0x80) {
        g_airTargetLock = -1;
    }
}

/* Sim-step half of the world effects: per-step hit tests, damage and the
 * burst timer. Runs inside the sim update (single-player under F15_NET:
 * updateFrame; the server calls it per player ctx so every player's rounds
 * resolve under their own globals), never at render time — hit results no
 * longer depend on screen visibility or render rate. Player rounds are owned
 * (targetPlayer) and only processed in the owner's pass; the 4 enemy tracers
 * are unowned: every player's pass tests them against its own position, so
 * each player can be hit by them exactly like single-player's resident was.
 * The tracer/spark drawing half stays in egtarget.c's drawWorldEffects. */
void simBulletHits(void) {
    int hitFlag, idx, objIdx, pointY, pointX, dist, wpEntry, gunRadius;
    int16 bx, by;

    gunRadius = 0x200 / isqrt(g_frameRateScaling.scaled(4) + 8);

    for (idx = 0; idx < g_bulletTrackCount + 4; idx++) {
        if (bulletTracks[idx].posX.isZero()) continue;
        if (idx < g_bulletTrackCount &&
            bulletTracks[idx].targetPlayer != g_residentPlayer)
            continue;

        hitFlag = 0;
        bx = (int16)bulletTracks[idx].posX.mapWord();
        by = (int16)bulletTracks[idx].posY.mapWord();

        if (idx < g_bulletTrackCount) {
            const int16 scanBound =
                g_simObjScanBound > 0 ? g_simObjScanBound : g_groundUnitCount;
            for (objIdx = 0; objIdx < scanBound; objIdx++) {
                /* never test own parked remote object (would self-kill) */
                if ((g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) &&
                    g_simObjects[objIdx].objType == g_residentPlayer)
                    continue;
                if ((g_simObjects[objIdx].flags.w & (SIMOBJ_ALIVE | SIMOBJ_DESTROYED)) == SIMOBJ_ALIVE) {

                    dist = (abs((int16)(bulletTracks[idx].alt - g_simObjects[objIdx].alt)) >> 5) +
                           abs((int16)(bx - g_simObjects[objIdx].posX)) +
                           abs((int16)(by - g_simObjects[objIdx].posY));
                    dist = abs(dist);

                    /* Broad phase (cheap, and bounds the squared math below): only
                     * targets within the old generous radius are worth the precise
                     * swept-path test against the model-sized box. */
                    if (dist < gunRadius / (g_missionStatus + 1)) {
                        int rFine = aircraftGunRadiusFine(g_simObjects[objIdx].spec);
                        long d2 = roundToTargetDist2(idx, objIdx);
                        long r2 = (long)rFine * rFine;

                        if (d2 < r2) {

                            hitFlag = 1;
                            g_simObjects[objIdx].flags.w |= SIMOBJ_PAINTED;
                            g_hitEffectTimer = TickDuration::fromWord(1);

                            if (d2 * 4 < r2) {
                                destroyAircraft(objIdx);
                                strcat(strBuf, " destroyed by gunfire");
                                hudMessage(strBuf);
                                g_hitEffectTimer = TickDuration::fromWord(8);
                                bulletTracks[idx].posX = {};
                            }
                        }
                    }
                }
            }
        } else {
            dist = (abs((int16)(bulletTracks[idx].alt - f15::math::legacy::Altitudes::renderWord(g_sceneHeightRender))) >> 5) +
                   abs((int16)(bx - mapWordX(flightMapPosition()))) +
                   abs((int16)(by - mapWordY(flightMapPosition())));
            dist = abs(dist);
            if (dist < 0x20) {
                hitFlag = 1;
                hudMessage("Hit by gunfire");
                if (0x20 / (4 - g_missionStatus) > dist) {
                    bombTarget();
                }
            }
        }

        if (hitFlag) {
            g_hitMapPos = mapPosition(bx, by);
            g_hitAlt = terrainFromUnits((std::int16_t)bulletTracks[idx].alt);
            g_hitEffectTimer = TickDuration::fromWord(-1);
        }

        if (bulletTracks[idx].alt < 0) {
            if (g_hitEffectTimer.atMost(0)) {
                g_hitMapPos = mapPosition(bx, by);
                g_hitAlt = terrainFromUnits((std::int16_t)bulletTracks[idx].alt);
                g_hitEffectTimer = TickDuration::fromWord(-1);
            }
            bulletTracks[idx].posX = {};

            wpEntry = findWaypointEntry(mapWordX(g_hitMapPos), mapWordY(g_hitMapPos));
            if (wpEntry != -1 && !(g_planeTable.planes[wpEntry].flags & 0x80)) {
                pointX = (int16)(g_nearestTileObj->x >> 5);
                pointY = MAP_Y_MIRROR - (int16)(g_nearestTileObj->y >> 5);

                if (mapRangeDelta(mapWordX(g_hitMapPos) - pointX, mapWordY(g_hitMapPos) - pointY) <
                        groundHitRadiusMap(g_planeTable.planes[wpEntry].nameIndex) &&
                    (g_planeTable.planes[wpEntry].nameIndex & 0x7f) != *(uint8 *)g_landTargetId) {
                    destroyGroundTarget(wpEntry);
                    strcat(strBuf, " destroyed by gunfire");
                    hudMessage(strBuf);
                    g_hitEffectTimer = TickDuration::fromWord(8);
                    g_hitAlt = {};
                }
            }
        }
    }

    if (!g_hitEffectTimer.isZero()) {
        g_hitEffectTimer.stepTowardZero();
    } else {
        g_lockedTargetKilled = 0;
    }
}
