#ifndef F15_SE2_EGFRAME
#define F15_SE2_EGFRAME
/* public interface of egframe.c */
#include "inttype.h"

void countermeasures(int16 eventType);
void resetSimObjectLocks();
void initWeaponLoadout(void);
void drawWeaponAmmo();
void drawWeaponSelectMarker(int16 weaponIdx);
void finalizeMission(int outcome);
void scheduleEventCheck(int16 eventObjIdx, uint16 priority);
void scheduleTimedEvent(ViewMode viewMode, int16 delay);
void appendMapEvent(int16 eventType, int16 eventArg);
void placeString(int16 waypointIdx);
void initMissionStrings();
void updateWorldFrame(void);  /* server: world-only pass of updateFrame() */
void updatePlayerFrame(void); /* server: per-player pass of updateFrame() */

/* The updateFrame() segments (egframeseg.c; server composition in f15world.c).
 * P = player-scoped ctx only, W = world state, per the split in egframeseg.c. */
void framePlayerPre(void);     /* P */
void framePlayerTimers(void);  /* P */
void framePlayerMission(void); /* P */
void frameWorldTick(void);     /* W */
void moveBullets(void);        /* W */
void tryPlayerFire(void);      /* P */
void frameThreatScan(void);   /* ctx part: per-player nearest-threat scan */
/* world part: escort/interceptor spawn, driven by the caller-chosen threat
 * index (server: most-threatened player's scan result; SP: own ctx). */
void frameThreatEscort(int16 threatIdx, int16 threatChanged);
/* Net client: the tacmap backing/blip maintenance the player pass does locally
 * (skipped server-side under g_headlessSim). Called per render frame. */
void frameTacmapBlip(void);
int objectToScreen(int mapX, int mapY, int16 *outScreenX, int16 *outScreenY);
int randomRange(int);
int16 gunSpreadAngle(void);

#endif /* F15_SE2_EGFRAME */
