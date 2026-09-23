/* net_sim_tests.cpp - deterministic headless-combat checks for the
 * server-authoritative paths added by the netcode review fixes:
 *
 *  - simTargetLock(): authoritative air-target acquisition against a parked
 *    remote-player object, incl. the own-object exclusion.
 *  - simBulletHits(): a player round damages/destroys a parked remote object
 *    and the g_playerObjectHitHook maps it back to its owner; owner filtering
 *    skips other players' rounds; a miss does nothing.
 *  - updateThreatTargeting(): per-ctx ownership - a projectile stamped for a
 *    different player is not advanced under the resident ctx.
 *
 * World state is fabricated directly (no world file needed): the "world" is a
 * couple of inert SimObjects, plus parked remote-player slots above
 * g_groundUnitCount covered by g_simObjScanBound - the same layout the server
 * publishes each tick.
 */
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "egcombat.h"
#include "egdata.h"
#include "egframe.h"
#include "egplayer.h"
#include "egtarget.h"
#include "inttype.h"
#include "net/codec.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "struct.h"

static int fails;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            fails++;                                                 \
        }                                                            \
    } while (0)

/* ---- helpers ---------------------------------------------------------- */

#define WORLD_OBJS 4
#define OWNER_IDX 1
#define PARKED_SLOT (WORLD_OBJS + OWNER_IDX)

static int16 hookCalls;
static int16 hookLastObj;

static void testHitHook(int16 objIdx) {
    hookCalls++;
    hookLastObj = objIdx;
}

/* Minimal world: WORLD_OBJS inert objects + parked remote player slots.
 * Parked objects sit at g_groundUnitCount + playerIdx (server convention). */
static void setupWorld(void) {
    int i;
    memset(g_simObjects, 0, sizeof(g_simObjects[0]) * F15_MAX_SIM_OBJECTS);
    g_groundUnitCount = WORLD_OBJS;
    g_simObjScanBound = WORLD_OBJS + F15_MAX_PLAYERS;
    for (i = 0; i < WORLD_OBJS; i++) {
        g_simObjects[i].flags.b[0] = 0; /* inert: not lockable/hittable */
    }
    /* resident ctx (player 0): on the runway, heading 0 */
    g_viewX_ = 0x4000;
    g_viewY_ = 0x4000;
    g_viewZ = 0x80;
    g_ourHead = 0;
    g_viewHeadingOffset = 0;
    g_viewMode = (ViewMode)0;
    g_residentPlayer = 0;
    g_nightMode = 0;
    g_directorMode = 0;
    g_currentWeaponType = 1; /* A2A missile selected */
    g_airTargetLock = -1;
    g_groundTargetLock = -1;
    g_aamLockCooldown = 0;
    g_aamLockActive = 0;
    g_lockedTargetKilled = 0;
    g_activePanelMode = 0;
    g_nearestThreatRange = 0x7fff;
    g_closestThreatIndex = 0;
    g_radarScopeRange = 2; /* obs tests: mid zoom, scope covers +-0x400-ish */
    g_planeCount = 0;
    memset(&g_planeTable.planes, 0, sizeof(g_planeTable.planes));
    memset(g_projectiles, 0, sizeof(g_projectiles[0]) * F15_MAX_PROJECTILES);
    g_missionStatus = 1;
    g_frameRateScaling = 1;
    frameTick = 100;
}

static void parkPlayer(int playerIdx, int16 posX, int16 posY, int16 alt,
                       int16 speed) {
    int slot = WORLD_OBJS + playerIdx;
    struct SimObject *o = &g_simObjects[slot];
    o->posX = posX;
    o->posY = posY;
    o->alt = alt;
    o->spec = 0;
    o->speed = speed;
    o->objType = (int16)playerIdx;
    o->flags.b[0] = 2;
    o->flags.b[1] = SIMFLAG_B1_REMOTE_PLAYER;
}

/* ---- simTargetLock ---------------------------------------------------- */

/* A parked remote player ahead of the resident ctx is acquired as the
 * authoritative air target. */
static void test_lock_acquires_remote(void) {
    setupWorld();
    /* heading 0: ahead = decreasing map-Y (viewY_ space) */
    parkPlayer(OWNER_IDX, 0x4000, 0x4000 - 0x300, 0x90, 100);
    g_airTargetLock = (int16)(-1 | 0x80); /* reacquire request */
    simTargetLock();
    CHECK(g_airTargetLock == PARKED_SLOT);
}

/* The owner of a parked object must never lock itself. */
static void test_lock_skips_own_object(void) {
    setupWorld();
    g_residentPlayer = OWNER_IDX;
    parkPlayer(OWNER_IDX, 0x4000, 0x4000 - 0x300, 0x90, 100);
    g_airTargetLock = (int16)(-1 | 0x80);
    simTargetLock();
    CHECK(g_airTargetLock == -1);
}

/* A parked player with speed 0 (sitting on the runway) is not eligible -
 * same rule as any world object. */
static void test_lock_skips_stationary(void) {
    setupWorld();
    parkPlayer(OWNER_IDX, 0x4000, 0x4000 - 0x300, 0x90, 0);
    g_airTargetLock = (int16)(-1 | 0x80);
    simTargetLock();
    CHECK(g_airTargetLock == -1);
}

/* ---- simBulletHits ---------------------------------------------------- */

/* A resident player's round crossing a parked remote object registers a hit;
 * at point-blank the object is destroyed and the owner hook fires. */
static void test_bullet_hits_remote(void) {
    struct SimObject *o;
    setupWorld();
    parkPlayer(OWNER_IDX, 0x4100, 0x3F00, 0x90, 100);
    o = &g_simObjects[PARKED_SLOT];

    g_playerObjectHitHook = testHitHook;
    hookCalls = hookLastObj = 0;
    g_bulletTrackCount = 1;
    memset(bulletTracks, 0, sizeof(bulletTracks));
    bulletTracks[0].posX = (int32)(uint16)o->posX << 5;
    bulletTracks[0].posY = (int32)(uint16)o->posY << 5;
    bulletTracks[0].alt = o->alt;
    bulletTracks[0].velX = bulletTracks[0].velY = bulletTracks[0].velZ = 0;
    bulletTracks[0].targetPlayer = 0; /* resident player's round */

    simBulletHits();
    CHECK(hookCalls == 1);
    CHECK(hookLastObj == PARKED_SLOT);
    CHECK((o->flags.b[0] & 2) == 0 || (o->flags.b[0] & 0x30) != 0);
    g_playerObjectHitHook = 0;
}

/* Rounds stamped for another player are not processed under the resident
 * ctx (no hit, no hook). */
static void test_bullet_owner_filter(void) {
    struct SimObject *o;
    setupWorld();
    parkPlayer(OWNER_IDX, 0x4100, 0x3F00, 0x90, 100);
    o = &g_simObjects[PARKED_SLOT];

    g_playerObjectHitHook = testHitHook;
    hookCalls = hookLastObj = 0;
    g_bulletTrackCount = 1;
    memset(bulletTracks, 0, sizeof(bulletTracks));
    bulletTracks[0].posX = (int32)(uint16)o->posX << 5;
    bulletTracks[0].posY = (int32)(uint16)o->posY << 5;
    bulletTracks[0].alt = o->alt;
    bulletTracks[0].targetPlayer = 2; /* belongs to player 2, not resident 0 */

    simBulletHits();
    CHECK(hookCalls == 0);
    CHECK((o->flags.b[0] & 0x30) == 0); /* object untouched */
    g_playerObjectHitHook = 0;
}

/* A round nowhere near any object damages nothing. */
static void test_bullet_miss(void) {
    setupWorld();
    parkPlayer(OWNER_IDX, 0x4100, 0x3F00, 0x90, 100);

    g_playerObjectHitHook = testHitHook;
    hookCalls = hookLastObj = 0;
    g_bulletTrackCount = 1;
    memset(bulletTracks, 0, sizeof(bulletTracks));
    bulletTracks[0].posX = 0x6000 << 5;
    bulletTracks[0].posY = 0x6000 << 5;
    bulletTracks[0].alt = 0x400;
    bulletTracks[0].targetPlayer = 0;

    simBulletHits();
    CHECK(hookCalls == 0);
    CHECK((g_simObjects[PARKED_SLOT].flags.b[0] & 0x30) == 0);
    g_playerObjectHitHook = 0;
}

/* ---- projectile ownership --------------------------------------------- */

/* A projectile stamped for a different ctx is not advanced: ttl must not
 * tick down under the wrong resident player. */
static void test_projectile_owner_filter(void) {
    setupWorld();
    memset(g_projectiles, 0, sizeof(g_projectiles[0]) * F15_MAX_PROJECTILES);
    g_projectiles[8].ttl = 100;
    g_projectiles[8].specIdx = 0;
    g_projectiles[8].targetPlayer = OWNER_IDX; /* owned by player 1 */
    g_residentPlayer = 0;
    updateThreatTargeting();
    CHECK(g_projectiles[8].ttl == 100);
}

/* ---- netObsBuild ------------------------------------------------------- */

static int findContact(const struct NetObs *obs, uint32_t id) {
    int i;
    for (i = 0; i < obs->nContacts; i++)
        if (obs->contacts[i].id == id)
            return i;
    return -1;
}

/* Encode one observation for player `playerIdx` under the globals setupWorld
 * established, then decode it back. parkedObjBase == WORLD_OBJS (same layout
 * as parkPlayer). */
static void buildAndDecode(int playerIdx, int obsFull,
                           struct NetPlayerState *own, struct NetObs *obs) {
    static uint8_t buf[4096];
    static struct PlayerSim ctx;
    struct NetWriter w;
    struct NetReader r;
    memset(&ctx, 0, sizeof(ctx));
    ctx.viewX_ = g_viewX_;
    ctx.viewY_ = g_viewY_;
    ctx.viewZ = (int16)g_viewZ;
    ctx.ourHead = g_ourHead;
    ctx.radarScopeRange = g_radarScopeRange;
    ctx.airTargetLock = g_airTargetLock;
    ctx.groundTargetLock = g_groundTargetLock;
    ctx.closestThreatIndex = g_closestThreatIndex;
    ctx.nearestThreatRange = g_nearestThreatRange;
    nwInit(&w, buf, sizeof(buf));
    netObsBuild(&w, &ctx, playerIdx, WORLD_OBJS, obsFull, 0xDEADBEEF);
    CHECK(!w.overflow);
    nrInit(&r, buf, w.len);
    CHECK(decObs(&r, own, obs));
}

/* Pilot entitlement: a moving remote player on the scope is a contact; the
 * one behind the scope's region is not; the observer's own parked object
 * never is. */
static void test_obs_scope_filter(void) {
    struct NetPlayerState own;
    struct NetObs obs;
    int i;
    setupWorld();
    parkPlayer(1, 0x4000, 0x4000 - 0x300, 0x90, 100); /* ahead: on scope */
    parkPlayer(2, 0x4000, 0x4000 + 0x600, 0x90, 100); /* behind: off scope */
    parkPlayer(0, 0x4000, 0x3F00, 0x90, 100);         /* own parked slot */
    buildAndDecode(0, 0, &own, &obs);
    CHECK(own.mapX == 0x4000 && own.mapY == 0x4000);
    i = findContact(&obs, NET_ID_PLAYER_BASE + 1);
    CHECK(i >= 0);
    if (i >= 0) {
        CHECK(obs.contacts[i].kind == OBSK_PLAYER);
        CHECK(obs.contacts[i].range >= 0x2f0 && obs.contacts[i].range <= 0x310);
    }
    CHECK(findContact(&obs, NET_ID_PLAYER_BASE + 2) < 0);
    CHECK(findContact(&obs, NET_ID_PLAYER_BASE + 0) < 0);
}

/* Human radar shows stationary remote pilots, but not stationary world aircraft. */
static void test_obs_skips_stationary(void) {
    struct NetPlayerState own;
    struct NetObs obs;
    setupWorld();
    parkPlayer(1, 0x4000, 0x4000 - 0x300, 0x90, 0);
    g_simObjects[0] = g_simObjects[WORLD_OBJS + 1];
    g_simObjects[0].flags.b[1] = 0; /* stationary world aircraft */
    buildAndDecode(0, 0, &own, &obs);
    CHECK(findContact(&obs, NET_ID_PLAYER_BASE + 1) >= 0);
    CHECK(findContact(&obs, 0) < 0);
}

/* --obs-full bypasses the scope test (privileged/omniscient). */
static void test_obs_full_mode(void) {
    struct NetPlayerState own;
    struct NetObs obs;
    setupWorld();
    parkPlayer(2, 0x4000, 0x4000 + 0x600, 0x90, 100); /* off scope */
    buildAndDecode(0, 1, &own, &obs);
    CHECK(findContact(&obs, NET_ID_PLAYER_BASE + 2) >= 0);
}

/* RWR entitlement: a threat missile (slots 0-7, targetPlayer = engaged
 * victim) shows up even outside the scope; someone else's off-scope threat
 * doesn't. Player-fired missiles (slots 8+, targetPlayer = shooter) are
 * inbound only when their targetLock is the observer's parked object. */
static void test_obs_inbound_missile(void) {
    struct NetPlayerState own;
    struct NetObs obs;
    setupWorld();
    g_projectiles[3].ttl = 100;
    g_projectiles[3].fineX = 0x4000 << 5;
    g_projectiles[3].fineY = (0x4000 + 0x600) << 5; /* behind ownship */
    g_projectiles[3].alt = 0x90;
    g_projectiles[3].targetPlayer = 0; /* threat shot inbound at player 0 */
    g_projectiles[4].ttl = 100;
    g_projectiles[4].fineX = 0x4000 << 5;
    g_projectiles[4].fineY = (0x4000 + 0x700) << 5;
    g_projectiles[4].alt = 0x90;
    g_projectiles[4].targetPlayer = 1; /* threat shot inbound at someone else */
    /* my own outgoing missile (slot 9, targetPlayer = me the shooter):
     * off scope, must NOT be flagged inbound */
    g_projectiles[9].ttl = 100;
    g_projectiles[9].fineX = 0x4000 << 5;
    g_projectiles[9].fineY = (0x4000 + 0x800) << 5;
    g_projectiles[9].alt = 0x90;
    g_projectiles[9].targetPlayer = 0;
    /* enemy missile (slot 10, shooter = player 1) locked on my parked
     * object: RWR-inbound even off scope */
    parkPlayer(0, 0x4000, 0x3F00, 0x90, 100); /* my parked slot (WORLD_OBJS+0) */
    g_projectiles[10].ttl = 100;
    g_projectiles[10].fineX = 0x4000 << 5;
    g_projectiles[10].fineY = (0x4000 + 0x900) << 5;
    g_projectiles[10].alt = 0x90;
    g_projectiles[10].targetPlayer = 1;
    g_projectiles[10].targetLock = WORLD_OBJS + 0;
    buildAndDecode(0, 0, &own, &obs);
    {
        int i = findContact(&obs, NET_ID_PROJECTILE_BASE + 3);
        CHECK(i >= 0);
        if (i >= 0) {
            CHECK(obs.contacts[i].kind == OBSK_MISSILE);
            CHECK(obs.contacts[i].flags & OBSF_INBOUND);
        }
    }
    CHECK(findContact(&obs, NET_ID_PROJECTILE_BASE + 4) < 0);
    CHECK(findContact(&obs, NET_ID_PROJECTILE_BASE + 9) < 0); /* own shot: no scope, no flag */
    {
        int i = findContact(&obs, NET_ID_PROJECTILE_BASE + 10);
        CHECK(i >= 0);
        if (i >= 0)
            CHECK(obs.contacts[i].flags & OBSF_INBOUND);
    }
}

/* Tacmap entitlement + RWR block: live site on scope is a contact carrying
 * ACTIVE/THREAT flags and becomes threatId; destroyed sites are omitted. */
static void test_obs_sites_and_threat(void) {
    struct NetPlayerState own;
    struct NetObs obs;
    int i;
    setupWorld();
    g_planeCount = 3;
    g_planeTable.planes[0].mapX = 0x4000;
    g_planeTable.planes[0].mapY = 0x4000 - 0x300;
    g_planeTable.planes[0].flags = 0x201;
    g_planeTable.planes[0].active = 1;
    g_planeTable.planes[1].mapX = 0x4000;
    g_planeTable.planes[1].mapY = 0x4000 + 0x600; /* off scope */
    g_planeTable.planes[1].flags = 0x201;
    g_planeTable.planes[2].mapX = 0x4000;
    g_planeTable.planes[2].mapY = 0x4000 - 0x380;
    g_planeTable.planes[2].flags = 0x80 | 0x201; /* destroyed */
    g_closestThreatIndex = 0;
    g_nearestThreatRange = 0x300;
    buildAndDecode(0, 0, &own, &obs);
    CHECK(obs.threatId == (int16)(NET_ID_MAPTARGET_BASE + 0));
    CHECK(obs.threatRange == 0x300);
    i = findContact(&obs, NET_ID_MAPTARGET_BASE + 0);
    CHECK(i >= 0);
    if (i >= 0) {
        CHECK(obs.contacts[i].kind == OBSK_SITE);
        CHECK(obs.contacts[i].flags & OBSF_ACTIVE);
        CHECK(obs.contacts[i].flags & OBSF_THREAT);
    }
    CHECK(findContact(&obs, NET_ID_MAPTARGET_BASE + 1) < 0);
    CHECK(findContact(&obs, NET_ID_MAPTARGET_BASE + 2) < 0);
    CHECK(obs.missionStatus == 1);
    CHECK(obs.stateHash == 0xDEADBEEF);
}

/* ---- tryPlayerFire: per-player tracer slots ----------------------------- */

void tryPlayerFire(void); /* egframe.c segment fn (no header, same as f15world.c) */

/* Server gun rounds claim any FREE slot in the shared pool (stamped with
 * the shooter). A trigger release or a later shot never deletes an
 * airborne round, and other players' passes clear nothing - rounds keep
 * full flight lifetime for longer-range hits. SP keeps the legacy
 * rotating slot + release sweep. */
static void test_bullet_pool_lifetime(void) {
    static struct GameComm comm; /* localFireButton reads commData->setupUseJoy */
    int i;
    setupWorld();
    commData = &comm;
    memset(&comm, 0, sizeof(comm));
    g_bulletTrackCount = F15_MAX_PLAYERS * 2; /* server pool: 16 slots */
    memset(bulletTracks, 0, sizeof(bulletTracks));
    g_gunAmmo = 500;
    g_ejectState = 0;
    g_ourHead = 0x1000; /* nonzero yaw so the tracer's posX liveness is set */

    /* shot 1 claims the first free slot, stamped owner 0 */
    frameTick = 1;
    g_residentPlayer = 0;
    g_axisInputAccum[0] = 1;
    tryPlayerFire();
    CHECK(bulletTracks[0].posX != 0 || bulletTracks[0].posY != 0);
    CHECK(bulletTracks[0].targetPlayer == 0);

    /* trigger release on a later odd tick: the airborne round survives */
    frameTick = 3;
    g_axisInputAccum[0] = 0;
    tryPlayerFire();
    CHECK(bulletTracks[0].posX != 0 || bulletTracks[0].posY != 0);

    /* second shot lands in a DIFFERENT slot; the first keeps flying */
    frameTick = 5;
    g_axisInputAccum[0] = 1;
    tryPlayerFire();
    CHECK(bulletTracks[0].posX != 0 || bulletTracks[0].posY != 0);
    CHECK(bulletTracks[1].posX != 0 || bulletTracks[1].posY != 0);
    CHECK(bulletTracks[1].targetPlayer == 0);

    /* another player's idle pass clears nothing */
    frameTick = 7;
    g_residentPlayer = 2;
    g_axisInputAccum[0] = 0;
    tryPlayerFire();
    for (i = 0; i < 2; i++)
        CHECK(bulletTracks[i].posX != 0 || bulletTracks[i].posY != 0);

    /* saturated pool: all slots busy -> round-robin overwrite via the
     * world cursor, so same-tick shooters get DIFFERENT slots and nobody's
     * new shot is erased by a later player's pass */
    for (i = 0; i < g_bulletTrackCount; i++)
        if (!bulletTracks[i].posX) {
            bulletTracks[i].posX = 1;
            bulletTracks[i].targetPlayer = 3;
        }
    g_bulletPoolCursor = 0;
    frameTick = 9;
    /* an IDLE player must not advance the shared cursor - allocation runs
     * after the trigger/ammo checks, so a passing pass can't reshuffle which
     * airborne round a later shooter evicts */
    g_residentPlayer = 4;
    g_axisInputAccum[0] = 0;
    tryPlayerFire();
    CHECK(g_bulletPoolCursor == 0);
    /* out of ammo: also no allocation */
    g_gunAmmo = 0;
    g_axisInputAccum[0] = 1;
    tryPlayerFire();
    CHECK(g_bulletPoolCursor == 0);
    g_gunAmmo = 500;
    g_residentPlayer = 0;
    g_axisInputAccum[0] = 1;
    tryPlayerFire(); /* cursor 0 -> slot 0 */
    CHECK(bulletTracks[0].targetPlayer == 0);
    /* player 2 fires the same tick: cursor 1 -> slot 1, slot 0 survives */
    g_residentPlayer = 2;
    tryPlayerFire();
    CHECK(bulletTracks[1].targetPlayer == 2);
    CHECK(bulletTracks[0].targetPlayer == 0);
    /* negative frameTick: unsigned cursor still yields a valid slot */
    frameTick = -3;
    g_residentPlayer = 3;
    tryPlayerFire(); /* cursor 2 -> slot 2 */
    CHECK(bulletTracks[2].targetPlayer == 3);
    CHECK(bulletTracks[0].targetPlayer == 0);
    CHECK(bulletTracks[1].targetPlayer == 2);

    /* free->full transition: ONE free slot remains and the shared cursor
     * points AT it. Player 5's free-scan claims it; player 6's pass finds
     * the pool saturated and the cursor lands on that same slot - the
     * fresh mask must skip it so the fallback evicts an OLD round, not
     * the shot that hasn't left the barrel yet (was: owner 5 -> 6 in the
     * same slot, same tick). */
    memset(bulletTracks, 0, sizeof(bulletTracks));
    for (i = 0; i < g_bulletTrackCount; i++) {
        bulletTracks[i].posX = 1;
        bulletTracks[i].targetPlayer = 3;
    }
    bulletTracks[4].posX = 0;          /* the single free slot */
    g_bulletPoolCursor = 4;            /* cursor aimed right at it */
    g_bulletFreshMask = 0;
    g_bulletFreshTick = 0;
    frameTick = 13;
    g_residentPlayer = 5;
    g_axisInputAccum[0] = 1;
    tryPlayerFire();                   /* free-scan -> slot 4 */
    CHECK(bulletTracks[4].targetPlayer == 5);
    g_residentPlayer = 6;
    tryPlayerFire();                   /* saturated: slot 4 is fresh -> skip */
    CHECK(bulletTracks[4].targetPlayer == 5); /* NOT evicted same-tick */
    CHECK(bulletTracks[5].targetPlayer == 6); /* fell through to slot 5 */
    CHECK(g_bulletPoolCursor == 6);    /* consumed both cursor steps */

    /* SP mode (resident -1): rotating slot + release sweep unchanged */
    g_residentPlayer = -1;
    memset(bulletTracks, 0, sizeof(bulletTracks));
    frameTick = 11; /* rotating slot (11>>1)%16 = 5 */
    g_axisInputAccum[0] = 1;
    tryPlayerFire();
    CHECK(bulletTracks[5].posX != 0 || bulletTracks[5].posY != 0);
    frameTick = 43; /* rotation back to slot 5: release sweep clears it */
    g_axisInputAccum[0] = 0;
    tryPlayerFire();
    CHECK(bulletTracks[5].posX == 0);
    /* SP negative frameTick: the rotating index is sign-normalized, so a
     * shot during the negative half of the tick cycle stays in bounds */
    memset(bulletTracks, 0, sizeof(bulletTracks));
    frameTick = -3; /* (int16)(-3>>1) = -2 -> normalized slot 14 */
    g_axisInputAccum[0] = 1;
    tryPlayerFire();
    CHECK(bulletTracks[14].posX != 0 || bulletTracks[14].posY != 0);
    g_residentPlayer = -1;
}

/* ---- render interpolation: map coords stay in map space ------------------ */

void netRenderSnapCapture(void);
void netRenderApplyInterp(int64 num, int64 den);
void netRenderRestore(void);

/* Parked remote players carry posY in map space (0x8000-(ViewY>>5)), NOT
 * wy>>5 - the 0x01000000 render-Y offset doesn't survive the uint16
 * truncation. An interpolated render between snapshots must keep mapY
 * stable for the whole frame (was: 0x4000 -> 0xc000 mid-render, breaking
 * the 3D range check and radar projection until the restore pass). */
static void test_interp_preserves_map_coords(void) {
    int slot;
    setupWorld();
    g_groundUnitCount = 20; /* sweep covers the parked slot */
    slot = 19;
    memset(&g_simObjects[slot], 0, sizeof(g_simObjects[slot]));
    /* the netPlayerPublishObject layout: render worldY inverted, map posY
     * in map space */
    g_simObjects[slot].worldX = 0x12345000;
    g_simObjects[slot].worldY = 0x01000000 - 0x80000; /* ViewY = 0x80000 */
    g_simObjects[slot].posX = 0x2468;
    g_simObjects[slot].posY = 0x4000;
    g_simObjects[slot].alt = 500;
    g_simObjects[slot].flags.b[0] = 2; /* alive */
    g_simObjects[slot].flags.b[1] = SIMFLAG_B1_REMOTE_PLAYER;

    /* two identical captures = two consecutive identical snapshots */
    netRenderSnapCapture();
    netRenderSnapCapture();
    /* mid-frame tween: map coords must hold their own convention */
    netRenderApplyInterp(1, 2);
    CHECK(g_simObjects[slot].posY == 0x4000);
    CHECK(g_simObjects[slot].posX == 0x2468);
    /* restore returns the authoritative fields */
    netRenderRestore();
    CHECK(g_simObjects[slot].posY == 0x4000);
    CHECK(g_simObjects[slot].worldY == 0x01000000 - 0x80000);

    /* map-boundary crossing wraps the short way: 0xffe0 -> 0x0020
     * (delta +0x40 across 0x10000), midpoint lands near 0, not ~0x8000 */
    g_simObjects[slot].posY = 0xffe0;
    netRenderSnapCapture();
    g_simObjects[slot].posY = 0x0020;
    netRenderSnapCapture();
    netRenderApplyInterp(1, 2);
    {
        int16 mid = (int16)g_simObjects[slot].posY;
        CHECK(mid <= 0x20 && mid >= -0x20);
    }
    netRenderRestore();
    CHECK(g_simObjects[slot].posY == 0x0020);
}

/* ---- frameThreatScan: per-player scope-sweep timer ----------------------- */

/* g_scopeSweepTimer is a per-pilot RWR debounce: it must tick down inside
 * each player's OWN pass (frameThreatScan), not once under whichever ctx
 * the world pass happens to hold. One pilot's active sweep can no longer
 * suppress a site engaging another pilot. */
static void test_scope_timer_per_player(void) {
    static struct PlayerSim a, b;
    setupWorld();
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    g_scopeSweepTimer = 5;
    playerSwapOut(&a); /* a.scopeSweepTimer = 5 */
    g_scopeSweepTimer = 12;
    playerSwapOut(&b); /* b.scopeSweepTimer = 12 */
    frameTick = 1;
    playerSwapIn(&a);
    g_residentPlayer = 0;
    frameThreatScan();
    playerSwapOut(&a);
    CHECK(a.scopeSweepTimer == 4);
    playerSwapIn(&b);
    g_residentPlayer = 1;
    frameThreatScan();
    playerSwapOut(&b);
    CHECK(b.scopeSweepTimer == 11);
    CHECK(a.scopeSweepTimer == 4); /* B's pass didn't touch A's timer */
    g_residentPlayer = -1;
}

/* ---- netSnapApply: atomic decode/validate/commit ------------------------- */

static int16 s_snapDamageSeq;
static int buildSnap(uint8_t *buf, size_t cap, int16 ft) {
    struct NetWriter w;
    struct PlayerSim ctx;
    int ids[1] = {0};
    memset(&ctx, 0, sizeof(ctx));
    frameTick = ft;
    ctx.ViewX = 0x12345;
    ctx.viewZ = 0x80;
    ctx.ejectState = 0;
    ctx.damageSeq = s_snapDamageSeq;
    nwInit(&w, buf, cap);
    netSnapBuild(&w, &ctx, ids, 1, 0xABCD);
    return (int)w.len;
}

/* A truncated snapshot must fail WITHOUT touching any live global. */
static void test_snap_truncated_atomic(void) {
    uint8_t buf[8192];
    struct NetReader r;
    int len;
    setupWorld();
    len = buildSnap(buf, sizeof(buf), 500);
    g_ViewX = 0x777;
    g_missionTick = 42;
    frameTick = 123; /* buildSnap set it to 500 - reset sentinel for apply */
    nrInit(&r, buf, (size_t)len - 2);
    CHECK(netSnapApply(&r, 0) == 0);
    CHECK(g_ViewX == 0x777);      /* own block not committed */
    CHECK(g_missionTick == 42);   /* world fields not committed */
    CHECK(g_simObjects[0].posX == 0);
    CHECK(frameTick == 123);      /* tail never applied */
}

/* Out-of-order/repeated delivery must not roll state back. */
static void test_snap_stale_rejected(void) {
    uint8_t buf[8192];
    struct NetReader r;
    int len;
    setupWorld();
    len = buildSnap(buf, sizeof(buf), 500);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(frameTick == 500);
    /* identical resend: same frameTick -> stale */
    g_missionTick = 99;
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 0);
    CHECK(g_missionTick == 99); /* rejected snap didn't re-commit */
    /* older tick: also stale */
    len = buildSnap(buf, sizeof(buf), 499);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 0);
    /* newer tick: accepted and commits */
    len = buildSnap(buf, sizeof(buf), 501);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(frameTick == 501);
}

/* Damage events are numbered (damageSeq), not level-triggered: the HUD
 * shake fires exactly once per new count - a repeated seq on a later
 * snapshot must NOT re-fire it. */
static void test_damage_seq_edge_trigger(void) {
    uint8_t buf[8192];
    struct NetReader r;
    int len;
    setupWorld();
    /* runs after test_snap_negative_ticks: its last commit was -32768, so
     * modularly-later ticks are -32767 up (the staleness gate is a file
     * static shared by every snap test in this binary) */
    s_snapDamageSeq = 1;
    len = buildSnap(buf, sizeof(buf), -32767);
    g_damageTakenFlag = 0;
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(g_damageTakenFlag == 1); /* first event fires the shake */
    /* the HUD consumes the latch; next snapshot at the SAME seq: no refire */
    g_damageTakenFlag = 0;
    len = buildSnap(buf, sizeof(buf), -32766);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(g_damageTakenFlag == 0);
    /* a second damage event (seq 2) fires once more */
    s_snapDamageSeq = 2;
    len = buildSnap(buf, sizeof(buf), -32765);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(g_damageTakenFlag == 1);
    s_snapDamageSeq = 0;
}

/* Structural validation: duplicate player ids / out-of-range entity ids are
 * rejected before any commit. */
static void test_snap_invalid_ids_rejected(void) {
    uint8_t buf[8192];
    struct NetWriter w;
    struct NetReader r;
    struct PlayerSim ctx;
    int i;
    setupWorld();
    memset(&ctx, 0, sizeof(ctx));
    /* hand-build: two player blocks with the SAME id */
    nwInit(&w, buf, sizeof(buf));
    nwU8(&w, 2);
    nwU8(&w, 1);
    netEncPlayerFromCtx(&w, &ctx);
    nwU8(&w, 1);
    netEncPlayerFromCtx(&w, &ctx);
    nwU8(&w, 0); /* 0 sim objects */
    nwU8(&w, 0); /* 0 projectiles */
    nwU8(&w, 0); /* 0 map targets */
    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        nwU16(&w, 0); nwU16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    }
    nwI16(&w, 7);            /* missionTick */
    nwI16(&w, 1);            /* missionStatus */
    frameTick = 600;
    nwI16(&w, frameTick);
    for (i = 0; i < F15_WAYPOINTS; i++) {
        nwU16(&w, 0); nwU16(&w, 0);
    }
    for (i = 0; i < 2; i++) {
        nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    }
    nwU32(&w, 0xABCD);
    g_missionTick = 55;
    nrInit(&r, buf, w.len);
    CHECK(netSnapApply(&r, 0) == 0);
    CHECK(g_missionTick == 55); /* nothing committed */

    /* an out-of-range object id likewise */
    nwInit(&w, buf, sizeof(buf));
    nwU8(&w, 0);             /* 0 players */
    nwU8(&w, 1);             /* 1 sim object, id out of range */
    nwU32(&w, F15_MAX_SIM_OBJECTS); /* id = one past the array */
    nwI32(&w, 0); nwI32(&w, 0);
    nwU16(&w, 0); nwU16(&w, 0);
    nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    nwU16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    nwU8(&w, 0); nwU8(&w, 0);
    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        nwU16(&w, 0); nwU16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    }
    nwI16(&w, 8); nwI16(&w, 1);
    frameTick = 601;
    nwI16(&w, frameTick);
    for (i = 0; i < F15_WAYPOINTS; i++) {
        nwU16(&w, 0); nwU16(&w, 0);
    }
    for (i = 0; i < 2; i++) {
        nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0); nwI16(&w, 0);
    }
    nwU32(&w, 0xABCD);
    nrInit(&r, buf, w.len);
    CHECK(netSnapApply(&r, 0) == 0);
    CHECK(g_missionTick == 55);
}

/* frameTick is signed: the staleness gate must keep working across the
 * negative half of the cycle (was: s_lastFrameTick >= 0 meant "unset",
 * so every negative tick looked like a first snapshot). */
static void test_snap_negative_ticks(void) {
    uint8_t buf[8192];
    struct NetReader r;
    int len;
    setupWorld();
    /* first negative tick commits (wrap-ambiguous vs the previous 601:
     * -32760-601 wraps positive, so it's "newer" modularly) */
    len = buildSnap(buf, sizeof(buf), -32760);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(frameTick == -32760);
    /* resend of the same negative tick: stale, must not re-commit */
    g_missionTick = 77;
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 0);
    CHECK(g_missionTick == 77);
    /* older negative tick: -32761 -> rejected (the reported repro) */
    len = buildSnap(buf, sizeof(buf), -32761);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 0);
    /* newer negative tick accepted */
    len = buildSnap(buf, sizeof(buf), -32759);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(frameTick == -32759);
    /* the wrap itself: 32767 -> -32768 is +1 modularly -> accepted.
     * (modular order means only ~half the tick space is reachable in one
     * step, so walk up: -32759 -> 8 -> 32767 -> -32768) */
    len = buildSnap(buf, sizeof(buf), 8);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    len = buildSnap(buf, sizeof(buf), 32767);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    len = buildSnap(buf, sizeof(buf), -32768);
    nrInit(&r, buf, (size_t)len);
    CHECK(netSnapApply(&r, 0) == 1);
    CHECK(frameTick == -32768);
}

/* ---- delayed-camera ring: signed-wrap backfill --------------------------- */

void netViewRingPush(void); /* egsys.c */

/* netViewRingPush backfills missed ring ticks by lerping prev->cur. Across
 * the int16 wrap (32766 -> -32766) the modular gap is 4 but any signed
 * 't < frameTick' loop runs zero times; the fill must iterate by elapsed
 * steps and wrap each destination index. */
static void test_view_ring_wrap_backfill(void) {
    /* first push seeds every slot with pose A (tick 32766 -> slot 14) */
    frameTick = 32766;
    g_ourHead = 0;
    g_ourPitch = 0;
    g_ourRoll = 0;
    g_ViewX = 0;
    g_ViewY = 0;
    g_viewZ = 0;
    netViewRingPush();
    /* pose B lands +4 modularly at -32766 with a different worldX */
    frameTick = -32766;
    g_ViewX = 0x1000;
    netViewRingPush();
    /* the three missed ticks got 1/4, 2/4, 3/4 of the A->B lerp */
    CHECK(g_viewSnapshotRing[15].worldX == 0x400);  /* tick 32767 */
    CHECK(g_viewSnapshotRing[0].worldX == 0x800);   /* tick -32768 */
    CHECK(g_viewSnapshotRing[1].worldX == 0xC00);   /* tick -32767 */
    CHECK(g_viewSnapshotRing[2].worldX == 0x1000);  /* tick -32766 = B */
}

/* A gimbal flip rewrites heading/roll by 0x8000 while pitch reflects as the
 * airframe crosses vertical. Backfilled ring slots must snap the whole pose
 * (lerpPose policy): component-wise tweening invents -90deg poses between
 * the two representations. Covers both crossings and the int16 wrap. */
static void test_view_ring_gimbal_backfill(void) {
    /* prev test ended at -32766; seed a normal-representation base pose */
    frameTick = -32765;
    g_ourHead = 0x1000;
    g_ourPitch = 0x2000;
    g_ourRoll = 0;
    g_ViewX = g_ViewY = 0;
    g_viewZ = 0;
    netViewRingPush();
    /* gap=3, cur is the flipped representation (h/r shifted by 0x8000) */
    frameTick = -32762;
    g_ourHead = -0x7000;
    g_ourPitch = 0x6000;
    g_ourRoll = -0x8000;
    netViewRingPush();
    /* missed ticks -32764 -> slot 4, -32763 -> slot 5: snap to cur pose */
    CHECK(g_viewSnapshotRing[4].heading == -0x7000 &&
          g_viewSnapshotRing[4].pitch == 0x6000 &&
          g_viewSnapshotRing[4].roll == -0x8000);
    CHECK(g_viewSnapshotRing[5].heading == -0x7000 &&
          g_viewSnapshotRing[5].pitch == 0x6000 &&
          g_viewSnapshotRing[5].roll == -0x8000);
    /* opposite crossing: flipped -> normal across another 3-tick gap */
    frameTick = -32761;
    netViewRingPush(); /* gap=1: only writes slot 7, keeps prev = flipped */
    frameTick = -32758;
    g_ourHead = 0x1000;
    g_ourPitch = 0x2000;
    g_ourRoll = 0;
    netViewRingPush();
    /* missed ticks -32760 -> slot 8, -32759 -> slot 9: snap back to normal */
    CHECK(g_viewSnapshotRing[8].heading == 0x1000 &&
          g_viewSnapshotRing[8].pitch == 0x2000 &&
          g_viewSnapshotRing[8].roll == 0);
    CHECK(g_viewSnapshotRing[9].heading == 0x1000 &&
          g_viewSnapshotRing[9].pitch == 0x2000 &&
          g_viewSnapshotRing[9].roll == 0);
    /* same flip but straddling the int16 wrap (gap 4: 32766 -> -32766) */
    frameTick = 32766;
    netViewRingPush(); /* big modular jump: just re-seeds, prev = normal */
    frameTick = -32766;
    g_ourHead = -0x7000;
    g_ourPitch = 0x6000;
    g_ourRoll = -0x8000;
    netViewRingPush();
    CHECK(g_viewSnapshotRing[15].heading == -0x7000 &&
          g_viewSnapshotRing[15].roll == -0x8000); /* 32767 */
    CHECK(g_viewSnapshotRing[0].heading == -0x7000 &&
          g_viewSnapshotRing[0].roll == -0x8000);  /* -32768 */
    CHECK(g_viewSnapshotRing[1].heading == -0x7000 &&
          g_viewSnapshotRing[1].roll == -0x8000);  /* -32767 */
    CHECK(g_viewSnapshotRing[2].heading == -0x7000 &&
          g_viewSnapshotRing[2].roll == -0x8000);  /* -32766 = cur */
}

/* ---- runner ------------------------------------------------------------ */

int main(void) {
    test_lock_acquires_remote();
    test_lock_skips_own_object();
    test_lock_skips_stationary();
    test_bullet_hits_remote();
    test_bullet_owner_filter();
    test_bullet_miss();
    test_projectile_owner_filter();
    test_obs_scope_filter();
    test_obs_skips_stationary();
    test_obs_full_mode();
    test_obs_inbound_missile();
    test_obs_sites_and_threat();
    test_bullet_pool_lifetime();
    test_interp_preserves_map_coords();
    test_scope_timer_per_player();
    test_snap_truncated_atomic();
    test_snap_stale_rejected();
    test_snap_invalid_ids_rejected();
    test_snap_negative_ticks();
    test_damage_seq_edge_trigger();
    test_view_ring_wrap_backfill();
    test_view_ring_gimbal_backfill();
    if (fails == 0) {
        printf("net_sim_tests: all pass\n");
        return 0;
    }
    printf("net_sim_tests: %d failure(s)\n", fails);
    return 1;
}
