/*
 * snapshot.cpp - authoritative world state <-> wire (plan §11/§26).
 *
 * Wire order is fixed and explicit; every multi-byte field goes through
 * serialize.h (little-endian). SimObject/MapTarget slots map 1:1 to entity ids
 * (v1: id == index; plan §7 stable ids arrive with delta/reconnect work).
 */
#include <string.h>

#include "codec.h"
#include "snapshot.h"

#include "comm.h"
#include "egdata.h"
#include "egkeys.h"
#include "egtypes.h"
#include "struct.h"

extern "C" {

/* entity id layout (v1): 0..19 simObjects, 0x100+ projectiles,
 * 0x200+i remote players, 0x300+i planeTable sites (see protocol.h). */

} /* extern "C" - the helpers below ride the project's plain C++ linkage
   * (the .c TUs are compiled as C++, so their symbols are C++-mangled) */

void projectMapPoint(int mapX, int mapY); /* egui.c: scope entitlement test */
int16 computeTargetBearing(int16 targetX, int16 targetY,
                           int16 wantBearing); /* egtgt2.c */

extern "C" {

/* ---------------------------------------------------------------- setup */

/* Where remote players are parked inside g_simObjects for rendering/radar:
 * contiguously above the world's own objects (g_groundUnitCount as received in
 * MISSION_SETUP). Every renderer/radar loop bounded by g_groundUnitCount then
 * sees them; the client-side count is extended to cover the parked slots.
 * g_planeTable.planes[] gets the same treatment above s_worldPlaneCount: the
 * tactical map iterates planeTable, not simObjects, so remote pilots need an
 * entry there too (flags 0x401 = airborne aircraft marker). */
static int s_worldObjCount = -1;      /* authoritative object count from setup */
static int s_worldPlaneCount = -1;    /* authoritative planeTable count */
static unsigned s_parkedMask;         /* player ids currently published */
static unsigned s_seenMask;           /* player ids seen in this snapshot */
static int16_t s_lastFrameTick;       /* last committed authoritative tick */
static int s_haveLastTick;            /* 1 once a snapshot has committed -
                                       * frameTick is signed, so "unset"
                                       * needs its own flag, not a sentinel */
static int16_t s_lastDamageSeq;       /* last damageSeq that fired the HUD
                                       * shake - edge trigger, not state */

void netSetupBuild(struct NetWriter *w) {
    int i, n;
    nwI16(w, gameData->theater);
    nwI16(w, gameData->difficulty);
    nwI32(w, gameData->rand);
    nwU16(w, gameData->unk4);
    nwI16(w, g_planeCount);
    nwI16(w, g_targetEntityCount);
    nwI16(w, g_planeScanCount);
    nwI16(w, g_groundUnitCount);
    nwI16(w, g_missionStatus);
    nwI16(w, g_unusedSavedWord);
    nwI16(w, g_padlockAircraft);
    nwU8(w, g_landTargetId[0]);
    nwU8(w, g_waterTargetId[0]);
    nwI16(w, g_planeTable.nameIndexLead);
    /* comm/loadout the client needs for its own cockpit instruments */
    for (i = 0; i < 4; i++)
        nwU16(w, commData->weaponType[i]);
    for (i = 0; i < 4; i++)
        nwI16(w, commData->weaponCount[i]);
    nwI16(w, commData->setupDetail);
    nwI16(w, commData->trainingFlag);

    n = g_planeCount;
    if (n > F15_MAX_MAP_TARGETS)
        n = F15_MAX_MAP_TARGETS;
    for (i = 0; i < n; i++) {
        const struct MapTarget *p = &g_planeTable.planes[i];
        nwU16(w, p->mapX);
        nwU16(w, p->mapY);
        nwI16(w, p->active);
        nwI16(w, p->flags);
        nwI16(w, p->alertLevel);
        nwI16(w, p->threatTimer);
        nwI16(w, p->nameIndex);
        nwI16(w, p->secondaryNameIndex);
    }
    n = g_groundUnitCount;
    if (n > F15_MAX_SIM_OBJECTS)
        n = F15_MAX_SIM_OBJECTS;
    for (i = 0; i < n; i++) {
        const struct SimObject *o = &g_simObjects[i];
        nwI16(w, o->objType);
        nwU16(w, o->posX);
        nwU16(w, o->posY);
        nwI16(w, o->alt);
        nwI32(w, o->worldX);
        nwI32(w, o->worldY);
        nwI16(w, o->heading.w);
        nwI16(w, o->pitch);
        nwI16(w, o->bank.w);
        nwI16(w, o->spec);
        nwU16(w, o->flags.w);
        nwI16(w, o->speed);
        nwI16(w, o->timer);
        nwI16(w, o->weaponType);
        nwI16(w, o->damage);
    }
    for (i = 0; i < F15_WAYPOINTS; i++) {
        nwU16(w, waypoints[i].mapX);
        nwU16(w, waypoints[i].mapY);
    }
    /* targetSlots: 9 int16 words each, order-preserving opaque mirror of the
     * 18-byte Target record the world import memcpy'd in. */
    for (i = 0; i < 2; i++) {
        const int16 *ts = (const int16 *)&g_targetSlots[i];
        int j;
        for (j = 0; j < 9; j++)
            nwI16(w, ts[j]);
    }
    nwBytes(w, g_shapeTargetCategory, 100);
    nwBytes(w, g_tileKillTally, 100);
    nwBytes(w, g_stringPool, 750);
    nwBytes(w, g_mapCellFlags, 0x100);
}

int netSetupApply(struct NetReader *r) {
    int i, n, j;
    gameData->theater = nrI16(r);
    gameData->difficulty = nrI16(r);
    gameData->rand = nrI32(r);
    gameData->unk4 = nrU16(r);
    g_planeCount = nrI16(r);
    g_targetEntityCount = nrI16(r);
    g_planeScanCount = nrI16(r);
    g_groundUnitCount = nrI16(r);
    s_worldObjCount = g_groundUnitCount; /* remote players park above this */
    s_worldPlaneCount = g_planeCount;
    s_parkedMask = s_seenMask = 0;
    s_haveLastTick = 0; /* new world: staleness gate re-arms on first snap */
    s_lastDamageSeq = 0; /* new ctx starts its damage counter at 0 */
    g_missionStatus = nrI16(r);
    g_unusedSavedWord = nrI16(r);
    g_padlockAircraft = nrI16(r);
    g_landTargetId[0] = nrU8(r);
    g_waterTargetId[0] = nrU8(r);
    g_planeTable.nameIndexLead = nrI16(r);
    for (i = 0; i < 4; i++)
        commData->weaponType[i] = nrU16(r);
    for (i = 0; i < 4; i++)
        commData->weaponCount[i] = nrI16(r);
    commData->setupDetail = nrI16(r);
    commData->trainingFlag = nrI16(r);

    n = g_planeCount;
    if (n < 0 || n > F15_MAX_MAP_TARGETS)
        return 0;
    memset(&g_planeTable.planes, 0, sizeof(g_planeTable.planes));
    for (i = 0; i < n; i++) {
        struct MapTarget *p = &g_planeTable.planes[i];
        p->mapX = nrU16(r);
        p->mapY = nrU16(r);
        p->active = nrI16(r);
        p->flags = nrI16(r);
        p->alertLevel = nrI16(r);
        p->threatTimer = nrI16(r);
        p->nameIndex = nrI16(r);
        p->secondaryNameIndex = nrI16(r);
    }
    n = g_groundUnitCount;
    if (n < 0 || n > F15_MAX_SIM_OBJECTS)
        return 0;
    memset(g_simObjects, 0, F15_MAX_SIM_OBJECTS * sizeof(g_simObjects[0]));
    for (i = 0; i < n; i++) {
        struct SimObject *o = &g_simObjects[i];
        o->objType = nrI16(r);
        o->posX = nrU16(r);
        o->posY = nrU16(r);
        o->alt = nrI16(r);
        o->worldX = nrI32(r);
        o->worldY = nrI32(r);
        o->heading.w = nrI16(r);
        o->pitch = nrI16(r);
        o->bank.w = nrI16(r);
        o->spec = nrI16(r);
        o->flags.w = nrU16(r);
        o->speed = nrI16(r);
        o->timer = nrI16(r);
        o->weaponType = nrI16(r);
        o->damage = nrI16(r);
    }
    for (i = 0; i < F15_WAYPOINTS; i++) {
        waypoints[i].mapX = nrU16(r);
        waypoints[i].mapY = nrU16(r);
    }
    for (i = 0; i < 2; i++) {
        int16 *ts = (int16 *)&g_targetSlots[i];
        for (j = 0; j < 9; j++)
            ts[j] = nrI16(r);
    }
    nrBytes(r, g_shapeTargetCategory, 100);
    nrBytes(r, g_tileKillTally, 100);
    nrBytes(r, g_stringPool, 750);
    nrBytes(r, g_mapCellFlags, 0x100);
    return !r->underrun;
}

/* ------------------------------------------------------------- snapshot */

static void encPlayerBlock(struct NetWriter *w, const struct PlayerSim *c) {
    struct NetPlayerState s;
    int i;
    memset(&s, 0, sizeof(s));
    s.worldX = c->ViewX;
    s.worldY = c->ViewY;
    s.alt = c->viewZ;
    s.head = c->ourHead;
    s.pitch = c->ourPitch;
    s.roll = c->ourRoll;
    s.mapX = c->viewX_;
    s.mapY = c->viewY_;
    s.knots = c->knots;
    s.thrust = c->thrust;
    s.setThrust = c->setThrust;
    s.velocity = c->velocity;
    s.altitude = (uint16_t)c->altitude;
    s.fuel = c->fuelRemaining;
    s.gunAmmo = c->gunAmmo;
    for (i = 0; i < 3; i++)
        s.weaponAmmo[i] = c->missleSpec[i].ammo;
    s.curWeapon = c->currentWeaponType;
    s.weaponSel = c->missileSpecIndex;
    s.planeFlags = c->playerPlaneFlags;
    s.ejectState = c->ejectState;
    s.autopilotAlt = c->autopilotAltitude;
    s.airLock = c->airTargetLock;
    s.groundLock = c->groundTargetLock;
    s.radarRange = c->radarScopeRange;
    s.waypointIdx = c->waypointIndex;
    s.waypointBearing = c->waypointBearing;
    s.gearArmed = c->gearDownArmed;
    s.stallSpeed = c->stallSpeed;
    s.cornerSpeed = c->cornerSpeed;
    s.aamSeekerX = c->aamSeekerX;
    s.aamSeekerY = c->aamSeekerY;
    s.rollPitchTrim = c->rollPitchTrim;
    s.gees = (int16_t)c->gees;
    s.damageFlag = c->damageTakenFlag;
    s.damageSeq = c->damageSeq;
    s.alive = (uint8_t)(c->ejectState == 0);
    s.missionEnded = (uint8_t)c->missionEndedFlag[0];
    s.landingType = c->comm.landingType;
    s.score = (uint16_t)c->finalThreatScore;
    s.viewMode = (uint8_t)c->viewMode;
    s.mapMode = (uint8_t)c->mapMode;
    s.activePanelMode = (uint8_t)c->activePanelMode;
    s.directorMode = (uint8_t)c->directorMode;
    s.hudVisible = (uint8_t)c->hudVisible;
    s.detailLevel = (uint8_t)c->detailLevel;
    s.nightMode = (uint8_t)c->nightMode;
    s.autopilotEngaged = (uint8_t)c->autopilotEngaged;
    s.viewTargetObj = c->viewTargetObj;
    s.lastMissileSlot = c->lastMissileSlot;
    s.mapZoomLevel = c->mapZoomLevel;
    s.mapCenterX = c->mapCenterX;
    s.mapCenterY = c->mapCenterY;
    s.crashX = c->crashCamX;
    s.crashY = c->crashCamY;
    s.crashZ = c->crashCamZ;
    s.wreckX = c->wreckX;
    s.wreckY = c->wreckY;
    s.wreckAlt = c->wreckAlt;
    encPlayerState(w, &s);
}

void netSnapBuild(struct NetWriter *w, const struct PlayerSim *players,
                  const int *playerIds, int nPlayers, uint32 stateHash) {
    int i, n;
    nwU8(w, (uint8_t)nPlayers);
    for (i = 0; i < nPlayers; i++) {
        nwU8(w, (uint8_t)playerIds[i]);
        encPlayerBlock(w, &players[i]);
    }

    n = g_groundUnitCount;
    if (n > F15_MAX_SIM_OBJECTS)
        n = F15_MAX_SIM_OBJECTS;
    nwU8(w, (uint8_t)n);
    for (i = 0; i < n; i++) {
        const struct SimObject *o = &g_simObjects[i];
        nwU32(w, (uint32_t)i);
        nwI32(w, o->worldX);
        nwI32(w, o->worldY);
        nwU16(w, o->posX);
        nwU16(w, o->posY);
        nwI16(w, o->alt);
        nwI16(w, o->heading.w);
        nwI16(w, o->pitch);
        nwI16(w, o->bank.w);
        nwI16(w, o->spec);
        nwU16(w, o->flags.w);
        nwI16(w, o->speed);
        nwI16(w, o->objType);
    }

    nwU8(w, F15_MAX_PROJECTILES);
    for (i = 0; i < F15_MAX_PROJECTILES; i++) {
        const struct Projectile *p = &g_projectiles[i];
        nwU32(w, 0x100u + (uint32_t)i);
        nwI32(w, p->fineX);
        nwI32(w, p->fineY);
        nwI16(w, p->alt);
        nwI16(w, p->ttl);
        nwI16(w, p->specIdx);
    }

    nwU8(w, (uint8_t)g_planeCount);
    for (i = 0; i < g_planeCount && i < F15_MAX_MAP_TARGETS; i++) {
        const struct MapTarget *p = &g_planeTable.planes[i];
        nwU16(w, p->mapX);
        nwU16(w, p->mapY);
        nwI16(w, p->active);
        nwI16(w, p->flags);
        nwI16(w, p->alertLevel);
        nwI16(w, p->threatTimer);
        nwI16(w, p->nameIndex);
    }

    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        nwU16(w, mapEvents[i].mapX);
        nwU16(w, mapEvents[i].mapY);
        nwI16(w, mapEvents[i].type);
        nwI16(w, mapEvents[i].ttl);
    }

    nwI16(w, g_missionTick);
    nwI16(w, g_missionStatus);
    nwI16(w, frameTick);
    for (i = 0; i < F15_WAYPOINTS; i++) {
        nwU16(w, waypoints[i].mapX);
        nwU16(w, waypoints[i].mapY);
    }
    for (i = 0; i < 2; i++) {
        nwI16(w, g_targetSlots[i].state);
        nwI16(w, g_targetSlots[i].planeIndex);
        nwI16(w, g_targetSlots[i].viewIndex);
        nwI16(w, g_targetSlots[i].flags);
    }
    nwU32(w, stateHash);
}

/* ------------------------------------------------------ AI observation */
/* Plan §20: the observing pilot's entitled view. Air/ground contacts pass
 * the exact test the radar scope and tacmap use (alive/moving +
 * projectMapPoint on-scope); missiles appear when inbound (RWR) or visible
 * on the scope. obsFull bypasses the scope test for privileged debugging.
 * Must run with the observer's ctx swapped in (the projection and bearing
 * math reads g_viewX_/g_viewY_/g_ourHead/g_radarScopeRange). */
void netObsBuild(struct NetWriter *w, const struct PlayerSim *ctx,
                 int playerIdx, int parkedObjBase, int obsFull,
                 uint32 stateHash) {
    int i, bound, n = 0;
    int ownSlot = (parkedObjBase >= 0 && playerIdx >= 0)
                      ? parkedObjBase + playerIdx
                      : -1;
    size_t countPos;

    encPlayerBlock(w, ctx); /* ownship: identical block to the snapshot's */

    /* RWR: the per-player threat scan ran under this ctx this tick. */
    if (ctx->closestThreatIndex >= 0 && ctx->closestThreatIndex < g_planeCount) {
        nwI16(w, (int16_t)(NET_ID_MAPTARGET_BASE + ctx->closestThreatIndex));
        nwI16(w, ctx->nearestThreatRange);
        computeTargetBearing(g_planeTable.planes[ctx->closestThreatIndex].mapX,
                             g_planeTable.planes[ctx->closestThreatIndex].mapY,
                             1);
        nwI16(w, (int16_t)(g_targetBearing - ctx->ourHead));
    } else {
        nwI16(w, -1);
        nwI16(w, 0);
        nwI16(w, 0);
    }
    nwI16(w, g_missionTick);
    nwI16(w, g_missionStatus);

    countPos = w->len;
    nwU8(w, 0); /* contact count, patched after the loops */

    bound = g_simObjScanBound > 0 ? g_simObjScanBound : g_groundUnitCount;
    for (i = 0; i < bound && i < F15_MAX_SIM_OBJECTS; i++) {
        const struct SimObject *o = &g_simObjects[i];
        struct NetObsContact c;
        int isPlayer;
        if (i == ownSlot || !(o->flags.b[0] & 2))
            continue;
        if (!obsFull) {
            if (o->speed == 0)
                continue;
            projectMapPoint(o->posX, o->posY);
            if (g_projDepth == -1)
                continue;
        }
        isPlayer = (o->flags.b[1] & SIMFLAG_B1_REMOTE_PLAYER) != 0;
        memset(&c, 0, sizeof(c));
        c.id = isPlayer ? NET_ID_PLAYER_BASE + (uint16_t)o->objType
                        : (NetEntityId)i;
        computeTargetBearing(o->posX, o->posY, 1);
        c.relBear = (int16_t)(g_targetBearing - ctx->ourHead);
        c.range = (uint16_t)g_targetRange;
        c.altDelta = (int16_t)(o->alt - (int16_t)g_viewZ);
        c.heading = o->heading.w;
        c.speed = o->speed;
        c.kind = isPlayer ? OBSK_PLAYER : OBSK_AIRCRAFT;
        if (i == ctx->airTargetLock)
            c.flags |= OBSF_LOCKED_AIR;
        encObsContact(w, &c);
        if (++n >= F15_OBS_MAX_CONTACTS)
            goto done;
    }
    /* Missiles: RWR-entitled when inbound at this observer, visible-trail
     * entitled when on the scope. "Inbound" means the shot seeks ME:
     * slots 0-7 are threat shots whose targetPlayer is the engaged victim;
     * slots 8+ are player-fired and targetPlayer is the SHOOTER - for those
     * inbound means their targetLock is this observer's parked object. */
    for (i = 0; i < F15_MAX_PROJECTILES; i++) {
        const struct Projectile *p = &g_projectiles[i];
        struct NetObsContact c;
        int16_t mx, my;
        int inbound;
        if (p->ttl == 0)
            continue;
        mx = (int16_t)(p->fineX >> 5);
        my = (int16_t)(p->fineY >> 5);
        if (i < 8) {
            inbound = p->targetPlayer == playerIdx;
        } else {
            inbound = p->targetLock >= 0 &&
                      p->targetLock < F15_MAX_SIM_OBJECTS &&
                      (g_simObjects[p->targetLock].flags.b[1] &
                       SIMFLAG_B1_REMOTE_PLAYER) &&
                      g_simObjects[p->targetLock].objType == playerIdx;
        }
        if (!inbound && !obsFull) {
            projectMapPoint(mx, my);
            if (g_projDepth == -1)
                continue;
        }
        memset(&c, 0, sizeof(c));
        c.id = NET_ID_PROJECTILE_BASE + (uint32_t)i;
        computeTargetBearing(mx, my, 1);
        c.relBear = (int16_t)(g_targetBearing - ctx->ourHead);
        c.range = (uint16_t)g_targetRange;
        c.altDelta = (int16_t)(p->alt - (int16_t)g_viewZ);
        c.kind = OBSK_MISSILE;
        if (inbound)
            c.flags |= OBSF_INBOUND;
        encObsContact(w, &c);
        if (++n >= F15_OBS_MAX_CONTACTS)
            goto done;
    }
    /* Ground/naval sites: the tacmap entitlement - every live site inside
     * the scope region (destroyed sites report 0x80 and are omitted). */
    for (i = 0; i < g_planeCount && i < F15_MAX_MAP_TARGETS; i++) {
        const struct MapTarget *t = &g_planeTable.planes[i];
        struct NetObsContact c;
        if (t->flags & 0x80)
            continue;
        if (!obsFull) {
            projectMapPoint(t->mapX, t->mapY);
            if (g_projDepth == -1)
                continue;
        }
        memset(&c, 0, sizeof(c));
        c.id = NET_ID_MAPTARGET_BASE + (uint32_t)i;
        computeTargetBearing(t->mapX, t->mapY, 1);
        c.relBear = (int16_t)(g_targetBearing - ctx->ourHead);
        c.range = (uint16_t)g_targetRange;
        c.altDelta = (int16_t)(-(int16_t)g_viewZ); /* sites sit at ground */
        c.kind = OBSK_SITE;
        if (t->active)
            c.flags |= OBSF_ACTIVE;
        if (i == ctx->groundTargetLock)
            c.flags |= OBSF_LOCKED_GND;
        if (i == ctx->closestThreatIndex)
            c.flags |= OBSF_THREAT;
        encObsContact(w, &c);
        if (++n >= F15_OBS_MAX_CONTACTS)
            break;
    }
done:
    if (!w->overflow)
        w->buf[countPos] = (uint8_t)n;
    nwU32(w, stateHash);
}

/* ------------------------------------------------------------ apply side */

int netPlayerObjectSlot(int idx) {
    int slot;
    if (idx < 0 || idx >= F15_MAX_PLAYERS || s_worldObjCount < 0)
        return -1;
    slot = s_worldObjCount + idx;
    return slot < F15_MAX_SIM_OBJECTS ? slot : -1;
}

/* planeTable slot for a remote pilot (tacmap + scope target blips). */
static int netPlayerPlaneSlot(int idx) {
    int slot;
    if (idx < 0 || idx >= F15_MAX_PLAYERS || s_worldPlaneCount < 0)
        return -1;
    slot = s_worldPlaneCount + idx;
    return slot < F15_MAX_MAP_TARGETS ? slot : -1;
}

void netPlayerPublishObject(int idx, const struct NetPlayerState *s) {
    int slot = netPlayerObjectSlot(idx);
    int pslot = netPlayerPlaneSlot(idx);
    struct SimObject *o;
    struct MapTarget *t;
    s_seenMask |= 1u << idx;
    /* Only mark parked when a slot was actually allocated - a failed alloc
     * must not extend the render/radar iteration bounds past the arrays. */
    if (slot >= 0 || pslot >= 0)
        s_parkedMask |= 1u << idx;
    if (slot >= 0) {
        o = &g_simObjects[slot];
        /* Two Y conventions, kept separate:
         * - render space (worldY): object Y is inverted vs player ViewY -
         *   ownship draws at 0x01000000 - g_ViewY (egtarget.c).
         * - map space (posY, viewY_): 0x8000 - (ViewY>>5) - carried on the
         *   wire as s->mapY; projectWorldToHud/computeTargetBearing consume
         *   pos* in this convention. */
        o->worldX = s->worldX;
        o->worldY = (int32)(0x01000000L - s->worldY);
        o->posX = s->mapX;
        o->posY = s->mapY;
        o->alt = s->alt;
        o->heading.w = s->head;
        o->pitch = s->pitch;
        o->bank.w = s->roll;
        o->spec = 0;
        o->speed = s->knots;
        o->objType = pslot;    /* back-link to the parked planeTable entry */
        o->flags.b[0] = s->alive ? 2 : 0; /* alive bit (world objects) */
        /* remote-pilot marker: renderer draws the player F-15 model for these */
        o->flags.b[1] = SIMFLAG_B1_REMOTE_PLAYER;
        if (s->planeFlags & 1)
            o->flags.b[1] |= SIMFLAG_B1_GEAR_DOWN;
    }
    if (pslot >= 0) {
        t = &g_planeTable.planes[pslot];
        /* radar/tacmap entries use the same map convention as viewX_/viewY_ */
        t->mapX = s->mapX;
        t->mapY = s->mapY;
        t->active = 1;
        /* 0x400 = aircraft class, 0x01 = air unit: renders as the airborne
         * blip on the tacmap and a target marker on the scope. */
        t->flags = s->alive ? 0x401 : 0x80;
        t->alertLevel = 0;
        t->threatTimer = 0;
        t->nameIndex = 0;
        t->secondaryNameIndex = 0;
    }
}

/* Decoded snapshot scratch: the whole payload lands here first; live globals
 * only change in the commit pass after every byte validated (atomic apply -
 * a truncated/invalid packet mutates nothing). Single consumer, static. */
struct SnapTmp {
    uint8_t nPlayers;
    uint8_t ids[F15_MAX_PLAYERS];
    struct NetPlayerState players[F15_MAX_PLAYERS];
    uint8_t nObjs;
    struct NetSimObject objs[F15_MAX_SIM_OBJECTS];
    uint8_t nProj;
    struct NetProjectile projs[F15_MAX_PROJECTILES];
    uint8_t nPlanes;
    struct NetMapTarget planes[F15_MAX_MAP_TARGETS];
    struct NetMapEvent events[F15_MAX_MAP_EVENTS];
    int16_t missionTick, missionStatus, frameTick;
    uint16_t wpX[F15_WAYPOINTS], wpY[F15_WAYPOINTS];
    int16_t tsState[2], tsPlane[2], tsView[2], tsFlags[2];
    uint32_t stateHash;
};

static void applyPlayerGlobals(const struct NetPlayerState *s);

int netSnapApply(struct NetReader *r, int playerId) {
    static struct SnapTmp t;
    int i;

    /* ---- decode: temp storage only, no live-state writes ---- */
    t.nPlayers = nrU8(r);
    if (t.nPlayers > F15_MAX_PLAYERS)
        return 0; /* count overflow would misalign the rest of the payload */
    for (i = 0; i < t.nPlayers; i++) {
        t.ids[i] = nrU8(r);
        decPlayerState(r, &t.players[i]);
    }
    t.nObjs = nrU8(r);
    if (t.nObjs > F15_MAX_SIM_OBJECTS)
        return 0;
    for (i = 0; i < t.nObjs; i++)
        decSimObject(r, &t.objs[i]);
    t.nProj = nrU8(r);
    if (t.nProj > F15_MAX_PROJECTILES)
        return 0;
    for (i = 0; i < t.nProj; i++)
        decProjectile(r, &t.projs[i]);
    t.nPlanes = nrU8(r);
    if (t.nPlanes > F15_MAX_MAP_TARGETS)
        return 0;
    for (i = 0; i < t.nPlanes; i++)
        decMapTarget(r, &t.planes[i]);
    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        t.events[i].mapX = nrU16(r);
        t.events[i].mapY = nrU16(r);
        t.events[i].type = nrI16(r);
        t.events[i].ttl = nrI16(r);
    }
    t.missionTick = nrI16(r);
    t.missionStatus = nrI16(r);
    t.frameTick = nrI16(r);
    for (i = 0; i < F15_WAYPOINTS; i++) {
        t.wpX[i] = nrU16(r);
        t.wpY[i] = nrU16(r);
    }
    for (i = 0; i < 2; i++) {
        t.tsState[i] = nrI16(r);
        t.tsPlane[i] = nrI16(r);
        t.tsView[i] = nrI16(r);
        t.tsFlags[i] = nrI16(r);
    }
    t.stateHash = nrU32(r);

    /* ---- validate: integrity, bounds, ids, staleness ---- */
    if (r->underrun)
        return 0;
    for (i = 0; i < t.nPlayers; i++) {
        int j;
        if (t.ids[i] >= F15_MAX_PLAYERS)
            return 0;
        for (j = 0; j < i; j++)
            if (t.ids[j] == t.ids[i])
                return 0; /* duplicate player block */
    }
    for (i = 0; i < t.nObjs; i++)
        if (t.objs[i].id >= F15_MAX_SIM_OBJECTS)
            return 0;
    for (i = 0; i < t.nProj; i++)
        if ((t.projs[i].id & ~0xFFu) != NET_ID_PROJECTILE_BASE ||
            (t.projs[i].id & 0xFFu) >= F15_MAX_PROJECTILES)
            return 0;
    /* stale/out-of-order delivery must never roll live state back
     * (int16 diff handles the wrap; negative ticks compare correctly) */
    if (s_haveLastTick && (int16_t)(t.frameTick - s_lastFrameTick) <= 0)
        return 0;

    /* ---- commit: validated, now mutate ---- */
    s_seenMask = 0;
    for (i = 0; i < t.nPlayers; i++) {
        if (t.ids[i] == playerId) {
            /* own aircraft: write the authoritative block into the globals the
             * renderer/cockpit read. */
            applyPlayerGlobals(&t.players[i]);
        } else {
            netPlayerPublishObject(t.ids[i], &t.players[i]);
        }
    }
    /* Departed players: unpark their object/plane slots so husks don't linger. */
    {
        unsigned gone = s_parkedMask & ~s_seenMask;
        while (gone) {
            int pid = __builtin_ctz(gone);
            int slot = netPlayerObjectSlot(pid);
            int pslot = netPlayerPlaneSlot(pid);
            gone &= gone - 1;
            s_parkedMask &= ~(1u << pid);
            if (slot >= 0)
                memset(&g_simObjects[slot], 0, sizeof(g_simObjects[slot]));
            if (pslot >= 0)
                memset(&g_planeTable.planes[pslot], 0,
                       sizeof(g_planeTable.planes[pslot]));
        }
        /* Extend the client-side iteration bounds to cover parked player slots
         * (3D render, radar scope, target scan and the interp sweep loop
         * 0..g_groundUnitCount-1; the tacmap loops 0..g_planeCount-1). */
        if (s_worldObjCount >= 0) {
            int hi = s_worldObjCount;
            unsigned m = s_parkedMask;
            while (m) {
                int pid = 31 - __builtin_clz(m);
                int slot = s_worldObjCount + pid + 1;
                if (slot > hi) hi = slot;
                m &= m - 1;
            }
            if (hi > F15_MAX_SIM_OBJECTS)
                hi = F15_MAX_SIM_OBJECTS;
            g_groundUnitCount = (int16)hi;
        }
        if (s_worldPlaneCount >= 0) {
            int hi = s_worldPlaneCount;
            unsigned m = s_parkedMask;
            while (m) {
                int pid = 31 - __builtin_clz(m);
                int slot = s_worldPlaneCount + pid + 1;
                if (slot > hi) hi = slot;
                m &= m - 1;
            }
            if (hi > F15_MAX_MAP_TARGETS)
                hi = F15_MAX_MAP_TARGETS;
            g_planeCount = (int16)hi;
        }
    }

    for (i = 0; i < t.nObjs; i++) {
        int slot = (int)t.objs[i].id;
        g_simObjects[slot].worldX = t.objs[i].worldX;
        g_simObjects[slot].worldY = t.objs[i].worldY;
        g_simObjects[slot].posX = t.objs[i].posX;
        g_simObjects[slot].posY = t.objs[i].posY;
        g_simObjects[slot].alt = t.objs[i].alt;
        g_simObjects[slot].heading.w = t.objs[i].head;
        g_simObjects[slot].pitch = t.objs[i].pitch;
        g_simObjects[slot].bank.w = t.objs[i].bank;
        g_simObjects[slot].spec = t.objs[i].spec;
        g_simObjects[slot].flags.w = t.objs[i].flags;
        g_simObjects[slot].speed = t.objs[i].speed;
        g_simObjects[slot].objType = t.objs[i].objType;
    }

    for (i = 0; i < t.nProj; i++) {
        int slot = (int)(t.projs[i].id & 0xFFu);
        g_projectiles[slot].fineX = t.projs[i].fineX;
        g_projectiles[slot].fineY = t.projs[i].fineY;
        g_projectiles[slot].alt = t.projs[i].alt;
        g_projectiles[slot].ttl = t.projs[i].ttl;
        g_projectiles[slot].specIdx = t.projs[i].specIdx;
        g_projectiles[slot].mapX = (uint16_t)(t.projs[i].fineX >> 5);
        g_projectiles[slot].mapY = (uint16_t)(t.projs[i].fineY >> 5);
    }

    for (i = 0; i < t.nPlanes; i++) {
        struct MapTarget *p = &g_planeTable.planes[i];
        p->mapX = t.planes[i].mapX;
        p->mapY = t.planes[i].mapY;
        p->active = t.planes[i].active;
        p->flags = t.planes[i].flags;
        p->alertLevel = t.planes[i].alertLevel;
        p->threatTimer = t.planes[i].threatTimer;
        p->nameIndex = t.planes[i].nameIndex;
    }

    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        mapEvents[i].mapX = t.events[i].mapX;
        mapEvents[i].mapY = t.events[i].mapY;
        mapEvents[i].type = t.events[i].type;
        mapEvents[i].ttl = t.events[i].ttl;
    }

    g_missionTick = t.missionTick;
    g_missionStatus = t.missionStatus;
    frameTick = t.frameTick; /* authoritative sim tick - drives the view ring etc. */
    s_lastFrameTick = t.frameTick;
    s_haveLastTick = 1;
    for (i = 0; i < F15_WAYPOINTS; i++) {
        waypoints[i].mapX = t.wpX[i];
        waypoints[i].mapY = t.wpY[i];
    }
    for (i = 0; i < 2; i++) {
        g_targetSlots[i].state = t.tsState[i];
        g_targetSlots[i].planeIndex = t.tsPlane[i];
        g_targetSlots[i].viewIndex = t.tsView[i];
        g_targetSlots[i].flags = t.tsFlags[i];
    }
    (void)t.stateHash; /* compare against client-side hash later */
    return 1;
}

/* own player block -> globals (client). Kept separate so the decoder stays
 * the single writer of the cockpit view state. */
static void applyPlayerGlobals(const struct NetPlayerState *s) {
    int i;
    g_ViewX = s->worldX;
    g_ViewY = s->worldY;
    g_viewZ = s->alt;
    g_ourHead = s->head;
    g_ourPitch = s->pitch;
    g_ourRoll = s->roll;
    g_viewX_ = s->mapX;
    g_viewY_ = s->mapY;
    g_knots = s->knots;
    g_thrust = s->thrust;
    g_setThrust = s->setThrust;
    g_velocity = s->velocity;
    g_altitude = s->altitude;
    g_fuelRemaining = s->fuel;
    g_gunAmmo = s->gunAmmo;
    for (i = 0; i < 3; i++)
        missleSpec[i].ammo = s->weaponAmmo[i];
    g_currentWeaponType = s->curWeapon;
    missileSpecIndex = s->weaponSel;
    g_playerPlaneFlags = s->planeFlags;
    g_ejectState = s->ejectState;
    g_autopilotAltitude = s->autopilotAlt;
    g_airTargetLock = s->airLock;
    g_groundTargetLock = s->groundLock;
    g_radarScopeRange = s->radarRange;
    waypointIndex = s->waypointIdx;
    g_waypointBearing = s->waypointBearing;
    g_gearDownArmed = s->gearArmed;
    g_stallSpeed = s->stallSpeed;
    g_cornerSpeed = s->cornerSpeed;
    g_aamSeekerX = s->aamSeekerX;
    g_aamSeekerY = s->aamSeekerY;
    g_rollPitchTrim = s->rollPitchTrim;
    g_gees = s->gees;
    /* Damage events are numbered, not level-triggered: fire the HUD shake
     * exactly once per new damageSeq. (The server's damageTakenFlag is a
     * per-tick transient it releases each tick - copying it as state used to
     * re-fire the shake every snapshot when the flag latched.) */
    if (s->damageSeq != s_lastDamageSeq) {
        s_lastDamageSeq = s->damageSeq;
        g_damageTakenFlag = 1;
    }
    commData->landingType = s->landingType;
    g_finalThreatScore = s->score;
    /* Sim-driven display state comes back over the wire (view/panel commands
     * execute in the server ctx, and director/autopilot code can force them).
     * Deliberately NOT applied: mapZoomLevel/mapCenterX/mapCenterY (the local
     * tacmap blip pass owns zoom/centering). detailLevel/nightMode ARE applied:
     * they gate the sky dome/terrain fill/LOD tables and nightMode is set
     * sim-side at mission start, so the client has no other source for them. */
    g_viewMode = (ViewMode)s->viewMode;
    g_mapMode = s->mapMode;
    g_activePanelMode = s->activePanelMode;
    g_directorMode = s->directorMode;
    g_viewTargetObj = s->viewTargetObj;
    g_lastMissileSlot = s->lastMissileSlot;
    g_autopilotEngaged = s->autopilotEngaged;
    if (g_detailLevel != s->detailLevel) {
        g_detailLevel = s->detailLevel;
        setupLodDistances(); /* LOD/cull distance tables derive from it */
    }
    g_nightMode = s->nightMode;
    /* Camera-interp inputs: crash-cam eye and wreck/parachute pose. */
    g_crashCamX = s->crashX;
    g_crashCamY = s->crashY;
    g_crashCamZ = s->crashZ;
    g_wreckX = s->wreckX;
    g_wreckY = s->wreckY;
    g_wreckAlt = s->wreckAlt;
}

/* Public wrappers (snapshot.h): single player-block encode/apply, used by
 * tests and by netclient when a lone block needs decoding outside a full
 * snapshot. */
void netEncPlayerFromCtx(struct NetWriter *w, const struct PlayerSim *c) {
    encPlayerBlock(w, c);
}

void netApplyPlayerToGlobals(struct NetReader *r) {
    struct NetPlayerState s;
    decPlayerState(r, &s);
    applyPlayerGlobals(&s);
}

} /* extern "C" */
