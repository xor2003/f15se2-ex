/* F-19 END.EXE module — shared types and decls for the debrief screens.
 *
 * Sources are native adaptations of the verified f19ru src_end ports.
 * The DOS dseg lives in f19_enSpace (f19envars.h, world 2 in f19seg.c);
 * pointer-typed cells materialize as native pointers at module init.
 */
#ifndef F19EN_H
#define F19EN_H

#include "f19.h"
#include <stdlib.h>
#include <string.h>
#include "f19enpfx.h"

/* f19.h binds commData/gameData to the START/EGAME views — END keeps its
 * own struct view (CommDataEnd/PilotRecEnd) over the same comm block. */
#undef commData
#undef gameData
#undef word_2D066
#undef word_2991C

/* ---------- shared record types (DOS layouts, packed) ---------- */
#pragma pack(push, 1)

/* MenuItem fields as END.EXE addresses them (0x32 stride) */
typedef struct {
    int16 hitX1;            /* 0x00 */
    int16 hitY1;            /* 0x02 */
    int16 hitX2;            /* 0x04 */
    int16 hitY2;            /* 0x06 */
    int16 colorX1;          /* 0x08 */
    int16 colorY1;          /* 0x0a */
    int16 colorX2;          /* 0x0c */
    int16 colorY2;          /* 0x0e */
    int16 colorTableIdx;    /* 0x10 */
    int16 colorPair;        /* 0x12 */
    int16 labelData1[5];    /* 0x14 */
    int16 pagePtr;          /* 0x1e — page desc near-offset (word in DOS layout) */
    int16 labelData2[4];    /* 0x20 */
    int16 spriteNormal;     /* 0x28 */
    int16 spriteBlink;      /* 0x2a */
    int16 unk_2c;           /* 0x2c */
    int16 state;            /* 0x2e */
    uint16 flags;           /* 0x30 */
} MenuItem;

typedef struct {
    int16 target1Type[2];     /* 0x00 */
    int16 waypointData;       /* 0x04 */
    int16 pad06;              /* 0x06 */
    int16 target1MiscBits[5]; /* 0x08 */
    int16 target2Type[4];     /* 0x12 */
    int16 target2MiscBits[5]; /* 0x1a */
} TargetBlock;                                  /* 0x24 — over targetBlockWd */

struct EvtItem {                                /* item/page recs, 0x20 stride */
    int16 win;                                  /* +0x00 — window ptr (dseg off) */
    int8  pad02[0x18];
    int16 page;                                 /* +0x1a */
    int8  pad1c[4];
};
#define evtItemWin(i) ((int16 *)f19_dsegAt((uint16)evtItems[i].win))

struct BlinkSprite {
    int16 pad00;
    int16 srcX;                 /* +2 */
    int16 pad04;
    int16 pad06;
    int16 dstX;                 /* +8 */
    int16 dstY;                 /* +a */
};

struct FlightLogRec {
    uint8 mapX, mapY;
    int8 status;
    int8 unitId;
    uint8 pad4, pad5;
};

/* record fields as the animation DSL addresses them (recOff = byte offset) */
struct AnimRec {
    int16 posX;             /* +0x00 */
    int16 posY;             /* +0x02 */
    int16 maxA;             /* +0x04 counter-A wrap max */
    int16 maxB;             /* +0x06 counter-B wrap max */
    uint8 field8;           /* +0x08 */
    uint8 field9;           /* +0x09 */
    uint8 cntA;             /* +0x0A frame counter A */
    uint8 cntB;             /* +0x0B frame counter B */
    uint8 chanIdx;          /* +0x0C channel index */
    uint8 repeat[10];       /* +0x0D repeat counter[channel] */
    uint8 strPos[11];       /* +0x17 strPos[channel] */
    int16 frameW[18];       /* +0x22 frame word table */
    uint8 frameB[18];       /* +0x46 frame byte table */
    int16 strOff;           /* +0x58 DSL script offset */
    uint8 active;           /* +0x5A */
    uint8 pad5b;
};                              /* 0x5C */
/* companion 0x49-stride channel array parallel to AnimRec (0x3426) */
struct ChanRec {
    uint8  f0;              /* +0x00 channel/sub-frame count */
    uint8  f1[18];          /* +0x01 */
    uint8  f2[18];          /* +0x13 */
    uint8  f3[18];          /* +0x25 */
    uint8  f4[18];          /* +0x37 */
};                              /* 0x49 */

struct MapRect {
    int16 rx, ry, rw, rh;                       /* +0,+2,+4,+6 */
    uint8 pad[0x52];
    uint8 active;                               /* +0x5a */
    uint8 padEnd;
};

struct PlaneObjEnd { int16 validFlag; int8 pad02[0x1e]; };   /* stride 0x20 */
struct WorldObjEnd {                            /* stride 0x10 */
    int16 unitRef;                              /* +0x00 */
    int8  pad02[0x0c];
    int16 objectIdx;                            /* +0x0e */
};
struct PlaneNameEnd { char name[0x20]; };       /* stride 0x20 */
struct SamNameEnd  { char name[0x12]; };        /* stride 0x12 */
struct UnitInfo { int16 w0; int16 pad02[8]; };  /* stride 0x12 */
struct OrdType  { int16 f0; int16 pad02[5]; };  /* stride 0x0c */

struct CommDataEnd {                            /* comm block at f19_commBase */
    int8 pad1a[0x1a];
    int16 field1a;                              /* 0x1a — gfx driver tbl seg */
    int8 pad1c[0x02];
    int16 field1e;                              /* 0x1e — misc driver tbl seg */
    int16 gfxInitResult;                        /* 0x20 */
    int8 pad22[0x02];
    int16 setupMono;                            /* 0x24 */
    int16 landingType;                          /* 0x26 */
    int16 bailout;                              /* 0x28 */
    int8 pad2a[0x02];
    uint16 field2c;                             /* 0x2c */
    uint16 missionTime;                         /* 0x2e */
    int16 trainingFlag;                         /* 0x30 */
    int8 pad32[0x02];
    uint8 commFlags34;                          /* 0x34 */
    int8 pad35;
    uint16 commField36;                         /* 0x36 */
    uint16 slotWpn[29];                         /* 0x38 */
    int16 setupUseJoy;                          /* 0x72 */
    uint16 posX;                                /* 0x74 — fixed-point (>>11 tile) */
    uint16 posY;                                /* 0x76 */
    int8 pad78[0x02];
    uint8 joyData[0x0c];                        /* 0x7a..? joy table area */
};

struct PilotRecEnd {                            /* pilot record at comm+0x120e */
    int8  pad0[0x20];
    uint16 rank;                                /* 0x20 */
    uint16 flag22;                              /* 0x22 — one-shot award flag */
    uint16 award24, award26, award28, award2a;  /* 0x24-0x2a — tier counters */
    uint16 flag2c;                              /* 0x2c — 1200-pt award flag */
    uint16 bestScore;                           /* 0x2e */
    uint16 awardPoints;                         /* 0x30 — award threshold pool */
    int32 totalScore;                           /* 0x32 */
    uint16 missionCount;                        /* 0x36 */
    uint16 field38;                             /* 0x38 — res-name tbl idx (a.k.a. multTheater) */
    uint16 field3a;                             /* 0x3a — ROE-violation class (a.k.a. isCampaignMission) */
    uint16 field3c, field3e, field40;           /* 0x3c-0x40 (a.k.a. multMission/multDiff/multUnk) */
    int8  pad42[2];
    uint16 flag44;                              /* 0x44 — training award flag */
    uint16 flag46, flag48, flag4a, flag4c;      /* 0x46-0x4c — ribbon flags */
    int16  field4e;                             /* 0x4e */
};

union FarWords { uint8 *p; struct { uint16 off; uint16 seg; } w; };

#pragma pack(pop)

#include "f19envars.h"

#define commData ((struct CommDataEnd *)f19_commBase)
#define pilotRec ((struct PilotRecEnd *)(f19_commBase + 0x120e))

/* targetBlock is a struct view over the targetBlockWd cell block (0x8c7a) */
#define targetBlock (*(TargetBlock *)f19_enSpace.m_targetBlockWd)
/* worldStrings[] is the 100-entry near-ptr table DOS kept at dseg 0x9A22;
 * natively it holds real char* into worldStringBuf, so it lives off-seg. */
extern char *worldStrings[];
extern uint8 *worldBufPtr;                      /* comm+0x7a rolling cursor */

/* ---- pointer cells: declared extern ptr in the sources; natively each is
 * a real pointer materialized from the cell's image offset at module init
 * (f19_enInitPtrs).  The dseg cell itself stays as m_<name>. ---- */
extern struct BlinkSprite *spriteAir;         /* cell 0x5934 -> 0x5916 */
extern struct BlinkSprite *spriteGround;      /* cell 0x5974 -> 0x5956 */
extern struct BlinkSprite *spriteSam;         /* cell 0x59B4 -> 0x5996 */
extern struct BlinkSprite *spriteWaypoint;    /* cell 0x5A34 -> 0x5A16 */
extern struct BlinkSprite *spriteAirBlink;    /* cell 0x5954 -> 0x5936 */
extern struct BlinkSprite *spriteGroundBlink; /* cell 0x5994 -> 0x5976 */
extern struct BlinkSprite *spriteSamBlink;    /* cell 0x59D4 -> 0x59B6 */
extern struct BlinkSprite *spriteWaypointBlink;/* cell 0x5A54 -> 0x5A36 */
extern struct BlinkSprite *spriteMapArea;     /* cell 0x58F4 -> 0x58D6 */
extern int16 *word_1BAD0;                     /* cell 0x1D40 -> 0x1D0E */
extern int16 *word_1BACE;                     /* cell 0x1D3E -> 0x1D02 */
extern int16 *word_1BAD2;                     /* cell 0x1D42 -> 0x1D1A */
extern int16 *word_1BAD4;                     /* cell 0x1D44 -> 0x1D26 */
extern int16 *word_1BAD6;                     /* cell 0x1D46 -> 0x1D32 */
extern int16 *word_1BAD8;                     /* cell 0x1D48 -> 0x0000 */
extern int16 *word_19806;                     /* cell 0x5806 -> 0x57F0 */
extern int16 *word_1ED12;                     /* cell 0x4F82 -> 0x4F6C */
extern int16 *word_1981E;                     /* cell 0x57AE -> 0x5798 */
extern int16 *word_19704;                     /* cell 0x5704 -> 0x56EE */
extern int16 *word_2379E;                     /* cell 0x9A0E -> string tbl */
extern int16 *word_1F664;                     /* cell 0x58D4 -> 0x58BE */
extern int16 *word_1F856;                     /* cell 0x5AC6 -> 0x5ABA */
extern int16 *awardTextItem;                  /* cell       -> 0x61D0 */
extern int16 *word_207B2;                     /* cell 0x6A22 — debrief panel */
extern int16 *word_1F426;                     /* cell 0x5696 — eval panel */
extern int16 *purpleHeartSpr;                 /* cell 0x66AE */
extern int16 *medalSpriteTab;                 /* cell 0x66A2 */
extern int16 *rankSpriteA, *rankSpriteB, *rankSpriteC;  /* 0x667A/88/96 */
extern int16 *medalNames;                     /* cell 0x66E8 */
extern int16 *queuedAwardName;                /* cell 0x66F4 */
extern int16 *ribbonNames;                    /* cell 0x66FE */
extern int16 *newRankNames;                   /* cell 0x6706 */
extern int16 *nextRankNames;                  /* cell 0x6714 */
/* element access for the string/sprite-offset tables above */
#define f19en_strTab(tab, i) ((char *)f19_dsegAt((uint16)(tab)[(i)]))
#define f19en_sprTab(tab, i) ((int16 *)f19_dsegAt((uint16)(tab)[(i)]))
extern uint8 *word_1C6E8;                     /* staging buf (0x2150) */
extern uint8 *word_1C6EA;                     /* staging buf (0x2550) */
extern uint16 *colorTablePtr;                 /* cell 0x8690 */

/* sprite-record cells that push their content (record near-ptr) */
#define word_1F684 f19en_cellW(0x58F4)        /* -> spriteMapArea rec */
#define word_1F6A4 f19en_cellW(0x5914)        /* -> second map-area rec */

void f19_enInitPtrs(void);                    /* f19enmain.c */

/* ---- ported END routines (declared under their source names; the pfx
 * macros above retarget every declaration and call to f19en_* linkage) ---- */
uint16 allocBuffer(int16 size);
void   freeBuffer(uint16 segment);
int16  allocClearBuf(int16 size);
void   loadPicFromFileAt(const char *name, int16 off, int16 whence);
void   openBlitClosePic(const char *name, int16 page);
void   openDecodeClosePic(const char *name, int16 page);
void   openDecodePicAt(const char *name, int16 page, int32 offset);
int16  isPointInRect(MenuItem *p);
int16  mapToScreenX(int16 mapCoord);
int16  mapToScreenY(int16 mapCoord);
void   drawMapPixel(int16 x, int16 y, int16 color);
void   timerWait(uint16 ticks);
void   processMenuItems(MenuItem *items, int16 unused, int16 itemCount,
                        int16 cursorStartX, int16 cursorStartY, int16 *gfxPage);
int16  selectMenuItem(MenuItem *items, int16 unused, int16 itemCount,
                      int16 *inputState, int16 *gfxPage);
void   processDebriefInput(int16 *cursorBounds, MenuItem *menuItem, int16 *gfxPage);
void   drawEventSprite(uint16 rec);
void   blinkWidget(MenuItem *item, int16 *gfxPage);
void   plotMapPoint(int16 x, int16 y, int16 color, int16 unused);
void   drawClippedLineEx(int16 x1, int16 y1, int16 x2, int16 y2,
                         int16 wx1, int16 wx2, int16 wy1, int16 wy2, int16 flag);
void   drawClippedLine(int16 x1, int16 y1, int16 x2, int16 y2);
void   drawFlightLine(int16 p1, int16 p2, int16 p3, int16 p4);
uint16 drawFlightPath(int16 *gfxPage, uint16 maxRecord);
char  *formatFlightTime(int16 timeValue, char *buffer);
int16  drawMapView(int16 viewY, int16 viewX, int16 sel);
void   serviceTick(void);
int16  parseCmd(uint8 *base, int16 *pc);
void   tickRecAnim(struct AnimRec *recOff);
void   tickRecords(void);
void   clearActiveInRect(int16 x, int16 y, int16 w, int16 h);
void   resetRecField9(void);
void   checkPromotion(void);
void   checkAwardCodes(void);
void   sub_1883C(void);
void   drawMenuItem(const MenuItem *items, uint16 index, int16 *gfxPage);
void   animateFlightPath(int16 *gfxPage);
int32  calcMissionScore(int16 param);
int16  loadMapView(char *fname, int16 a2);
int16  sub_17248(void);
void   sub_17334(void);
void   sub_16076(void);
void   sub_17094(void);
void   sub_1714C(void);
void   sub_17280(void);
void   sub_15D1B(void);
void   sub_16486(void);
void   sub_175BC(void);
void   drawFarString(int16 *s, char far *str);
void   drawStringAtPos(int16 *s, char far *str, int16 x, int16 y);
void   drawStringAt(int16 *pageNum, const char *string, int16 x, int16 y);
void   drawWrappedTextFar(int16 *page, char far *str, uint16 maxWidth,
                          int16 x, int16 y, int16 lineHeight);
void   drawWrappedText(int16 *page, char *str, uint16 maxWidth,
                       int16 x, int16 y, int16 lineHeight);
int16  stringWidth(int16 *item, uint8 *str);
void   sub_12EF2(int16 *p1, int16 a2, int16 a3, int16 *p4,
                 int16 a5, int16 a6, int16 a7, int16 a8);
void   sub_12F27(int16 *p1, int16 a2, int16 a3, int16 *p4,
                 int16 a5, int16 a6, int16 a7, int16 a8);
int16  openFileWrapper(const char *name, int16 mode);
int16  createFileWrapper(const char *name, int16 attr);
void   closeFileWrapper(int16 fd);
int16  readFile1Wrapper(int16 fd, int16 count, int16 off);
int16  readFile2Wrapper(int16 fd, int16 count, int16 off, int16 seg);
int16  writeFileAtRawWrapper(int16 fd, int16 count, int16 off, int16 seg,
                             int16 addend);
int16  readPicStream(uint8 *dst, int16 count, int16 fd);
int16  readStageStream(uint8 *dst, int16 count);
int16  stageAppend(int16 n);
void   initGraphics(void);
void   cleanup(void);
void   restoreVideoMode(void);
void   restoreInterrupts(void);
void   clearKeybuf(void);
void   waitForKeyOrJoy(void);
void   waitForKeyOrJoy2(void);
void   sub_102FD(void);
void   sub_10010(void);
void   mystrcpy(char *dst, const char *src);
void   mystrcat(char *d, const char *s);
void   seedRandom(void);
int16  randomRange(int16 maxVal);
void   readWorldData(void);
void   loadWorldData(void *dest, int16 size);
int16  setupWorldBufPtr(void);
void   readFromWorldBuf(void *dest, int16 size, int16 count, int16 handle);
void   writeToWorldBuf(void *dest, int16 size, int16 count, int16 handle);
void   loadWorldStrings(void);
void   movedata(int16 sseg, const void *soff, int16 dseg, void *doff,
                uint16 len);
void   picStreamRead(int16 fd);
void   seedRandom16(int16 v);
void   srand(int16 v);
int16  lseek(int16 fd, int32 off, int16 whence);
void   seekFileAt(int16 fd, int16 off, int16 whence);
uint16 dos_alloc(uint16 size);
int16  dos_free(uint16 seg);
void   dos_printstring(const char *s);
int16  dos_read(int16 fd, uint8 *dst, int16 count);
void   intDispatch(int16 intNum, uint8 *inRegs, uint8 *outRegs);
void far joyTableSetup(char *p);
void far pollJoystick(void);
int16  readJoyAxis(int16 a);
void   exit(int16 code);
int16  rand(void);
int16  readBiosTickLo(void);
int16  misc_jump_5a_keybuf(void);
int16  misc_jump_5b_getkey(void);
int16  misc_jump_5d_readJoy(int16 axis);
void   misc_jump_5e_clearKeyFlags(void);
void   memsetNear(uint8 *dst, int16 val, uint16 count);
void   memsetFar(uint8 *dst, int16 val, uint16 count);
void   memcpyFromFar(char *dst, char *src, int16 n);
void   farStrcpy(char *dst, char *src);
void   strcpyToFar(char *dst, const char *src);
int16  mystrlen(const char *s);
char  *mystrchr(char *s, int16 c);
int16  memeq(const void *a, const void *b, int16 n);
void   copyBytes(char *dst, char *src, int16 n);
void   picBlit(int16 fd, int16 page);
void   decodePic(int16 fd, int16 page);
void   drawLineWrapper(void);
void   clearRect(int16 *item, int16 x1, int16 y1, int16 x2, int16 y2);
int16  sub_11A1C(int16 count, int16 fd);
void   sub_10D1A(int16 seg);
void   sub_10E50(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2);
int16  sub_130BE(void);
int16  sub_131E7(void);
void   gety(int16 a, int16 b, int16 c, int16 d);
void   sub_12FE8(int16 a, int16 b, int16 c, int16 d);
void   sub_13335(int16 w, int16 h, int16 sseg, int16 soff, int16 dseg, int16 doff);
void   sub_133BA(int16 w, int16 h, int16 sseg, int16 soff, int16 dseg, int16 doff);
void   sub_133BD(int16 w, int16 h, int16 sseg, int16 soff, int16 dseg, int16 doff);
void   sub_133EC(int16 w, int16 h, int16 sseg, int16 soff, int16 dseg, int16 doff);
void   sub_13436(int16 w, int16 h, int16 sseg, int16 soff, int16 dseg, int16 doff);
int16  runMapView(int16 sel);
void   picStageRefill(void);
int16  picReadBlock(void);
void far textOp_165(int16 p1, int16 a2, int16 a3, int16 p4,
                    int16 a5, int16 a6, int16 a7, int16 a8);
void far textOp_477(int16 p1, int16 a2, int16 a3, int16 p4,
                    int16 a5, int16 a6, int16 a7, int16 a8);
void far textOp_762(int16 p1, int16 a2, int16 a3, int16 p4,
                    int16 a5, int16 a6, int16 a7, int16 a8);
void far textOp_A61(int16 p1, int16 a2, int16 a3, int16 p4,
                    int16 a5, int16 a6, int16 a7, int16 a8);
void far gfx_drvMode(void);

/* ---- service shims + skeleton-native ports (f19enskel.c) ---- */
int16 f19en_rand(void);
void  f19en_seedRandom16(int16 v);
int16 f19en_readBiosTickLo(void);
void  f19en_exit(int16 code);
int16 f19en_picReadBlock(void);
void  f19en_picStageRefill(void);
int16 f19en_sub_11A1C(int16 count, int16 fd);
void  f19en_sub_10E50(int16 *page, int16 x1, int16 y1, int16 x2, int16 y2);
void  f19en_sub_10D1A(int16 seg);
int16 f19en_sub_130BE(void);
int16 f19en_sub_131E7(void);
void  f19en_gety(int16 a, int16 b, int16 c, int16 d);
void  f19en_sub_13335(int16 w, int16 h, int16 sseg, int16 soff,
                      int16 dseg, int16 doff);
void  f19en_sub_133BA(int16 w,int16 h,int16 sseg,int16 soff,int16 dseg,int16 doff);
void  f19en_sub_133BD(int16 w,int16 h,int16 sseg,int16 soff,int16 dseg,int16 doff);
void  f19en_sub_133EC(int16 w,int16 h,int16 sseg,int16 soff,int16 dseg,int16 doff);
void  f19en_sub_13436(int16 w,int16 h,int16 sseg,int16 soff,int16 dseg,int16 doff);
int16 f19en_runMapView(int16 sel);
int16 f19en_misc_jump_5a_keybuf(void);
int16 f19en_misc_jump_5b_getkey(void);
int16 f19en_misc_jump_5d_readJoy(int16 axis);
void  f19en_misc_jump_5e_clearKeyFlags(void);
void  f19en_dos_alloc_dummy(void);

/* raw cell accessors for offsets that have no named member (alias cells
 * inside member spans) */
#define f19en_cellW(off) (*(int16 *)((uint8 *)&f19_enSpace + (off)))
#define f19en_cellB(off) (*(uint8 *)((uint8 *)&f19_enSpace + (off)))

/* module entry (f19enmain.c) */
int f19_end_main(void);

#endif /* F19EN_H */
