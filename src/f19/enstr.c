/* ported from f19ru src_end/enstr.c — see that file for seg000 offsets */
/* END.EXE — string helpers (seg000:0x38ba-0x394a, f15 util lineage).
 * Same cluster as START's stutil.c twins; see notes below for the
 * hand-asm members that stay in the skeleton. */
#include "f19en.h"

void mystrcpy(char *dst, const char *src) {   /* seg000:0x38ba */
    do {
    } while ((*dst++ = *src++) != '\0');
}

void mystrcat(char *d, const char *s) {  /* seg000:0x3923 */
    for (;;) {
        if (*d == 0) break;
        d++;
    }
    for (; (*d = *s++) != 0; d++) ;
}

/* seg000:0x38d5 strcpyToFar, 0x38ec farStrcpy — hand-asm (LES/LDS +
 * LODSB/STOSB, register-save pattern). Skeleton. */
/* seg000:0x3907 mystrlen — hand-asm: preloads s into ax pre-loop,
 * ~(s_orig - s_end) tail, zero locals. Skeleton. */
/* seg000:0x394b mystrchr — hand-asm (push si never used, ch hoisted to ax
 * before loop); skeleton stays. */
/* seg000:0x396e memsetNear, 0x3982 memsetFar, 0x39b6 memcpyFromFar —
 * rep-stosb/movsb asm; 0x3998 copyBytes (`loop`) and 0x39d2 memeq
 * (byte-stepped word cmp + `loope`) likewise. Skeleton. */

extern int16 rand(void);                                  /* 0x8c96 — libc */
extern void seedRandom16(int16 v);                        /* 0x8c84 — srand */
extern int16 readBiosTickLo(void);                        /* 0x3844 */

/* seg000:0x0cf3 — naked: no bp frame even under opt (same as START
 * sub_161F1). /Gs module. */
void seedRandom(void) {
    seedRandom16(readBiosTickLo());
}

/* seg000:0x0cfe — (zext(maxVal) * rand()) >> 15 */
int16 randomRange(int16 maxVal) {
    return (int16)(((int32)(uint16)maxVal * (int32)rand()) >> 15);
}
