/* START.EXE — utility helpers (tmp/linker shared/util.c+miscstub.c, strand.c,
 * stgrid.c, stinkey.c, stinit.c lineage; adapted/verified vs EN binary).
 * Driver slots: far calls into the patched jump table at para 0x1000,
 * offset = 0xAFA + 5*slot (misc 5a..5e, gfx 0..0x59, audio 0x64+). */
#include "f19.h"
#include "f19stvars.h"

#define cbreakHit (*(uint8 *)((uint8 *)f19_stSpace.m_esTable + 2248))
#define g_lineX1 (*(int16 *)((uint8 *)f19_stSpace.m_esTable + 825))
#define g_lineY1 (*(int16 *)((uint8 *)f19_stSpace.m_esTable + 825))
#define f19_worldObjects ((WorldObject *)f19_stSpace.m_f19_worldObjects_B390)
#define g_clipMaxX (*(int16 *)((uint8 *)f19_stSpace.m_esTable + 819))
#define g_clipMaxY (*(int16 *)((uint8 *)f19_stSpace.m_esTable + 819))
extern int16 *g_vpParms;
#define objectActive ((int8 *)f19_stSpace.m_objectActive)
#define tileMarksOn (*(uint8 *)((uint8 *)f19_stSpace.m_f19_flightUnits + 720))
extern int16 mapClipX1;
extern int16 mapClipY1;
#define word_22322 (*(int16 *)((uint8 *)f19_stSpace.m_word_21718 + 3082))
#define byte_20A1A timerCounter   /* shared/timer.c 60 Hz tick — was PIT-ISR cell */
#define pathWpB (*(int16 *)((uint8 *)f19_stSpace.m_f19_targets + 2))
#define pathWpC (*(int16 *)f19_dsegAt(0xB95A))
#define pathWpD (*(int16 *)f19_dsegAt(0xB95C))
extern char str682E[];
extern char str6832[];
extern char str6834[];
extern char str6836[];
extern char str6838[];
#define f19_targets ((int16 *)((uint8 *)f19_stSpace.m_f19_targets + 8))
extern struct BriefTarget briefTargs[];
extern struct MissionKind missionKinds[];

extern int16 f19_rangeApprox(int16, int16);
struct CommData;   /* TU-local opaque view */
struct GameData;
extern struct CommData *commData;
extern struct GameData *gameData;
extern int16 sub_16261(int16 e);
extern void f19_wrapUnitTextFar(void *page, const char *s, int16 a, int16 b, int16 c, int16 d);
#define briefTextP  ((char *)f19_farAt(0x99A))

/* ---- driver-slot callees ---- */
extern void far gfx_setColor(int color);            /* slot 0x21 */
extern void far gfx_resetBlitOffset2(void);           /* slot 0x23 */
extern int16 far gfx_setFont(uint16 ch, uint16 font); /* slot 0x2f */
extern int16 far misc_jump_5a_keybuf(void);           /* slot 0x5a */
extern int16 far misc_jump_5b_getkey(void);           /* slot 0x5b */
extern int16 far misc_jump_5d_readJoy(int16);         /* slot 0x5d */
extern void far misc_jump_5e_clearKeyFlags(void);     /* slot 0x5e */
extern void far audio_jump_6b(void);                  /* slot 0x6b */
extern int16 far gfx_allocPage(int16 page);           /* slot 0x00 */
extern void far gfx_setPageN(uint16 n);               /* slot 0x0e */
void far gfx_setMode13(int16 mono);   /* f19 shim: arg ignored, app takes none */            /* slot 0x3c */
extern int16 far gfx_getModecode(void);               /* slot 0x3f */
extern void sub_140A3(void);                          /* seg000:0x40a3 seedRandom */

/* ---- skeleton callees ---- */
extern int16 f19_allocBuffer(uint16 a);                      /* seg000:0x6828 */
extern void sub_151E8(char far *dst, int16 val, int16 n); /* seg000:0x51e8 far memset */
extern void sub_141A3(void);                            /* seg000:0x41a3 clipper */
extern void sub_18B7E(const char *, int16, int16, int16);      /* seg000:0x8b7e */
extern int16 getch(void);                               /* seg000:0xe200 */
extern void sub_146E3(void);                            /* seg000:0x46e3 restoreCbreak */
extern void sub_1DCAC(int16);                           /* seg000:0xdcac exit */
extern char *f19_formatGridRef(int16, int16, int16);        /* seg000:0x8d98 */
extern void f19_cleanup(void);

/* ---- globals ---- */

struct CommData {                             /* far ptr dseg:0xd066 */
    int8 pad24[0x20];
    int16 f20;                                /* 0x20 — sprite-res sel */
    int8 pad22[0x02];
    int16 setupMono;                          /* 0x24 */
    int8 pad26[0x08];
    int16 fuelEst;                            /* 0x2e */
    int8 pad30[0x08];
    uint16 storeType;                         /* 0x38 — (&storeType)[i] slots */
    int8 pad3A[0x38];
    int16 setupUseJoy;                        /* 0x72 */
    int8 pad74[0x04];
    int16 gfxModeNum;                         /* 0x78 */
};

typedef struct {                          /* f19_worldObjects: stride 0x10 */
    int16 x_coord, y_coord;               /* 0x00, 0x02 */
    int16 pad4;                           /* 0x04 */
    int16 targetFlags;                    /* 0x06 */
    int16 pad8[4];                        /* 0x08 */
} WorldObject;

struct GameData { int8 pad[0x38]; int16 theater; int16 roeIdx; };

/* seg000:0xe29e/0xe2b0 — START's own LCG f19_rand/f19_srand (libc-style duplicates
 * of the CRT pair): 32-bit state (the original keeps it at dseg:0x7a50), MSVC
 * constants.  Verified vs the original via dosunit replay (start_util spec). */
uint32 f19_rngState;

void f19_srand(uint16 seed) {
    f19_rngState = (uint32)seed;
}

int16 f19_rand(void) {
    f19_rngState = f19_rngState * 0x343FDUL + 0x269EC3UL;
    return (int16)(f19_rngState >> 16) & 0x7FFF;
}

/* seg000:0x40ae — (rand() * arg) >> 15 via unsigned 32x32 mul + logical shift */
int16 f19_randMul(uint16 arg) {
    return (int16)(((uint32)(uint16)f19_rand() * (uint32)arg) >> 0xF);
}

/* seg000:0x40a3 — seed the LCG from the BIOS tick counter. */
extern int16 sub_150BA(void);                     /* int 1Ah tick read (asm) */

void f19_seedRng(void) {
    f19_srand(sub_150BA());
}

/* mystrlen (seg000:0x516d) is hand-asm — preloads s into ax pre-loop,
 * ~(s_orig - s_end) tail, zero locals; no C shape produces it. Skeleton. */

void f19_getTimeOfDay(void) {                 /* seg000:0x40c8 */
    g_cntB++;
    g_cntC++;
    g_cntA++;
    g_cntD++;
    audio_jump_6b();
}

/* seg000:0x5ad2 — NOT f19_drawStringCentered: callers push (n, 0) and store the
 * result as a page segment; body is f19_allocBuffer(n) + zero-fill(seg:0, n).
 * i.e. alloc-zeroed-buffer. Map name kept for mzdiff lookup. */
union FarWords { char far *p; struct { uint16 off; uint16 seg; } w; };
int16 f19_drawStringCentered(int16 size, int16 unused) {
    int16 seg;
    union FarWords u;
    seg = f19_allocBuffer(size);
    u.w.seg = seg;
    u.w.off = 0;
    sub_151E8(u.p, 0, size);
    return seg;
}

int16 f19_showMsgWaitKey(const char *msg) {   /* seg000:0x7518 */
    sub_18B7E(msg, 0, 0x60, 0xf);
    return getch();
}

int16 f19_itemDistance(int16 idx1, int16 idx2) {  /* seg000:0x8746 */
    return f19_rangeApprox(f19_worldObjects[idx1].x_coord - f19_worldObjects[idx2].x_coord,
                       f19_worldObjects[idx1].y_coord - f19_worldObjects[idx2].y_coord);
}

char *f19_getItemCoordStr(int16 idx) {        /* seg000:0x8d74 */
    return f19_formatGridRef(f19_worldObjects[idx].x_coord, f19_worldObjects[idx].y_coord,
                         gameData->theater);
}

int16 f19_readInputKey(void) {                /* seg000:0x8c8 */
    int16 key;
    if (commData->setupUseJoy == 1) {
        do {
            if (misc_jump_5a_keybuf() == 0) break;
        } while (misc_jump_5d_readJoy(0) == 0);
        if (misc_jump_5a_keybuf() != 0)
            goto checkKey;              /* key left unassigned — asm reads it */
    }
    key = misc_jump_5b_getkey();
checkKey:
    if (key == 0x1000) {
        f19_cleanup();
        if (cbreakHit != 0)
            sub_146E3();
        sub_1DCAC(0);
    }
    return key; /* goto path: uninit — original reads [bp-2] garbage */
}

void f19_initGraphics(void) {                 /* seg000:0x827 */
    uint8 unused[0xe];
    sub_140A3();                          /* seedRandom */
    gfx_setPageN(0);
    gfx_allocPage(0);
    if (*(uint16 *)f19_farAt(0x98EC) == 0) {
        gfx_setMode13(commData->setupMono);
        *(uint16 *)f19_farAt(0x98EC) = 1;
    }
    commData->gfxModeNum = g_gfxModeNum = gfx_getModecode();
    misc_jump_5e_clearKeyFlags();
}

int16 f19_stringWidth(int16 *page, const uint8 *str) {   /* seg000:0x3e38 */
    const uint8 *l;
    int16 j, n;
    l = str;
    /* callers pass the descriptor as a bare dseg offset or a resolved pointer */
    if ((uintptr_t)page < 0x10000)
        page = (int16 *)(((uint8 *)f19_dsegAt((uintptr_t)page)));
    j = page[6];
    n = 0;
    while (*l != 0)
        n += gfx_setFont(*l++, j);
    return n;
}

void f19_drawLine(int16 x0, int16 y0, int16 x1, int16 y1, int16 color) { /* seg000:0xc083 */
    gfx_setColor(color);
    g_lineX0 = x0;
    g_lineY0 = y0;
    g_lineX1 = x1;
    g_lineY1 = y1;
    sub_141A3();
    gfx_resetBlitOffset2();
}

/* seg000:0xc0b3 f19_drawClippedLineEx — f19_drawLine with a clip window; START's
 * variant stores raw coords and re-states the driver clip bounds. */
extern int   far gfx_calcRowAddr(int a, int b);  /* slot 0x3e */
extern void far gfx_setBlitOffset(int a);          /* slot 0x1a */
extern void far gfx_setOvlVal1(int v);             /* slot 0x40 */
extern void far gfx_setOvlVal2(int v);             /* slot 0x41 */

void f19_drawClippedLineEx(int16 x1, int16 y1, int16 x2, int16 y2,
                       int16 clipL, int16 clipR, int16 clipT, int16 clipB,
                       int16 both) {
    int16 clipH, clipW;
    clipW = clipR - clipL;
    clipH = clipB - clipT;
    gfx_setBlitOffset(gfx_calcRowAddr(clipL, clipT));
    g_clipMaxX = clipW - 1;
    g_clipMaxY = clipH - 1;
    gfx_setOvlVal1(clipH - 1);
    gfx_setOvlVal2(g_clipMaxX);
    gfx_setColor(g_vpParms[2]);
    g_lineX0 = x1;
    g_lineY0 = y1;
    g_lineX1 = x2;
    g_lineY1 = y2;
    sub_141A3();
    gfx_resetBlitOffset2();
    g_clipMaxX = 0x13F;
    g_clipMaxY = 0xC7;
    gfx_setOvlVal1(0xC7);
    gfx_setOvlVal2(g_clipMaxX);
    gfx_setBlitOffset(0);
}

/* seg000:0x3886 — draws a tile's icon sprites while flag bit 0x80 is set;
 * forces obj.f06 = 3 (palette bank) around the blit. */
struct TileEntry {
    int16 pad[0x10];
    int16 x0, y0, x1, y1;              /* +0x20..+0x27 span rect for sub_14622 */
    int16 spr1;                        /* +0x28 sprite handle */
    int16 spr2;                        /* +0x2A optional second sprite */
    int16 f2C, f2E;
    int8  flag;                        /* +0x30 bit 0x80 = visible */
    int8  pad31;
};
extern void far gfx_blitSprite(int16 spr);            /* slot 0x11 */
extern void sub_14622(void *o, int16 x0, int16 y0, int16 x1, int16 y1);
struct ObjF06 { int16 pad[3]; int16 f06; };

void f19_drawTileIcon(struct TileEntry *t, uint16 idx, struct ObjF06 *o) {
    int16 save;
    if (t[idx].flag & 0x80) {
        save = o->f06;
        o->f06 = 3;
        if (flag_29948 == 0) {
            register struct TileEntry *e = &t[idx];
            sub_14622(o, e->x0, e->y0, e->x1, e->y1);
        }
        gfx_blitSprite(t[idx].spr1);
        o->f06 = save;
        if (t[idx].spr2 != 0)
            gfx_blitSprite(t[idx].spr2);
    }
}

/* seg000:0x51b1 mystrchr — hand-asm (push si never used, ch hoisted to ax
 * before loop); skeleton stays. */

/* seg000:0x262c/0x2672/0x2706 — cyclic next/prev selectors over f19_worldObjects */

void f19_selectNextObject(void) {
    int8 again;
    again = 1;
    do {
        objCursor++;
        if (objCursor > objectCount) objCursor = 0;
        if (f19_worldObjects[objCursor].pad4 != 0 || objectActive[objCursor] != 0)
            again = 0;
    } while (again != 0);
}

void f19_selectNextUnit(void) {
    int8 again;
    again = 1;
    do {
        selCursor++;
        if (selCursor > objectCount) selCursor = 0;
        if ((f19_worldObjects[selCursor].targetFlags & 1) != 0 ||
            (f19_worldObjects[selCursor].targetFlags & 0x200) != 0)
            if ((f19_worldObjects[selCursor].targetFlags & 0x800) == 0)
                again = 0;
    } while (again != 0);
}

void f19_selectPrevUnit(void) {
    int8 again;
    again = 1;
    do {
        selCursor--;
        if (selCursor < 0) selCursor = objectCount - 1;
        if ((f19_worldObjects[selCursor].targetFlags & 1) != 0 ||
            (f19_worldObjects[selCursor].targetFlags & 0x200) != 0)
            if ((f19_worldObjects[selCursor].targetFlags & 0x800) == 0)
                again = 0;
    } while (again != 0);
}

void f19_selectPrevObject(void) {
    int8 again;
    again = 1;
    do {
        objCursor--;
        if (objCursor < 0) objCursor = objectCount - 1;
        if (f19_worldObjects[objCursor].pad4 != 0 || objectActive[objCursor] != 0)
            again = 0;
    } while (again != 0);
}

/* seg000:0xbd7e / 0xbe09 — map overlay markers: per-unit blip & 16x16 tile grid.
 * dstX/dstY written via byte offset into the SpriteParams pool at word_20000. */
extern void far gfx_blitSprite(int16 sprOff);
extern int16 f19_mapToScreenX(int16 v);
extern int16 f19_mapToScreenY(int16 v);

void f19_drawUnitMarkers(void) {
    uint16 i;
    if (unitMarksOn == 0) return;
    for (i = 0; i < (uint16)objectCount; i++) {
        if ((f19_worldObjects[i].targetFlags & 1) != 0 ||
            (f19_worldObjects[i].targetFlags & 0x200) != 0)
            if ((f19_worldObjects[i].targetFlags & 0x800) == 0) {
                *(int16 *)(sprParmsTab + unitSprOff + 8) =
                    f19_mapToScreenX(f19_worldObjects[i].x_coord) + mapClipX1 - 2;
                *(int16 *)(sprParmsTab + unitSprOff + 0xA) =
                    f19_mapToScreenY(f19_worldObjects[i].y_coord) + mapClipY1 - 2;
                gfx_blitSprite(unitSprOff);
            }
    }
}

void f19_drawTileMarkers(void) {
    uint16 i, j;
    if (tileMarksOn == 0) return;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            if ((tileMarkMap[i + j * 16] & 0x10) != 0) {
                *(int16 *)(sprParmsTab + gridSprOff + 8) =
                    f19_mapToScreenX(i * 0x7FF) + mapClipX1;
                *(int16 *)(sprParmsTab + gridSprOff + 0xA) =
                    f19_mapToScreenY(j * 0x7FF) + mapClipY1;
                gfx_blitSprite(gridSprOff);
            }
        }
    }
}


/* ==== seg000:0xbc81 f19_drawSiteMarkers — ring+icon for each live site entry:
 * full-circle arc (radius = siteTypeParms[kind]<<6, color 4), then blit one
 * of two sprite slots selected by flags & 8 (store block duplicated per
 * arm; the blit call tail-merges). ==== */
extern void f19_drawMapArc(int16 cx, int16 cy, int16 radius, int16 color,
                       int16 connect, int16 a1, int16 a2);

void f19_drawSiteMarkers(void) {
    uint16 i;
    if (siteMarksOn == 0) return;
    for (i = 0; i < (uint16)siteMarkCount; i++) {
        if (f19_worldObjects[i].pad4 != 0) {
            f19_drawMapArc(f19_worldObjects[i].x_coord, f19_worldObjects[i].y_coord,
                       siteTypeParms[f19_worldObjects[i].pad4 * 9] << 6,
                       4, 1, 0, 0x100);
            if (f19_worldObjects[i].targetFlags & 8) {
                *(int16 *)(sprParmsTab + siteSprOff2 + 8) =
                    f19_mapToScreenX(f19_worldObjects[i].x_coord) + mapClipX1 - 2;
                *(int16 *)(sprParmsTab + siteSprOff2 + 0xA) =
                    f19_mapToScreenY(f19_worldObjects[i].y_coord) + mapClipY1 - 2;
                gfx_blitSprite(siteSprOff2);
            } else {
                *(int16 *)(sprParmsTab + siteSprOff1 + 8) =
                    f19_mapToScreenX(f19_worldObjects[i].x_coord) + mapClipX1 - 2;
                *(int16 *)(sprParmsTab + siteSprOff1 + 0xA) =
                    f19_mapToScreenY(f19_worldObjects[i].y_coord) + mapClipY1 - 2;
                gfx_blitSprite(siteSprOff1);
            }
        }
    }
}


/* ==== seg000:0xbac2 f19_drawThreatRings — per-object range ring + icon on the
 * tactical map overlay. Skips dead/low-activity objects; ring radius comes
 * from ringTypes[pad4] (mode 1 scales f0 by f1/16), color by the type flag
 * and objectActive level. The blit arm duplicates the store block like
 * f19_drawSiteMarkers. ==== */
typedef struct { int16 f0, f1; uint8 flag; uint8 padT[9]; } RingType; /* 0xE */

void f19_drawThreatRings(void) {
    int16 c;
    uint16 i;
    if (ringMode == 0) return;
    for (i = 0; i < (uint16)objectCount; i++) {
        if (f19_worldObjects[i].pad4 == 0 && (uint8)objectActive[i] <= 1) continue;
        if (ringTypes[f19_worldObjects[i].pad4].flag & 1) {
            if ((uint8)objectActive[i] > 1) c = 0xF;
            else c = 1;
            if (ringMode == 1)
                f19_drawMapArc(f19_worldObjects[i].x_coord, f19_worldObjects[i].y_coord,
                           (ringTypes[f19_worldObjects[i].pad4].f0 *
                            ringTypes[f19_worldObjects[i].pad4].f1) / 16 << 6,
                           c, 1, 0, 0x100);
            else
                f19_drawMapArc(f19_worldObjects[i].x_coord, f19_worldObjects[i].y_coord,
                           ringTypes[f19_worldObjects[i].pad4].f0 << 6,
                           c, 1, 0, 0x100);
        } else {
            if ((uint8)objectActive[i] > 1) c = 0xF;
            else c = 0;
            if (ringMode == 1)
                f19_drawMapArc(f19_worldObjects[i].x_coord, f19_worldObjects[i].y_coord,
                           (ringTypes[f19_worldObjects[i].pad4].f0 *
                            ringTypes[f19_worldObjects[i].pad4].f1) / 16 << 6,
                           c, 0, 0, 0x100);
            else
                f19_drawMapArc(f19_worldObjects[i].x_coord, f19_worldObjects[i].y_coord,
                           ringTypes[f19_worldObjects[i].pad4].f0 << 6,
                           c, 0, 0, 0x100);
        }
        if (f19_worldObjects[i].targetFlags & 8) {
            *(int16 *)(sprParmsTab + siteSprOff2 + 8) =
                f19_mapToScreenX(f19_worldObjects[i].x_coord) + mapClipX1 - 2;
            *(int16 *)(sprParmsTab + siteSprOff2 + 0xA) =
                f19_mapToScreenY(f19_worldObjects[i].y_coord) + mapClipY1 - 2;
            gfx_blitSprite(siteSprOff2);
        } else {
            *(int16 *)(sprParmsTab + siteSprOff1 + 8) =
                f19_mapToScreenX(f19_worldObjects[i].x_coord) + mapClipX1 - 2;
            *(int16 *)(sprParmsTab + siteSprOff1 + 0xA) =
                f19_mapToScreenY(f19_worldObjects[i].y_coord) + mapClipY1 - 2;
            gfx_blitSprite(siteSprOff1);
        }
    }
}


/* seg000:0x25ea — RTC sync when enabled: int 0x1a read, stash tick, then 10
 * settle ticks via sub_16208 */
extern void  sub_167FD(void);
extern void  sub_16208(void);
extern void  intDispatch(int16 n, uint8 *a, uint8 *b);

void f19_rtcSync(void) {
    uint16 i;
    if (rtcEnabled == 1) {
        sub_167FD();
        rtcFlagByte = 0;
        intDispatch(0x1a, (uint8*)(((uint8 *)f19_dsegAt(0x3e1e))), (uint8*)(((uint8 *)f19_dsegAt(0x3e1e))));
        rtcTickSaved = rtcTickBuf;
        sub_16208();
        i = 0;
        do {
            i++;
            sub_16208();
        } while (i < 0xa);
    }
}

/* seg000:0x669f — script expression evaluator: walks *pp (a near cursor the
 * routine advances) at fixed index; ')'→0, '|'→1, ':N'→N-1, '('→skip to the
 * matching ')'.  Called by the briefing-choice interpreter sub_16261. */
int16 f19_evalChoiceExpr(uint16 *cellp, int16 idx) {
#define pp_deref(i) (((uint8 *)f19_dsegAt(*cellp)))[i]

    int8  a, uz;                  /* ch -> [bp-2], digit ch -> [bp-0a] */
    int16 f, i, res;              /* n -> [bp-4], paren depth -> [bp-6] */
    res = 0;
    for (;;) {
        a = pp_deref(idx);
        (*cellp)++;
        if (a == 0x29) return 0;
        if (a == 0x7C) return 1;
        if (a == 0x3A) {
            f = 0;
            goto t;
b:          if (uz > 0x39) goto r;
            f = f * 10 + uz - 0x30;
            (*cellp)++;
t:          uz = pp_deref(idx);
            if (uz >= 0x30) goto b;
r:          return f - 1;
        }
        if (a == 0x28) {
            i = 1;
            do {
                i += (pp_deref(idx) == 0x28) ? 1 : 0;
                i -= (pp_deref(idx) == 0x29) ? 1 : 0;
                (*cellp)++;
            } while (i > 0);
            continue;
        }
    }
}


/* seg000:0x6763 — marks clipTable[i].flag = 0 for entries whose rect
 * intersects the (x,y,w,h) window translated by the view origin.
 * The x0 else-arm reads clipTable[iu].x0 (not e->x0): that keeps MSC
 * from binding e->x0 to si, so the dispatch emits cmp [bx],ax plus a
 * plain [bx] reload and the second-written arm sinks to body top. */
struct ClipEntry {
    int16 x0, y0, w, h;           /* +0,+2,+4,+6 */
    int8  f8, f9;                 /* +8,+9 anim phase step/accum (sub_16208) */
    uint8 col, row;               /* +0x0a,+0x0b cell cursor */
    uint8 grp;                    /* +0x0c saved group index */
    int8  timer[10];              /* +0x0d per-group countdown */
    uint8 save[11];               /* +0x17 per-group saved cursor */
    int16 sprX[18];               /* +0x22 per-field sprite X */
    uint8 sprY[18];               /* +0x46 per-field sprite Y/icon */
    uint16 strOff;                /* +0x58 DSL string offset into dseg */
    int8  flag;                   /* +0x5a */
};

void f19_clipEntries(int16 x, int16 y, int16 w, int16 h) {
    struct ClipEntry *e;
    int16 f, i, m, iu, uz;
    x -= viewOriginX;
    y -= viewOriginY;
    for (iu = 0; iu < clipEntryCount; iu++) {
        e = &clipTable[iu];
        if (e->x0 > x) uz = clipTable[iu].x0; else uz = x;
        if (e->y0 > y) m = e->y0; else m = y;
        i = (e->x0 + e->w <= x + w) ? e->x0 + e->w : x + w;
        f = (e->y0 + e->h <= y + h) ? e->y0 + e->h : y + h;
        if (uz < i && m < f)
            e->flag = 0;
    }
}

/* ==== seg000:0xbf03 f19_drawMapArc — angle-swept arc/ring on the map.
 * /Os module: under /Ot the step-size ternary emits a stray relax-pad nop.
 * Local slots: q@-2, x@-4, i@-6, j(prevX)@-8, k(y)@-A, l(step)@-C,
 * m(spare)@-E, n(prevY)@-0x10 — names chosen for the hash buckets. ==== */
extern int16 f19_sinMul(int16 angle, int16 value);
extern int16 f19_cosMul(int16 angle, int16 value);
extern void  f19_plotMapPoint(int16 x, int16 y, int16 color, int16 unused);
extern void  f19_drawMapLine(int16 x1, int16 y1, int16 x2, int16 y2);

void f19_drawMapArc(int16 cx, int16 cy, int16 radius, int16 color,
                int16 connect, int16 a1, int16 a2) {
    int16 q, x, i, j, k, l, m, n;

    if (a2 < a1)
        a1 += 0x100;
    g_vpParms[2] = color;
    l = connect ? 8 : 0x10;
    if (!connect && radius >= 0xBB8)
        l = 8;
    if (!connect && radius >= 0x1B58)
        l = 4;
    i = a1;
    goto test;
body:
    q = i << 8;
    x = cx + f19_sinMul(q, radius);
    k = cy - f19_cosMul(q, radius);
    if ((uint16)x > 0xC000)
        x = 0;
    if ((uint16)k > 0xC000)
        k = 0;
    if (x && k && j && n) {
        if (i != a1 && connect)
            f19_drawMapLine(x, k, j, n);
        else
            f19_plotMapPoint(x, k, color, 0);
    }
    j = x;
    n = k;
    i += l;
test:
    if (i <= a2)
        goto body;
}

/* seg000:0x47e8 — thin CDECL wrapper over the int21h raw-read routine
 * (seg000:0x48fe: bx=handle, cx=count, dx=buf). */
extern int16 sub_148FE(int16 handle, int16 count, int16 buf);

int16 f19_dosRead(int16 handle, int16 count, int16 buf) {
    return sub_148FE(handle, count, buf);
}

/* seg000:0x5b02 — advances the scratch-buffer cursor word_22322 by n after
 * handing (pos, n) to sub_15B22; returns the pre-advance position. */
extern int16 sub_15B22(int16 pos, int16 n);
int16 f19_advanceBufPos(int16 n) {
    sub_15B22(word_22322, n);
    word_22322 += n;
    return word_22322 - n;
}

/* seg000:0x61cc — stores the view origin then redraws via f19_sub_15B68. */
extern int16 f19_sub_15B68(int16 flag);

int16 f19_setViewOrigin(int16 a, int16 b, int16 flag) {
    viewOriginX = b;
    viewOriginY = a;
    if (f19_sub_15B68(flag) != 0)
        return 1;
    return 0;
}

/* seg000:0x61f1 — tick gate: on every 7th tick resets byte_20A1B and runs
 * sub_16208 once when word_22324 is armed. */
extern void sub_16208(void);

void f19_sub_161F1(void) {
    if (byte_20A1B > 6) {
        byte_20A1B = 0;
        if (word_22324 != 0)
            sub_16208();
    }
}

/* seg000:0x4089 — waits n RTC ticks: arms the tick counter byte_20A1A via
 * sub_14E9C (PIT/vector install), spins until it reaches n, then restores
 * via sub_14EDA. */
extern void sub_14E9C(void);
extern void sub_14EDA(void);
void f19_delayTicks(int16 n) {
    byte_20A1A = 0;
    sub_14E9C();
    while (n >= (uint8)byte_20A1A)
        timerYield();
    sub_14EDA();
}

/* seg000:0x5b22 — drains n bytes from the 0x200 file buffer at dseg:0x12c2;
 * refills via sub_16C0E and resets the read pos when it passes 0x1ff. */
extern void sub_16C0E(void);
int16 f19_bufReadBytes(int8 *dst, int16 n) {
    int16 c;
    for (c = 0; c < n; c++) {
        if (word_21714 > 0x1FF) {
            sub_16C0E();
            word_21714 = 0;
        }
        *dst++ = byte_212C2[word_21714++];
    }
    return c;
}

/* seg000:0x67fd — clears byte +9 on 30 entries of the 0x5c-stride table
 * at dseg:0x2326. */
void f19_resetTableFlags(void) {
    int16 i;
    int16 e;
    for (i = 0; i < 0x1E; i++) {
        e = 0x2326 + i * 0x5C;
        ((int8 *)e)[9] = 0;
    }
}

/* seg000:0x5414 — same 0x200-byte file-buffer drain as f19_bufReadBytes, but
 * refills via sub_149A1(handle) — used while reading a specific file. */
extern void sub_149A1(int16 h);

int16 f19_bufReadFile(uint8 *dst, int16 n, int16 h) {
    int16 c;
    for (c = 0; c < n; c++) {
        if (word_21714 > 0x1FF) {
            sub_149A1(h);
            word_21714 = 0;
        }
        *dst++ = byte_212C2[word_21714++];
    }
    return c;
}

/* seg000:0x6208 — per-tick effect table walk (0x5c-stride, word_2367C
 * entries): if flag +0x5a set, f09 += f08; on wrap past 0xff fires
 * f19_stepPanelAnim(entry) and stores the wrapped byte. */
extern void f19_stepPanelAnim(struct ClipEntry *e);
struct TickEnt { char _p[8]; uint8 f08, f09; char _q[0x52]; int8 f5A; };

void f19_tickEffectTable(void) {
    int16 i;
    int16 e;
    int16 t;
    for (i = 0; i < word_2367C; i++) {
        e = 0x2326 + i * 0x5C;
        if (((struct TickEnt *)e)->f5A != 0) {
            t = ((struct TickEnt *)e)->f09 + ((struct TickEnt *)e)->f08;
            if (t > 0xFF) {
                t -= 0x100;
                sub_16261(e);
            }
            ((struct TickEnt *)e)->f09 = t;
        }
    }
}

/* seg000:0x68fd — dispatch to overlay draw proc selected by byte_2B83E (0-3).
 * pa/pd are object pointers dereferenced for the call's w1/w4 args. */
extern void far ovl_47B(int16 w1, int16 w2, int16 w3, int16 w4,
                        int16 w5, int16 w6, int16 w7, int16 w8);
extern void far ovl_766(int16 w1, int16 w2, int16 w3, int16 w4,
                        int16 w5, int16 w6, int16 w7, int16 w8);
extern void far ovl_169(int16 w1, int16 w2, int16 w3, int16 w4,
                        int16 w5, int16 w6, int16 w7, int16 w8);
extern void far ovl_A65(int16 w1, int16 w2, int16 w3, int16 w4,
                        int16 w5, int16 w6, int16 w7, int16 w8);

void f19_dispatchDrawMode(int16 *pa, int16 b, int16 c, int16 *pd,
                    int16 e, int16 f, int16 g, int16 h) {
    if (g == 0) return;
    if (h == 0) return;
    switch (drawModeSel) {
    case 0: ovl_47B(*pa, b, c, *pd, e, f, g, h); break;
    case 1: ovl_766(*pa, b, c, *pd, e, f, g, h); break;
    case 2: ovl_169(*pa, b, c, *pd, e, f, g, h); break;
    case 3: ovl_A65(*pa, b, c, *pd, e, f, g, h); break;
    }
}

/* seg000:0x3d15 — word-wrap renderer for near strings: measures text char by
 * char via the gfx setFont (char-width) slot, wraps at space/CR/LF/hyphen,
 * copies each line to a stack buffer and draws it through the drawString slot.
 * `o` is a byte offset into the SpriteParams pool (sprParmsTab): +8/+A are the
 * draw position fields, +C holds the current font id (saved into `a`). */
extern void far gfx_drawString(int16 o, char *s);   /* slot 0x05 */
extern void     sub_151FE(char *d, uint8 *s, int16 n); /* near copy */
extern void     sub_1521C(char *d, char far *s, int16 n); /* far copy */

void f19_wrapUnitText(int16 o, char *s, uint16 w, int16 x, int16 y, int16 dy) {
    int16 h;
    int16 a, d, e, i;
    uint8 *c;
    uint8 *b, *f;
    int8  g;
    char  buf[0x3E6], n[2];

    f = (uint8 *)s; b = (uint8 *)s; c = (uint8 *)s;
    a = *(int16 *)(sprParmsTab + o + 0xC);
    *(int16 *)(sprParmsTab + o + 0xA) = y;
    g = 1;
    for (;;) {
        h = d = 0;
        while (h < w) {
            n[0] = *c;
            if (n[0] == 0 || n[0] == 0x0D || n[0] == 0x0A) goto disp;
            h += gfx_setFont(*c++, a);
            d++;
        }
disp:   if (h >= w) goto b1;
        goto b0;
        do {
chk:        if (n[0] == 0 || n[0] == 0x0D || n[0] == 0x0A || n[0] == '-')
                goto join;
            if (c <= f) goto join;
b1:         c--;
            d--;
b0:         n[0] = *c;
        } while (n[0] != ' ');
join:
        if (*c == '-') d++;
        if (*c == 0) g = 0;
        if (d != 0) {
            sub_151FE(buf, b, d);
            buf[d] = 0;
            *(int16 *)(sprParmsTab + o + 8) = x;
            gfx_drawString(o, buf);
            *(int16 *)(sprParmsTab + o + 0xA) += dy;
            if (*c == 0x0D) *(int16 *)(sprParmsTab + o + 0xA) += 2;
        }
        c++;
        b = c;
        if (!g) break;
    }
}

/* seg000:0x3bc4 — far-string variant of f19_wrapUnitText. Same wrap loop, but the
 * source is a far pointer (es: derefs), the line copy goes through sub_1521C,
 * and the next line start skips leading spaces (`while (*b == ' ') b++`).
 * Locals: e/v are spare slots the original frame reserved between d and f. */
void f19_wrapUnitTextFar(int16 o, char far *s, uint16 w, int16 x, int16 y, int16 dy) {
    int16 h;
    int16 a, d;
    char far *b;
    char far *c;
    int16 e;
    char far *f;
    int16 v;
    int8  g;
    char  buf[0x1F4], n[2];

    f = s; b = s; c = s;
    a = *(int16 *)(sprParmsTab + o + 0xC);
    *(int16 *)(sprParmsTab + o + 0xA) = y;
    g = 1;
    for (;;) {
        h = d = 0;
        while (h < w) {
            n[0] = *c;
            if (n[0] == 0 || n[0] == 0x0D || n[0] == 0x0A) goto disp;
            h += gfx_setFont(*c++, a);
            d++;
        }
disp:   if (h >= w) goto b1;
        goto b0;
        do {
chk:        if (n[0] == 0 || n[0] == 0x0D || n[0] == 0x0A || n[0] == '-')
                goto join;
            if (c <= f) goto join;
b1:         c--;
            d--;
b0:         n[0] = *c;
        } while (n[0] != ' ');
join:
        if (*c == '-') d++;
        while (*b == ' ') b++;
        if (*c == 0) g = 0;
        if (d != 0) {
            sub_1521C(buf, b, d);
            buf[d] = 0;
            *(int16 *)(sprParmsTab + o + 8) = x;
            gfx_drawString(o, buf);
            *(int16 *)(sprParmsTab + o + 0xA) += dy;
            if (*c == 0x0D) *(int16 *)(sprParmsTab + o + 0xA) += 2;
        }
        c++;
        b = c;
        if (!g) break;
    }
}

/* seg000:0xb8e7 — route-path overlay on the tactical map: polyline through the
 * four route waypoint indices (f19_worldObjects entries) plus waypoint labels.
 * Endpoints share one label when the path is closed (A==D). Called back-to-back
 * with drawRouteFill (sub_1BAC2) from the map orchestrator. No stack frame. */
extern void  sub_13B76(void *o, char *s, int16 x, int16 y); /* drawObjString */

void f19_drawRoutePath(void) {
    g_vpParms[2] = 0;
    f19_plotMapPoint(f19_worldObjects[pathWpA].x_coord, f19_worldObjects[pathWpA].y_coord,
                 0xF, 0);
    f19_drawMapLine(f19_worldObjects[pathWpA].x_coord, f19_worldObjects[pathWpA].y_coord,
                f19_worldObjects[pathWpB].x_coord, f19_worldObjects[pathWpB].y_coord);
    f19_drawMapLine(f19_worldObjects[pathWpB].x_coord, f19_worldObjects[pathWpB].y_coord,
                f19_worldObjects[pathWpC].x_coord, f19_worldObjects[pathWpC].y_coord);
    f19_drawMapLine(f19_worldObjects[pathWpC].x_coord, f19_worldObjects[pathWpC].y_coord,
                f19_worldObjects[pathWpD].x_coord, f19_worldObjects[pathWpD].y_coord);
    g_vpParms[2] = 1;
    if (pathWpA == pathWpD)
        sub_13B76(g_vpParms, str682E,
                  f19_mapToScreenX(f19_worldObjects[pathWpA].x_coord) + mapClipX1,
                  f19_mapToScreenY(f19_worldObjects[pathWpA].y_coord - 2) + mapClipY1);
    else {
        sub_13B76(g_vpParms, str6832,
                  f19_mapToScreenX(f19_worldObjects[pathWpA].x_coord) + mapClipX1,
                  f19_mapToScreenY(f19_worldObjects[pathWpA].y_coord - 2) + mapClipY1);
        sub_13B76(g_vpParms, str6834,
                  f19_mapToScreenX(f19_worldObjects[pathWpD].x_coord) + mapClipX1,
                  f19_mapToScreenY(f19_worldObjects[pathWpD].y_coord - 2) + mapClipY1);
    }
    sub_13B76(g_vpParms, str6836,
              f19_mapToScreenX(f19_worldObjects[pathWpB].x_coord) + mapClipX1,
              f19_mapToScreenY(f19_worldObjects[pathWpB].y_coord - 2) + mapClipY1);
    sub_13B76(g_vpParms, str6838,
              f19_mapToScreenX(f19_worldObjects[pathWpC].x_coord) + mapClipX1,
              f19_mapToScreenY(f19_worldObjects[pathWpC].y_coord - 2) + mapClipY1);
}


/* ==== seg000:0xc1a8 f19_printMission — the mission-briefing screen. Loads
 * briefing.txt into an alloc'd block, draws the frame + CLASSIFIED banner +
 * title, then either the objectives page (briefPage==1: PRIMARY/SECONDARY
 * mission headers, mission-type numbers, wrapped objective text via
 * sub_17558 + f19_printObjective) or the flight-plan page (FLIGHT PLAN header,
 * TAKEOFF/RETURN fields built from waypoint name/coord lookups, FUEL
 * ESTIMATE, MISSION BEGINS AT, RULES OF ENGAGEMENT wrapped far text).
 * Locals a/e/f/h are color/CR escape strings; only a is actually read. ==== */
extern void   f19_sub_108B7(void);                  /* seg000:0x08b7 misc jump    */
extern void   f19_printObjective(uint16 n);               /* seg000:0xc699 obj detail   */
extern char  *sub_17558(int16 n, char *b, int16 t); /* 0x7558 briefing line  */
extern void   far gfx_commitPage(void);         /* far driver slot 0x50      */
extern void   my_itoa(int16 n, char *b);        /* seg000:0x3fb3             */
extern void   f19_freeBuffer(uint16 s);             /* seg000:0x685c stalloc.c   */
extern int16  resFileReadBlock(const char *p, int16 a, int16 b); /* 0x4746    */

void f19_printMission(void) {
    char   a[2];                 /* "\r" line terminator appended into scrStr */
    int16  b, c, d;
    char   e[3];                 /* {9,10,0} — init but unread                */
    char   f[2];                 /* {0x80,0} — init but unread               */
    char   g[0x10];              /* coord/name scratch (my_itoa)              */
    char   h[2];                 /* {0x8E,0} — init but unread               */
    uint16 i;                    /* f19_stringWidth result — unsigned: >>1 = shr  */

    f19_sub_108B7();
    a[0] = 0x0D; a[1] = 0;
    e[0] = 9; e[1] = 0x0A; e[2] = 0;
    h[0] = 0x8E; h[1] = 0;
    f[0] = 0x80; f[1] = 0;
    briefTab = 0x99E;
    c = b = f19_allocBuffer(0x2328);
    d = 0;
    resFileReadBlock("briefing.txt", d += 0, b);   /* +=0: force slot reload */
    flag_29948 = 0;
    sub_14622((void *)briefParms, 8, 0, 0x13F, 0xB1);
    gfx_commitPage();
    *(int16 *)(sprParmsTab + titleParms + 4) = 0xC;
    sub_13B76((int16 *)titleParms, "CLASSIFIED                        CLASSIFIED", 0x1E, 5);
    *(int16 *)(sprParmsTab + titleParms + 4) = 0;
    sub_13B76((int16 *)titleParms, "Mission Briefing", 0x6F, 5);
    briefActive = 1;
    if (briefPage == 1) {
        mystrcpy(scrStr, "\x89PRIMARY MISSION ");
        my_itoa(f19_targets[0], g);
        mystrcat(scrStr, g);
        i = f19_stringWidth((int16 *)briefParms, (uint8 *)scrStr);
        sub_13B76((int16 *)briefParms, scrStr, (0x140 - i) >> 1, 0x14);
        *(int16 *)(sprParmsTab + briefParms + 4) = 0;
        f19_wrapUnitText(briefParms, sub_17558(f19_targets[0], scrStr, b),
                     0x12C, 0x0A, 0x1E, 8);
        mystrcpy(scrStr, "Your \x89primary\x80 objective is ");
        f19_printObjective(0);
        f19_wrapUnitText(briefParms, scrStr, 0x12C, 0x0A,
                     *(int16 *)(sprParmsTab + briefParms + 0xA), 8);
        mystrcpy(scrStr, "\x89SECONDARY MISSION ");
        my_itoa(f19_targets[9], g);
        mystrcat(scrStr, g);
        i = f19_stringWidth((int16 *)briefParms, (uint8 *)scrStr);
        sub_13B76((int16 *)briefParms, scrStr, (0x140 - i) >> 1,
                  *(int16 *)(sprParmsTab + briefParms + 0xA) + 8);
        *(int16 *)(sprParmsTab + briefParms + 4) = 0;
        f19_wrapUnitText(briefParms, sub_17558(f19_targets[9], scrStr, b),
                     0x12C, 0x0A,
                     *(int16 *)(sprParmsTab + briefParms + 0xA) + 8, 8);
        mystrcpy(scrStr, "Your \x89secondary\x80 objective is ");
        f19_printObjective(1);
        f19_wrapUnitText(briefParms, scrStr, 0x12C, 0x0A,
                     *(int16 *)(sprParmsTab + briefParms + 0xA), 8);
    } else {
        mystrcpy(scrStr, "\x89FLIGHT PLAN");
        i = f19_stringWidth((int16 *)briefParms, (uint8 *)scrStr);
        sub_13B76((int16 *)briefParms, scrStr, (0x140 - i) >> 1, 0x14);
        mystrcpy(scrStr, "\x89TAKEOFF:\x80 You will depart from ");
        {
            register int16 v;    /* si: site unitRef, register-held         */
            v = *(int16 *)(siteNameData + (pathWpA << 4));
            mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[v ? v : siteObjData[pathWpA << 4]]))));
        }
        mystrcat(scrStr, ", ONC ");
        mystrcat(scrStr, f19_getItemCoordStr(pathWpA));
        mystrcat(scrStr, a);
        f19_wrapUnitText(briefParms, scrStr, 0x12C, 0x0A, 0x1E, 8);
        f19_wrapUnitTextFar(briefParms, briefTextP, 0x12C, 0x0A,
                        *(int16 *)(sprParmsTab + briefParms + 0xA), 8);
        mystrcpy(scrStr, "\x89RETURN:\x80 You are scheduled to land at ");
        {
            register int16 v;
            v = *(int16 *)(siteNameData + (pathWpD << 4));
            mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[v ? v : siteObjData[pathWpD << 4]]))));
        }
        mystrcat(scrStr, ", ONC ");
        mystrcat(scrStr, f19_getItemCoordStr(pathWpD));
        mystrcat(scrStr, a);
        mystrcat(scrStr, "\x89FUEL ESTIMATE: \x80");
        my_itoa(commData->fuelEst, g);
        mystrcat(scrStr, g);
        mystrcat(scrStr, " lbs.");
        mystrcat(scrStr, a);
        mystrcat(scrStr, "\x89MISSION BEGINS AT: \x80");
        mystrcat(scrStr, "00:00");
        f19_wrapUnitText(briefParms, scrStr, 0x12C, 0x0A,
                     *(int16 *)(sprParmsTab + briefParms + 0xA) + 2, 8);
        mystrcpy(scrStr, "\x89RULES OF ENGAGEMENT");
        i = f19_stringWidth((int16 *)briefParms, (uint8 *)scrStr);
        sub_13B76((int16 *)briefParms, scrStr, (0x140 - i) >> 1,
                  *(int16 *)(sprParmsTab + briefParms + 0xA) + 8);
        *(int16 *)(sprParmsTab + briefParms + 4) = 0;
        f19_wrapUnitTextFar(briefParms, (char *)f19_farAt(briefTab + 4 * (gameData->roeIdx)), 0x12C, 0x0A,
                        *(int16 *)(sprParmsTab + briefParms + 0xA) + 8, 8);
    }
    *(int16 *)(sprParmsTab + briefParms + 4) = 9;
    sub_13B76((int16 *)briefParms, "Press Selector to continue", 0x64, 0xB2);
    gfx_commitPage();
    f19_readInputKey();
    f19_freeBuffer(b);
    gamePhase = 4;
}



/* -------------------------------------------------------------------------
 * f19_printObjective (seg000:0xc699) — append the detail sentence for briefing
 * objective n to scrStr.  Eight mission kinds dispatched on
 * missionKinds[briefTargs[n].missionNum].kind; each case is a chain of
 * mystrcat() appends of literals plus generator-filled time/coord buffers.
 * ------------------------------------------------------------------------- */
struct BriefTarget {                        /* dseg:0xb946, stride 0x12       */
    int16 missionType;                      /* +0                             */
    int16 targetIdx;                        /* +2  siteRec index (<<4)        */
    int16 baseIdx;                          /* +4                             */
    int16 missionCode;                      /* +6                             */
    int16 missionNum;                       /* +8  x0xC into missionKinds[]   */
    char  coord[6];                         /* +A  ONC grid ref               */
    int16 distance;                         /* +10                            */
};
struct MissionKind {                        /* dseg:0x4b10, stride 0x0C       */
    int16 kind;                             /* +0  text selector 1..8         */
    int16 pad2;                             /* +2                             */
    uint8 flags;                            /* +4  0x20/0x02 deadline flags   */
    int8  pad5;                             /* +5                             */
    int16 status;                           /* +6  == -2 -> patrol suffix     */
    int16 pad8[2];                          /* +8,+A                          */
};
extern int16  mystrlen(const char *s);            /* seg000:0x516d hand-asm strlen  */

void f19_printObjective(uint16 n) {
    int16 a, b, c, d, e;                    /* dead locals: 0xA frame pad     */
    switch (missionKinds[briefTargs[n].missionNum].kind) {
    case 1:
        mystrcat(scrStr, "to \x89photograph the ");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[siteObjData[briefTargs[n].targetIdx << 4]]))));
        if (mystrlen((char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))))) {
            mystrcat(scrStr, " at ");
            mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))));
        }
        mystrcat(scrStr, "\x80, ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        if (missionKinds[briefTargs[n].missionNum].flags & 0x20) {
            mystrcat(scrStr, ", \x89before ");
            mystrcat(scrStr, briefTimeB);
            mystrcat(scrStr, " hours\x80, while the cargo is still being unloaded.");
        } else
            mystrcat(scrStr, ".");
        break;
    case 2:
        mystrcat(scrStr, "to \x89destroy the ");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[siteObjData[briefTargs[n].targetIdx << 4]]))));
        if (mystrlen((char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))))) {
            mystrcat(scrStr, " at ");
            mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))));
        }
        mystrcat(scrStr, "\x80, ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        if (missionKinds[briefTargs[n].missionNum].flags & 0x20) {
            mystrcat(scrStr, ", \x89before ");
            mystrcat(scrStr, briefTimeB);
            mystrcat(scrStr, " hours\x80, while the cargo is still being unloaded.");
        } else if (missionKinds[briefTargs[n].missionNum].flags & 2) {
            mystrcat(scrStr, ", \x89before ");
            mystrcat(scrStr, briefTimeB);
            mystrcat(scrStr, " hours\x80.");
        } else
            mystrcat(scrStr, ".");
        break;
    case 3:
        mystrcat(scrStr, "to \x89reach the beacon\x80 at ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        mystrcat(scrStr, " and \x89drop the supplies before ");
        mystrcat(scrStr, briefTimeB);
        mystrcat(scrStr, " hours\x80.  To minimize the chance of enemy detection, ");
        mystrcat(scrStr, "the beacon will be on ONLY during this period.");
        break;
    case 4:
        mystrcat(scrStr, "to \x89reach the secret airstrip\x80 at ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        mystrcat(scrStr, " \x89before ");
        mystrcat(scrStr, briefTimeB);
        mystrcat(scrStr, " hours\x80.  The airstrip will be lighted ONLY during this period.");
        break;
    case 5:
        mystrcat(scrStr, "to \x89intercept and destroy\x80 the \x89AN-72 Coaler transport\x80 departing from ");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefDepartSite << 4))]))));
        mystrcat(scrStr, " airbase, ONC ");
        mystrcat(scrStr, briefCoord2);
        mystrcat(scrStr, ", at ");
        mystrcat(scrStr, briefTimeA);
        mystrcat(scrStr, " hours.  It is expected to arrive at ");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))));
        mystrcat(scrStr, " airbase, ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        mystrcat(scrStr, ", at ");
        mystrcat(scrStr, briefTimeB);
        mystrcat(scrStr, " hours.");
        break;
    case 7:
        mystrcat(scrStr, "to \x89intercept and destroy\x80 the \x89AN-72 Coaler transport\x80 leaving from ");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))));
        mystrcat(scrStr, " airbase, ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        mystrcat(scrStr, ", at ");
        mystrcat(scrStr, briefTimeA);
        mystrcat(scrStr, " hours.  The ultimate destination is unknown.");
        break;
    case 6:
        mystrcat(scrStr, "to \x89intercept and destroy the Tu-95\x80, last seen in grid ONC ");
        mystrcat(scrStr, briefCoord2);
        mystrcat(scrStr, " at ");
        mystrcat(scrStr, briefTimeA);
        mystrcat(scrStr, " hours.  Based on past experience it is probably heading for");
        mystrcat(scrStr, (char *)(((uint8 *)f19_dsegAt(wldNameTab[*(int16 *)(siteNameData + (briefTargs[n].targetIdx << 4))]))));
        mystrcat(scrStr, " airbase.  If so, its estimated arrival time is ");
        mystrcat(scrStr, briefTimeB);
        mystrcat(scrStr, " hours.");
        break;
    case 8:
        mystrcat(scrStr, "to \x89intercept and destroy the ");
        mystrcat(scrStr, unitNameTab[briefPatrolType]);
        if (missionKinds[briefTargs[n].missionNum].status == -2)
            mystrcat(scrStr, " fighter patrol");
        mystrcat(scrStr, "\x80 at, ONC ");
        mystrcat(scrStr, briefTargs[n].coord);
        mystrcat(scrStr, ".  ");
        break;
    }
}

/* ==== seg000:0xd97a — f19_drawStoreIcons: loads the arming.spr / f19.spr sheets
 * and blits the four weapon-station icons into the sprite page.  A station
 * type of 0x13 paints the empty-pylon icon 0xA5; anything else indexes the
 * 6-column icon sheet at ((t%6)*0x33+1, (t/6)*0x23+2).  The first arm draws
 * straight to the display page; the other allocates an offscreen page and
 * redirects the blitter there.  f19_loadSpriteScaled is invoked with only two
 * args — its callee reads a third it never receives (the original TU's
 * extern decl had two params). ==== */
extern int16 far gfx_getVal(void);                        /* slot 0x4e */
extern void  far gfx_setDac(uint16 pal);                   /* slot 0x44 */
extern void  far gfx_setFadeSteps(int steps);           /* slot 0x3d */
extern void  far gfx_storeBufPtr(int16 p, int16 n);       /* slot 0x4b */
extern void  far gfx_copyRect(int src, uint16 sx, uint16 sy, int dst,
                              uint16 dx, uint16 dy, int w, int h); /* 0x2a */
extern void  f19_loadSpriteScaled(const char *n, int16 mode, int16 b); /* callee reads a 3rd it never received */
extern void  f19_loadSpriteRes(const char *n, int16 sel);

void f19_drawStoreIcons(void) {
    uint16 i;
    int16 page;

    word_298E6 = gfx_getVal();
    gfx_setDac(3);
    gfx_setFadeSteps(1);
    if (word_298E6 == 0) {
        f19_loadSpriteScaled("arming.spr", 0, 0);
        gfx_setFadeSteps(8);
        f19_loadSpriteScaled("f19.spr", 2, 0);
        for (i = 0; i < 4; i++) {
            if ((&commData->storeType)[i] != 0x13)
                gfx_copyRect(0, ((&commData->storeType)[i] % 6) * 0x33 + 1,
                             ((&commData->storeType)[i] / 6) * 0x23 + 2, 2,
                             word_27990[i], word_27998[i], 0x31, 0x15);
            else
                gfx_copyRect(0, 1, 0xA5, 2,
                             word_27990[i], word_27998[i], 0x31, 0x15);
        }
    } else {
        page = gfx_allocPage(1);
        gfx_storeBufPtr(page, 1);
        f19_loadSpriteScaled("arming.spr", 1, 0);
        gfx_setFadeSteps(8);
        f19_loadSpriteRes("f19.spr", commData->f20);
        for (i = 0; i < 4; i++) {
            if ((&commData->storeType)[i] != 0x13)
                gfx_copyRect(1, ((&commData->storeType)[i] % 6) * 0x33 + 1,
                             ((&commData->storeType)[i] / 6) * 0x23 + 2, 2,
                             word_27990[i], word_27998[i], 0x31, 0x15);
            else
                gfx_copyRect(1, 1, 0xA5, 2,
                             word_27990[i], word_27998[i], 0x31, 0x15);
        }
    }
}

/* ==== seg000:0x6261 — panel-DSL stepper: runs the layout program at
 * e->strOff for one clipTable record until a field redraws (dflag=1) or a
 * saved group resumes.  DSL: digits set the current group's repeat count,
 * ':' skips a numeric arg, '<' '>' '^' '_' move the cell cursor with
 * wraparound, 'A'..'Z' draw field c&0x1F-1 (whole panel when the cursor is
 * at 0,0, else the four edge strips around the cell), '(...)'.N repeats the
 * group while timer>0 else skips to the matching ')', '|'/')' pop a group
 * level, and NUL restarts the program. ==== */
void f19_stepPanelAnim(struct ClipEntry *e) {
    int16 num, c, i, cur, fidx, nlvl, dflag, dead, totx;
    uint16 srcof;
    uint8 peekz;

    dflag = 0;
    srcof = e->strOff;
    i = e->grp;
    if (e->timer[i] != 0) {
        cur = e->save[i];
        e->timer[i]--;
    } else
        cur = e->save[i] + 1;
    while (dflag == 0) {
        c = (*(uint8 *)f19_dsegAt(srcof + cur++));
        if (c >= 0x30 && c <= 0x39) {
            num = c - 0x30;
            while ((peekz = (*(uint8 *)f19_dsegAt(srcof + cur))) >= 0x30 && peekz <= 0x39)
                num = num * 10 + (*(uint8 *)f19_dsegAt(srcof + cur++)) - 0x30;
            e->timer[i] = num - 1;
        } else if (c == 0x3A) {
            while ((peekz = (*(uint8 *)f19_dsegAt(srcof + cur))) >= 0x30 && peekz <= 0x39)
                cur++;
        } else if (c == 0x3C || c == 0x3E || c == 0x5E || c == 0x5F) {
            switch (c) {
            case 0x3E:
                e->col--;
                if (e->col == 0xFF) goto wc;
                break;
            wc: e->col = e->w - 1;
                break;
            case 0x3C:
                e->col++;
                if (e->col == e->w) goto zc;
                break;
            zc: e->col = 0;
                break;
            case 0x5F:
                e->row--;
                if (e->row == 0xFF) goto wr;
                break;
            wr: e->row = e->h - 1;
                break;
            case 0x5E:
                e->row++;
                if (e->row == e->h) goto zr;
                break;
            zr: e->row = 0;
                break;
            }
        } else if (c >= 0x41 && c <= 0x5A) {
            fidx = (c & 0x1F) - 1;
            if ((e->col | e->row) == 0) {
                f19_dispatchDrawMode((int16 *)(((uint8 *)f19_dsegAt(word_2170C))), e->sprX[fidx], e->sprY[fidx],
                                 (int16 *)(((uint8 *)f19_dsegAt(word_2170A))), e->x0 + viewOriginX,
                                 e->y0 + viewOriginY, e->w, e->h);
            } else {
                f19_dispatchDrawMode((int16 *)(((uint8 *)f19_dsegAt(word_2170C))), e->sprX[fidx], e->sprY[fidx],
                                 (int16 *)(((uint8 *)f19_dsegAt(word_2170A))),
                                 e->x0 + e->w + viewOriginX - e->col,
                                 e->y0 + e->h + viewOriginY - e->row,
                                 e->col, e->row);
                f19_dispatchDrawMode((int16 *)(((uint8 *)f19_dsegAt(word_2170C))), e->sprX[fidx] + e->col,
                                 e->sprY[fidx], (int16 *)(((uint8 *)f19_dsegAt(word_2170A))),
                                 e->x0 + viewOriginX,
                                 e->y0 + e->h + viewOriginY - e->row,
                                 e->w - e->col, e->row);
                f19_dispatchDrawMode((int16 *)(((uint8 *)f19_dsegAt(word_2170C))), e->sprX[fidx],
                                 e->sprY[fidx] + e->row, (int16 *)(((uint8 *)f19_dsegAt(word_2170A))),
                                 e->x0 + e->w + viewOriginX - e->col,
                                 e->y0 + viewOriginY,
                                 e->col, e->h - e->row);
                f19_dispatchDrawMode((int16 *)(((uint8 *)f19_dsegAt(word_2170C))), e->sprX[fidx] + e->col,
                                 e->sprY[fidx] + e->row, (int16 *)(((uint8 *)f19_dsegAt(word_2170A))),
                                 e->x0 + viewOriginX, e->y0 + viewOriginY,
                                 e->w - e->col, e->h - e->row);
            }
            e->save[i] = cur - 1;
            dflag = 1;
        } else if (c == 0x28) {
            e->save[i++] = cur - 1;
            totx = 1;
            word_2BE50 = cur;
            do {
                fidx = f19_evalChoiceExpr((uint16 *)(((uint8 *)f19_dsegAt(0xBE50))), srcof);
                totx += fidx;
            } while (fidx != 0);
            totx = f19_randMul(-1) % totx;
            word_2BE50 = cur;
            while (totx > 0)
                totx -= f19_evalChoiceExpr((uint16 *)(((uint8 *)f19_dsegAt(0xBE50))), srcof);
            cur = word_2BE50;
        } else if (c == 0x7C || c == 0x29) {
            i--;
            if (e->timer[i] == 0) {
                cur = e->save[i] + 1;
                nlvl = 1;
                do {
                    nlvl += ((*(uint8 *)f19_dsegAt(srcof + cur)) == 0x28) ? 1 : 0;
                    nlvl -= ((*(uint8 *)f19_dsegAt(srcof + cur)) == 0x29) ? 1 : 0;
                    cur++;
                } while (nlvl > 0);
            } else {
                e->timer[i]--;
                cur = e->save[i];
            }
        } else if (c == 0) {
            i = 0;
            e->save[0] = 0;
            e->timer[0] = 0;
            cur = 0;
        }
    }
    e->grp = i;
}
