#include "f19seg.h"
#include "f19stvars.h"
#include "f19egvars.h"
#include <stdlib.h>
#include <string.h>

#define F19_MAX_SEGS 512

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

/* --- dseg object tables --------------------------------------------------*/

static int f19_world;    /* 0 = START objects, 1 = EGAME objects */

void f19_segUseWorld(int world) {
    f19_world = world;
}

static const struct F19SegObj *f19_worldObjs(int *count) {
    if (f19_world == 1) {
        *count = f19_egObjCount;
        return f19_egObjs;
    }
    *count = f19_stObjCount;
    return f19_stObjs;
}

void *f19_dsegAt(uint32 off) {
    const struct F19SegObj *t;
    int i, n;
    t = f19_worldObjs(&n);
    for (i = 0; i < n; i++)
        if (off >= t[i].off && off < t[i].off + t[i].size)
            return (char *)t[i].base + (off - t[i].off);
    return NULL;    /* unreachable: gaps tile [0,0x10000) */
}

uint16 f19_dsegOff(const void *p) {
    const struct F19SegObj *tabs[2];
    int counts[2], w, i;
    tabs[0] = f19_stObjs; counts[0] = f19_stObjCount;
    tabs[1] = f19_egObjs; counts[1] = f19_egObjCount;
    /* pointer membership is world-independent: scan both tables */
    for (w = 0; w < 2; w++)
        for (i = 0; i < counts[w]; i++) {
            const char *b = (const char *)tabs[w][i].base;
            if ((const char *)p >= b &&
                (const char *)p < b + tabs[w][i].size)
                return tabs[w][i].off +
                       (uint16)((const char *)p - b);
        }
    return 0xFFFF;
}

void *f19_segResolve(uint16 off, uint16 seg) {
    if (seg == 0)
        return f19_dsegAt(off);
    return (char *)f19_segPtr((int16)seg) + off;
}

void *f19_segPtr(int16 seg) {
    if (seg == 1) return f19_commBase;      /* handle 1: comm/game block  */
    if (seg >= 0x10 && seg < F19_MAX_SEGS) return f19_blocks[seg];
    return NULL;
}

uint32 f19_farOf(const void *p) {
    const char *c = (const char *)p;
    uint16 off;
    int i;
    off = f19_dsegOff(p);
    if (off != 0xFFFF)
        return off;                                  /* {off, 0} */
    if (c >= (const char *)f19_commBase && c < (const char *)f19_commBase + 0x8000)
        return (uint16)(c - (const char *)f19_commBase) | 0x10000;
    for (i = 0x10; i < F19_MAX_SEGS; i++)
        if (f19_blocks[i] && c >= (const char *)f19_blocks[i] &&
            c < (const char *)f19_blocks[i] + f19_blocksz[i])
            return (uint16)(c - (const char *)f19_blocks[i]) | ((uint32)i << 16);
    return (uint32)(uintptr_t)p & 0xffffffff;   /* foreign ptr: raw low bits */
}

void *f19_farAt(uint16 celloff) {
    uint32 c = *(const uint32 *)f19_dsegAt(celloff);
    return (char *)f19_segResolve((uint16)c, (uint16)(c >> 16)) ;
}

void f19_setFar(uint16 celloff, void *p) {
    *(uint32 *)f19_dsegAt(celloff) = f19_farOf(p);
}
