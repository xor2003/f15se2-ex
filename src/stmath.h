#ifndef F15_SE2_STMATH
#define F15_SE2_STMATH

#include "inttype.h"

int16 approxDistance(int16 dx, int16 dy);
int16 calcBearing(int16 dx, int16 dy);
int16 clampValue(int16 val, int16 lo, int16 hi);
uint32 scaleCoordByLevel(int16 level, uint32 coord);

#endif /* F15_SE2_STMATH */
