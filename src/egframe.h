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
int objectToScreen(int mapX, int mapY, int16 *outScreenX, int16 *outScreenY);
int randomRange(int);
int16 gunSpreadAngle(void);

#endif /* F15_SE2_EGFRAME */
