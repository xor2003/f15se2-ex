#ifndef R3D_GL_PLATFORM_H
#define R3D_GL_PLATFORM_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

/* Compile-time desktop hooks for the shared renderer. Keep the original
 * desktop context defaults rather than imposing the GLES profile on it. */
static inline void glPlatformSetAttributes(void) {}

/* Explicit fog coordinates are optional on desktop GL; MSAA is harmless when
 * the selected framebuffer has no sample buffers. */
static inline int glPlatformInit(void (**fogCoord)(GLfloat), int samples) {
    *fogCoord = (void (*)(GLfloat))SDL_GL_GetProcAddress("glFogCoordf");
    if (samples > 0) glEnable(GL_MULTISAMPLE);
    return 1;
}

/* The backend registry owns selection; this is only its stable display name. */
static inline const char *glPlatformName(void) { return "opengl1"; }

/* Desktop camera angles are supplied entirely by the game. */
static inline void glPlatformAdjustView(int *, int *, int *) {}

/* Desktop always uses the game's sky and opaque framebuffer. */
static inline int glPlatformDrawSky(void) { return 1; }

/* Shared call sites supply either black letterbox bars or the palette sky. */
static inline void glPlatformClearColor(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
}

#endif
