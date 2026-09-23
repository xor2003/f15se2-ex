/*
 * netrender.c - network-client render glue.
 *
 * A network client never steps the sim locally: authoritative snapshots
 * land in the live globals via netSnapApply(), so snapshot ARRIVAL
 * replaces the sim step as the capture boundary. The render frame then
 * tweens prev->cur exactly like gameMainLoop does between sim steps
 * (egsys.c), and restores the authoritative "cur" afterwards so the next
 * capture diffs clean snapshot data, never an interpolated leftover.
 *
 * Everything here reuses the egsnap.h machinery shared with egsys.c.
 */

#include "egdata.h"
#include "egsnap.h"
#include "struct.h"
#include "inttype.h"

#include <string.h>

/* prev/cur capture pair: cur is written at each snapshot apply, the
 * interpolated copy only lives in the live globals during the draw. */
static CamSnapshot netCamPrev, netCamCur;
static SimObjSnap netObjPrev[SIM_OBJ_MAX], netObjCur[SIM_OBJ_MAX];
static ProjSnap netProjPrev[PROJ_MAX], netProjCur[PROJ_MAX];

/* Call right after a snapshot has been applied to the live globals. */
void netRenderSnapCapture(void) {
    netCamPrev = netCamCur;
    memcpy(netObjPrev, netObjCur, sizeof(netObjPrev));
    memcpy(netProjPrev, netProjCur, sizeof(netProjPrev));
    camCapture(&netCamCur);
    objCapture(netObjCur, netProjCur);
}

/* ---- delayed-camera history --------------------------------------------
 * VIEW_EXT_DYNAMIC reads g_viewSnapshotRing[(frameTick-k)&RING_MASK]: the
 * server writes one entry per sim tick; here one lands per snapshot.
 * Without care the camera reads zeroed slots at join and stale slots
 * across packet gaps, and with a stationary aircraft the delayed pose
 * coincides with the plane (camera inside it). Push via netViewRingPush:
 * seed the whole ring on the first snapshot, then backfill skipped ticks
 * by lerping the previous pose toward the current one so every slot holds
 * a plausible sample. */
#define RING_LERP_SHIFT 12 /* backfill lerp fraction is Q12 fixed-point */
#define RING_LERP_ONE   (1 << RING_LERP_SHIFT)

static int s_ringHave;
static int16 s_ringLastTick;
static struct ViewSnapshot s_ringPrev;

void netViewRingPush(void) {
    struct ViewSnapshot cur;
    cur.heading = g_ourHead;
    cur.pitch = (int16)g_ourPitch;
    cur.roll = g_ourRoll;
    cur.worldX = g_ViewX;
    cur.worldY = g_ViewY;
    cur.alt = g_viewZ;
    if (!s_ringHave) {
        int k;
        for (k = 0; k < F15_VIEW_RING_SLOTS; k++)
            g_viewSnapshotRing[k] = cur;
        s_ringHave = 1;
    } else {
        int16 gap = (int16)(frameTick - s_ringLastTick); /* modular */
        if (gap > 1) {
            /* Iterate by elapsed-step count, not signed tick order: across
             * the int16 wrap (32766 -> -32766) gap is still 4 but any
             * 't < frameTick' loop sees -32766 < 32766 and fills nothing.
             * dt wraps into the missed ticks; the lerp fraction uses the
             * same modular distance. */
            int i, fill = gap - 1;
            if (fill > F15_VIEW_RING_SLOTS - 1)
                fill = F15_VIEW_RING_SLOTS - 1; /* only the last N-1 ticks stay addressable */
            for (i = 1; i <= fill; i++) {
                int16 dt = (int16)(frameTick - fill + i - 1);
                int a = (int)(((int32)(int16)(dt - s_ringLastTick) << RING_LERP_SHIFT) / gap);
                int s = dt & F15_VIEW_RING_MASK;
                struct ViewSnapshot *e = &g_viewSnapshotRing[s];
                e->worldX = s_ringPrev.worldX +
                    (int32)(((int64)(cur.worldX - s_ringPrev.worldX) * a) >> RING_LERP_SHIFT);
                e->worldY = s_ringPrev.worldY +
                    (int32)(((int64)(cur.worldY - s_ringPrev.worldY) * a) >> RING_LERP_SHIFT);
                e->alt = (int16)(s_ringPrev.alt +
                    (((int32)(cur.alt - s_ringPrev.alt) * a) >> RING_LERP_SHIFT));
                /* whole-pose lerp: a gimbal flip rewrites h/p/r together, so
                 * component-wise tweening would invent in-between poses */
                {
                    int32 fh, fp, fr;
                    lerpPose(s_ringPrev.heading, s_ringPrev.pitch, s_ringPrev.roll,
                             cur.heading, cur.pitch, cur.roll, a, RING_LERP_ONE,
                             &fh, &fp, &fr);
                    e->heading = (int16)fh;
                    e->pitch = (int16)fp;
                    e->roll = (int16)fr;
                }
            }
        }
    }
    g_viewSnapshotRing[frameTick & F15_VIEW_RING_MASK] = cur;
    s_ringPrev = cur;
    s_ringLastTick = frameTick;
}

/* Write the interpolated pose into the live globals for one renderFrame(). */
void netRenderApplyInterp(int64 num, int64 den) {
    if (den <= 0) den = 1;
    camApplyInterp(&netCamPrev, &netCamCur, num, den);
    objApplyInterp(netObjPrev, netObjCur, netProjPrev, netProjCur, num, den);
}

/* Put the authoritative latest snapshot back after rendering. */
void netRenderRestore(void) {
    camRestore(&netCamCur);
    objRestore(netObjCur, netProjCur);
}
