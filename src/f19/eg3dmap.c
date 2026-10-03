/* seg000 routines — 3D map/model decode (ported, verified vs original) */
#include "f19.h"

extern int16 g_modelEdgeCount;
extern int16 g_vtxSignMaskLo;
extern int16 g_vtxSignMaskHi;
extern int8 g_modelWideVtxFlag;
extern int16 g_mapOriginX;
extern int16 g_mapOriginY;
extern int16 g_mapLodIndex;
extern int16 g_curLod;
extern int16 g_modelEvenOddBit;
extern int16 g_tileZoomShift;
extern int16 g_tileWorldSize;
extern int16 g_tileGridDim;
extern const int16 g_mapTileLodTable[5];
extern struct TileSceneObject *matrix3dt_2[5][32];
extern uint16 matrix3dt[5][32];
extern struct TileSceneObject *g_curTileEntry;
extern const uint16 buf3d3[];
extern int16 g_viewCenterY2;
extern int16 g_viewCenterX;
extern int16 g_clipMaxX;
extern int16 g_clipMaxY;
#define vtxScratch (*(struct VtxScratch *)(f19_dseg + 0xACC))
extern char FAR*g_modelStreamPtr;
extern int16 g_modelVtxCount;
#define buf3d3_1 ((uint8 *)(f19_dseg + 0x857E))
#define buf3d3_2 ((uint8 *)(f19_dseg + 0x857E))
#define g_modelVtxXTab ((int16 *)(f19_dseg + 0x946A))
#define g_modelVertY ((int16 *)(f19_dseg + 0x946A))
extern int16 g_objDistance;
#define nearestTile (*(struct TileObject *)(f19_dseg + 0x8E4C))
#define g_neighborSampling (*(struct NeighborSampling *)(f19_dseg + 0x5BC))
#define g_shapeTargetCategory ((uint8 *)(f19_dseg + 0x95F0))
#define g_dynTileEntries ((struct DynTileOverride *)(f19_dseg + 0x8B56))
extern int16 g_tileEntryIdx;
extern int16 g_render3DTiles;
extern int16 g_viewPosX;
extern int16 g_viewPosY;
extern int16 g_viewPosZ;
extern int16 g_posVisibleFlag;
extern int16 g_objRelX;
extern int16 g_objRelY;
extern int16 g_objTransform[];
extern int16 g_objRenderMode;
extern int8 g_objHasRotation;
extern struct Proj3d g_proj3d;
extern int16 g_objLocalX;
extern int16 g_objLocalY;
extern int16 g_objColorBase;
extern const int16 g_lodObjectCount[];
extern const int16 g_dirGridOffsets[];
extern int16 g_detailLevel;
extern int16 g_lodGridDim[];
extern uint8 g_topLodGrid[];
extern uint8 buf1_3dg[];
extern uint8 buf2_3dg[];
extern uint8 buf3_3dg[];
extern uint8 buf4_3dg[];
extern int16 g_tileEntryCount;

extern char *g_modelStreamPtr;        /* dword_2F8F8 — real object (ptr cell) */

/* ==== seg000:0x1846 ==== */
void f19_buildVertexSignMask(int16 sx, int16 sy) {
    int32 bit;
    int16 edgeIdx;

    bit = 1L;
    g_modelEdgeCount = (int16)(uint8)(*((*(char far **)&g_modelStreamPtr)++)) & 0x1f;
    g_vtxSignMaskLo = -1;
    g_vtxSignMaskHi = -1;
    *(char *)&g_modelWideVtxFlag = (g_modelEdgeCount > 16) ? 1 : 0;
    edgeIdx = 0;
    while (edgeIdx < g_modelEdgeCount) {
        g_modelStreamPtr += 4;
        if (*(*(int16 far **)&g_modelStreamPtr)++ < 0) {
            *(int32 *)&g_vtxSignMaskLo ^= bit;
        }
        g_modelStreamPtr += 2;
        bit <<= 1;
        edgeIdx++;
    }
}

#pragma pack(1)
struct TileSceneObject {
    int16 x, y, z;
    uint8 shape;
};
#pragma pack()

extern char far g_world3dData[];  /* UNMAPPED */

void f19_computeTileBounds(int16 *minX, int16 *maxX, int16 *minY, int16 *maxY);
int16 f19_process3dg(int16 lod, int16 col, int16 row);
void f19_drawMapTileObject(char far *modelData, int16 screenX, int16 screenY);

/* ==== seg000:0x1564 ==== */
void f19_drawMapTiles(int16 originX, int16 originY, int16 zoomShift) {
    int16 maxTileY, screenY, minTileX, minTileY, subIdx, col, row, cell, maxTileX, screenX;

    g_mapOriginX = originX >> (char)zoomShift;
    g_mapOriginY = originY >> (char)zoomShift;
    for (g_mapLodIndex = 4; g_mapLodIndex >= 0; g_mapLodIndex--) {
        g_curLod = g_mapTileLodTable[g_mapLodIndex];
        g_modelEvenOddBit = (g_mapLodIndex <= 1) ? 0x40 : 0;
        g_tileZoomShift = zoomShift - g_curLod * 2 + 8;
        g_tileWorldSize = 0x1000 >> (char)g_tileZoomShift;
        if (g_tileWorldSize > 16) {
            g_tileGridDim = 4 << (8 - (char)g_curLod * 2);
            f19_computeTileBounds(&minTileX, &maxTileX, &minTileY, &maxTileY);
            for (row = minTileY; row <= maxTileY; row++) {
                for (col = minTileX; col <= maxTileX; col++) {
                    screenX = col * g_tileWorldSize - g_mapOriginX + (g_tileWorldSize >> 1);
                    screenY = row * g_tileWorldSize - g_mapOriginY + (g_tileWorldSize >> 1);
                    cell = f19_process3dg(g_curLod, col, row);
                    if (cell != -1) {
                        g_curTileEntry = matrix3dt_2[g_curLod][cell];
                        for (subIdx = 0; matrix3dt[g_curLod][cell] > subIdx; subIdx++) {
                            if (g_curTileEntry->z == 0) {
                                g_modelStreamPtr = (char far *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                                f19_drawMapTileObject(g_modelStreamPtr,
                                                  (g_curTileEntry->x >> (char)g_tileZoomShift) + screenX,
                                                  (g_curTileEntry->y >> (char)g_tileZoomShift) + screenY);
                            }
                            g_curTileEntry++;
                        }
                    }
                }
            }
        }
    }
}


/* ==== seg000:0x174c ==== */
void f19_worldToTileIndex(int16 worldX, int16 worldY, int16 *outCol, int16 *outRow) {
    *outCol = (worldX - g_viewCenterX + g_mapOriginX) / g_tileWorldSize;
    *outRow = ((worldY - g_viewCenterY2) * 4 / 3 + g_mapOriginY) / g_tileWorldSize;
}

/* ==== seg000:0x16f0 ==== */
void f19_computeTileBounds(int16 *minTileX, int16 *maxTileX, int16 *minTileY, int16 *maxTileY) {
    f19_worldToTileIndex(0, 0, minTileX, minTileY);
    if (*minTileX < 0)
        *minTileX = 0;
    if (*minTileY < 0)
        *minTileY = 0;
    f19_worldToTileIndex(g_clipMaxX, g_clipMaxY, maxTileX, maxTileY);
    if (*maxTileX >= g_tileGridDim)
        *maxTileX = g_tileGridDim - 1;
    if (*maxTileY >= g_tileGridDim)
        *maxTileY = g_tileGridDim - 1;
}

/* ==== seg000:0x18ce ==== */
#pragma pack(1)
struct VertexProj {
    struct { int16 num; int16 div; } in[121];
    union { int32 v[121]; int16 lo; } x;
    union { int32 v[121]; int16 lo; } y;
    uint8 scratch[3784];
};
struct VtxScratch { uint8 dictHead[0x404]; struct VertexProj vproj; };
#pragma pack()
/* ==== seg000:0x19da ==== */
int16 f19_aspectScaleY(int16 y) {
    return y - (y >> 2);
}

void f19_projectModelVertices(int16 screenX, int16 screenY) {
    int16 vtxIdx, vtxRef, packed, screenVtxX, screenVtxY;
    packed = (int16)(uint8) * *(char FAR **)&g_modelStreamPtr & 0x80;
    g_modelVtxCount = (int16)(uint8)(*(*(char FAR **)&g_modelStreamPtr)++) & 0x7F;
    for (vtxIdx = 0; vtxIdx < g_modelVtxCount; vtxIdx++) {
        g_modelStreamPtr += (uint8)g_modelWideVtxFlag * 2 + 2;
        if (packed != 0) {
            vtxRef = (int16)(uint8)(*(*(char FAR **)&g_modelStreamPtr)++);
            screenVtxX = (g_modelVtxXTab[buf3d3_1[vtxRef]] >> g_tileZoomShift) + screenX;
            screenVtxY = (g_modelVertY[buf3d3_2[vtxRef]] >> g_tileZoomShift) + screenY;
        } else {
            screenVtxX = (*(*(int16 FAR **)&g_modelStreamPtr)++ >> g_tileZoomShift) + screenX;
            screenVtxY = (*(*(int16 FAR **)&g_modelStreamPtr)++ >> g_tileZoomShift) + screenY;
            g_modelStreamPtr += 2;
        }
        vtxScratch.vproj.in[vtxIdx].num = 1;
        vtxScratch.vproj.in[vtxIdx].div = 1;
        vtxScratch.vproj.x.v[vtxIdx] = screenVtxX + g_viewCenterX;
        vtxScratch.vproj.y.v[vtxIdx] = -f19_aspectScaleY(screenVtxY) + g_viewCenterY2;
    }
}

/* ==== seg000:0x178a ==== */
extern void drawModelPoint();              /* sub_11802 (K&R: called w/o args at 0x3f) */
extern void far advanceModelPointerLod(void);   /* sub_208EC */
extern void far projectModelEdgesFar(void);     /* sub_2105A */
extern void far drawModelDisplayList(void);     /* sub_215F0 */

void f19_drawMapTileObject(char FAR *modelData, int16 screenX, int16 screenY) {
    *(char FAR **)&g_modelStreamPtr = modelData;
    g_modelStreamPtr++;
    g_objDistance = 0;
    advanceModelPointerLod();
    if (g_curLod >= 3) {
        if ((**(char FAR **)&g_modelStreamPtr & 0x40) != g_modelEvenOddBit)
            return;
    }
    switch ((uint16)(uint8) * *(char FAR **)&g_modelStreamPtr & 0x3f) {
    case 0x3e:
        return;
    case 0x3f:
        drawModelPoint();
        return;
    }
    f19_buildVertexSignMask(screenX, screenY);
    f19_projectModelVertices(screenX, screenY);
    projectModelEdgesFar();
    drawModelDisplayList();
}

/* ==== seg000:0x1092 ==== */
#pragma pack(1)
struct TileObject {
    int16 id;                      /* +0x00 */
    int16 dist;                    /* +0x02 */
    int32 x;                       /* +0x04 */
    int32 y;                       /* +0x08 */
    struct TileSceneObject *entry; /* +0x0C */
    uint8 lod;                     /* +0x0E */
    uint8 subIndex;                /* +0x0F */
    uint8 tileX;                   /* +0x10 */
    uint8 tileY;                   /* +0x11 */
    int16 shapeOff;                /* +0x12 */
    uint8 flag;                    /* +0x14 */
    uint8 pad15;                   /* +0x15 */
};
#pragma pack()
#pragma pack(1)
struct NeighborSampling {
    int16 gridX[9];
    int16 gridY[11];
    int16 lut[3];
};
#pragma pack()
#pragma pack(1)
struct DynTileOverride {
    uint8 lod;       /* +0x00 */
    uint8 subIndex;  /* +0x01 */
    uint8 tileX;     /* +0x02 */
    uint8 tileY;     /* +0x03 */
    int16 value;     /* +0x04 */
    uint8 shape;     /* +0x06 */
    uint8 pad7;
};
#pragma pack()
extern uint32 f19_scaleCoordToLod(int16, uint32);         /* sub_10918 */
extern int16 f19_process3dg(int16, int16, int16);         /* sub_1099A */
extern int16 f19_lookupTileEntry(int16, int16, int16, int16); /* sub_1131E */
extern int16 abs(int16);

struct TileObject *f19_findNearestTileObject(uint32 worldX, uint32 worldY) {
    /* Single-letter names are load-bearing: MSC 5.1 hashes each name to a
       fixed stack slot; this 18-local frame only byte-matches with this set. */
    int16 p, q, a, r, b, c, d, e, f, g, h, i, j, k, l, m, n, o;

    nearestTile.dist = 0x7fff;
    for (c = 1; c <= 2; c++) {
        for (e = 0; e < 9; e++) {
            *(int32 *)&m = f19_scaleCoordToLod(c, worldX);
            i = *(uint32 *)&m >> 0xc;
            r = m & 0xfff;
            *(int32 *)&m = f19_scaleCoordToLod(c, worldY);
            k = *(uint32 *)&m >> 0xc;
            d = m & 0xfff;
            a = g_neighborSampling.gridX[e];
            b = g_neighborSampling.gridY[e];
            o = g_neighborSampling.lut[a] - r + 0x800;
            p = g_neighborSampling.lut[b] - d + 0x800;
            n = f19_process3dg(c, i += a, k += b);
            if (n != -1) {
                g_curTileEntry = matrix3dt_2[c][n];
                for (f = 0; matrix3dt[c][n] > f; f++) {
                    if (g_shapeTargetCategory[g_curTileEntry->shape & 0x7f] != 0) {
                        h = o + g_curTileEntry->x;
                        j = g_curTileEntry->y + p;
                        q = abs(h) + abs(j);
                        if (c == 1) {
                            q >>= 2;
                        } else {
                            h <<= 2;
                            j <<= 2;
                        }
                        g = g_curTileEntry->shape;
                        if ((g_curTileEntry->shape & 0x80) != 0 &&
                            f19_lookupTileEntry(c, f, i, k) != 0) {
                            g = g_dynTileEntries[g_tileEntryIdx].shape;
                        }
                        if (q < nearestTile.dist) {
                            g_modelStreamPtr = (char *)(g_world3dData + buf3d3[g]);
                            if (*(int16 FAR *)g_modelStreamPtr != 0 ||
                                *((char FAR *)g_modelStreamPtr + 2) != 0 ||
                                g_render3DTiles != 0) {
                                nearestTile.lod = (uint8)c;
                                nearestTile.subIndex = (uint8)f;
                                nearestTile.tileX = (uint8)i;
                                nearestTile.tileY = (uint8)k;
                                nearestTile.entry = g_curTileEntry;
                                nearestTile.id = g;
                                nearestTile.dist = q;
                                nearestTile.x = worldX + (int32)h;
                                nearestTile.y = worldY + (int32)j;
                            }
                        }
                    }
                    g_curTileEntry++;
                }
            }
        }
    }
    if (nearestTile.dist != 0x7fff) {
        return &nearestTile;
    }
    return 0;
}

/* ==== seg000:0x1372 ==== */
extern void far rotatePoint3dFar(void);             /* sub_20A5C */

void f19_drawNearestTileObject(uint32 coord1, uint32 coord2, uint32 coord3) {
    int16 yOff, fracX, lod, fracY, subIdx, relX, relY, tileX, tileY, cell, xOff;
    uint32 scaled;

    *(char *)&g_posVisibleFlag = 0;
    nearestTile.dist = 0x7fff;
    lod = 4;
    scaled = f19_scaleCoordToLod(lod, coord1);
    tileX = (int16)(scaled >> 12);
    fracX = (int16)scaled & 0xfff;
    scaled = f19_scaleCoordToLod(lod, coord2);
    tileY = (int16)(scaled >> 12);
    fracY = (int16)scaled & 0xfff;
    g_viewPosZ = (int16)f19_scaleCoordToLod(lod, coord3);
    xOff = 0x800 - fracX;
    yOff = 0x800 - fracY;
    g_viewPosX = fracX - 0x800;
    g_viewPosY = fracY - 0x800;
    cell = f19_process3dg(lod, tileX, tileY);
    if (cell != -1) {
        g_curTileEntry = matrix3dt_2[lod][cell];
        for (subIdx = 1; subIdx < matrix3dt[lod][cell]; subIdx++) {
            relX = g_curTileEntry->x + xOff;
            relY = g_curTileEntry->y + yOff;
            g_objDistance = abs(relX) + abs(relY);
            if (nearestTile.dist > g_objDistance) {
                nearestTile.entry = g_curTileEntry;
                nearestTile.dist = g_objDistance;
            }
            g_curTileEntry++;
        }
    }
    if (nearestTile.dist != 0x7fff) {
        g_curTileEntry = nearestTile.entry;
        g_modelStreamPtr = (char *)(g_world3dData + buf3d3[nearestTile.entry->shape]);
        g_objRelX = g_curTileEntry->x - g_viewPosX;
        g_objRelY = g_curTileEntry->y - g_viewPosY;
        g_objTransform[0] = g_curTileEntry->z - g_viewPosZ;
        g_modelStreamPtr++;
        *(uint8 *)&g_objRenderMode = 0;
        g_objDistance = 0;
        advanceModelPointerLod();
        if (*g_modelStreamPtr & 0x40) {
            g_objHasRotation = 0;
            rotatePoint3dFar();
        }
    }
}

/* ==== seg000:0x0522 ==== */
#pragma pack(1)
struct Proj3d {
    int32 x;         /* word_38D20 */
    int32 y;         /* word_38D24 */
    int16 w;         /* word_38D28 — written by _main, not read here */
    int32 z;         /* word_38D2A */
};
#pragma pack()
extern void setViewPosition(int16, int16, int16);   /* sub_11B56 (eg3dview) */
int16 far transformAndCullObjectFar(int16, int16, int16); /* sub_208D9 (seg001:0x9e9) */
int16 FAR projectSceneObject(uint8 FAR *model, int16 yaw, int16 pitch, int16 roll,
                             int16 relX, int16 relY, int16 flag);   /* sub_20716 */

void f19_projectObjects(int16 heading, int16 rangeGate, int32 worldX, int32 worldY, int32 worldZ) {
    int16 gridX, gridY, dirSector, fracX, subIdx, fracY, sampleIdx, tmp0, tileX, tileY, tmp1, cell;
    int32 scaled;

    g_proj3d.x = worldX;
    g_proj3d.y = worldY;
    g_proj3d.z = worldZ;
    worldX = g_proj3d.x;
    worldY = g_proj3d.y;
    worldZ = g_proj3d.z;
    dirSector = (uint16)(-heading + 0x1000) >> 13;
    g_curLod = (g_detailLevel != 0) ? 4 : 3;
    goto outer_test;
    do {
        g_curLod--;
    outer_test:
        if (g_curLod < 1) {
            return;
        }
        if (g_lodObjectCount[g_curLod] == 0) {
            continue;
        }
        scaled = f19_scaleCoordToLod(g_curLod, worldX);
        tileX = (uint32)scaled >> 12;
        fracX = (int16)scaled & 0xfff;
        scaled = f19_scaleCoordToLod(g_curLod, worldY);
        tileY = (uint32)scaled >> 12;
        fracY = (int16)scaled & 0xfff;
        scaled = f19_scaleCoordToLod(g_curLod, worldZ);
        if ((uint32)scaled < 0x7FFFUL) {
            g_tileWorldSize = (int16)(((uint32)scaled < 2UL) ? 2UL : (uint32)scaled);
            for (sampleIdx = 0;; sampleIdx++) {
                if (g_curLod == 4 && g_detailLevel >= 2) {
                    if (sampleIdx == 15) {
                        break;
                    }
                    gridX = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (uint16)18 * (uint16)dirSector);
                    gridY = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (uint16)18 * (uint16)((dirSector + 2) & 7));
                    g_objLocalX = fracX - (gridX << 12) - 0x800;
                    g_objLocalY = fracY - (gridY << 12) - 0x800;
                    g_objRenderMode = 7;
                    if (transformAndCullObjectFar(-g_objLocalX, -g_objLocalY, -g_tileWorldSize) != 0) {
                        goto next_iter;
                    }
                } else {
                    if (sampleIdx == 9) {
                        break;
                    }
                    if (g_curLod != 4 && g_detailLevel < 2 && sampleIdx < 4) {
                        goto next_iter;
                    }
                    if (rangeGate < (int16)0xd555) {
                        gridX = g_neighborSampling.gridX[sampleIdx];
                        gridY = g_neighborSampling.gridY[sampleIdx];
                    } else {
                        gridX = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (uint16)18 * (uint16)dirSector);
                        gridY = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (uint16)18 * (uint16)((dirSector + 2) & 7));
                    }
                    g_objLocalX = fracX - (gridX << 12) - 0x800;
                    g_objLocalY = fracY - (gridY << 12) - 0x800;
                }
                setViewPosition(g_objLocalX, g_objLocalY, g_tileWorldSize);
                cell = f19_process3dg(g_curLod, tileX + gridX, tileY + gridY);
                if (cell == -1) {
                    goto next_iter;
                }
                if (sampleIdx >= 4 || g_detailLevel >= 2) {
                    g_objColorBase = (g_detailLevel >= 2) ? 0 : ((uint8)g_curLod << 8);
                    g_curTileEntry = matrix3dt_2[g_curLod][cell];
                    for (subIdx = 0; matrix3dt[g_curLod][cell] > subIdx; subIdx++) {
                        if (g_curTileEntry->shape & 0x80) {
                            g_modelStreamPtr = (char *)(g_world3dData + f19_lookupTileEntry(g_curLod, subIdx, tileX + gridX, tileY + gridY));
                            if (g_modelStreamPtr == (char FAR *)g_world3dData) {
                                g_modelStreamPtr = (char *)(g_world3dData + buf3d3[g_curTileEntry->shape & 0x7f]);
                            }
                        } else {
                            g_modelStreamPtr = (char *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                        }
                        projectSceneObject((uint8 far *)g_modelStreamPtr, 0, 0, 0,
                                           g_curTileEntry->x,
                                           g_curTileEntry->y,
                                           g_curTileEntry->z);
                        g_curTileEntry++;
                        g_objColorBase++;
                    }
                } else {
                    if (g_curLod == 4) {
                        g_curTileEntry = matrix3dt_2[g_curLod][cell];
                        g_modelStreamPtr = (char *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                        g_objColorBase = 0x400;
                        projectSceneObject((uint8 far *)g_modelStreamPtr, 0, 0, 0,
                                           g_curTileEntry->x,
                                           g_curTileEntry->y,
                                           g_curTileEntry->z);
                    }
                }
            next_iter:;
            }
        }
    } while (1);
}

/* ==== seg000:0x918 ==== */
uint32 f19_scaleCoordToLod(int16 level, uint32 coord) {
    switch (level) {
    case 4:
        return (coord + 0x20) >> 6;
    case 3:
        return (coord + 8) >> 4;
    case 2:
        return (coord + 2) >> 2;
    case 1:
        return coord;
    case 0:
        return coord << 1;
    }
}

/* ==== seg000:0x99a ==== */
int16 f19_process3dg(int16 lod, int16 col, int16 row) {
    if (col < 0 || row < 0 || col >= g_lodGridDim[lod] || row >= g_lodGridDim[lod]) {
        return -1;
    }
    switch (lod) {
    case 4:
        return g_topLodGrid[col + (row << 2)];
    case 3:
        return buf1_3dg[col + (row << 4)];
    case 2:
        return buf2_3dg[(col & 3) + ((row & 3) << 2) + (f19_process3dg(3, col >> 2, row >> 2) << 4)];
    case 1:
        return buf3_3dg[(col & 3) + ((row & 3) << 2) + (f19_process3dg(2, col >> 2, row >> 2) << 4)];
    case 0:
        return buf4_3dg[(col & 3) + ((row & 3) << 2) + (f19_process3dg(1, col >> 2, row >> 2) << 4)];
    }
}

/* ==== seg000:0x131e ==== */

int16 f19_lookupTileEntry(int16 lod, int16 subIndex, int16 tileX, int16 tileY) {
    for (g_tileEntryIdx = g_tileEntryCount - 1; g_tileEntryIdx >= 0; g_tileEntryIdx--) {
        if (g_dynTileEntries[g_tileEntryIdx].lod == lod &&
            g_dynTileEntries[g_tileEntryIdx].subIndex == subIndex &&
            g_dynTileEntries[g_tileEntryIdx].tileX == tileX &&
            g_dynTileEntries[g_tileEntryIdx].tileY == tileY) {
            return g_dynTileEntries[g_tileEntryIdx].value;
        }
    }
    return 0;
}

/* ==== seg000:0x12dc ==== */
extern void *memcpy(void *, const void *, int);

void f19_addTileEntry(struct TileObject *rec, int16 value, char tag) {
    rec->shapeOff = value;
    rec->flag = tag;
    memcpy(&g_dynTileEntries[g_tileEntryCount++], &rec->lod, 8);
    rec->entry->shape |= 0x80;
}
