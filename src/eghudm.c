/*
 * eghudm.c - HUD math/raster helpers for the egame C renderer.
 *
 * Companion to eghudr.c. eghudr is the large C analogue of egseg2.asm and lives
 * in its own code segment (EGHUD_TEXT) so it does not push the shared _TEXT past
 * 64K. MSC emits NEAR calls to the C runtime's 32-bit arithmetic helpers
 * (__aNlmul/__aNlshr/__aNldiv/__aNlrem/__aNulmul), which live in _TEXT and are
 * unreachable by a near call from a different segment. egseg2.asm sidesteps this
 * by doing its 32-bit math inline in assembly; the C port instead keeps every
 * routine that needs `long` arithmetic here in _TEXT (where those helpers are
 * near) and lets eghudr far-call them. Each is a thin far entry point.
 *
 * Routines: hudSine (roll sine), hudPitchScale (pitch->ladder offset),
 * drawClipLineGlobal (clipLineFar, also used by the tac map / flight overlay),
 * hudComplex (pitch-ladder column, MGRAPHIC slot 0x0b), hudRotateLadder (rotate
 * the built ladder vertices by roll).
 */
#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "inttype.h"
#include "struct.h"
#include "gfx.h"
#include "r2d.h"
#include <dos.h>
#include <math.h>

#define W16(p) rdI16(p)            /* unaligned-safe 16-bit read */
#define W16W(p, v) wrI16((p), (v)) /* unaligned-safe 16-bit write */

/* sin/cos via the shared 256-entry table + linear interpolation (16-bit angle:
 * high byte = table index, low byte = fraction). Matches the renderer's
 * lookupSineFar closely enough for the HUD. */
static int16 nsine(int16 angle) {
    uint16 a = (uint16)angle;
    int16 idx = (a >> 8) & 0xff;
    int16 frac = a & 0xff;
    int16 v0 = g_angleLut[idx];
    int16 v1 = g_angleLut[idx + 1];
    return v0 + (int16)((((long)(v1 - v0) * frac) + 0x80) >> 8);
}

int16 FAR CDECL hudSine(int16 angle) { return nsine(angle); }

/* Vertical gain that makes the GL pitch ladder a RIGID on-screen rotation. The ladder
 * transform squashes Y by 3/4 (= 3/131072 vs 1/32768 in X); the 2D present then
 * stretches Y by parY = (320/200)/(4:3) = 1.2. The DOS product is 0.75*1.2 = 0.9 — a
 * rotation-plus-squash, so a local right angle (rung vs its bend tick) shears off
 * perpendicular, worsening with roll. Using 1/parY here makes the net gain 1.0, so
 * the bends stay square at any roll. Software keeps the DOS 3/4 (hudRotateLadder). */
static const float LADDER_ASPECT =
    (float)LOGICAL_HEIGHT / (float)LOGICAL_WIDTH * TARGET_DAR; /* = 1/1.2 */

/* pitch -> ladder pixel offset: (|pitch|>>6) * 360, taking the high bytes. */
int FAR CDECL hudPitchScale(int ap) {
    /* The original works on the raw 16-bit pitch word; negative host ints must
     * wrap as unsigned DOS words before scaling. */
    return (int)(((unsigned long)(uint16)ap * 360u) >> 8);
}

/* ===== drawClipLineGlobal (clipLineFar) =====
 * Clip the global segment (g_lineX1,g_lineY1)-(g_lineX2,g_lineY2) to the window
 * [0,g_clipMaxX] x [0,g_clipMaxY] (Cohen-Sutherland), then emit the visible part
 * via gfx_drawLine (which folds in the current blitOffset window origin, clips
 * to the page and uses the stored fill colour). */
static int16 clipOutcode(int16 x, int16 y, int16 maxX, int16 maxY) {
    int16 c = 0;
    if (x < 0)
        c |= 1;
    else if (x > maxX)
        c |= 2;
    if (y < 0)
        c |= 4;
    else if (y > maxY)
        c |= 8;
    return c;
}

int FAR drawClipLineGlobal(void) {
    int16 x1 = g_lineX1, y1 = g_lineY1, x2 = g_lineX2, y2 = g_lineY2;
    int16 maxX = g_clipMaxX, maxY = g_clipMaxY;
    int16 c1 = clipOutcode(x1, y1, maxX, maxY);
    int16 c2 = clipOutcode(x2, y2, maxX, maxY);
    for (;;) {
        int16 oc, cx, cy;
        if ((c1 | c2) == 0) break;    /* fully inside */
        if ((c1 & c2) != 0) return 0; /* fully outside */
        oc = c1 ? c1 : c2;
        cx = 0;
        cy = 0;
        if (oc & 8) {
            cx = x1 + (int32)(x2 - x1) * (maxY - y1) / (y2 - y1);
            cy = maxY;
        } else if (oc & 4) {
            cx = x1 + (int32)(x2 - x1) * (0 - y1) / (y2 - y1);
            cy = 0;
        } else if (oc & 2) {
            cy = y1 + (int32)(y2 - y1) * (maxX - x1) / (x2 - x1);
            cx = maxX;
        } else {
            cy = y1 + (int32)(y2 - y1) * (0 - x1) / (x2 - x1);
            cx = 0;
        }
        if (oc == c1) {
            x1 = cx;
            y1 = cy;
            c1 = clipOutcode(x1, y1, maxX, maxY);
        } else {
            x2 = cx;
            y2 = cy;
            c2 = clipOutcode(x2, y2, maxX, maxY);
        }
    }
    gfx_drawLine((uint16)x1, (uint16)y1, (uint16)x2, (uint16)y2);
    return 0;
}

/* slot 0x0b (gfx_complexRender) — the pitch-ladder column. No cdecl trampoline
 * exists, so draw it directly into the current page. base/loY/hiY come from the
 * overlay's 12-word geometry table (baked here); thickness cycles 1..10. */
static const int16 g_ladderGeom[12] = {
    71, 248, 120, 200, 26, 26, 68, 68, 86, 86, 98, 98};
void FAR CDECL hudComplex(int16 bxArg, int16 dxArg, int16 cxArg, int16 siArg) {
    uint8 color = 0x0f;
    int16 dir = (siArg == 0) ? -1 : 1;
    int16 cl = cxArg & 0xff, dl = dxArg & 0xff;
    uint16 bx = (uint16)(bxArg - 1);
    uint16 base, loY, hiY;
    int16 wi;
    int32 t;
    if ((int8)dl >= 1) bx += 20;
    if (cl != 0) {
        siArg += 4;
        bx++;
    }
    wi = siArg / 2;
    if (wi < 0 || wi + 8 > 11) return;
    base = (uint16)g_ladderGeom[wi];
    loY = (uint16)g_ladderGeom[wi + 4];
    hiY = (uint16)g_ladderGeom[wi + 8];
    t = 1;
    if (bx > hiY) {
        int32 skip = ((int32)(uint16)(bx - hiY) + 1L) / 2L;
        bx = (uint16)(bx - (uint16)(skip * 2L));
        t += skip;
    }
    for (;; t++) {
        int16 phase = (int16)((t - 1L) % 10L);
        int16 thick = (phase == 0) ? 10 : phase;
        if (bx < loY) break;
        if (bx <= hiY && bx < 200) {
            /* All marks land on row bx at columns base, base+dir, ...; submitted
             * as 2D points so the GL backend replays the ladder crisply at native
             * resolution (software still plots them into the page). */
            int col = (int)base;
            if (thick == 5) {
                r2d_submitPoint(col, (int)bx, color);
                col += dir;
                r2d_submitPoint(col, (int)bx, color);
            } else if (thick == 10) {
                r2d_submitPoint(col, (int)bx, color);
                col += dir;
                r2d_submitPoint(col, (int)bx, color);
                col += dir;
                if (cl == 0) r2d_submitPoint(col, (int)bx, color);
            } else {
                r2d_submitPoint(col, (int)bx, color);
            }
        }
        bx -= 2;
    }
}

/* rotate each built ladder vertex pair (x,y) by roll, mirroring asm loc_0764:
 *   newX = hi(sinR*x*2) - hi(cosR*y*2)
 *   newY = (hi(3*sinR*y) >> 1) + (hi(3*cosR*x) >> 1)
 * sinR = sine(0x4000-roll), cosR = sine(-roll); the high-word extraction is the
 * asm's shl/rcl, the >>1 is `sar DX,1`. `di` is the highest vertex byte offset;
 * walk down to 0 in steps of 2. */
void FAR CDECL hudRotateLadder(int16 di) {
    int32 sinR = (int32)nsine((int16)(int16)(0x4000 - g_ourRoll.signedWord()));
    int32 cosR = (int32)nsine((int16)(int16)(-g_ourRoll.signedWord()));
    for (; di >= 0; di -= 2) {
        int32 x = (int32)W16(g_compassTapeBuf + 0xec + di);
        int32 y = (int32)W16(g_compassTapeBuf + 0x15c + di);
        int16 nx, ny;
        int32 v;
        nx = (int16)(((sinR * x) << 1) >> 16);
        nx -= (int16)(((cosR * y) << 1) >> 16);
        v = sinR * y;
        v = (v << 1) + v;
        ny = (int16)((int16)(v >> 16) >> 1);
        v = cosR * x;
        v = (v << 1) + v;
        ny += (int16)((int16)(v >> 16) >> 1);
        W16W(g_compassTapeBuf + 0xec + di, nx);
        W16W(g_compassTapeBuf + 0x15c + di, ny);
    }
}

/* Float companion to hudRotateLadder for the GL native-res HUD overlay. Rotates the
 * built ladder vertices by roll in float (no int16 truncation) and folds the
 * sub-pixel pitch-scroll offset dyFrac into each vertex's Y, so the steep climb/dive
 * rungs glide instead of snapping to the 320x200 grid — the same sub-pixel-vector
 * fix as the target box / scope lines. Reads the UN-rotated buffer, so call it
 * BEFORE hudRotateLadder overwrites g_compassTapeBuf. Vertex k → outX[k],outY[k].
 * The arithmetic mirrors hudRotateLadder ((sinR*x)*2>>16 etc.) without the (int16)
 * truncations, which never wrap for the HUD's small vertex range. */
void FAR hudRotateLadderF(int16 di, float dyFrac, float *outX, float *outY) {
    float sinR = (float)nsine((int16)(0x4000 - g_ourRoll.signedWord()));
    float cosR = (float)nsine((int16)(-g_ourRoll.signedWord()));
    int16 o;
    for (o = 0; o <= di; o += 2) {
        float x = (float)(int16)W16(g_compassTapeBuf + 0xec + o);
        float y = (float)(int16)W16(g_compassTapeBuf + 0x15c + o) + dyFrac;
        outX[o >> 1] = (sinR * x - cosR * y) / 32768.0f;
        /* rigid on screen: LADDER_ASPECT cancels the present's parY (vs the DOS 3/4) */
        outY[o >> 1] = LADDER_ASPECT * (sinR * y + cosR * x) / 32768.0f;
    }
}

/* 320-space text basis for the rotated pitch-ladder labels: ex = unit step per glyph
 * column, ey = unit step per row. ex follows the rung's on-screen direction from the
 * ladder transform (a horizontal rung's screen delta is proportional to (sinR,
 * 0.75*cosR) — the 0.75 is the transform's Y/X gain ratio 3/4), so labels tilt with
 * (and stay parallel to) the lines; ey is its rigid perpendicular, pointing down at
 * roll 0. Normalised so glyph texels keep unit (one-320-pixel) size at any roll. */
void FAR hudLabelBasis(float *exX, float *exY, float *eyX, float *eyY) {
    float sinR = (float)nsine((int16)(0x4000 - g_ourRoll.signedWord()));
    float cosR = (float)nsine((int16)(-g_ourRoll.signedWord()));
    float dx = sinR, dy = LADDER_ASPECT * cosR; /* match the rigid line transform */
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) len = 1.0f;
    *exX = dx / len;
    *exY = dy / len;
    *eyX = -*exY;
    *eyY = *exX;
}
