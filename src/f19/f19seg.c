#include "f19seg.h"
#include <stdlib.h>
#include <string.h>

#define F19_MAX_SEGS 512

extern uint8 f19_dseg[];
extern uint8 f19_commBase[];

static void  *f19_blocks[F19_MAX_SEGS];
static size_t f19_blocksz[F19_MAX_SEGS];
static int16 f19_nextSeg = 0x10;  /* DOS segs were >= 0x10; keep that floor */

int16 f19_allocSeg(uint16 paras) {
    int i;
    for (i = f19_nextSeg; i < F19_MAX_SEGS; i++) {
        if (f19_blocks[i] == NULL) {
            f19_blocks[i] = calloc(1, (size_t)paras << 4);
            f19_blocksz[i] = (size_t)paras << 4;
            f19_nextSeg = i + 1;
            return i;
        }
    }
    for (i = 0x10; i < F19_MAX_SEGS; i++) {
        if (f19_blocks[i] == NULL) {
            f19_blocks[i] = calloc(1, (size_t)paras << 4);
            f19_blocksz[i] = (size_t)paras << 4;
            return i;
        }
    }
    return 0;   /* out of handles */
}

int16 f19_freeSeg(int16 seg) {
    if (seg >= 0x10 && seg < F19_MAX_SEGS && f19_blocks[seg]) {
        free(f19_blocks[seg]);
        f19_blocks[seg] = NULL;
        if (seg < f19_nextSeg) f19_nextSeg = seg;
        return 0;
    }
    return -1;
}

void *f19_segPtr(int16 seg) {
    if (seg == 0) return f19_dseg;          /* handle 0: the data segment */
    if (seg == 1) return f19_commBase;      /* handle 1: comm/game block  */
    if (seg >= 0x10 && seg < F19_MAX_SEGS) return f19_blocks[seg];
    return NULL;
}

uint32 f19_farOf(const void *p) {
    const char *c = (const char *)p;
    int i;
    if (c >= (const char *)f19_dseg && c < (const char *)f19_dseg + 0x10000)
        return (uint16)(c - (const char *)f19_dseg);          /* {off, 0} */
    if (c >= (const char *)f19_commBase && c < (const char *)f19_commBase + 0x8000)
        return (uint16)(c - (const char *)f19_commBase) | 0x10000;
    for (i = 0x10; i < F19_MAX_SEGS; i++)
        if (f19_blocks[i] && c >= (const char *)f19_blocks[i] &&
            c < (const char *)f19_blocks[i] + f19_blocksz[i])
            return (uint16)(c - (const char *)f19_blocks[i]) | ((uint32)i << 16);
    return (uint32)(uintptr_t)p & 0xffffffff;   /* foreign ptr: raw low bits */
}

void *f19_farAt(uint16 celloff) {
    uint32 c = *(const uint32 *)(f19_dseg + celloff);
    return (char *)f19_segPtr((int16)(c >> 16)) + (uint16)c;
}

void f19_setFar(uint16 celloff, void *p) {
    *(uint32 *)(f19_dseg + celloff) = f19_farOf(p);
}
