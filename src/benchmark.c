#include "benchmark.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__) && defined(__has_include)
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define F15_HAVE_CALLGRIND 1
#endif
#endif
#ifndef F15_HAVE_CALLGRIND
#define CALLGRIND_START_INSTRUMENTATION ((void)0)
#define CALLGRIND_STOP_INSTRUMENTATION ((void)0)
#define CALLGRIND_TOGGLE_COLLECT ((void)0)
#define CALLGRIND_DUMP_STATS ((void)0)
#define CALLGRIND_ZERO_STATS ((void)0)
#endif

static int benchmarkFrameLimit;
static int benchmarkFrameCount;
static SDL_Surface *benchmarkLastSurface;

void benchmarkInitialize(void) {
    const char *value = getenv("F15_BENCHMARK_FRAMES");
    benchmarkFrameLimit = value ? atoi(value) : 0;
    benchmarkFrameCount = 0;
    benchmarkLastSurface = NULL;
    if (benchmarkFrameLimit <= 0) return;
    srand(1);
    CALLGRIND_ZERO_STATS;
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_TOGGLE_COLLECT;
}

int benchmarkEnabled(void) {
    return benchmarkFrameLimit > 0;
}

int benchmarkRngSeed(void) {
    return 1;
}

uint64 benchmarkElapsedNs(uint64 nowNs, uint64 prevNs, uint64 simStepNs) {
    return benchmarkEnabled() ? simStepNs : nowNs - prevNs;
}

int benchmarkFrameComplete(void) {
    if (!benchmarkEnabled()) return 0;
    benchmarkFrameCount++;
    return benchmarkFrameCount >= benchmarkFrameLimit;
}

int benchmarkSkipPresent(SDL_Surface *surface) {
    if (!benchmarkEnabled()) return 0;
    benchmarkLastSurface = surface;
    return 1;
}

void benchmarkFinish(void) {
    const char *dumpPath = getenv("F15_BENCHMARK_DUMP");
    uint32 hash = 2166136261u;
    int y;
    if (!benchmarkEnabled()) return;
    CALLGRIND_TOGGLE_COLLECT;
    CALLGRIND_STOP_INSTRUMENTATION;
    CALLGRIND_DUMP_STATS;
    if (dumpPath && benchmarkLastSurface) {
        SDL_SaveBMP(benchmarkLastSurface, dumpPath);
    }
    if (benchmarkLastSurface && benchmarkLastSurface->pixels) {
        const uint8 *pixels = (const uint8 *)benchmarkLastSurface->pixels;
        for (y = 0; y < benchmarkLastSurface->h; y++) {
            int x;
            for (x = 0; x < benchmarkLastSurface->w; x++) {
                hash ^= pixels[(size_t)y * benchmarkLastSurface->pitch + x];
                hash *= 16777619u;
            }
        }
    }
    fprintf(stderr, "benchmark: frames=%d framebuffer=%08x\n",
            benchmarkFrameCount, (unsigned)hash);
    fflush(stderr);
    exit(0);
}
