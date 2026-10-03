/* F-19 EGAME service shims — flat-model implementations of the CRT/asm
 * helpers the ported sources declare, plus no-op stubs for routines the
 * reconstruction left unported (f19ru src/stubs.c has the same status). */
#include "f19eg.h"
#include <stdio.h>

/* ---- CRT helpers ------------------------------------------------------- */

#undef getTimeOfDay
extern int getTimeOfDay(void);          /* shared/miscimpl.c — BIOS 18.2Hz low word */
int f19eg_getTimeOfDay(void) { return getTimeOfDay(); }

/* the sources declare `int16 abs(int16)` — a real overload alongside the
 * library's int/long variants */
int16 abs(int16 v) { return (int16)(v < 0 ? -v : v); }

/* int32* overloads of the eg3dmath shift helpers (the app's take long*) */
void shiftLongLeftInPlace(int count, int32 *ptr) {
    *ptr = (int32)((uint32)*ptr << count);
}
void shiftLongRightInPlace(int count, int32 *ptr) {
    *ptr = *ptr >> count;
}

/* ---- gfx driver slots -------------------------------------------------- */

/* F-19 decl sigs (int16/uint8 args) coexist as overloads next to the app's
 * int-arg impls — the wrappers forward to the shared driver (MGRAPHIC is
 * ~94% identical between the games per f19ru AGENTS.md). */
extern void FAR CDECL gfx_setColor(int color);
extern void FAR CDECL gfx_setFadeSteps(int steps);
extern void FAR CDECL gfx_setDacAnimCount(uint16 count);
extern int  FAR CDECL audio_playSound(int soundId);
extern int  fixedMulQ14(int a, int b);

extern int  FAR CDECL audio_setup(int16, int16);
extern void FAR CDECL gfxInit(void);
extern void openBlitClosePic(const char *filename, int page);  /* egpic.c */

void far gfx_setColor(uint8 c)        { gfx_setColor((int)c); }
void far gfx_setFadeSteps(int16 n)    { gfx_setFadeSteps((int)n); }
void far gfx_setDacAnimCount(int16 n) { gfx_setDacAnimCount((uint16)n); }
void far audio_playSound(int16 id)    { audio_playSound((int)id); }
int16 fixedMulQ14(int16 a, int16 b)   { return (int16)fixedMulQ14((int)a, (int)b); }

/* egmain's no-arg decls overload the app's slotted versions */
void audio_setup(void)                { audio_setup(0, 0); }
void gfx_initOverlay(void)            { }           /* overlay table is pre-bound */
void gfx_setMonoFlag(int16 f)         { (void)f; }  /* mono/herc flag — EGA path only */
void setupOverlaySlots(uint16 addr)   { (void)addr; } /* far-jump table pre-bound */

/* F-19 decl: (dseg offset of filename, page). The app's openBlitClosePic
 * prefers a PNG replacement whose embedded palette would override the F-19
 * DAC table that setupDac just loaded — decode the legacy PIC instead. The
 * decoder targets the single back buffer regardless of page; F-19's copyRect
 * sources from seg-backed pages, so mirror the decode into the requested
 * page. */
uint8 *gfx_pagePixels(int page, int *pitchOut);
void *f19_pagePixels(int16 n);                                  /* f19ovl.c */
SDL_IOStream *openFileWrapper(const char *filename, int mode);
void closeFileWrapper(SDL_IOStream *handle);
void showPicFile(SDL_IOStream *handle, int pageNum);
void openBlitClosePic(int16 nameOff, int16 page) {
    int pitch, y;
    uint8 *src, *dst;
    SDL_IOStream *h = openFileWrapper((const char *)(f19_dseg + nameOff), 0);
    showPicFile(h, page);
    closeFileWrapper(h);
    src = gfx_pagePixels(0, &pitch);
    dst = (uint8 *)f19_pagePixels(page);
    if (!src || !dst) return;
    for (y = 0; y < 200; y++)
        memcpy(dst + y * 320, src + y * pitch, 320);
}

void FAR CDECL gfx_drawLine(uint16, uint16, uint16, uint16);   /* gfx_impl.c */
void drawLine(int16 x1, int16 y1, int16 x2, int16 y2) {
    gfx_drawLine((uint16)x1, (uint16)y1, (uint16)x2, (uint16)y2);
}

/* draw-page cell — the F-19 driver's primitives read it implicitly */
static int16 f19eg_drawPage;
uint8 far gfx_getDrawPage(void) { return (uint8)f19eg_drawPage; }
void  far gfx_setDrawPage(int16 p) { f19eg_drawPage = p; }

void far gfx_drawString(int16 *page, const char *str);         /* gfx_impl.c */
void far gfx_drawString(int16 *page, const char *str, int16 len) {
    (void)len;
    gfx_drawString(page, str);
}

/* ---- timer tick hook -----------------------------------------------------
 * sub_120B0 — the work F-19's int8 ISR did each tick beyond the shared
 * 60 Hz pump: bump the timing accumulator (word_32DD0) and the
 * waitFrameSync byte (byte_32DD2 = g_timerTick), then the DAC
 * colour-cycle slot (sub_2F110 = gfx_dacCycle). Registered from
 * egmain.c's runGameSession via setTimerTickHook. */
/* sub_11BB4 — F-19's DAC init loads ALL 256 registers from the baked table
 * at dseg:0x3482 (int10 AX=1012, BX=0, CX=0x100) — unlike the app's setupDac
 * which only writes 0x10-0xFF from F-15's tables. */
extern void gfx_setDacRange(uint16 start, uint16 count, const uint8 *triples);
void f19eg_setupDac(void) {
    gfx_setDacRange(0, 0x100, f19_dseg + 0x3482);
}

void FAR CDECL gfx_dacCycle(void);
void f19eg_advanceFrameTick(void) {
    g_frameSyncPending = 0;               /* byte_32DA3 — ISR clears before 120B0 */
    *(int16 *)(f19_dseg + 0x3F60) += 1;   /* word_32DD0 — frame-timing accum */
    g_timerTick++;                        /* byte_32DD2 — waitFrameSync byte */
    gfx_dacCycle();
}

/* ---- unported asm routines (f19ru stubs.c status) ---------------------- */

/* sub_186FC — port of the real routine (was skeleton-only in f19ru): panel
 * 0x16 repaints the 7 weapon-station indicators from g_bombDamageMask; if
 * id is the live panel mode its mode text is redrawn too. */
void drawStatusItem(int16, int16);      /* sub_19007 (egtacmap.c) */
void drawPanelModeText(int16);          /* sub_18651 (egtacmap.c) */
void refreshActivePanel(int16 id) {
    int16 i;
    if (id == 0x16) {
        for (i = 0; i < 7; i++)
            drawStatusItem(i + 0x0A,
                           (g_bombDamageMask & (1 << i)) ? 0x0C : 0x0A);
    }
    if (id == g_curPanelMode)
        drawPanelModeText(id);
}

void notifyViewObj(int16 idx) { (void)idx; }                   /* sub_14C98 */
void hwPortWrite(int16 cmd) { (void)cmd; }                     /* sub_14CAC */
extern void f19eg_keyDispatch(uint16 scanCode);                /* egkeys.c sub_1D4C6 */
int16 dispatchKeyCmd(int16 key) { f19eg_keyDispatch((uint16)key); return 0; }
/* drawFuelCell (sub_19D5E): fuel bar x=180..180+amount/112, y=187..193. */
extern int16 f19eg_clampRange(int16 v, int16 lo, int16 hi);
extern void f19eg_setDrawColor(int16 color);
extern void f19eg_fillRectBoth(int16 x1, int16 y1, int16 x2, int16 y2);
void drawFuelCell(int16 amount, int16 color) {
    amount = f19eg_clampRange(amount, 0, 10000);
    if (amount > 0x6F) {
        f19eg_setDrawColor(color);
        f19eg_fillRectBoth(0xB4, 0xBB, 0xB4 + amount / 0x70, 0xC1);
    }
}
/* projectVertex (sub_11372) == drawNearestTileObject in eg3dmap.c */
extern void f19eg_drawNearestTileObject(uint32 c1, uint32 c2, uint32 c3);
void projectVertex(int32 vx, int32 vy, int32 vz) {
    f19eg_drawNearestTileObject((uint32)vx, (uint32)vy, (uint32)vz);
}
void FAR gfx_drawStatusBox(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2,
                           int16 old, int16 val) {
    (void)page; (void)x1; (void)y1; (void)x2; (void)y2; (void)old; (void)val;
}
/* ---- setupInstrumentLayoutFar (sub_2208A -> sub_22A14/sub_22A28) ------------
 * F-19's copy of the driver-adjacent layout init. The app's eghudr.c version
 * writes F-15 globals; F-19's instrument table lives in its own dseg cells
 * (tape sprite records 0x41D4..0x4240, layout params 0x424C..0x427A, tape
 * text records 0x4188..0x41CA — listing VAs word_33044..byte_330EA).
 * Ported verbatim from the listing — the ==1 (full-detail) constants are
 * kept for fidelity though byte_330EA is always 0 after sub_22A14. Also
 * calls the app impl so the shared HUD overlay keeps its own layout state.
 * EW/EB take the listing VA low word (VA - 0x2EE70 = dseg offset). */
#undef setupInstrumentLayoutFar
extern void FAR setupInstrumentLayoutFar(void);     /* eghudr.c (F-15 impl) */
extern int  FAR CDECL gfx_getPresetOffset2(void);   /* slot 0x1d = sub_2F0BB */
extern int  FAR CDECL gfx_getPresetOffset1(void);   /* slot 0x1c = sub_2F0B6 */
/* gfxBufPtr: f19eg macro bound to dseg 0x9EAC (= word_38D1C) */

#define EW(o)   (*(int16 *)(f19_dseg + (o) + 0x1190))
#define EB(o)   (*(uint8 *)(f19_dseg + (o) + 0x1190))

/* sub_22A28 body: the layout-table write branch. Also called standalone as
 * applyViewScaleMode (sub_2208E trampoline) after byte_330EA toggles. */
static void writeInstrumentLayoutCells(void) {
    if (EB(0x30EA) == 1) {
        EW(0x30BD) = 0x64;  EB(0x30BF) = 8;
        EW(0x30C0) = 0x0A;  EB(0x30C2) = 0;
        EW(0x30C3) = 0x66;  EW(0x30C5) = 0xCC;
        EB(0x30BC) = 0x88;
        EW(0x30C7) = 0x12;  EW(0x30C9) = 0xAF;
        EW(0x30CB) = 4;     EW(0x30CD) = 2;
        EW(0x30CF) = 0xFFE9; EW(0x30D1) = 0xFFF8;
        EW(0x30D3) = 9;     EW(0x30D5) = 0x17;
        EW(0x30D7) = 0x1A;
        EB(0x30D9) = 0x34;
        EW(0x30DA) = 0x1F;  EW(0x30DC) = 0x0D;
        EW(0x30DE) = 0x50;  EW(0x30E0) = 0x9F;
        EW(0x30E2) = gfx_getPresetOffset1();        /* sub_2F0B6 */
        EW(0x30E4) = 0x42;  EW(0x30E6) = 0x25;
        EW(0x30E8) = 0x6C;
        EW(0x2FFA) = 0x44;  EW(0x2FFC) = 0x60;
        EW(0x2FF8) = EW(0x300E) = EW(0x3024) = EW(0x303A) = 2;
        EW(0x300C) = 0x3B;
        EW(0x3014) = 0x82;  EW(0x3016) = 0xBC;
        EW(0x3064) = 0x93;  EW(0x3066) = 0x14;
        EW(0x306A) = 0x99;  EW(0x306C) = 0x4C;
        EW(0x306E) = 0x0D;  EW(0x3070) = 9;
        EW(0x304C) = 0x82;  EW(0x304E) = 0x40;
        EW(0x3050) = 0x3B;  EW(0x3052) = 2;
        EW(0x3026) = 0x44;  EW(0x302A) = 0x7F;
        EW(0x302C) = 0xC3;  EW(0x3038) = 0x3F;
        return;
    }
    EW(0x30BD) = 0x5E;  EB(0x30BF) = 0x11;
    EW(0x30C0) = 0x14;  EB(0x30C2) = 1;
    EW(0x30C3) = 0x31;  EW(0x30C5) = 0xFF;
    EB(0x30BC) = 0x6D;
    EW(0x30C7) = 0x2D;  EW(0x30C9) = 0xF8;
    EW(0x30CB) = 0x0A;  EW(0x30CD) = 5;
    EW(0x30CF) = 0xFFC4; EW(0x30D1) = 0xFFF1;
    EW(0x30D3) = 0x10;  EW(0x30D5) = 0x3C;
    EW(0x30D7) = 0x34;
    EB(0x30D9) = 0x68;
    EW(0x30DA) = 0x4F;  EW(0x30DC) = 0x24;
    EW(0x30DE) = 0x38;  EW(0x30E0) = 0x9F;
    EW(0x30E2) = gfx_getPresetOffset2();            /* sub_2F0BB */
    EW(0x30E4) = 0xA0;  EW(0x30E6) = 0x56;
    EW(0x30E8) = 0x3C;
    EW(0x2FFA) = 0x1A;  EW(0x2FFC) = 0x56;
    EW(0x2FF8) = EW(0x300E) = EW(0x3024) = EW(0x303A) = 0;
    EW(0x300C) = 0x0A;
    EW(0x3014) = 0x5A;  EW(0x3016) = 0xE6;
    EW(0x3064) = 0x82;  EW(0x3066) = 0x39;
    EW(0x306A) = 0x93;  EW(0x306C) = 0x30;
    EW(0x306E) = 0x19;  EW(0x3070) = 0x0F;
    EW(0x304C) = 0x5A;  EW(0x304E) = 0x10;
    EW(0x3050) = 0x8D;  EW(0x3052) = 3;
    EW(0x3026) = 0x14;  EW(0x302A) = 0x4E;
    EW(0x302C) = 0xF1;  EW(0x3038) = 0x10;
}

/* sub_2208A: sub_22A14 (sprite-ptr init + byte_330EA=0) then the 22A28 body */
void far f19eg_setupInstrumentLayoutFar(void) {
    setupInstrumentLayoutFar();                     /* app overlay state */
    EW(0x3044) = EW(0x3062) = EW(0x3080) = EW(0x309E) = gfxBufPtr;
    EB(0x30EA) = 0;
    writeInstrumentLayoutCells();
}

/* sub_2208E: standalone trampoline into the 22A28 body (view-scale toggle) */
void far applyViewScaleMode(void) {
    writeInstrumentLayoutCells();
}

/* ---- chain entry -----------------------------------------------------------
 * Loads the EGAME dseg image + far cells, then runs the ported session.
 * f19eg_main returns the DOS exit status (chain code) to the dispatcher. */
/* ---- silhouette edge-group slots (sub_21D18/21D2E/21EB0 + sub_2F0CF) --------
 * F-19's insertOutlineEdges feeds the shared scanline-span rasterizer —
 * the same engine the app's drawVectorShape uses (resetScanlineSpans /
 * clipAndRasterizeEdge / flushSpanDirtyRect in eg3drast.c). g_edgeQuad is
 * bound to the app's g_lineX1 block in f19egglobals.h. */
extern int far resetScanlineSpans(void);
extern int far clipAndRasterizeEdge(void);
extern int far flushSpanDirtyRect(void);

void far gfx_setObjAttr(int16 attr)  { gfx_setColor((int)attr); } /* slot 0x21 */
void far beginEdgeGroup(void)        { resetScanlineSpans(); }
void far insertEdge(void)            { clipAndRasterizeEdge(); }
void far endEdgeGroup(void)          { flushSpanDirtyRect(); }

/* BIOS 0x16 keyboard services: cmd 0 = blocking read, scan|ascii packed ->
 * egReadKey (eginput.c — INPUT_MODE_FLIGHT, matching in-flight callers). */
extern int egReadKey(void);
int16 _bios_keybrd(int16 cmd) {
    (void)cmd;
    return (int16)egReadKey();
}

/* int16-arg overload of the ASOUND engine-pitch slot */
extern int FAR CDECL audio_setEnginePitch(int knots, int thrust); /* slot.h */
int far audio_setEnginePitch(int16 knots, int16 thrust) {
    return audio_setEnginePitch((int)knots, (int)thrust);
}

/* ---- runGameLoop (sub_11CD2 -> sub_11CDE) -----------------------------------
 * The shared egsys gameMainLoop drives F-15's sim+render suite on F-15
 * globals; F-19's loop calls the F-19 routines on F-19 cells. Original order:
 * render+hud, the view-mode layout slot, then the sim step; exits on
 * g_commEventFlag (mission end) or the app's quit flag. sub_22092 (the
 * viewMode==0 instrument-layout refresh) is not yet ported; the
 * sub_2F106(g_viewClipBottom) driver slot is handled inside the renderer. */
extern void f19eg_stepFlightModel(void);   /* egflight.c  sub_1215C */
extern void f19eg_updateFrame(void);       /* egframe.c   sub_13DC2 */
extern void f19eg_renderFrame(void);       /* egflight.c  sub_133D9 */
extern void f19eg_renderHudFrame(void);    /* egtacmap.c  sub_17E74 */
extern void timerPump(void);               /* shared/timer.c */
extern void gfx_dacAnimate(void);          /* slot 0x2c present */
extern uint8 g_missionEndedFlag[];         /* app quit flag (window close) */

void f19eg_runGameLoop(void) {
    static int dbg = -1, frames = 0;
    if (dbg < 0) dbg = getenv("F19_DBG") ? 1 : 0;
    for (;;) {
        timerPump();
        f19eg_renderFrame();
        if (dbg && (frames++ & 0x3F) == 0)
            fprintf(stderr, "f19loop vm=%d sorted=%d view=(%ld,%ld,%ld) init=%d pend=%d\n",
                    (int)g_viewMode, (int)g_sortedObjCount,
                    (long)g_ViewX, (long)g_ViewY, (long)g_viewZ,
                    (int)g_initPhase, (int)g_frameSyncPending);
        f19eg_renderHudFrame();
        /* if (g_viewMode == 0) sub_22086(); — instrument layout refresh (TODO) */
        g_frameSyncPending = 1;              /* byte_32DA3 */
        f19eg_stepFlightModel();
        f19eg_updateFrame();
        gfx_dacAnimate();
        if (g_commEventFlag != 0 || g_missionEndedFlag[0] != 0)
            break;
    }
}

int f19eg_main(void);
void f19_egDsegLoad(void);
int f19_egame_main(void) {
    f19_egDsegLoad();
    return f19eg_main();
}

void nullsub_3(void) { }
