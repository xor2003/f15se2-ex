/* player_ctx_tests.cpp - PlayerSim swap correctness + remote input semantics.
 *
 * Verifies (plan §6): every field in the ctx round-trips through globals, and
 * the remote input source serves at most one key per frameTick (matching the
 * legacy "read one key, flush the rest" behaviour).
 */
#include <stdio.h>
#include <string.h>

#include "comm.h"
#include "egdata.h"
#include "egplayer.h"
#include "inttype.h"
#include "struct.h"

static int fails;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            fails++;                                                 \
        }                                                            \
    } while (0)

/* Swap-out/in round-trip on a few sentinel fields. */
static void test_swap_roundtrip(void) {
    struct PlayerSim a, b;

    g_ViewX = 0x11223344;
    g_ourHead = 0x7F00;
    g_knots = 321;
    g_ejectState = 5;
    g_fuelRemaining = 4242;
    g_airTargetLock = 17;
    g_axisInputAccum[0] = 1;
    missleSpec[1].ammo = 3;
    strcpy(strBuf, "sentinel");
    commData->landingType = 7;

    playerSwapOut(&a);

    /* trash the globals */
    g_ViewX = 0;
    g_ourHead = 0;
    g_knots = 0;
    g_ejectState = 0;
    g_fuelRemaining = 0;
    g_airTargetLock = -1;
    g_axisInputAccum[0] = 0;
    missleSpec[1].ammo = 0;
    strBuf[0] = 0;
    commData->landingType = 0;

    playerSwapIn(&a);
    CHECK(g_ViewX == 0x11223344);
    CHECK(g_ourHead == 0x7F00);
    CHECK(g_knots == 321);
    CHECK(g_ejectState == 5);
    CHECK(g_fuelRemaining == 4242);
    CHECK(g_airTargetLock == 17);
    CHECK(g_axisInputAccum[0] == 1);
    CHECK(missleSpec[1].ammo == 3);
    CHECK(!strcmp(strBuf, "sentinel"));
    CHECK(commData->landingType == 7);

    /* two ctxs are independent */
    playerSwapOut(&b);
    CHECK(a.ViewX == b.ViewX && a.ourHead == b.ourHead && a.knots == b.knots);
}

/* Hash is stable across swap round-trips (plan §24). */
static void test_ctx_hash(void) {
    struct PlayerSim a, b;
    uint32_t ha, hb;

    playerInitCtx(&a);
    a.ViewX = 100;
    a.ourHead = 0x1234;
    a.knots = 300;
    ha = playerCtxHash(&a);

    playerSwapIn(&a);
    g_ourPitch = 999; /* mutate a hashed field */
    playerSwapOut(&b);
    hb = playerCtxHash(&b);

    CHECK(ha != hb);
    b.ourPitch = a.ourPitch; /* restore: hash the canonical region again */
    CHECK(playerCtxHash(&b) == ha);
}

/* Remote source serves at most one key per frameTick (the legacy sim reads a
 * single key then flushes). */
static void test_remote_input_one_key_per_tick(void) {
    struct RemoteInput r;
    remoteInputInit(&r);
    remoteInputPushKey(&r, 0x1e61, 0, 0); /* 'a' */
    remoteInputPushKey(&r, 0x266c, 0, 0); /* 'l' */
    remoteInputPushKey(&r, 0x1372, 0, 0); /* 'r' */

    frameTick = 100;
    CHECK(simInputKeyWaiting() == 0); /* local ops still installed */

    simInputSet(remoteInputOps(), &r);
    CHECK(simInputKeyWaiting() == 1);
    CHECK(simInputReadKey() == 0x1e61);
    /* same tick: queue is held back */
    CHECK(simInputKeyWaiting() == 0);
    frameTick = 101;
    CHECK(simInputKeyWaiting() == 1);
    CHECK(simInputReadKey() == 0x266c);
    frameTick = 102;
    CHECK(simInputReadKey() == 0x1372);
    CHECK(simInputKeyWaiting() == 0);
    frameTick = 103;
    CHECK(simInputKeyWaiting() == 0);

    /* axes/buttons pass through */
    remoteInputSetAxes(&r, 0x20, 0xE0, 3);
    {
        uint8_t jx = 0, jy = 0;
        simInputPollAxes(&jx, &jy);
        CHECK(jx == 0x20 && jy == 0xE0);
    }
    CHECK(simInputFireButton(0) == 1);
    CHECK(simInputFireButton(1) == 1);
    simInputReset();
    CHECK(simInputFireButton(0) == 0); /* local ops: no joystick in test env */
}

/* Overflow of the remote queue drops, never corrupts. */
static void test_remote_input_queue_full(void) {
    struct RemoteInput r;
    int i;
    remoteInputInit(&r);
    for (i = 0; i < REMOTE_KEY_QUEUE + 8; i++)
        remoteInputPushKey(&r, 0x100 + i, 0, 0);

    frameTick = 1;
    simInputSet(remoteInputOps(), &r);
    for (i = 0; i < REMOTE_KEY_QUEUE; i++) {
        CHECK(simInputKeyWaiting());
        CHECK(simInputReadKey() == 0x100 + i);
        frameTick++;
    }
    CHECK(simInputKeyWaiting() == 0);
    simInputReset();
}

/* Command-ack tags ride the queue to the sim-side read (NE_CMD_ACK feed). */
static struct RemoteInput *s_servedR;
static uint16 s_servedKey;
static uint32 s_servedSeq;
static uint8 s_servedIdx;
static void servedHook(struct RemoteInput *r, uint16 scan, uint32 seq,
                       uint8 idx) {
    s_servedR = r;
    s_servedKey = scan;
    s_servedSeq = seq;
    s_servedIdx = idx;
}
static void test_remote_input_served_hook(void) {
    struct RemoteInput r;
    remoteInputInit(&r);
    remoteInputPushKey(&r, 0x2049, 42, 1); /* cmd id (seq=42, idx=1) */
    g_remoteKeyServedHook = servedHook;
    s_servedR = 0;
    frameTick = 5;
    simInputSet(remoteInputOps(), &r);
    CHECK(simInputReadKey() == 0x2049);
    CHECK(s_servedR == &r);
    CHECK(s_servedKey == 0x2049 && s_servedSeq == 42 && s_servedIdx == 1);
    g_remoteKeyServedHook = 0;
    simInputReset();
}

int main(void) {
    /* commData/gameData are pointers that game_init() normally installs; the
     * swap path reads commData tail fields, so point them at our own blocks. */
    static struct GameComm cb;
    static struct Game gb;
    commData = &cb;
    gameData = &gb;

    test_swap_roundtrip();
    test_ctx_hash();
    test_remote_input_one_key_per_tick();
    test_remote_input_queue_full();
    test_remote_input_served_hook();
    if (fails == 0)
        printf("player_ctx_tests: OK\n");
    return fails ? 1 : 0;
}
