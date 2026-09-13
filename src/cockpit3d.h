#ifndef COCKPIT3D_H
#define COCKPIT3D_H

/* Called around the existing cockpit/instrument pass, with a current GL context. */
void cockpit3d_captureScene(int windowWidth, int windowHeight);
void cockpit3d_present(int windowWidth, int windowHeight, int shakePixels, int viewYawDegrees);
void cockpit3d_shutdown(void);

#endif
