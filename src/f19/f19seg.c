#include "f19seg.h"
#include "f19stvars.h"
#include "f19egvars.h"
#include "f19envars.h"
#include <stdlib.h>
#include <string.h>

#define F19_MAX_SEGS 512

extern uint8 f19_commBase[];

static void  *f19_blocks[F19_MAX_SEGS];
static size_t f19_blocksz[F19_MAX_SEGS];
static uint8 f19_blockalias[F19_MAX_SEGS]; /* alias slots never own the block */
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
        if (!f19_blockalias[seg]) free(f19_blocks[seg]);
        f19_blocks[seg] = NULL;
        f19_blockalias[seg] = 0;
        if (seg < f19_nextSeg) f19_nextSeg = seg;
        return 0;
    }
    return -1;
}

/* f19_segAlias — DOS "seg + N" arithmetic on a native handle: returns a new
 * handle slot resolving to base's block + byteoff.  The alias never owns the
 * block; f19_freeSeg on it only clears the slot. */
int16 f19_segAlias(int16 base, uint32 byteoff) {
    char *p = (char *)f19_segPtr(base);
    int i;
    if (!p) return 0;
    for (i = f19_nextSeg; i < F19_MAX_SEGS; i++) {
        if (f19_blocks[i] == NULL) {
            f19_blocks[i] = p + byteoff;
            f19_blocksz[i] = f19_blocksz[base] > byteoff
                             ? f19_blocksz[base] - byteoff : 0;
            f19_blockalias[i] = 1;
            f19_nextSeg = i + 1;
            return i;
        }
    }
    for (i = 0x10; i < F19_MAX_SEGS; i++) {
        if (f19_blocks[i] == NULL) {
            f19_blocks[i] = p + byteoff;
            f19_blocksz[i] = f19_blocksz[base] > byteoff
                             ? f19_blocksz[base] - byteoff : 0;
            f19_blockalias[i] = 1;
            return i;
        }
    }
    return 0;
}

/* --- dseg spaces ---------------------------------------------------------
   Each world is one packed struct covering DOS offsets [0,0x10000), so an
   offset resolves as space+off and a pointer as ptr-space. */

static int f19_world;    /* 0 = START objects, 1 = EGAME objects, 2 = END */

void f19_segUseWorld(int world) {
    f19_world = world;
}

void *f19_dsegAt(uint32 off) {
    if (off >= 0x10000) return NULL;
    if (f19_world == 2)
        return (char *)&f19_enSpace + off;
    return (char *)(f19_world == 1 ? (void *)&f19_egSpace
                                  : (void *)&f19_stSpace) + off;
}

uint16 f19_dsegOff(const void *p) {
    const char *c = (const char *)p;
    if (c >= (const char *)&f19_stSpace &&
        c < (const char *)&f19_stSpace + sizeof f19_stSpace)
        return (uint16)(c - (const char *)&f19_stSpace);
    if (c >= (const char *)&f19_egSpace &&
        c < (const char *)&f19_egSpace + sizeof f19_egSpace)
        return (uint16)(c - (const char *)&f19_egSpace);
    if (c >= (const char *)&f19_enSpace &&
        c < (const char *)&f19_enSpace + sizeof f19_enSpace)
        return (uint16)(c - (const char *)&f19_enSpace);
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
