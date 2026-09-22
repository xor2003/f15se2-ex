// seg000 non-debug code (/Gs, no /Zi)
// Functions whose block scheduling only matches when compiled without /Zi.
#include "egcombat.h"
#include "egdata.h"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
#include "math/guidance.hpp"
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleMagnitude;
using f15::math::legacy::angleSeparation;
using f15::math::legacy::angleFromWord;
using f15::math::legacy::wordRep;
using f15::math::legacy::uwordRep;
using f15::math::legacy::wordClamp;
using f15::math::legacy::attitudeStep;
using f15::math::legacy::wordProductQ14;
using f15::math::legacy::objectAttitudeSet;
using f15::math::legacy::objectAttitudeAdvance;
using f15::math::legacy::objectLinearSet;
using f15::math::legacy::objectLinearAdvance;
using f15::math::legacy::mapOffset;
using f15::math::legacy::mapRange;
using f15::math::legacy::objectFineAdvance;
using f15::math::legacy::objectFineRep;
using f15::math::ViewXAxis;
using f15::math::ViewYAxis;
using FineCoord = f15::math::FineCoord<f15::math::GameBackend>;
using TrackMath = f15::math::GuidanceMath<f15::math::GameBackend>;
using AircraftAngle = f15::math::legacy::AircraftAngle;
using WordScalar = f15::math::legacy::WordScalar<>;
#include "egflight.h"
#include "egframe.h"
#include "egkeys.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "const.h"
#include "comm.h"
#include "campaign_allegiance.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void fireGroundThreat(int16 planeIdx);

// ==== seg000:0x6172 ====
void updateThreatSites() {
    int16 p, arcRadius, b, c, siteIdx, e;

    if ((g_scopeSweepTimer.isZero() || g_prevScopeRange != g_threatScopeRange) && g_hudVisible != 0) {
        if (g_scopeSweepTimer.isZero() && g_mapMode == 0) {
            restoreScopePanel();
            g_scopeArcStart = 0;
            g_scopeArcEnd = 0x100;
        }
    }

    for (siteIdx = 0; siteIdx < g_targetEntityCount; siteIdx++) {
        if (g_planeTable.planes[siteIdx].active != 0 &&
            !(g_planeTable.planes[siteIdx].flags & 0x80) &&
            ((siteIdx * frameTick.shifted(10) * 7 & 7) <= 7 ||
             g_planeTable.planes[siteIdx].alertLevel != 0 ||
             (g_planeTable.planes[siteIdx].flags & 0x100) != 0)) {
            g_planeTable.planes[siteIdx].threatTimer -= 1;
            if (g_planeTable.planes[siteIdx].threatTimer <= 0) {
                g_planeTable.planes[siteIdx].threatTimer =
                    ((int16)(char)g_frameRateScaling.word() << 8) /
                        ((g_planeTable.planes[siteIdx].alertLevel >> 3) + 0x20) +
                    siteIdx / 2;
            }
            if (g_planeTable.planes[siteIdx].threatTimer == 4 && g_scopeSweepTimer.isNegative()) {
                fireGroundThreat(siteIdx);
                g_planeTable.planes[siteIdx].flags |= 0x02;
            }
        } else {
            g_planeTable.planes[siteIdx].flags &= ~0x02;
        }
    }

    if (g_mapMode == 0 && g_scopeSweepTimer.isPositive() && g_hudVisible != 0 && g_scopeArcRange > 1) {
        if (g_detailLevel != 0) {
            captureScopePanel();
            arcRadius = (int16)((int32)clampRange(g_scopeSweepTimer.elapsedWithin(g_frameRateScaling.word()), 1, g_frameRateScaling.word()) * (int32)g_scopeArcRange / (int32)g_frameRateScaling.word()) << 6;
        } else {
            arcRadius = g_scopeArcRange << 6;
            g_scopeArcRange = 0;
        }
        if (g_threatLabelTarget >= 0) {
            drawMapRangeArc(g_planeTable.planes[g_threatLabelTarget].mapX,
                            g_planeTable.planes[g_threatLabelTarget].mapY,
                            arcRadius, g_scopeArcColor, g_threatRadarFlag,
                            g_scopeArcStart, g_scopeArcEnd);
        }
    }

    --g_scopeSweepTimer;
}

/* ---- merged from egflt.c ---- */
void fireGroundThreat(int16 planeIdx) {
    int16 bearing[11];
    uint16 range[4];
    int16 clampedRange, threatType, slot, m, n, score;

    threatType = g_planeTable.planes[planeIdx].active;
    score = computeThreatRangeBearing(g_planeTable.planes[planeIdx].mapX, g_planeTable.planes[planeIdx].mapY, 0, threatType, bearing, (int16 *)range);
    g_threatToneLevel = 0;
    if (score > 0) {
        clampedRange = score;
        if (clampedRange > 99) {
            clampedRange = 99;
        }
        g_threatToneLevel = 4;
        if (score + g_threatScopeRange > 50) {
            g_threatToneLevel = 12;
        }
        if (score + g_threatScopeRange > 100) {
            g_threatToneLevel = 14;
        }
        g_scopeArcRange = score;
        g_scopeSweepTimer = f15::math::TickDuration::fromWord(g_frameRateScaling.word());
        g_threatLabelTarget = planeIdx;
        g_threatRadarFlag = aNone[threatType].flags & 1;
        if (g_planeTable.planes[planeIdx].alertLevel != 0) {
            g_scopeArcStart = (bearing[0] >> 8) - 0x20;
            g_scopeArcEnd = (bearing[0] >> 8) + 0x20;
        }
        g_scopeArcColor = g_threatToneLevel;
        if (!(*(uint8 *)&g_planeTable.planes[planeIdx].flags & 4)) {
            *(uint8 *)&g_planeTable.planes[planeIdx].flags |= 4;
        }
    }
    if ((unsigned)score > range[0]) {
        g_planeTable.planes[planeIdx].alertLevel += (g_difficultyTier + g_missionStatus) * 32 + 32;
        if (g_planeTable.planes[planeIdx].alertLevel > 255) {
            g_planeTable.planes[planeIdx].alertLevel = 255;
        }
        if (!(g_planeTable.planes[planeIdx].flags & 0x100) && mapEvents[0].ttl == 0 &&
            g_planeTable.planes[planeIdx].alertLevel > 0x7f) {
            updateThreatAlert();
        }
        if (g_enemyThreatCount <= g_missionStatus) {
            if (g_planeTable.planes[planeIdx].alertLevel > 0xc0) {
                if (threatType != 21) {
                    if (g_nearestThreatRange > 0x500) {
                        if ((uint16) - (g_missionStatus * 3 - 20) < range[0]) {
                            g_enemyAlertFlag++;
                            if (g_planeTable.planes[planeIdx].alertLevel >= 250) {
                                slot = (g_missionStatus != 0) ? planeIdx % g_missionStatus : 0;
                                if (g_projectiles[slot].ttl == 0) {
                                    if (sams[threatType].lockRange > (uint16)range[0]) {
                                        g_projectiles[slot].mapX = g_planeTable.planes[planeIdx].mapX + 8;
                                        g_projectiles[slot].mapY = g_planeTable.planes[planeIdx].mapY;
                                        /* Seed the fine (mapX<<5) position so the first
                                         * movement step (which derives mapX = fineX>>5)
                                         * starts at the launcher, not a stale slot value. */
                                        g_projectiles[slot].fineX = FineCoord::fromRep((int32)(uint16)g_projectiles[slot].mapX << 5);
                                        g_projectiles[slot].fineY = FineCoord::fromRep((int32)(uint16)g_projectiles[slot].mapY << 5);
                                        g_projectiles[slot].alt = 0;
                                        g_projectiles[slot].speed = 1;
                                        g_projectiles[slot].head = angleFromWord(bearing[0]);
                                        g_projectiles[slot].pitch = angleFromWord(0x4000);
                                        g_projectiles[slot].ttl = (int16)((((int32)sams[threatType].lockRange << 3) * (int32)g_frameRateScaling.word()) / (int32)(sams[threatType].maxSpeed >> 6));
                                        g_projectiles[slot].specIdx = threatType;
                                        g_projectiles[slot].targetRef = planeIdx;

                                        placeString(planeIdx);
                                        strcat(strBuf, " firing ");
                                        strcat(strBuf, (char *)&sams[threatType]);
                                        hudMessage(strBuf);
                                        makeSound(6, 2);
                                        scheduleEventCheck(planeIdx + 0x40, 2);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        *(uint8 *)&g_planeTable.planes[planeIdx].flags |= 0x10;
    } else {
        *(uint8 *)&g_planeTable.planes[planeIdx].flags &= 0xEF;
        g_planeTable.planes[planeIdx].alertLevel -= 0x10;
        if (g_planeTable.planes[planeIdx].alertLevel < 0) {
            g_planeTable.planes[planeIdx].alertLevel = 0;
        }
    }
}

// ==== seg000:0x660e routine_324 ====
int16 computeThreatRangeBearing(int16 threatX, int16 threatY, int16 threatAlt, int16 threatType, int16 *outBearing, int16 *outRange) {
    int16 p, a, bearing, range;
    uint16 score;

    if (threatType == 0 || threatType == -1) {
        return 0;
    }
    /* Player-to-threat delta on the typed map position — modern keeps the
     * fractional position instead of rounding to a coarse word first. */
    const auto offset = mapOffset(flightMapPosition(), threatX, threatY);
    range = (uint16)mapRange(offset) >> 6;
    bearing = signedAngle(TrackMath::aimBearing(offset.dx, -offset.dy));
    score = (score = (aNone[threatType].dangerTier + g_missionStatus * 2 + 3) * aNone[threatType].lethality / 16) * (((uint16)f15::math::legacy::Altitudes::renderWord(flightSceneHeight()) >> 6) + 0x40) >> 7;
    *outBearing = bearing;
    *outRange = range;
    return score;
}

// ==== seg000:0x66be ====
void updateThreatAlert(void) {
    int16 planeIdx;
    g_threatActiveTimer = g_threatTimerInit;
    if (mapEvents[0].ttl != 0) {
        g_threatRefX = mapEvents[0].mapX;
        g_threatRefY = mapEvents[0].mapY;
    } else {
        g_threatRefX = f15::math::legacy::mapWordX(flightMapPosition());
        g_threatRefY = f15::math::legacy::mapWordY(flightMapPosition());
    }
    g_threatRefZ = f15::math::legacy::Altitudes::renderWord(flightSceneHeight());
    g_threatRefHead = signedAngle(g_ourHead);
    g_unusedEventHist0 = 0xFF;
    for (planeIdx = 0; planeIdx < g_planeScanCount; planeIdx++) {
        if (g_planeTable.planes[planeIdx].active != 0) {
            g_planeTable.planes[planeIdx].alertLevel = clampRange(g_planeTable.planes[planeIdx].alertLevel, ((g_missionStatus + g_difficultyTier) << 4) - 16, 0xFF);
        }
    }
}

// ==== seg000:0x6742 ====
int16 computeThreatScore(void) {
    int16 threatType, idx, score;

    score = 0;
    for (idx = 0; idx < g_targetEntityCount; idx++) {
        threatType = g_planeTable.planes[idx].active;
        if (threatType != 0) {
            score += aNone[threatType].lethality * aNone[threatType].dangerTier * (g_missionStatus + 2) / 64;
        }
    }
    score /= 100;
    return score;
}

// ==== seg000:0x67b4 ====
void updateObjects(void) {
    int16 candBearing, pitchCmd, viewBearing, aggrIdx, bearing, relBearing, tgtIdx, acRange, hdg, aspect, u0, e0, best, range, moveAmt, mode, fireOffset, objIdx, vel, scanIdx, trackSlot, tgtX, deltaX, deltaY, rollCmd, tgtY, smokeSlot, tgtZ;
    /* Raw control-signal difference (pitchCmd - pitch word rep): int16 under
     * fixed, fractional word units under modern. */
    WordScalar pitchDelta;
    /* horizontal magnitude after the pitch decompose — the original stored it
     * back into the int16 vel; StepRep keeps the fraction under modern.
     * Declared here so the after_missile_table goto doesn't cross its init. */
    FineCoord::StepRep horizVel;
    /* Gun-fire heading (heading + dispersion) — same goto-hoist reason. */
    AircraftAngle fireHdg;

    if (frameTick.phase(2) == 0 && g_smokeSourceIdx == -1) {
        g_particles[frameTick.ring(1, 8)].posX = 0;
    }

    bulletTracks[frameTick.ring(2, 4) + g_bulletTrackCount].posX = {};

    g_enemyThreatCount = g_activeThreatCount;
    g_activeThreatCount = 0;
    for (objIdx = 0; objIdx < g_groundUnitCount; objIdx++) {
        if (g_simObjects[objIdx].flags.b[0] & 1) {
            g_threatSpec = g_simObjects[objIdx].spec;
            if ((g_simObjects[objIdx].flags.b[0] & 2) && g_simObjectSpeed[objIdx] != 0) {
                mode = 0;
                if (!(g_simObjects[objIdx].flags.b[0] & 4)) {
                    const int friendlyAircraft = campaignFriendlyAircraft(objIdx, g_groundUnitCount, g_simObjects[objIdx].objType);
                    if (!friendlyAircraft && !g_threatActiveTimer.isZero() && (!((g_simObjects[objIdx].flags.w) & 0x140) || g_threatActiveTimer.exceeds(g_threatDisplayTtl))) {
                        tgtX = g_threatRefX;
                        tgtY = g_threatRefY;
                        tgtZ = g_threatRefZ;
                        mode = 1;
                        if (mapEvents[0].ttl != 0) goto padlock_target;
                        goto got_target;
                    }

                    mode = 3;
                    if ((g_simObjects[objIdx].flags.w) & 0x100) {
                        if (g_padlockAircraft != -1) {
                            const auto pickAngle = g_simObjectHeading[g_padlockAircraft] +
                                angleFromWord((objIdx & 7) * 0x800 - 0x1800);
                            tgtX = (int16)((int)TrackMath::sineVelocity(pickAngle,
                                          g_simObjects[g_padlockAircraft].speed, g_angleLut) +
                                   g_simObjects[g_padlockAircraft].posX);

                            tgtY = (int16)(g_simObjects[g_padlockAircraft].posY -
                                   (int)TrackMath::cosineVelocity(pickAngle,
                                          g_simObjects[g_padlockAircraft].speed, g_angleLut));

                            tgtZ = g_simObjects[g_padlockAircraft].alt + (objIdx & 7) * 0x40;
                            goto set_target_alt;
                        }
                    }

                    if (((uint8)objIdx * 8 + (uint8)g_missionTick.phase(256)) & 0xbf) goto after_retarget;
                    if (!(g_simObjects[objIdx].flags.b[0] & 0x40)) {
                        best = 0x7fff;
                        viewBearing = computeBearing(f15::math::legacy::mapWordX(flightMapPosition()) - g_simObjects[objIdx].posX,
                                                     g_simObjects[objIdx].posY - f15::math::legacy::mapWordY(flightMapPosition()));
                        for (scanIdx = 0; scanIdx < 8; scanIdx++) {
                            tgtIdx = randomRange(g_planeCount) + 1;
                            if (!(g_planeTable.planes[tgtIdx].flags & 0x400)) {
                                candBearing = computeBearing(g_planeTable.planes[tgtIdx].mapX - g_simObjects[objIdx].posX,
                                                             g_simObjects[objIdx].posY - g_planeTable.planes[tgtIdx].mapY);
                                if (abs(viewBearing - candBearing) < best) {
                                    best = abs(viewBearing - candBearing);
                                    g_simObjects[objIdx].objType = tgtIdx;
                                    if (-(g_missionStatus * 0x1000 - 0x4000) > best) break;
                                }
                            }
                        }
                        if (((int)mapRange(mapOffset(flightMapPosition(), g_simObjects[objIdx].posX,
                                                    g_simObjects[objIdx].posY)) >> 6) > 350 &&
                            objIdx != 0) {
                            (g_simObjects[objIdx].flags.w) &= 0x1c1;
                            g_simObjects[objIdx].timer = 0;
                        }
                    }

                after_retarget:
                    tgtIdx = g_simObjects[objIdx].objType;
                    tgtX = g_planeTable.planes[tgtIdx].mapX;
                    tgtY = g_planeTable.planes[tgtIdx].mapY;
                    tgtZ = clampRange((int)f15::math::legacy::Altitudes::render(flightSceneHeight()) + 1000, 5000, 20000);
                set_target_alt:
                    goto got_target;
                padlock_target:
                    tgtX = mapEvents[0].mapX;
                    tgtY = mapEvents[0].mapY;
                    tgtZ = clampRange((int)f15::math::legacy::Altitudes::render(flightSceneHeight()), 1000, 30000);
                    goto got_target;
                }

                tgtX = g_planeTable.planes[g_simObjects[objIdx].objType].mapX;
                if ((g_simObjects[objIdx].flags.w) & 0x200) {
                    tgtZ = g_simObjects[objIdx].posX - tgtX;
                    tgtY = g_planeTable.planes[g_simObjects[objIdx].objType].mapY;
                    tgtX = tgtX - tgtZ * 2;
                    tgtZ = ((g_planeTable.planes[g_simObjects[objIdx].objType].flags + abs(tgtZ)) & 0x200)
                               ? 140
                               : 12;
                } else {
                    tgtY = g_planeTable.planes[g_simObjects[objIdx].objType].mapY + g_northSouthSign * 0x500;
                    tgtZ = (int16)(f15::math::legacy::mapRangeDelta(g_simObjects[objIdx].posX - tgtX,
                                       g_simObjects[objIdx].posY - tgtY) +
                           2000);
                }
                mode = 2;

            got_target:
                if (mode == 3 && (g_simObjects[objIdx].flags.b[0] & 8)) {
                    tgtX = f15::math::legacy::mapWordX(flightMapPosition());
                    tgtY = f15::math::legacy::mapWordY(flightMapPosition());
                    tgtZ = g_simObjects[objIdx].alt;
                }
                deltaX = tgtX - g_simObjects[objIdx].posX;
                deltaY = tgtY - g_simObjects[objIdx].posY;
                bearing = signedAngle(TrackMath::aimBearing(deltaX, -deltaY));
                range = (int16)f15::math::legacy::mapRangeDelta(deltaX, deltaY);
                pitchCmd = signedAngle(TrackMath::aimBearing((tgtZ - g_simObjects[objIdx].alt) >> 5, range));
                pitchCmd = clampRange(pitchCmd, -0x2000, 0x1000);
                if (mode == 1 && (uint16)range < 0x600) {
                    g_activeThreatCount++;
                    if ((uint16)range >= 0x400) goto after_missile_table;
                    if (frameTick.phase(4)) goto after_missile_table;
                    if (angleSeparation(g_simObjectHeading[objIdx],
                                        angleFromWord(bearing)) >= 0x800) goto after_missile_table;
                    if (angleSeparation(g_simObjectPitch[objIdx],
                                        angleFromWord(pitchCmd)) >= 0x800) goto after_missile_table;

                    trackSlot = frameTick.ring(2, 4) + g_bulletTrackCount;
                    /* Fine units per step + dispersion cone, like the player's
                     * gun (the original 312 coarse/s). */
                    vel = g_frameRateScaling.perTick(312 << 5);
                    bulletTracks[trackSlot].velZ = TrackMath::sineVelocity(
                        angleFromWord(gunSpreadAngle()) - g_simObjectPitch[objIdx], vel, g_angleLut);
                    horizVel = TrackMath::cosineVelocity(
                        g_simObjectPitch[objIdx], vel, g_angleLut);
                    fireHdg = g_simObjectHeading[objIdx] + angleFromWord(gunSpreadAngle());
                    bulletTracks[trackSlot].velX = TrackMath::sineVelocity(fireHdg, horizVel, g_angleLut);
                    bulletTracks[trackSlot].velY = -TrackMath::cosineVelocity(fireHdg, horizVel, g_angleLut);
                    bulletTracks[trackSlot].posX = FineCoord::fromRep(objectFineRep(g_simObjectFineX[objIdx]));
                    bulletTracks[trackSlot].posY = FineCoord::fromRep(objectFineRep(g_simObjectFineY[objIdx]));
                    bulletTracks[trackSlot].alt = g_simObjects[objIdx].alt;

                after_missile_table:
                    aggrIdx = clampRange((objIdx & 3) + g_missionStatus, 0, 2);
                    if (objIdx == 0) aggrIdx = 1;
                    hdg = signedAngle(g_simObjectHeading[objIdx]);
                    if (angleMagnitude(g_simObjectBank[objIdx]) < 0x4000) {
                        hdg += signedAngle(g_simObjectBank[objIdx].shiftedDown(2));
                    }
                    relBearing = (int16)(bearing - hdg) >> 13 & 7;
                    hdg = signedAngle(g_ourHead);
                    if (angleMagnitude(g_ourRoll) < 0x4000) {
                        hdg += (int16)signedAngle(g_ourRoll) >> 1;
                    }
                    aspect = (((signedAngle(g_simObjectHeading[objIdx]) - hdg) >> 13) + 4) & 7;
                    {
                        int16 maneuver;
                        maneuver = g_maneuverTable[aggrIdx][relBearing][aspect];
                        rollCmd = (maneuver & 0xf) << 12;
                        if (maneuver == 0x100) {
                            pitchCmd = 0x6000;
                            rollCmd = frameTick.bit(11) ? 0x4000 : -0x4000;
                        }
                    }
                    if (g_maneuverTable[aggrIdx][relBearing][aspect] == 0x200) {
                        pitchCmd = (int16)0xa000;
                        rollCmd = frameTick.bit(11) ? -0x4000 : 0x4000;
                    }
                    if (pitchCmd == (int16)0xa000) {
                        if (-(signedAngle(g_simObjectPitch[objIdx].shiftedDown(3)) - 3000) > g_simObjectAlt[objIdx]) {
                            pitchCmd = (int16)(wordRep(g_simObjectPitch[objIdx]) + 0x1000);
                        }
                    }
                    if (angleMagnitude(g_simObjectBank[objIdx]) > 0x4000) {
                        pitchCmd = rollCmd = 0;
                    }
                    goto after_accel;
                }

                rollCmd = signedAngle(TrackMath::limitTurn(
                    angleFromWord(bearing) - g_simObjectHeading[objIdx],
                    -0x3000, 0x3000)) << 1;
                if (mode == 1 && g_missionStatus + 1 <= g_enemyThreatCount) {
                    rollCmd = 0x3000;
                }

            after_accel:
                if (mode == 1 && (g_planeTable.planes[g_closestThreatIndex].flags & 0x400) && g_nearestThreatRange < 0x780) {
                    rollCmd = 0x3000;
                }

                rollCmd = clampRange(rollCmd, -aircraftTypes[g_threatSpec].maneuverability * 0x1000,
                                     aircraftTypes[g_threatSpec].maneuverability * 0x1000);
                rollCmd = wordClamp((int16)(rollCmd - wordRep(g_simObjectBank[objIdx])),
                                    -aircraftTypes[g_threatSpec].maneuverability * 256,
                                    aircraftTypes[g_threatSpec].maneuverability * 256);

                if ((g_simObjects[objIdx].flags.w) & 0x400) {
                    if (g_simObjectSpeed[objIdx] < 150) {
                        objectAttitudeSet(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                          AircraftAngle{});
                    } else {
                        objectAttitudeAdvance(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                              angleFromWord(0x100));
                    }
                    rollCmd = 0;
                    if (g_simObjectSpeed[objIdx] < aircraftTypes[g_threatSpec].maxSpeed) {
                        objectLinearAdvance(g_simObjectSpeed[objIdx], g_simObjects[objIdx].speed,
                                            g_frameRateScaling.perTick(60.0));
                    } else if (g_simObjectAlt[objIdx] > 300) {
                        g_simObjects[objIdx].flags.b[1] &= 0xfb;
                    }
                }

                if (g_simObjects[objIdx].flags.b[0] & 0x30) {
                    rollCmd = 0x400;
                }

                if (((uint8)objIdx & 3) == frameTick.phase(4)) {
                    testWorldPosVisible(g_simObjects[objIdx].posX,
                                        g_simObjects[objIdx].posY,
                                        g_simObjects[objIdx].alt);
                    if (*(int8 *)&g_posVisibleFlag != 0) {
                        g_simObjects[objIdx].flags.b[1] |= 0x20;
                    } else {
                        g_simObjects[objIdx].flags.b[1] &= 0xdf;
                    }
                }

                if ((g_simObjects[objIdx].flags.w) & 0x2000) {
                    pitchCmd = 0x3000;
                }

                if (g_missionTick.below(10)) {
                    rollCmd >>= 2;
                }

                {
                    int16 u = objIdx * 36;
                    objectAttitudeAdvance(g_simObjectBank[objIdx], g_simObjects[objIdx].bank.w,
                        attitudeStep(rollCmd * (g_missionStatus + 2), g_frameRateScaling.word()));
                    objectAttitudeAdvance(g_simObjectHeading[objIdx], g_simObjects[objIdx].heading.w,
                        g_simObjectBank[objIdx].shiftedDown(3).dividedBy(g_frameRateScaling.word()));

                    pitchDelta = pitchCmd - wordRep(g_simObjectPitch[objIdx]);
                    if (!(g_simObjects[objIdx].flags.b[0] & 0x20)) goto no_smoke;
                    pitchDelta = -0x200;
                    if (frameTick.phase(4)) goto no_smoke;
                    smokeSlot = frameTick.ring(1, 8);
                    g_particles[smokeSlot].posX = *(int16 *)((char *)g_simObjects + u + 2);
                }
                {
                    int16 t = smokeSlot * 8;
                    int16 v = objIdx * 36;
                    /* g_particles[ma] via register offset t: idiomatic g_particles[ma].field_N
                       recomputes ma*8 and shifts register allocation (verify mismatch). */
                    *(int16 *)((char *)g_particles + t + 2) = *(int16 *)((char *)g_simObjects + v + 4);
                    *(int16 *)((char *)g_particles + t + 4) = *(int16 *)((char *)g_simObjects + v + 6);
                    *(int16 *)((char *)g_particles + t + 6) = randomRange(32) << 11;
                    g_smokeParticleSlot = smokeSlot;
                }
            no_smoke:

                if (g_simObjectPitch[objIdx].isNegative() &&
                    -(TrackMath::sineVelocity(g_simObjectPitch[objIdx], 2000,
                                              g_angleLut) - 200) > g_simObjectAlt[objIdx] &&
                    ((g_simObjects[objIdx].flags.w) & 0x220) == 0) {
                    pitchDelta = 0x400;
                }

                pitchDelta = wordClamp(pitchDelta, -0x400, 0x400);
                objectAttitudeAdvance(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                      attitudeStep(pitchDelta * 4, g_frameRateScaling.word()));
                if (angleMagnitude(g_simObjectPitch[objIdx]) > 0x4000) {
                    objectAttitudeAdvance(g_simObjectHeading[objIdx], g_simObjects[objIdx].heading.w,
                                          AircraftAngle::halfTurn());
                    objectAttitudeAdvance(g_simObjectBank[objIdx], g_simObjects[objIdx].bank.w,
                                          AircraftAngle::halfTurn());
                    objectAttitudeSet(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                      AircraftAngle::halfTurn() - g_simObjectPitch[objIdx]);
                }

                g_simObjects[objIdx].flags.b[0] &= 0xef;

                moveAmt = wordProductQ14(uwordRep(-(g_simObjectPitch[objIdx].dividedBy(2) +
                                                    AircraftAngle::halfTurn())),
                                         g_simObjects[objIdx].speed);
                moveAmt -= (int16)(std::abs(TrackMath::sineVelocity(
                    g_simObjectBank[objIdx], moveAmt, g_angleLut)) / 2);
                moveAmt = g_frameRateScaling.perTick(moveAmt * 4);
                moveAmt >>= 2;

                const auto horizStep = TrackMath::cosineVelocity(
                    g_simObjectPitch[objIdx], moveAmt, g_angleLut);

                objectFineAdvance<ViewXAxis>(g_simObjectFineX[objIdx], g_simObjects[objIdx].worldX,
                    TrackMath::sineVelocity(
                        g_simObjectHeading[objIdx], horizStep, g_angleLut));
                objectFineAdvance<ViewYAxis>(g_simObjectFineY[objIdx], g_simObjects[objIdx].worldY,
                    -TrackMath::cosineVelocity(
                        g_simObjectHeading[objIdx], horizStep, g_angleLut));

                objectLinearAdvance(g_simObjectAlt[objIdx], g_simObjects[objIdx].alt,
                    TrackMath::sineVelocity(g_simObjectPitch[objIdx], moveAmt, g_angleLut));

                g_simObjects[objIdx].posX = (int16)(g_simObjects[objIdx].worldX >> 5);
                g_simObjects[objIdx].posY = (int16)(g_simObjects[objIdx].worldY >> 5);

                if (g_simObjectAlt[objIdx] <= 30000) goto alt_ok;
                objectAttitudeSet(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                  AircraftAngle{});
            alt_ok:

                if (g_simObjectAlt[objIdx] < 0) {
                    (g_simObjects[objIdx].flags.w) &= (objIdx != 0) ? 0x1c1 : 0;
                    g_hitMapX = g_simObjects[objIdx].posX;
                    g_hitMapY = g_simObjects[objIdx].posY;
                    g_hitAlt = g_simObjects[objIdx].alt;
                    g_hitEffectTimer = f15::math::TickDuration::fromWord(-8);
                    if (objIdx == g_airTargetLock) {
                        g_airTargetLock = -1;
                    }
                }

                if ((uint16)range < 0x10 && mode == 2) {
                    if ((g_simObjects[objIdx].flags.w) & 0x200) {
                        (g_simObjects[objIdx].flags.w) |= 0x1000;
                    } else {
                        (g_simObjects[objIdx].flags.w) |= 0x200;
                    }
                }

                if ((g_simObjects[objIdx].flags.w) & 0x1000) {
                    objectAttitudeSet(g_simObjectBank[objIdx], g_simObjects[objIdx].bank.w,
                                      AircraftAngle{});
                    objectAttitudeSet(g_simObjectPitch[objIdx], g_simObjects[objIdx].pitch,
                                      AircraftAngle{});
                    objectAttitudeSet(g_simObjectHeading[objIdx], g_simObjects[objIdx].heading.w,
                                      angleFromWord(g_northSouthSign == 1 ? 0 : -0x8000));
                    objectLinearSet(g_simObjectAlt[objIdx], g_simObjects[objIdx].alt,
                        (g_planeTable.planes[g_closestThreatIndex].flags & 0x200) ? 140 : 12);
                    if (g_simObjectSpeed[objIdx] > 0) {
                        objectLinearAdvance(g_simObjectSpeed[objIdx], g_simObjects[objIdx].speed,
                                            -g_frameRateScaling.perTick(120.0));
                    } else {
                        (g_simObjects[objIdx].flags.w) &= 0x1c1;
                        if (objIdx == 0 && g_targetSlots[0].state >= 5) {
                            (g_simObjects[objIdx].flags.w) = 0;
                        }
                    }
                    if (objIdx >= g_groundUnitCount - 4 && g_simObjectSpeed[objIdx] < 100) {
                        (g_simObjects[objIdx].flags.w) &= 0x1c1;
                        (g_simObjects[objIdx].flags.w) |= 0x406;
                    }
                }

                if (--g_simObjects[objIdx].timer == 0) {
                    g_simObjects[objIdx].flags.b[0] |= 4;
                    best = 0x7fff;
                    for (scanIdx = 3; scanIdx < g_planeScanCount; scanIdx++) {
                        if ((g_planeTable.planes[scanIdx].flags & 0x101) == 1) {
                            smokeSlot = (int16)f15::math::legacy::mapRangeDelta(
                                g_simObjects[objIdx].posX - g_planeTable.planes[scanIdx].mapX,
                                g_simObjects[objIdx].posY - g_planeTable.planes[scanIdx].mapY);
                            if (smokeSlot < best) {
                                g_simObjects[objIdx].objType = scanIdx;
                                best = smokeSlot;
                            }
                        }
                    }
                }

                {
                    char o;
                    o = g_simObjects[objIdx].flags.b[0];
                    if ((o & 2) &&
                        (fireOffset = (((uint8)objIdx & 8) >> 3) + (objIdx & 7) * 2,
                         frameTick.mod(g_frameRateScaling.shifted(4)) == g_frameRateScaling.scaled(fireOffset)) &&
                        !(o & 0x20)) {
                        fireAirThreat(objIdx);
                    }
                }
            } else {
                if (((uint8)objIdx & 7) == g_missionTick.ring(4, 8)) {
                    if (objIdx < g_groundUnitCount - 4) {
                        if (objIdx != 0) {
                            if (224 / (g_missionStatus + 2) < g_missionTick.elapsedSince(g_lastSpawnTick)) {
                                tgtIdx = randomRange(g_planeScanCount);
                                if (!g_threatActiveTimer.isZero() || (g_simObjects[objIdx].flags.b[0] & 0x80)) {
                                    if ((g_planeTable.planes[tgtIdx].flags & 0x181) == 1) {
                                        if (g_simObjects[objIdx].spec == g_planeTable.planes[tgtIdx].alertLevel) {
                                            if (g_missionStatus * 2 >= g_enemyThreatCount) {
                                                deltaX = g_threatRefX - g_planeTable.planes[tgtIdx].mapX;
                                                deltaY = g_threatRefY - g_planeTable.planes[tgtIdx].mapY;
                                                range = (uint16)f15::math::legacy::mapRangeDelta(deltaX, deltaY) >> 6;
                                                acRange = aircraftTypes[g_threatSpec].range;
                                                if ((uint16)(acRange / 2) > (uint16)range) {
                                                    g_lastSpawnTick = g_missionTick;
                                                    spawnEnemyAircraft(objIdx, tgtIdx);
                                                    scheduleEventCheck(objIdx + 0x20, 2);
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
    }
}
