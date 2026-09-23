/*
 * egtarget_net.c - target/combat code added for the authoritative server
 * (split out of egtarget.c to keep that file close to upstream).
 *
 * Three groups:
 *
 *  1. Remote-player identity: parked remote aircraft carry no valid spec, so
 *     every depiction (view model, display name, HUD-projection Y) resolves
 *     them explicitly as the player F-15 instead of reading aircraftTypes[0].
 *
 *  2. Sim-step halves of formerly render-side logic: simTargetLock and
 *     simBulletHits run inside the sim update (single-player: updateFrame;
 *     server: per player ctx) so locks, hits and damage are authoritative and
 *     independent of render rate or screen visibility.
 *
 *  3. Hit-box machinery + deterministic cosmetic RNG: model-derived radii
 *     (computeHitRadii and friends) size the swept-path gun test, and
 *     fxRandomRange drives the explosion flicker without touching the sim's
 *     deterministic randomRange() sequence.
 */
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
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
#include "r2d.h"
#include "r3dmesh.h"
#include "const.h"
#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* egtgt2.c helpers (same raw-decl convention as egtarget.c). */
void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
void projectWorldToHudFine(int32 fineX, int32 fineY, int fineZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);
void drawViewportLine(int16 x0, int16 y0, int16 x1, int16 y1);

/* ---- 1. aircraft identity ----------------------------------------------
 * One resolver for every depiction of a sim object: remote-piloted parked
 * objects are the player F-15 (model 6 gear-down / 7 gear-up - their spec
 * field is unused/0, so aircraftTypes[spec] would read "MIG-23"); everything
 * else resolves via its spec's type table. nearModel selects the close vs
 * far LOD entry for spec-typed objects. */
int16 simObjectViewModel(int objIdx, int nearModel) {
    if (g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER)
        return (g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_GEAR_DOWN) ? 6 : 7;
    return (&aircraftTypes[g_simObjects[objIdx].spec].viewModelId)[nearModel ? 0 : 1];
}

/* Display name for the same identity (target panel/labels). */
const char *simObjectTypeName(int objIdx) {
    if (g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER)
        return "F-15";
    return aircraftTypes[g_simObjects[objIdx].spec].name;
}

/* projectWorldToHudFine expects map-fine coords (its relY subtracts 0x100000,
 * i.e. fineY == posY<<5). Ordinary objects store worldY in exactly that
 * convention, but a parked remote player's worldY is render-space
 * (0x01000000 - ViewY), 0xF00000 higher - feeding it through unchanged
 * throws the projected box/label half a map away. Convert explicitly;
 * the fine precision is preserved, so the box still glides. */
int32 simObjectFineY(int objIdx) {
    int32 y = g_simObjects[objIdx].worldY;
    if (g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER)
        y -= 0x01000000L - 0x100000L;
    return y;
}

/* ---- 2. sim-step target acquisition ------------------------------------
 * The AAM/ground-target scan and the air-target select that resolve
 * g_groundTargetLock/g_airTargetLock from world state + the resident ctx.
 * Runs in the sim step (single-player: updateFrame; the server calls it per
 * player ctx) so locks are authoritative and independent of render rate -
 * updateTargetLock() (egtarget.c) is drawing only. */
void simTargetLock(void) {
    int16 range, idx, best, lockedRange, airSelect;

    if (g_aamLockCooldown != 0) {
        g_aamLockCooldown--;
    }

    if (!(g_groundTargetLock & 0x80)) {
        if (frameTick & 0x0f) goto skip_aam;
        if (g_aamLockActive != 0) goto skip_aam;
    }
    if (g_activePanelMode != 0x13) goto skip_aam;
    if (g_aamLockCooldown != 0) goto skip_aam;
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
        if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) > 0x2000) {
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
        if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) < 0x1800 &&
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
            g_aamLockCooldown = 4;
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
            if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) > 0x2000) {
                lockedRange = 0;
            }
        } else {
            lockedRange = 0;
        }
    } else if (g_currentWeaponType == 1 &&
               (g_simObjects[g_airTargetLock].flags.b[0] & 0x22) == 2) {
        /* A2A missile selected and the designated target is still a live
           contact: hold the lock (T cycles to the next one). */
        airSelect = 0;
        lockedRange = 0;
    } else {
        airSelect = 1;
        lockedRange = 0;
    }

    best = -1;
    for (idx = 0; idx < g_simObjScanBound; idx++) {
        if (!(g_simObjects[idx].flags.b[0] & 2))
            continue;
        /* own parked remote object must never be lockable */
        if ((g_simObjects[idx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) &&
            g_simObjects[idx].objType == g_residentPlayer)
            continue;
        if (computeSimObjectRange(idx) >= 4800 && g_directorMode == 0)
            continue;
        if (airSelect && range > g_targetRange && lockedRange < g_targetRange &&
            !(g_viewMode & 0x80) && !(g_simObjects[idx].flags.b[0] & 0x20) &&
            g_simObjects[idx].speed != 0) {
            computeTargetBearing(g_simObjects[idx].posX, g_simObjects[idx].posY, 1);
            if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) < 0x2000) {
                range = g_targetRange;
                best = idx;
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

/* ---- 3a. deterministic cosmetic RNG -------------------------------------
 * For the explosion spark flicker: deliberately NOT randomRange()/rand() so
 * drawing can never perturb the sim's deterministic sequence (headless and
 * rendered runs must produce identical sim state). The LCG constants are
 * the classic Borland pair; only visual variety matters, not quality. */
#define FX_RNG_SEED 0x5eed
#define FX_LCG_MUL  25173
#define FX_LCG_ADD  13849
static uint16 s_fxRandState = FX_RNG_SEED;
static int fxRandomRange(int maxVal) {
    s_fxRandState = (uint16)(s_fxRandState * FX_LCG_MUL + FX_LCG_ADD);
    return (int)(((long)(s_fxRandState >> 1) * (long)maxVal) >> 15);
}

/* World-space radius of an explosion burst, in fine map units (the alt axis
 * shares the same 1/32-coarse scale). Tunable by eye. */
static const int EXPLOSION_WORLD_RADIUS = 0x20;

/* ---- 3b. model-derived hit boxes ----------------------------------------
 * Gun hit box for aircraft, sized to the actual model. The original hit test
 * used a flat radius (0x200/isqrt(frameRateScaling*4+8), ~64 map units at full
 * rate / difficulty 0) for every target, so a bomber and a fighter shared the
 * same oversized box and shots well off-target still scored. Instead we derive
 * the box from each type's 3D model bounding size, computed once at load.
 *
 * s_aircraftModelRadius[spec] is the model-space bounding-cube half-extent of
 * the type's near view model, in model units; 1 model unit == 2 fine world units
 * (drawWorldObject scale at true size), so *2 converts to the fine units the
 * swept hit test works in. Fighters come out ~30 (=>~60 fine), bombers ~64.
 *
 * The hit test is a point-to-segment distance: because rounds are only sampled
 * once per 15 Hz sim step they leap many units between samples, so a box that
 * exactly hugs a ~60-fine plane is nearly unhittable by point sampling. Testing
 * the round's swept path this step (pos-vel .. pos) against the target sphere
 * makes the tight, model-sized box actually connect. GUN_AIRCRAFT_HIT_SCALE_Q8
 * is a Q8 slack on the radius (256 = exactly the model bound). */
static const int GUN_AIRCRAFT_HIT_SCALE_Q8 = 256;
/* Gun-vs-ground box, Q8 slack on the ground target's world-shape footprint.
 * GROUND_HIT_MIN_MAP keeps tiny/point-model targets (footprint ~0) hittable
 * against the coarse ground-impact sampling, while staying far tighter than the
 * original flat 12-map radius. */
static const int GUN_GROUND_HIT_SCALE_Q8 = 256;
static const int GROUND_HIT_MIN_MAP = 6;

/* Model-space bounding half-extents (model units). Aircraft models come from the
 * constant 15FLT.3D3, world (ground/tile) shapes from the per-theater region, so
 * both are refilled each mission by computeHitRadii(). */
#define AIRCRAFT_TYPE_COUNT 19
#define WORLD_SHAPE_COUNT 100 /* indexed by (nameIndex & 0x7f), cf. buf3d3[] */
static int16 s_aircraftModelRadius[AIRCRAFT_TYPE_COUNT];
static int16 s_worldShapeRadius[WORLD_SHAPE_COUNT];

void computeHitRadii(void) {
    const uint8 *base = (const uint8 *)g_world3dData;
    const uint8 *limit = base + WORLD3D_DATA_SIZE;
    int i;
    for (i = 0; i < AIRCRAFT_TYPE_COUNT; i++)
        s_aircraftModelRadius[i] =
            (int16)r3dmesh_boundRadius(base + shapeDataOffset(aircraftTypes[i].viewModelId), limit);
    /* Cover the appended photo/target models too: they sit at buf3d3[size3d3]
     * and [size3d3+1] (eg3dload.c), past the main region shapes, and ground
     * targets reference them by nameIndex. */
    for (i = 0; i <= (int)size3d3 + 1 && i < WORLD_SHAPE_COUNT; i++)
        s_worldShapeRadius[i] = (int16)r3dmesh_boundRadius(base + buf3d3[i], limit);
}

/* Model bounding half-extent (model units) of an aircraft type / a ground
 * (tile-object) target's world shape, for hit-box sizing across weapons. */
int aircraftModelRadius(int spec) {
    if (spec < 0 || spec >= AIRCRAFT_TYPE_COUNT) spec = 0;
    return s_aircraftModelRadius[spec];
}
int groundModelRadius(int nameIndex) {
    int s = nameIndex & 0x7f;
    return (s >= 0 && s < WORLD_SHAPE_COUNT) ? s_worldShapeRadius[s] : 0;
}

/* Ground target gun/impact radius in map units, Q8-scaled and difficulty-tightened. */
static int groundHitRadiusMap(int nameIndex) {
    int r = (groundModelRadius(nameIndex) * GUN_GROUND_HIT_SCALE_Q8) >> (8 + 4);
    if (r < GROUND_HIT_MIN_MAP) r = GROUND_HIT_MIN_MAP;
    r /= (g_missionStatus + 1);
    return r < 1 ? 1 : r;
}

/* Model-derived gun hit radius (fine world units) for a target, with the
 * original difficulty tightening kept (higher g_missionStatus -> smaller). */
static int aircraftGunRadiusFine(int spec) {
    int r;
    if (spec < 0 || spec >= AIRCRAFT_TYPE_COUNT) spec = 0;
    r = ((int)s_aircraftModelRadius[spec] * 2 * GUN_AIRCRAFT_HIT_SCALE_Q8) >> 8;
    r /= (g_missionStatus + 1);
    return r < 1 ? 1 : r;
}

/* Shortest wrapped delta on the 21-bit fine world torus (BULLET_FINE_MASK). */
static long fineWrapDelta(long d) {
    d &= BULLET_FINE_MASK;
    if (d & ((BULLET_FINE_MASK + 1) >> 1)) d -= (BULLET_FINE_MASK + 1);
    return d;
}

/* Squared closest-approach distance (fine units) between the round idx's swept
 * path this step (previous pos = pos-vel, to current pos) and target objIdx's
 * center. X/Y wrap on the world torus; alt does not. */
static long roundToTargetDist2(int idx, int objIdx) {
    long ax = fineWrapDelta((long)bulletTracks[idx].posX - ((long)g_simObjects[objIdx].posX << 5));
    long ay = fineWrapDelta((long)bulletTracks[idx].posY - ((long)g_simObjects[objIdx].posY << 5));
    long az = (long)bulletTracks[idx].alt - (long)g_simObjects[objIdx].alt;
    /* segment vector = the round's per-step travel (= velocity) */
    long vx = bulletTracks[idx].velX, vy = bulletTracks[idx].velY, vz = bulletTracks[idx].velZ;
    long seg2 = vx * vx + vy * vy + vz * vz;
    long cx, cy, cz;
    if (seg2 == 0) {
        cx = ax; cy = ay; cz = az;
    } else {
        /* project the previous endpoint (a - v) onto the segment, clamped */
        long dot = -((ax - vx) * vx + (ay - vy) * vy + (az - vz) * vz);
        long t256 = dot <= 0 ? 0 : (dot >= seg2 ? 256 : (dot * 256 / seg2));
        cx = (ax - vx) + vx * t256 / 256;
        cy = (ay - vy) + vy * t256 / 256;
        cz = (az - vz) + vz * t256 / 256;
    }
    return cx * cx + cy * cy + cz * cz;
}

/* Game-logic half of the world effects: per-step hit tests, damage and the
 * burst timer. Runs inside the sim update (single-player: updateFrame; the
 * server calls it per player ctx so every player's rounds resolve under
 * their own globals), never at render time — hit results no longer depend
 * on screen visibility or render rate. Player rounds are owned (targetPlayer)
 * and only processed in the owner's pass; the 4 enemy tracers are unowned:
 * every player's pass tests them against its own position, so each player
 * can be hit by them exactly like single-player's resident was. */
void simBulletHits(void) {
    int hitFlag, idx, objIdx, pointY, pointX, dist, wpEntry, gunRadius;
    int16 bx, by;

    gunRadius = 0x200 / isqrt(g_frameRateScaling * 4 + 8);

    for (idx = 0; idx < g_bulletTrackCount + 4; idx++) {
        if (bulletTracks[idx].posX == 0) continue;
        if (idx < g_bulletTrackCount &&
            bulletTracks[idx].targetPlayer != g_residentPlayer)
            continue;

        hitFlag = 0;
        bx = (int16)(bulletTracks[idx].posX >> 5);
        by = (int16)(bulletTracks[idx].posY >> 5);

        if (idx < g_bulletTrackCount) {
            for (objIdx = 0; objIdx < g_simObjScanBound; objIdx++) {
                /* never test own parked remote object (would self-kill) */
                if ((g_simObjects[objIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) &&
                    g_simObjects[objIdx].objType == g_residentPlayer)
                    continue;
                if ((g_simObjects[objIdx].flags.b[0] & 0x22) == 2) {

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
                            g_simObjects[objIdx].flags.b[0] |= 0x10;
                            g_hitEffectTimer = 1;

                            if (d2 * 4 < r2) {
                                destroyAircraft(objIdx);
                                strcat(strBuf, " destroyed by gunfire");
                                hudMessage(strBuf);
                                g_hitEffectTimer = 8;
                                bulletTracks[idx].posX = 0;
                            }
                        }
                    }
                }
            }
        } else {
            dist = (abs((int16)(bulletTracks[idx].alt - g_viewZ)) >> 5) + abs((int16)(bx - g_viewX_)) + abs((int16)(by - g_viewY_));
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
            g_hitMapX = bx;
            g_hitMapY = by;
            g_hitAlt = (int16)bulletTracks[idx].alt;
            g_hitEffectTimer = -1;
        }

        if (bulletTracks[idx].alt < 0) {
            if (g_hitEffectTimer <= 0) {
                g_hitMapX = bx;
                g_hitMapY = by;
                g_hitAlt = (int16)bulletTracks[idx].alt;
                g_hitEffectTimer = -1;
            }
            bulletTracks[idx].posX = 0;

            wpEntry = findWaypointEntry(g_hitMapX, g_hitMapY);
            if (wpEntry != -1 && !(g_planeTable.planes[wpEntry].flags & 0x80)) {
                pointX = (int16)(g_nearestTileObj->x >> 5);
                pointY = 0x8000 - (int16)(g_nearestTileObj->y >> 5);

                if (rangeApprox(g_hitMapX - pointX, g_hitMapY - pointY) <
                        groundHitRadiusMap(g_planeTable.planes[wpEntry].nameIndex) &&
                    (g_planeTable.planes[wpEntry].nameIndex & 0x7f) != *(uint8 *)g_landTargetId) {
                    destroyGroundTarget(wpEntry);
                    strcat(strBuf, " destroyed by gunfire");
                    hudMessage(strBuf);
                    g_hitEffectTimer = 8;
                    g_hitAlt = 0;
                }
            }
        }
    }

    if (g_hitEffectTimer != 0) {
        g_hitEffectTimer -= signOf(g_hitEffectTimer);
    } else {
        g_lockedTargetKilled = 0;
    }
}

/* ---- 2b. render half ----------------------------------------------------
 * Cannon tracers + explosion sparks as real world-space 3D line geometry
 * (drawWorldLine): submitted into the scene BEFORE r3d_endScene so the software
 * depth sort occludes them and the GL backend z-tests + fogs them. Kept separate
 * from drawHudWorldOverlay (which does the 2D HUD symbology) so the effects join
 * the 3D pass. Drawing only: hit tests, damage and the burst timer tick moved
 * to simBulletHits() in the sim update. */
void drawWorldEffects(void) {
    int tmp, idx, radius, dist, prevX;

    for (idx = 0; idx < g_bulletTrackCount + 4; idx++) {
        long ax, ay, az, ex, ey, ez;
        if (bulletTracks[idx].posX == 0) continue;

        /* Render-interpolated position: rounds fly straight at constant speed,
         * so pos + vel*alpha is exact between sim steps (no snapshots needed). */
        ax = (bulletTracks[idx].posX + (((int32)bulletTracks[idx].velX * g_renderAlphaQ12) >> 12)) & BULLET_FINE_MASK;
        ay = (bulletTracks[idx].posY + (((int32)bulletTracks[idx].velY * g_renderAlphaQ12) >> 12)) & BULLET_FINE_MASK;
        az = bulletTracks[idx].alt + (((int32)bulletTracks[idx].velZ * g_renderAlphaQ12) >> 12);
        ex = (ax + (bulletTracks[idx].velX >> 1)) & BULLET_FINE_MASK;
        ey = (ay + (bulletTracks[idx].velY >> 1)) & BULLET_FINE_MASK;
        ez = az + (bulletTracks[idx].velZ >> 1);

        projectWorldToHudFine(ax, ay, (int)az);
        prevX = vtxScratch.vproj.x.lo;
        projectWorldToHudFine(ex, ey, (int)ez);
        if (vtxScratch.vproj.x.lo == -1 || prevX == -1) continue;

        /* The projectWorldToHudFine pair above gates on-screen visibility (as the
         * original did); the tracer itself is a real world-space 3D segment
         * (round -> half a velocity-step ahead) so it perspective-projects,
         * occludes and hazes with the scene instead of overlaying a flat line. */
        drawWorldLine(ax, ay, (int)az, ex, ey, (int)ez,
                      idx < g_bulletTrackCount ? 0x0d : 0x0c);
    }

    if (g_hitEffectTimer != 0) {
        /* Explosion burst as world-space 3D sparks radiating from the hit point:
         * each is a real line (drawWorldLine) so the star has perspective, occludes
         * and hazes — not a flat screen-space starburst. The projectWorldToHud call
         * only gates on-screen visibility (the sparks are re-randomised every frame
         * the timer is active, giving the flicker). radius is a WORLD radius (fine
         * map units), so the burst shrinks with distance instead of a screen-px fan. */
        projectWorldToHud(g_hitMapX, g_hitMapY, g_hitAlt);
        if (vtxScratch.vproj.x.lo != -1) {
            long hx = (long)(uint16)g_hitMapX << 5;
            long hy = (long)(uint16)g_hitMapY << 5;
            radius = EXPLOSION_WORLD_RADIUS;
            for (idx = 0; idx < 8; idx++) {
                int color = fxRandomRange(4) + COLOR_LIGHTRED;
                long ex, ey, ez;
                if (g_hitAlt > 0) {
                    /* airburst: scatter in a world-space sphere around the hit */
                    ex = hx + fxRandomRange(radius << 1) - radius;
                    ey = hy + fxRandomRange(radius << 1) - radius;
                    ez = g_hitAlt + fxRandomRange(radius << 1) - radius;
                } else {
                    /* ground burst: fan horizontally and plume upward */
                    tmp = fxRandomRange(0x8000) - 0x4000;
                    dist = fxRandomRange(radius);
                    ex = hx + sinMul(tmp, dist);
                    ey = hy - cosMul(tmp, dist);
                    ez = g_hitAlt + fxRandomRange(radius);
                }
                drawWorldLine(hx, hy, g_hitAlt, ex, ey, (int)ez, color);
            }
        }
    }
}
