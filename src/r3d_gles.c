/*
 * Android OpenGL ES backend.
 *
 * Keep the renderer policy in one implementation: mesh decoding, integer camera
 * transforms, painter ordering, coplanar behavior, palette colors, and overlay
 * placement are included from r3d_gl.c. Only the unavailable OpenGL 1.x
 * fixed-function calls are translated by r3d_gles_compat.c.
 */
#define R3D_GLES_BUILD 1
#include "r3d_gl.c"
