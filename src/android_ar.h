#ifndef F15_SE2_ANDROID_AR_H
#define F15_SE2_ANDROID_AR_H

#include "inttype.h"

/* Native half of the optional Android camera-sky prototype. These functions are
 * called only by the GLES backend; desktop GL and software rendering do not
 * depend on Android or JNI. */
int android_ar_requested(void);
int android_ar_active(void);
void android_ar_setGameAttitude(int pitchAngle, int rollAngle);
void android_ar_adjustView(int *yawAngle, int *pitchAngle, int *rollAngle);
void android_ar_getFlightAxes(uint8 *rollAxis, uint8 *pitchAxis);
void android_ar_recenterFlight(void);
void android_ar_setLookMode(int active);
void android_ar_addSwipePitch(float normalizedDelta);

#endif
