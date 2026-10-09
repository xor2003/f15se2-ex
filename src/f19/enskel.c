/* enskel.c — native ports of END.EXE routines that stay skeleton in the
 * f19ru reconstruction (hand-asm, CRT, int21/port-io and driver-internal
 * blits), plus the pointer-cell materialization and f19_end_main entry.
 *
 * Every routine cites the seg000 offset of the asm original.  Page/buffer
 * "segments" are f19seg handles; the display segs resolve to the app's
 * visible back buffer. */
#include "f19en.h"
#include "f19seg.h"
#include "gfx.h"
#include "slot.h"
#include "gfx_impl.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern jmp_buf f19_chainExit;
extern int getTimeOfDay(void);

/* ---- pixel/segment resolution -------------------------------------------
 * END keeps page/image buffers in f19seg handles; the display segs resolve
 * to the visible back buffer (320-byte pitch). */
static uint8 *f19en_pagePx(int16 seg) {
    if (seg <= 0 || seg == (int16)0xA000 || seg == (int16)0xA800 ||
        seg == (int16)0xB800)
        return gfx_pagePixels(0, (int *)0);
    return (uint8 *)f19_segPtr(seg);
}

/* ---- near-pointer cells materialized from the dseg image -----------------
 * The image stores DOS near offsets; END code uses them as real pointers. */
int16 *word_1BAD0, *word_1BACE, *word_1BAD2, *word_1BAD4, *word_1BAD6,
    *word_1BAD8;
uint8 *word_1C6E8, *word_1C6EA;
struct BlinkSprite *spriteAir, *spriteAirBlink, *spriteGround,
    *spriteGroundBlink, *spriteSam, *spriteSamBlink, *spriteMapArea,
    *spriteWaypoint, *spriteWaypointBlink;
int16 *word_19806, *word_2379E, *word_1F664, *word_1F856, *awardTextItem,
    *word_1ED12, *word_1981E, *word_19704, *word_207B2, *word_1F426,
    *purpleHeartSpr, *medalSpriteTab, *rankSpriteA, *rankSpriteB,
    *rankSpriteC, *medalNames, *queuedAwardName, *ribbonNames,
    *newRankNames, *nextRankNames;
uint16 *colorTablePtr;
uint8 *worldBufPtr;
char *worldStrings[100];

static void *f19en_cellPtr(uint8 *cell) {
    return f19_dsegAt(*(uint16 *)cell);
}

void f19_enInitPtrs(void) {
    word_1BAD0 = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BAD0);
    word_1BACE = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BACE);
    word_1BAD2 = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BAD2);
    word_1BAD4 = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BAD4);
    word_1BAD6 = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BAD4 + 2);
    word_1BAD8 = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1BAD4 + 4);
    /* staging cells are BSS — the DOS init stored near-offsets to two 0x400
     * staging buffers that alias the scanline-LUT arena (0x2150/0x2550). */
    *(uint16 *)f19_enSpace.m_word_1C6E8 = 0x2150;
    *(uint16 *)f19_enSpace.m_word_1C6EA = 0x2550;
    word_1C6E8 = (uint8 *)f19_dsegAt(0x2150);
    word_1C6EA = (uint8 *)f19_dsegAt(0x2550);
    spriteMapArea      = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteMapArea);
    spriteAir          = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteAir);
    spriteAirBlink     = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteAirBlink);
    spriteGround       = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteGround);
    spriteGroundBlink  = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteGroundBlink);
    spriteSam          = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteSam);
    spriteSamBlink     = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteSamBlink);
    spriteWaypoint     = (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteWaypoint);
    spriteWaypointBlink= (struct BlinkSprite *)f19en_cellPtr(f19_enSpace.m_spriteWaypointBlink);
    word_19806    = (int16 *)f19en_cellPtr(f19_enSpace.m_word_19806);
    word_2379E    = (int16 *)f19en_cellPtr(f19_enSpace.m_word_2379E);
    word_1F664    = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1F664);
    word_1F856    = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1F856);
    awardTextItem = (int16 *)f19en_cellPtr(f19_enSpace.m_awardTextItem);
    colorTablePtr = (uint16 *)f19en_cellPtr(f19_enSpace.m_colorTablePtr);
    word_1ED12  = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1ED12);
    word_1981E  = (int16 *)f19en_cellPtr(f19_enSpace.m_word_1981E);
    word_19704  = (int16 *)f19en_cellPtr(f19_enSpace.m_word_19704);
    /* pointer-table cells (int16 elements holding near offsets) */
    word_207B2   = (int16 *)f19_dsegAt(f19en_cellW(0x6A22));
    word_1F426   = (int16 *)f19_dsegAt(f19en_cellW(0x5696));
    purpleHeartSpr = (int16 *)f19_dsegAt(f19en_cellW(0x66AE));
    medalSpriteTab = (int16 *)f19_dsegAt(f19en_cellW(0x66A2));
    rankSpriteA  = (int16 *)f19_dsegAt(f19en_cellW(0x667A));
    rankSpriteB  = (int16 *)f19_dsegAt(f19en_cellW(0x6688));
    rankSpriteC  = (int16 *)f19_dsegAt(f19en_cellW(0x6696));
    medalNames   = (int16 *)f19_dsegAt(f19en_cellW(0x66E8));
    queuedAwardName = (int16 *)f19_dsegAt(f19en_cellW(0x66F4));
    ribbonNames  = (int16 *)f19_dsegAt(f19en_cellW(0x66FE));
    newRankNames = (int16 *)f19_dsegAt(f19en_cellW(0x6706));
    nextRankNames= (int16 *)f19_dsegAt(f19en_cellW(0x6714));
}

/* ---- CRT/asm scalar helpers --------------------------------------------- */
#undef rand
#undef srand
/* MSC CRT LCG — state*0x343FD+0x269EC3, result is bits 30..16 (15-bit). */
static uint32 f19en_randState;
int16 f19en_rand(void) {
    f19en_randState = f19en_randState * 0x343FDUL + 0x269EC3UL;
    return (int16)(f19en_randState >> 16) & 0x7FFF;
}
void  f19en_seedRandom16(int16 v) { f19en_randState = (uint32)(uint16)v; }
#define rand  f19en_rand
#define srand f19en_srand
int16 f19en_readBiosTickLo(void) { return (int16)getTimeOfDay(); }
void  f19en_exit(int16 code) { longjmp(f19_chainExit, code ? code : 1); }
void  f19en_srand(int16 v) { f19en_seedRandom16(v); }

/* ---- mem/str asm helpers --------------------------------------------------
 * src args the original passed as far pointers are real pointers here. */
void  f19en_memcpyFromFar(char *dst, char *src, int16 n) { memcpy(dst, src, n); }
void  f19en_farStrcpy(char *dst, char *src) { strcpy(dst, src); }
void  f19en_strcpyToFar(char *dst, const char *src) { strcpy(dst, src); }
int16 f19en_mystrlen(const char *s) { return (int16)strlen(s); }
char *f19en_mystrchr(char *s, int16 c) { return strchr(s, c); }
int16 f19en_memeq(const void *a, const void *b, int16 n) { return memcmp(a, b, n) == 0; }
void  f19en_copyBytes(char *dst, char *src, int16 n) { memcpy(dst, src, n); }
void  f19en_memsetNear(uint8 *dst, int16 val, uint16 count) { memset(dst, val, count); }
void  f19en_memsetFar(uint8 *dst, int16 val, uint16 count) { memset(dst, val, count); }
void  f19en_movedata(int16 sseg, const void *soff, int16 dseg, void *doff, uint16 len) {
    (void)sseg; (void)dseg;
    memmove(doff, soff, len);
}

/* ---- int21 file helpers -------------------------------------------------- */
int16 f19en_lseek(int16 fd, int32 off, int16 whence) {
    extern int16 sub_1E172(int16 h, int16 lo, int16 hi, int16 mode);
    return sub_1E172(fd, (int16)(off & 0xFFFF), (int16)(off >> 16), whence);
}
void f19en_seekFileAt(int16 fd, int16 off, int16 whence) {
    extern int16 sub_1E172(int16 h, int16 lo, int16 hi, int16 mode);
    (void)sub_1E172(fd, off, 0, whence);
}
uint16 f19en_dos_alloc(uint16 size) {
    return (uint16)f19_allocSeg((uint16)((size + 0xF) >> 4));
}
int16 f19en_dos_free(uint16 seg) {
    f19_freeSeg((int16)seg);
    return 0;
}
void f19en_dos_printstring(const char *s) { fprintf(stderr, "%s\n", s); }

/* ---- misc/joystick driver slots ------------------------------------------ */
int16 f19en_misc_jump_5a_keybuf(void) { return misc_checkKeyBuf(); }
int16 f19en_misc_jump_5b_getkey(void) { return misc_getKey(); }
int16 f19en_misc_jump_5d_readJoy(int16 a) { return misc_readJoystick(a); }
void  f19en_misc_jump_5e_clearKeyFlags(void) { misc_clearKeyFlags(); }
void far f19en_joyTableSetup(char *p) {
    /* the driver calibration leaves the axis cells centred; with no joystick
     * attached they must sit inside the menu deadzone (0x4e..0xb2) */
    (void)p;
    joyAxisX = joyAxisY = 0x80;
}
void far f19en_pollJoystick(void) {
    extern uint8 g_joyRawX, g_joyRawY;
    joyAxisX = g_joyRawX;
    joyAxisY = g_joyRawY;
}
int16 f19en_readJoyAxis(int16 a) { return misc_readJoystick(a); }
void  f19en_intDispatch(int16 intNum, uint8 *inRegs, uint8 *outRegs) {
    /* only used for int10 mode-set in cleanup() — native build is already in
     * the target video mode */
    (void)intNum; (void)inRegs; (void)outRegs;
}

/* ---- driver-internal ops ------------------------------------------------- */
/* seg000:0x0d1a — patches the driver's trampoline table to point at its
 * resident handlers.  In the merged build all driver calls are already
 * bound, so this is a no-op. */
void f19en_sub_10D1A(int16 seg) { (void)seg; }

/* seg000:0x0e50 — fill a rectangle on the descriptor's page via the driver's
 * span-fill op.  page[0] = page seg, page[6] = colour byte. */
void f19en_sub_10E50(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2) {
    uint8 *px, c;
    int16 y, w;
    px = f19en_pagePx(page[0]);
    if (!px) return;
    c = ((uint8 *)page)[6];
    w = x2 - x1 + 1;
    if (w <= 0) return;
    for (y = y1; y <= y2; y++)
        memset(px + y * 320 + x1, c, (size_t)w);
}

/* seg000:0x0db2 — same shape as sub_10E50 (duplicate span fill) */
void f19en_clearRect(int16 *item, int16 x1, int16 y1, int16 x2, int16 y2) {
    f19en_sub_10E50(item, x1, y1, x2, y2);
}

/* seg000:0x1a1c — fd-stream cursor append: read count bytes at the
 * word_1C6EA write cursor, advance it, return the start offset. */
extern int16 readPicStream(uint8 *dst, int16 count, int16 fd);
int16 f19en_sub_11A1C(int16 count, int16 fd) {
    int16 off = f19_dsegOff(word_1C6EA);
    readPicStream(word_1C6EA, count, fd);
    word_1C6EA += count;
    return off;
}

/* ---- pic stream refill ---------------------------------------------------- */
/* seg000:0x1500 — read a 0x200-byte block from the pic-stream fd
 * (word_1B944, an int16 cell inside picStreamBuf). */
#define word_1B944 (*(int16 *)(picStreamBuf + (0x1A14 - 0x194A)))
int16 f19en_picReadBlock(void) {
    extern int16 resFileRead(int16 handle, int16 count, int16 bufOff);
    return resFileRead(word_1B944, 0x200, 0x194A);
}

/* seg000:0x1521 — set the pic-stream fd then refill the block */
void f19en_picStreamRead(int16 fd) {
    word_1B944 = fd;
    f19en_picReadBlock();
}

/* seg000:0x3238 — copy the next 0x200 bytes of the staged resource seg
 * into picStreamBuf.  DOS kept the staged cursor at ss:word_2DB50. */
/* DOS word_2DB50 is a plain word: +0x200 wraps mod 0x10000 and si reads stay
 * inside the seg window (0..0xFFFF) — the tail slack covers reads past the
 * staged resource's own extent.  int16 here would wrap to -32768 and read
 * wild below the block. */
static uint16 f19en_stagePos;
void f19en_picStageRefill(void) {
    const uint8 *src = (const uint8 *)f19_segPtr(word_23C74);
    if (src) memcpy(picStreamBuf, src + f19en_stagePos, 0x200);
    f19en_stagePos += 0x200;
}

/* ---- Huffman bit readers ---------------------------------------------------
 * seg000:0x30be (file) / 0x31e7 (staged) — decode one symbol from the
 * bitstream into picStreamBuf using the word_1BAE0[] tree. */
static int16 f19en_bitRead(int staged) {
    int8 ah;
    uint8 al;
    int16 si, dx;
    ah = (int8)byte_1C6E5;
    al = byte_1C6E6;
    si = word_1BADC;
    dx = 0;
    for (;;) {
        ah--;
        if (ah < 0) {
            al = picStreamBuf[si++];
            if (si > 0x200) {
                if (staged) f19en_picStageRefill(); else f19en_picReadBlock();
                si = 1;
                al = picStreamBuf[0];
            }
            ah = 7;
        }
        dx = (dx << 1) | (al >> 7);
        al <<= 1;
        dx = word_1BAE0[dx];
        if (dx < 0) break;
    }
    byte_1C6E6 = al;
    byte_1C6E5 = (uint8)ah;
    word_1BADC = si;
    return (int16)(0xFFFF - dx);
}

int16 f19en_sub_130BE(void) { return f19en_bitRead(0); }
int16 f19en_sub_131E7(void) { return f19en_bitRead(1); }

/* ---- RLE nibble blits -------------------------------------------------------
 * seg000:0x2fe8 (file bitstream) / 0x3111 gety (staged bitstream) — decode
 * nibble-packed RLE pixels and XOR them into the planar-organised dst seg
 * (word_2351E[] maps row -> (row/4)*0xa0 + (row%4)*0x2000).  The two asm
 * twins differ only in the frame cells and the bit source. */
static void f19en_rleBlit(int16 x, int16 y, int16 w, int16 h,
                          int16 *x2c, int16 *y2c, int16 *rowc, uint8 *accc,
                          int staged) {
    uint8 *dst = f19en_pagePx(word_1BADA);
    int16 cx, di;
    uint8 al;
    *x2c = x + w;
    *y2c = y + h;
    *rowc = y;
    for (; *rowc < *y2c; (*rowc)++) {
        di = (x >> 1) + word_2351E[*rowc];
        *accc = 0;
        for (cx = x; cx < *x2c; cx++) {
            byte_1C6E0 ^= 1;
            if (byte_1C6E0 == 0) {
                al = byte_1C6E1 & 0xF;
            } else {
                uint8 a;
                if (word_1BADE == 0) {
                    a = (uint8)f19en_bitRead(staged);
                    if (a == byte_1C6E4) {
                        int16 r;
                        byte_1C6E3 = (uint8)f19en_bitRead(staged);
                        word_1BADE = 4;
                        do {
                            r = f19en_bitRead(staged);
                            word_1BADE += r;
                        } while ((uint8)r == 0xFF);
                    } else {
                        byte_1C6E1 = a;
                        al = byte_1C6E1 >> 4;
                        goto emit;
                    }
                }
                word_1BADE--;
                byte_1C6E1 = byte_1C6E3;
            emit:
                al = byte_1C6E1 >> 4;
            }
            al = (al - byte_1C6E2) & 0xF;
            if (cx & 1) {
                al |= *accc;
                dst[di] ^= al;
                di++;
                *accc = 0;
            } else {
                *accc |= al << 4;
            }
        }
        if (cx & 1)
            dst[di] ^= al;
    }
}

void f19en_sub_12FE8(int16 a, int16 b, int16 c, int16 d) {
    f19en_rleBlit(a, b, c, d, &word_1DF3E, &word_1DF40, &word_1DF42,
                  &byte_1DF44, 0);
}

void f19en_gety(int16 a, int16 b, int16 c, int16 d) {
    f19en_rleBlit(a, b, c, d, &word_1DF46, &word_1DF48, &word_1DF4A,
                  &byte_1DF4C, 1);
}

/* ---- planar/blit copy ops ---------------------------------------------------
 * These were the driver's mode-specialised rectangle copies; END only ever
 * runs the mode-13h (chunky) path in this build.  Args are
 * (w, h, sseg, soff, dseg, doff) in all cases.  Row pitch inside the
 * planar staging buffers is 0xa0; the chunky page pitch is 320. */
static void f19en_copySeg(int16 w, int16 h, int16 sseg, int16 soff,
                          int16 dseg, int16 doff, int16 spitch, int16 dpitch) {
    const uint8 *s = f19en_pagePx(sseg) + soff;
    uint8 *d = f19en_pagePx(dseg) + doff;
    int16 y;
    for (y = 0; y < h; y++)
        memcpy(d + y * dpitch, s + y * spitch, (size_t)w);
}

/* seg000:0x3335 — EGA write-mode-2 planar block write (in/out ports).
 * Dead path in this build; approximated by a planar-buffer copy. */
void f19en_sub_13335(int16 w, int16 h, int16 sseg, int16 soff,
                     int16 dseg, int16 doff) {
    const uint8 *s = f19en_pagePx(sseg) + soff;
    uint8 *d = f19en_pagePx(dseg) + doff;
    int16 rows = h * 2;
    int16 bytes = w * 2;
    int16 r;
    for (r = 0; r < rows; r++) {
        memcpy(d, s, (size_t)bytes);
        s += 0x2000 * 4;
        d += 0x280;
    }
}

/* seg000:0x3326 — rep-movs forward copy (clc path of 0x33ba) */
void f19en_sub_133BA(int16 w, int16 h, int16 sseg, int16 soff,
                     int16 dseg, int16 doff) {
    f19en_copySeg(w, h, sseg, soff, dseg, doff, 0xa0, 0xa0);
}

/* seg000:0x3335/0x3260 reverse-direction variant of the same copy */
void f19en_sub_133BD(int16 w, int16 h, int16 sseg, int16 soff,
                     int16 dseg, int16 doff) {
    f19en_copySeg(w, h, sseg, soff, dseg, doff, 0xa0, 0xa0);
}

/* seg000:0x340e — plane-interleave a 4bpp planar buffer into row-contiguous
 * 4bpp: per iteration copy 2*w words four times from +0x2000-strided source
 * planes into consecutive 0xa0-wide dst rows. */
void f19en_sub_133EC(int16 w, int16 h, int16 sseg, int16 soff,
                     int16 dseg, int16 doff) {
    const uint8 *s = f19en_pagePx(sseg) + soff;
    uint8 *d = f19en_pagePx(dseg) + doff;
    int16 rows = h * 2;
    int16 bytes = w * 4;
    int16 r, p;
    for (r = 0; r < rows; r++) {
        const uint8 *sp = s;
        for (p = 0; p < 4; p++) {
            memcpy(d + p * 0xa0, sp, (size_t)bytes);
            sp += 0x2000;
        }
        s += 0xa0;
        d += 0x280;
    }
}

/* seg000:0x345d — expand nibble-packed rows (4bpp) into chunky rows:
 * each source byte becomes two pixels (hi then lo nibble). */
void f19en_sub_13436(int16 w, int16 h, int16 sseg, int16 soff,
                     int16 dseg, int16 doff) {
    const uint8 *s = f19en_pagePx(sseg) + soff;
    uint8 *d = f19en_pagePx(dseg) + doff;
    int16 bytes = w * 4;
    int16 rows = h * 8;
    int16 r, i;
    for (r = 0; r < rows; r++) {
        uint8 *dp = d;
        for (i = 0; i < bytes; i++) {
            uint8 v = s[i];
            dp[0] = v >> 4;
            dp[1] = v & 0xF;
            dp += 2;
        }
        s += 0xa0;
        d += 0x140;
    }
}

/* ---- driver text/overlay blit ops ------------------------------------------
 * 91d:0x165/0x477/0x762/0xa61 — mode-specialised rectangle blits between
 * page segs: (sseg, sx, sy, dseg, dx, dy, w, h).  All are plain copies in
 * the chunky build. */
static void f19en_textOp(int16 sseg, int16 sx, int16 sy, int16 dseg,
                         int16 dx, int16 dy, int16 w, int16 h) {
    const uint8 *sbase = f19en_pagePx(sseg);
    uint8 *dbase = f19en_pagePx(dseg);
    int16 y;
    if (w <= 0 || h <= 0) return;
    if (getenv("F19_DBG"))
        fprintf(stderr, "[textOp] sseg=%d sx=%d sy=%d dseg=%d dx=%d dy=%d w=%d h=%d\n",
                sseg, sx, sy, dseg, dx, dy, w, h);
    for (y = 0; y < h; y++)
        /* di/si are 16-bit in the original: offsets wrap mod 0x10000
         * within the seg's 64KB window (slack in f19_allocSeg covers the
         * tail for alias bases). */
        memcpy(dbase + (((dy + y) * 320 + dx) & 0xFFFF),
               sbase + (((sy + y) * 320 + sx) & 0xFFFF), (size_t)w);
}

void far f19en_textOp_165(int16 p1, int16 a2, int16 a3, int16 p4,
                          int16 a5, int16 a6, int16 a7, int16 a8) {
    f19en_textOp(p1, a2, a3, p4, a5, a6, a7, a8);
}
void far f19en_textOp_477(int16 p1, int16 a2, int16 a3, int16 p4,
                          int16 a5, int16 a6, int16 a7, int16 a8) {
    f19en_textOp(p1, a2, a3, p4, a5, a6, a7, a8);
}
void far f19en_textOp_762(int16 p1, int16 a2, int16 a3, int16 p4,
                          int16 a5, int16 a6, int16 a7, int16 a8) {
    f19en_textOp(p1, a2, a3, p4, a5, a6, a7, a8);
}
void far f19en_textOp_A61(int16 p1, int16 a2, int16 a3, int16 p4,
                          int16 a5, int16 a6, int16 a7, int16 a8) {
    f19en_textOp(p1, a2, a3, p4, a5, a6, a7, a8);
}

/* ---- pic decode entry points ------------------------------------------------
 * 0x1706 picBlit / 0x17e2 decodePic — decode the open pic stream onto the
 * page buffer.  seg here is a page/buffer seg value. */
extern void f19_decodeTo(int16 fd, void *dst);
void f19en_picBlit(int16 fd, int16 page) {
    f19_decodeTo(fd, f19en_pagePx(page));
}
void f19en_decodePic(int16 fd, int16 page) {
    f19_decodeTo(fd, f19en_pagePx(page));
}

/* ---- line-draw wrapper -------------------------------------------------------
 * seg000:0x???? drawLineWrapper — shim over the resident drawLine. */
extern void drawLine(int16 x1, int16 y1, int16 x2, int16 y2, int16 c);
void f19en_drawLineWrapper(void) { /* state-driven; unused on the 13h path */ }

/* ---- runMapView (0x2192) -----------------------------------------------------
 * Staged-stream twin of loadMapView: the map-view resource has already been
 * loaded into an f19seg block (sel); decode it the same way then display.
 * Identical layout to enbrief2.c's loadMapView apart from the stream source
 * and the display tail. */
int16 f19en_runMapView(int16 sel) {
    AnimRec *pr;
    ChanRec *ch;
    int16 cnt, i, j;
    uint8 buf[16];

    word_1BADE = 0;
    byte_1C6E0 = byte_1C6E1 = byte_1C6E2 = byte_1C6E3 = 0;
    byte_1C6E4 = byte_1C6E5 = byte_1C6E6 = 0;
    for (i = 0; i < 0xC8; i++)
        word_2351E[i] = (i / 4) * 0xA0 + ((i & 3) << 13);

    word_23C60 = 0;
    f19en_stagePos = 0;
    word_23C74 = sel;
    f19en_picStageRefill();
    word_1BADC = 0;
    word_2244C = allocClearBuf((int32)0xFFFF);
    word_23C7C = f19_segAlias(word_2244C, 0x8000);   /* orig seg+0x800 paras */
    if (initResultFlag == 2)
        gfx_drvMode();
    word_1BA9E = word_2244C;
    word_1BA92 = word_23C7C;
    word_1BADA = *word_1BAD0;
    word_1C6E8 = (uint8 *)f19_dsegAt(0x2150);
    word_1C6EA = (uint8 *)f19_dsegAt(0x2550);
    readStageStream(word_1C6E8, 0x20);
    for (i = 0; i < 0x10; i++) {
        j = word_1C6E8[i*2]   & 0xF0; byte_2089D[i] = (j >> 4) | j;
        j = word_1C6E8[i*2]   & 0xF;  byte_208AD[i] = (j << 4) | j;
        j = word_1C6E8[i*2+1] & 0xF0; byte_208BD[i] = (j >> 4) | j;
        j = word_1C6E8[i*2+1] & 0xF;  byte_208CD[i] = (j << 4) | j;
    }
    readStageStream(word_1C6E8, 6);
    word_1DA44 = word_1C6E8[0];
    byte_1C6E2 = word_1C6E8[1];
    word_1DA46 = (word_1C6E8[2] << 8) + word_1C6E8[3];
    word_1DA48 = (word_1C6E8[4] << 8) + word_1C6E8[5];
    readStageStream(word_1C6E8, 7);
    for (i = 0; i < word_1DA44; i++) {
        pr = (AnimRec *)f19_dsegAt(0x295E + i * sizeof(AnimRec));
        ch = (ChanRec *)f19_dsegAt(0x3426 + i * sizeof(ChanRec));
        readStageStream(word_1C6E8, 0xB);
        pr->posX   = (word_1C6E8[0] << 8) + word_1C6E8[1];
        pr->posY   = (word_1C6E8[2] << 8) + word_1C6E8[3];
        pr->maxA   = (word_1C6E8[4] << 8) + word_1C6E8[5];
        pr->maxB   = (word_1C6E8[6] << 8) + word_1C6E8[7];
        pr->field8 = word_1C6E8[8];
        pr->field9 = word_1C6E8[9];
        pr->cntA   = word_1C6E8[10];
        ch->f0 = pr->field9;
        pr->strOff = stageAppend(pr->cntA);
        pr->field9 = 0;
        pr->cntA = 0;
        pr->cntB = 0;
        pr->chanIdx = 0;
        pr->repeat[0] = 1;
        pr->strPos[0] = 0;
        pr->active = 1;
        for (j = 0; j < ch->f0; j++) {
            readStageStream(word_1C6E8, 7);
            pr->frameW[j] = *(int16 *)word_1C6E8;
            pr->frameB[j] = word_1C6E8[2];
            ch->f1[j] = word_1C6E8[3];
            ch->f2[j] = word_1C6E8[4];
            ch->f3[j] = word_1C6E8[5];
            ch->f4[j] = word_1C6E8[6];
        }
    }
    readStageStream(word_1C6E8, 3);
    cnt = (word_1C6E8[0] << 8) + word_1C6E8[1];
    byte_1C6E4 = word_1C6E8[2];
    readStageStream(word_1C6E8, cnt * 2);
    for (i = 0; i < cnt * 2; i++)
        word_1BAE0[i] = word_1C6E8[i];
    readStageStream(word_1C6E8, cnt);
    for (i = 0; i < cnt; i++) {                          /* bit-pack decode */
        j = i * 2;
        word_1BAE0[j] = (((word_1C6E8[i] & 0x70) << 4) | word_1BAE0[j])
                        * ((word_1C6E8[i] & 0x80) ? 0xFFFF : 1);
        word_1BAE2[j] = (((word_1C6E8[i] & 7) << 8) | word_1BAE2[j])
                        * ((word_1C6E8[i] & 8) ? 0xFFFF : 1);
    }
    i = byte_1C6E2;
    byte_1C6E2 = 0;
    f19en_gety(word_23786, word_23788, word_1DA46, word_1DA48);
    byte_1C6E2 = (uint8)i;
    f19en_sub_12EF2(word_1BAD0, word_23786, word_23788, word_1BACE,
                    word_23786, word_23788, word_1DA46, word_1DA48);
    for (i = 0; i < word_1DA44; i++) {
        pr = (AnimRec *)f19_dsegAt(0x295E + i * sizeof(AnimRec));
        f19en_sub_12EF2(word_1BACE,
                        pr->posX + word_23786, pr->posY + word_23788,
                        word_1BAD0, pr->frameW[0], pr->frameB[0],
                        pr->maxA, pr->maxB);
    }
    for (i = 0; i < word_1DA44; i++) {
        pr = (AnimRec *)f19_dsegAt(0x295E + i * sizeof(AnimRec));
        ch = (ChanRec *)f19_dsegAt(0x3426 + i * sizeof(ChanRec));
        for (j = 1; j < ch->f0; j++) {
            f19en_sub_12EF2(word_1BAD0, pr->frameW[j-1], pr->frameB[j-1],
                            word_1BAD0, pr->frameW[j], pr->frameB[j],
                            pr->maxA, pr->maxB);
            f19en_gety(pr->frameW[j] + ch->f1[j], pr->frameB[j] + ch->f2[j],
                       ch->f3[j], ch->f4[j]);
        }
    }
    switch (initResultFlag) {
    case 0:
        *word_1BAD4 = *word_1BACE;
        *word_1BAD2 = (int16)0xB800;
        f19en_sub_133BD(0x28, 0x19, *word_1BAD0, 0, *word_1BAD4, 0);
        break;
    case 1:
        *word_1BAD2 = (int16)0xB800;
        *word_1BAD4 = *word_1BAD0;
        break;
    case 2:
        *word_1BAD2 = (int16)0xA800;
        *word_1BAD4 = (int16)0xA400;
        f19en_sub_133BA(0x28, 0x19, *word_1BAD0, 0, *word_1BAD4, 0);
        break;
    case 3:
        f19en_sub_133EC(0x28, 0x19, *word_1BAD0, 0, *word_1BACE, 0);
        *word_1BAD2 = (int16)0xA000;
        *word_1BAD4 = *word_1BAD0;
        f19en_sub_13436(0x28, 0x19, *word_1BACE, 0, *word_1BAD4, 0);
        break;
    }
    return 1;
}

/* ---- END-side service-name adapters ---------------------------------------
 * The ported sources call the driver slots by their F-15 names with int16
 * args; the real bindings live in f19ovl.c (ovlCall_*) and gfx_impl.c. */
int16 f19eg_loadFileSection(const char *name, int16 b, int16 c);
int   gfx_charWidthLegacy(int ch, int font);

int16 far gfx_charWidth(int16 ch, int16 font) { return (int16)gfx_charWidthLegacy(ch, font); }

void far gfx_switchColor(int16 *pg, int16 x1, int16 y1, int16 x2, int16 y2,
                         int16 oldC, int16 newC) {
    ovlCall_bc7(pg, x1, y1, x2, y2, oldC, newC);
}

/* sprite* are native pointers into world-2 dseg records; the slot consumes
 * the record's DOS offset. */
void far gfx_blitSprite(struct BlinkSprite *spr) {
    ovlCall_b4f((int16)f19_dsegOff(spr));
}
void far gfx_blitSprite(int16 *spr) {
    ovlCall_b4f((int16)f19_dsegOff(spr));
}

int16 far gfx_calcRowAddr(int16 x, int16 y) { return (int16)gfx_calcRowAddr((int)x, (int)y); }
void  far gfx_setBlitOffset(int16 v) { gfx_setBlitOffset((int)v); }
void  far gfx_getCurPage(int16 a) { gfx_setPageN((uint16)a); }
int16 far gfx_initDone(void) { return 3; }            /* native render is VGA-only */
void  far gfx_drvMode(void) { gfx_setMode13(0); }     /* EGA path; unreachable (initDone==3) */
int16 far gfx_getBufSize(void) { return ovlCall_b6d(); }
int16 far gfx_getAuxBufSize(void) { return ovlCall_bef(); }
int16 far gfx_getConst1(void) { return ovlCall_c49(); }
void  far gfx_setDac(int16 pal) { ovlCall_c4e(pal); }
void  far gfx_setOvlVal1(int16 v) { gfx_setOvlVal1((int)v); }
void  far gfx_setOvlVal2(int16 v) { gfx_setOvlVal2((int)v); }

int16 f19en_loadFileSection(const char *name, int16 b, int16 c) {
    return f19eg_loadFileSection(name, b, c);
}
