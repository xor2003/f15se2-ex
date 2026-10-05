#include "f19eg.h"
#include "f19egvars.h"

/* seg000 routines — 3D view/terrain render (ported, verified vs original) */


void setup3DTransform(const int16 *transform, int16 a, int16 b, int16 c,
                      int16 d, int16 e, int16 f, int16 g);  /* sub_119EA */
int far gfx_calcRowAddr(int, int);               /* sub_2F160 */
void far gfx_setBlitOffset(int);                       /* sub_2F0AC */
void drawMapTiles(int16 mapX, int16 mapY, int16 zoomShift);/* sub_11564 */
void rasterize3DWorld(void);                               /* sub_11A64 */
void far renderSortedListFar(void);                        /* sub_20908 */
void far gfx_setBlitOffset2(void);                         /* sub_2F0A2 */
void far gfx_nop23(void);                                  /* sub_2F0D9 */
extern void timerYield(void);                               /* shared/timer.c */

/* ==== seg000:0x1516 ==== */
void renderMapTerrain(const int16 *transform, int16 mapX, int16 mapY, int16 zoomShift) {
    int16 tmp0, tmp1;
    g_objShade = 0;
    setup3DTransform(transform, 0, 0, 0, 0, 0, 0, 0);
    gfx_setBlitOffset(gfx_calcRowAddr(transform[9], transform[7]));
    drawMapTiles(mapX, mapY, zoomShift);
    rasterize3DWorld();
}

/* ==== seg000:0x1a64 ==== */
void rasterize3DWorld(void) {
    renderSortedListFar();
    gfx_setBlitOffset2();
    gfx_nop23();
    g_offscreenRender = 0;
}

/* ==== seg000:0x1a7a ==== */
extern void far gfx_setOvlVal2(int);          /* sub_2F16F */
extern int far gfx_calcRowAddr(int, int);  /* sub_2F160 */
extern void far gfx_setBlitOffset(int);       /* sub_2F0AC */

void setupViewport(const int16 *rect) {
    int16 wx, wy;
    wx = rect[10] - rect[9] + 1;
    wy = rect[8] - rect[7] + 1;
    g_viewCenterX = ((wx + 1) >> 1) - 1;
    g_viewCenterY = ((wy + 1) >> 1) - 1;
    if (rect[7] == 0) {
        g_viewCenterY = (char)g_hudVisible != 0 ? (g_halfScaleRender != 0 ? 82 : 56) : 100;
    }
    gfx_setOvlVal2(wx - 1);
    gfx_setBlitOffset(gfx_calcRowAddr(rect[9], rect[7]));
    g_clipMaxX = wx - 1;
    g_clipMaxY = wy - 1;
    g_overlayCenterX = (int16 *)(((uint8 *)f19_dsegAt(0xA28)));
    g_overlayCenterY = (int16 *)(((uint8 *)f19_dsegAt(0xA48)));
    if (g_halfScaleRender != 0) {
        g_overlayCenterX += 8;
        g_overlayCenterY += 8;
    }
    if ((char)g_hudVisible != 0)
        g_overlayCenterY += 16;
}

/* ==== seg000:0x1b32 ==== */
extern int far buildRotationMatrixFar(int16 *, int, int, int); /* sub_211C2 */

void setViewRotation(int16 rotX, int16 rotY, int16 rotZ) {
    buildRotationMatrixFar(g_viewRotMatrix, -rotX, -rotY, -rotZ);
}

/* ==== seg000:0x19ea ==== */
extern void setViewPosition(int16, int16, int16);      /* sub_11B56 */
extern void far transformModelVerticesFar(void);       /* sub_20C6C */
extern void far drawProjectionSphere(int16);           /* sub_204FE */

void setup3DTransform(const int16 *model, int16 angleX, int16 angleY, int16 angleZ, int16 posX, int16 posY, int16 posZ, int16 renderScene) {
    setupViewport(model);
    setViewRotation(angleX, angleY, angleZ);
    setViewPosition(posX, posY, posZ);
    if (renderScene != 0) {
        g_posVisibleFlag = 0;
        if (g_detailLevel == 0)
            g_offscreenRender = 1;
        if (g_offscreenRender == 0)
            transformModelVerticesFar();
        while (g_frameSyncPending != 0) timerYield();
        drawProjectionSphere(model[2]);
    }
    g_sortedObjCount = 0;
    g_spinAngle -= 0x3000 / g_frameRateScaling;
}

/* ==== seg000:0x1b56 ==== */

void setViewPosition(int16 x, int16 y, int16 z) {
    g_viewPosX = x;
    g_viewPosY = y;
    g_viewPosZ = z;
}
