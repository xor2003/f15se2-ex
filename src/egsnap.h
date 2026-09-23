#ifndef F15_SE2_EGSNAP
#define F15_SE2_EGSNAP
/*
 * egsnap.h - snapshot types and capture/interpolation helpers shared
 * between the local render interpolation in egsys.c and the network
 * client's render glue in net/netrender.c.
 *
 * The render loop interpolates between two captured frames of sim state
 * and writes the blended copy into the live globals only for the draw,
 * then restores the authoritative values. The same machinery serves the
 * single-player sim-step interpolation and the snapshot-driven network
 * client, so the types and helpers live here once.
 */

#include "inttype.h"
#include "net/protocol.h" /* pool bounds are part of the wire contract */

/* Capture bounds: exactly the wire-visible sim pools, so a snapshot
 * capture covers what a NetSnapshot can carry. */
#define SIM_OBJ_MAX F15_MAX_SIM_OBJECTS
#define PROJ_MAX    F15_MAX_PROJECTILES

/* Camera-side state derived from the player each step: own coords (all
 * tracking views), map coords (player->target bearing), crash-cam eye and
 * wreck/parachute pose, plus the HUD reticle inputs (gun-reticle trim,
 * AAM seeker offset) so the reticles glide instead of snapping. */
typedef struct {
    int32 viewX, viewY, viewZ;
    int32 head, pitch, roll;
    int32 mapX, mapY; /* g_viewX_ / g_viewY_ */
    int32 crashX, crashY, crashZ;
    int32 wreckX, wreckY, wreckAlt; /* downed-aircraft wreck/parachute */
    int32 rollPitchTrim, aamSeekerX, aamSeekerY;
} CamSnapshot;

/* Per-sim-object render state. posX/posY are captured separately from
 * worldX/worldY: they are map-space fields in their own convention (for
 * parked remote players posY is 0x8000-(ViewY>>5), not a derivation of
 * the render coords), so they must be interpolated directly. */
typedef struct {
    int32 worldX, worldY;
    uint16 posX, posY;
    int16 alt, head, pitch, bank;
    uint8 alive;
} SimObjSnap;

/* Per-projectile render state: fine position, missile yaw/pitch (the
 * legacy struct overloads worldX/worldY for those), and ttl, which is
 * also the slot-liveness detector (ttl decrements by exactly 1 in flight
 * and a reused slot always passes through ttl==0). */
typedef struct {
    int32 fineX, fineY, alt; /* fineX/fineY: mapX<<5-scale position */
    int16 head, pitch;
    int16 ttl;
} ProjSnap;

/* Capture the live globals into a snapshot / write one back. */
void camCapture(CamSnapshot *s);
void camRestore(const CamSnapshot *s);
void objCapture(SimObjSnap *sim, ProjSnap *proj);
void objRestore(const SimObjSnap *sn, const ProjSnap *pn);

/* Write the num/den interpolation between two snapshots into the live
 * globals (teleport/reuse slots are skipped inside). */
void camApplyInterp(const CamSnapshot *p, const CamSnapshot *n,
                    int64 num, int64 den);
void objApplyInterp(const SimObjSnap *sp, const SimObjSnap *sn,
                    const ProjSnap *pp, const ProjSnap *pn,
                    int64 num, int64 den);

/* Whole-pose heading/pitch/roll interpolation: the gimbal flip at
 * +/-90deg pitch rewrites all three components at once, so a flip in any
 * of them snaps the whole triple to the new representation instead of
 * tweening into an invented pose. */
void lerpPose(int32 h0, int32 p0, int32 r0, int32 h1, int32 p1, int32 r1,
              int64 num, int64 den, int32 *h, int32 *p, int32 *r);

#endif /* F15_SE2_EGSNAP */
