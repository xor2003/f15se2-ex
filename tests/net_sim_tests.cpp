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
#include "net/protocol.h"
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
    g_missionStatus = 1;
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

/* ---- runner ------------------------------------------------------------ */

int main(void) {
    test_lock_acquires_remote();
    test_lock_skips_own_object();
    test_lock_skips_stationary();
    test_bullet_hits_remote();
    test_bullet_owner_filter();
    test_bullet_miss();
    test_projectile_owner_filter();
    if (fails == 0) {
        printf("net_sim_tests: all pass\n");
        return 0;
    }
    printf("net_sim_tests: %d failure(s)\n", fails);
    return 1;
}
