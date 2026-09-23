/* egtarget.c — target lock + HUD overlay (reads g_viewZ as uint16). */
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

/* Private helpers for this translation unit. The remote-player identity
 * resolvers (simObjectViewModel/simObjectTypeName/simObjectFineY), the
 * sim-step combat halves (simTargetLock/simBulletHits) and the hit-box +
 * deterministic-spark machinery moved to egtarget_net.c. */

void drawTargetBox(int16, int16, int16, int16);
void drawTargetBoxF(float centerX, float centerY, int size, int mode);
void drawMissileLock(void);
void drawTargetLabel(const char *, int16, int16);
void buildRangeString(int16 rangeRaw);
void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
void projectWorldToHudFine(int32 fineX, int32 fineY, int fineZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

/* simTargetLock (the acquisition half) lives in egtarget_net.c: it runs in
 * the sim step so locks are authoritative; updateTargetLock() below keeps
 * only the drawing half. */

void updateTargetLock(void) {
    int16 marker, idx, depthShift;
    int16 lodM, planeModelDepth, planeFineDepth;

    /* Fire at g_viewMode == 0x8b (sidewinder lock) */
    if (g_viewMode == VIEW_TARGET) {
        drawWorldObject(6, (int32)g_ViewX, 0x01000000L - g_ViewY,
                        g_viewZ + 0x10, g_ourHead, g_ourPitch, g_ourRoll, 2);
    }

    /* Missile/chaff loop (8 entries, stride 8) */
    for (idx = 0; idx < 8; idx++) {
        if (g_particles[idx].posX != 0) {
            projectWorldToHud(g_particles[idx].posX,
                              g_particles[idx].posY,
                              g_particles[idx].alt);
            if (g_projDepth < 0 && g_projDepth > -0x100) {
                drawWorldObject(
                    (uint8)(((uint8)g_smokeParticleSlot - (uint8)idx) & 7) < 4 ? 3 : 17,
                    (int32)(uint16)g_particles[idx].posX << 5,
                    (int32)(uint16)g_particles[idx].posY << 5,
                    g_particles[idx].alt, 0,
                    g_particles[idx].spin, 0, 0);
            }
        }
    }

    /* Air-to-ground targeting */

    /* depthShift is the original distance/altitude "spottability" zoom: with a far
       target (or high altitude) it right-shifts world objects' positions in
       drawWorldObject to draw them as if closer — bigger — without scaling their
       geometry, so distant aircraft/scenery balloon and then shrink to true size as
       the range closes. Detail level 4 ("no LOD tricks") disables it, so world
       objects hold true perspective size at any range (far ones resolve to the
       spottable single-pixel dot via the model-depth gate instead of ballooning). */
    if (g_detailLevel >= 4) {
        depthShift = 0;
    } else {
        depthShift = (g_hudVisible != 0 && (uint16)(g_nearestThreatRange + g_viewZ) > 1500) ? 1 : 0;
        if (g_hudVisible != 0 && (uint16)(g_nearestThreatRange + g_viewZ) > 4000) {
            depthShift = 2;
        }
    }

    /* Detail level 4 widens the depth band over which an air contact resolves
       from a single pixel (the drawViewportLine fallback below) to a full model,
       reaching -0x100 — the engine's established far model range (cf. the
       particle/wreck loops, which draw models out to -0x100). Levels 0-3 keep the
       original -0x20 gate. */
    lodM = (g_detailLevel >= 4) ? 8 : 1;
    planeModelDepth = -0x20 * lodM; /* dot -> model gate: -0x20 (normal) / -0x100 (detail 4) */
    planeFineDepth = -0x10 * lodM;  /* coarse -> fine model: -0x10 (normal) / -0x80 (detail 4) */

    /* Air contacts + threat aircraft (target acquisition itself moved to
     * simTargetLock in the sim update; this loop only draws). */
    for (idx = 0; idx < g_groundUnitCount; idx++) {
        if (!(g_simObjects[idx].flags.b[0] & 2))
            goto next2;

        if (computeSimObjectRange(idx) >= 4800 && g_directorMode == 0)
            goto next2;

        projectWorldToHud(g_simObjects[idx].posX, g_simObjects[idx].posY, g_simObjects[idx].alt);

        if (g_projDepth >= 0)
            goto next2;

        g_projDepth >>= depthShift;

        {
            int16 vmdl = simObjectViewModel(idx, g_projDepth > planeFineDepth);

        if (g_projDepth > planeModelDepth) {
            if (g_simObjects[idx].alt < 999 && g_nightMode == 0) {
                marker = 0;
                if ((g_planeTable.planes[g_closestThreatIndex].flags & 0x200) &&
                    abs(g_simObjects[idx].posX - g_planeTable.planes[g_closestThreatIndex].mapX) < g_attackRangeX >> 5 &&
                    abs(g_simObjects[idx].posY - g_planeTable.planes[g_closestThreatIndex].mapY) < g_attackRangeY >> 5) {
                    marker = 0x80;
                }
                if (g_viewZ != 0x80 || marker == 0x80) {
                    drawAircraftShadow(vmdl,
                                    g_simObjects[idx].worldX, g_simObjects[idx].worldY,
                                    marker, g_simObjects[idx].heading.w,
                                    g_simObjects[idx].pitch, g_simObjects[idx].bank.w,
                                    -(signOf(depthShift) - 2));
                }
            }

            /* Draw the target */
            drawWorldObject(vmdl,
                g_simObjects[idx].worldX, g_simObjects[idx].worldY, g_simObjects[idx].alt,
                g_simObjects[idx].heading.w, g_simObjects[idx].pitch,
                g_simObjects[idx].bank.w, 2 - depthShift);
        } else {
            setDrawColor(COLOR_WHITE);;
            drawViewportLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
        }
        }
    next2:;
    }

    /* SAM/missile visual loop (12 entries, stride 0x18) */
    for (idx = 0; idx < 12; idx++) {
        if (g_projectiles[idx].ttl != 0) {
            projectWorldToHud(g_projectiles[idx].mapX, g_projectiles[idx].mapY, g_projectiles[idx].alt);

            if (vtxScratch.vproj.x.lo == -1)
                goto next3;

            if (g_projDepth > -0x20) {
                drawWorldObject(sams[g_projectiles[idx].specIdx].modelId,
                                g_projInterpX[idx],
                                g_projInterpY[idx],
                                g_projectiles[idx].alt,
                                g_projectiles[idx].worldX, g_projectiles[idx].worldY,
                                g_projectiles[idx].worldZ + 0x2000,
                                ((g_viewMode & 0x80) && g_viewMode != 0x8b) ? 3 : 1);
            } else {
                setDrawColor(idx < 8 ? COLOR_LIGHTRED : COLOR_FLAMING);
                drawViewportLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
            }
        }
    next3:;
    }

    /* Runway/base visual */
    if (g_wreckAlt > 0) {
        projectWorldToHud(g_wreckX, g_wreckY, g_wreckAlt);
        if (g_projDepth < 0 && g_projDepth > -0x100) {
            drawWorldObject(14,
                            (int32)(uint16)g_wreckX << 5,
                            (int32)(uint16)g_wreckY << 5,
                            g_wreckAlt, 0, 0, 0,
                            g_wreckFallVel > 0 ? 4 : 3);
        }
    }

    /* Player's own aircraft fire */
    if (!(g_viewMode & 0x80)) goto done;
    if (g_viewMode == VIEW_TARGET) goto done;
    if (g_viewZ == 0 && g_ejectState != 0) goto done;

    drawWorldObject(((g_playerPlaneFlags & 1) == 0) + 6, (int32)g_ViewX,
                    0x01000000L - g_ViewY, g_viewZ + 0x10, g_ourHead, g_ourPitch, g_ourRoll,
                    2 - depthShift);

    if ((uint16)g_viewZ < 1000 && g_nightMode == 0) {
        drawAircraftShadow(((g_playerPlaneFlags & 1) == 0) + 6,
                        (int32)g_ViewX, 0x01000000L - g_ViewY,
                        g_groundAltitude, g_ourHead, g_ourPitch, g_ourRoll, 2);
    }

done:;
}

/* The deterministic cosmetic RNG, model-derived hit boxes
 * (computeHitRadii + radius helpers), the sim-step hit tests
 * (simBulletHits) and the world-space tracer/spark drawing
 * (drawWorldEffects) moved to egtarget_net.c. */

void drawHudWorldOverlay(void) {
    int p, lockFlag, r, wpEntry, tmp, t, missileSpecD, loftDist, e, missileSpec, marker, idx, g, radius, objIdx, pointY, pointX, dist, wpIdx, prevX, compat, prevY;

    g_prevKillMarker = g_targetInHudFlag;
    g_targetInHudFlag = 0;

    for (idx = 0; idx < 12; idx++) {
        if (g_projectiles[idx].ttl != 0) {
            projectWorldToHudFine(g_projInterpX[idx], g_projInterpY[idx], g_projectiles[idx].alt);
            if (vtxScratch.vproj.x.lo != -1) {
                setDrawColor(idx < 8 ? 0x0e : 0x0a);
                drawTargetBoxF(g_hudProjXf, g_hudProjYf, 6, 0);
            }
        }
    }

    if (g_hudVisible == 0) return;

    if (g_unusedHudFlag != 0) {
        g_unusedHudFlag = 0;
    }

    loadColorPalette(g_nightMode != 0 ? 2 : g_nightMode);
    setDrawColor(COLOR_WHITE);;
    drawFullscreenLine(319, 199, 319, 199);
    g_lockToneFlag = 0;

    if (g_currentWeaponType == 2) {
        if (g_viewMode == VIEW_COCKPIT) {
            if (g_groundTargetLock >= 0) {

                projectWorldToHudFine((int32)g_planeTable.planes[g_groundTargetLock].mapX << 5,
                                      (int32)g_planeTable.planes[g_groundTargetLock].mapY << 5, 0);

                missileSpec = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

                if (missileSpec == 28 && computeMapTargetRange(g_groundTargetLock) < (g_viewZ >> 5) * 5 && g_projDepth < 0) {
                    g_lockToneFlag = 1;
                }

                if (vtxScratch.vproj.x.lo != -1) {

                    setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                    lockFlag = 0;

                    compat = missileTargetCompat(missleSpec[missileSpecIndex].weaponIdx, g_groundTargetLock) != 0 ? 4 : 0;

                    if (compat != 0 && (missileSpec != 4 || g_planeTable.planes[g_groundTargetLock].active != 0)) {
                        if (missleSpec[missileSpecIndex].ammo != 0) {
                            setDrawColor(COLOR_WHITE);;
                            if ((rangeApprox(vtxScratch.vproj.x.lo - 160, vtxScratch.vproj.y.lo - 56) < 48 || g_lockToneFlag != 0) &&
                                -g_projDepth / 7 < sams[missileSpec].lockRange &&
                                sams[missileSpec].weaponClass != 7) {
                                if (sams[missileSpec].weaponClass != 28 || g_lockToneFlag != 0) {
                                    g_lockToneFlag = 1;
                                    lockFlag = 1;
                                    if (sams[missileSpec].lockRange > (-g_projDepth >> 1 >> 1)) {
                                        setDrawColor(COLOR_LIGHTRED);
                                    }
                                }
                            } else {
                                g_lockToneFlag = 0;
                            }
                        }
                    } else {
                        if (missileSpec != -1) {
                            setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                        }
                        g_lockToneFlag = 0;
                    }

                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, compat != 0 ? compat + 5 : 9, lockFlag);
                }
            }
        }
    }

    if (g_scopeSweepTimer > 0 && g_threatLabelTarget >= 0) {
        projectWorldToHudFine((int32)g_planeTable.planes[g_threatLabelTarget].mapX << 5,
                              (int32)g_planeTable.planes[g_threatLabelTarget].mapY << 5, 0);
        drawTargetLabel(g_targetNameTable[((int16 *)&g_planeTable)[g_threatLabelTarget * 8]], g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }

    g_playerPlaneFlags &= ~0x200;
    g_pageFront[1] = 4;

    if (g_activePanelMode == 0x13) {
        if (g_currentWeaponType == 2 || g_currentWeaponType == 0) {
            if (g_groundTargetLock != -1) {

                wpIdx = g_groundTargetLock & 0x7f;

                /* Ground targets only carry coarse map coords; scale to the fine
                 * (mapX<<5) space drawTargetView now differences against g_ViewX. */
                drawTargetView(getTargetSymbol(wpIdx),
                               (int32)g_planeTable.planes[wpIdx].mapX << 5,
                               (int32)g_planeTable.planes[wpIdx].mapY << 5,
                               0, 0, 0, 0, 1, -1);
                drawMissileLock();
                buildRangeString(computeMapTargetRange(wpIdx));
                drawStringActivePage(strBuf, 244, 170, 0x0f);

                strcpy(strBuf, g_targetNameTable[g_planeTable.planes[wpIdx].nameIndex & 0x7f]);
                drawStringActivePage(strBuf, -((int16)strlen(strBuf) * 2 - 268), 130, 0x0f);

                if ((int16)strlen(g_targetNameTable[((int16 *)&g_planeTable)[wpIdx * 8]]) != 0) {
                    strcpy(strBuf,
                           strlen(g_targetNameTable[g_planeTable.planes[wpIdx].nameIndex & 0x7f]) != 0 ? " at " : "");
                    strcat(strBuf, g_targetNameTable[((int16 *)&g_planeTable)[wpIdx * 8]]);
                    drawStringActivePage(strBuf, -((int16)strlen(strBuf) * 2 - 268), 136, 0x0f);
                }

                if (g_currentWeaponType == 0) {
                    projectWorldToHudFine((int32)g_planeTable.planes[g_groundTargetLock].mapX << 5,
                                          (int32)g_planeTable.planes[g_groundTargetLock].mapY << 5, 0);
                    setDrawColor(COLOR_WHITE);;
                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, 8, 0);
                } else if (g_targetSlots[0].planeIndex == g_groundTargetLock) {
                    drawStringActivePage(egPrimaryTarget, 0xec, 0x8e, 0x0f);
                } else if (g_targetSlots[1].planeIndex == g_groundTargetLock) {
                    drawStringActivePage("Secondary Target", 236, 142, 0x0f);
                } else if (!(frameTick & 1) &&
                           ((g_difficultyTier < 2 && (g_shapeTargetCategory[g_planeTable.planes[wpIdx].nameIndex & 0x7f] & 0xc0) != 0) ||
                            (g_planeTable.planes[wpIdx].flags & 0x500) != 0 ||
                            (g_mapCellFlags[((uint16)g_planeTable.planes[wpIdx].mapX >> 11) +
                                            ((uint16)g_planeTable.planes[wpIdx].mapY >> 11) * 16] &
                             1) != 0)) {
                    drawStringActivePage("No Target", 252, 142, 0x0f);
                }
            }
        }
    }

    g_axisInput1 = readAxisInput(1);

    if (g_currentWeaponType == 1) {
        if (g_viewMode == VIEW_COCKPIT) {
            if (!(g_airTargetLock & 0x80)) {

                projectWorldToHudFine(g_simObjects[g_airTargetLock].worldX,
                                      simObjectFineY(g_airTargetLock),
                                      g_simObjects[g_airTargetLock].alt);

                if (vtxScratch.vproj.x.lo != -1) {

                    setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                    lockFlag = 0;

                    missileSpec = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

                    if (missleSpec[missileSpecIndex].ammo != 0 && sams[missileSpec].weaponClass == 7) {
                        setDrawColor(COLOR_WHITE);;
                        if (rangeApprox(vtxScratch.vproj.x.lo - 160, vtxScratch.vproj.y.lo - 56) < 48) {
                            if (-g_projDepth >> 3 < sams[missileSpec].lockRange) {
                                g_lockToneFlag = 1;
                                lockFlag = 1;
                                if (-g_projDepth >> 1 >> 1 < sams[missileSpec].lockRange) {
                                    setDrawColor(COLOR_LIGHTRED);
                                }
                            }
                        }
                    }
                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, 9, lockFlag);
                }
            }
        }
    }

    if (g_activePanelMode == 0x13 && g_currentWeaponType == 1 && g_airTargetLock != -1) {
        wpIdx = g_airTargetLock & 0x7f;

        /* Fine (integrated) world position, not the coarse posX/posY seed, so the
         * tracked model doesn't jitter on the ÷32 grid as the view interpolates.
         * Same identity resolver as the world loop: a remote pilot previews as
         * the F-15, not aircraftTypes[0] (MiG-23). */
        drawTargetView(simObjectViewModel(wpIdx, 1),
                       g_simObjects[wpIdx].worldX,
                       /* drawTargetView differences against the MAP-fine
                        * viewer Y (0x100000-g_ViewY): feed the object's
                        * map-fine Y, not render-space worldY for remotes */
                       simObjectFineY(wpIdx),
                       g_simObjects[wpIdx].alt,
                       g_simObjects[wpIdx].heading.w,
                       g_simObjects[wpIdx].pitch,
                       g_simObjects[wpIdx].bank.w,
                       1, 1);
        drawMissileLock();
        buildRangeString(rangeApprox(g_viewX_ - g_simObjects[wpIdx].posX,
                                     g_viewY_ - g_simObjects[wpIdx].posY));
        drawStringActivePage(strBuf, 244, 170, 0x0f);

        idx = g_simObjects[wpIdx].spec;
        strcpy(strBuf, simObjectTypeName(wpIdx));
        if (!(g_simObjects[wpIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER))
            strcat(strBuf, aircraftTypes[idx].altName);
        drawStringActivePage(strBuf, 248, 134, 0x0f);

        if (aircraftTypes[idx].modelId == -1 &&
            !(g_simObjects[wpIdx].flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) &&
            !(frameTick & 1)) {
            drawStringActivePage("No Target", 252, 140, 0x0f);
        }

        if (g_detailLevel != 0 && (frameTick & 1)) {
            g_aamLeadDist = (int16)(((uint32)(uint16)(0x8000 - g_simObjects[wpIdx].pitch) *
                                     (int32)g_simObjects[wpIdx].speed) >>
                                    15);
            g_aamLeadDist -= abs(sinMul(g_simObjects[wpIdx].bank.w, g_aamLeadDist)) >> 1;
        }
    }

    g_pageFront[1] = 2;

    if (g_scopeSweepTimer > 0 && g_threatLabelTarget < 0) {
        idx = -1 - g_threatLabelTarget;
        projectWorldToHudFine(g_simObjects[idx].worldX,
                              simObjectFineY(idx),
                              g_simObjects[idx].alt);
        drawTargetLabel(simObjectTypeName(idx),
                        g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }

    if (g_currentWeaponType == 2 && g_viewMode == VIEW_COCKPIT) {
        missileSpecD = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

        if (missileSpecD == 30 && abs((int16)g_ourRoll) < 0x2000) {
            tmp = computeLoftAngle();
            loftDist = cosMul(tmp, g_altitude) / (sinMul(-tmp, 0x20) + 1);
            pointX = sinMul(g_ourHead, loftDist) + g_viewX_;
            pointY = g_viewY_ - cosMul(g_ourHead, loftDist);
            projectWorldToHud(pointX, pointY, 0);
            if (vtxScratch.vproj.x.lo == -1) {
                vtxScratch.vproj.x.lo = (sinMul(g_ourRoll, 96 - g_flightPathMarkerY) << 2) / 3 + 160;
                vtxScratch.vproj.y.lo = 96;
            } else {
                setDrawColor(COLOR_LIGHTRED);
                drawTargetBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, 5, 1);
            }
            setDrawColor(COLOR_WHITE);;
            drawHudViewLine(160, g_flightPathMarkerY, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
        }

        if ((missileSpecD == 30 || missileSpecD == 29) && g_groundTargetLock >= 0) {
            projectWorldToHud(g_planeTable.planes[g_groundTargetLock].mapX + sinMul(g_ourHead, 0x80),
                              g_planeTable.planes[g_groundTargetLock].mapY - cosMul(g_ourHead, 0x80),
                              g_viewZ);

            if (vtxScratch.vproj.x.lo != -1) {
                if (missileSpecD == 30) {
                    g_projDepth = clampRange(
                        rangeApprox(pointX - g_planeTable.planes[g_groundTargetLock].mapX,
                                    pointY - g_planeTable.planes[g_groundTargetLock].mapY) >>
                            3,
                        0x0000, 0x0040);
                } else {
                    g_projDepth = clampRange(computeMapTargetRange(g_groundTargetLock) >> 3, 0x0000, 0x0040);
                }
                setDrawColor(COLOR_LIGHTRED);
                drawViewportLine(159 - g_projDepth, 33, 159 - g_projDepth, 30);
                drawViewportLine(g_projDepth + 160, 33, g_projDepth + 160, 30);
                drawViewportLine(159 - g_projDepth, 30, g_projDepth + 160, 30);
                setDrawColor(COLOR_WHITE);;
                drawHudViewLine(vtxScratch.vproj.x.lo - 4, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo - 4);
                drawHudViewLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo - 4, vtxScratch.vproj.x.lo + 4, vtxScratch.vproj.y.lo);
                drawHudViewLine(vtxScratch.vproj.x.lo + 4, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo + 4);
                drawHudViewLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo + 4, vtxScratch.vproj.x.lo - 4, vtxScratch.vproj.y.lo);
            }
        }
    }

    if (g_hitEffectTimer != 0 && g_activePanelMode == 0x13 && g_lockedTargetKilled != 0 && g_targetInHudFlag != 0) {
        blitSprite(252, 140, (abs(g_hitEffectTimer) - 8) * -32, 0x3f, 32, 32, 0);
    }

    if (g_activePanelMode == 0x13 && g_prevKillMarker != 0 && g_targetInHudFlag == 0) {
        fillPanelBox(3, 3);
    }
}

/* ---- merged from egwaypt.c ---- */
void drawTargetBox(int16 centerX, int16 centerY, int16 size, int16 mode) {
    int16 halfHeight, left, top, right, bottom;

    if (g_hudVisible == 0) {
        return;
    }
    if (g_halfScaleRender != 0) {
        size >>= 1;
    }
    halfHeight = size - (size >> 2);
    right = centerX + size;
    left = centerX - size;
    bottom = centerY + halfHeight;
    top = centerY - halfHeight;
    if (mode == 0) {
        drawHudViewLine(left, top, left, bottom);
        drawHudViewLine(left, bottom, right, bottom);
        drawHudViewLine(right, bottom, right, top);
        drawHudViewLine(right, top, left, top);
    } else {
        drawHudViewLine(centerX, top, right, centerY - (halfHeight >> 1));
        drawHudViewLine(right, centerY - (halfHeight >> 1), right, centerY + (halfHeight >> 1));
        drawHudViewLine(right, (halfHeight >> 1) + centerY, centerX, bottom);
        drawHudViewLine(centerX, bottom, left, (halfHeight >> 1) + centerY);
        drawHudViewLine(left, centerY + (halfHeight >> 1), left, centerY - (halfHeight >> 1));
        drawHudViewLine(left, centerY - (halfHeight >> 1), centerX, top);
    }
}

/* Sub-pixel target box for the GL native-res overlay: identical geometry to
 * drawTargetBox but the corners are offset from the projector's fractional 320-space
 * centre (g_hudProjXf/Yf), so the box glides instead of snapping to the 320x200 grid
 * when the object is close. Box dimensions stay whole-pixel — only the centre is
 * fractional. Without a vector overlay (software backend) it falls back to the
 * integer drawTargetBox on the exact projected pixel, the faithful 320x200 raster. */
void drawTargetBoxF(float centerX, float centerY, int size, int mode) {
    int halfHeight, sz;
    float left, top, right, bottom, hq;

    if (g_hudVisible == 0) {
        return;
    }
    if (!r2d_vectorActive()) {
        drawTargetBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, size, mode);
        return;
    }
    sz = size;
    if (g_halfScaleRender != 0) {
        sz >>= 1;
    }
    halfHeight = sz - (sz >> 2);
    right = centerX + sz;
    left = centerX - sz;
    bottom = centerY + halfHeight;
    top = centerY - halfHeight;
    if (mode == 0) {
        drawHudViewLineF(left, top, left, bottom);
        drawHudViewLineF(left, bottom, right, bottom);
        drawHudViewLineF(right, bottom, right, top);
        drawHudViewLineF(right, top, left, top);
    } else {
        hq = (float)(halfHeight >> 1);
        drawHudViewLineF(centerX, top, right, centerY - hq);
        drawHudViewLineF(right, centerY - hq, right, centerY + hq);
        drawHudViewLineF(right, centerY + hq, centerX, bottom);
        drawHudViewLineF(centerX, bottom, left, centerY + hq);
        drawHudViewLineF(left, centerY + hq, left, centerY - hq);
        drawHudViewLineF(left, centerY - hq, centerX, top);
    }
}

// ==== seg000:0xC2F8 ====
void drawMissileLock(void) {
    int16 markX, markY;
    if (g_lockToneFlag != 0 && g_hudVisible != 0) {
        drawStringActivePage("Missile Lock", 244, 150, 14);
        setDrawColor(COLOR_YELLOW);
        markX = 268;
        markY = 156;
        drawFullscreenLine(258, 156, 278, 156);
        drawFullscreenLine(markX, markY - 8, markX, markY + 8);
    }
}

// ==== seg000:0xc371 ====
void drawTargetLabel(const char *text, int16 color, int16 size) {
    if (vtxScratch.vproj.x.lo == -1) {
        return;
    }
    setDrawColor(color);
    if (size < vtxScratch.vproj.x.lo && 319 - size > vtxScratch.vproj.x.lo &&
        size < vtxScratch.vproj.y.lo && 88 - size > vtxScratch.vproj.y.lo) {
        drawTargetBoxF(g_hudProjXf, g_hudProjYf, size, 1);
    }
    if (vtxScratch.vproj.x.lo > 20 && vtxScratch.vproj.x.lo < 280 &&
        vtxScratch.vproj.y.lo > 0 && vtxScratch.vproj.y.lo < 82) {
        drawStringActivePage(text, vtxScratch.vproj.x.lo - (int16)strlen(text) * 2, vtxScratch.vproj.y.lo + 5, g_scopeArcColor);
    }
}

// ==== seg000:0xc40b ====
void buildRangeString(int16 rangeRaw) {
    int16 p, a, b, c, d;

    strcpy(strBuf, "Range ");
    strcat(strBuf, itoa(rangeRaw >> 6, g_itoaScratch, 10));
    strcat(strBuf, ".");
    strcat(strBuf, itoa((rangeRaw & 0x3f) * 2 / 13, g_itoaScratch, 10));
    strcat(strBuf, " km");
}
