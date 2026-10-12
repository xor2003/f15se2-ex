/* f19r3d.c — F-19 scene/submit adapters onto the shared r3d backend API.
 *
 * The SW branch is deliberately the original call sequence (f19eg_setup3DTransform
 * / shared projectSceneObject / f19eg_rasterize3DWorld), so the software path is
 * unchanged. The GL branch submits the same objects through r3d; gl_beginScene
 * re-runs the shared setup3DTransform internally for the transform state, so the
 * only F-19-specific bits to reproduce are the per-frame spin advance and the
 * rasterizer epilogue flag (rasterize3DWorld's g_offscreenRender clear). */
#include "f19eg.h"
#include "f19egvars.h"
#include "f19r3d.h"
#include "r3d.h"
#include "r3d_gl.h"

void setup3DTransform(const int16 *, int16, int16, int16, int16, int16, int16, int16);
void rasterize3DWorld(void);
void far projectSceneObject(char far *model, int yaw, int pitch, int roll,
                            int posX, int posY, int posZ);

int f19_r3dGL(void) {
    if (r3dgl_active()) {
        /* F-19's model streams live in its own seg004 block (0x1000 paras = 64KB),
         * not the shared g_world3dData — the GL decoder's bounds limit must point
         * there or every stream fails validation and nothing draws. */
        r3dgl_setWorld3dLimit((const unsigned char *)f19_segPtr(f19eg_seg004) + 0x10000);
        return 1;
    }
    return 0;
}

void f19_beginScene(const int16 *vp, int16 ax, int16 ay, int16 az,
                    int16 px, int16 py, int16 pz, int16 renderScene) {
    if (f19_r3dGL()) {
        R3DScene s = {vp, ax, ay, az, px, py, pz, renderScene};
        r3d_beginScene(&s);
        /* f19eg_setup3DTransform advances the rotor-spin phase once per call;
         * the shared version scales by g_simStepsThisFrame (always 0 under F-19),
         * so the GL path must apply F-19's own advance here. */
        g_spinAngle -= 0x3000 / g_frameRateScaling;
        return;
    }
    setup3DTransform(vp, ax, ay, az, px, py, pz, renderScene);
}

void f19_submit(char *model, int yaw, int pitch, int roll,
                int posX, int posY, int posZ) {
    if (f19_r3dGL()) {
        R3DSubmit o = {model, -1, NULL, yaw, pitch, roll, posX, posY, posZ, 0};
        r3d_submit(&o);
        return;
    }
    projectSceneObject((char far *)model, yaw, pitch, roll, posX, posY, posZ);
}

void f19_endScene(void) {
    if (f19_r3dGL()) {
        r3d_endScene();
        g_offscreenRender = 0; /* rasterize3DWorld's epilogue flag */
        return;
    }
    rasterize3DWorld();
}
