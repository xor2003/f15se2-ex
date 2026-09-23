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
#include "egcombat.h"
#include "egdata.h"
#include "egframe.h"
#include "egplayer.h"
#include "egthreat.h"
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
void fireGroundThreat(int16 planeIdx); /* egthreat.c (file-local decl there) */

/* server tick on the wire: the sim's frameTick, widened */
static NetTick srvTick(void) { return (NetTick)(uint16_t)g_missionTick; }

#define DEFAULT_PORT F15_NET_DEFAULT_PORT

struct ServerPlayer {
    int used;
    int ready; /* HELLO seen, MISSION_SETUP sent */
    NetPeer peer;
    uint8_t role;
    char name[F15_NAME_LEN + 1];
    uint32_t lastSeq;
    uint32_t lastCmdSeq; /* app-level dedup: last clientSeq carrying cmds */
    uint32_t cmdDrops;   /* reliable cmds rejected by a full key queue */
    int32_t spawnOff; /* pending lateral spawn offset (world units), 0 = none */
    uint8_t inputArrived; /* sync-step: input packet seen since last tick */
    struct PlayerSim ctx;
    struct RemoteInput input;
};

static NetTransport *g_net;
static ServerPlayer g_players[F15_MAX_PLAYERS];
static int g_readyCount;
static int g_syncStep;
static int g_obsFull; /* --obs-full: privileged omniscient AI observations */
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
    if (to != NET_PEER_INVALID)
        g_net->send(to, buf, w.len, NET_SEND_RELIABLE);
    else if (g_curPeer != NET_PEER_INVALID)
        g_net->send(g_curPeer, buf, w.len, NET_SEND_RELIABLE);
    else {
        int i;
        for (i = 0; i < F15_MAX_PLAYERS; i++)
            if (g_players[i].used && g_players[i].ready)
                g_net->send(g_players[i].peer, buf, w.len, NET_SEND_RELIABLE);
    }
}

/* command outcome (NE_CMD_ACK): subject = the input packet's clientSeq,
 * object = command index inside that packet, arg = status. The pair
 * (clientSeq, idx) is the application-level command id - the sender can
 * match the ack against its own send order. */
static void sendCmdAck(NetPeer peer, uint32_t seq, uint8_t idx, int16_t st) {
    uint8_t buf[NET_MSG_HEADER_SIZE + 64];
    struct NetEvent ev;
    struct NetWriter w;
    memset(&ev, 0, sizeof(ev));
    ev.eventType = NE_CMD_ACK;
    ev.subject = seq;
    ev.object = idx;
    ev.arg = st;
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_EVENT, srvTick(), 0);
    encEvent(&w, &ev);
    g_net->send(peer, buf, w.len, NET_SEND_RELIABLE);
}

/* fired from remoteReadKey when the sim consumes a queued command key:
 * the ack now means "executed", not merely "accepted into the queue" */
static void onRemoteKeyServed(struct RemoteInput *r, uint16 scan,
                              uint32 seq, uint8 idx) {
    int i;
    (void)scan;
    if (!seq)
        return; /* untagged key */
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        if (g_players[i].used && &g_players[i].input == r) {
            sendCmdAck(g_players[i].peer, seq, idx, 0);
            return;
        }
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

/* Admission is bounded by BOTH parked-storages: each player needs a fixed
 * g_simObjects slot above g_groundUnitCount AND a g_planeTable map-target
 * slot above g_planeCount - the arrays have independent limits. A mission
 * short on either NAKs instead of accepting a player it can't simulate or
 * publish on the tactical map. */
static int playerCapacity(void) {
    int cap = F15_MAX_SIM_OBJECTS - g_groundUnitCount;
    int mapCap = F15_MAX_MAP_TARGETS - g_planeCount;
    if (mapCap < cap)
        cap = mapCap;
    if (cap > F15_MAX_PLAYERS)
        cap = F15_MAX_PLAYERS;
    return cap < 0 ? 0 : cap;
}

static int slotFree(void) {
    int i, cap = playerCapacity();
    for (i = 0; i < cap; i++)
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
    dst->damageSeq = 0;
    dst->threatWarningBits = 0;
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
    memset(&dst->comm, 0, sizeof(dst->comm));
    dst->comm.landingType = 1;
}

static void sendHelloAck(NetPeer peer, int slot) {
    struct NetHelloAck ack;
    uint8_t buf[NET_MSG_HEADER_SIZE + 16];
    struct NetWriter w;
    memset(&ack, 0, sizeof(ack));
    ack.playerId = (uint8_t)slot;
    ack.tickRate = F15_NET_TICKRATE;
    ack.flags = g_syncStep ? 1 : 0;
    ack.serverTick = srvTick();
    nwInit(&w, buf, sizeof(buf));
    netMsgWriteHeader(&w, NETMSG_HELLO_ACK, srvTick(), 0);
    encHelloAck(&w, &ack);
    g_net->send(peer, buf, w.len, NET_SEND_RELIABLE);
}

static void dropPlayer(int i);

static void onHello(NetPeer peer, struct NetReader *r) {
    struct NetHello h;
    int slot;
    if (!decHello(r, &h))
        return;
    if (h.protoVer != F15_NET_VERSION) {
        /* Local close does not produce a disconnect callback. A rejected
         * re-HELLO must release any slot already owned by this connection. */
        for (slot = 0; slot < F15_MAX_PLAYERS; slot++)
            if (g_players[slot].used && g_players[slot].peer == peer)
                dropPlayer(slot);
        sendSimple(peer, NETMSG_HELLO_NAK, 0, "bad version", 11);
        g_net->closePeer(peer, 0);
        return;
    }
    for (slot = 0; slot < F15_MAX_PLAYERS; slot++)
        if (g_players[slot].used && g_players[slot].peer == peer)
            break;
    if (slot < F15_MAX_PLAYERS) {
        /* Duplicate HELLO on a live association: the client lost the ACK or
         * restarted its handshake. Re-send join state but never touch the
         * running ctx - a real rejoin is BYE + new connection, not an
         * implicit reset. */
        ServerPlayer *p = &g_players[slot];
        sendHelloAck(peer, slot);
        sendMissionSetup(p);
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
    /* Spawn separation: alternating lateral offset in map units, applied once
     * the ctx finishes mission init (initPhase>=2) - for the playerInitCtx
     * path the position is only assigned during the first sim ticks, so the
     * offset must wait or it gets clobbered. Slot1 +0x1000, slot2 -0x1000,
     * slot3 +0x2000... a formation spread wide enough that each contact
     * resolves as a distinct radar blip, not a single point. */
    p->spawnOff = slot > 0 ? ((slot & 1) ? 1 : -1) * ((slot + 1) / 2) * 0x1000
                         : 0;
    sendHelloAck(peer, slot);
    sendMissionSetup(p);
    p->ready = 1;
    fprintf(stderr, "f15server: peer %u joined as player %d (%s, role %d)\n",
            (unsigned)peer, slot, p->name, (int)p->role);
}

static void onInput(ServerPlayer *p, struct NetReader *r) {
    struct NetInput in;
    int i, fresh;
    if (!decInput(r, &in))
        return;
    /* Axes are latest-wins: a stale/reordered packet must not regress
     * controls or count as sync-step readiness. Commands ride the reliable
     * stream and can legitimately arrive behind a newer unreliable axes
     * packet, so they always execute regardless of clientSeq freshness. */
    fresh = in.clientSeq > p->lastSeq;
    if (fresh) {
        remoteInputSetAxes(&p->input, in.joyX, in.joyY, in.buttons);
        p->lastSeq = in.clientSeq;
        p->inputArrived = 1;
    }
    /* Application-level dedup on the command stream: the reliable transport
     * already delivers each packet once, but a resent/replayed cmd packet
     * must not execute twice. (clientSeq, cmd index) is the command id;
     * axes freshness above is tracked separately so a cmd packet lagging
     * behind newer unreliable axes still runs. */
    if (in.nCmds > 0) {
        if (in.clientSeq <= p->lastCmdSeq)
            in.nCmds = 0; /* duplicate command packet: don't re-execute */
        else
            p->lastCmdSeq = in.clientSeq;
    }
    for (i = 0; i < in.nCmds; i++) {
        uint16_t scan;
        /* Pause/screenshot are client-local presentation ops; server-side
         * keyDispatch would block in waitForKeyPress() on a local keyboard
         * that does not exist, freezing the sim for everyone. */
        if (in.cmds[i] == NC_PAUSE || in.cmds[i] == NC_SCREENSHOT)
            continue;
        scan = netCmdToScan(in.cmds[i]);
        if (scan && !remoteInputPushKey(&p->input, scan, in.clientSeq,
                                        (uint8_t)i)) {
            /* queue full = explicit rejection: ack it so the sender can
             * tell dropped commands from executed ones */
            sendCmdAck(p->peer, in.clientSeq, (uint8_t)i, 1);
            if (++p->cmdDrops <= 4 || (p->cmdDrops & 0xFF) == 0)
                fprintf(stderr,
                        "f15server: player %d cmd queue full, dropped #%u\n",
                        (int)(p - g_players), (unsigned)p->cmdDrops);
        }
    }
}

static void dropPlayer(int i) {
    if (!g_players[i].used)
        return;
    fprintf(stderr, "f15server: player %d left\n", i);
    g_players[i].used = 0;
    g_players[i].ready = 0;
}

/* ---- per-player threat ownership ---- */

static int firstReadyPlayer(void) {
    int i;
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        if (g_players[i].used && g_players[i].ready && !g_players[i].ctx.ended)
            return i;
    return -1;
}

/* Which ready player a threat should engage: nearest by map range
 * (deterministic; ties break to the lowest slot). */
static int pickThreatTarget(int16 threatX, int16 threatY) {
    int i, best = -1;
    uint16 bestRange = 0xffff;
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        uint16 r;
        if (!p->used || !p->ready || p->ctx.ended)
            continue;
        r = (uint16)rangeApprox(p->ctx.viewX_ - threatX, p->ctx.viewY_ - threatY);
        if (r < bestRange) {
            bestRange = r;
            best = i;
        }
    }
    return best;
}

static int g_worldCtxIdx = -1; /* ctx currently resident in the world pass */

/* Temporarily swap the world pass's resident ctx out for `victim`'s, run the
 * enclosed fire routine, then restore. The legacy fire code's ctx reads
 * (range/bearing/envelope, warning cues, event checks) then see the player
 * actually being engaged, and the launched shot's targetPlayer owner is
 * stamped with the victim via g_residentPlayer. */
static void swapInVictim(int victim) {
    playerSwapOut(&g_players[g_worldCtxIdx].ctx);
    playerSwapIn(&g_players[victim].ctx);
    g_residentPlayer = (int16)victim;
    g_curPeer = g_players[victim].peer;
}

static void swapBackFromVictim(void) {
    int victim = g_residentPlayer;
    playerSwapOut(&g_players[victim].ctx);
    playerSwapIn(&g_players[g_worldCtxIdx].ctx);
    g_residentPlayer = (int16)g_worldCtxIdx;
    g_curPeer = g_players[g_worldCtxIdx].peer;
}

static void serverFireGroundThreat(int16 planeIdx) {
    int victim;
    if (g_worldCtxIdx < 0) { /* no resident ctx (shouldn't happen) */
        fireGroundThreat(planeIdx);
        return;
    }
    victim = pickThreatTarget(g_planeTable.planes[planeIdx].mapX,
                              g_planeTable.planes[planeIdx].mapY);
    if (victim < 0)
        victim = g_worldCtxIdx;
    /* g_scopeSweepTimer is the firing pilot's RWR debounce: evaluate it in
     * the VICTIM's ctx, so pilot A's active sweep never gates a site that
     * is engaging pilot B. */
    if (victim == g_worldCtxIdx) {
        if (g_scopeSweepTimer < 0)
            fireGroundThreat(planeIdx);
        return;
    }
    swapInVictim(victim);
    if (g_scopeSweepTimer < 0)
        fireGroundThreat(planeIdx);
    swapBackFromVictim();
}

static void serverFireAirThreat(int16 objIdx) {
    int victim;
    if (g_worldCtxIdx < 0) {
        fireAirThreat(objIdx);
        return;
    }
    victim = pickThreatTarget(g_simObjects[objIdx].posX,
                              g_simObjects[objIdx].posY);
    if (victim < 0 || victim == g_worldCtxIdx) {
        fireAirThreat(objIdx);
        return;
    }
    swapInVictim(victim);
    fireAirThreat(objIdx);
    swapBackFromVictim();
}

/* Shots whose owner ctx departed would freeze in place; re-home them to the
 * lowest-indexed ready player so they keep advancing exactly once per tick. */
static void rehomeOrphanProjectiles(void) {
    int i;
    for (i = 0; i < F15_MAX_PROJECTILES; i++) {
        int16 owner;
        if (g_projectiles[i].ttl == 0)
            continue;
        owner = g_projectiles[i].targetPlayer;
        if (owner >= 0 && owner < F15_MAX_PLAYERS && g_players[owner].used &&
            g_players[owner].ready && !g_players[owner].ctx.ended)
            continue;
        g_projectiles[i].targetPlayer = (int16)firstReadyPlayer();
    }
}

/* ---- parked remote-player objects (lockable/hittable player aircraft) ----
 * Each ready player's ctx is mirrored into g_simObjects at a fixed slot above
 * the real world objects (same convention as the client's parked remotes).
 * g_groundUnitCount stays untouched so updateObjects/escorts don't see them;
 * g_simObjScanBound extends the combat scans (AAM acquisition, gun tests,
 * missile target scans) over the parked slots. objType carries the owner
 * player index so loops can exclude "self". */
static int s_parkedObjBase = -1;

static void publishRemoteObjects(void) {
    int i;
    if (s_parkedObjBase < 0)
        s_parkedObjBase = g_groundUnitCount;
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        int slot = s_parkedObjBase + i;
        struct SimObject *o;
        ServerPlayer *p = &g_players[i];
        if (slot >= F15_MAX_SIM_OBJECTS)
            break;
        o = &g_simObjects[slot];
        if (p->used && p->ready && !p->ctx.ended) {
            o->posX = p->ctx.viewX_;
            o->posY = p->ctx.viewY_;
            o->worldX = p->ctx.ViewX;
            o->worldY = 0x01000000L - p->ctx.ViewY;
            o->alt = p->ctx.viewZ;
            o->heading.w = p->ctx.ourHead;
            o->pitch = p->ctx.ourPitch;
            o->bank.w = p->ctx.ourRoll;
            o->spec = 0;
            o->speed = p->ctx.knots;
            o->objType = (int16)i; /* owner player index */
            o->flags.b[0] = 2;     /* alive/eligible world object */
            o->flags.b[1] = SIMFLAG_B1_REMOTE_PLAYER |
                            ((p->ctx.playerPlaneFlags & 1) ? SIMFLAG_B1_GEAR_DOWN : 0);
        } else {
            o->flags.w = 0; /* gone -> no longer lockable/hittable */
        }
    }
    g_simObjScanBound = (int16)(s_parkedObjBase + F15_MAX_PLAYERS);
    if (g_simObjScanBound > F15_MAX_SIM_OBJECTS)
        g_simObjScanBound = F15_MAX_SIM_OBJECTS;
}

/* A parked remote object was destroyed (missile/guns): apply the damage to
 * the owning player's ctx — same fields the legacy bombTarget() bumps when a
 * SAM hits the player. Decisive: a destroyed airframe forces the eject path. */
static void onPlayerObjectHit(int16 objIdx) {
    int owner = objIdx - s_parkedObjBase;
    ServerPlayer *p;
    if (owner < 0 || owner >= F15_MAX_PLAYERS)
        return;
    p = &g_players[owner];
    if (!p->used)
        return;
    p->ctx.gunHits += 8;
    p->ctx.bombDamageMask |= 0xff;
    p->ctx.damageTakenFlag = 1;
}

/* ---- tick ---- */

static uint32_t fnvBlock(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

/* Canonical simulation-state hash (plan §24, review round 3):
 * covers every authoritative table the sim reads/writes - world objects,
 * projectiles/missiles, the bullet table, map-target table, map events,
 * waypoints, target slots - plus mission/timing scalars and the in-sim RNG
 * position (g_randCallCount). Player state folds in through
 * playerCtxHash(), which is defined to hash ONLY the simulation region of
 * PlayerSim (ViewX..missionEndedFlag): cosmetic/presentation fields and
 * string scratch cannot perturb the hash, and padding never leaks in. */
static uint32_t worldHash(void) {
    uint32_t h = 2166136261u;
    int i, n;
    h = fnvBlock(h, g_simObjects,
                 F15_MAX_SIM_OBJECTS * sizeof(g_simObjects[0]));
    h = fnvBlock(h, g_projectiles,
                 F15_MAX_PROJECTILES * sizeof(g_projectiles[0]));
    n = g_bulletTrackCount + 4; /* player pool + enemy tracer slots */
    if (n > 20)
        n = 20;
    h = fnvBlock(h, bulletTracks, (size_t)n * sizeof(bulletTracks[0]));
    n = g_planeCount;
    if (n > F15_MAX_MAP_TARGETS)
        n = F15_MAX_MAP_TARGETS;
    if (n > 0)
        h = fnvBlock(h, g_planeTable.planes, (size_t)n * sizeof(g_planeTable.planes[0]));
    h = fnvBlock(h, mapEvents, F15_MAX_MAP_EVENTS * sizeof(mapEvents[0]));
    h = fnvBlock(h, waypoints, F15_WAYPOINTS * sizeof(waypoints[0]));
    h = fnvBlock(h, g_targetSlots, 2 * sizeof(g_targetSlots[0]));
    h ^= (uint32_t)g_targetEntityCount;
    h ^= (uint32_t)g_planeCount << 8;
    h ^= (uint32_t)g_groundUnitCount << 16;
    h ^= (uint32_t)g_planeScanCount << 24;
    h *= 16777619u;
    h ^= (uint32_t)g_missionTick;
    h ^= (uint32_t)g_missionStatus << 16;
    h *= 16777619u;
    h ^= (uint32_t)g_randCallCount;
    h ^= g_bulletPoolCursor; /* next saturated-pool overwrite slot */
    h ^= (uint32_t)(uint16_t)g_bulletFreshTick << 8;
    h ^= g_bulletFreshMask << 16; /* this-tick allocs the fallback skips */
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        if (!g_players[i].used || !g_players[i].ready)
            continue;
        h ^= (uint32_t)i; /* which slot the ctx lives in matters */
        h ^= playerCtxHash(&g_players[i].ctx);
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
        /* AI-role clients consume NETMSG_OBS instead (plan §20) */
        if (g_players[ids[i]].role != NET_ROLE_AI)
            g_net->send(g_players[ids[i]].peer, buf, w.len,
                        NET_SEND_UNRELIABLE);
}

/* Plan §20: structured observation for AI-role players, built under each
 * observer's own ctx so the contact list carries exactly what that pilot's
 * radar scope/tacmap/RWR would show. One extra swap pair per AI player. */
static void sendObservations(void) {
    static uint8_t buf[4096];
    int i;
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        struct NetWriter w;
        if (!p->used || !p->ready || p->role != NET_ROLE_AI)
            continue;
        playerSwapIn(&p->ctx);
        nwInit(&w, buf, sizeof(buf));
        netMsgWriteHeader(&w, NETMSG_OBS, srvTick(), 0);
        netObsBuild(&w, &p->ctx, i, s_parkedObjBase, g_obsFull, g_stateHash);
        /* Observation helpers write range/bearing scratch globals. Discard
         * those writes: querying a pilot must not change their next step or
         * the state whose hash was already sent with the snapshot. */
        playerSwapIn(&p->ctx);
        if (w.overflow)
            continue;
        g_net->send(p->peer, buf, w.len, NET_SEND_UNRELIABLE);
    }
}

/* The world pass still has residual ctx-relative reads (last-hit refs,
 * autopilot flag, threat-scope bookkeeping). Run it under the most-threatened
 * player's ctx so those reads follow the action; deterministic tiebreak by
 * lowest slot keeps swapped slot assignments equivalent. */
static int pickWorldCtx(void) {
    int i, best = -1;
    int16 bestRange = 0x7fff;
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        if (!p->used || !p->ready || p->ctx.ended)
            continue;
        if (p->ctx.nearestThreatRange < bestRange) {
            bestRange = p->ctx.nearestThreatRange;
            best = i;
        }
    }
    return best >= 0 ? best : firstReadyPlayer();
}

static void serverTick(void) {
    int i;
    /* park each ready player as a lockable/hittable sim object (positions
     * from last tick - one consistent view for the whole tick's combat) */
    publishRemoteObjects();
    /* per-player pass: each ctx steps its own flight, fires, takes its own
     * threat guidance/damage and threat scan under its own globals */
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        if (!p->used || !p->ready || p->ctx.ended)
            continue;
        g_curPeer = p->peer;
        g_residentPlayer = (int16)i;
        simInputSet(remoteInputOps(), &p->input);
        simulatePlayer(&p->ctx);
        g_residentPlayer = -1;
        if (p->spawnOff != 0 && p->ctx.initPhase >= 2) {
            p->ctx.viewX_ += (int16_t)(p->spawnOff >> 5);
            p->ctx.ViewX += p->spawnOff;
            p->spawnOff = 0;
        }
        if (p->ctx.missionEndedFlag[0] && !p->ctx.ended) {
            p->ctx.ended = 1;
            sendEvent(NE_MISSION_END, p->peer, p->ctx.comm.landingType, 0);
        }
    }
    g_curPeer = NET_PEER_INVALID;
    rehomeOrphanProjectiles();
    /* world pass once, under the most-threatened player's ctx */
    i = pickWorldCtx();
    if (i >= 0) {
        int16 threatIdx, threatChanged;
        g_worldCtxIdx = i;
        g_curPeer = g_players[i].peer;
        g_residentPlayer = (int16)i;
        playerSwapIn(&g_players[i].ctx);
        updateWorldFrame();
        /* escort/interceptor spawns track the most-threatened player's
         * closest threat (its scan ran in this tick's player pass) */
        threatIdx = g_players[i].ctx.closestThreatIndex;
        threatChanged =
            g_players[i].ctx.prevThreatIndex != g_players[i].ctx.closestThreatIndex;
        if (threatIdx >= 0)
            frameThreatEscort(threatIdx, threatChanged);
        playerSwapOut(&g_players[i].ctx);
        g_worldCtxIdx = -1;
        g_curPeer = NET_PEER_INVALID;
        g_residentPlayer = -1;
        if (!g_haveTemplate && g_players[i].ctx.initPhase >= 2) {
            g_spawnTemplate = g_players[i].ctx;
            g_haveTemplate = 1;
        }
    }
    /* damageTakenFlag is a transient "you were hit this tick" latch: in SP the
     * HUD clears it when it fires the 60-tick shake, but the headless server
     * has no HUD, so it latched forever and every snapshot re-triggered the
     * client's shake. Number each damage event instead (damageSeq rides the
     * snapshot), release the latch every tick, and keep the persistent damage
     * where it belongs - gunHits/bombDamageMask. Catches sets from the hit
     * hook AND from in-ctx code like bombTarget. */
    for (i = 0; i < F15_MAX_PLAYERS; i++) {
        ServerPlayer *p = &g_players[i];
        if (p->used && p->ctx.damageTakenFlag) {
            p->ctx.damageTakenFlag = 0;
            p->ctx.damageSeq++;
        }
    }
    simInputReset();
    g_stateHash = worldHash();
    /* sync-step: consume this tick's input readiness */
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        g_players[i].inputArrived = 0;
}

static int allInputsArrived(void) {
    int i;
    /* sync-step mode: tick only when every ready player sent input since the
     * last tick (plan §22 - deterministic training). The flag is consumed by
     * the completed tick so stale readiness can't free-run the sim. */
    for (i = 0; i < F15_MAX_PLAYERS; i++)
        if (g_players[i].used && g_players[i].ready && !g_players[i].ctx.ended &&
            !g_players[i].inputArrived)
            return 0;
    return g_readyCount > 0;
}

static void usage(void) {
    fprintf(stderr,
            "f15server --game <dir> [--port N] [--bind IP] [--seed N]\n"
            "          [--theater N] [--difficulty N] [--sync-step] [--obs-full]\n");
    exit(1);
}

/* Entry point shared by the standalone f15server binary (server_main.cpp)
 * and `f15se2-ex --server` (f15.c) - the single game executable can also be
 * the dedicated server. */
int f15ServerMain(int argc, char **argv) {
    int port = DEFAULT_PORT;
    int seed = 12345, theater = 0, difficulty = 0;
    const char *gameDir = getenv("F15SE2_DIR");
    const char *bindAddr = 0; /* NULL = all interfaces */
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc)
            gameDir = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bind") && i + 1 < argc)
            bindAddr = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--theater") && i + 1 < argc)
            theater = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc)
            difficulty = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sync-step"))
            g_syncStep = 1;
        else if (!strcmp(argv[i], "--obs-full"))
            g_obsFull = 1;
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
    /* threat fire routines delegate victim selection + ctx swap to us */
    g_threatFireHook = serverFireGroundThreat;
    g_airThreatFireHook = serverFireAirThreat;
    g_remoteKeyServedHook = onRemoteKeyServed; /* cmd execution acks */
    g_playerObjectHitHook = onPlayerObjectHit;
    g_simEvents.onHudMessage = evHud;
    g_simEvents.onTimedMessage = evTimed;
    g_simEvents.onSound = evSound;
    g_simEvents.onVoice = evVoice;
    g_simEvents.onMapEvent = evMap;

    if (!bootWorld(seed, theater, difficulty)) {
        fprintf(stderr, "f15server: asset/boot failure\n");
        return 1;
    }
    /* server player rounds share the whole player pool: free-slot claim,
     * rotating overwrite under saturation; last 4 slots stay enemy tracers */
    g_bulletTrackCount = F15_MAX_PLAYERS * 2;

    g_net = createGnsTransport();
    if (!g_net || !g_net->listen(bindAddr, (uint16_t)port)) {
        fprintf(stderr, "f15server: listen failed\n");
        return 1;
    }
    /* --host waits on this line for readiness: it must print only once the
     * socket actually accepts. */
    fprintf(stderr, "f15server: world ready, listening on %s:%d\n",
            bindAddr ? bindAddr : "", port);

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
                            /* graceful leave: free the slot now - a locally
                             * initiated closePeer does not surface a
                             * NET_EV_DISCONNECTED event back to us. */
                            for (i = 0; i < F15_MAX_PLAYERS; i++)
                                if (g_players[i].used &&
                                    g_players[i].peer == ev.peer)
                                    dropPlayer(i);
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
                sendObservations();
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
