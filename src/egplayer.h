#ifndef F15_EGPLAYER_H
#define F15_EGPLAYER_H
/*
 * egplayer.h - per-player simulation context (plan §5/§6).
 *
 * The original code models THE player as ~150 egdata globals - exactly one
 * aircraft. For the authoritative server we need N of those. Rather than
 * rewriting every function signature, a PlayerSim captures the player-scoped
 * globals; the server swaps a context in, runs the legacy per-player step, and
 * swaps it back out (plan §6 migration mechanism).
 *
 * Field selection rule: a global goes into PlayerSim iff it carries state
 * across ticks AND describes this pilot's aircraft/HUD. World tables
 * (g_simObjects, g_projectiles, g_planeTable, mapEvents, waypoints,
 * targetSlots, missiles[], sams[], stringPool...) stay process-global - there
 * is one shared world. Render scratch (clip state, matrices used only inside a
 * call, file buffers) also stays global: it is transient within a call and the
 * server steps players serially.
 */
#include "egtypes.h"
#include "inttype.h"
#include "struct.h"

#ifdef __cplusplus
extern "C" {
#endif

/* commData tail fields that finalizeMission() writes per pilot; swapped as a
 * unit so each remote pilot's outcome lands in their own context. */
struct PlayerCommTail {
    int16 landingType;
    int16 bailoutSurvived;
    int16 trainingFlag;
    int16 gunHits;
    int16 weaponCount[4];
    uint16 worldX, worldY;
};

struct PlayerSim {
    int active;   /* slot in use */
    int ended;    /* this pilot's mission is over (per-player g_missionEndedFlag) */

    /* position / motion */
    int32 ViewX, ViewY;
    int16 viewZ;
    int16 viewX_, viewY_;
    int16 ourHead, ourPitch, ourRoll, viewRoll;
    int16 orientMatrix[9], yawMatrix[9], pitchMatrix[9], rollMatrix[9];
    int16 matrixScratch[9];
    char orientationDirty, rollWasNonzero;
    int16 rotationCounter;
    unsigned int altitude;
    int velocity;
    int16 knots, thrust, setThrust, climbRate, liftForce;
    int gees;
    char geeStringBuf[12];
    uint8 highGeeFlag[1];
    int rollInput;
    int16 pitchInput;
    uint16 keyScancode;
    int16 axisInputAccum[4];
    int16 joyCalibTimer;
    int16 groundAltitude;
    int16 stallSpeed, cornerSpeed;
    int16 autoCrashDive;

    /* resources */
    int16 fuelRemaining;
    int16 gunAmmo, gunHits, gunFiredFlag;
    int16 bombDamageMask;
    struct MissileSpec missleSpec[4];
    int16 missileSpecIndex;
    int16 currentWeaponType;
    int16 weaponMarkerSel;
    int16 lastMissileSlot;
    int16 fireCooldown;
    int16 eventTimers[3];

    /* targeting / threat view (per-pilot; plan §18) */
    int16 airTargetLock, groundTargetLock, aamLockActive, aamLockCooldown;
    int16 lockedTargetKilled, targetLeadAngle, aamSeekerX, aamSeekerY;
    int16 rollPitchTrim, aamLeadDist, loftTargetIdx, acqRange, acqAimY;
    int16 lockToneFlag, targetRange, targetBearing, targetInHudFlag, prevKillMarker;
    int16 axisInput1;
    int16 closestThreatIndex, nearestThreatRange, prevThreatIndex, threatActiveTimer;
    int16 smokeSourceIdx, smokeParticleSlot, trackedEnemyIdx, threatLabelTarget;
    int16 threatScopeRange, threatRadarFlag, threatDisplayTtl, savedSamTtl;
    int16 threatToneLevel, enemyThreatCount, threatRefX, threatRefY, threatRefZ;
    int16 threatRefHead, threatSpec, activeThreatCount;
    int16 scopeArcStart, scopeArcEnd, scopeSweepTimer, prevScopeRange;
    int16 scopeCenterX, scopeCenterY;
    int scopeArcColor;
    int16 scopeClipLeft, scopeClipTop, scopeClipRight, scopeClipBottom;
    int16 scopeArcRange;
    int16 radarScopeRange;

    /* navigation / autopilot */
    int16 autopilotEngaged, autopilotAltitude;
    int directorMode;
    int16 directorEventDeadline, destroyedCueDeadline;
    int16 waypointIndex;
    int16 waypointBearing;
    char savedPosVisible;
    int16 northSouthSign;
    int16 lastSpawnTick;

    /* landing / mission state */
    int16 inLandingCorridor, landingDoneFlag, landingTimer;
    int16 autoLandingActive, gearDownArmed, resupplyCount;
    int16 playerPlaneFlags; /* g_playerPlaneFlags: gear/brake/canopy bits */
    int16 attackRangeX, attackRangeY;
    int16 ejectState, ejectPending, damageTakenFlag;
    /* server-side counter: bumped each tick damageTakenFlag latches on, so a
     * client can fire the HUD effect exactly once per damage event (the flag
     * itself is a transient the headless server must release each tick). */
    int16 damageSeq;
    int16 wreckX, wreckY, wreckAlt, wreckFallVel;
    int16 crashCamX, crashCamY, crashCamZ;
    int16 hitMapX, hitMapY, hitAlt;
    int hitEffectTimer;
    int16 initPhase;
    int16 inputDisabled;
    int16 enemyAlertFlag;
    int16 finalThreatScore;
    uint16 wingmanX, wingmanY;
    uint8 missionEndedFlag[2];

    /* view / HUD presentation that carries across ticks */
    ViewMode viewMode;
    ViewMode lastViewKey;
    int16 viewHeading, viewPitch, viewHeadingOffset, extViewPitch;
    int32 viewTargetX, viewTargetY;
    int16 viewTargetAlt, viewTargetObj;
    int16 padlockAircraft, externalCamDist;
    struct ViewSnapshot viewSnapshotRing[16];
    int16 camRotMatrix[9];
    int32 camEyeX, camEyeY, camEyeZ;
    int16 camEyeFracX, camEyeFracY, camEyeFracZ;
    int16 viewPosX, viewPosY, viewPosZ;
    int16 viewPosFracX, viewPosFracY, viewPosFracZ;
    int16 sphereTiltZ;
    int16 hudMsgTimer, dirMsgTimer;
    int16 hudBottomY;
    int16 tacmapIndicators[156];
    int16 mapZoomLevel, mapCenterX, mapCenterY, mapMode, activePanelMode;
    int16 hudVisible, kbdSensitivity, detailLevel, nightMode;
    int16 posVisibleFlag, flightPathMarkerY, offscreenProjX;
    int16 trkRange, trkBearing, trkSize, trkScale, trkPitch, trkRoll;
    int16 spherePitch, sphereRoll, sphereDistZ, sphereRadius;
    char strBuf[78], tempString[80], string3C04A[80], itoaScratch[12];
    uint8 strTruncDot, strTruncTerm[1], exitMsgDigit, exitMsgTerm;
    int16 unusedHudFlag;

    struct PlayerCommTail comm;
};

/* Context swap: out = capture globals into p, in = restore globals from p.
 * Must bracket every per-player step on the server. */
void playerSwapOut(struct PlayerSim *p);
void playerSwapIn(const struct PlayerSim *p);
void playerInitCtx(struct PlayerSim *p); /* zero + slot defaults */
uint32 playerCtxHash(const struct PlayerSim *p); /* determinism hash (§24) */

/* One full legacy per-player step for ctx: swap-in, stepFlightModel() +
 * the player-scoped half of updateFrame(), swap-out. */
void simulatePlayer(struct PlayerSim *ctx);

/* ---- input seam (plan §3) ----
 * The legacy code reads kbhit()/egReadKey()/misc_readJoystick()/g_joyRaw*.
 * SimInputOps virtualises that surface; the local (human-at-keyboard) ops are
 * the default, the server installs a remote ops per context. */
struct SimInputOps {
    int (*keyWaiting)(void *ctx);          /* a discrete command is queued */
    uint16 (*readKey)(void *ctx);          /* pop one BIOS-word command */
    void (*pollAxes)(void *ctx, uint8 *joyX, uint8 *joyY);
    int (*fireButton)(void *ctx, int n);   /* level-triggered: 0=gun 1=missile */
};

void simInputSet(const struct SimInputOps *ops, void *ctx);
void simInputReset(void); /* restore local SDL/keyboard ops */

int simInputKeyWaiting(void);
uint16 simInputReadKey(void);
void simInputPollAxes(uint8 *joyX, uint8 *joyY);
int simInputFireButton(int n);

/* ---- remote input source (server side) ----
 * One queued command is served per frameTick (the legacy code only consumes a
 * single key per tick and flushes the rest). */
#define REMOTE_KEY_QUEUE 32

struct RemoteInput {
    uint16 queue[REMOTE_KEY_QUEUE];
    /* execution-ack tags parallel to queue[]: which (clientSeq, cmd index)
     * produced each key, so the server can emit NE_CMD_ACK when the sim
     * actually consumes it. 0 seq = untagged. */
    uint32 queueSeq[REMOTE_KEY_QUEUE];
    uint8 queueIdx[REMOTE_KEY_QUEUE];
    uint8 head, tail;
    uint8 joyX, joyY, buttons;
    int16 lastServeTick;
    int16 *frameTickPtr;
};

void remoteInputInit(struct RemoteInput *r);
int remoteInputPushKey(struct RemoteInput *r, uint16 scan,
                       uint32 cmdSeq, uint8 cmdIdx); /* 0 = queue full */
/* fired when the sim consumes a queued remote key (server installs it to
 * emit the execution ack; NULL in single-player/tools) */
extern void (*g_remoteKeyServedHook)(struct RemoteInput *r, uint16 scan,
                                     uint32 cmdSeq, uint8 cmdIdx);
void remoteInputSetAxes(struct RemoteInput *r, uint8 joyX, uint8 joyY,
                        uint8 buttons);
const struct SimInputOps *remoteInputOps(void);

/* ---- presentation event sink (plan §12) ----
 * The headless server installs these to turn hudMessage/makeSound/etc into
 * wire events. All NULL by default = pure no-op. */
struct SimEvents {
    void (*onHudMessage)(const char *text);
    void (*onTimedMessage)(const char *text);
    void (*onSound)(int id, int priority);
    void (*onVoice)(int cue);
    void (*onMapEvent)(int16 type, int16 arg);
};

extern struct SimEvents g_simEvents;
extern int g_headlessSim;   /* 1 = skip presentation-only work (server) */
extern int g_netClientMode; /* 1 = world state arrives via snapshots (client) */

void simEventsHudMessage(const char *text);
void simEventsTimedMessage(const char *text);
void simEventsSound(int id, int priority);
void simEventsVoice(int cue);
void simEventsMapEvent(int16 type, int16 arg);

#ifdef __cplusplus
}
#endif

#endif /* F15_EGPLAYER_H */
