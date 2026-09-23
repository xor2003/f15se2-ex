#ifndef F15_SE2_EGTARGET
#define F15_SE2_EGTARGET
/* public interface of egtarget.c */

/* per-step target acquisition (air/ground locks) - sim update, not render */
void simTargetLock(void);
void updateTargetLock();
void computeHitRadii(void);
int aircraftModelRadius(int spec);
int groundModelRadius(int nameIndex);
/* per-step gun tracer hit tests + damage (sim update, not render) */
void simBulletHits(void);
void drawWorldEffects(void);
void drawHudWorldOverlay(void);
int16 findWaypointEntry(int16 mapX, int16 mapY);
int16 computeLoftAngle();
int16 getTargetSymbol(int16 wpIdx);
/* shared aircraft-identity resolution: remote pilots are the F-15 (spec
 * field unused), everything else via aircraftTypes[spec] */
int16 simObjectViewModel(int objIdx, int nearModel);
const char *simObjectTypeName(int objIdx);

#endif /* F15_SE2_EGTARGET */
