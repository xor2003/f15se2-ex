#pragma once

#include <SDL3/SDL.h>
#include "r3d_gles_compat.h"

static inline void glPlatformSetAttributes(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
}

static inline int glPlatformInit(void (**fogCoord)(GLfloat), int) {
    if (!r3dgles_compatInit()) return 0;
    *fogCoord = r3dgles_fogCoordf;
    return 1;
}

static inline const char *glPlatformName(void) { return "opengl1"; }
static inline void glPlatformAdjustView(int *, int *, int *) {}
static inline int glPlatformDrawSky(void) { return 1; }
static inline void glPlatformClearColor(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
}
