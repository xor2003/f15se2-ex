#ifndef R3D_GLES_PLATFORM_H
#define R3D_GLES_PLATFORM_H

#include <SDL3/SDL.h>
#include "android_ar.h"
#include "r3d_gles_compat.h"
#include "log.h"

/* Request transparency only for the optional camera background. */
static inline void glPlatformSetAttributes(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, android_ar_requested() ? 8 : 0);
}

/* GLES implements fixed-function intent with shaders. Context ownership and
 * failure cleanup stay in the shared renderer, just as on desktop. */
static inline int glPlatformInit(void (**fogCoord)(GLfloat), int) {
    if (!r3dgles_compatInit()) {
        LogCritical(("GLES2 shader initialization failed"));
        return 0;
    }
    *fogCoord = r3dgles_fogCoordf;
    return 1;
}

/* Match the name accepted by the backend registry. */
static inline const char *glPlatformName(void) { return "opengles2"; }

/* Only the main view calls this hook; the target MFD remains game-controlled. */
static inline void glPlatformAdjustView(int *yaw, int *pitch, int *roll) {
    android_ar_adjustView(yaw, pitch, roll);
}

/* Camera failure restores the normal game sky without changing scene order. */
static inline int glPlatformDrawSky(void) { return !android_ar_active(); }

/* Transparent black exposes the camera beneath SDL, including letterboxing. */
static inline void glPlatformClearColor(float r, float g, float b) {
    if (android_ar_active()) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    else glClearColor(r, g, b, 1.0f);
}

#endif
