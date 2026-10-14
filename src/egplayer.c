/*
 * egplayer.c - PlayerSim context swap + input seam (plan §6).
 *
 * The swap table below lists every player-scoped egame global. Adding a field
 * means adding one PS_FIELD/PS_ARRAY line - save/load/hash all follow it, so
 * the three can't drift apart.
 */
#include <stddef.h>
#include <string.h>

#include "comm.h"
#include "egdata.h"
#include "egcode.h"
#include "eginput.h"
#include "input.h"
#include "slot.h"
#include "egplayer.h"
#include "egtypes.h"
#include "inttype.h"
#include "struct.h"

/* The PlayerSim ctx snapshot machinery below is the F15_NET server path: the
 * ctx is the raw wire format while the globals it snapshots are typed reps,
 * so this section builds only under the network target. The input seam and
 * simEvents sinks below stay unconditional — single-player callers route
 * through the same ops table (defaulting to kLocalInput). */
#ifdef F15_NET

#include "math/legacy_altitude.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_rotation.hpp"
#define F15_MATH_BOUNDARY_ACCESS
#include "math/boundary.hpp"
#include "math/control_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS

/* ctx wire <-> typed-global conversion. PlayerSim keeps the DOS word layout
 * (the snapshot codec and playerCtxHash read it verbatim), so each typed
 * global serializes through its boundary rep at the swap edge. Raw fields
 * pass through the generic templates; typed quantities pick an overload on
 * the global's type. */
namespace psx {
using namespace f15::math;
using namespace f15::math::legacy;
using GB = GameBackend;
using Bound = Boundary<GB>;
using Ctrl = ControlBoundary<GB>;

template <class D, class S>
static inline void store(D &d, const S &s) { d = static_cast<D>(s); }
static inline void store(int32 &d, ViewCoordinate<GB, ViewXAxis> s) { d = fineRep(s); }
static inline void store(int32 &d, ViewCoordinate<GB, ViewYAxis> s) { d = fineRep(s); }
static inline void store(int16 &d, Angle<GB> s) { d = s.signedWord(); }
static inline void store(int16 &d, TickDuration s) { d = s.word(); }
static inline void store(int &d, TickDuration s) { d = s.word(); }
static inline void store(int16 &d, Ticks s) { d = s.word(); }
static inline void store(unsigned &d, FlightAltitude<GB> s) { d = altitudeUnits(s); }
static inline void store(int &d, FlightSpeed<GB> s) { d = (int)speedUnits(s); }
static inline void store(int16 &d, CornerSpeed<GB> s) { d = cornerKnots(s); }
static inline void store(int16 &d, StallSpeed<GB> s) { d = Airspeeds::stall(s); }
static inline void store(int16 &d, EngineThrust<GB> s) { d = thrustUnits(s); }
static inline void store(int16 &d, ClimbRate<GB> s) { d = climbUnits(s); }
static inline void store(int &d, FlightLoad<GB> s) { d = (int)loadSixteenths(s); }
static inline void store(int16 &d, FuelLoad<GB> s) { d = fuelUnits(s); }
static inline void store(int16 &d, TerrainHeight<GB> s) { d = terrainUnits(s); }
static inline void store(int16 &d, RenderHeight<GB> s) { d = Altitudes::render(s); }
static inline void store(int &d, RollCommand<GB> s) { d = Ctrl::roll(s); }
static inline void store(int16 &d, PitchCommand<GB> s) { d = Ctrl::pitch(s); }
static inline void store(int16 (&d)[9], const Matrix3<GB> &s) { Bound::matrixWords(s, d); }

template <class D, class S>
static inline void load(D &d, const S &s) { d = static_cast<D>(s); }
static inline void load(ViewCoordinate<GB, ViewXAxis> &d, int32 s) { d = viewX(s); }
static inline void load(ViewCoordinate<GB, ViewYAxis> &d, int32 s) { d = viewY(s); }
static inline void load(Angle<GB> &d, int16 s) { d = angleFromWord(s); }
static inline void load(TickDuration &d, int16 s) { d = TickDuration::fromWord(s); }
static inline void load(TickDuration &d, int s) { d = TickDuration::fromWord((int16)s); }
static inline void load(Ticks &d, int16 s) { d = Ticks::fromWord(s); }
static inline void load(FlightAltitude<GB> &d, unsigned s) { d = altitudeFromUnits(s); }
static inline void load(FlightSpeed<GB> &d, int s) { d = speedFromUnits(s); }
static inline void load(CornerSpeed<GB> &d, int16 s) { d = Airspeeds::corner(s); }
static inline void load(StallSpeed<GB> &d, int16 s) { d = stallFromUnits(s); }
static inline void load(EngineThrust<GB> &d, int16 s) { d = thrustFromUnits(s); }
static inline void load(ClimbRate<GB> &d, int16 s) { d = climbFromUnits(s); }
static inline void load(FlightLoad<GB> &d, int s) { d = loadFromSixteenths(s); }
static inline void load(FuelLoad<GB> &d, int16 s) { d = fuelFromUnits(s); }
static inline void load(TerrainHeight<GB> &d, int16 s) { d = terrainFromUnits(s); }
static inline void load(RenderHeight<GB> &d, int16 s) { d = renderHeightFromUnits(s); }
static inline void load(RollCommand<GB> &d, int s) { d = Ctrl::roll(s); }
static inline void load(PitchCommand<GB> &d, int16 s) { d = Ctrl::pitch(s); }
static inline void load(Matrix3<GB> &d, const int16 (&s)[9]) { d = Bound::matrixWords(s); }
} /* namespace psx */

/* Scalar: X(ctxField, globalSymbol); POD array: XA(field, global);
 * 3x3 matrix: XM(field, Matrix3 global); map-position pair: XP(fx, fy,
 * MapPosition global). */
#define PLAYER_FIELDS(X, XA, XM, XP)                  \
    X(ViewX, g_ViewX)                                 \
    X(ViewY, g_ViewY)                                 \
    X(viewZ, g_viewZ)                                 \
    X(viewX_, g_viewX_)                               \
    X(viewY_, g_viewY_)                               \
    X(ourHead, g_ourHead)                             \
    X(ourPitch, g_ourPitch)                           \
    X(ourRoll, g_ourRoll)                             \
    X(viewRoll, g_viewRoll)                           \
    XM(orientMatrix, g_orientMatrix)                  \
    XM(matrixScratch, g_matrixScratch)                \
    X(orientationDirty, g_orientationDirty)           \
    X(rollWasNonzero, g_rollWasNonzero)               \
    X(rotationCounter, g_rotationCounter)             \
    X(altitude, g_altitude)                           \
    X(velocity, g_velocity)                           \
    X(knots, g_knots)                                 \
    X(thrust, g_thrust)                               \
    X(setThrust, g_setThrust)                         \
    X(climbRate, g_climbRate)                         \
    X(liftForce, g_liftForce)                         \
    X(gees, g_gees)                                   \
    XA(geeStringBuf, g_geeStringBuf)                  \
    XA(highGeeFlag, g_highGeeFlag)                    \
    X(rollInput, g_rollInput)                         \
    X(pitchInput, g_pitchInput)                       \
    X(keyScancode, keyScancode)                       \
    XA(axisInputAccum, g_axisInputAccum)              \
    X(joyCalibTimer, g_joyCalibTimer)                 \
    X(groundAltitude, g_groundAltitude)               \
    X(stallSpeed, g_stallSpeed)                       \
    X(cornerSpeed, g_cornerSpeed)                     \
    X(autoCrashDive, g_autoCrashDive)                 \
    X(fuelRemaining, g_fuelRemaining)                 \
    X(gunAmmo, g_gunAmmo)                             \
    X(gunHits, g_gunHits)                             \
    X(gunFiredFlag, g_gunFiredFlag)                   \
    X(bombDamageMask, g_bombDamageMask)               \
    XA(missleSpec, missleSpec)                        \
    X(missileSpecIndex, missileSpecIndex)             \
    X(currentWeaponType, g_currentWeaponType)         \
    X(weaponMarkerSel, g_weaponMarkerSel)             \
    X(lastMissileSlot, g_lastMissileSlot)             \
    X(fireCooldown, g_fireCooldown)                   \
    XA(eventTimers, g_eventTimers)                    \
    X(airTargetLock, g_airTargetLock)                 \
    X(groundTargetLock, g_groundTargetLock)           \
    X(aamLockActive, g_aamLockActive)                 \
    X(aamLockCooldown, g_aamLockCooldown)             \
    X(lockedTargetKilled, g_lockedTargetKilled)       \
    X(targetLeadAngle, g_targetLeadAngle)             \
    X(aamSeekerX, g_aamSeekerX)                       \
    X(aamSeekerY, g_aamSeekerY)                       \
    X(rollPitchTrim, g_rollPitchTrim)                 \
    X(aamLeadDist, g_aamLeadDist)                     \
    X(loftTargetIdx, g_loftTargetIdx)                 \
    X(acqRange, g_acqRange)                           \
    X(acqAimY, g_acqAimY)                             \
    X(lockToneFlag, g_lockToneFlag)                   \
    X(targetRange, g_targetRange)                     \
    X(targetBearing, g_targetBearing)                 \
    X(targetInHudFlag, g_targetInHudFlag)             \
    X(prevKillMarker, g_prevKillMarker)               \
    X(axisInput1, g_axisInput1)                       \
    X(closestThreatIndex, g_closestThreatIndex)       \
    X(nearestThreatRange, g_nearestThreatRange)       \
    X(prevThreatIndex, g_prevThreatIndex)             \
    X(threatActiveTimer, g_threatActiveTimer)         \
    X(smokeSourceIdx, g_smokeSourceIdx)               \
    X(smokeParticleSlot, g_smokeParticleSlot)         \
    X(trackedEnemyIdx, g_trackedEnemyIdx)             \
    X(threatLabelTarget, g_threatLabelTarget)         \
    X(threatScopeRange, g_threatScopeRange)           \
    X(threatRadarFlag, g_threatRadarFlag)             \
    X(threatDisplayTtl, g_threatDisplayTtl)           \
    X(savedSamTtl, g_savedSamTtl)                     \
    X(threatToneLevel, g_threatToneLevel)             \
    X(enemyThreatCount, g_enemyThreatCount)           \
    XP(threatRefX, threatRefY, g_threatRefPos)        \
    X(threatRefZ, g_threatRefZ)                       \
    X(threatRefHead, g_threatRefHead)                 \
    X(threatSpec, g_threatSpec)                       \
    X(activeThreatCount, g_activeThreatCount)         \
    X(scopeArcStart, g_scopeArcStart)                 \
    X(scopeArcEnd, g_scopeArcEnd)                     \
    X(scopeSweepTimer, g_scopeSweepTimer)             \
    X(prevScopeRange, g_prevScopeRange)               \
    X(scopeCenterX, g_scopeCenterX)                   \
    X(scopeCenterY, g_scopeCenterY)                   \
    X(scopeArcColor, g_scopeArcColor)                 \
    X(scopeClipLeft, g_scopeClipLeft)                 \
    X(scopeClipTop, g_scopeClipTop)                   \
    X(scopeClipRight, g_scopeClipRight)               \
    X(scopeClipBottom, g_scopeClipBottom)             \
    X(scopeArcRange, g_scopeArcRange)                 \
    X(radarScopeRange, g_radarScopeRange)             \
    X(autopilotEngaged, g_autopilotEngaged)           \
    X(autopilotAltitude, g_autopilotAltitude)         \
    X(directorMode, g_directorMode)                   \
    X(directorEventDeadline, g_directorEventDeadline) \
    X(destroyedCueDeadline, g_destroyedCueDeadline)   \
    X(waypointIndex, waypointIndex)                   \
    X(waypointBearing, g_waypointBearing)             \
    X(savedPosVisible, g_savedPosVisible)             \
    X(northSouthSign, g_northSouthSign)               \
    X(lastSpawnTick, g_lastSpawnTick)                 \
    X(inLandingCorridor, g_inLandingCorridor)         \
    X(landingDoneFlag, g_landingDoneFlag)             \
    X(landingTimer, g_landingTimer)                   \
    X(autoLandingActive, g_autoLandingActive)         \
    X(gearDownArmed, g_gearDownArmed)                 \
    X(playerPlaneFlags, g_playerPlaneFlags)           \
    X(resupplyCount, g_resupplyCount)                 \
    X(attackRangeX, g_attackRangeX)                   \
    X(attackRangeY, g_attackRangeY)                   \
    X(ejectState, g_ejectState)                       \
    X(ejectPending, g_ejectPending)                   \
    X(damageTakenFlag, g_damageTakenFlag)             \
    X(threatWarningBits, g_threatWarningBits)         \
    X(damageSeq, g_damageSeq)                         \
    XP(wreckX, wreckY, g_wreckPos)                    \
    X(wreckAlt, g_wreckAlt)                           \
    X(wreckFallVel, g_wreckFallVel)                   \
    X(crashCamX, g_crashCamX)                         \
    X(crashCamY, g_crashCamY)                         \
    X(crashCamZ, g_crashCamZ)                         \
    XP(hitMapX, hitMapY, g_hitMapPos)                 \
    X(hitAlt, g_hitAlt)                               \
    X(hitEffectTimer, g_hitEffectTimer)               \
    X(initPhase, g_initPhase)                         \
    X(inputDisabled, g_inputDisabled)                 \
    X(enemyAlertFlag, g_enemyAlertFlag)               \
    X(finalThreatScore, g_finalThreatScore)           \
    X(wingmanX, g_wingmanX)                           \
    X(wingmanY, g_wingmanY)                           \
    XA(missionEndedFlag, g_missionEndedFlag)          \
    X(viewMode, g_viewMode)                           \
    X(lastViewKey, g_lastViewKey)                     \
    X(viewHeading, g_viewHeading)                     \
    X(viewPitch, g_viewPitch)                         \
    X(viewHeadingOffset, g_viewHeadingOffset)         \
    X(extViewPitch, g_extViewPitch)                   \
    X(viewTargetX, g_viewTargetX)                     \
    X(viewTargetY, g_viewTargetY)                     \
    X(viewTargetAlt, g_viewTargetAlt)                 \
    X(viewTargetObj, g_viewTargetObj)                 \
    X(padlockAircraft, g_padlockAircraft)             \
    X(externalCamDist, g_externalCamDist)             \
    XA(viewSnapshotRing, g_viewSnapshotRing)          \
    XA(camRotMatrix, g_camRotMatrix)                  \
    X(camEyeX, g_camEyeX)                             \
    X(camEyeY, g_camEyeY)                             \
    X(camEyeZ, g_camEyeZ)                             \
    X(camEyeFracX, g_camEyeFracX)                     \
    X(camEyeFracY, g_camEyeFracY)                     \
    X(camEyeFracZ, g_camEyeFracZ)                     \
    X(viewPosX, g_viewPosX)                           \
    X(viewPosY, g_viewPosY)                           \
    X(viewPosZ, g_viewPosZ)                           \
    X(viewPosFracX, g_viewPosFracX)                   \
    X(viewPosFracY, g_viewPosFracY)                   \
    X(viewPosFracZ, g_viewPosFracZ)                   \
    X(sphereTiltZ, g_sphereTiltZ)                     \
    X(hudMsgTimer, g_hudMsgTimer)                     \
    X(dirMsgTimer, g_dirMsgTimer)                     \
    X(hudBottomY, g_hudBottomY)                       \
    XA(tacmapIndicators, g_tacmapIndicators)          \
    X(mapZoomLevel, g_mapZoomLevel)                   \
    X(mapCenterX, g_mapCenterX)                       \
    X(mapCenterY, g_mapCenterY)                       \
    X(mapMode, g_mapMode)                             \
    X(activePanelMode, g_activePanelMode)             \
    X(hudVisible, g_hudVisible)                       \
    X(kbdSensitivity, g_kbdSensitivity)               \
    X(detailLevel, g_detailLevel)                     \
    X(nightMode, g_nightMode)                         \
    X(posVisibleFlag, g_posVisibleFlag)               \
    X(flightPathMarkerY, g_flightPathMarkerY)         \
    X(offscreenProjX, g_offscreenProjX)               \
    X(trkRange, g_trkRange)                           \
    X(trkBearing, g_trkBearing)                       \
    X(trkSize, g_trkSize)                             \
    X(trkScale, g_trkScale)                           \
    X(trkPitch, g_trkPitch)                           \
    X(trkRoll, g_trkRoll)                             \
    X(spherePitch, g_spherePitch)                     \
    X(sphereRoll, g_sphereRoll)                       \
    X(sphereDistZ, g_sphereDistZ)                     \
    X(sphereRadius, g_sphereRadius)                   \
    XA(strBuf, strBuf)                                \
    XA(tempString, tempString)                        \
    XA(string3C04A, string_3C04A)                     \
    XA(itoaScratch, g_itoaScratch)                    \
    X(strTruncDot, g_strTruncDot)                     \
    XA(strTruncTerm, g_strTruncTerm)                  \
    X(exitMsgDigit, g_exitMsgDigit)                   \
    X(exitMsgTerm, g_exitMsgTerm)                     \
    X(unusedHudFlag, g_unusedHudFlag)

#define PS_SAVE(f, g) psx::store(p->f, g);
#define PS_LOAD(f, g) psx::load(g, p->f);
#define PS_SAVE_ARR(f, g) memcpy(p->f, g, sizeof(p->f));
#define PS_LOAD_ARR(f, g) memcpy(g, p->f, sizeof(p->f));
#define PS_SAVE_MAT(f, g) psx::store(p->f, g);
#define PS_LOAD_MAT(f, g) psx::load(g, p->f);
#define PS_SAVE_POS(fx, fy, g) do { \
    p->fx = f15::math::legacy::mapWordX(g); \
    p->fy = f15::math::legacy::mapWordY(g); \
} while (0);
#define PS_LOAD_POS(fx, fy, g) g = f15::math::legacy::mapPosition(p->fx, p->fy);

/* commData tail written by finalizeMission()/initWeaponLoadout(): per-pilot
 * mission outcome + loadout bookkeeping, so it rides the ctx too. */
static void commTailSave(struct PlayerSim *p) {
    p->comm.landingType = commData->landingType;
    p->comm.bailoutSurvived = commData->bailoutSurvived;
    p->comm.trainingFlag = commData->trainingFlag;
    p->comm.gunHits = commData->gunHits;
    memcpy(p->comm.weaponCount, commData->weaponCount, sizeof(p->comm.weaponCount));
    p->comm.worldX = commData->worldX;
    p->comm.worldY = commData->worldY;
}

static void commTailLoad(const struct PlayerSim *p) {
    commData->landingType = p->comm.landingType;
    commData->bailoutSurvived = p->comm.bailoutSurvived;
    commData->trainingFlag = p->comm.trainingFlag;
    commData->gunHits = p->comm.gunHits;
    memcpy(commData->weaponCount, p->comm.weaponCount, sizeof(p->comm.weaponCount));
    commData->worldX = p->comm.worldX;
    commData->worldY = p->comm.worldY;
}

void playerSwapOut(struct PlayerSim *p) {
    PLAYER_FIELDS(PS_SAVE, PS_SAVE_ARR, PS_SAVE_MAT, PS_SAVE_POS)
    commTailSave(p);
}

void playerSwapIn(const struct PlayerSim *p) {
    PLAYER_FIELDS(PS_LOAD, PS_LOAD_ARR, PS_LOAD_MAT, PS_LOAD_POS)
    commTailLoad(p);
}

/* Canonical hash over the simulation-meaningful ctx state (plan §24): used by
 * determinism/regression tests. Covers the contiguous sim region plus the
 * gameplay fields that live OUTSIDE it; pure-presentation scratch and
 * string buffers are excluded. Field classification follows actual
 * consumers:
 *   active            slot lifecycle - gates the server player pass
 *   ended             mission-over - gates sim participation
 *   viewHeadingOffset read by simTargetLock's look-away checks (egtarget.c)
 *   padlockAircraft   read by threat targeting/escort spawn (egthreat.c)
 *   activePanelMode   gates the sim-side AAM/ground acquisition pass
 *   viewMode          external-view bit skips acquisition (simTargetLock)
 *   nightMode         scales acquisition ranges (100<<6-night / 0x4b<<6-night)
 * The rest of the trailing block (view/camera/scope string scratch,
 * tacmap indicators, HUD timers) only feeds draw paths. */
uint32 playerCtxHash(const struct PlayerSim *p) {
    const uint8 *b = (const uint8 *)p;
    size_t lo = offsetof(struct PlayerSim, ViewX);
    size_t hi = offsetof(struct PlayerSim, missionEndedFlag) + sizeof(p->missionEndedFlag);
    uint32 h = 2166136261u;
    size_t i;
    for (i = lo; i < hi; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    h ^= (uint32)(uint16)p->active;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->ended;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->viewHeadingOffset;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->padlockAircraft;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->activePanelMode;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->viewMode;
    h *= 16777619u;
    h ^= (uint32)(uint16)p->nightMode;
    h *= 16777619u;
    return h;
}

void playerInitCtx(struct PlayerSim *p) { memset(p, 0, sizeof(*p)); }

#endif /* F15_NET */

/* ---- input seam ---- */

/* Use public declarations: return types are part of MSVC mangling. */

static int localKeyWaiting(void *ctx) {
    (void)ctx;
    return kbhit();
}
static uint16 localReadKey(void *ctx) {
    (void)ctx;
    return egReadKey();
}
static void localPollAxes(void *ctx, uint8 *joyX, uint8 *joyY) {
    (void)ctx;
    if (input_preferGamepad()) {
        readCalibratedJoystick();
    } else {
        joyAxes[0] =
            (uint8)(((int16)((uint8)g_joyRawX - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;
        joyAxes[1] =
            (uint8)(((int16)((uint8)g_joyRawY - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;
    }
    *joyX = joyAxes[0];
    *joyY = joyAxes[1];
}
static int localFireButton(void *ctx, int n) {
    (void)ctx;
    return (commData->setupUseJoy && misc_readJoystick(n)) ? 1 : 0;
}

static const struct SimInputOps kLocalInput = {localKeyWaiting, localReadKey,
                                               localPollAxes, localFireButton};

static const struct SimInputOps *g_inputOps = &kLocalInput;
static void *g_inputCtx = 0;

void simInputSet(const struct SimInputOps *ops, void *ctx) {
    g_inputOps = ops ? ops : &kLocalInput;
    g_inputCtx = ctx;
}
void simInputReset(void) { simInputSet(0, 0); }

int simInputKeyWaiting(void) { return g_inputOps->keyWaiting(g_inputCtx); }
uint16 simInputReadKey(void) { return g_inputOps->readKey(g_inputCtx); }
void simInputPollAxes(uint8 *joyX, uint8 *joyY) {
    g_inputOps->pollAxes(g_inputCtx, joyX, joyY);
}
int simInputFireButton(int n) { return g_inputOps->fireButton(g_inputCtx, n); }

/* ---- presentation sink (plan §12) + headless flag ---- */

struct SimEvents g_simEvents;
int g_headlessSim = 0;
int g_netClientMode = 0;

void simEventsHudMessage(const char *text) {
    if (g_simEvents.onHudMessage)
        g_simEvents.onHudMessage(text);
}
void simEventsTimedMessage(const char *text) {
    if (g_simEvents.onTimedMessage)
        g_simEvents.onTimedMessage(text);
}
void simEventsSound(int id, int priority) {
    if (g_simEvents.onSound)
        g_simEvents.onSound(id, priority);
}
void simEventsVoice(int cue) {
    if (g_simEvents.onVoice)
        g_simEvents.onVoice(cue);
}
void simEventsMapEvent(int16 type, int16 arg) {
    if (g_simEvents.onMapEvent)
        g_simEvents.onMapEvent(type, arg);
}

#ifdef F15_NET
/* ---- per-player step (server path) ---- */

extern void stepFlightModel(void);  /* egflight.c */
extern void updatePlayerFrame(void); /* egframe.c */

void simulatePlayer(struct PlayerSim *ctx) {
    playerSwapIn(ctx);
    stepFlightModel();
    updatePlayerFrame();
    playerSwapOut(ctx);
}

/* ---- remote input source ----
 * The legacy sim reads at most ONE key per tick (it pops keyScancode then
 * flushes the rest), so the remote source serves one queued command per tick:
 * keyWaiting reports a pending cmd only if none was served this frameTick. */

void remoteInputInit(struct RemoteInput *r) {
    memset(r, 0, sizeof(*r));
    r->joyX = r->joyY = 0x80;
    r->lastServeTick = -1;
    r->frameTickPtr = &frameTick;
}

void (*g_remoteKeyServedHook)(struct RemoteInput *r, uint16 scan,
                              uint32 cmdSeq, uint8 cmdIdx);

int remoteInputPushKey(struct RemoteInput *r, uint16 scan,
                       uint32 cmdSeq, uint8 cmdIdx) {
    if ((uint8)(r->tail - r->head) >= REMOTE_KEY_QUEUE)
        return 0; /* queue full: rejected (rate-limit protection, plan §28) */
    r->queue[r->tail % REMOTE_KEY_QUEUE] = scan;
    r->queueSeq[r->tail % REMOTE_KEY_QUEUE] = cmdSeq;
    r->queueIdx[r->tail % REMOTE_KEY_QUEUE] = cmdIdx;
    r->tail++;
    return 1;
}

void remoteInputSetAxes(struct RemoteInput *r, uint8 joyX, uint8 joyY,
                        uint8 buttons) {
    r->joyX = joyX;
    r->joyY = joyY;
    r->buttons = buttons;
}

static int remoteKeyWaiting(void *ctx) {
    struct RemoteInput *r = (struct RemoteInput *)ctx;
    return r->head != r->tail && r->lastServeTick != r->frameTickPtr->word();
}
static uint16 remoteReadKey(void *ctx) {
    struct RemoteInput *r = (struct RemoteInput *)ctx;
    uint16 k = r->queue[r->head % REMOTE_KEY_QUEUE];
    uint32 cmdSeq = r->queueSeq[r->head % REMOTE_KEY_QUEUE];
    uint8 cmdIdx = r->queueIdx[r->head % REMOTE_KEY_QUEUE];
    r->head++;
    r->lastServeTick = r->frameTickPtr->word();
    if (g_remoteKeyServedHook)
        g_remoteKeyServedHook(r, k, cmdSeq, cmdIdx); /* sim consumed it */
    return k;
}
static void remotePollAxes(void *ctx, uint8 *joyX, uint8 *joyY) {
    struct RemoteInput *r = (struct RemoteInput *)ctx;
    *joyX = joyAxes[0] = r->joyX;
    *joyY = joyAxes[1] = r->joyY;
}
static int remoteFireButton(void *ctx, int n) {
    struct RemoteInput *r = (struct RemoteInput *)ctx;
    return (r->buttons >> n) & 1;
}

static const struct SimInputOps kRemoteInput = {remoteKeyWaiting, remoteReadKey,
                                                remotePollAxes, remoteFireButton};

const struct SimInputOps *remoteInputOps(void) { return &kRemoteInput; }
#endif /* F15_NET */
