#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
using f15::math::legacy::fineRep;
using f15::math::legacy::viewX;
using f15::math::legacy::viewY;
using f15::math::legacy::moveX;
using f15::math::legacy::moveY;
// seg000 debug code (/Zi) - split from egmain.c
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/guidance.hpp"
using f15::math::legacy::altitudeFromUnits;
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleFromWord;
using f15::math::legacy::angleMagnitude;
using f15::math::legacy::mapOffset;
using f15::math::legacy::mapRange;
using f15::math::legacy::objectFineAdvance;
using f15::math::legacy::objectLinearSet;
using f15::math::legacy::objectFineSet;
using f15::math::legacy::objectAttitudeSet;
using f15::math::ViewXAxis;
using f15::math::ViewYAxis;
using SpeedMath = f15::math::AirspeedMath<f15::math::GameBackend>;
using FineCoord = f15::math::FineCoord<f15::math::GameBackend>;
using TrackMath = f15::math::GuidanceMath<f15::math::GameBackend>;
using Ticks = f15::math::Ticks;
using TickDuration = f15::math::TickDuration;
#include "egflight.h"
#include "egframe.h"
#include "android_ar.h"
#include "game_options.h"
#include "worldxfer.h"
#include "egkeys.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "const.h"
#include "comm.h"
#include "shared/common.h"
#include "replacement_terrain_collision.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
static bool missionAtHeight(f15::math::TerrainHeight<f15::math::GameBackend> height) {
    using namespace f15::math;
    return AltitudeMath<GameBackend>::atGround(flightSceneHeight(), height);
}

void updateFrame(void);
void dispatchKeyScancode();
void tickMessageTimers();
void updateBulletsAndFire();
void updateTracerParticles();
void applyGravityFall();
void initFrameRandom();
void generateRandomRadioMessage();
void findWaypointFeatures();
void moveStuff();
void moveNearFar(void *nearPtr, int16 count);
int16 setCommWorldbufPtr();

// ==== seg000:0x0720 ====
void updateFrame(void) {
    int16 tmp, unused;
    uint16 val;
    uint16 screenY;
    int16 i, objIdx;

    {
        const auto pos = f15::math::legacy::mapPosition(g_ViewX, g_ViewY);
        g_viewX_ = f15::math::legacy::mapWordX(pos);
        g_viewY_ = f15::math::legacy::mapWordY(pos);
    }

    if (g_initPhase == 1) {
        g_playerPlaneFlags = 0;
        if (gameData->difficulty == 4) {
            gameData->difficulty = 2;
            g_autopilotEngaged = 1;
            g_playerPlaneFlags |= 0x1000;
            *(char far *)&commData->trainingFlag |= 1;
        }
        findWaypointFeatures();
        g_threatActiveTimer = TickDuration{};
        g_scopeSweepTimer = TickDuration::fromWord(1);
        g_airTargetLock = g_groundTargetLock = -1;
        g_fireCooldown = g_bombDamageMask = missileSpecIndex = waypointIndex = g_unusedWaypointTail = 0;
        g_autopilotAltitude = {};
        g_closestThreatIndex = g_unusedEventHist0 = g_unusedEventHist1 = g_unusedEventHist2 = (int8)(g_halfScaleRender = 0);
        g_threatRefPos = {}; g_threatRefZ = {};
        g_prevThreatIndex = g_smokeSourceIdx = -1;
        g_fuelRemaining = f15::math::legacy::fuelFromUnits(10000);
        g_gunHits = 0;
        waypointIndex = 1;
        g_currentWeaponType = 1;
        drawWeaponSelectMarker(0);
        g_frameTimingAccum = 12;
        /* Render/sim decoupled: pin max sim precision instead of letting the
         * governor ramp 4->15. The sim steps fixed-rate (gameMainLoop) and the
         * mission clock stays 1 Hz at any scaling, so 15 is just the best
         * physics resolution. ALT+A "ACCEL" now speeds the sim via the wall-clock
         * step rate (egsys.c), not by lowering this. */
        g_frameRateScaling = f15::math::SimRate::fromWord(15);
        recalcTimeScale();
        g_mapZoomLevel = 1;
        g_radarScopeRange = 1;
        tmp = g_northSouthSign = (gameData->theater == 6) ? 1 : (gameData->theater & 1) ? 1
                                                                                        : -1;

        if (((g_planeTable.planes[g_targetSlots[0].viewIndex].flags) & 0x200) != 0) {
            g_ViewX -= moveX((int32)(tmp * 0x80));
            *(char *)&g_playerPlaneFlags |= 8;
        } else {
            g_ViewY -= moveY((int32)(1800 * g_northSouthSign));
        }
        initFrameRandom();
        appendMapEvent(8, 0);
        initTacMapView();
        switchIndicatorColor(3, 10);
        setActivePanel(19);
        g_groundTargetLock = g_airTargetLock = -1;
        g_difficultyTier = 2;
        g_missionStatus = gameData->difficulty;
        gameData->unk4 = 1;
        g_detailLevel = commData->setupDetail;
        setupLodDistances();
        commData->landingType = 1;
        g_gunAmmo = 1000;
        if (g_missionStatus == 0 || g_autopilotEngaged != 0) {
            g_northSouthSign = (mapOffset(flightMapPosition(), waypoints[1].mapX, waypoints[1].mapY).ringY() < 0x8000u) ? 1 : -1;
            g_altitude = altitudeFromUnits(2000);
            g_velocity = f15::math::legacy::speedFromUnits(8100);
            g_setThrust = f15::math::legacy::thrustFromUnits(100);
            UpdateThrottleState();
            *(char *)&g_playerPlaneFlags |= 1;
            *(char *)&g_playerPlaneFlags &= ~8;
            if (gameData->totalScore == 0 && gameData->theater != 6) {
                for (i = 0; i < g_groundUnitCount - 4; i++) {
                    if ((i & 1) == 0) {
                        g_simObjects[i].flags.w |= SIMOBJ_ALIVE;
                        objectLinearSet(g_simObjectAlt[i], g_simObjects[i].alt, 2200);
                        objectLinearSet(g_simObjectSpeed[i], g_simObjects[i].speed, 300);
                        g_simObjects[i].posX = i * 12 + f15::math::legacy::mapWordX(flightMapPosition()) - 36;
                        g_simObjects[i].posY = f15::math::legacy::mapWordY(flightMapPosition()) - (i * 0x20 + 150) * g_northSouthSign;
                        objectFineSet<ViewXAxis>(g_simObjectFineX[i], g_simObjects[i].worldX,
                                                 (int32)g_simObjects[i].posX * 32);
                        objectFineSet<ViewYAxis>(g_simObjectFineY[i], g_simObjects[i].worldY,
                                                 (int32)g_simObjects[i].posY * 32);
                        objectAttitudeSet(g_simObjectHeading[i], g_simObjects[i].heading.w,
                            g_ourHead + f15::math::Angle<f15::math::GameBackend>::halfTurn());
                    }
                }
            }
        }
        if (gameData->theater != 6) {
            g_simObjects[1].flags.w |= SIMOBJ_ALIVE;
            objectLinearSet(g_simObjectAlt[1], g_simObjects[1].alt, 2100);
            objectLinearSet(g_simObjectSpeed[1], g_simObjects[1].speed, 700);
            g_wingmanX = f15::math::legacy::mapWordX(flightMapPosition());
            g_wingmanY = 80 * g_northSouthSign + f15::math::legacy::mapWordY(flightMapPosition());
            objectFineSet<ViewXAxis>(g_simObjectFineX[1], g_simObjects[1].worldX,
                                     (int32)g_wingmanX * 32);
            objectFineSet<ViewYAxis>(g_simObjectFineY[1], g_simObjects[1].worldY,
                                     (int32)g_wingmanY * 32);
            objectAttitudeSet(g_simObjectHeading[1], g_simObjects[1].heading.w, g_ourHead);
        }
        g_northSouthSign = tmp;
        initWeaponLoadout();
        g_initPhase = 2;
        gfx_flipPage();
        g_finalThreatScore = computeThreatScore();
    }

    {
        // Coarse map units confine play to the [0x100,0x7e00]x[0x200,0x7d00]
        // theater; clipping a bound snaps the fine coordinate to the cell edge.
        const auto pos = flightMapPosition();
        const int cx = clampRange(f15::math::legacy::mapWordX(pos), 0x100, 0x7e00);
        if (cx != f15::math::legacy::mapWordX(pos)) {
            g_viewX_ = (int16)cx;
            g_ViewX = viewX((int32)cx << 5);
        }
        const int cy = clampRange(f15::math::legacy::mapWordY(pos), 0x200, 0x7d00);
        if (cy != f15::math::legacy::mapWordY(pos)) {
            g_viewY_ = (int16)cy;
            g_ViewY = viewY((int32)(0x8000 - cy) << 5);
        }
    }

    updateThreatSites();
    updateObjects();
    updateThreatTargeting();
    tickMessageTimers();
    updateBulletsAndFire();
    updateTracerParticles();
    applyGravityFall();

    if (objectToScreen(f15::math::legacy::mapWordX(flightMapPosition()),
                       f15::math::legacy::mapWordY(flightMapPosition()), (int16 *)&val, (int16 *)&screenY) != 0) {
        /* Software retained-page patch: erase last frame's player blip from the cached
         * map and stamp the new one. On GL renderTacMapOverlay re-emits the whole map
         * (incl. the blip) immediately every frame, so this bake is redundant — and
         * skipping it keeps the moving blip from dirtying the cached page texture. */
        if (!r2d_hasNativeOverlay()) {
            gfx_restoreFromImage(g_eg2dBacking, 0, val - 3, screenY - 3, val - 3, screenY - 3, 6, 6);
            blitSprite(val - 1, screenY - 1, ((signedAngle(g_ourHead) + 0x1000) >> 0xd & 7) * 4 + 164, 4, 4, 4, 0);
        }
        if (((int16)val < 32 || (int16)val > 88 || (int16)screenY < 118 || (int16)screenY > 162) && g_mapZoomLevel > 2) {
            g_mapZoomLevel--;
            redrawTacMap(f15::math::legacy::mapWordX(flightMapPosition()),
                         f15::math::legacy::mapWordY(flightMapPosition()));
        }
    } else {
        redrawTacMap(f15::math::legacy::mapWordX(flightMapPosition()),
                     f15::math::legacy::mapWordY(flightMapPosition()));
    }

    g_unusedViewXSnap = f15::math::legacy::mapWordX(flightMapPosition());
    g_unusedViewYSnap = f15::math::legacy::mapWordY(flightMapPosition());

    if (g_directorEventDeadline == frameTick) {
        if (g_autopilotEngaged == 0) {
            g_viewMode = VIEW_COCKPIT;
        }
        g_directorEventDeadline = Ticks::fromWord(-1);
    }
    if (!g_threatActiveTimer.isZero()) {
        --g_threatActiveTimer;
    }
    if (!g_destroyedCueDeadline.isZero() && frameTick == g_destroyedCueDeadline) {
        g_destroyedCueDeadline = Ticks{};
        playVoiceCue(2);
    }

    if (frameTick.phase(8) != 0) goto skip_target_section;

    g_prevThreatIndex = g_closestThreatIndex;
    g_nearestThreatRange = 0x7fff;
    for (i = 0; i < g_planeCount; i++) {
        if ((g_planeTable.planes[i].flags & 0x201) != 0 &&
            (g_planeTable.planes[i].flags & 0x500) != 0 &&
            (g_planeTable.planes[i].flags & 0x800) == 0) {
            const auto dist = mapRange(mapOffset(flightMapPosition(),
                                                 g_planeTable.planes[i].mapX, g_planeTable.planes[i].mapY));
            if (dist < g_nearestThreatRange) {
                g_nearestThreatRange = dist;
                g_closestThreatIndex = i;
            }
        }
    }
    /* Do not move the destination while the autopilot is flying its approach.
     * Nearest-base tracking still serves ground contact and nearby aircraft. */
    {
        const int autopilotLanding = !g_autopilotAltitude.isZero() && waypointIndex == 3;
        const int recoveryIndex = g_targetSlots[1].viewIndex;
        const int recoveryFlags = recoveryIndex >= 0 && recoveryIndex < g_planeCount
            ? g_planeTable.planes[recoveryIndex].flags : 0;
        const int recoveryUsable = (recoveryFlags & 0x500) != 0 &&
            (recoveryFlags & 0x201) != 0 && (recoveryFlags & 0x800) == 0;
        const int alternativeAvailable = g_nearestThreatRange != 0x7fff;
        const int recoveryBaseChanged = g_prevThreatIndex != g_closestThreatIndex ||
            g_targetSlots[1].viewIndex != g_closestThreatIndex;
        if (alternativeAvailable && recoveryBaseChanged && (!autopilotLanding || !recoveryUsable)) {
            g_targetSlots[1].viewIndex = g_closestThreatIndex;
            waypoints[3].mapX = g_planeTable.planes[g_closestThreatIndex].mapX;
            waypoints[3].mapY = g_planeTable.planes[g_closestThreatIndex].mapY;
        }
        if (autopilotLanding && !recoveryUsable && !alternativeAvailable) {
            g_autopilotAltitude = {};
            g_autoLandingActive = 0;
            hudMessage("No landing base available");
        }
    }

    if (g_prevThreatIndex != g_closestThreatIndex && (g_planeTable.planes[g_closestThreatIndex].flags & 0x800) == 0) {
        for (i = 1; i <= 2; i++) {
            g_simObjects[g_groundUnitCount - i].flags.w &= ~SIMOBJ_ALIVE;
            g_simObjects[g_groundUnitCount - i].spec = g_planeTable.planes[g_closestThreatIndex].flags & 0x400 ? 13 : 0;
            if (customWorldScenarioIs("SVN")) {
                g_simObjects[g_groundUnitCount - i].spec =
                    g_planeTable.planes[g_closestThreatIndex].flags & 0x400 ? 14 : 6;
            }
            if (g_planeTable.planes[g_closestThreatIndex].flags & 0x100) {
                g_simObjects[g_groundUnitCount - i].spec = 18;
            }
            g_simObjects[g_groundUnitCount - i].objType = g_closestThreatIndex;
        }
        for (i = 3; i <= 4; i++) {
            objIdx = g_groundUnitCount - i;
            g_simObjects[objIdx].flags.w |= SIMOBJ_ALIVE;
            g_simObjects[objIdx].posX = g_planeTable.planes[g_closestThreatIndex].mapX;
            g_simObjects[objIdx].posY = g_planeTable.planes[g_closestThreatIndex].mapY;
            if ((g_planeTable.planes[g_closestThreatIndex].flags & 0x200) != 0) {
                g_simObjects[objIdx].posX += g_northSouthSign * 5;
                g_simObjects[objIdx].posY += (i & 1) * g_northSouthSign * 0x10;
                objectLinearSet(g_simObjectAlt[objIdx], g_simObjects[objIdx].alt, 132);
            } else {
                g_simObjects[objIdx].posX += 10;
                g_simObjects[objIdx].posY += ((i + g_closestThreatIndex) & 3) * 0x10;
                objectLinearSet(g_simObjectAlt[objIdx], g_simObjects[objIdx].alt, 4);
            }
            objectFineSet<ViewXAxis>(g_simObjectFineX[objIdx], g_simObjects[objIdx].worldX,
                                     (int32)g_simObjects[objIdx].posX << 5);
            objectFineSet<ViewYAxis>(g_simObjectFineY[objIdx], g_simObjects[objIdx].worldY,
                                     (int32)g_simObjects[objIdx].posY << 5);
            objectAttitudeSet(g_simObjectHeading[objIdx], g_simObjects[objIdx].heading.w,
                              angleFromWord(-randomRange(0x4000)));
            g_simObjects[objIdx].spec = g_planeTable.planes[g_closestThreatIndex].flags & 0x400 ? 8 : 11;
            if (customWorldScenarioIs("SVN")) {
                g_simObjects[objIdx].spec =
                    g_planeTable.planes[g_closestThreatIndex].flags & 0x400 ? 0 : 7;
            }
            if (g_planeTable.planes[g_closestThreatIndex].flags & 0x100) {
                g_simObjects[objIdx].spec = 9;
            }
        }
    }

    if (frameTick.phase(128) == 0) {
        if ((g_planeTable.planes[g_closestThreatIndex].flags & 0x800) == 0) {
            objIdx = g_groundUnitCount - 2 + frameTick.ring(7, 2);
            if ((g_simObjects[objIdx].flags.w & SIMOBJ_ALIVE) == 0) {
                spawnEnemyAircraft(objIdx, g_closestThreatIndex);
                g_simObjects[objIdx].flags.w = SIMOBJ_ACTIVE | SIMOBJ_ALIVE | SIMOBJ_ENGAGED | SIMOBJ_INTERCEPTOR;
                objectLinearSet(g_simObjectAlt[objIdx], g_simObjects[objIdx].alt, 1000);
                objectLinearSet(g_simObjectSpeed[objIdx], g_simObjects[objIdx].speed, 250);
                objectFineAdvance<ViewYAxis>(g_simObjectFineY[objIdx], g_simObjects[objIdx].worldY,
                                             g_northSouthSign * 0x3000);
            }
        }
        g_unusedEventHist2 = g_unusedEventHist1;
        g_unusedEventHist1 = g_unusedEventHist0;
        g_unusedEventHist0 = 0;
    }

skip_target_section:
    if (g_nearestThreatRange < 0x200 || missionAtHeight(g_groundAltitude)) {
        g_groundAltitude = {};
        g_attackRangeX = 0xa0;
        g_attackRangeY = 0x800;
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x800) {
            g_attackRangeY = 0x400;
        }
        const auto threatOff = mapOffset(flightMapPosition(),
                                         g_planeTable.planes[g_closestThreatIndex].mapX,
                                         g_planeTable.planes[g_closestThreatIndex].mapY);
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x200) {
            g_groundAltitude = f15::math::legacy::terrainFromUnits(0x80);
            g_attackRangeX = 0x100;
            g_attackRangeY = 0x3c0;
            if (missionAtHeight(g_groundAltitude) && flightKnots() > SpeedMath::knots(0x50)) {
                const auto forwardDist = threatOff.ringY() * g_northSouthSign;
                if (forwardDist >= 0x10 && forwardDist <= 0x14) {
                    if (!gameOptionsEnabled(GAME_OPTION_NO_DAMAGE) &&
                        angleMagnitude(g_ourHead - angleFromWord((1 - g_northSouthSign) << 0xe)) < 0x2000) {
                        g_autoCrashDive = 1;
                        makeSound(22, 2);
                    }
                }
            }
        }
        if (gameData->unk4 == 1) {
            g_attackRangeX += 0x100;
            g_attackRangeY += 0x200;
        }
        if (std::abs(threatOff.dx) > (g_attackRangeX >> 5) ||
            (std::abs(threatOff.dy) > (g_attackRangeY >> 5))) {
            g_groundAltitude = {};
            g_inLandingCorridor = 0;
        } else {
            g_inLandingCorridor = 1;
            if ((flightKnots() <= SpeedMath::knots(1)) && (frameTick.phase(8) == 0) && g_planeTable.planes[g_closestThreatIndex].flags & 0x500 && !g_landingTimer.isZero() && !(g_planeTable.planes[g_closestThreatIndex].flags & 0x800)) {
                g_gearDownArmed = 1;
                g_landingDoneFlag = 1;
                if (g_landingTimer++.equals(1)) {
                    hudMessage("Safe Landing");
                    g_autopilotAltitude = {};
                    g_autoLandingActive = 0;
                    playVoiceCue(4);
                }
                if ((g_playerPlaneFlags & 0x6000) == 0x6000) {
                    if (g_landingTimer.exceeds(g_frameRateScaling.word())) {
                        finalizeMission(0);
                    }
                } else {
                    if (g_landingTimer.equals(2)) {
                        g_resupplyCount++;
                        appendMapEvent(10, g_closestThreatIndex);
                    }
                    if (g_landingTimer.exceeds(g_frameRateScaling.word())) {
                        initWeaponLoadout();
                        if (frameTick.bit(3)) {
                            hudMessage("Ready for takeoff");
                        } else {
                            hudMessage("Weapons replenished");
                        }
                    }
                }
            }
        }
    end_landing_check:
        if ((g_landingDoneFlag == 0) && (g_missionStatus == 0) && g_playerPlaneFlags & 0x6000) {
            if (std::abs(threatOff.dx) < 0x10 && std::abs(threatOff.dy) < 0x10) {
                g_altitude = {};
                g_velocity = {};
                g_setThrust = {};
                g_ViewX = viewX((int32)g_planeTable.planes[g_closestThreatIndex].mapX << 5);
                g_ViewY = viewY((int32)(0x8000 - g_planeTable.planes[g_closestThreatIndex].mapY) << 5);
            } else {
                hudMessage("Automatic Landing Engaged");
                g_autoLandingActive = 1;
                i = g_frameRateScaling.scaled(2);
                if (i > 14) {
                    i = 14;
                }
                g_velocity = f15::math::legacy::speedFromUnits(5400);
                g_altitude = f15::math::AltitudeMath<f15::math::GameBackend>::landingApproach(
                    g_altitude, g_groundAltitude, i);
                using HorizontalMath = f15::math::HorizontalMath<f15::math::GameBackend>;
                g_ViewX = HorizontalMath::approach(g_ViewX, viewX((int32)g_planeTable.planes[g_closestThreatIndex].mapX << 5), i);
                g_ViewY = HorizontalMath::approach(g_ViewY, viewY((int32)(0x8000 - g_planeTable.planes[g_closestThreatIndex].mapY) << 5), i);
            }
        }
    } else {
        g_landingDoneFlag = 0;
        g_inLandingCorridor = 0;
    }

skip_autopilot:
    if (g_inLandingCorridor == 0) {
        if (missionAtHeight({})) {
            if (!android_ar_preventCrashes() &&
                !gameOptionsEnabled(GAME_OPTION_NO_DAMAGE) &&
                (gameData->unk4 != 0 || g_gunHits > 4 || g_fuelRemaining.isZero()) &&
                g_ejectState == 0 && flightKnots() > SpeedMath::knots(50)) {
                makeSound(0, 2);
                setDrawColor(COLOR_BLACK);
                fillRectBoth(0, 0, 319, 199);
                waitFrameSync(120);
                finalizeMission(1);
            }
        } else {
            g_landingTimer = TickDuration::fromWord(1);
        }
    }

    if ((g_savedPosVisible != 0 || aircraftInsideReplacementTerrain()) && (g_viewMode & 0x80) == 0) {
        if (!gameOptionsEnabled(GAME_OPTION_NO_DAMAGE) &&
            gameData->unk4 != 0 && !g_altitude.isZero()) {
            makeSound(0, 2);
            gfx_waitRetrace();
            waitFrameSync(120);
            finalizeMission(2);
        } else {
            g_altitude = f15::math::AltitudeMath<f15::math::GameBackend>::obstacleEscape(g_altitude);
            g_autopilotAltitude = {};
        }
    }

    g_targetLeadAngle = (g_planeTable.planes[g_closestThreatIndex].flags & 0x200 && g_nearestThreatRange < 0x500) ? f15::math::legacy::angleFromWord(((g_frameRateScaling.perTick(g_northSouthSign << 8)) + f15::math::legacy::signedAngle(g_targetLeadAngle)) & 0xfff) : f15::math::Angle<f15::math::GameBackend>{};

    frameTick++;
    if (frameTick.mod(g_frameRateScaling.word()) == 0) {
        g_missionTick++;
        if (g_missionTick.phase(32) == 0) {
            appendMapEvent(9, 0);
        }
        if (g_missionTick.equals(1)) {
            playVoiceCue(0);
            updateEngineSound();
        }
        if (g_autopilotEngaged != 0 && g_missionTick.phase(4) == 0) {
            generateRandomRadioMessage();
        }
    }

    if (++g_frameRateAccum >= g_frameRateScaling.scaled(4)) {
        /* Render/sim decoupled: the adaptive frame governor (which rescaled
         * g_frameRateScaling and added sleep ticks to throttle the loop) is
         * retired — the sim now steps fixed-rate in gameMainLoop. Keep the
         * jiffies debug readout and the periodic enemy-alert scan. */
        g_jiffiesPerFrame = g_frameRateScaling.perTick(g_frameTimingAccum);
        g_frameRateAccum = g_frameTimingAccum = 0;
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
                (g_simObjects[i].flags.w & SIMOBJ_ALIVE) != 0) {
                g_enemyAlertFlag++;
                break;
            }
        }
    }
    dispatchKeyScancode();
}

// ==== seg000:0x14e8 ====
void dispatchKeyScancode(void) {
    int16 unused0, unused1, unused2, unused3, unused4, unused5, unused6, unused7;
    keyDispatch(keyScancode);
}

// ==== seg000:0x14fc ====
void countermeasures(int16 eventType) {
    const char *name = "Countermeasure";
    int16 i, slot;

    slot = -1;
    if (!gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS) &&
        g_eventTimers[eventType]--.atMost(0)) {
        g_eventTimers[eventType] = TickDuration{};
        hudMessage("Stores exhausted");
    } else {
        for (i = 1; i < 4; i++) {
            if (mapEvents[i].ttl == 0)
                slot = i;
        }
        if (slot != -1) {
            mapEvents[slot].mapX = f15::math::legacy::mapWordX(flightMapPosition());
            mapEvents[slot].mapY = f15::math::legacy::mapWordY(flightMapPosition());
            mapEvents[slot].type = eventType;
            mapEvents[slot].ttl =
                g_frameRateScaling.scaled(-(g_missionStatus * 3 - 15));
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
            strcat(strBuf, itoa(g_eventTimers[eventType].word(), g_itoaScratch, 10));
            setTimedMessage(strBuf);
        }
        makeSound(22, 2);
    }
}

// ==== seg000:0x1636 ====
void tickMessageTimers(void) {
    int16 i;
    for (i = 0; i < 4; i++) {
        if (mapEvents[i].ttl != 0) {
            (mapEvents[i].ttl)--;
            if (mapEvents[i].ttl == 0) {
                mapEvents[i].type = 0;
            }
        }
    }
}

void updateBulletsAndFire(void) {
    int16 firing, mag, yaw, pitch, i, slot;
    /* horizontal magnitude after the pitch decompose — the original stored it
     * back into the int16 mag; StepRep keeps the fraction under modern.
     * Declared here so the goto paths don't cross its initialization. */
    FineCoord::StepRep horizMag;

    for (i = 0; i < g_bulletTrackCount + 4; i++) {
        if (!bulletTracks[i].posX.isZero()) {
            bulletTracks[i].posX = bulletTracks[i].posX.advanced(bulletTracks[i].velX);
            bulletTracks[i].posY = bulletTracks[i].posY.advanced(bulletTracks[i].velY);
            bulletTracks[i].alt += bulletTracks[i].velZ;
        }
    }
    if (!frameTick.bit(0)) {
        return;
    }
    slot = frameTick.shifted(1) % g_bulletTrackCount;
    firing = readAxisInput(0);
    if (!firing) goto no_fire;
    if (g_gunAmmo <= 0) goto no_fire;
    if (g_ejectState != 0) goto no_fire;
    if (!gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS))
        g_gunAmmo = clampRange(g_gunAmmo - g_frameRateScaling.perTick(40), 0, 1000);
    makeSound(4, 2);
    /* Round leaves the barrel along the airframe axis plus the M61's dispersion
     * cone; magnitude in fine units per step (the original 186 coarse/s). */
    yaw = (int16)signedAngle(g_ourHead) + gunSpreadAngle();
    pitch = (int16)signedAngle(g_ourPitch) + gunSpreadAngle();
    mag = g_frameRateScaling.perTick(186 << 5);
    bulletTracks[slot].velZ = TrackMath::sineVelocity(angleFromWord(pitch), mag, g_angleLut);
    horizMag = TrackMath::cosineVelocity(angleFromWord(pitch), mag, g_angleLut);
    bulletTracks[slot].velX = TrackMath::sineVelocity(angleFromWord(yaw), horizMag, g_angleLut);
    bulletTracks[slot].velY = -TrackMath::cosineVelocity(angleFromWord(yaw), horizMag, g_angleLut);
    bulletTracks[slot].posX = FineCoord::fromRep(fineRep(g_ViewX) + bulletTracks[slot].velX);
    bulletTracks[slot].posY = FineCoord::fromRep(0x100000L - fineRep(g_ViewY) + bulletTracks[slot].velY);
    bulletTracks[slot].alt = bulletTracks[slot].velZ + f15::math::legacy::Altitudes::renderWord(flightSceneHeight()) - 2;
    g_gunFiredFlag = 1;
    goto done_fire;
no_fire:
    bulletTracks[slot].posX = {};
    g_gunFiredFlag = 0;
done_fire:
    if (firing) {
        strcpy(strBuf, "GUN:");
        strcat(strBuf, itoa(g_gunAmmo, g_itoaScratch, 10));
        setTimedMessage(strBuf);
    }
}

// ==== seg000:0x1841 ====
void updateTracerParticles() {
    int16 i, slot;

    if (g_smokeSourceIdx != -1) {
        for (i = 0; i < 8; i++) {
            g_particles[i].alt += 10;
            g_particles[i].posY += g_particles[i].alt >> 9;
            *(((char *)&g_particles[i].spin) + 1) += 6;
        }
        if (frameTick.phase(16) == 0) {
            slot = frameTick.ring(4, 8);
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
    if (g_wreckAlt.isPositive()) {
        if (g_wreckFallVel > f15::math::legacy::climbFromUnits(-16)) {
            g_wreckFallVel -= f15::math::legacy::climbFromUnits(12);
        }
        g_wreckAlt = f15::math::AltitudeMath<f15::math::GameBackend>::integrate(g_wreckAlt, g_wreckFallVel);
    }
}

// ==== seg000:0x18f6 ====
void initFrameRandom(void) {
    int16 seedSum, unused0, unused1, unused2;

    seedRng();
    clearStatusPanel();
    frameTick = Ticks::fromWord((int16)(randomRange(0x1000) & 0x7ff8));
    seedSum = g_targetSlots[0].seedNoise + g_targetSlots[1].seedNoise;
    g_nightMode = (gameData->theater == 6 ? 5 : 9) < randomRange(0x10);
    if (g_nightMode && g_dacSupported) {
        setupDac();
    }
    g_unusedFrameVal = (seedSum & 0xF) << 8;
    g_missionTick = TickDuration{};
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
    g_fuelRemaining = f15::math::legacy::fuelFromUnits(10000);
    g_eventTimers[2] = TickDuration::fromWord(18);
    g_eventTimers[1] = TickDuration::fromWord(12);
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
    commData->worldX = f15::math::legacy::mapWordX(flightMapPosition());
    commData->worldY = f15::math::legacy::mapWordY(flightMapPosition());
    commData->weaponCount[0] = g_finalThreatScore;
    commData->weaponCount[1] = g_resupplyCount;
    commData->gunHits = g_gunHits;
    appendMapEvent(8, 0);
}

// ==== seg000:0x1bc3 ====
void scheduleEventCheck(int16 eventObjIdx, uint16 priority) {
    if (priority > (uint16)g_directorMode) return;
    if (g_directorEventDeadline.word() != -1) return;
    g_viewTargetObj = eventObjIdx;
    scheduleTimedEvent(VIEW_MISSILE, g_directorMode == 1 ? 3 : 4);
}

// ==== seg000:0x1bfd scheduleTimedEvent ====
void scheduleTimedEvent(ViewMode viewMode, int16 delay) {
    if (g_directorMode == 0) {
        return;
    }
    g_viewMode = viewMode;
    g_directorEventDeadline = frameTick.offset(g_frameRateScaling.scaled(delay));
}

// ==== seg000:0x1c21 routine_180 ====
void generateRandomRadioMessage(void) {
    int16 idx;

    if (g_directorEventDeadline.word() != -1) {
        return;
    }
    g_autopilotAltitude = f15::math::legacy::renderHeightFromUnits(500);
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
        } while (g_simObjectSpeed[idx] == 0);
        g_viewTargetObj = idx + 0x20;
        g_viewMode = VIEW_MISSILE;
        strcpy(strBuf, aircraftTypes[g_simObjects[idx].spec].name);
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
    if (g_eventLogCount >= 255) {
        return;
    }
    g_replayLog.events[g_eventLogCount].coord = g_missionTick.word();
    g_replayLog.events[g_eventLogCount].screenX = (uint16)f15::math::legacy::mapWordX(flightMapPosition()) >> 7;
    g_replayLog.events[g_eventLogCount].screenY = (uint16)f15::math::legacy::mapWordY(flightMapPosition()) >> 7;
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
    worldImportToEgame();
    g_targetNameTable[0] = g_stringPool;
    nameIdx = 1;
    for (i = 0; i < 750; ++i) {
        if (g_stringPool[i] == 0 && nameIdx < MODEL_SLOT_CAPACITY) {
            g_targetNameTable[nameIdx++] = &g_stringPool[i + 1];
        }
    }
    if (gameData->difficulty != 0) { // 1e6c
        g_ViewX = viewX(((int32)(g_planeTable.planes[g_targetSlots[0].viewIndex].mapX) << 5) + 2);
        g_ViewY = viewY((0x8000 - (int32)(g_planeTable.planes[g_targetSlots[0].viewIndex].mapY)) << 5);
    } else {
        g_ViewX = viewX(((int32)waypoints[0].mapX << 5) + 2);
        g_ViewY = viewY((0x8000 - (int32)waypoints[0].mapY) << 5);
    }
    const auto pos = f15::math::legacy::mapPosition(g_ViewX, g_ViewY);
    g_viewX_ = f15::math::legacy::mapWordX(pos);
    g_viewY_ = f15::math::legacy::mapWordY(pos);
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
