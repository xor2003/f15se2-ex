/*
 * netclient.cpp - networked client mode for f15se2-ex (plan §9).
 *
 * Connects to f15server over GNS, sends NetInput (semantic commands + stick
 * axes - never positions), applies authoritative snapshots into the egdata
 * globals, and renders through the stock renderFrame/HUD path. Presentation-
 * only commands (views, map zoom, detail) are applied locally as well as sent.
 *
 *   f15se2-ex --connect host:port [--name PILOT]
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "egcode.h"
#include "egdata.h"
#include "egframe.h"
#include "egkeys.h"
#include "egplayer.h"
#include "egtacmap.h"
#include "egtypes.h"
#include "gfx.h"
#include "input.h"
#include "inttype.h"
#include "joystick.h"
#include "slot.h"
#include "struct.h"

#include "codec.h"
#include "commands.h"
#include "snapshot.h"
#include "transport.h"

/* no-header decls (C++ linkage, matching the headers like the rest of the
 * tree - everything compiles as C++ here) */
void game_init(int16 showIntro);            /* f15.c */
void resetMissionRuntimeState(void);        /* egmain.c */
void drawCockpit(void);                     /* egmain.c */
void renderHudFrame(int unused);            /* egsys.c */
void renderFrame(void);                     /* egdraw.c */
bool setGamePath(const char *path);         /* file_io.c */
bool verifyGameAssets();                    /* file_io.c */

static NetTransport *g_net;
static int g_myPlayerId = -1;
static int g_missionOver;
static int16 g_landingType = 3;
static uint32_t g_clientSeq;

static void sendMsg(uint8_t type, NetTick tick, const void *payload,
                    size_t len, int reliable) {
    uint8_t buf[NET_MSG_HEADER_SIZE + 4096];
    struct NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, type, tick, (uint16_t)len);
    nwBytes(&w, payload, len);
    g_net->send(NET_PEER_SERVER, buf, w.len,
                reliable ? NET_SEND_RELIABLE : NET_SEND_UNRELIABLE);
}

/* ---- event application (NE_* -> presentation) ---- */

static void applyEvent(const struct NetEvent *ev) {
    switch (ev->eventType) {
    case NE_HUD_MESSAGE:
        hudMessage(ev->text);
        break;
    case NE_TIMED_MESSAGE: {
        char tmp[80];
        snprintf(tmp, sizeof(tmp), "%s", ev->text);
        setTimedMessage(tmp);
        break;
    }
    case NE_SOUND:
        audio_playSound(ev->arg & 0xFF);
        break;
    case NE_VOICE_CUE:
        playVoiceCue(ev->arg);
        break;
    case NE_MISSION_END:
        g_missionOver = 1;
        g_landingType = ev->arg;
        break;
    default:
        break;
    }
}

/* ---- handshake: HELLO -> HELLO_ACK + MISSION_SETUP ---- */

static int doHandshake(const char *name) {
    struct NetHello h;
    memset(&h, 0, sizeof(h));
    h.protoVer = F15_NET_VERSION;
    h.role = NET_ROLE_HUMAN;
    snprintf(h.name, sizeof(h.name), "%s", name);
    {
        uint8_t buf[NET_MSG_HEADER_SIZE + 64];
        struct NetWriter w;
        nwInit(&w, buf, sizeof(buf));
        netMsgWriteHeader(&w, NETMSG_HELLO, 0, 0);
        encHello(&w, &h);
        g_net->send(NET_PEER_SERVER, buf, w.len, NET_SEND_RELIABLE);
    }
    /* wait for ACK + MISSION_SETUP (up to ~5 s) */
    {
        uint64_t deadline = SDL_GetTicksNS() + 10000000000ULL;
        int gotAck = 0, gotSetup = 0;
        while (SDL_GetTicksNS() < deadline && !(gotAck && gotSetup)) {
            NetRecv ev;
            g_net->poll();
            while (g_net->recv(&ev)) {
                struct NetReader r;
                uint8_t type;
                NetTick tick;
                uint16_t plen;
                if (ev.kind == NET_EV_DISCONNECTED)
                    return 0;
                if (ev.kind != NET_EV_MESSAGE)
                    continue;
                nrInit(&r, ev.msg, ev.len);
                if (!netMsgReadHeader(&r, &type, &tick, &plen))
                    continue;
                if (type == NETMSG_HELLO_ACK) {
                    struct NetHelloAck ack;
                    if (decHelloAck(&r, &ack)) {
                        g_myPlayerId = ack.playerId;
                        gotAck = 1;
                    }
                } else if (type == NETMSG_HELLO_NAK) {
                    return 0;
                } else if (type == NETMSG_MISSION_SETUP) {
                    if (netSetupApply(&r))
                        gotSetup = 1;
                }
            }
            SDL_DelayNS(2000000);
        }
        return gotAck && gotSetup;
    }
}

/* ---- local input -> NetInput ---- */

static void gatherInput(struct NetInput *in) {
    uint8_t jx = 0x80, jy = 0x80;
    memset(in, 0, sizeof(*in));
    in->clientSeq = ++g_clientSeq;

    input_setMode(INPUT_MODE_FLIGHT);
    input_pumpEvents();
    /* drain the key ring -> semantic commands; local-only keys also drive the
     * local renderer's view/zoom state directly. */
    while (input_keyWaiting() && in->nCmds < F15_MAX_COMMANDS) {
        uint16_t scan = input_readKey();
        uint8_t cmd = netScanToCmd(scan);
        if (cmd == NC_NONE)
            continue;
        in->cmds[in->nCmds++] = cmd;
        if (netCmdIsLocalOnly(cmd))
            keyDispatch(scan);
    }
    simInputPollAxes(&jx, &jy);
    in->joyX = jx;
    in->joyY = jy;
    if (commData->setupUseJoy) {
        if (misc_readJoystick(0))
            in->buttons |= NB_GUN;
        if (misc_readJoystick(1))
            in->buttons |= NB_MISSILE;
    }
}

/* ---- main ---- */

int netClientMain(const char *hostPort, const char *name) {
    if (!verifyGameAssets()) {
        fprintf(stderr, "netclient: game assets not found\n");
        return 1;
    }
    game_init(0);
    joy_init();

    g_net = createGnsTransport();
    if (!g_net || !g_net->connect(hostPort)) {
        fprintf(stderr, "netclient: connect failed\n");
        return 1;
    }
    if (!doHandshake(name)) {
        fprintf(stderr, "netclient: handshake failed\n");
        return 1;
    }

    /* world data applied by netSetupApply inside the handshake; now load the
     * renderer resources and derive names/spawn from the received tables. */
    resetMissionRuntimeState();
    gfxBufPtr = commData->gfxInitResult;
    setupInstrumentLayoutFar();
    g_netClientMode = 1; /* initMissionStrings skips worldImportToEgame */
    drawCockpit();
    initWeaponLoadout(); /* missiles[]/HUD ammo labels from the setup loadout */
    audio_setup(0, f15DgtlResult);
    g_headlessSim = 0;

    {
        uint64_t lastSend = 0;
        while (!g_missionOver) {
            NetRecv ev;
            struct NetInput in;

            gatherInput(&in);
            {
                uint8_t buf[NET_MSG_HEADER_SIZE + 64];
                struct NetWriter w;
                nwInit(&w, buf, sizeof(buf));
                netMsgWriteHeader(&w, NETMSG_INPUT, (NetTick)frameTick, 0);
                encInput(&w, &in);
                g_net->send(NET_PEER_SERVER, buf, w.len, NET_SEND_UNRELIABLE);
            }
            (void)lastSend;

            g_net->poll();
            while (g_net->recv(&ev)) {
                struct NetReader r;
                uint8_t type;
                NetTick tick;
                uint16_t plen;
                if (ev.kind == NET_EV_DISCONNECTED) {
                    g_missionOver = 1;
                    break;
                }
                if (ev.kind != NET_EV_MESSAGE)
                    continue;
                nrInit(&r, ev.msg, ev.len);
                if (!netMsgReadHeader(&r, &type, &tick, &plen))
                    continue;
                if (type == NETMSG_SNAPSHOT)
                    netSnapApply(&r, g_myPlayerId);
                else if (type == NETMSG_EVENT) {
                    struct NetEvent e;
                    if (decEvent(&r, &e))
                        applyEvent(&e);
                }
            }

            timerPump();
            renderFrame();
            renderHudFrame(0);
            if (g_viewMode == VIEW_COCKPIT)
                drawInstrumentGaugesFar();
            gfx_dacAnimate();
        }
    }
    audio_shutdown();
    commData->landingType = g_landingType;
    sendMsg(NETMSG_BYE, (NetTick)frameTick, 0, 0, 1);
    g_net->shutdown();
    return exitCode;
}
