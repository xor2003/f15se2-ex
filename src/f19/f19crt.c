/* F-19 CRT/environment shims — the DOS routines' libc and int21 helpers
 * re-based on the app's services. */
#include <SDL3/SDL.h>
#include "f19.h"
#include "slot.h"

/* seg000:0xe236 — DOS time() was seconds since epoch via int21/2Ah+2Ch */
int16 time(int16 *t) {
    int32 now = (int32)SDL_GetTicks() / 1000;
    if (t) *t = (int16)(now & 0xffff);
    return (int16)now;
}

/* console input: original used CRT getch/getche + int21/0Bh key-wait */
int16 getch(void)  { return misc_getKey(); }
int16 getche(void) { return misc_getKey(); }
int16 putch(int16 c) { (void)c; return c; }   /* tty echo: no-op in SDL */
int16 sub_1E1EC(void) { return misc_checkKeyBuf() ? 1 : 0; }

/* DOS misc helpers used by the reconstruction */
int16 sub_14980(int16 h, int16 b, int16 c, int16 d) { (void)h;(void)b;(void)c;(void)d; return 0; }
/* seg000:0x4622 — clearRect: fill [x0,y0,x1,y1] inclusive on the descriptor's
 * page with the descriptor's fill colour (desc byte +6), then mark dirty
 * spans.  Natively: fill on the resolved page pixels; presents are whole-page
 * so the dirty-span tables aren't needed.  The descriptor arrives either as a
 * dseg pointer or a raw offset cast depending on call site. */
void  sub_14622(void *o, int16 x0, int16 y0, int16 x1, int16 y1) {
    int16 *d = ((uintptr_t)o < 0x10000)
             ? (int16 *)(f19_dseg + (uintptr_t)o) : (int16 *)o;
    int16 pg = d ? d[0] : 0;
    uint8 col = d ? ((uint8 *)d)[6] : 0;
    uint8 *px = (uint8 *)f19_pagePixels(pg);
    int16 y;
    if (!px) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 0x13F) x1 = 0x13F;
    if (y1 > 0xC7) y1 = 0xC7;
    for (y = y0; y <= y1; y++)
        memset(px + y * 320 + x0, col, (uint16)(x1 - x0) + 1);
}
void  sub_146E3(void) { }
void  sub_14107(int16 overlaySeg) { (void)overlaySeg; } /* slot patcher: native calls are direct */
int16 sub_1E454(int32 v, int16 n) { return (int16)((uint32)v >> n); }

/* exit() in START means "chain to the next program" — the merged build
 * returns to the descriptor dispatcher instead: f19_start_main setjmps
 * here and returns the code (0xC = RET_MENU -> run egame). */
#include <setjmp.h>
#include <time.h>
jmp_buf f19_chainExit;

void  sub_1DCAC(int16 code) { longjmp(f19_chainExit, code ? code : 1); }

/* seg000:0xe236 — CRT time(): seconds since epoch into *t and returned. */
int16 f19_time(int16 *t) {
    time_t now = time((time_t *)0);
    if (t) *t = (int16)now;
    return (int16)now;
}
