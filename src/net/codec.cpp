/*
 * codec.cpp - protocol payload encode/decode (plan §26).
 */
#include "codec.h"

void netMsgWriteHeader(struct NetWriter *w, uint8_t type, NetTick tick,
                       uint16_t payloadLen) {
    nwU32(w, F15_NET_MAGIC);
    nwU16(w, F15_NET_VERSION);
    nwU8(w, type);
    nwU16(w, payloadLen);
    nwU32(w, tick);
}

int netMsgReadHeader(struct NetReader *r, uint8_t *type, NetTick *tick,
                     uint16_t *payloadLen) {
    if (nrLeft(r) < NET_MSG_HEADER_SIZE)
        return 0;
    if (nrU32(r) != F15_NET_MAGIC)
        return 0;
    if (nrU16(r) != F15_NET_VERSION)
        return 0;
    *type = nrU8(r);
    *payloadLen = nrU16(r);
    *tick = nrU32(r);
    return r->underrun ? 0 : 1;
}

void encHello(struct NetWriter *w, const struct NetHello *v) {
    nwU32(w, v->protoVer);
    nwU8(w, v->role);
    nwStr(w, v->name, F15_NAME_LEN);
}

int decHello(struct NetReader *r, struct NetHello *v) {
    v->protoVer = nrU32(r);
    v->role = nrU8(r);
    nrStr(r, v->name, F15_NAME_LEN);
    return !r->underrun;
}

void encHelloAck(struct NetWriter *w, const struct NetHelloAck *v) {
    nwU8(w, v->playerId);
    nwU8(w, v->tickRate);
    nwU16(w, v->flags);
    nwU32(w, v->serverTick);
}

int decHelloAck(struct NetReader *r, struct NetHelloAck *v) {
    v->playerId = nrU8(r);
    v->tickRate = nrU8(r);
    v->flags = nrU16(r);
    v->serverTick = nrU32(r);
    return !r->underrun;
}

void encInput(struct NetWriter *w, const struct NetInput *v) {
    int i;
    nwU32(w, v->clientSeq);
    nwU32(w, v->clientTick);
    nwU32(w, v->lastSnapAck);
    nwU8(w, v->joyX);
    nwU8(w, v->joyY);
    nwU8(w, v->buttons);
    nwU8(w, v->nCmds);
    for (i = 0; i < F15_MAX_COMMANDS; i++)
        nwU8(w, i < v->nCmds ? v->cmds[i] : 0);
}

int decInput(struct NetReader *r, struct NetInput *v) {
    int i;
    v->clientSeq = nrU32(r);
    v->clientTick = nrU32(r);
    v->lastSnapAck = nrU32(r);
    v->joyX = nrU8(r);
    v->joyY = nrU8(r);
    v->buttons = nrU8(r);
    v->nCmds = nrU8(r);
    for (i = 0; i < F15_MAX_COMMANDS; i++)
        v->cmds[i] = nrU8(r);
    if (v->nCmds > F15_MAX_COMMANDS)
        v->nCmds = F15_MAX_COMMANDS; /* clamp, don't reject whole frame */
    return !r->underrun;
}

void encPlayerState(struct NetWriter *w, const struct NetPlayerState *v) {
    int i;
    nwI32(w, v->worldX);
    nwI32(w, v->worldY);
    nwI16(w, v->alt);
    nwI16(w, v->head);
    nwI16(w, v->pitch);
    nwI16(w, v->roll);
    nwI16(w, v->mapX);
    nwI16(w, v->mapY);
    nwI16(w, v->knots);
    nwI16(w, v->thrust);
    nwI16(w, v->setThrust);
    nwI32(w, v->velocity);
    nwU16(w, v->altitude);
    nwI16(w, v->fuel);
    nwI16(w, v->gunAmmo);
    for (i = 0; i < 3; i++)
        nwI16(w, v->weaponAmmo[i]);
    nwI16(w, v->curWeapon);
    nwI16(w, v->weaponSel);
    nwI16(w, v->planeFlags);
    nwI16(w, v->ejectState);
    nwI16(w, v->autopilotAlt);
    nwI16(w, v->airLock);
    nwI16(w, v->groundLock);
    nwI16(w, v->radarRange);
    nwI16(w, v->waypointIdx);
    nwI16(w, v->waypointBearing);
    nwI16(w, v->gearArmed);
    nwI16(w, v->stallSpeed);
    nwI16(w, v->cornerSpeed);
    nwI16(w, v->aamSeekerX);
    nwI16(w, v->aamSeekerY);
    nwI16(w, v->rollPitchTrim);
    nwI16(w, v->gees);
    nwI16(w, v->damageFlag);
    nwU8(w, v->alive);
    nwU8(w, v->missionEnded);
    nwI16(w, v->landingType);
    nwU16(w, v->score);
    nwU8(w, v->viewMode);
    nwU8(w, v->mapMode);
    nwU8(w, v->activePanelMode);
    nwU8(w, v->directorMode);
    nwU8(w, v->hudVisible);
    nwU8(w, v->detailLevel);
    nwU8(w, v->nightMode);
    nwU8(w, v->autopilotEngaged);
    nwI16(w, v->viewTargetObj);
    nwI16(w, v->lastMissileSlot);
    nwI16(w, v->mapZoomLevel);
    nwI16(w, v->mapCenterX);
    nwI16(w, v->mapCenterY);
    nwI16(w, v->crashX);
    nwI16(w, v->crashY);
    nwI16(w, v->crashZ);
    nwI16(w, v->wreckX);
    nwI16(w, v->wreckY);
    nwI16(w, v->wreckAlt);
}

void decPlayerState(struct NetReader *r, struct NetPlayerState *v) {
    int i;
    v->worldX = nrI32(r);
    v->worldY = nrI32(r);
    v->alt = nrI16(r);
    v->head = nrI16(r);
    v->pitch = nrI16(r);
    v->roll = nrI16(r);
    v->mapX = nrI16(r);
    v->mapY = nrI16(r);
    v->knots = nrI16(r);
    v->thrust = nrI16(r);
    v->setThrust = nrI16(r);
    v->velocity = nrI32(r);
    v->altitude = nrU16(r);
    v->fuel = nrI16(r);
    v->gunAmmo = nrI16(r);
    for (i = 0; i < 3; i++)
        v->weaponAmmo[i] = nrI16(r);
    v->curWeapon = nrI16(r);
    v->weaponSel = nrI16(r);
    v->planeFlags = nrI16(r);
    v->ejectState = nrI16(r);
    v->autopilotAlt = nrI16(r);
    v->airLock = nrI16(r);
    v->groundLock = nrI16(r);
    v->radarRange = nrI16(r);
    v->waypointIdx = nrI16(r);
    v->waypointBearing = nrI16(r);
    v->gearArmed = nrI16(r);
    v->stallSpeed = nrI16(r);
    v->cornerSpeed = nrI16(r);
    v->aamSeekerX = nrI16(r);
    v->aamSeekerY = nrI16(r);
    v->rollPitchTrim = nrI16(r);
    v->gees = nrI16(r);
    v->damageFlag = nrI16(r);
    v->alive = nrU8(r);
    v->missionEnded = nrU8(r);
    v->landingType = nrI16(r);
    v->score = nrU16(r);
    v->viewMode = nrU8(r);
    v->mapMode = nrU8(r);
    v->activePanelMode = nrU8(r);
    v->directorMode = nrU8(r);
    v->hudVisible = nrU8(r);
    v->detailLevel = nrU8(r);
    v->nightMode = nrU8(r);
    v->autopilotEngaged = nrU8(r);
    v->viewTargetObj = nrI16(r);
    v->lastMissileSlot = nrI16(r);
    v->mapZoomLevel = nrI16(r);
    v->mapCenterX = nrI16(r);
    v->mapCenterY = nrI16(r);
    v->crashX = nrI16(r);
    v->crashY = nrI16(r);
    v->crashZ = nrI16(r);
    v->wreckX = nrI16(r);
    v->wreckY = nrI16(r);
    v->wreckAlt = nrI16(r);
}

void encSimObject(struct NetWriter *w, const struct NetSimObject *v) {
    nwU32(w, v->id);
    nwI32(w, v->worldX);
    nwI32(w, v->worldY);
    nwU16(w, v->posX);
    nwU16(w, v->posY);
    nwI16(w, v->alt);
    nwI16(w, v->head);
    nwI16(w, v->pitch);
    nwI16(w, v->bank);
    nwI16(w, v->spec);
    nwU16(w, v->flags);
    nwI16(w, v->speed);
    nwI16(w, v->objType);
}

void decSimObject(struct NetReader *r, struct NetSimObject *v) {
    v->id = nrU32(r);
    v->worldX = nrI32(r);
    v->worldY = nrI32(r);
    v->posX = nrU16(r);
    v->posY = nrU16(r);
    v->alt = nrI16(r);
    v->head = nrI16(r);
    v->pitch = nrI16(r);
    v->bank = nrI16(r);
    v->spec = nrI16(r);
    v->flags = nrU16(r);
    v->speed = nrI16(r);
    v->objType = nrI16(r);
}

void encProjectile(struct NetWriter *w, const struct NetProjectile *v) {
    nwU32(w, v->id);
    nwI32(w, v->fineX);
    nwI32(w, v->fineY);
    nwI16(w, v->alt);
    nwI16(w, v->ttl);
    nwI16(w, v->specIdx);
}

void decProjectile(struct NetReader *r, struct NetProjectile *v) {
    v->id = nrU32(r);
    v->fineX = nrI32(r);
    v->fineY = nrI32(r);
    v->alt = nrI16(r);
    v->ttl = nrI16(r);
    v->specIdx = nrI16(r);
}

void encMapTarget(struct NetWriter *w, const struct NetMapTarget *v) {
    nwU16(w, v->mapX);
    nwU16(w, v->mapY);
    nwI16(w, v->active);
    nwI16(w, v->flags);
    nwI16(w, v->alertLevel);
    nwI16(w, v->threatTimer);
    nwI16(w, v->nameIndex);
}

void decMapTarget(struct NetReader *r, struct NetMapTarget *v) {
    v->mapX = nrU16(r);
    v->mapY = nrU16(r);
    v->active = nrI16(r);
    v->flags = nrI16(r);
    v->alertLevel = nrI16(r);
    v->threatTimer = nrI16(r);
    v->nameIndex = nrI16(r);
}

void encEvent(struct NetWriter *w, const struct NetEvent *v) {
    nwU16(w, v->eventType);
    nwU32(w, v->subject);
    nwU32(w, v->object);
    nwI16(w, v->arg);
    nwStr(w, v->text, sizeof(v->text));
}

int decEvent(struct NetReader *r, struct NetEvent *v) {
    v->eventType = nrU16(r);
    v->subject = nrU32(r);
    v->object = nrU32(r);
    v->arg = nrI16(r);
    nrStr(r, v->text, sizeof(v->text) - 1);
    v->text[sizeof(v->text) - 1] = 0;
    return !r->underrun;
}
