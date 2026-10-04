/* START.EXE — mission-generator helpers (f15se2 stgen.c lineage; F19 field
 * offsets differ from F15's, verified against START.EXE disasm) */
#include <stdlib.h>
#include <stdio.h>
#include "f19.h"
#include <dos.h>


extern int16 *f19_nearestTerrainResult;
#define readItemSize (*(int16 *)(f19_dseg + 0xC978))
extern FILE *f19_fileHandle;
#define groundUnitCount (*(int16 *)(f19_dseg + 0x994A))
#define worldObjectCount (*(int16 *)(f19_dseg + 0xD05E))
#define flightUnitCount (*(int16 *)(f19_dseg + 0xCA66))
#define wldReadBuf1 ((uint8 *)(f19_dseg + 0xCA52))
#define wldReadBuf7 ((uint8 *)(f19_dseg + 0xC9E2))
#define wldReadBuf8 ((uint8 *)(f19_dseg + 0xC97A))
#define objectTypeTable ((uint8 *)(f19_dseg + 0xC162))
#define terrainGrid ((uint8 *)(f19_dseg + 0xB842))
#define wldReadBuf11 ((int8 *)(f19_dseg + 0xCB38))
#define wldOffsets ((int16 *)(f19_dseg + 0xCA70))
#define missionDistAccum (*(int16 *)(f19_dseg + 0xCA4A))
#define escortMissionFlag (*(int16 *)(f19_dseg + 0x98EA))
#define missionMidX ((uint16 *)(f19_dseg + 0x3E0A))

#define escortObj (*(int16 *)(f19_dseg + 0xBB72))
#define tgtPreciseX (*(int32 *)(f19_dseg + 0xC146))
#define tgtPreciseY (*(int32 *)(f19_dseg + 0xC146))
#define loadoutTab ((uint8 *)(f19_dseg + 0x46F6))
#define missionSpeedTab ((int16 *)(f19_dseg + 0x4506))
#define briefTimeA ((char *)(f19_dseg + 0x4DB6))
#define briefTimeB ((char *)(f19_dseg + 0x4DBC))
#define briefTimeC ((char *)(f19_dseg + 0x4DC2))
#define briefCoord2 ((char *)(f19_dseg + 0x4DC8))
#define bufCoordStr ((char *)(f19_dseg + 0x98CC))
#define missionTimeFlag (*(int16 *)(f19_dseg + 0x44E4))
#define difficultySaved (*(int16 *)(f19_dseg + 0x44E0))
#define theaterSaved (*(int16 *)(f19_dseg + 0x98C6))
#define flag4Saved (*(int16 *)(f19_dseg + 0x98C4))
#define regnPlhPtr   (*(uint16 *)(f19_dseg + 0x4DAC))
#define plhFiles ((uint16 *)(f19_dseg + 0x4DAE))

extern void movedata(int16 srcSeg, int16 srcOff, int16 dstSeg, int16 dstOff, int16 len);

uint8 *moveDst;   /* ds:0x98c8 — worldBuf write cursor */

#define XYDIST_MAX 0x7fff

int16 f19_rangeApprox(int16 deltaX, int16 deltaY) {
    int32 dist;
    deltaX = abs16Compat(deltaX);
    deltaY = abs16Compat(deltaY);
    if (deltaX > deltaY)
        dist = (int32)(deltaY >> 1) + (int32)deltaX;
    else
        dist = (int32)(deltaX >> 1) + (int32)deltaY;
    if (dist > XYDIST_MAX)
        dist = XYDIST_MAX;
    return (int16)dist;
}

void f19_memAppend(const void *ptr, int16 itemsz, int16 count, FILE *unused) {
    const void *farptr;
    farptr = ptr;
    memcpy(moveDst, farptr, itemsz * count);
    moveDst += itemsz * count;
}

/* seg000:0x8d06 — pull bytes back out of the comm buffer (movedata FROM
 * moveDst; arg order mirrors f19_memAppend's) */
void f19_commFetch(void *ptr, int16 itemsz, int16 count) {
    const void *farptr;
    farptr = ptr;
    memcpy((void *)farptr, moveDst, itemsz * count);
    moveDst += itemsz * count;
}

/* seg000:0x8cf0 — point the comm write cursor at commData->worldBuf */
struct GameComm { int8 pad[0x2e]; int16 missionRange;  /* +0x2e */
                  int8 pad30[8];  int16 missionKind[4];  /* +0x38..3e */
                  int16 missionStat[4];                  /* +0x40..46 */
                  int8 pad48[0x32]; int16 worldBuf; };
extern struct GameComm *commData;   /* the real commData object, this TU's view */

FILE *f19_setMoveDstComm7A(const char *unused0, const char *unused1) {  /* K&R empty-decl */
    moveDst = (uint8 *)&commData->worldBuf;
    return (FILE *)1;    /* sentinel: the comm-buffer "file" is a memory sink */
}

void f19_doNothing(FILE *h) {   /* seg000:0x8d72 — bare ret */
}

/* FlightUnit stride 0x24, f19_worldObjects stride 0x10, planes stride 0x20 */
typedef struct {
    int16 waypointIdx;     /* 0x00 */
    uint16 x, y;           /* 0x02, 0x04 (uint16: <<5 widening emits sub dx,dx) */
    int16 altitude;        /* 0x06 */
    int32 xPrecise;        /* 0x08 */
    int32 yPrecise;        /* 0x0c */
    int16 heading;         /* 0x10 */
    int16 pitch;           /* 0x12 */
    int16 roll;            /* 0x14 */
    int16 planeType;       /* 0x16 */
    int16 flags;           /* 0x18 */
    int16 maxSpeed;        /* 0x1a */
    int16 fuel;            /* 0x1c */
    int16 pad1e[3];        /* 0x1e */
} FlightUnit;

typedef struct {
    int16 link;             /* 0x00 — object name/link word (dseg 0xb38e) */
    uint16 x_coord, y_coord; /* 0x02, 0x04 (uint16: <<5 widening emits sub dx,dx) */
    int16 unitType;         /* 0x06 — ground-unit type (link-chase) */
    int16 targetFlags;      /* 0x08 */
    int16 escortType;       /* 0x0a */
    int16 escortNum;        /* 0x0c */
    int16 objectIdx;        /* 0x0e */
} WorldObject;

typedef struct {
    int16 maxSpeed;         /* 0x00 */
    int16 range;            /* 0x02 */
    int16 pad4[14];         /* 0x04 */
} PlaneEntry;

#define f19_flightUnits   ((FlightUnit *)(f19_dseg + 0xBB78))
#define f19_worldObjects  ((WorldObject *)(f19_dseg + 0xB38E))
#define planes        ((PlaneEntry *)(f19_dseg + 0x3F72))

/* f19_worldObjects is the object table at dseg 0xB38E — aliased by the menu
 * modules as struct ObjD word_2B38E[]. */

void f19_positionUnit(int16 unit, int16 loc) {
    int16 planeType;
    planeType = f19_flightUnits[unit].planeType;
    f19_flightUnits[unit].x = f19_worldObjects[loc].x_coord + 9;
    f19_flightUnits[unit].y = f19_worldObjects[loc].y_coord - 12;
    f19_flightUnits[unit].xPrecise = (int32)f19_flightUnits[unit].x << 5;
    f19_flightUnits[unit].yPrecise = (int32)f19_flightUnits[unit].y << 5;
    f19_flightUnits[unit].altitude = f19_worldObjects[loc].targetFlags & 0x200 ? 0x8c : 0xc;
    f19_flightUnits[unit].maxSpeed = planes[planeType].maxSpeed;
    f19_flightUnits[unit].heading = 0xfc00;
    f19_flightUnits[unit].pitch = 0;
    f19_flightUnits[unit].roll = 0;
    f19_flightUnits[unit].flags |= 0x403;
    f19_flightUnits[unit].waypointIdx = loc;
    f19_flightUnits[unit].fuel = ((int32)planes[planeType].range << 0xd) / f19_flightUnits[unit].maxSpeed;
}

int16 f19_calcBearing(int16 dx, int16 dy) {   /* seg000:0x8b80 */
    int16 angle, result;
    int32 ratio;
    int16 divisor, swapped, quotient;
    if (dx == 0) {
        return (dy > 0) ? 0 : (int16)0x8000;
    }
    if (dy == 0) {
        return (dx > 0) ? 0x4000 : (int16)0xC000;
    }
    if (abs16Compat(dx) > abs16Compat(dy)) {
        ratio = (int32)abs16Compat(dy) << 0xe;
        divisor = abs16Compat(dx);
        swapped = 1;
    }
    else {
        ratio = (int32)abs16Compat(dx) << 0xe;
        divisor = abs16Compat(dy);
        swapped = 0;
    }
    quotient = ratio / (int32)divisor;
    angle = ((0x2800 - (((int32)abs16Compat((0x1333 - quotient)) * (int32)0xb00) >> 0xe)) * (int32)quotient) >> 0xe;
    if (dx > 0) {
        if (dy > 0) {
            result = swapped != 0 ? 0x4000 - angle : angle;
        }
        else {
            result = (swapped != 0) ? angle + 0x4000 : 0x8000 - angle;
        }
    }
    else {
        if (dy > 0) {
            result = (swapped != 0) ? angle + 0xC000 : -angle;
        }
        else {
            result = (swapped != 0) ? 0xC000 - angle : angle + 0x8000;
        }
    }
    return result;
}

/* seg000:0x866c — f19_findNearestTerrain hit → snap wx/wy to the terrain anchor,
 * reuse a matching f19_worldObjects[] slot or write into `slot`. */
int16 *f19_findNearestTerrain(int32 wx, int32 wy); /* seg000:0x6e8c */

int16 f19_findOrPlaceItem(int16 wx, int16 wy, int16 slot) {
    int16 j;
    if ((f19_nearestTerrainResult = f19_findNearestTerrain((int32)wx << 5,
            (0x8000 - (int32)wy) << 5)) != 0) {
        wx = ((int32 *)f19_nearestTerrainResult)[1] >> 5;
        wy = -((((int32 *)f19_nearestTerrainResult)[2] >> 5) - 0x8000);
        for (j = 3; j < readItemSize; j++) {
            if (f19_worldObjects[j].x_coord == wx && f19_worldObjects[j].y_coord == wy)
                return j;
        }
        f19_worldObjects[slot].x_coord = wx;
        f19_worldObjects[slot].y_coord = wy;
        f19_worldObjects[slot].objectIdx = *f19_nearestTerrainResult + 0x100;
        return slot;
    }
    return -1;
}

/* ---- f19_parseWorld / f19_exportWorldToComm (seg000:0x88a2, 0x8a0e) ---- */

struct Target {             /* dseg:0xb946, stride 0x12 */
    int16 kind;             /* +0  mission-target kind */
    int16 objIdx;           /* +2  f19_worldObjects[] index */
    int16 siteObj;          /* +4  site anchor object */
    int16 flags;            /* +6  lo=site flags, hi=extra */
    int16 siteIdx;          /* +8  f19_siteParms[] index */
    char  name[6];          /* +0a coord string */
    int16 tail;             /* +10 */
};
struct SiteParm {           /* dseg:0x4b0c, stride 0x0c */
    int16 theaterMask;      /* +0  bit per theater */
    int16 campMask;         /* +2  bit per campaign flag */
    int16 kind;             /* +4  -> f19_targets.kind */
    int16 reqType;          /* +6  required unit-class byte */
    int16 flags;            /* +8  -> f19_targets.flags lo byte */
    int16 extra;            /* +0a >0 -> flags hi byte, <0 -> -planeType */
};
struct LinkPair {           /* dseg:0x447e, stride 4 */
    int16 nextA;            /* +0 */
    int16 nextB;            /* +2 */
};

#define f19_targets    ((struct Target *)(f19_dseg + 0xB946))
#define f19_siteParms  ((struct SiteParm *)(f19_dseg + 0x4B0C))
#define f19_linkTab    ((struct LinkPair *)(f19_dseg + 0x447E))
extern int16  randMul(uint16 n);
extern int16  f19_itemDistance(int16 a, int16 b);
extern char  *f19_getItemCoordStr(int16 idx);

void f19_parseWorld(const char *filename) {
    int16 j, l;
    if ((f19_fileHandle = fopen(filename, "rb")) == 0) return;
    fread(wldReadBuf1, 2, 1, f19_fileHandle);
    fread(&readItemSize, 2, 1, f19_fileHandle);
    fread(&groundUnitCount, 2, 1, f19_fileHandle);
    fread(&worldObjectCount, 2, 1, f19_fileHandle);
    fread(f19_worldObjects, 0x10, readItemSize, f19_fileHandle);
    fread(&flightUnitCount, 2, 1, f19_fileHandle);
    fread(f19_flightUnits, 0x24, flightUnitCount, f19_fileHandle);
    fread(wldReadBuf7, 0x64, 1, f19_fileHandle);
    fread(wldReadBuf8, 0x64, 1, f19_fileHandle);
    fread(objectTypeTable, 0x64, 1, f19_fileHandle);
    fread(terrainGrid, 1, 0x100, f19_fileHandle);
    fread(wldReadBuf11, 1, 0x2ee, f19_fileHandle);
    fclose(f19_fileHandle);
    wldOffsets[0] = 0xcb38;    /* dseg offset of wldReadBuf11 */
    j = 1;
    for (l = 0; l < 0x2ee; l++) {
        if (wldReadBuf11[l] == 0 && j < 0x64)
            wldOffsets[j++] = (int16)((uint8 *)(wldReadBuf11 + l + 1) - f19_dseg);
    }
}

void f19_exportWorldToComm(const char *filename) {
    int16 unused;
    if ((f19_fileHandle = f19_setMoveDstComm7A(filename, "wb")) == 0) return;
    f19_memAppend(wldReadBuf1, 2, 1, f19_fileHandle);
    f19_memAppend(&readItemSize, 2, 1, f19_fileHandle);
    f19_memAppend(&groundUnitCount, 2, 1, f19_fileHandle);
    f19_memAppend(&worldObjectCount, 2, 1, f19_fileHandle);
    f19_memAppend(f19_worldObjects, 0x10, readItemSize, f19_fileHandle);
    f19_memAppend(&flightUnitCount, 2, 1, f19_fileHandle);
    f19_memAppend(f19_flightUnits, 0x24, flightUnitCount, f19_fileHandle);
    f19_memAppend(wldReadBuf7, 0x64, 1, f19_fileHandle);
    f19_memAppend(wldReadBuf8, 0x64, 1, f19_fileHandle);
    f19_memAppend(wldReadBuf11, 1, 0x2ee, f19_fileHandle);
    f19_memAppend(terrainGrid, 1, 0x100, f19_fileHandle);
    f19_memAppend(&missionDistAccum, 2, 1, f19_fileHandle);
    f19_memAppend(&escortMissionFlag, 2, 1, f19_fileHandle);
    f19_memAppend(missionMidX, 4, 4, f19_fileHandle);
    f19_memAppend(f19_targets, 0x12, 2, f19_fileHandle);
    f19_doNothing(f19_fileHandle);
}

/* seg000:0x8d98 — format "TD00"-style grid ref into bufCoordStr (dseg:0x98cc).
 * EN has only theaters 0-3; other values fall through with gridOff* uninitialized. */
struct GD { int8 pad[0x38]; int16 theater; int16 isCampaignMission;
            uint8 flags3c; int8 pad3d; int16 difficulty; };
extern struct GD *gameData;
extern void mystrcpy(char *d, const char *s);

char *f19_formatGridRef(int16 wx, int16 wy, int16 theater) {
    int16 gridOffX, gridOffY;
    switch (gameData->theater) {
    case 0: mystrcpy(bufCoordStr, "TD00"); gridOffX = 6; gridOffY = 4; break;
    case 1: mystrcpy(bufCoordStr, "JZ00"); gridOffX = 0; gridOffY = 0; break;
    case 2: mystrcpy(bufCoordStr, "WX00"); gridOffX = 0; gridOffY = 0; break;
    case 3: mystrcpy(bufCoordStr, "CC00"); gridOffX = 3; gridOffY = 5; break;
    }
    wx = (((wx >> 5) * 0x14) >> 0xa) + gridOffX;
    while (wx > 9) {
        wx -= 0xa;
        bufCoordStr[0]++;
    }
    bufCoordStr[2] += (int8)wx;
    wy = (((wy >> 5) * 0x14) >> 0xa) + gridOffY;
    while (wy > 9) {
        wy -= 0xa;
        bufCoordStr[1]--;
    }
    bufCoordStr[3] += 9 - (int8)wy;
    return bufCoordStr;
}

/* seg000:0x8e9c — format "HH:MM" into buf (minutes floored to 5; first digit
 * is flagPrefix+1, set by f19_runGenerator — END enbrief.c formatTime lineage) */

void f19_formatTimeStr(char *buf, int16 v) {
    int16 h, m;
    mystrcpy(buf, "00:00");
    h = v / 0x708;
    buf[0] += missionTimeFlag + 1;
    buf[1] += h % 10;
    m = ((v / 0x1e) % 0x3c) / 5 * 5;
    buf[3] += m / 10;
    buf[4] += m % 10;
}

/* seg000:0x8e72 — clamp with a 0xC000 wrap guard (bearing-style clamp) */
int16 f19_clampValue(int16 v, int16 lo, int16 hi) {
    if (v > hi) return hi;
    if (v >= lo) return v;
    if (v <= (int16)0xC000) return hi;
    return lo;
}

/* seg000:0x76c8 — snapshot gameData fields, pick theater world file, then
 * grid/terrain parse + mission generator. Frameless (no params/locals). */
extern void f19_parseGridTerrain(void);         /* seg000:0x71f8 */
extern void f19_runGenerator(void);             /* seg000:0x7738 */

void f19_missionGenerate() {
    difficultySaved = gameData->difficulty;
    theaterSaved = gameData->theater;
    flag4Saved = gameData->isCampaignMission;
    switch (gameData->theater) {
    case 0: f19_parseWorld("libya.wld"); break;
    case 1: f19_parseWorld("gulf.wld"); break;
    case 2: f19_parseWorld("nc.wld"); break;
    case 3: f19_parseWorld("ce.wld"); break;
    }
    mystrcpy((char *)(f19_dseg + regnPlhPtr), (char *)(f19_dseg + plhFiles[gameData->theater]));
    f19_parseGridTerrain();
    f19_runGenerator();
}

/* ---- f19_runGenerator (seg000:0x7738) — campaign mission generator: pick two
 * target sites, score them, place escorts, export mission data to commData.
 * Retries via goto restart_40a8 (re-runs cycle++/check) — the inner target
 * pick is a do/while, so its re-rolls do NOT consume an cycle. */

void f19_runGenerator(void)
{
    int16 cycle;
    int16 mDist;
    int16 head;
    int16 mKind;
    int16 have;
    int16 waypt;
    int16 baseBrg;
    int16 pick;
    int16 tmpW;
    int16 rngLim;
    int16 minD2;
    int16 grType;
    int16 randW;
    int16 weap;
    int16 range[3];
    int16 randY;
    int16 retryCount;
    int16 sl;
    int16 okCnt;
    int16 m2;

    cycle = missionDistAccum = 0;
    minD2 = 0x1c2;
restart_40a8:
    cycle = cycle + 1;
    if (999 < cycle) goto counterMore1k;
    do {
        if (!(gameData->flags3c & 1)) {
            do {
                randW = randMul(worldObjectCount - 3) + 3;
            } while ((f19_worldObjects[randW].targetFlags & 0xd01) != 1);
            f19_targets[0].objIdx = randW;
        }
        else {
            do {
                randW = randMul(0xe0) * 0x80 + 0x840;
                randY = randMul(0xe0) * 0x80 + 0x840;
            } while ((terrainGrid[(randW >> 0xb) + ((randY >> 0xb) * 0x10)] & 3) != 0 ||
                     (f19_targets[0].objIdx = f19_findOrPlaceItem(randW, randY, 1)) == 0xffff ||
                     (f19_worldObjects[f19_targets[0].objIdx].targetFlags & 0x801) == 1);
        }
        do {
            randW = randMul(0xe0) * 0x80 + 0x840;
            randY = randMul(0xe0) * 0x80 + 0x840;
        } while ((terrainGrid[(randW >> 0xb) + ((randY >> 0xb) * 0x10)] & 3) != 0 ||
                 (f19_targets[1].objIdx = f19_findOrPlaceItem(randW, randY, 2)) == 0xffff ||
                 ((gameData->flags3c & 1) &&
                  (f19_worldObjects[f19_targets[1].objIdx].targetFlags & 0x801) == 1));
    } while (f19_targets[0].objIdx == f19_targets[1].objIdx ||
             (f19_itemDistance(f19_targets[0].objIdx, f19_targets[1].objIdx) >> 6) > 0xc8);
    for (sl = 0; sl < 2; sl++) {
        range[sl] = 0x7fff;
        for (m2 = worldObjectCount; m2 < readItemSize; m2++) {
            register int16 f = f19_worldObjects[m2].targetFlags;
            if ((f & 0x500) != 0 && (f & 0x201) != 0) {
                range[2] = f19_clampValue(f19_itemDistance(f19_targets[sl].objIdx, m2) +
                    ((f & 0x100) != 0 ?
                     randMul(0x64) * 0x40 + 0xc80 : 0), 0, 0x7fff);
                if (range[2] < 0x7000 &&
                    randMul(0x500) + range[2] <
                        ((f19_worldObjects[m2].targetFlags & 0x200) ? 0xc80 : 0) +
                        range[sl]) {
                    f19_targets[sl].siteObj = m2;
                    range[sl] = range[2];
                }
            }
        }
    }
    if ((gameData->flags3c & 2) && theaterSaved == 0) {
        if (gameData->flags3c & 1) {
            f19_targets[0].objIdx = 3;
            f19_targets[1].objIdx = 0xf;
            f19_targets[0].siteObj = 0x22;
            f19_targets[1].siteObj = 0x21;
        }
        else {
            f19_targets[0].objIdx = 0x16;
            f19_targets[1].objIdx = 8;
            f19_targets[0].siteObj = 0x22;
            f19_targets[1].siteObj = 0x23;
        }
        range[0] = f19_itemDistance(f19_targets[0].objIdx, f19_targets[0].siteObj);
        range[1] = f19_itemDistance(f19_targets[1].objIdx, f19_targets[1].siteObj);
    }
    mDist = (f19_itemDistance(f19_targets[0].objIdx, f19_targets[1].objIdx) >> 6) +
                (range[0] >> 6) + (range[1] >> 6);
    if (cycle + 0x2e4 < mDist || mDist < minD2) {
        minD2 -= 5 - difficultySaved;
        goto restart_40a8;
    }
    for (m2 = 0; m2 < 2; m2++) {
        f19_targets[m2].kind = 0;
        for (retryCount = 0; retryCount < 2; retryCount++) {
            okCnt = 0;
            for (sl = 0; sl < 0x38; sl++) {
                if ((f19_siteParms[sl].theaterMask & (1 << gameData->theater)) != 0 &&
                    (f19_siteParms[sl].campMask & (1 << gameData->isCampaignMission)) != 0 &&
                    (int8)objectTypeTable[
                        f19_worldObjects[f19_targets[m2].objIdx].objectIdx & 0x7f] ==
                        f19_siteParms[sl].reqType &&
                    (m2 == 0 || sl != f19_targets[0].siteIdx)) {
                    if (retryCount != 0 && okCnt == pick) {
                        f19_targets[m2].kind = f19_siteParms[sl].kind;
                        f19_targets[m2].siteIdx = sl;
                        f19_targets[m2].flags = f19_siteParms[sl].flags;
                        if (f19_siteParms[sl].extra > 0)
                            f19_targets[m2].flags += f19_siteParms[sl].extra << 8;
                    }
                    okCnt++;
                }
            }
            pick = randMul(okCnt);
        }
    }
    if ((gameData->flags3c & 2) && theaterSaved == 0) {
        sl = (gameData->flags3c & 1) ? 5 : 0x37;
        f19_targets[0].kind = f19_siteParms[sl].kind;
        f19_targets[0].siteIdx = sl;
        f19_targets[0].flags = f19_siteParms[sl].flags;
    }
    if (f19_targets[0].kind == 0) goto restart_40a8;
    if (f19_targets[1].kind == 0) goto restart_40a8;
    if (f19_targets[0].siteIdx == f19_targets[1].siteIdx) goto restart_40a8;
    if ((f19_targets[0].flags & 1) && f19_targets[1].kind == 1) goto restart_40a8;
    if ((f19_targets[1].flags & 1) && f19_targets[0].kind == 1) goto restart_40a8;
    if (range[0] < range[1] && !(gameData->flags3c & 2)) {
        tmpW = f19_targets[0].objIdx;
        f19_targets[0].objIdx = f19_targets[1].objIdx;
        f19_targets[1].objIdx = tmpW;
        tmpW = f19_targets[0].kind;
        f19_targets[0].kind = f19_targets[1].kind;
        f19_targets[1].kind = tmpW;
        tmpW = f19_targets[0].siteObj;
        f19_targets[0].siteObj = f19_targets[1].siteObj;
        f19_targets[1].siteObj = tmpW;
        tmpW = f19_targets[0].siteIdx;
        f19_targets[0].siteIdx = f19_targets[1].siteIdx;
        f19_targets[1].siteIdx = tmpW;
        tmpW = f19_targets[0].flags;
        f19_targets[0].flags = f19_targets[1].flags;
        f19_targets[1].flags = tmpW;
        tmpW = range[0];
        range[0] = range[1];
        range[1] = tmpW;
    }
    if ((f19_targets[1].flags & 2) && (f19_targets[1].flags & 2)) goto restart_40a8;
    if (f19_targets[1].kind == 5) goto restart_40a8;
    if (f19_targets[1].kind == 7) goto restart_40a8;
    if (f19_targets[1].kind == 6) goto restart_40a8;
    if (f19_targets[1].kind == 8) goto restart_40a8;
    if (f19_targets[0].kind == 4 && difficultySaved == 0) goto restart_40a8;
    if ((f19_targets[0].flags & 8) && f19_targets[1].kind == 1) goto restart_40a8;
    if ((f19_targets[1].flags & 8) && f19_targets[0].kind == 1) goto restart_40a8;
    if ((f19_targets[0].flags & 8) && (f19_targets[1].flags & 8)) goto restart_40a8;
    if (f19_targets[0].flags & 2)
        missionDistAccum =
            (f19_itemDistance(f19_targets[0].siteObj, f19_targets[0].objIdx) >> 4) + 0x1c2;
    if (f19_targets[1].flags & 2)
        missionDistAccum =
            (f19_itemDistance(f19_targets[0].siteObj, f19_targets[1].objIdx) >> 4) + 0x1c2;
    escortMissionFlag = -1;
    if (f19_siteParms[f19_targets[0].siteIdx].extra < 0)
        f19_flightUnits[0].planeType = -f19_siteParms[f19_targets[0].siteIdx].extra;
    if (f19_targets[0].kind == 5) {
        tmpW = 0x7fff;
        escortObj = -1;
        for (m2 = 0; m2 < worldObjectCount; m2++) {
            range[2] = abs16Compat(f19_itemDistance(f19_targets[0].objIdx, m2) - range[0]);
            if (range[2] < tmpW &&
                (f19_worldObjects[m2].targetFlags & 1) != 0 &&
                (f19_worldObjects[m2].targetFlags & 0x100) == 0) {
                escortObj = m2;
                tmpW = range[2];
            }
        }
        if (escortObj == -1) goto restart_40a8;
        f19_positionUnit(0, escortObj);
        f19_flightUnits[0].waypointIdx = f19_targets[0].objIdx;
        f19_flightUnits[0].flags |= 4;
        escortMissionFlag = 0;
        mystrcpy(briefCoord2, f19_getItemCoordStr(escortObj));
        missionDistAccum = f19_itemDistance(escortObj, f19_targets[0].objIdx) /
            ((f19_flightUnits[escortMissionFlag].maxSpeed >> 6) * 3);
    }
    if (f19_targets[0].kind == 7 || f19_targets[0].kind == 6) {
        tmpW = 0x7fff;
        for (m2 = 3; m2 < readItemSize; m2++) {
            register int16 f = f19_worldObjects[m2].targetFlags;
            if ((f & 0x500) == 0) continue;
            if ((f & 0xa00) != 0) continue;
            range[2] = abs16Compat(f19_itemDistance(f19_targets[0].objIdx, m2) - range[0]);
            if (range[2] >= tmpW) continue;
            if (m2 == f19_targets[0].siteObj) continue;
            escortObj = m2;
            tmpW = range[2];
        }
        mystrcpy(briefCoord2, f19_getItemCoordStr(escortObj));
        if (f19_targets[0].kind == 7) {
            f19_positionUnit(0, f19_targets[0].objIdx);
            f19_flightUnits[0].waypointIdx = escortObj;
            f19_flightUnits[0].flags |= 4;
            escortMissionFlag = 0;
            escortObj = f19_targets[0].objIdx;
        }
        else {
            f19_positionUnit(0, escortObj);
            f19_flightUnits[0].waypointIdx = f19_targets[0].objIdx;
            f19_flightUnits[0].flags |= 4;
            escortMissionFlag = 0;
            missionDistAccum = f19_itemDistance(escortObj, f19_targets[0].objIdx) /
                ((f19_flightUnits[escortMissionFlag].maxSpeed >> 6) * 2);
        }
    }
    if (f19_targets[0].kind == 8) {
        f19_positionUnit(0, f19_targets[0].objIdx);
        if (f19_flightUnits[0].planeType == 2 && theaterSaved == 1)
            f19_flightUnits[0].planeType = 0xc;
        f19_flightUnits[0].waypointIdx = f19_targets[0].objIdx;
        f19_flightUnits[0].flags |= 0x40;
        escortMissionFlag = 0;
        escortObj = f19_targets[0].objIdx;
    }
    if (escortMissionFlag == 0)
        f19_flightUnits[0].fuel = 0x4e1f;
    for (m2 = 0; m2 < 2; m2++) {
        mystrcpy(f19_targets[m2].name, f19_getItemCoordStr(f19_targets[m2].objIdx));
        if (f19_targets[m2].objIdx < 3) {
            tmpW = 0x7fff;
            for (sl = 3; sl < readItemSize; sl++) {
                if ((f19_worldObjects[sl].targetFlags & 0x500) == 0 &&
                    f19_itemDistance(sl, f19_targets[m2].objIdx) < tmpW &&
                    f19_worldObjects[sl].link != 0) {
                    tmpW = f19_itemDistance(sl, f19_targets[m2].objIdx);
                    f19_worldObjects[f19_targets[m2].objIdx].link = f19_worldObjects[sl].link;
                }
            }
        }
    }
    f19_targets[0].tail = missionDistAccum >> 4;
counterMore1k:
    tgtPreciseX = (int32)f19_worldObjects[f19_targets[0].siteObj].x_coord << 5;
    tgtPreciseY = (-((int32)f19_worldObjects[f19_targets[0].siteObj].y_coord - 0x8000) << 5)
                  - (int32)((f19_worldObjects[f19_targets[0].siteObj].targetFlags & 0x200) ?
                            0 : 0x708);
    missionMidX[2] = f19_worldObjects[f19_targets[0].objIdx].x_coord;
    missionMidX[3] = f19_worldObjects[f19_targets[0].objIdx].y_coord;
    missionMidX[0] = (f19_worldObjects[f19_targets[0].siteObj].x_coord / 2) +
                     (missionMidX[2] / 2);
    missionMidX[1] = (f19_worldObjects[f19_targets[0].siteObj].y_coord / 2) +
                     (missionMidX[3] / 2);
    missionMidX[6] = f19_worldObjects[f19_targets[1].siteObj].x_coord;
    missionMidX[7] = f19_worldObjects[f19_targets[1].siteObj].y_coord;
    missionMidX[4] = f19_worldObjects[f19_targets[1].objIdx].x_coord;
    missionMidX[5] = f19_worldObjects[f19_targets[1].objIdx].y_coord;
    if (f19_targets[0].flags & 0x10) {
        missionMidX[2] = ((missionMidX[2] >> 0xa) << 0xa) + 0x200;
        missionMidX[3] = ((missionMidX[3] >> 0xa) << 0xa) + 0x200;
    }
    for (m2 = 0; m2 < flightUnitCount - 4; m2++) {
        if ((int8)f19_flightUnits[m2].flags & 0x80) {
            rngLim = (range[0] / 4) * (4 - difficultySaved);
            if ((int8)f19_flightUnits[m2].flags & 0x40)
                rngLim = range[0] << 1;
            do {
                range[2] = randMul(worldObjectCount - 3) + 3;
            } while ((f19_worldObjects[range[2]].targetFlags & 0x100) ||
                     f19_rangeApprox(missionMidX[0] - f19_worldObjects[range[2]].x_coord,
                                 missionMidX[1] - f19_worldObjects[range[2]].y_coord) >
                     (rngLim += 0x10));
            f19_positionUnit(m2, range[2]);
            rngLim = 0x3000;
            baseBrg = f19_calcBearing(
                f19_worldObjects[f19_targets[0].siteObj].x_coord - f19_flightUnits[m2].x,
                f19_flightUnits[m2].y - f19_worldObjects[f19_targets[0].siteObj].y_coord);
            for (sl = 0; sl < 8; sl++) {
                waypt = randMul(worldObjectCount) + 1;
                if ((f19_worldObjects[waypt].targetFlags & 0x400) == 0) {
                    head = f19_calcBearing(
                        f19_worldObjects[waypt].x_coord - f19_flightUnits[m2].x,
                        f19_flightUnits[m2].y - f19_worldObjects[waypt].y_coord);
                    if (abs16Compat(baseBrg - head) < rngLim) {
                        rngLim = abs16Compat(baseBrg - head);
                        f19_flightUnits[m2].waypointIdx = waypt;
                        break;
                    }
                }
            }
        }
        if ((f19_flightUnits[m2].flags & 0x100) != 0 && escortMissionFlag != -1) {
            f19_positionUnit(m2, escortObj);
            f19_flightUnits[m2].fuel = 0x4e1f;
        }
        if (m2 != 0) {
            range[2] = 0;
            do {
                waypt = randMul(worldObjectCount - 3) + 3;
            } while (!((f19_worldObjects[waypt].targetFlags & 0x801) == 1 &&
                       f19_worldObjects[waypt].escortNum == 0) &&
                     range[2]++ < 20);
            f19_worldObjects[waypt].escortType = f19_flightUnits[m2].planeType;
            f19_worldObjects[waypt].escortNum = randMul(theaterSaved + 1) + 1;
        }
    }
    for (m2 = 0; m2 < groundUnitCount; m2++) {
        grType = f19_worldObjects[m2].unitType;
        if (grType != 0 && grType != 0x15) {
            switch (randMul(5) + (gameData->isCampaignMission != 0) +
                    difficultySaved) {
            case 0:
            case 1:
            case 3:
                grType = f19_linkTab[grType].nextB;
            case 2:
            case 4:
            case 6:
                break;
            case 5:
            case 7:
            case 8:
                grType = f19_linkTab[grType].nextA;
                break;
            }
            f19_worldObjects[m2].unitType = grType;
            if ((f19_worldObjects[m2].targetFlags & 8) != 0 &&
                gameData->isCampaignMission + difficultySaved + 2 < randMul(0xa))
                f19_worldObjects[m2].unitType = 0;
        }
    }
    for (randW = 0; randW < 0x10; randW++) {
        for (randY = 0; randY < 0x10; randY++) {
            if ((terrainGrid[randY + randW * 0x10] & 0x10) != 0 &&
                randMul(5) >= difficultySaved)
                terrainGrid[randY + randW * 0x10] &= 0xef;
        }
    }
    commData->missionKind[0] = 1;
    commData->missionKind[2] = randMul(2) ? 5 : 9;
    commData->missionKind[3] = 0;
    commData->missionKind[1] = 2;
    commData->missionRange = mDist << 4;
    if (mDist * 16 > 0x2710)
        commData->missionKind[2] = 0x11;
    have = 0;
    for (m2 = 0; m2 < 2; m2++) {
        weap = -1;
        if (f19_targets[m2].kind == 1 && have == 0) {
            weap = 0x10;
            have = 1;
        }
        if (f19_targets[m2].kind == 4 || f19_targets[m2].kind == 3)
            weap = 0x13;
        if (f19_targets[m2].kind == 2) {
            do {
                weap = randMul(0x10);
            } while (loadoutTab[weap * 13 +
                ((int8)wldReadBuf7[
                    f19_worldObjects[f19_targets[m2].objIdx].objectIdx & 0x7f] &
                 0xf)] < 4);
        }
        if (weap != -1)
            commData->missionKind[m2] = weap;
    }
    if ((gameData->flags3c & 2) && theaterSaved == 0) {
        if (gameData->flags3c & 1) {
            commData->missionKind[0] = 5;
            commData->missionKind[2] = 1;
        }
        else {
            commData->missionKind[2] = 5;
            commData->missionKind[0] = 1;
        }
        commData->missionKind[3] = 0;
        if (mDist * 16 > 0x2710)
            commData->missionKind[3] = 0x11;
    }
    for (m2 = 0; m2 < 4; m2++)
        commData->missionStat[m2] =
            missionSpeedTab[commData->missionKind[m2] * 13];
    mKind = f19_targets[0].siteIdx + f19_targets[1].siteIdx;
    missionTimeFlag = ((uint8)mKind & 3) == 0;
    mKind = (mKind & 0xf) << 8;
    if (f19_targets[0].kind == 1 || f19_targets[1].kind == 1)
        missionTimeFlag = 0;
    if (f19_targets[0].kind == 4 || f19_targets[1].kind == 4)
        missionTimeFlag = 1;
    f19_formatTimeStr(briefTimeA, mKind);
    f19_formatTimeStr(briefTimeB, mKind + missionDistAccum);
    f19_formatTimeStr(briefTimeC, mKind + missionDistAccum + 0x1c2);
    missionDistAccum -= (mKind + missionDistAccum) % 0x96;
}
