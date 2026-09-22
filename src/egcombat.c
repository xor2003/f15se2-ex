#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
// seg000 optimized code (/Ot)
#include "eg3dmap.h"
#include "egcode.h"
#include "egcombat.h"
#include "game_options.h"
#include "egdata.h"
#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
#include "math/guidance.hpp"
#include "spec_units.hpp"
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleFromWord;
using f15::math::legacy::angleMagnitude;
using f15::math::legacy::angleMagnitudeCompat;
using f15::math::legacy::angleSeparation;
using f15::math::legacy::fineRep;
using f15::math::legacy::objectFineRep;
using f15::math::legacy::objectFineSet;
using f15::math::legacy::objectAttitudeSet;
using f15::math::legacy::objectAttitudeAdvance;
using AircraftAngle = f15::math::legacy::AircraftAngle;
using f15::math::ViewXAxis;
using f15::math::ViewYAxis;
using FineCoord = f15::math::FineCoord<f15::math::GameBackend>;
using ProjectileGuidance = f15::math::GuidanceMath<f15::math::GameBackend>;
#include "egflight.h"
#include "egframe.h"
#include "egkeys.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "const.h"
#include "comm.h"
#include "campaign_allegiance.h"

#include <dos.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
int samCanAcquireTarget(int slot, int targetX, int targetY, int targetAlt, int mode);
int16 markTargetReached(int16 targetIdx);

/* Player-missile proximity fuze, Q8 scale on the original speed-derived
 * detonation radius (256 = original). Shrinks how close-but-not-touching counts
 * as a hit so shots have to be pressed a bit harder; the target's model size is
 * a floor so a round never "misses" while inside the airframe. Incoming enemy
 * missiles (slot < 8) keep the original radius. */
static const int MISSILE_PROX_SCALE_Q8 = 176;

void fireAirThreat(int16 objIdx) {
    if (campaignFriendlyAircraft(objIdx, g_groundUnitCount, g_simObjects[objIdx].objType)) return;
    int16 p, a, b, c, bearing, e, f;
    uint16 acqRange;
    int16 h, idx, slot, k, l, range, n;

    idx = aircraftTypes[g_threatSpec].modelId;

    range = computeThreatRangeBearing(
        g_simObjects[objIdx].posX,
        g_simObjects[objIdx].posY,
        g_simObjects[objIdx].alt,
        idx,
        &bearing, (int16 *)&acqRange);

    g_threatToneLevel = 4;

    if ((uint16)range > acqRange) {
        /* close enough — increment heat */
        g_simObjects[objIdx].damage += ((g_difficultyTier + g_missionStatus) * 16 + 0x20) >> ((g_playerPlaneFlags & 0x10) != 0);

        if (g_simObjects[objIdx].damage > 0xc0) {
            g_enemyAlertFlag++;
            g_simObjects[objIdx].flags.b[1] |= 0x40;
            updateThreatAlert();

            slot = objIdx % (g_missionStatus + 1);

            if (g_missionStatus * 2 >= g_enemyThreatCount &&
                g_projectiles[slot].ttl.isZero() &&
                acqRange > 8 &&
                angleSeparation(angleFromWord(bearing),
                                g_simObjectHeading[objIdx]) < 0x1800) {

                idx = g_simObjects[objIdx].weaponType;

                if ((unsigned)sams[idx].lockRange > (acqRange >> 1)) {
                    if ((uint16)(-(g_missionStatus * 3 - 0x10)) < acqRange) {
                        if (acqRange < 0x1000) {
                            if (idx != 0) {

                                /* launch missile into slot j */
                                g_projectiles[slot].mapX = g_simObjects[objIdx].posX;
                                g_projectiles[slot].mapY = g_simObjects[objIdx].posY;
                                /* seed the fine position from the launcher's fine
                                 * coords (posX/Y are worldX/Y>>5, so fine>>5 == map) */
                                g_projectiles[slot].fineX = FineCoord::fromRep(objectFineRep(g_simObjectFineX[objIdx]));
                                g_projectiles[slot].fineY = FineCoord::fromRep(objectFineRep(g_simObjectFineY[objIdx]));
                                f15::math::legacy::objectLinearSet(g_projectileAlt[slot],
                                    g_projectiles[slot].alt, g_simObjectAlt[objIdx] - 25);
                                g_projectiles[slot].speed = f15::specProjSpeed(sams[idx].maxSpeed);
                                g_projectiles[slot].head = g_simObjectHeading[objIdx];
                                g_projectiles[slot].pitch = g_simObjectPitch[objIdx] - angleFromWord(0x400);
                                g_projectiles[slot].bank = g_simObjectBank[objIdx];

                                g_projectiles[slot].ttl = f15::math::TickDuration::fromWord((int16)((f15::specLockRangeUnits(sams[idx].lockRange) * (int32)g_frameRateScaling.word()) / (int32)g_projectiles[slot].speed));

                                g_projectiles[slot].specIdx = idx;
                                g_projectiles[slot].targetRef = -objIdx;

                                strcpy(strBuf, sams[idx].name);
                                strcat(strBuf, " fired by ");
                                strcat(strBuf, aircraftTypes[g_threatSpec].name);
                                hudMessage(strBuf);

                                makeSound(6, 2);
                                scheduleEventCheck(objIdx + 0x20, 2);

                                if (randomRange(4) == 0) {
                                    g_simObjects[objIdx].flags.b[0] |= 4;
                                }
                            }
                        }
                    }
                }
            }
        }
        g_simObjects[objIdx].flags.b[0] |= 8;
    } else {
        g_simObjects[objIdx].flags.b[0] &= 0xf7;
        g_simObjects[objIdx].damage -= 0x20;
    }

    g_simObjects[objIdx].damage =
        clampRange(g_simObjects[objIdx].damage, 0, 0xff);
}

// ==== seg000:0x783A ====
void spawnEnemyAircraft(int16 slot, int16 objType) {
    int16 spec;

    spec = g_simObjects[slot].spec;
    objectAttitudeSet(g_simObjectHeading[slot], g_simObjects[slot].heading.w,
        angleFromWord(g_northSouthSign == 1 ? 0 : -0x8000));
    if (g_planeTable.planes[objType].flags & 0x200) {
        g_simObjects[slot].posX = g_northSouthSign * 3 + g_planeTable.planes[objType].mapX;
        g_simObjects[slot].posY = g_planeTable.planes[objType].mapY - g_northSouthSign * 12;
        f15::math::legacy::objectLinearSet(g_simObjectAlt[slot], g_simObjects[slot].alt, 140);
        f15::math::legacy::objectLinearSet(g_simObjectSpeed[slot], g_simObjects[slot].speed, 100);
        /* heading.b[1] += 0xfc — a -0x400-word turn step. */
        objectAttitudeAdvance(g_simObjectHeading[slot], g_simObjects[slot].heading.w,
            angleFromWord(-0x400));
    } else {
        g_simObjects[slot].posX = g_planeTable.planes[objType].mapX;
        g_simObjects[slot].posY = 30 * g_northSouthSign + g_planeTable.planes[objType].mapY;
        f15::math::legacy::objectLinearSet(g_simObjectAlt[slot], g_simObjects[slot].alt, 12);
        f15::math::legacy::objectLinearSet(g_simObjectSpeed[slot], g_simObjects[slot].speed, 10);
    }
    objectFineSet<ViewXAxis>(g_simObjectFineX[slot], g_simObjects[slot].worldX,
                             (int32)(uint16)g_simObjects[slot].posX << 5);
    objectFineSet<ViewYAxis>(g_simObjectFineY[slot], g_simObjects[slot].worldY,
                             (int32)(uint16)g_simObjects[slot].posY << 5);
    objectAttitudeSet(g_simObjectPitch[slot], g_simObjects[slot].pitch, AircraftAngle{});
    objectAttitudeSet(g_simObjectBank[slot], g_simObjects[slot].bank.w, AircraftAngle{});
    g_simObjects[slot].flags.w |= 0x403;
    g_simObjects[slot].objType = objType;
    g_simObjects[slot].timer = (int16)(((int32)aircraftTypes[spec].range << 11) * (int32)g_frameRateScaling.word() / (int32)aircraftTypes[spec].maxSpeed);
    if (g_padlockAircraft == -1) {
        g_simObjects[slot].flags.b[1] &= 0xfe;
    }
    placeString(objType);
    strcat(strBuf, " - ");
    strcat(strBuf, aircraftTypes[g_simObjects[slot].spec].name);
    strcat(strBuf, " taking off");
    if (slot < g_groundUnitCount - 4) {
        hudMessage(strBuf);
    }
}

// ==== seg000:0x79ee ====
void updateThreatTargeting(void) {
    int16 slot, scan, mode, spec, locked, aimY, bestIdx, step;
    int16 viewX, viewY, alt0, wpX, wpY, ring, acq, wp;
    /* best/dist carry mapRange results — fractional under modern; the
     * uint16 wrap on uncapped modern ranges was a legacy limit. */
    f15::math::WordRep<f15::math::GameBackend> best, dist;

    switchIndicatorColor(0, 8);
    switchIndicatorColor(1, 8);
    if (mapEvents[0].ttl != 0) {
        viewX = mapEvents[0].mapX;
        viewY = mapEvents[0].mapY;
    } else {
        viewX = f15::math::legacy::mapWordX(flightMapPosition());
        viewY = f15::math::legacy::mapWordY(flightMapPosition());
    }

    for (slot = 0; slot < 12; slot++) {
        if (!g_projectiles[slot].ttl.isZero()) {
            spec = g_projectiles[slot].specIdx;
            locked = 0;
            aimY = 0;
            /* true while aimY echoes the projectile's own heading word — the
             * speed-up tick steers toward the current heading, i.e. not at all.
             * Modern must see the typed heading itself, not a re-quantized word. */
            bool aimIsHeading = false;
            mode = sams[spec].weaponClass;

            if (slot < 8) {
                plotMapObject(g_projectiles[slot].mapX, g_projectiles[slot].mapY, g_projectiles[slot].targetLock, 0);
                alt0 = f15::math::legacy::Altitudes::renderWord(flightSceneHeight());
                locked = samCanAcquireTarget(slot, viewX, viewY, alt0, mode);
                best = g_acqRange;
                aimY = g_acqAimY;
                scan = 1;
                do {
                    if ((mapEvents[scan].type == 1 && mode <= 0) ||
                        (mapEvents[scan].type == 2 &&
                         (mode == 1 || mode == 2 ||
                          (mode == 3 &&
                           -(g_missionStatus * 12 - 0x40) >
                               abs(abs(signedAngle(angleFromWord(aimY) - g_ourHead) >> 8) - 0x40))))) {
                        acq = samCanAcquireTarget(slot, mapEvents[scan].mapX,
                                                  mapEvents[scan].mapY, alt0, mode);
                        if (acq != 0) {
                            aimY = acq;
                            locked = 0;
                        }
                    }
                    scan++;
                } while (scan < 4);

                if (best > 0x200) {
                    if (g_projectiles[slot].targetRef > 2 &&
                        !(g_planeTable.planes[g_projectiles[slot].targetRef].flags & 0x10))
                        locked = 0;
                    if (g_projectiles[slot].targetRef <= 0 &&
                        !(g_simObjects[-g_projectiles[slot].targetRef].flags.b[0] & 8))
                        locked = 0;
                }
                if (g_projectiles[slot].speed < f15::specProjSpeed(sams[spec].maxSpeed) && frameTick.bit(0))
                    g_projectiles[slot].speed++;
            } else {
                best = 0x7fff;
                if (mode == 7) {
                    for (scan = 0; scan < g_groundUnitCount; scan++) {
                        /* A2A: once the shot has a remembered launch target
                           (targetLock, a g_simObjects index) only that contact is
                           eligible, so the missile tracks the target that was
                           locked at fire time instead of re-acquiring the nearest
                           one each frame. targetLock == -1 (boresight shot) keeps
                           the original nearest-contact scan. */
                        if (g_projectiles[slot].targetLock != -1 &&
                            scan != g_projectiles[slot].targetLock)
                            continue;
                        if ((g_simObjects[scan].flags.b[0] & 2) &&
                            g_simObjectSpeed[scan] != 0) {
                            acq = samCanAcquireTarget(slot, g_simObjects[scan].posX,
                                                      g_simObjects[scan].posY,
                                                      g_simObjects[scan].alt, mode);
                            if (g_acqRange < best && acq != 0) {
                                aimY = g_acqAimY;
                                best = g_acqRange;
                                bestIdx = scan;
                                alt0 = g_simObjects[scan].alt;
                                locked = 1;
                                if (best < 0x180) {
                                    g_simObjects[scan].flags.b[0] |= 0x10;
                                    scheduleEventCheck(scan + 0x20, 1);
                                }
                            }
                        }
                    }
                }
                if (g_projectiles[slot].speed < f15::specProjSpeed(sams[spec].maxSpeed) && frameTick.bit(0)) {
                    g_projectiles[slot].speed++;
                    aimY = signedAngle(g_projectiles[slot].head);
                    aimIsHeading = true;
                }
                if (mode == 4 || mode == 6 || mode == 5 || mode == 28) {
                    if (g_projectiles[slot].targetLock == -1) {
                        for (scan = 0; scan < g_planeCount; scan++) {
                            if ((mode != 4 || g_planeTable.planes[scan].active != 0) &&
                                (((mode == 5 || mode == 6) && (g_planeTable.planes[scan].flags & 8)) ||
                                 (mode != 5 && !(g_planeTable.planes[scan].flags & 8))) &&
                                (acq = samCanAcquireTarget(slot, g_planeTable.planes[scan].mapX,
                                                           g_planeTable.planes[scan].mapY, 0, mode),
                                 g_acqRange < best && acq != 0)) {
                                aimY = g_acqAimY;
                                aimIsHeading = false;
                                best = g_acqRange;
                                bestIdx = scan;
                                alt0 = 0;
                                locked = 1;
                            }
                        }
                    } else {
                        scan = g_projectiles[slot].targetLock;
                        acq = samCanAcquireTarget(slot, g_planeTable.planes[scan].mapX,
                                                  g_planeTable.planes[scan].mapY, 0, mode);
                        if (acq != 0) {
                            aimY = g_acqAimY;
                            aimIsHeading = false;
                            best = g_acqRange;
                            bestIdx = scan;
                            alt0 = 0;
                            locked = 1;
                            if (best < 0xc0)
                                scheduleEventCheck(scan + 0x40, 1);
                        }
                    }
                }
            }

            if (locked != 0 && slot < 8 &&
                angleSeparation(angleFromWord(g_acqAimY), g_projectiles[slot].head) < 0x1000 && mapEvents[0].ttl == 0) {
                if (mode <= 0 && frameTick.bit(1))
                    switchIndicatorColor(1, 0xc);
                if (mode != 0 && !frameTick.bit(1))
                    switchIndicatorColor(0, 0xe);
                if (frameTick.phase(4) == 0 && best < (uint16)(g_projectiles[slot].speed << 5)) {
                    makeSound(10, 1);
                    scheduleEventCheck(slot, 2);
                }
            }

            /* Steer whenever a target is locked. aimY is the target *bearing*, and
               0x0000 (due north) is a valid bearing — gating on aimY != 0 dropped
               all guidance for a target lying due north, so a shot at a contact
               spawned directly ahead (mission start puts you dead astern of the
               locked target, bearing exactly 0) flew its launch attitude (pitched
               down 0x1000) ballistically into the ground. locked is the real
               have-a-target flag. */
            if (locked != 0) {
                const auto aim = aimIsHeading ? g_projectiles[slot].head : angleFromWord(aimY);
                auto delta = aim - g_projectiles[slot].head;
                if (slot < 8)
                    delta = ProjectileGuidance::limitTurn(delta, -(g_missionStatus + 1) << 8,
                                                          (g_missionStatus + 1) << 8);
                delta = ProjectileGuidance::limitTurn(delta, -f15::specYawClamp(sams[spec].turnRate),
                                                      f15::specYawClamp(sams[spec].turnRate));
                g_projectiles[slot].head += ProjectileGuidance::turnStep(delta, g_frameRateScaling.word());
                g_projectiles[slot].bank = ProjectileGuidance::bankFromTurn(delta);
                f15::math::Angle<f15::math::GameBackend> pitchAim;
                if (slot < 8 && best < 0x400) {
                    pitchAim = ProjectileGuidance::aimBearing((alt0 - g_projectiles[slot].alt) >> 4,
                                                              f15::math::legacy::wordAbs(best));
                } else {
                    pitchAim = ProjectileGuidance::aimBearing(((alt0 - g_projectiles[slot].alt) >> 5) +
                                              (f15::math::legacy::wordAbs(best) > 0x140 ? (int)f15::math::legacy::wordAbs(best) >> 3 : 0),
                                          f15::math::legacy::wordAbs(best));
                }
                auto bear = pitchAim - g_projectiles[slot].pitch;
                bear = ProjectileGuidance::limitTurn(bear, -f15::specPitchDiveLimit(sams[spec].turnRate),
                                                     f15::specPitchClimbLimit(sams[spec].turnRate));
                g_projectiles[slot].pitch += ProjectileGuidance::turnStep(bear, g_frameRateScaling.word());
            } else {
                if (g_projectiles[slot].pitch.isPositive() && mode != 30)
                    g_projectiles[slot].pitch -=
                        ProjectileGuidance::scaledStep(g_projectiles[slot].pitch.sign() << 0xc,
                                                       g_frameRateScaling.word());
            }

            if (mode == 28 && g_projectiles[slot].pitch > angleFromWord(-0x800))
                g_projectiles[slot].pitch = angleFromWord(-0x800);
            if (mode == 30 || g_projectiles[slot].alt == 1) {
                if ((g_projectiles[slot].pitch -= ProjectileGuidance::scaledStep(0x800, g_frameRateScaling.word())) <
                    angleFromWord(g_projectiles[slot].targetRef))
                    g_projectiles[slot].pitch = angleFromWord(g_projectiles[slot].targetRef);
            }

            /* Advance the position at fine (mapX<<5) scale, keeping the fraction
             * the original truncated twice per step (step's <<3/scaling divide and
             * sinMul's whole-map-unit result) — slow or oblique flight otherwise
             * stair-steps a map unit at a time. mapX/mapY are derived (fine>>5). */
            /* Pitch runs typed — signedAngle would re-quantize the fractional
             * modern pitch that advanceSteering just computed. */
            step = (int)g_frameRateScaling.perTick(ProjectileGuidance::cosineVelocity(g_projectiles[slot].pitch,
                                                   g_projectiles[slot].speed, g_angleLut) * 256);
            if (mode == 30) {
                step /= 2;
                f15::math::legacy::objectLinearAdvance(g_projectileAlt[slot],
                    g_projectiles[slot].alt,
                    ProjectileGuidance::sineVelocity(g_projectiles[slot].pitch,
                        g_frameRateScaling.perTick(g_projectiles[slot].speed << 7), g_angleLut));
            } else {
                f15::math::legacy::objectLinearAdvance(g_projectileAlt[slot],
                    g_projectiles[slot].alt,
                    ProjectileGuidance::sineVelocity(g_projectiles[slot].pitch,
                        g_frameRateScaling.perTick((int16)(*(uint8 *)&g_projectiles[slot].speed << 8)), g_angleLut));
            }
            g_projectiles[slot].fineX = g_projectiles[slot].fineX.advanced(
                ProjectileGuidance::sineStep(g_projectiles[slot].head, step, g_angleLut));
            g_projectiles[slot].fineY = g_projectiles[slot].fineY.advanced(
                -ProjectileGuidance::cosineStep(g_projectiles[slot].head, step, g_angleLut));
            g_projectiles[slot].mapX = g_projectiles[slot].fineX.mapWord();
            g_projectiles[slot].mapY = g_projectiles[slot].fineY.mapWord();
            g_projectiles[slot].ttl--;
            if (slot < 8)
                f15::math::legacy::objectLinearFlag0(g_projectileAlt[slot],
                    g_projectiles[slot].alt, locked != 0);
            *(char *)&g_posVisibleFlag = 0;
            if ((slot & 3) == frameTick.phase(4))
                testWorldPosVisible(g_projectiles[slot].mapX, g_projectiles[slot].mapY, g_projectiles[slot].alt);

            if (g_projectileAlt[slot] < 0 || *(int8 *)&g_posVisibleFlag != 0) {
                g_hitMapX = g_projectiles[slot].mapX;
                g_hitMapY = g_projectiles[slot].mapY;
                g_hitAlt = g_projectiles[slot].alt;
                /* -3 (DOS wrote 0xfffd into a 16-bit int; as a 32-bit int that
                 * is +65533 and the impact burst lingers for ~65k frames). */
                g_hitEffectTimer = f15::math::TickDuration::fromWord(-3);
                g_savedSamTtl = g_projectiles[slot].ttl;
                g_projectiles[slot].ttl = f15::math::TickDuration{};
                strcpy(strBuf,
                       missiles[g_projectiles[slot].weaponIdx].longName);
                if (mode == 30 || mode == 29 || mode == 28) {
                    scheduleTimedEvent(VIEW_COCKPIT, 1);
                    makeSound(2, 2);
                    strcat(strBuf, " misses ");
                    dist = f15::math::legacy::mapRangeDelta(g_hitMapX - g_planeTable.planes[g_loftTargetIdx].mapX,
                                       g_hitMapY - g_planeTable.planes[g_loftTargetIdx].mapY);
                    if (dist < 0x100 / (g_missionStatus + 1)) {
                        destroyGroundTarget(g_loftTargetIdx);
                        strcat(strBuf, " destroyed by ");
                        strcat(strBuf,
                               missiles[g_projectiles[slot].weaponIdx].longName);
                        g_hitEffectTimer = f15::math::TickDuration::fromWord(8);
                        g_hitAlt = 0;
                    } else {
                        wp = findWaypointEntry(g_hitMapX, g_hitMapY);
                        if (wp == -1 || (g_planeTable.planes[wp].flags & 0x80))
                            goto msg_done;
                        wpX = (int16)(g_nearestTileObj->x >> 5);
                        wpY = -((int16)(g_nearestTileObj->y >> 5) - 0x8000);
                        dist = f15::math::legacy::mapRangeDelta(g_hitMapX - wpX, g_hitMapY - wpY);
                        if (dist >= 0x180 / (g_missionStatus + 2))
                            goto msg_done;
                        destroyGroundTarget(wp);
                        strcat(strBuf, " destroyed by ");
                        strcat(strBuf,
                               missiles[g_projectiles[slot].weaponIdx].longName);
                        g_hitEffectTimer = f15::math::TickDuration::fromWord(8);
                        g_hitAlt = 0;
                    }
                msg_done:
                    hudMessage(strBuf);
                } else if (slot >= 8 && g_projectiles[slot].ttl.exceeds(g_frameRateScaling.scaled(2))) {
                    strcat(strBuf, " ground impact");
                    hudMessage(strBuf);
                }
            }

            long baseDetR = (long)g_frameRateScaling.perTick(g_projectiles[slot].speed << 4);
            long detR = baseDetR;
            if (locked != 0 && slot >= 8) {
                int tr = 0;
                detR = (baseDetR * MISSILE_PROX_SCALE_Q8) >> 8;
                if (mode == 7)
                    tr = aircraftModelRadius(g_simObjects[bestIdx].spec) >> 4;
                else if (mode == 4 || mode == 5 || mode == 6 || mode == 28)
                    tr = groundModelRadius(g_planeTable.planes[bestIdx].nameIndex) >> 4;
                if (detR < tr) detR = tr;
            }

            if ((uint16)((abs(alt0 - g_projectiles[slot].alt) >> 5) + best) < (uint16)detR &&
                locked != 0) {
                g_hitMapX = g_projectiles[slot].mapX;
                g_hitMapY = g_projectiles[slot].mapY;
                g_hitAlt = g_projectiles[slot].alt;
                g_hitEffectTimer = f15::math::TickDuration::fromWord(8);
                if (!g_projectiles[slot].ttl.isZero())
                    g_savedSamTtl = g_projectiles[slot].ttl;
                g_projectiles[slot].ttl = f15::math::TickDuration{};
                if (slot < 8) {
                    if (mapEvents[0].ttl == 0) {
                        strcpy(strBuf, "Hit by ");
                        strcat(strBuf, sams[spec].name);
                        hudMessage(strBuf);
                        bombTarget();
                        ring = frameTick.ring(1, 8);
                        g_particles[ring].posX = g_hitMapX;
                        g_particles[ring].posY = g_hitMapY;
                        g_particles[ring].alt = g_hitAlt;
                        if (!(g_playerPlaneFlags & 0x1000))
                            appendMapEvent(5, spec);
                    }
                } else {
                    if (mode == 7) {
                        destroyAircraft(bestIdx);
                        ring = frameTick.ring(1, 8);
                        g_particles[ring].posX = g_hitMapX =
                            g_simObjects[bestIdx].posX;
                        g_particles[ring].posY = g_hitMapY =
                            g_simObjects[bestIdx].posY;
                        g_particles[ring].alt = g_hitAlt =
                            g_simObjects[bestIdx].alt;
                    } else {
                        if (missileTargetCompat(g_projectiles[slot].weaponIdx, bestIdx) >
                                randomRange(4) ||
                            g_savedSamTtl.atLeast((uint16)g_frameRateScaling.scaled(10))) {
                            destroyGroundTarget(bestIdx);
                        } else {
                            strcpy(strBuf, "Ineffective");
                        }
                        g_projectiles[slot].ttl = f15::math::TickDuration{};
                        g_threatActiveTimer = g_threatTimerInit;
                        g_threatRefX = g_hitMapX;
                        g_threatRefY = g_hitMapY;
                        g_threatRefZ = 3000;
                    }
                    strcat(strBuf, " hit by ");
                    strcat(strBuf, sams[spec].name);
                    hudMessage(strBuf);
                }
            }

            if (slot < 8 && !g_projectiles[slot].ttl.isZero()) {
                g_projectiles[slot].targetLock =
                    readMapPixelColor(g_projectiles[slot].mapX, g_projectiles[slot].mapY);
                if (frameTick.bit(0))
                    plotMapObject(g_projectiles[slot].mapX, g_projectiles[slot].mapY, 0xe, 0);
            }
        }
    }
}

// ==== seg000:0x85be ====
int samCanAcquireTarget(int slot, int targetX, int targetY, int targetAlt, int mode) {
    int dx, dy;

    dx = targetX - g_projectiles[slot].mapX;
    dy = targetY - g_projectiles[slot].mapY;
    /* auto keeps mapRangeDelta's rep — fractional under modern. */
    auto range = f15::math::legacy::mapRangeDelta(dx, dy);
    g_acqAimY = signedAngle(ProjectileGuidance::aimBearing(dx, -dy));
    if (g_frameRateScaling.perTick(g_projectiles[slot].speed * 24) > range) {
        g_acqRange = range;
        return 1;
    }
    const auto bearDiff = angleMagnitudeCompat(angleFromWord(g_acqAimY) - g_projectiles[slot].head);
    if (bearDiff > 0x1000 && mode != 3) {
        if (bearDiff > 0x6000 && slot < 8) {
            if (g_frameRateScaling.perTick(g_projectiles[slot].speed << 4) < range) {
                g_projectiles[slot].ttl = g_projectiles[slot].ttl.clamped(0, g_frameRateScaling.shifted(4));
            }
        }
        return 0;
    }
    if (mode == 0) {
        if (angleMagnitudeCompat(g_projectiles[slot].head - g_ourHead) > 0x2000) {
            return 0;
        }
    }
    if (mode == 0) {
        g_acqRange = range;
        return 1;
    }
    const auto headDiff = angleMagnitudeCompat(g_projectiles[slot].head - g_ourHead);
    if (std::abs(headDiff - 0x4000) >= 0x2000 - g_missionStatus * 2048) {
        g_acqRange = range;
        return 1;
    }
    return 0;
}

// ==== seg000:0x86f8 ====
void destroyAircraft(int16 objIdx) {
    int16 eventType;

    if (!(g_simObjects[objIdx].flags.b[0] & 0x20)) {
        aircraftTypes[g_simObjects[objIdx].spec].killCount += 1;
        if (g_simObjects[objIdx].flags.w & 0x800) {
            g_enemyAirRemaining--;
        }
        if (objIdx == g_padlockAircraft) {
            g_padlockAircraft = -1;
        }
        g_simObjects[objIdx].flags.b[0] |= 0x20;
        g_smokeSourceIdx = -1;
        g_wreckPos = f15::math::legacy::mapPosition((std::int16_t)g_simObjects[objIdx].posX,
                                                  (std::int16_t)g_simObjects[objIdx].posY);
        g_wreckAlt = f15::math::legacy::terrainFromUnits(g_simObjects[objIdx].alt);
        g_wreckFallVel = f15::math::legacy::climbFromUnits(0x80);
        eventType = 3;
        appendMapEvent(eventType, (g_simObjects[objIdx].flags.w & 0x4000 ? 0x80 : 0) + g_simObjects[objIdx].spec);
        if (g_simObjectSpeed[objIdx] != 0) goto done;
        g_simObjects[objIdx].flags.w &= 0x1c1;
    done:;
    }
    strcpy(strBuf, aircraftTypes[g_simObjects[objIdx].spec].name);
    makeSound(2, 2);
    if (g_currentWeaponType == 1 && objIdx == g_airTargetLock) {
        g_lockedTargetKilled = 1;
    }
}

void destroyGroundTarget(int16 planeIdx) {
    int16 eventType, slot, symbol;

    placeString(planeIdx);
    eventType = 1;
    if ((g_planeTable.planes[planeIdx].flags & 0x80) == 0) {
        if (g_planeTable.planes[planeIdx].flags & 0x1000) {
            g_enemyGroundRemaining--;
        }

        g_nearestTileObj = findNearestTileObject(
            (int32)g_planeTable.planes[planeIdx].mapX << 5,
            (0x8000L - (int32)g_planeTable.planes[planeIdx].mapY) << 5);

        if (planeIdx != 0) {
            if (g_planeTable.planes[planeIdx].active == 0) {
                eventType = 12;
            }
            *((uint8 *)&g_planeTable.planes[planeIdx].flags) |= 0x80;
            g_planeTable.planes[planeIdx].active = 0;
            for (slot = 0; slot < 2; slot++) {
                if (g_targetSlots[slot].planeIndex == planeIdx) {
                    markTargetReached(slot);
                    eventType |= (slot != 0 ? 0x40 : 0x80);
                    g_destroyedCueDeadline = frameTick.offset(g_frameRateScaling.word());
                    makeSound(0, 2);
                }
            }
            appendMapEvent(eventType, planeIdx);
            symbol = getTargetSymbol(planeIdx);
        } else {
            if (isTargetOverWater(planeIdx) != 0) {
                symbol = (int16)(char)g_waterTargetId[0];
            } else {
                symbol = (int16)(char)g_landTargetId[0];
            }
            if (symbol != g_nearestTileObj->id) {
                g_tileKillTally[g_nearestTileObj->id]++;
                appendMapEvent(2, g_nearestTileObj->id);
            }
            *(((uint8 *)&symbol) + 1) |= 1;
            g_planeTable.planes[planeIdx].nameIndex = symbol;
        }

        if (g_nearestTileObj != 0) {
            addTileEntry(g_nearestTileObj, shapeDataOffset(symbol), symbol);
        }
    }

    g_smokeSourceIdx = planeIdx;
    makeSound(2, 2);
    if (g_currentWeaponType == 2 && planeIdx == g_groundTargetLock) {
        g_lockedTargetKilled = 1;
    }
    if (g_mapMode == 0) {
        redrawTacMap(f15::math::legacy::mapWordX(flightMapPosition()),
                     f15::math::legacy::mapWordY(flightMapPosition()));
    }
    if (g_missionStatus < 2) {
        updateThreatAlert();
    }
}

// ==== seg000:0x89aa ====
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

extern int16 randomRange(int16);
#if defined(__ANDROID__)
extern "C" void android_haptics_playerDamage(void);
#endif
void bombTarget(void) {
    int16 hit;
    if (gameOptionsEnabled(GAME_OPTION_NO_DAMAGE)) return;
    if (!(g_playerPlaneFlags & 0x1000) && g_autopilotEngaged != -1) {
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
#if defined(__ANDROID__)
        android_haptics_playerDamage();
#endif
    }
}

// ==== seg000:0x8aa6 ====

void fireMissile() {
    int16 spec, tmp, weaponIdx, slot;

    if (angleMagnitude(g_ourRoll) > 0x3000) return;
    if (g_inLandingCorridor != 0) return;
    if (g_ejectState != 0) return;

    weaponIdx = missleSpec[missileSpecIndex].weaponIdx;
    spec = missiles[weaponIdx].specIndex;

    if (missleSpec[missileSpecIndex].ammo == 0) {
        strcpy(strBuf, missiles[weaponIdx].longName);
        strcat(strBuf, ":0");
        setTimedMessage(strBuf);
        goto end;
    }

    if (spec == 0) return;
    if (spec == -1) return;

    if (!gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS))
        missleSpec[missileSpecIndex].ammo--;

    if (g_hudVisible != 0) {
        setDrawColor(COLOR_BLACK);
        tmp = ammoNumX[missileSpecIndex];
        fillRectBoth(tmp - 1, 190, tmp + 2, 194);
        drawNumber(missleSpec[missileSpecIndex].ammo, tmp, 190, 0x0c);
        strcpy(strBuf, missiles[weaponIdx].longName);
        strcat(strBuf, ":");
        strcat(strBuf, itoa(missleSpec[missileSpecIndex].ammo, g_itoaScratch, 10));
        setTimedMessage(strBuf);
    }

    slot = -1;
    tmp = 8;
    do {
        if (g_projectiles[tmp].ttl.isZero()) {
            slot = tmp;
        }
        tmp++;
    } while (tmp < 12);

    if (slot == -1) goto check_end;

    g_projectiles[slot].mapX = f15::math::legacy::mapWordX(flightMapPosition());
    g_projectiles[slot].mapY = f15::math::legacy::mapWordY(flightMapPosition());
    /* Seed the fine position with the player's sub-map-unit remainder so the
     * missile doesn't visibly snap to the 32-fine-unit map grid on the first
     * frame. (mapX<<5) + (fine+0x10)&0x1f == fine+0x10 and the mirrored Y
     * recomposes as 0x10000F - fine, both modulo the 21-bit mask. */
    g_projectiles[slot].fineX = FineCoord::fromRep(fineRep(g_ViewX) + 0x10);
    g_projectiles[slot].fineY = FineCoord::fromRep(0x10000F - fineRep(g_ViewY));
    f15::math::legacy::objectLinearSet(g_projectileAlt[slot], g_projectiles[slot].alt,
        f15::math::legacy::Altitudes::renderWord(flightSceneHeight()) - 20);
    g_projectiles[slot].speed = f15::math::legacy::projectileSpeed(g_velocity);
    g_projectiles[slot].head = g_ourHead;
    g_projectiles[slot].pitch = g_ourPitch;
    g_projectiles[slot].bank = g_ourRoll;

    g_projectiles[slot].ttl = f15::math::TickDuration::fromWord((int16)((f15::specLockRangeUnits(sams[spec].lockRange) * (sams[spec].weaponClass == 6 ? 1 : 2)) * (int32)g_frameRateScaling.word() / (int32)(f15::specProjSpeed(sams[spec].maxSpeed) + 1)) + 6);

    if (g_projectiles[slot].ttl.atMost(6)) {
        g_projectiles[slot].ttl = f15::math::TickDuration::fromWord(999);
    }

    g_projectiles[slot].specIdx = spec;
    g_projectiles[slot].weaponIdx = weaponIdx;
    g_projectiles[slot].targetLock = -1;

    if (spec != 30) {
        g_projectiles[slot].pitch -= angleFromWord(0x1000);
    } else {
        g_projectiles[slot].targetRef = computeLoftAngle() - 0x400;
        g_loftTargetIdx = g_groundTargetLock;
    }

    if (g_groundTargetLock >= 0 && sams[spec].weaponClass == 6) {
        g_projectiles[slot].targetLock = g_groundTargetLock;
    }

    if (g_groundTargetLock >= 0 && sams[spec].weaponClass == 5 && (g_planeTable.planes[g_groundTargetLock].flags & 8)) {
        g_projectiles[slot].targetLock = g_groundTargetLock;
    }

    /* A2A missiles (weaponClass 7): remember the air contact the HUD had locked
       (g_airTargetLock, a g_simObjects index) so the shot tracks that specific
       target for its whole flight — the mode-7 update reuses this rather than
       re-acquiring the nearest contact each frame. Firing without an air lock
       (g_airTargetLock == -1) leaves targetLock -1 → boresight nearest-scan. */
    if (g_airTargetLock >= 0 && sams[spec].weaponClass == 7) {
        g_projectiles[slot].targetLock = g_airTargetLock;
    }

    if (spec == 29) {
        g_projectiles[slot].pitch = angleFromWord(0xc000);
        g_projectiles[slot].speed = 1;
    }

    g_lastMissileSlot = slot;
    strcpy(strBuf, missiles[weaponIdx].longName);
    strcat(strBuf, " fired");
    hudMessage(strBuf);

    makeSound(sams[spec].lockRange != 0 ? 18 : 24, 2);

    scheduleEventCheck(slot, 1);

check_end:
    if (g_activePanelMode == 0x15) {
        refreshActivePanel(0x15);
    }
end:;
}

// ==== seg000:0x8df4 ====
void testWorldPosVisible(int16 worldX, int16 worldY, int16 worldZ) {
    *(char *)&g_posVisibleFlag = 0;
    drawNearestTileObject((int32)worldX << 5, -((int32)worldY - 0x8000L) << 5, (int32)worldZ);
}
