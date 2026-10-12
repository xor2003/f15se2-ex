/* F-19 skeleton stubs — routines the byte-exact reconstruction kept in asm
 * (int21h file I/O, rep-stosw blitters, LZW/pic decode, jump-table patchers,
 * CRT helpers, register-ABI trampolines) plus the undiscovered menu screens.
 * Signatures match the ported callers exactly (C++ name mangling); bodies are
 * no-ops / best-effort returns until each routine is ported and verified.
 *
 * TODO(port): the picture/decode cluster is what makes START's screens
 * actually paint — port these in lst order: sub_14B14/sub_14B86/sub_14C62
 * (sprite+pic loaders feeding resFileOpen'd data), sub_13B76/sub_15120/
 * sub_13D15/sub_13E38 (text), sub_14622 (clearRect), sub_1CBD8/sub_1CE56
 * (undiscovered ~500B menu screens at seg000:0xcbd8/0xce56). */
#include "f19.h"
#include "f19stvars.h"
#include "f19seg.h"
#include <stdarg.h>
#include <SDL3/SDL.h>

struct MenuRow { int16 name, yoff; };   /* stmenu.c private layout */
struct TileEntry;

/* ---- res-loaders / file wrappers (int21h family) ------------------------ */
int16 sub_148FE(int16 h, int16 n, int16 b) { return h; }   /* raw read */
void  sub_149A1(int16 h) { }
int16 sub_16828(int16 paras) {
    /* seg000:0x6828 — dos_alloc(paras); ax < 0x10 -> cleanup + errstr + exit. */
    extern void sub_10882(void);
    extern void dos_printstring(const char *s);
    extern void sub_1DCAC(int16);
    int16 seg = f19_allocSeg((uint16)paras);
    if (getenv("F19_DBG")) fprintf(stderr, "[ALLOC] paras=%x -> %d\n", paras, seg);
    if (seg < 0x10) {
        sub_10882();
        dos_printstring((char *)(((uint8 *)f19_dsegAt(0x3682))));
        sub_1DCAC(0);
    }
    return seg;
}
int16 sub_15AD2(int32 a) {
    /* seg000:0x5ad2 — dos_alloc(a paras) then far memset(seg:0,0,a);
     * f19_allocSeg callocs, so the zero-fill is already done. */
    return sub_16828((int16)a);
}
int16 sub_15B02(int16 n) { return n; }
int16 sub_15B22(int16 a, int16 b) { return a; }
int16 sub_153F2(int16 a, int16 fd) { return 0; }

/* The three seg000:0x4b14/4b86/4c62 routines are the same row-by-row PIC
 * decoder writing into a caller-selected destination (page index or seg).
 * Natively: app picDecodeToSurface into a wrapped INDEX8 view of the block. */
void f19_decodeTo(int16 fd, void *dst) {
    static int dbg = -1;
    SDL_IOStream *io;
    SDL_Surface *sf;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "decodeTo fd=%d dst=%p\n", fd, dst);
    if (!dst || fd < 0) return;
    io = f19_fileIo(fd);
    if (!io) return;
    sf = SDL_CreateSurfaceFrom(320, 200, SDL_PIXELFORMAT_INDEX8, dst, 320);
    if (sf) {
        picDecodeToSurface(io, sf);
        SDL_DestroySurface(sf);
    }
}
void  sub_14B14(int16 fd, int16 page, int16 b) {
    (void)b;
    f19_decodeTo(fd, f19_pagePixels(page));
}
void  sub_14B86(int16 fd, int16 sel) {
    void *d = f19_segPtr(sel);
    f19_decodeTo(fd, d ? d : f19_pagePixels(sel));
}
void  sub_14C62(int16 fd, int16 seg) {
    void *d = f19_segPtr(seg);
    f19_decodeTo(fd, d ? d : f19_pagePixels(seg));
}

/* ---- text / draw helpers ------------------------------------------------ */
static int16 *f19_descPtr(void *o);
extern void f19_drawStringFar(int16 *pageNum, const char far *string);
void  sub_13B50(int16 *p, struct MenuRow r, int16 c, int16 d) {
    /* seg000:0x3b50 — set the descriptor's draw position (+8/+A) then draw the
     * far name string via sub_13B9B (buf copy + drawString slot). The MenuRow
     * {name,yoff} pair is the string's (dseg-offset, seg-handle) far ptr. */
    p = f19_descPtr(p);
    p[4] = c;
    p[5] = d;
    f19_drawStringFar(p, (const char far *)F19_FP(r.yoff, r.name));
}
/* resolve a descriptor arg that callers pass either as a bare dseg offset
 * (cast through a pointer type) or as a real dseg-object pointer. */
static int16 *f19_descPtr(void *o) {
    if (f19_dsegOff(o) != 0xFFFF)
        return (int16 *)o;
    return (int16 *)f19_dsegAt((uint16)(uintptr_t)o);
}
void  sub_13B76(void *o, char *s, int16 x, int16 y) {
    /* seg000:0x3b76 — drawStringAt over a page-descriptor cell (shared engine
     * text service; identical routine exists in F-15 as drawStringAt). */
    drawStringAt(f19_descPtr(o), s, x, y);
}
extern void f19_wrapUnitText(int16 o, char *s, uint16 w, int16 x, int16 y, int16 dy);
extern void f19_wrapUnitTextFar(int16 o, char far *s, uint16 w, int16 x, int16 y, int16 dy);
struct TileEntry;
extern void f19_drawUnitList(struct TileEntry *tab, int16 namesOfs, int16 count, int16 outA, int16 outB, int16 *pd);
void  sub_13D15(int16 *pg, char *s, int16 a, int16 b, int16 c, int16 d) {
    /* seg000:0x3d15 — near-string wrap draw; = f19_wrapUnitText. Callers
     * pass the descriptor as a bare dseg offset OR a resolved pointer. */
    f19_wrapUnitText((int16)f19_dsegOff(f19_descPtr(pg)), s, a, b, c, d);
}
extern int16 f19_stringWidth(int16 *page, const uint8 *str);
int16 sub_13E38(int16 *p, char *s) {                       /* seg000:0x3e38 stringWidth */
    return f19_stringWidth(f19_descPtr(p), (uint8 *)s);
}
void  sub_13E7C(long v, char *buf) {
    /* seg000:0x3e7c — my_ltoa with a thousands comma (verified port from
     * f19ru src_start/textfmt.c). */
    int8 i, k;
    char *p;
    int8 n[6];
    p = buf;
    if (v < 0) { v = -v; *p++ = '-'; }
    n[0] = v % 0xa; v /= 0xa;
    n[1] = v % 0xa; v /= 0xa;
    n[2] = v % 0xa; v /= 0xa;
    n[3] = v % 0xa; v /= 0xa;
    n[4] = v % 0xa; v /= 0xa;
    n[5] = v % 0xa;
    i = 0;
    for (k = 5; k > 0; k--) {
        if (n[k] != 0) break;
    }
    do {
        if (k == 2 && i == 1) *p++ = ',';
        *p++ = n[k] + '0';
        i = 1;
    } while (--k >= 0);
    *p = 0;
}
void  sub_13FB3(int16 n, char *b) {
    /* seg000:0x3fb3 — itoa with a thousands comma: digits extracted LSD→MSD,
     * emitted MSD-first; ',' inserted before the hundreds digit only once at
     * least one digit has been emitted (i.e. value >= 1000). */
    int8  num[6];
    int8  i;
    int8  started = 0;
    if (n < 0) { n = -n; *b++ = '-'; }
    num[0] = n % 10; n /= 10;
    num[1] = n % 10; n /= 10;
    num[2] = n % 10; n /= 10;
    num[3] = n % 10; n /= 10;
    num[4] = n % 10; n /= 10;
    num[5] = n % 10;
    for (i = 5; i > 0 && num[i] == 0; i--) ;
    for (; i >= 0; i--) {
        if (i == 2 && started == 1)
            *b++ = ',';
        *b++ = num[i] + '0';
        started = 1;
    }
    *b = 0;
}
void  sub_10924(char *tab, int16 sel, int16 cnt, int16 x, int16 y, ...) {
    /* seg000:0x0924 — row repaint; already ported as f19_drawUnitList. */
    va_list ap;
    int16 *pd;
    va_start(ap, y);
    pd = va_arg(ap, int16 *);
    va_end(ap);
    f19_drawUnitList((struct TileEntry *)tab, sel, cnt, x, y, pd);
}
void  sub_14584(void *o, int16 a, int16 b, int16 c, int16 d) { }
extern void resFileReadBlock(const char *name, int16 page, int16 bufSeg);
extern int16 resFileOpen(const char *path, int16 mode);
extern int16 resFileClose(int16 h);
void  sub_14746(char *s, int16 a, int16 b) {
    /* seg000:0x4746 — resFileOpen(s) + raw whole-file read into seg b offset a
     * (sub_14929 = int21/3Fh with DS=seg, DX=off, CX=0xFFFF). */
    static int dbg = -1;
    int16 fd = resFileOpen(s, 0);
    SDL_IOStream *io;
    void *dst;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "loadFile %s -> seg %d off %d\n", s, b, a);
    if (fd < 0) return;
    io = f19_fileIo(fd);
    dst = f19_segPtr(b);
    if (io && dst)
        fileRead((char *)dst + (uint16)a, 1, 0x10000 - (uint16)a, io);
    resFileClose(fd);
}
void  sub_14A5F(char *s, int16 v) {          /* open + decode + close */
    int16 fd = resFileOpen(s, 0);
    sub_14B86(fd, v);
    resFileClose(fd);
}
void  sub_14ACB(char *s, int16 v, long x) {
    /* seg000:0x4acb — resFileOpen(s), lseek(x), decode PIC into seg v
     * (sub_1E172 + sub_14C62 in the original). */
    int16 fd = resFileOpen(s, 0);
    SDL_IOStream *h;
    if (fd < 0) return;
    h = f19_fileIo(fd);
    if (h) {
        SDL_SeekIO(h, (Sint64)x, SDL_IO_SEEK_SET);
        sub_14C62(fd, v);
    }
    resFileClose(fd);
}
void  sub_14BEE(int16 a, int16 b) {
    /* seg000:0x4bee — decode PIC bytes held in seg buffer `a` into the
     * 320x200 frame buffer `b` (original decoded straight to the hidden
     * page; here dst is an f19seg block wrapped as an INDEX8 surface). */
    void *src = f19_segPtr(a);
    void *dst = f19_segPtr(b);
    static int dbg = -1;
    SDL_IOStream *io;
    SDL_Surface *surf;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "decMemPic a=%d(%p) b=%d(%p) head=%02x %02x %02x %02x\n",
                     a, src, b, dst,
                     src ? ((uint8 *)src)[0] : 0, src ? ((uint8 *)src)[1] : 0,
                     src ? ((uint8 *)src)[2] : 0, src ? ((uint8 *)src)[3] : 0);
    if (!src || !dst) return;
    io = SDL_IOFromConstMem(src, 0x20000);
    if (!io) return;
    surf = SDL_CreateSurfaceFrom(320, 200, SDL_PIXELFORMAT_INDEX8, dst, 320);
    if (surf) {
        picDecodeToSurface(io, surf);
        SDL_DestroySurface(surf);
    }
    SDL_CloseIO(io);
}
extern int16 f19_setViewOrigin(int16 a, int16 b, int16 flag);
int16 sub_161CC(int16 a, int16 b, int16 c) {
    /* seg000:0x61cc — store (word_2CA50=a, word_2CA4E=b) then redraw through
     * f19_sub_15B68; = f19_setViewOrigin. */
    return f19_setViewOrigin(a, b, c);
}
void  sub_1685C(int16 v) {
    /* seg000:0x685c — dos_free(seg); failure: cleanup + errstr + exit(0). */
    extern void sub_10882(void);
    extern void dos_printstring(const char *s);
    extern void sub_1DCAC(int16);
    if (getenv("F19_DBG")) fprintf(stderr, "[FREE] h=%d\n", v);
    if (f19_freeSeg(v) != 0) {
        sub_10882();
        dos_printstring((char *)(((uint8 *)f19_dsegAt(0x36AC))));
        sub_1DCAC(0);
    }
}
/* sub_14E9C/sub_14EDA — PIT int8 install/uninstall. The original ISR bumps
 * byte_20A1A..byte_20A1D asynchronously; only the three non-aliased cells are
 * ticked here (byte_20A1A is the app's timerCounter). ~60 Hz like the app's
 * tick so the ported thresholds keep their timing. */
static SDL_Thread *f19_pitThread;
static volatile int f19_pitArmed;
static int f19_pitRun(void *unused) {
    (void)unused;
    for (;;) {
        SDL_Delay(16);
        if (!f19_pitArmed) continue;
        ++*(uint8 *)(((uint8 *)f19_dsegAt(0xA1A)));
        ++*(uint8 *)(((uint8 *)f19_dsegAt(0xA1B)));
        ++*(uint8 *)(((uint8 *)f19_dsegAt(0xA1C)));
        ++*(uint8 *)(((uint8 *)f19_dsegAt(0xA1D)));
    }
}
void  sub_14E9C(void) {
    f19_pitArmed = 1;
    if (!f19_pitThread) {
        f19_pitThread = SDL_CreateThread(f19_pitRun, "f19pit", NULL);
        if (f19_pitThread) SDL_DetachThread(f19_pitThread);
    }
}
void  sub_14EDA(void) { f19_pitArmed = 0; }

/* ---- string/move helpers ------------------------------------------------ */
void  sub_15120(char *d, char *s) { mystrcpy(d, s); }
void  sub_1513B(char *d, char *s) { mystrcpy(d, s); }  /* seg000:0x513b near->far strcpy */
void  sub_15152(char *d, const char *s) { mystrcpy(d, s); }  /* seg000:0x5152 far->near strcpy */
void  sub_15189(char *d, char *s) { mystrcat(d, s); }      /* seg000:0x5189 strcat */
void  sub_151D4(void *d, int8 v, int16 n) { memset(d, v, (uint16)n); }
void  sub_151E8(char *d, int16 v, int16 n) { memset(d, v, (uint16)n); } /* far memset */
void  sub_151FE(char *d, uint8 *s, int16 n) { memcpy(d, s, (uint16)n); }
void  sub_1521C(char *d, char *s, int16 n) { memcpy(d, s, (uint16)n); }
/* ---- seg000:0x7558-0x76c7 — briefing.txt LZW decoder ---------------------
 * The compressed stream lives in an f19seg block (arg t = seg handle);
 * records are NUL-terminated: record n is inflated into near buffer b.
 * Decoder state + the 0x800-entry {u16 prefix, u8 ch} dictionary occupy
 * real dseg cells 0x8074..0x98C2 (m_byte_27D67 + 0x30D / + 0x1B0D..). */
#define lzwDict   ((uint8 *)f19_stSpace.m_byte_27D67 + 0x30D)   /* dseg:0x8074 */
#define byte_29874 (*(uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B0D))
#define byte_29875 (*(uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B0E))
#define word_29876 (*(int16  *)(f19_stSpace.m_byte_27D67 + 0x1B0F))
#define word_29878 (*(int16  *)(f19_stSpace.m_byte_27D67 + 0x1B11))
#define byte_2987A (*(uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B13))
#define byte_2987B (*(uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B14))
#define byte_2987C ((uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B15)) /* 0x40 stack */
#define word_298BC (*(int16  *)(f19_stSpace.m_byte_27D67 + 0x1B55))
#define word_298BE (*(int16  *)(f19_stSpace.m_byte_27D67 + 0x1B57))
#define word_298C0 (*(int16  *)(f19_stSpace.m_byte_27D67 + 0x1B59))
#define byte_298C2 (*(uint8  *)(f19_stSpace.m_byte_27D67 + 0x1B5B))

static const uint8 *lzwSrc;                 /* es:di stream */
static uint16       lzwPos;

static void lzw_dictInit(void) {            /* sub_175A4 */
    int16 i;
    byte_29874 = 9;
    word_29876 = 0x1FF;
    word_29878 = 0x100;
    for (i = 0; i < 0x800; i++)
        *(int16 *)(lzwDict + i * 3) = -1;
    for (i = 0; i < 0x100; i++)
        lzwDict[i * 3 + 2] = (uint8)i;
}

static int16 lzw_readCode(void) {           /* sub_1768F -> cx */
    uint16 dx; int16 bl; int16 ax;
    dx = (uint16)(byte_2987A >> (8 - byte_2987B));
    bl = byte_2987B;
    while (bl < byte_29874) {
        byte_2987A = lzwSrc[lzwPos++];
        ax = (int16)byte_2987A;
        ax = (int16)(ax << bl);
        dx |= (uint16)ax;
        bl += 8;
    }
    bl -= byte_29874;
    byte_2987B = (uint8)bl;
    return (int16)(dx & (uint16)word_29876);
}

static void lzw_decodeCode(void) {          /* sub_175F0 */
    int16 cx, bx; uint8 al;
    cx = lzw_readCode();
    word_298C0 = cx;
    if (cx >= word_29878) {                 /* KwKwK: decode prev + first ch */
        byte_2987C[word_298BC] = byte_298C2;
        word_298BC++;
        cx = word_298BE;
        word_298C0 = word_29878;
    }
    for (;;) {
        bx = cx * 3;
        if (*(int16 *)(lzwDict + bx) == -1)
            break;
        cx = *(int16 *)(lzwDict + bx);
        al = lzwDict[bx + 2];
        if (word_298BC < 0x40)
            byte_2987C[word_298BC] = al;
        word_298BC++;
    }
    al = lzwDict[bx + 2];
    byte_298C2 = al;
    byte_2987C[word_298BC] = al;
    word_298BC++;
    bx = word_29878 * 3;
    lzwDict[bx + 2] = al;
    *(int16 *)(lzwDict + bx) = word_298BE;
    word_29878++;
    if (word_29878 > word_29876) {
        byte_29874++;
        word_29876 = (int16)((word_29876 << 1) | 1);
    }
    if (byte_29874 > byte_29875)
        lzw_dictInit();
    word_298BE = word_298C0;
}

static uint8 lzw_popByte(void) {            /* sub_175D8 -> al */
    if (word_298BC == 0)
        lzw_decodeCode();
    word_298BC--;
    return byte_2987C[word_298BC];
}

char *sub_17558(int16 n, char *b, int16 t) {  /* briefing line -> b */
    char  *ret = b;
    int16  cx;
    uint8  al;
    lzwSrc = (const uint8 *)f19_segPtr(t);
    lzwPos = 0;
    lzw_dictInit();                          /* sub_1758E body */
    word_298BC = 0;
    byte_2987B = 0;
    byte_29875 = lzwSrc[lzwPos++];
    cx = n;
    if (cx) {
        do {
            do { al = lzw_popByte(); } while (al != 0);
        } while (--cx);
    }
    cx = 0x200;
    do {
        al = lzw_popByte();
        *b++ = (char)al;
    } while (al != 0 && --cx);
    return ret;
}
void  sub_18B7E(const char *s, int16 a, int16 b, int16 c) { }

/* ---- input/poll/anim helpers -------------------------------------------- */
void  sub_10882(void) { }
void  sub_14089(int16 n) { }
extern void f19_seedRng(void);          /* seg000:0x40a3 — ported in stutil.c */
void  sub_140A3(void) { f19_seedRng(); }
/* sub_141AF — clip the (lineX0,lineY0)-(lineX1,lineY1) segment to the blit
 * window [0..clipMaxX]x[0..clipMaxY] (Cohen-Sutherland outcodes) and emit
 * through the driver line slot (far 1000h:0B95h = gfx_drawLine). The stub
 * swallowed every START line; window-relative coords, blitOffset adds the
 * viewport origin. */
extern void far gfx_drawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2);
void  sub_141A3(void) {
    int32 x0 = g_lineX0, y0 = g_lineY0, x1 = g_lineX1, y1 = g_lineY1;
    int32 cx = g_clipMaxX, cy = g_clipMaxY;
    for (;;) {
        int c0 = (x0 < 0) | ((x0 > cx) << 1) | ((y0 < 0) << 2) | ((y0 > cy) << 3);
        int c1 = (x1 < 0) | ((x1 > cx) << 1) | ((y1 < 0) << 2) | ((y1 > cy) << 3);
        if (!(c0 | c1)) { gfx_drawLine((uint16)x0, (uint16)y0, (uint16)x1, (uint16)y1); return; }
        if (c0 & c1) return;
        {
            int c = c0 ? c0 : c1;
            int64 x, y;
            if (c & 8)      { x = x0 + (x1 - x0) * (int64)(cy - y0) / (y1 - y0); y = cy; }
            else if (c & 4) { x = x0 + (x1 - x0) * (int64)(0  - y0) / (y1 - y0); y = 0;  }
            else if (c & 2) { y = y0 + (y1 - y0) * (int64)(cx - x0) / (x1 - x0); x = cx; }
            else            { y = y0 + (y1 - y0) * (int64)(0  - x0) / (x1 - x0); x = 0;  }
            if (c == c0) { x0 = (int32)x; y0 = (int32)y; }
            else         { x1 = (int32)x; y1 = (int32)y; }
        }
    }
}
int16 sub_150BA(void) {                /* int 1Ah AH=0 — DX low tick word */
    /* Deterministic parity with the original under DOSBOX_DETERMINISTIC:
     * START's seedRng read tick-low=0x59 there; F19_SEED overrides. */
    const char *s = getenv("F19_SEED");
    return (int16)(s ? (int)strtol(s, 0, 0) : 0x59);
}
int16 sub_16261(int16 e) { return e; }
void  sub_16208(void) { }
void  sub_167FD(void) { }
void  sub_16C0E(void) { }
void  sub_16D90(int16 a,int16 b,int16 c,int16 d,int16 e,int16 f) { }
void  sub_16D93(int16 a,int16 b,int16 c,int16 d,int16 e,int16 f) { }
void  sub_16DC2(int16 a,int16 b,int16 c,int16 d,int16 e,int16 f) { }
void  sub_16E0C(int16 a,int16 b,int16 c,int16 d,int16 e,int16 f) { }
void  sub_168C8(int16 a,int16 b,int16 c,int16 d,
                int16 e,int16 f,int16 g,int16 h) { }
void  sub_169BE(int16 a,int16 b,int16 c,int16 d) { }
void  sub_16AE7(int16 a,int16 b,int16 c,int16 d) { }

/* ---- tile/object redraw -------------------------------------------------- */
/* sub_13218 (pilot detail panel, seg000:0x3218) ported in stpanel.c. */

/* ---- mode/settle screens ------------------------------------------------ */
void  sub_125EA(void) { }
/* seg000:0xa376 — Roster.Fil save (fopen "wb" + record write; write-protect
 * banner arm) is ported as f19_saveHallfame in stpinp.c. seg000:0xa4fb is
 * the matching load (fopen "rb") = f19_loadHallfame. */
extern void f19_saveHallfame(void);
extern void f19_loadHallfame(void);
void  sub_1A376(void) { f19_saveHallfame(); }
void  sub_1A4FB(void) { f19_loadHallfame(); }
/* sub_1CBD8/sub_1CE56 ported in starm.c (notepad + arming screens). */

/* ---- theater-specific draw procs (byte_2B83E selects 0..3) --------------- */
void far ovl_47B(int16 w1,int16 w2,int16 w3,int16 w4,
                 int16 w5,int16 w6,int16 w7,int16 w8) { }
void far ovl_766(int16 w1,int16 w2,int16 w3,int16 w4,
                 int16 w5,int16 w6,int16 w7,int16 w8) { }
void far ovl_169(int16 w1,int16 w2,int16 w3,int16 w4,
                 int16 w5,int16 w6,int16 w7,int16 w8) { }
void far ovl_A65(int16 w1,int16 w2,int16 w3,int16 w4,
                 int16 w5,int16 w6,int16 w7,int16 w8) { }

/* ---- terrain query (seg000:0x6e8c) — real port lives in stgen.c ------------ */

