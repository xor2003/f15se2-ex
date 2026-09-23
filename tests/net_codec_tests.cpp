/* net_codec_tests.cpp - wire codec round-trip + cmd mapping. */
#include <stdio.h>
#include <string.h>

#include "net/codec.h"
#include "net/commands.h"
#include "net/protocol.h"
#include "net/serialize.h"
#include "egkeys.h"

static int fails;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                             \
        }                                                        \
    } while (0)

static void test_header_roundtrip(void) {
    uint8_t buf[64];
    NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_INPUT, 0xDEADBEEF, 42);

    NetReader r;
    uint8_t type;
    NetTick tick;
    uint16_t plen;
    nrInit(&r, buf, w.len);
    CHECK(netMsgReadHeader(&r, &type, &tick, &plen));
    CHECK(type == NETMSG_INPUT);
    CHECK(tick == 0xDEADBEEF);
    CHECK(plen == 42);
}

static void test_header_rejects_garbage(void) {
    uint8_t buf[NET_MSG_HEADER_SIZE];
    NetReader r;
    uint8_t type;
    NetTick tick;
    uint16_t plen;
    memset(buf, 0xA5, sizeof(buf));
    nrInit(&r, buf, sizeof(buf));
    CHECK(!netMsgReadHeader(&r, &type, &tick, &plen));

    /* short buffer */
    nrInit(&r, buf, 4);
    CHECK(!netMsgReadHeader(&r, &type, &tick, &plen));
}

static void test_hello_roundtrip(void) {
    uint8_t buf[128];
    NetWriter w;
    NetReader r;
    NetHello a, b;
    memset(&a, 0, sizeof(a));
    a.protoVer = F15_NET_VERSION;
    a.role = NET_ROLE_AI;
    snprintf(a.name, sizeof(a.name), "trainer-01");
    nwInit(&w, buf, sizeof(buf));
    encHello(&w, &a);
    CHECK(!w.overflow);
    nrInit(&r, buf, w.len);
    CHECK(decHello(&r, &b));
    CHECK(b.protoVer == F15_NET_VERSION);
    CHECK(b.role == NET_ROLE_AI);
    CHECK(!strcmp(b.name, "trainer-01"));
}

static void test_input_roundtrip(void) {
    uint8_t buf[64];
    NetWriter w;
    NetReader r;
    NetInput a, b;
    memset(&a, 0, sizeof(a));
    a.clientSeq = 77;
    a.clientTick = 0x1234;
    a.lastSnapAck = 0x9999;
    a.joyX = 0x40;
    a.joyY = 0xC0;
    a.buttons = NB_GUN | NB_MISSILE;
    a.nCmds = 3;
    a.cmds[0] = NC_GEAR_TOGGLE;
    a.cmds[1] = NC_WEAPON_AMRAAM;
    a.cmds[2] = NC_TARGET_DESIGNATE;
    nwInit(&w, buf, sizeof(buf));
    encInput(&w, &a);
    nrInit(&r, buf, w.len);
    CHECK(decInput(&r, &b));
    CHECK(b.clientSeq == 77);
    CHECK(b.clientTick == 0x1234);
    CHECK(b.lastSnapAck == 0x9999);
    CHECK(b.joyX == 0x40 && b.joyY == 0xC0);
    CHECK(b.buttons == (NB_GUN | NB_MISSILE));
    CHECK(b.nCmds == 3);
    CHECK(b.cmds[0] == NC_GEAR_TOGGLE && b.cmds[2] == NC_TARGET_DESIGNATE);
}

static void test_input_cmd_clamp(void) {
    /* nCmds > F15_MAX_COMMANDS clamps rather than corrupting */
    uint8_t buf[64];
    NetWriter w;
    NetReader r;
    NetInput a, b;
    memset(&a, 0, sizeof(a));
    nwInit(&w, buf, sizeof(buf));
    encInput(&w, &a);
    /* corrupt the nCmds byte in the payload */
    buf[15] = 0xFF; /* nCmds: after seq/tick/ack (12) + joyX/joyY/buttons (3) */
    nrInit(&r, buf, w.len);
    CHECK(decInput(&r, &b));
    CHECK(b.nCmds <= F15_MAX_COMMANDS);
}

static void test_event_roundtrip(void) {
    uint8_t buf[128];
    NetWriter w;
    NetReader r;
    NetEvent a, b;
    memset(&a, 0, sizeof(a));
    a.eventType = NE_HUD_MESSAGE;
    a.subject = 0x200;
    a.object = 5;
    a.arg = -3;
    snprintf(a.text, sizeof(a.text), "Brakes on");
    nwInit(&w, buf, sizeof(buf));
    encEvent(&w, &a);
    nrInit(&r, buf, w.len);
    CHECK(decEvent(&r, &b));
    CHECK(b.eventType == NE_HUD_MESSAGE);
    CHECK(b.subject == 0x200 && b.object == 5 && b.arg == -3);
    CHECK(!strcmp(b.text, "Brakes on"));
}

static void test_cmd_scan_mapping(void) {
    /* every defined command maps to a nonzero scan and back */
    int i;
    for (i = 1; i < NC__COUNT; i++) {
        uint16_t scan = netCmdToScan((uint8_t)i);
        CHECK(scan != 0);
        CHECK(netScanToCmd(scan) == i);
    }
    CHECK(netCmdToScan(NC_GEAR_TOGGLE) == SCAN_L);
    CHECK(netCmdToScan(NC_GUN_FIRE) == SCAN_BACKSPACE);
    CHECK(netCmdToScan(NC_MISSILE_FIRE) == SCAN_ENTER);
    CHECK(netScanToCmd(SCAN_MINUS) == NC_THROTTLE_DOWN);
    CHECK(netScanToCmd(0xFFFF) == NC_NONE);
    /* local-only classification */
    CHECK(netCmdIsLocalOnly(NC_VIEW_COCKPIT));
    CHECK(netCmdIsLocalOnly(NC_PAUSE));
    CHECK(!netCmdIsLocalOnly(NC_GEAR_TOGGLE));
    CHECK(!netCmdIsLocalOnly(NC_WEAPON_AMRAAM));
}

static void test_player_state_roundtrip(void) {
    uint8_t buf[256];
    NetWriter w;
    NetReader r;
    NetPlayerState a, b;
    memset(&a, 0x5A, sizeof(a)); /* any garbage: encode must write every field */
    memset(&b, 0x5A, sizeof(b)); /* same fill so struct padding can't false-fail */
    nwInit(&w, buf, sizeof(buf));
    encPlayerState(&w, &a);
    CHECK(!w.overflow);
    nrInit(&r, buf, w.len);
    decPlayerState(&r, &b);
    CHECK(!r.underrun);
    CHECK(r.pos == w.len); /* decoder consumed exactly the encoded bytes */
    CHECK(!memcmp(&a, &b, sizeof(a)));
}

static void test_endianness(void) {
    /* wire must be LE regardless of host */
    uint8_t buf[8];
    NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    nwU16(&w, 0x1234);
    CHECK(buf[0] == 0x34 && buf[1] == 0x12);
    nwU32(&w, 0xAABBCCDD);
    CHECK(buf[2] == 0xDD && buf[5] == 0xAA);
}

int main(void) {
    test_header_roundtrip();
    test_header_rejects_garbage();
    test_hello_roundtrip();
    test_input_roundtrip();
    test_input_cmd_clamp();
    test_event_roundtrip();
    test_cmd_scan_mapping();
    test_player_state_roundtrip();
    test_endianness();
    if (fails == 0)
        printf("net_codec_tests: OK\n");
    return fails ? 1 : 0;
}
