#ifndef F15_NET_SNAPSHOT_H
#define F15_NET_SNAPSHOT_H
/*
 * snapshot.h - build/apply authoritative world snapshots and mission setup.
 *
 * Server side: netSnapBuild() encodes the live world (full snapshot, plan §11)
 * and netSetupBuild() encodes the mission bootstrap tables (worldImportToEgame
 * products). Client side: netSnapApply()/netSetupApply() write the decoded
 * state into the egdata globals so the stock renderer draws the remote world.
 */
#include <stdint.h>

#include "egplayer.h"
#include "protocol.h"
#include "serialize.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- server side (globals + player ctxs -> wire) --- */
void netSetupBuild(struct NetWriter *w);   /* MISSION_SETUP payload */
void netSnapBuild(struct NetWriter *w, const struct PlayerSim *players,
                  const int *playerIds, int nPlayers, uint32 stateHash);

/* --- client side (wire -> globals) --- */
/* Applies the mission bootstrap to egdata globals. Call BEFORE
 * initMissionStrings()/drawCockpit() so the name table and spawn derive from
 * the received world. */
int netSetupApply(struct NetReader *r);
/* Applies a snapshot: world tables + own aircraft into globals. playerId is
 * this client's assigned slot (other players' blocks update their published
 * SimObjects, not the cockpit globals). */
int netSnapApply(struct NetReader *r, int playerId);

/* ctx -> wire block (server) and wire block -> ctx-independent globals
 * (client owns exactly one cockpit, so applying own block writes globals). */
void netEncPlayerFromCtx(struct NetWriter *w, const struct PlayerSim *c);
void netApplyPlayerToGlobals(struct NetReader *r);

/* Remote players appear to the local renderer as aircraft: their NetPlayerState
 * is mirrored into reserved g_simObjects slots. Returns the simObjects slot
 * used for net player `idx`, or -1 if none free. */
int netPlayerObjectSlot(int idx);
void netPlayerPublishObject(int idx, const struct NetPlayerState *s);

#ifdef __cplusplus
}
#endif

#endif /* F15_NET_SNAPSHOT_H */
