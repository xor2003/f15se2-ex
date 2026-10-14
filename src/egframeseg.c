/*
 * egframeseg.c - the ordered segments of the original monolithic updateFrame().
 *
 * Upstream ran one giant per-tick function. For the authoritative server it
 * was split into named segments so f15world.c can run the world-mutating
 * (W) segments once per tick and the player-scoped (P) segments once per
 * PlayerSim context; single-player updateFrame() (egframe.c) inlines the
 * same logic in the original order:
 *
 *   P-pre W-sims P-fire W-tracers P-gravity P-tacmap P-timers W-threat
 *   P-mission W-tick P-keys
 *
 * The function BODIES here are the typed-math port of the same code;
 * multiplayer-specific additions are commented inline. The
 * server-side composition lives in f15world.c (updateWorldFrame /
 * updatePlayerFrame); the threat scan/escort split is defined there too
 * because its world half needs the caller-chosen threat index.
 */
#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
using f15::math::legacy::fineRep;
using f15::math::legacy::viewX;
using f15::math::legacy::viewY;
using f15::math::legacy::moveX;
using f15::math::legacy::moveY;

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
#include "game_options.h"
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

void findWaypointFeatures();
void initFrameRandom();
void generateRandomRadioMessage();
int16 setCommWorldbufPtr();
int randomRange(int);
int16 gunSpreadAngle(void);
void updateBulletsAndFire();

static bool missionAtHeight(f15::math::TerrainHeight<f15::math::GameBackend> height) {
    using namespace f15::math;
    return AltitudeMath<GameBackend>::atGround(flightSceneHeight(), height);
}

/* P: view/map coords, first-tick player + world init, edge clamp. */
void framePlayerPre(void) {
    int16 tmp, unused;
    uint16 val;
    int16 i;

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
        g_fireCooldown = {};
        g_bombDamageMask = missileSpecIndex = waypointIndex = g_unusedWaypointTail = 0;
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
        /* Coarse map units confine play to the [0x100,0x7e00]x[0x200,0x7d00]
         * theater; clipping a bound snaps the fine coordinate to the cell edge. */
        const auto pos = flightMapPosition();
        const int cx = clampRange(f15::math::legacy::mapWordX(pos), 0x100, 0x7e00);
        if (cx != f15::math::legacy::mapWordX(pos)) {
            g_viewX_ = (int16)cx;
            g_ViewX = viewX((int32)cx << 5);
        }
        const int cy = clampRange(f15::math::legacy::mapWordY(pos), 0x200, 0x7d00);
        if (cy != f15::math::legacy::mapWordY(pos)) {
            g_viewY_ = (int16)cy;
            g_ViewY = viewY((int32)(MAP_Y_MIRROR - cy) << 5);
        }
    }
    (void)unused;
}

/* P: the player blip on the tactical map + zoom auto-adjust. Presentation
 * only; the headless server skips it (caller gates on g_headlessSim). */
void frameTacmapBlip(void) {
    uint16 val;
    uint16 screenY;

    if (objectToScreen(f15::math::legacy::mapWordX(flightMapPosition()),
                       f15::math::legacy::mapWordY(flightMapPosition()), (int16 *)&val, (int16 *)&screenY) != 0) {
        /* Software retained-page patch: erase last frame's player blip from the cached
         * map and stamp the new one. On GL renderTacMapOverlay re-emits the whole map
         * (incl. the blip) immediately every frame, so this bake is redundant — and
         * skipping it keeps the moving blip from dirtying the cached page texture. */
        if (!r2d_hasNativeOverlay()) {
            gfx_restoreFromImage(g_eg2dBacking, 0, val - 3, screenY - 3, val - 3, screenY - 3, 6, 6);
            blitSprite(val - 1, screenY - 1, ((f15::math::legacy::signedAngle(g_ourHead) + 0x1000) >> 0xd & 7) * 4 + 164, 4, 4, 4, 0);
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
}

/* P: per-tick countdown timers that belong to the player (director event,
 * threat-active flash, destroyed-voice cue). */
void framePlayerTimers(void) {
    if (g_directorEventDeadline == frameTick) {
        if (g_autopilotEngaged == 0) {
            g_viewMode = VIEW_COCKPIT;
        }
        g_directorEventDeadline = Ticks::disarmed();
    }
    if (!g_threatActiveTimer.isZero()) {
        --g_threatActiveTimer;
    }
    if (!g_destroyedCueDeadline.isZero() && frameTick == g_destroyedCueDeadline) {
        g_destroyedCueDeadline = Ticks{};
        playVoiceCue(2);
    }
}

/* P: landing-corridor/autopilot mission logic driven off the player's own
 * position vs the closest threat airfield. */
void framePlayerMission(void) {
    int16 i;

    if (g_nearestThreatRange < 0x200 || missionAtHeight(g_groundAltitude)) {
        /* Corridor half-extents in map words. The originals stored fine
         * (word<<5) constants that every consumer shifted back — all exact
         * multiples of 32, so the word values are lossless. The scan box is
         * a long thin approach funnel (shorter when the base is destroyed);
         * the landing box is the |dx|<=8, |dy|<=30 corridor the auto-land
         * check gates on. gameData->unk4==1 widens both. */
        constexpr int16 kScanHalfX = 0xa0 >> 5;        /* 5  */
        constexpr int16 kScanHalfY = 0x800 >> 5;       /* 64 */
        constexpr int16 kScanHalfYDestroyed = 0x400 >> 5; /* 32 */
        constexpr int16 kCorridorHalfX = 0x100 >> 5;   /* 8  */
        constexpr int16 kCorridorHalfY = 0x3c0 >> 5;   /* 30 */
        constexpr int16 kEasyHalfXBonus = 0x100 >> 5;  /* +8  */
        constexpr int16 kEasyHalfYBonus = 0x200 >> 5;  /* +16 */
        g_groundAltitude = {};
        g_attackRangeX = kScanHalfX;
        g_attackRangeY = kScanHalfY;
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x800) {
            g_attackRangeY = kScanHalfYDestroyed;
        }
        const auto threatOff = mapOffset(flightMapPosition(),
                                         g_planeTable.planes[g_closestThreatIndex].mapX,
                                         g_planeTable.planes[g_closestThreatIndex].mapY);
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x200) {
            g_groundAltitude = f15::math::legacy::terrainFromUnits(0x80);
            g_attackRangeX = kCorridorHalfX;
            g_attackRangeY = kCorridorHalfY;
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
            g_attackRangeX += kEasyHalfXBonus;
            g_attackRangeY += kEasyHalfYBonus;
        }
        if (std::abs(threatOff.dx) > g_attackRangeX ||
            (std::abs(threatOff.dy) > g_attackRangeY)) {
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
                g_ViewY = viewY((int32)(MAP_Y_MIRROR - g_planeTable.planes[g_closestThreatIndex].mapY) << 5);
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
                g_ViewY = HorizontalMath::approach(g_ViewY, viewY((int32)(MAP_Y_MIRROR - g_planeTable.planes[g_closestThreatIndex].mapY) << 5), i);
            }
        }
    } else {
        g_landingDoneFlag = 0;
        g_inLandingCorridor = 0;
    }

skip_autopilot:
    if (g_inLandingCorridor == 0) {
        if (missionAtHeight({})) {
            if ((gameData->unk4 != 0 || g_gunHits > 4 || g_fuelRemaining.isZero()) &&
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

    if (g_savedPosVisible != 0 && (g_viewMode & 0x80) == 0) {
        if (gameData->unk4 != 0 && !g_altitude.isZero()) {
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
}

/* W: the global mission clock. frameTick/missionTick advance once per sim
 * step regardless of player count; the periodic enemy-alert scan is world
 * state too. */
void frameWorldTick(void) {
    int16 i;

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
}

/* W: advance every live tracer round by its velocity. World state: a round
 * must keep flying even while its owner is not the resident ctx. */
void moveBullets(void) {
    int16 i;

    for (i = 0; i < g_bulletTrackCount + 4; i++) {
        if (!bulletTracks[i].posX.isZero()) {
            bulletTracks[i].posX = bulletTracks[i].posX.advanced(bulletTracks[i].velX);
            bulletTracks[i].posY = bulletTracks[i].posY.advanced(bulletTracks[i].velY);
            bulletTracks[i].alt += bulletTracks[i].velZ;
        }
    }
}

/* P: player gun trigger + tracer spawn; reads the ctx's axis/button state. */
void tryPlayerFire(void) {
    int16 firing, mag, yaw, pitch, slot;
    FineCoord::StepRep horizMag;

    if (!frameTick.bit(0)) {
        return;
    }
    firing = readAxisInput(0);
    if (!firing) goto no_fire;
    if (g_gunAmmo <= 0) goto no_fire;
    if (g_ejectState != 0) goto no_fire;
    /* Round slot selection - AFTER the eligibility checks: an idle/out-of-ammo
     * player must not advance the shared cursor (it fixes saturated eviction
     * order, so a pass here would reshuffle which airborne round a later
     * shooter loses).
     * - server: claim any FREE slot in the shared player pool (0..count-1)
     *   and stamp the owner. A saturated pool overwrites in round-robin
     *   order via g_bulletPoolCursor: every firing player gets a DIFFERENT
     *   slot within a tick (allocation order can't erase a teammate's new
     *   shot) and each round survives >= count launches globally. The
     *   cursor is unsigned, so it stays valid across the int16 frameTick
     *   wrap; the SP remainder in no_fire is sign-normalized likewise.
     * - free->full transition: the cursor may point AT the free slot the
     *   previous shooter just claimed; the fresh mask records every slot
     *   allocated this tick and the fallback skips them, so a later same-
     *   tick shooter can never evict a round that hasn't left the barrel
     *   zone yet. With <=8 shooters and count>=16 a non-fresh victim
     *   always exists; the bounded loop degrades to plain round-robin if
     *   every slot is somehow fresh.
     *   A later shot or a trigger release never deletes an airborne round. */
    if (g_residentPlayer >= 0) {
        int tries;
        if (frameTick.word() != g_bulletFreshTick)
            g_bulletFreshMask = 0;
        g_bulletFreshTick = frameTick.word();
        for (slot = 0; slot < g_bulletTrackCount; slot++)
            if (bulletTracks[slot].posX.isZero())
                break;
        if (slot >= g_bulletTrackCount) {
            tries = g_bulletTrackCount;
            do {
                slot = (int16)(g_bulletPoolCursor++ % (uint32)g_bulletTrackCount);
            } while ((g_bulletFreshMask & (1u << slot)) && --tries > 0);
        }
        g_bulletFreshMask |= 1u << slot;
    } else {
        slot = frameTick.shifted(1) % g_bulletTrackCount;
        if (slot < 0)
            slot += g_bulletTrackCount;
    }
    if (!gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS))
        g_gunAmmo = clampRange(g_gunAmmo - g_frameRateScaling.perTick(40), 0, 1000);
    makeSound(4, 2);
    /* Round leaves the barrel along the airframe axis plus the M61's dispersion
     * cone; magnitude in fine units per step (the original 186 coarse/s). */
    yaw = (int16)f15::math::legacy::signedAngle(g_ourHead) + gunSpreadAngle();
    pitch = (int16)f15::math::legacy::signedAngle(g_ourPitch) + gunSpreadAngle();
    mag = g_frameRateScaling.perTick(186 << 5);
    bulletTracks[slot].velZ = TrackMath::sineVelocity(angleFromWord(pitch), mag, g_angleLut);
    horizMag = TrackMath::cosineVelocity(angleFromWord(pitch), mag, g_angleLut);
    bulletTracks[slot].velX = TrackMath::sineVelocity(angleFromWord(yaw), horizMag, g_angleLut);
    bulletTracks[slot].velY = -TrackMath::cosineVelocity(angleFromWord(yaw), horizMag, g_angleLut);
    bulletTracks[slot].posX = FineCoord::fromRep(fineRep(g_ViewX) + bulletTracks[slot].velX);
    bulletTracks[slot].posY = FineCoord::fromRep(0x100000L - fineRep(g_ViewY) + bulletTracks[slot].velY);
    bulletTracks[slot].alt = bulletTracks[slot].velZ + f15::math::legacy::Altitudes::renderWord(flightSceneHeight()) - 2;
    bulletTracks[slot].targetPlayer = g_residentPlayer;
    g_gunFiredFlag = 1;
    goto done_fire;
no_fire:
    /* legacy release-sweep, SP only: on the server an airborne round keeps
     * flying (it dies on a hit or when the shared pool overwrites it). The
     * rotating slot is the original frame-derived index; a round lives
     * ~2*g_bulletTrackCount ticks before the rotation overwrites it. */
    if (g_residentPlayer < 0) {
        slot = frameTick.shifted(1) % g_bulletTrackCount;
        if (slot < 0)
            slot += g_bulletTrackCount;
        bulletTracks[slot].posX = {};
    }
    g_gunFiredFlag = 0;
done_fire:
    if (firing) {
        strcpy(strBuf, "GUN:");
        strcat(strBuf, itoa(g_gunAmmo, g_itoaScratch, 10));
        setTimedMessage(strBuf);
    }
}
