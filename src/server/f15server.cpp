/*
 * f15server.cpp - headless authoritative F-15 server (plan §9/§16/§22).
 *
 * Owns the full world sim: remote players are PlayerSim contexts stepped
 * through the legacy stepFlightModel()+updatePlayerFrame() under their own
 * input; the world pass (updateWorldFrame) runs once per tick. Clients send
 * NetInput (commands+axes), never positions; snapshots go out at tick rate.
 *
 *   f15server --game <assets> --port 27015 --seed 12345 [--sync-step]
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "common.h"
#include "egcode.h"
#include "egdata.h"
#include "egframe.h"
#include "egplayer.h"
#include "egtypes.h"
#include "gfx.h"
#include "inttype.h"
#include "slot.h"
#include "stgen.h"
#include "struct.h"

#include "net/codec.h"
#include "net/commands.h"
#include "net/snapshot.h"
#include "net/transport.h"

/* core-lib entry points (same boot path as f15.c, minus the UI); plain C++
 * linkage like every header in this tree */
void game_init(int16 showIntro);       /* f15.c */
void resetMissionRuntimeState(void);   /* egmain.c */
void drawCockpit(void);                /* egmain.c */
bool setGamePath(const char *path);    /* file_io.c */
bool verifyGameAssets();               /* file_io.c */

/* server tick on the wire: the sim's frameTick, widened */
static NetTick srvTick(void) { return (NetTick)(uint16_t)g_missionTick; }

#define DEFAULT_PORT 27015

struct ServerPlayer {
    int used;
    int ready; /* HELLO seen, MISSION_SETUP sent */
    NetPeer peer;
    uint8_t role;
    char name[F15_NAME_LEN + 1];
    uint32_t lastSeq;
    struct PlayerSim ctx;
    struct RemoteInput input;
};

static NetTransport *g_net;
static ServerPlayer g_players[F15_MAX_PLAYERS];
static int g_readyCount;
static int g_syncStep;
static uint32_t g_stateHash;
static struct PlayerSim g_spawnTemplate;
static int g_haveTemplate;
static NetPeer g_curPeer = NET_PEER_INVALID; /* ctx owner during a P-pass */

/* ---- presentation events -> wire (plan §12) ---- */

static void sendEvent(uint16_t type, NetPeer to, int16_t arg, const char *text) {
    uint8_t buf[NET_MSG_HEADER_SIZE + 64];
    struct NetEvent ev;
    struct NetWriter w;
    memset(&ev, 0, sizeof(ev));
    ev.eventType = type;
    ev.subject = NET_ENTITY_INVALID;
    ev.object = NET_ENTITY_INVALID;
    ev.arg = arg;
    if (text)
        snprintf(ev.text, sizeof(ev.text), "%s", text);
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_EVENT, srvTick(), 0);
    encEvent(&w, &ev);
    if (g_curPeer != NET_PEER_INVALID)
        g_net->send(g_curPeer, buf, w.len, NET_SEND_RELIABLE);
    else {
        int i;
        for (i = 0; i < F15_MAX_PLAYERS; i++)
            if (g_players[i].used && g_players[i].ready)
                g_net->send(g_players[i].peer, buf, w.len, NET_SEND_RELIABLE);
    }
    (void)to;
}

static void evHud(const char *t) { sendEvent(NE_HUD_MESSAGE, g_curPeer, 0, t); }
static void evTimed(const char *t) { sendEvent(NE_TIMED_MESSAGE, g_curPeer, 0, t); }
static void evSound(int id, int prio) { sendEvent(NE_SOUND, g_curPeer, (int16_t)((prio << 8) | id), 0); }
static void evVoice(int cue) { sendEvent(NE_VOICE_CUE, g_curPeer, (int16_t)cue, 0); }
static void evMap(int16_t type, int16_t arg) {
    sendEvent(NE_MAP_EVENT, NET_PEER_INVALID, (int16_t)((arg << 8) | type), 0);
}

/* ---- mission bootstrap (mirrors the DEBUG_AUTOSTART recipe) ---- */

static int bootWorld(int seed, int theater, int difficulty) {
    if (!verifyGameAssets())
        return 0;
    game_init(0);

    gameData->difficulty = difficulty;
    gameData->theater = theater;
    gameData->missionReady = 1;
    gameData->isCampaignMission = 0;
    gameData->campaignProgress = 0;
    gameData->rand = seed;
    joyAxes[0] = joyAxes[1] = 0x80;
    srand(seed);
    missionGenerate();

    exitCode = 12;
    commData->needSplash = 0;
    gfx_setFadeSteps(8);
    loadPic("f15.spr", commData->gfxInitResult);
    commData->trainingFlag = (gameData->missionReady > 1);

    resetMissionRuntimeState();
    gfxBufPtr = commData->gfxInitResult;
    setupInstrumentLayoutFar();
    drawCockpit(); /* world import + model/region loads; renderer never runs */
    audio_setup(0, f15DgtlResult); /* dummy driver: audio_* calls are no-ops */
    setTimerTickHook(egAdvanceFrameTick);
    setTimerIrqHandler();
    return 1;
}

/* ---- player slots ---- */

static int slotFree(void) {
    int i;
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        if (!g_players[i].used)
            return i;
    return -1;
}

static void sendSimple(NetPeer peer, uint8_t type, NetTick tick,
                       const void *payload, size_t len) {
    uint8_t buf[NET_MSG_HEADER_SIZE + 64];
    struct NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, type, tick, (uint16_t)len);
    nwBytes(&w, payload, len);
    g_net->send(peer, buf, w.len, NET_SEND_RELIABLE);
}

static void sendMissionSetup(ServerPlayer *p) {
    static uint8_t buf[8192];
    struct NetWriter w;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_MISSION_SETUP, srvTick(), 0);
    netSetupBuild(&w);
    g_net->send(p->peer, buf, w.len, NET_SEND_RELIABLE);
}

/* Spawn a late-joining pilot from the post-init template (player 0's ctx
 * captured once initPhase hit 2), offset so they don't co-locate. */
static void spawnFromTemplate(struct PlayerSim *dst, int idx) {
    *dst = g_spawnTemplate;
    /* per-pilot transients reset */
    dst->keyScancode = 0;
    memset(dst->axisInputAccum, 0, sizeof(dst->axisInputAccum));
    dst->ejectState = dst->ejectPending = 0;
    dst->damageTakenFlag = dst->gunFiredFlag = 0;
    dst->wreckX = dst->wreckY = dst->wreckAlt = dst->wreckFallVel = 0;
    dst->crashCamX = dst->crashCamY = dst->crashCamZ = 0;
    dst->hitMapX = dst->hitMapY = dst->hitAlt = 0;
    dst->hitEffectTimer = 0;
    dst->gunHits = 0;
    dst->airTargetLock = dst->groundTargetLock = -1;
    dst->smokeSourceIdx = dst->prevThreatIndex = -1;
    dst->trackedEnemyIdx = -1;
    dst->inLandingCorridor = 0;
    dst->landingDoneFlag = dst->landingTimer = dst->autoLandingActive = 0;
    dst->resupplyCount = 1;
    dst->inputDisabled = 0;
    dst->autopilotEngaged = dst->autopilotAltitude = 0;
    dst->savedPosVisible = 0;
    dst->hudMsgTimer = dst->dirMsgTimer = 0;
    dst->missionEndedFlag[0] = dst->missionEndedFlag[1] = 0;
    dst->ended = 0;
    dst->viewMode = VIEW_COCKPIT;
    /* lateral offset by slot index (map units -> world <<5) */
    dst->ViewX += (int32_t)(idx * 96) << 5;
    dst->viewX_ += (int16_t)(idx * 96);
    memset(&dst->comm, 0, sizeof(dst->comm));
    dst->comm.landingType = 1;
}

static void onHello(NetPeer peer, struct NetReader *r) {
    struct NetHello h;
    struct NetHelloAck ack;
    int slot;
    if (!decHello(r, &h))
        return;
    if (h.protoVer != F15_NET_VERSION) {
        sendSimple(peer, NETMSG_HELLO_NAK, 0, "bad version", 11);
        g_net->closePeer(peer, 0);
        return;
    }
    slot = slotFree();
    if (slot < 0) {
        sendSimple(peer, NETMSG_HELLO_NAK, 0, "server full", 11);
        g_net->closePeer(peer, 0);
        return;
    }
    ServerPlayer *p = &g_players[slot];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->peer = peer;
    p->role = h.role;
    snprintf(p->name, sizeof(p->name), "%s", h.name);
    remoteInputInit(&p->input);

    if (!g_haveTemplate) {
        /* first pilot owns the live globals: capture them as ctx 0 and let
         * the first step run initPhase 0->1->2 under the ctx. */
        playerInitCtx(&p->ctx);
        playerSwapOut(&p->ctx);
        p->ctx.active = 1;
    } else {
        spawnFromTemplate(&p->ctx, slot);
        p->ctx.active = 1;
    }
    memset(&ack, 0, sizeof(ack));
    ack.playerId = (uint8_t)slot;
    ack.tickRate = F15_NET_TICKRATE;
    ack.flags = g_syncStep ? 1 : 0;
    ack.serverTick = srvTick();
    {
        uint8_t buf[NET_MSG_HEADER_SIZE + 16];
        struct NetWriter w;
        nwInit(&w, buf, sizeof(buf));
        netMsgWriteHeader(&w, NETMSG_HELLO_ACK, srvTick(), 0);
        encHelloAck(&w, &ack);
        g_net->send(peer, buf, w.len, NET_SEND_RELIABLE);
    }
    sendMissionSetup(p);
    p->ready = 1;
    fprintf(stderr, "f15server: peer %u joined as player %d (%s, role %d)\n",
            (unsigned)peer, slot, p->name, (int)p->role);
}

static void onInput(ServerPlayer *p, struct NetReader *r) {
    struct NetInput in;
    int i;
    if (!decInput(r, &in))
        return;
    /* latest-wins axes/buttons; commands queue (plan §3) */
    remoteInputSetAxes(&p->input, in.joyX, in.joyY, in.buttons);
    for (i = 0; i < in.nCmds; i++) {
        uint16_t scan = netCmdToScan(in.cmds[i]);
        if (scan)
            remoteInputPushKey(&p->input, scan);
    }
    p->lastSeq = in.clientSeq;
}

static void dropPlayer(int i) {
    if (!g_players[i].used)
        return;
    fprintf(stderr, "f15server: player %d left\n", i);
    g_players[i].used = 0;
    g_players[i].ready = 0;
}

/* ---- tick ---- */

static uint32_t worldHash(void) {
    /* canonical world hash (plan §24): player ctxs + shared tables */
    uint32_t h = 2166136261u;
    int i;
    const uint8_t *b;
    b = (const uint8_t *)g_simObjects;
    for (i = 0; i < (int)(F15_MAX_SIM_OBJECTS * sizeof(g_simObjects[0])); i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    b = (const uint8_t *)g_projectiles;
    for (i = 0; i < (int)(F15_MAX_PROJECTILES * sizeof(g_projectiles[0])); i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    h ^= (uint32_t)frameTick;
    return h;
}

static void sendSnapshots(void) {
    static uint8_t buf[4096];
    struct NetWriter w;
    int ids[F15_MAX_PLAYERS], n = 0, i;
    struct PlayerSim *ctxs[F15_MAX_PLAYERS];
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        if (g_players[i].used && g_players[i].ready) {
            ids[n] = i;
            ctxs[n] = &g_players[i].ctx;
            n++;
        }
    }
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_SNAPSHOT, srvTick(), 0);
    /* netSnapBuild takes a contiguous ctx array; pass through by copying into a
     * scratch (players are already swapped out - globals hold last ctx). */
    {
        static struct PlayerSim flat[F15_MAX_PLAYERS];
        for (i = 0; i < n; i++)
            flat[i] = *ctxs[i];
        netSnapBuild(&w, flat, ids, n, g_stateHash);
    }
    if (w.overflow)
        return;
    for (i = 0; i < n; i++)
        g_net->send(g_players[ids[i]].peer, buf, w.len, NET_SEND_UNRELIABLE);
}

static void serverTick(void) {
    int i;
    /* per-player pass */
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        if (!p->used || !p->ready || p->ctx.ended)
            continue;
        g_curPeer = p->peer;
        simInputSet(remoteInputOps(), &p->input);
        simulatePlayer(&p->ctx);
        if (p->ctx.missionEndedFlag[0] && !p->ctx.ended) {
            p->ctx.ended = 1;
            sendEvent(NE_MISSION_END, p->peer, p->ctx.comm.landingType, 0);
        }
    }
    g_curPeer = NET_PEER_INVALID;
    /* world pass under ctx 0's globals (frameThreatScan is player-centric) */
    if (g_players[0].used) {
        playerSwapIn(&g_players[0].ctx);
        updateWorldFrame();
        playerSwapOut(&g_players[0].ctx);
        if (!g_haveTemplate && g_players[0].ctx.initPhase >= 2) {
            g_spawnTemplate = g_players[0].ctx;
            g_haveTemplate = 1;
        }
    }
    simInputReset();
    g_stateHash = worldHash();
}

static int allInputsArrived(void) {
    int i;
    /* sync-step mode: tick only when every ready player has input for this
     * tick (plan §22 - deterministic training). v1: lastSeq monotonic. */
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        if (g_players[i].used && g_players[i].ready && !g_players[i].ctx.ended &&
            g_players[i].lastSeq == 0)
            return 0;
    return g_readyCount > 0;
}

static void usage(void) {
    fprintf(stderr,
            "f15server --game <dir> [--port N] [--seed N] [--theater N]\n"
            "          [--difficulty N] [--sync-step]\n");
    exit(1);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    int seed = 12345, theater = 0, difficulty = 0;
    const char *gameDir = getenv("F15SE2_DIR");
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc)
            gameDir = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--theater") && i + 1 < argc)
            theater = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc)
            difficulty = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sync-step"))
            g_syncStep = 1;
        else
            usage();
    }

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    /* headless: no SDL event pump, so don't let SDL swallow SIGINT/SIGTERM */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!setGamePath(gameDir)) {
        fprintf(stderr, "f15server: bad game path\n");
        return 1;
    }
    g_headlessSim = 1;
    g_simEvents.onHudMessage = evHud;
    g_simEvents.onTimedMessage = evTimed;
    g_simEvents.onSound = evSound;
    g_simEvents.onVoice = evVoice;
    g_simEvents.onMapEvent = evMap;

    if (!bootWorld(seed, theater, difficulty)) {
        fprintf(stderr, "f15server: asset/boot failure\n");
        return 1;
    }
    fprintf(stderr, "f15server: world ready, listening on :%d\n", port);

    g_net = createGnsTransport();
    if (!g_net || !g_net->listen((uint16_t)port)) {
        fprintf(stderr, "f15server: listen failed\n");
        return 1;
    }

    {
        uint64_t nextNs = SDL_GetTicksNS();
        const uint64_t stepNs = 1000000000ULL / F15_NET_TICKRATE;
        for (;;) {
            NetRecv ev;
            uint64_t now;
            g_net->poll();
            while (g_net->recv(&ev)) {
                if (ev.kind == NET_EV_CONNECTED) {
                    /* wait for HELLO */
                } else if (ev.kind == NET_EV_DISCONNECTED) {
                    for (i = 0; i < F15_MAX_PLAYERS; i++)
                        if (g_players[i].used && g_players[i].peer == ev.peer)
                            dropPlayer(i);
                } else if (ev.kind == NET_EV_MESSAGE) {
                    struct NetReader r;
                    uint8_t type;
                    NetTick tick;
                    uint16_t plen;
                    nrInit(&r, ev.msg, ev.len);
                    if (netMsgReadHeader(&r, &type, &tick, &plen)) {
                        if (type == NETMSG_HELLO) {
                            onHello(ev.peer, &r);
                        } else if (type == NETMSG_BYE) {
                            g_net->closePeer(ev.peer, 0);
                        } else {
                            for (i = 0; i < F15_MAX_PLAYERS; i++)
                                if (g_players[i].used &&
                                    g_players[i].peer == ev.peer) {
                                    if (type == NETMSG_INPUT)
                                        onInput(&g_players[i], &r);
                                    break;
                                }
                        }
                    }
                }
            }

            g_readyCount = 0;
            for (i = 0; i < F15_MAX_PLAYERS; i++)
                if (g_players[i].used && g_players[i].ready)
                    g_readyCount++;

            now = SDL_GetTicksNS();
            if (g_readyCount > 0 &&
                (g_syncStep ? allInputsArrived() : now >= nextNs)) {
                serverTick();
                sendSnapshots();
                if (!g_syncStep)
                    nextNs = now + stepNs;
            }
            /* idle pacing: ~1.5ms poll granularity; sleeping before the next
             * tick when players are live */
            now = SDL_GetTicksNS();
            if (g_syncStep || g_readyCount == 0 || now >= nextNs) {
                SDL_DelayNS(1500000);
            } else {
                uint64_t d = nextNs - now;
                SDL_DelayNS(d > 1500000 ? 1500000 : d);
            }
        }
    }
    return 0;
}
