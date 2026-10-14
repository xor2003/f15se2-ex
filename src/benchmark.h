#ifndef F15_BENCHMARK_H
#define F15_BENCHMARK_H

#include "inttype.h"

struct SDL_Surface;

#ifdef __cplusplus
extern "C" {
#endif

void benchmarkInitialize(void);
int benchmarkEnabled(void);
int benchmarkRngSeed(void);
uint64 benchmarkElapsedNs(uint64 nowNs, uint64 prevNs, uint64 simStepNs);
int benchmarkFrameComplete(void);
int benchmarkSkipPresent(struct SDL_Surface *surface);
void benchmarkFinish(void);

#ifdef __cplusplus
}
#endif

#endif
