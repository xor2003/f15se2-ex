/*
 * egframeseg.c - the ordered segments of the original monolithic updateFrame().
 *
 * Upstream ran one giant per-tick function. For the authoritative server it
 * was split into named segments so f15world.c can run the world-mutating
 * (W) segments once per tick and the player-scoped (P) segments once per
 * PlayerSim context; single-player updateFrame() (egframe.c) calls the same
 * segments in the original interleaved order:
 *
 *   P-pre W-sims P-fire W-tracers P-gravity P-tacmap P-timers W-threat
 *   P-mission W-tick P-keys
 *
 * The function BODIES here are the upstream code, re-wrapped but otherwise
 * unchanged; multiplayer-specific additions are commented inline. The
 * server-side composition lives in f15world.c (updateWorldFrame /
 * updatePlayerFrame); the threat scan/escort split is defined there too
 * because its world half needs the caller-chosen threat index.
 */
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

void findWaypointFeatures();
void initFrameRandom();
void generateRandomRadioMessage();
int16 setCommWorldbufPtr();
int randomRange(int);
int16 gunSpreadAngle(void);

/* P: view/map coords, first-tick player + world init, edge clamp. */
void framePlayerPre(void) {
    int16 tmp, unused;
    uint16 val;
    int16 i;

    g_viewX_ = (int16)((g_ViewX + 0x10L) >> 5);
    g_viewY_ = -((int16)((g_ViewY + 0x10L) >> 5) - 0x8000);

    if (g_initPhase == 1) {
        g_playerPlaneFlags = 0;
        if (gameData->difficulty == 4) {
            gameData->difficulty = 2;
            g_autopilotEngaged = 1;
            g_playerPlaneFlags |= 0x1000;
            *(char far *)&commData->trainingFlag |= 1;
        }
        findWaypointFeatures();
        g_threatActiveTimer = 0;
        g_scopeSweepTimer = 1;
        g_airTargetLock = g_groundTargetLock = -1;
        g_fireCooldown = g_bombDamageMask = missileSpecIndex = g_autopilotAltitude = waypointIndex = g_unusedWaypointTail = 0;
        g_closestThreatIndex = g_unusedEventHist0 = g_unusedEventHist1 = g_unusedEventHist2 = (int8)(g_halfScaleRender = 0);
        g_threatRefX = g_threatRefY = g_threatRefZ = 0;
        g_prevThreatIndex = g_smokeSourceIdx = -1;
        g_fuelRemaining = 10000;
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
        g_frameRateScaling = 15;
        recalcTimeScale();
        g_mapZoomLevel = 1;
        g_radarScopeRange = 1;
        tmp = g_northSouthSign = (gameData->theater == 6) ? 1 : (gameData->theater & 1) ? 1
                                                                                        : -1;

        if (((g_planeTable.planes[g_targetSlots[0].viewIndex].flags) & 0x200) != 0) {
            g_ViewX -= (int32)(tmp * 0x80);
            *(char *)&g_playerPlaneFlags |= 8;
        } else {
            g_ViewY -= (int32)(1800 * g_northSouthSign);
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
            g_northSouthSign = ((uint16)(g_viewY_ - waypoints[1].mapY) < 0x8000u) ? 1 : -1;
            g_altitude = 2000;
            g_velocity = 8100;
            g_setThrust = 100;
            UpdateThrottleState();
            *(char *)&g_playerPlaneFlags |= 1;
            *(char *)&g_playerPlaneFlags &= ~8;
            if (gameData->totalScore == 0 && gameData->theater != 6) {
                for (i = 0; i < g_groundUnitCount - 4; i++) {
                    if ((i & 1) == 0) {
                        g_simObjects[i].flags.b[0] |= 2;
                        g_simObjects[i].alt = 2200;
                        g_simObjects[i].speed = 300;
                        g_simObjects[i].posX = i * 12 + g_viewX_ - 36;
                        g_simObjects[i].posY = g_viewY_ - (i * 0x20 + 150) * g_northSouthSign;
                        g_simObjects[i].worldX = (int32)g_simObjects[i].posX * 32;
                        g_simObjects[i].worldY = (int32)g_simObjects[i].posY * 32;
                        g_simObjects[i].heading.w = g_ourHead + 0x8000;
                    }
                }
            }
        }
        if (gameData->theater != 6) {
            g_simObjects[1].flags.b[0] |= 2;
            g_simObjects[1].alt = 2100;
            g_simObjects[1].speed = 700;
            g_wingmanX = g_viewX_;
            g_wingmanY = 80 * g_northSouthSign + g_viewY_;
            g_simObjects[1].worldX = (int32)g_wingmanX * 32;
            g_simObjects[1].worldY = (int32)g_wingmanY * 32;
            g_simObjects[1].heading.w = g_ourHead;
        }
        g_northSouthSign = tmp;
        initWeaponLoadout();
        g_initPhase = 2;
        gfx_flipPage();
        g_finalThreatScore = computeThreatScore();
    }

    val = clampRange(g_viewX_, 0x100, 0x7e00);
    if (val != g_viewX_) {
        g_viewX_ = val;
        g_ViewX = (int32)val << 5;
    }
    val = clampRange(g_viewY_, 0x200, 0x7d00);
    if (val != g_viewY_) {
        g_viewY_ = val;
        g_ViewY = (int32)(0x8000 - g_viewY_) << 5;
    }
    (void)unused;
}

/* P: the player blip on the tactical map + zoom auto-adjust. Presentation
 * only, but the server skips it under g_headlessSim so it stays a segment. */
void frameTacmapBlip(void) {
    uint16 val;
    uint16 screenY;

    if (objectToScreen(g_viewX_, g_viewY_, (int16 *)&val, (int16 *)&screenY) != 0) {
        /* Software retained-page patch: erase last frame's player blip from the cached
         * map and stamp the new one. On GL renderTacMapOverlay re-emits the whole map
         * (incl. the blip) immediately every frame, so this bake is redundant — and
         * skipping it keeps the moving blip from dirtying the cached page texture. */
        if (!r2d_hasNativeOverlay()) {
            gfx_restoreFromImage(g_eg2dBacking, 0, val - 3, screenY - 3, val - 3, screenY - 3, 6, 6);
            blitSprite(val - 1, screenY - 1, ((g_ourHead + 0x1000) >> 0xd & 7) * 4 + 164, 4, 4, 4, 0);
        }
        if (((int16)val < 32 || (int16)val > 88 || (int16)screenY < 118 || (int16)screenY > 162) && g_mapZoomLevel > 2) {
            g_mapZoomLevel--;
            redrawTacMap(g_viewX_, g_viewY_);
        }
    } else {
        redrawTacMap(g_viewX_, g_viewY_);
    }

    g_unusedViewXSnap = g_viewX_;
    g_unusedViewYSnap = g_viewY_;
}

/* P: per-tick countdown timers that belong to the player (director event,
 * threat-active flash, destroyed-voice cue). */
void framePlayerTimers(void) {
    if (g_directorEventDeadline == frameTick) {
        if (g_autopilotEngaged == 0) {
            g_viewMode = VIEW_COCKPIT;
        }
        g_directorEventDeadline = -1;
    }
    if (g_threatActiveTimer != 0) {
        g_threatActiveTimer--;
    }
    if (g_destroyedCueDeadline != 0 && frameTick == g_destroyedCueDeadline) {
        g_destroyedCueDeadline = 0;
        playVoiceCue(2);
    }
}

/* P: landing-corridor/autopilot mission logic driven off the player's own
 * position vs the closest threat airfield. */
void framePlayerMission(void) {
    int16 i;

    if (g_nearestThreatRange < 0x200 || g_groundAltitude == g_viewZ) {
        g_groundAltitude = 0;
        g_attackRangeX = 0xa0;
        g_attackRangeY = 0x800;
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x800) {
            g_attackRangeY = 0x400;
        }
        if (g_planeTable.planes[g_closestThreatIndex].flags & 0x200) {
            g_groundAltitude = 0x80;
            g_attackRangeX = 0x100;
            g_attackRangeY = 0x3c0;
            if (g_viewZ == 0x80 && g_knots > 0x50) {
                if ((uint16)(g_viewY_ - g_planeTable.planes[g_closestThreatIndex].mapY) * g_northSouthSign >= 0x10 && (uint16)(g_viewY_ - g_planeTable.planes[g_closestThreatIndex].mapY) * g_northSouthSign <= 0x14) {
                    if (abs((int16)(g_ourHead - ((1 - g_northSouthSign) << 0xe))) < 0x2000) {
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
        if (abs(g_viewX_ - g_planeTable.planes[g_closestThreatIndex].mapX) > (g_attackRangeX >> 5) ||
            (abs(g_viewY_ - g_planeTable.planes[g_closestThreatIndex].mapY) > (g_attackRangeY >> 5))) {
            g_groundAltitude = 0;
            g_inLandingCorridor = 0;
        } else {
            g_inLandingCorridor = 1;
            if ((g_knots <= 1) && ((frameTick & 7) == 0) && g_planeTable.planes[g_closestThreatIndex].flags & 0x500 && g_landingTimer != 0 && !(g_planeTable.planes[g_closestThreatIndex].flags & 0x800)) {
                g_gearDownArmed = 1;
                g_landingDoneFlag = 1;
                if (g_landingTimer++ == 1) {
                    hudMessage("Safe Landing");
                    g_autopilotAltitude = 0;
                    g_autoLandingActive = 0;
                    playVoiceCue(4);
                }
                if ((g_playerPlaneFlags & 0x6000) == 0x6000) {
                    if (g_landingTimer > g_frameRateScaling) {
                        finalizeMission(0);
                    }
                } else {
                    if (g_landingTimer == 2) {
                        g_resupplyCount++;
                        appendMapEvent(10, g_closestThreatIndex);
                    }
                    if (g_landingTimer > g_frameRateScaling) {
                        initWeaponLoadout();
                        if (frameTick & 8) {
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
            if (abs(g_viewX_ - g_planeTable.planes[g_closestThreatIndex].mapX) < 0x10 && abs(g_viewY_ - g_planeTable.planes[g_closestThreatIndex].mapY) < 0x10) {
                g_setThrust = g_velocity = g_altitude = 0;
                g_ViewX = (int32)g_planeTable.planes[g_closestThreatIndex].mapX << 5;
                g_ViewY = (int32)(0x8000 - g_planeTable.planes[g_closestThreatIndex].mapY) << 5;
            } else {
                hudMessage("Automatic Landing Engaged");
                g_autoLandingActive = 1;
                i = g_frameRateScaling * 2;
                if (i > 14) {
                    i = 14;
                }
                g_velocity = 5400;
                g_altitude -= (g_altitude - g_groundAltitude) / i;
                if (g_altitude < (unsigned)(g_groundAltitude + 5)) {
                    g_altitude = g_groundAltitude + 5;
                }
                g_ViewX -= (g_ViewX - ((int32)g_planeTable.planes[g_closestThreatIndex].mapX << 5)) / (int32)i;
                g_ViewY -= (g_ViewY - ((int32)(0x8000 - g_planeTable.planes[g_closestThreatIndex].mapY) << 5)) / (int32)i;
            }
        }
    } else {
        g_landingDoneFlag = 0;
        g_inLandingCorridor = 0;
    }

skip_autopilot:
    if (g_inLandingCorridor == 0) {
        if (g_viewZ == 0) {
            if ((gameData->unk4 != 0 || g_gunHits > 4 || g_fuelRemaining == 0) &&
                g_ejectState == 0 && g_knots > 50) {
                makeSound(0, 2);
                setDrawColor(COLOR_BLACK);
                fillRectBoth(0, 0, 319, 199);
                waitFrameSync(120);
                finalizeMission(1);
            }
        } else {
            g_landingTimer = 1;
        }
    }

    if (g_savedPosVisible != 0 && (g_viewMode & 0x80) == 0) {
        if (gameData->unk4 != 0 && g_altitude != 0) {
            makeSound(0, 2);
            gfx_waitRetrace();
            waitFrameSync(120);
            finalizeMission(2);
        } else {
            g_altitude += 500;
            g_autopilotAltitude = 0;
        }
    }

    g_targetLeadAngle = (g_planeTable.planes[g_closestThreatIndex].flags & 0x200 && g_nearestThreatRange < 0x500) ? (((g_northSouthSign << 8) / g_frameRateScaling) + g_targetLeadAngle) & 0xfff : 0;
}

/* W: the global mission clock. frameTick/missionTick advance once per sim
 * step regardless of player count; the periodic enemy-alert scan is world
 * state too. */
void frameWorldTick(void) {
    int16 i;

    frameTick++;
    if (frameTick % g_frameRateScaling == 0) {
        g_missionTick++;
        if ((g_missionTick & 0x1f) == 0) {
            appendMapEvent(9, 0);
        }
        if (g_missionTick == 1) {
            playVoiceCue(0);
            updateEngineSound();
        }
        if (g_autopilotEngaged != 0 && (g_missionTick & 3) == 0) {
            generateRandomRadioMessage();
        }
    }

    if (++g_frameRateAccum >= g_frameRateScaling * 4) {
        /* Render/sim decoupled: the adaptive frame governor (which rescaled
         * g_frameRateScaling and added sleep ticks to throttle the loop) is
         * retired — the sim now steps fixed-rate in gameMainLoop. Keep the
         * jiffies debug readout and the periodic enemy-alert scan. */
        g_jiffiesPerFrame = g_frameTimingAccum / g_frameRateScaling;
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
                (g_simObjects[i].flags.b[0] & 2) != 0) {
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
        if (bulletTracks[i].posX != 0) {
            bulletTracks[i].posX = (bulletTracks[i].posX + bulletTracks[i].velX) & BULLET_FINE_MASK;
            bulletTracks[i].posY = (bulletTracks[i].posY + bulletTracks[i].velY) & BULLET_FINE_MASK;
            bulletTracks[i].alt += bulletTracks[i].velZ;
        }
    }
}

/* P: player gun trigger + tracer spawn; reads the ctx's axis/button state. */
void tryPlayerFire(void) {
    int16 firing, mag, yaw, pitch, slot;

    if (!(frameTick & 1)) {
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
        if ((int16)(frameTick - g_bulletFreshTick) != 0)
            g_bulletFreshMask = 0; /* different tick (modular compare) */
        g_bulletFreshTick = frameTick;
        for (slot = 0; slot < g_bulletTrackCount; slot++)
            if (bulletTracks[slot].posX == 0)
                break;
        if (slot >= g_bulletTrackCount) {
            tries = g_bulletTrackCount;
            do {
                slot = (int16)(g_bulletPoolCursor++ % (uint32)g_bulletTrackCount);
            } while ((g_bulletFreshMask & (1u << slot)) && --tries > 0);
        }
        g_bulletFreshMask |= 1u << slot;
    } else {
        slot = (frameTick >> 1) % g_bulletTrackCount;
        if (slot < 0)
            slot += g_bulletTrackCount;
    }
    g_gunAmmo = clampRange(g_gunAmmo - 40 / g_frameRateScaling, 0, 1000);
    makeSound(4, 2);
    /* Round leaves the barrel along the airframe axis plus the M61's dispersion
     * cone; magnitude in fine units per step (the original 186 coarse/s). */
    yaw = (int16)g_ourHead + gunSpreadAngle();
    pitch = (int16)g_ourPitch + gunSpreadAngle();
    mag = (186 << 5) / g_frameRateScaling;
    bulletTracks[slot].velZ = sinMul(pitch, mag);
    mag = cosMul(pitch, mag);
    bulletTracks[slot].velX = sinMul(yaw, mag);
    bulletTracks[slot].velY = -cosMul(yaw, mag);
    bulletTracks[slot].posX = (g_ViewX + bulletTracks[slot].velX) & BULLET_FINE_MASK;
    bulletTracks[slot].posY = (0x100000L - g_ViewY + bulletTracks[slot].velY) & BULLET_FINE_MASK;
    bulletTracks[slot].alt = bulletTracks[slot].velZ + g_viewZ - 2;
    bulletTracks[slot].targetPlayer = g_residentPlayer;
    g_gunFiredFlag = 1;
    goto done_fire;
no_fire:
    /* legacy release-sweep, SP only: on the server an airborne round keeps
     * flying (it dies on a hit or when the shared pool overwrites it). The
     * rotating slot is the original frame-derived index; a round lives
     * ~2*g_bulletTrackCount ticks before the rotation overwrites it. */
    if (g_residentPlayer < 0) {
        slot = (frameTick >> 1) % g_bulletTrackCount;
        if (slot < 0)
            slot += g_bulletTrackCount;
        bulletTracks[slot].posX = 0;
    }
    g_gunFiredFlag = 0;
done_fire:
    if (firing) {
        strcpy(strBuf, "GUN:");
        strcat(strBuf, itoa(g_gunAmmo, g_itoaScratch, 10));
        setTimedMessage(strBuf);
    }
}
