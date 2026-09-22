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
#include "egtypes.h"
#include "struct.h"

extern "C" {

void decApplyOwnPlayer(struct NetReader *r); /* fwd: defined below */

/* entity id layout (v1): 0..19 simObjects, 0x100+ projectiles,
 * 0x200+i remote players (published into reserved simObject slots). */
#define NET_ID_PLAYER_BASE 0x200u

/* ---------------------------------------------------------------- setup */

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
    s.alive = (uint8_t)(c->ejectState == 0);
    s.missionEnded = (uint8_t)c->missionEndedFlag[0];
    s.landingType = c->comm.landingType;
    s.score = (uint16_t)c->finalThreatScore;
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

/* ------------------------------------------------------------ apply side */

/* Where remote players are parked inside g_simObjects for rendering/radar:
 * the tail slots past g_groundUnitCount. */
int netPlayerObjectSlot(int idx) {
    int slot = F15_MAX_SIM_OBJECTS - 1 - idx;
    return (slot >= g_groundUnitCount && idx >= 0) ? slot : -1;
}

void netPlayerPublishObject(int idx, const struct NetPlayerState *s) {
    int slot = netPlayerObjectSlot(idx);
    struct SimObject *o;
    if (slot < 0)
        return;
    o = &g_simObjects[slot];
    o->worldX = s->worldX;
    o->worldY = s->worldY;
    o->posX = (uint16_t)(s->worldX >> 5);
    o->posY = (uint16_t)(s->worldY >> 5);
    o->alt = s->alt;
    o->heading.w = s->head;
    o->pitch = s->pitch;
    o->bank.w = s->roll;
    o->spec = 0;             /* F-15 model */
    o->speed = s->knots;
    o->objType = 0;
    o->flags.b[0] = s->alive ? 2 : 0; /* alive bit (world objects) */
    o->flags.b[1] = 0;
}

int netSnapApply(struct NetReader *r, int playerId) {
    int i, n;
    uint8_t nPlayers = nrU8(r);
    for (i = 0; i < nPlayers; i++) {
        uint8_t pid = nrU8(r);
        if (pid == playerId) {
            /* own aircraft: write the authoritative block into the globals the
             * renderer/cockpit read. */
            decApplyOwnPlayer(r);
        } else {
            struct NetPlayerState s;
            decPlayerState(r, &s);
            netPlayerPublishObject(pid, &s);
        }
    }

    n = nrU8(r);
    for (i = 0; i < n; i++) {
        struct NetSimObject o;
        int slot;
        decSimObject(r, &o);
        slot = (int)o.id;
        if (slot < 0 || slot >= F15_MAX_SIM_OBJECTS)
            continue;
        g_simObjects[slot].worldX = o.worldX;
        g_simObjects[slot].worldY = o.worldY;
        g_simObjects[slot].posX = o.posX;
        g_simObjects[slot].posY = o.posY;
        g_simObjects[slot].alt = o.alt;
        g_simObjects[slot].heading.w = o.head;
        g_simObjects[slot].pitch = o.pitch;
        g_simObjects[slot].bank.w = o.bank;
        g_simObjects[slot].spec = o.spec;
        g_simObjects[slot].flags.w = o.flags;
        g_simObjects[slot].speed = o.speed;
        g_simObjects[slot].objType = o.objType;
    }

    n = nrU8(r);
    for (i = 0; i < n; i++) {
        struct NetProjectile p;
        int slot;
        decProjectile(r, &p);
        slot = (int)(p.id & 0xFFu);
        if (slot >= F15_MAX_PROJECTILES)
            continue;
        g_projectiles[slot].fineX = p.fineX;
        g_projectiles[slot].fineY = p.fineY;
        g_projectiles[slot].alt = p.alt;
        g_projectiles[slot].ttl = p.ttl;
        g_projectiles[slot].specIdx = p.specIdx;
        g_projectiles[slot].mapX = (uint16_t)(p.fineX >> 5);
        g_projectiles[slot].mapY = (uint16_t)(p.fineY >> 5);
    }

    n = nrU8(r);
    for (i = 0; i < n && i < F15_MAX_MAP_TARGETS; i++) {
        struct MapTarget *p = &g_planeTable.planes[i];
        p->mapX = nrU16(r);
        p->mapY = nrU16(r);
        p->active = nrI16(r);
        p->flags = nrI16(r);
        p->alertLevel = nrI16(r);
        p->threatTimer = nrI16(r);
        p->nameIndex = nrI16(r);
    }

    for (i = 0; i < F15_MAX_MAP_EVENTS; i++) {
        mapEvents[i].mapX = nrU16(r);
        mapEvents[i].mapY = nrU16(r);
        mapEvents[i].type = nrI16(r);
        mapEvents[i].ttl = nrI16(r);
    }

    g_missionTick = nrI16(r);
    g_missionStatus = nrI16(r);
    (void)nrI16(r); /* server frameTick - diagnostic only */
    for (i = 0; i < F15_WAYPOINTS; i++) {
        waypoints[i].mapX = nrU16(r);
        waypoints[i].mapY = nrU16(r);
    }
    for (i = 0; i < 2; i++) {
        g_targetSlots[i].state = nrI16(r);
        g_targetSlots[i].planeIndex = nrI16(r);
        g_targetSlots[i].viewIndex = nrI16(r);
        g_targetSlots[i].flags = nrI16(r);
    }
    (void)nrU32(r); /* stateHash: compare against client-side hash later */
    return !r->underrun;
}

/* own player block -> globals (client). Kept separate so the decoder stays
 * the single writer of the cockpit view state. */
void decApplyOwnPlayer(struct NetReader *r) {
    struct NetPlayerState s;
    int i;
    decPlayerState(r, &s);
    g_ViewX = s.worldX;
    g_ViewY = s.worldY;
    g_viewZ = s.alt;
    g_ourHead = s.head;
    g_ourPitch = s.pitch;
    g_ourRoll = s.roll;
    g_viewX_ = s.mapX;
    g_viewY_ = s.mapY;
    g_knots = s.knots;
    g_thrust = s.thrust;
    g_setThrust = s.setThrust;
    g_velocity = s.velocity;
    g_altitude = s.altitude;
    g_fuelRemaining = s.fuel;
    g_gunAmmo = s.gunAmmo;
    for (i = 0; i < 3; i++)
        missleSpec[i].ammo = s.weaponAmmo[i];
    g_currentWeaponType = s.curWeapon;
    missileSpecIndex = s.weaponSel;
    g_playerPlaneFlags = s.planeFlags;
    g_ejectState = s.ejectState;
    g_autopilotAltitude = s.autopilotAlt;
    g_airTargetLock = s.airLock;
    g_groundTargetLock = s.groundLock;
    g_radarScopeRange = s.radarRange;
    waypointIndex = s.waypointIdx;
    g_waypointBearing = s.waypointBearing;
    g_gearDownArmed = s.gearArmed;
    g_stallSpeed = s.stallSpeed;
    g_cornerSpeed = s.cornerSpeed;
    g_aamSeekerX = s.aamSeekerX;
    g_aamSeekerY = s.aamSeekerY;
    g_rollPitchTrim = s.rollPitchTrim;
    g_gees = s.gees;
    g_damageTakenFlag = s.damageFlag;
    commData->landingType = s.landingType;
    g_finalThreatScore = s.score;
}

} /* extern "C" */
