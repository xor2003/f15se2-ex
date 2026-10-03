/* F-19 overlay-call shims — native implementations of the DOS trampoline
 * slots.  MGRAPHIC.EXE's handler table was decoded from the driver image:
 * slot N's trampoline lives at dseg 0xAFA + N*5 (jmp far), handlers are the
 * seg001 routines in map/mgraphic_en.map.  The F-19 sources name them either
 * ovlCall_<dseg trampoline off> or by the F-15 slot names (gfx_/misc_/audio_ prefixes).
 *
 * Page/buffer calls the F-15 app paths never used are modeled locally:
 * page "segments" are f19seg handles, blitToCurrent uploads a block into the
 * page surface.  Driver "const cell" getters return the MGRAPHIC (VGA) values
 * read from the image (cs:0x1d0..0x1dc): bufSize 0xFA00, auxBufSize 0xFA00,
 * modeFlag2 0, const1 1, modeFlag 1, val2 1, val 0.
 */
#include "f19.h"
#include "struct.h"
#include "gfx.h"
#include "slot.h"
#include "asound/asound_model.h"
#include "gfx_impl.h"
#include "r2d.h"
#include <SDL3/SDL.h>
#include <string.h>

#undef audio_jump_6b   /* asound_model maps it to the timer tick; we need the real symbol */

void far gfx_storeBufPtr(int16 p, int16 n);

/* slot17 gfx_blitSprite — arg is a dseg offset of a SpriteParams record.
 * stutil declares the offset-taking form under the slot's own name, so both
 * the ovlCall_* trampoline and the gfx_ alias resolve here. */
static int16 f19_pageSegTab[16];   /* page idx -> seg handle (0 = app page 0) */
static int16 f19_bufTab[64];       /* storeBufPtr slot -> seg handle */

static R2DImage *f19_sprImg[32];

void far gfx_blitSprite(int16 spr) {
    /* SpriteParams.bufPtr is the driver's sprite-buffer slot; F-19 keeps its
     * sheets in allocated segments registered via gfx_storeBufPtr. */
    struct SpriteParams *p = (struct SpriteParams *)(f19_dseg + (uint16)spr);
    int16 seg;
    void *src;
    R2DImage *img;
    SDL_Surface *sf;
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (p->bufPtr >= 0 && p->bufPtr < 64)
        seg = f19_bufTab[p->bufPtr];
    else
        seg = 0;
    src = seg ? f19_segPtr(seg) : f19_segPtr(p->bufPtr);  /* raw seg handle works too */
    if (dbg)
        fprintf(stderr, "blitSprite spr=0x%x buf=%d seg=%d src=%p dst=%d,%d wh=%d,%d sxy=%d,%d\n",
                (uint16)spr, p->bufPtr, seg, src, p->dstX, p->dstY, p->width, p->height, p->srcX, p->srcY);
    if (!src) {                                        /* app sprite-buf handle */
        gfx_blitSprite(p);
        return;
    }
    img = f19_sprImg[p->bufPtr & 0x1F];
    if (!img) {
        img = r2d_registerImage(320, 200);
        f19_sprImg[p->bufPtr & 0x1F] = img;
    }
    sf = img ? r2d_imageSurface(img) : (SDL_Surface *)0;
    if (!sf) return;
    memcpy(sf->pixels, src, 320 * 200);
    r2d_submitImage(img, p->srcX, p->srcY, p->width, p->height,
                    p->dstX, p->dstY, 0);
}
int16 far ovlCall_b4f(int16 spr) { gfx_blitSprite(spr); return 0; }

/* slot 0x3c — F-19's driver took a mono flag; the native renderer doesn't */
void far gfx_setMode13(int16 mono) {
    (void)mono;
    gfx_setMode13();
}

int16 far ovlCall_b6d(void) { return 0xFA00; }   /* gfx_getBufSize cell */
void  far ovlCall_b9f(int16 color) { gfx_setColor(color); }
void  far ovlCall_ba9(void) { }                  /* nop22: bare RETF */
void  far ovlCall_bc7(int16 *pg, int16 x1, int16 y1, int16 x2, int16 y2,
                      int16 oldC, int16 newC) {
    gfx_switchColor(pg, x1, y1, x2, y2, oldC, newC);
}
int16 far ovlCall_bef(void) { return 0xFA00; }   /* gfx_getAuxBufSize cell */
int16 far ovlCall_bf4(void) { return 0x6000; }   /* gfx_getFreeMem paras:
                                                    native heap is large;
                                                    384KB always picks the
                                                    full-featured tier */
void  far ovlCall_c2b(int16 steps) { gfx_setFadeSteps(steps); }
int16 far ovlCall_c49(void) { return 1; }        /* gfx_getConst1 */
void  far ovlCall_c4e(int16 pal) { gfx_setDac(pal); }
int16 far ovlCall_c53(void) { gfx_waitRetrace(); return 0; }
static void f19_dumpFrame(void) {
    static int en = -1, n;
    SDL_Surface *pg;
    char path[64];
    if (en < 0) en = getenv("F19_DUMP") ? 1 : 0;
    if (!en) return;
    pg = gfx_getCurPageSurface();
    if (!pg) return;
    SDL_SaveBMP(pg, "/tmp/f19frame.bmp");
    if (n < 60) {
        snprintf(path, sizeof(path), "/tmp/f19seq_%03d.bmp", n++);
        SDL_SaveBMP(pg, path);
    }
}
void  far ovlCall_c58(void) { gfx_flipPage(); f19_dumpFrame(); }
void  far ovlCall_c71(int16 ptr, int16 n) {      /* gfx_storeBufPtr */
    gfx_storeBufPtr(ptr, n);
}
int16 far ovlCall_c7b(void) { return 1; }        /* gfx_getVal2: driver cs:1da */
void  far ovlCall_c8a(void) { gfx_commitPage(); f19_dumpFrame(); }

int16 far ovlCall_cbc(void) { return misc_checkKeyBuf(); }
int16 far ovlCall_cc1(void) { return misc_getKey(); }
int16 far ovlCall_ccb(int16 axis) { return misc_readJoystick(axis); }
void  far ovlCall_cd0(void) { misc_clearKeyFlags(); }

void  far ovlCall_cee(void) { audio_setup(0, 0); }
void  far ovlCall_cf3(void) { audio_shutdown(); }
void  far ovlCall_cfd(void) { audio_playIntro(); }

/* ---- page-buffer table ---------------------------------------------------
 * The DOS driver recorded buffer segments at load; here they're f19seg
 * handles so blit paths can resolve them. */
void *f19_pagePixels(int16 n) {
    int16 seg;
    if (n <= 0)
        return gfx_pagePixels(0, (int *)0);      /* the visible back buffer */
    seg = f19_pageSegTab[n & 0xF];
    if (!seg) {
        seg = f19_allocSeg(0x1000);              /* lazily back scratch pages */
        f19_pageSegTab[n & 0xF] = seg;
    }
    return f19_segPtr(seg);
}

int16 far gfx_allocPage(int16 pageNum) {                 /* slot 0x00 */
    int16 h = f19_allocSeg(0x1000);                      /* 64KB page */
    if (pageNum >= 0 && pageNum < 8)
        f19_pageSegTab[pageNum] = h;
    return h;
}
void  far gfx_setPageN(uint16 n) {                       /* slot 0x0e */
    (void)n;   /* curPage selection is implicit in the SDL surface model */
}
void  far gfx_storeBufPtr(int16 p, int16 n) {            /* slot 0x4b */
    if (n >= 0 && n < 64)
        f19_bufTab[n] = p;
}
int16 far gfx_getVal(void) { return 0; }                 /* slot 0x4e cell */
void  far gfx_blitToCurrent(int16 seg) {                 /* slot 0x30 */
    void *buf = f19_segPtr(seg);
    SDL_Surface *pg;
    if (!buf) return;
    pg = gfx_getCurPageSurface();
    if (pg) memcpy(pg->pixels, buf, 320 * 200);
}
void  far ovlCall_bea(int16 seg) {
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "blitToCurrent seg=%d ptr=%p\n", seg, f19_segPtr(seg));
    gfx_blitToCurrent(seg);
}

/* slot 0x2b gfx_clearVga: fills VGA 0xA000 with the arg colour — natively
 * the current page surface */
int16 far gfx_unknown2b(int16 v) {
    SDL_Surface *pg = gfx_getCurPageSurface();
    if (pg) memset(pg->pixels, v & 0xFF, 320 * 200);
    return v;
}
void  far gfx_resetBlitOffset2(void) { }                 /* slot 0x23 = nop */

/* slot 0x2a — rect copy; F-19 page indices are seg-backed buffers here */
void  far f19_gfx_copyRect(int src, uint16 sx, uint16 sy, int dst,
                           uint16 dx, uint16 dy, int w, int h) {
    uint8 *sp = (uint8 *)f19_pagePixels((int16)src);
    uint8 *dp = (uint8 *)f19_pagePixels((int16)dst);
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    if (dbg) fprintf(stderr, "copyRect %d(%d,%d)->%d(%d,%d) %dx%d sp=%p dp=%p\n",
                     src, sx, sy, dst, dx, dy, w, h, sp, dp);
    int y;
    if (!sp || !dp) return;
    for (y = 0; y < h; y++)
        memcpy(dp + (dy + y) * 320 + dx, sp + (sy + y) * 320 + sx, w);
}

/* drawString via params-pool offset (f19's 2-arg form; the app's takes a
 * record pointer — overloads coexist by signature) */
void  far gfx_drawString(int16 o, char *s) {
    gfx_drawString((int16 *)(f19_dseg + (uint16)o), s);
}

/* ---- misc/input + audio slot aliases ------------------------------------- */
int16 far misc_jump_5a_keybuf(void) { return misc_checkKeyBuf(); }
int16 far misc_jump_5b_getkey(void) { return misc_getKey(); }
int16 far misc_jump_5d_readJoy(int16 n) { return misc_readJoystick(n); }
void  far misc_jump_5e_clearKeyFlags(void) { misc_clearKeyFlags(); }
void  far audio_jump_6b(void) { sound_driver_timer_tick(); }

/* ovlF43_a / ovlF43_10d live in f19file.c (scenery0.exe string-resource
 * loader + dseg far-ptr table splat). */
void  far ovlFee_23(void) { }
void  far ovlFee_fd(int8 far *p) { (void)p; }
