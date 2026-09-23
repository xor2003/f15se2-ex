/*
 * netprobe.cpp - scripted headless client for the f15server acceptance gates.
 *
 * Drives the real wire protocol through the real GNS transport so the review
 * DoD checks (sync-step consumption, duplicate HELLO, join/leave, fault
 * injection) run against the unmodified server binary.
 *
 * Usage:
 *   netprobe --connect HOST:PORT --name X [--role N]
 *            [--loss PCT] [--rloss PCT] [--dup PCT] [--reorder MS] [--lag MS]
 *            -- <script tokens>
 *
 * Script tokens (executed in order):
 *   hello      send NETMSG_HELLO (reliable)
 *   input      send NETMSG_INPUT with any queued commands
 *   cmd:N      queue NetCmd N into the next input packet
 *   sleep:MS   pump the transport for MS, printing every event
 *   bye        send NETMSG_BYE (reliable)
 *   quit       stop
 *
 * Output lines are machine-grepable: "PROBE <kind> <fields>".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <thread>

#include "net/transport.h"
#include "net/protocol.h"
#include "net/codec.h"
#include "net/serialize.h"

static NetTransport *g_net;
static uint32_t g_seq;
static uint8_t g_pendCmds[F15_MAX_COMMANDS];
static int g_nPend;
static const char *g_name = "PROBE";
static int g_role = NET_ROLE_HUMAN;

static void sendHello(void) {
    static uint8_t buf[256];
    struct NetWriter w;
    struct NetHello h;
    memset(&h, 0, sizeof(h));
    h.protoVer = F15_NET_VERSION;
    h.role = (uint8_t)g_role;
    strncpy(h.name, g_name, sizeof(h.name) - 1);
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_HELLO, 0, 0);
    encHello(&w, &h);
    g_net->send(NET_PEER_SERVER, buf, w.len, NET_SEND_RELIABLE);
    printf("PROBE hello-sent name=%s role=%d\n", g_name, g_role);
    fflush(stdout);
}

static void sendInput(void) {
    static uint8_t buf[256];
    struct NetWriter w;
    struct NetInput in;
    memset(&in, 0, sizeof(in));
    in.clientSeq = ++g_seq;
    in.joyX = in.joyY = 0x80;
    in.nCmds = (uint8_t)g_nPend;
    memcpy(in.cmds, g_pendCmds, g_nPend);
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_INPUT, 0, 0);
    encInput(&w, &in);
    /* mirror the client rule: reliable when the packet carries commands */
    g_net->send(NET_PEER_SERVER, buf, w.len,
                g_nPend > 0 ? NET_SEND_RELIABLE : NET_SEND_UNRELIABLE);
    printf("PROBE input-sent seq=%u cmds=%d\n", g_seq, g_nPend);
    fflush(stdout);
    g_nPend = 0;
}

static void sendBye(void) {
    static uint8_t buf[64];
    struct NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_BYE, 0, 0);
    g_net->send(NET_PEER_SERVER, buf, w.len, NET_SEND_RELIABLE);
    printf("PROBE bye-sent\n");
    fflush(stdout);
}

static void printMsg(const uint8_t *msg, size_t len) {
    struct NetReader r;
    uint8_t type = 0;
    NetTick tick = 0;
    uint16_t payloadLen = 0;
    nrInit(&r, msg, len);
    if (!netMsgReadHeader(&r, &type, &tick, &payloadLen)) {
        printf("PROBE badmsg len=%u\n", (unsigned)len);
        return;
    }
    switch (type) {
    case NETMSG_HELLO_ACK: {
        struct NetHelloAck a;
        if (decHelloAck(&r, &a))
            printf("PROBE ack id=%u rate=%u flags=0x%x tick=%u\n",
                   a.playerId, a.tickRate, a.flags, (unsigned)a.serverTick);
        break;
    }
    case NETMSG_HELLO_NAK:
        printf("PROBE nak tick=%u\n", (unsigned)tick);
        break;
    case NETMSG_MISSION_SETUP:
        printf("PROBE setup tick=%u len=%u\n", (unsigned)tick, (unsigned)len);
        break;
    case NETMSG_SNAPSHOT:
        printf("PROBE snap tick=%u\n", (unsigned)tick);
        break;
    case NETMSG_EVENT: {
        struct NetEvent ev;
        if (decEvent(&r, &ev))
            printf("PROBE ev type=%u subj=%u obj=%u arg=%d text=\"%s\" tick=%u\n",
                   ev.eventType, (unsigned)ev.subject, (unsigned)ev.object,
                   ev.arg, ev.text, (unsigned)tick);
        break;
    }
    case NETMSG_MISSION_END:
        printf("PROBE mission-end tick=%u\n", (unsigned)tick);
        break;
    default:
        printf("PROBE msg type=%u tick=%u len=%u\n", type, (unsigned)tick,
               (unsigned)len);
        break;
    }
    fflush(stdout);
}

/* pump for `ms` milliseconds, printing every event */
static void pump(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        NetRecv ev;
        g_net->poll();
        while (g_net->recv(&ev)) {
            if (ev.kind == NET_EV_MESSAGE)
                printMsg(ev.msg, ev.len);
            else if (ev.kind == NET_EV_CONNECTED)
                printf("PROBE conn\n");
            else if (ev.kind == NET_EV_DISCONNECTED) {
                printf("PROBE disc\n");
                fflush(stdout);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    const char *addr = nullptr;
    int loss = 0, rloss = 0, dup = 0, reorder = 0, lag = 0;
    int i = 1;
    int scriptStart = -1;

    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--")) { scriptStart = i + 1; break; }
        if (!strcmp(argv[i], "--connect") && i + 1 < argc) addr = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) g_name = argv[++i];
        else if (!strcmp(argv[i], "--role") && i + 1 < argc) g_role = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) loss = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rloss") && i + 1 < argc) rloss = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dup") && i + 1 < argc) dup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reorder") && i + 1 < argc) reorder = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lag") && i + 1 < argc) lag = atoi(argv[++i]);
        else { fprintf(stderr, "netprobe: bad arg %s\n", argv[i]); return 2; }
    }
    if (!addr || scriptStart < 0) {
        fprintf(stderr, "usage: netprobe --connect HOST:PORT [opts] -- <script>\n");
        return 2;
    }

    g_net = createGnsTransport();
    if (!g_net || !g_net->connect(addr)) {
        fprintf(stderr, "netprobe: connect %s failed\n", addr);
        return 1;
    }
    if (loss || rloss || dup || reorder || lag) {
        gnsSetFakeNet(g_net, loss, rloss, dup, reorder, lag);
        printf("PROBE fakenet loss=%d rloss=%d dup=%d reorder=%d lag=%d\n",
               loss, rloss, dup, reorder, lag);
    }
    pump(200); /* let the connection come up */

    for (i = scriptStart; i < argc; i++) {
        const char *t = argv[i];
        if (!strcmp(t, "hello")) sendHello();
        else if (!strcmp(t, "input")) sendInput();
        else if (!strcmp(t, "bye")) sendBye();
        else if (!strcmp(t, "quit")) break;
        else if (!strncmp(t, "cmd:", 4)) {
            int c = atoi(t + 4);
            if (g_nPend < F15_MAX_COMMANDS)
                g_pendCmds[g_nPend++] = (uint8_t)c;
            printf("PROBE cmd-queued %d\n", c);
        } else if (!strncmp(t, "sleep:", 6)) {
            pump(atoi(t + 6));
        } else {
            fprintf(stderr, "netprobe: bad token %s\n", t);
            break;
        }
        pump(10);
    }

    g_net->shutdown();
    delete g_net;
    printf("PROBE done\n");
    return 0;
}
