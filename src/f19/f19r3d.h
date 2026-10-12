#ifndef F19R3D_H
#define F19R3D_H

#include "inttype.h"

/* F-19 3D backend seam — routes the ported EGAME render calls through the app's
 * r3d API so the OpenGL backend can draw F-19's scenes. When the GL context is
 * not live the helpers run the original software path verbatim. */

int  f19_r3dGL(void);
void f19_beginScene(const int16 *vp, int16 ax, int16 ay, int16 az,
                    int16 px, int16 py, int16 pz, int16 renderScene);
void f19_submit(char *model, int yaw, int pitch, int roll,
                int posX, int posY, int posZ);
void f19_endScene(void);

#endif
