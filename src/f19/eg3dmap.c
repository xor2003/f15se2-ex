#include "f19eg.h"

/* seg000 routines — 3D map/model decode (ported, verified vs original) */


/* ==== seg000:0x1846 ==== */
void buildVertexSignMask(void) {
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


void computeTileBounds(int16 *minX, int16 *maxX, int16 *minY, int16 *maxY);
int16 process3dg(int16 lod, int16 col, int16 row);
void drawMapTileObject(char far *modelData, int16 screenX, int16 screenY);

/* ==== seg000:0x1564 ==== */
void drawMapTiles(int16 originX, int16 originY, int16 zoomShift) {
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
            computeTileBounds(&minTileX, &maxTileX, &minTileY, &maxTileY);
            for (row = minTileY; row <= maxTileY; row++) {
                for (col = minTileX; col <= maxTileX; col++) {
                    screenX = col * g_tileWorldSize - g_mapOriginX + (g_tileWorldSize >> 1);
                    screenY = row * g_tileWorldSize - g_mapOriginY + (g_tileWorldSize >> 1);
                    cell = process3dg(g_curLod, col, row);
                    if (cell != -1) {
                        g_curTileEntry = (struct TileSceneObject *)(f19_dseg + matrix3dt_2[g_curLod][cell]);
                        for (subIdx = 0; matrix3dt[g_curLod][cell] > subIdx; subIdx++) {
                            if (g_curTileEntry->z == 0) {
                                g_modelStreamPtr = (char far *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                                drawMapTileObject(g_modelStreamPtr,
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
void worldToTileIndex(int16 worldX, int16 worldY, int16 *outCol, int16 *outRow) {
    *outCol = (worldX - g_viewCenterX + g_mapOriginX) / g_tileWorldSize;
    *outRow = ((worldY - g_viewCenterY2) * 4 / 3 + g_mapOriginY) / g_tileWorldSize;
}

/* ==== seg000:0x16f0 ==== */
void computeTileBounds(int16 *minTileX, int16 *maxTileX, int16 *minTileY, int16 *maxTileY) {
    worldToTileIndex(0, 0, minTileX, minTileY);
    if (*minTileX < 0)
        *minTileX = 0;
    if (*minTileY < 0)
        *minTileY = 0;
    worldToTileIndex(g_clipMaxX, g_clipMaxY, maxTileX, maxTileY);
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
int16 aspectScaleY(int16 y) {
    return y - (y >> 2);
}

void projectModelVertices(int16 screenX, int16 screenY) {
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
        vtxScratch.vproj.y.v[vtxIdx] = -aspectScaleY(screenVtxY) + g_viewCenterY2;
    }
}

/* ==== seg000:0x178a ==== */
extern void drawModelPoint(int16, int16);              /* sub_11802 (K&R: called w/o args at 0x3f) */
extern void far advanceModelPointerLod(void);   /* sub_208EC */
extern void far projectModelEdgesFar(void);     /* sub_2105A */
extern void far drawModelDisplayList(void);     /* sub_215F0 */

void drawMapTileObject(char FAR *modelData, int16 screenX, int16 screenY) {
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
        drawModelPoint(screenX, screenY);
        return;
    }
    buildVertexSignMask();
    projectModelVertices(screenX, screenY);
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
extern uint32 scaleCoordToLod(int16, uint32);         /* sub_10918 */
extern int16 process3dg(int16, int16, int16);         /* sub_1099A */
extern int16 lookupTileEntry(int16, int16, int16, int16); /* sub_1131E */


struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY) {
    /* Single-letter names are load-bearing: MSC 5.1 hashes each name to a
       fixed stack slot; this 18-local frame only byte-matches with this set. */
    int16 p, q, a, r, b, c, d, e, f, g, h, i, j, k, l, m, n, o;

    nearestTile.dist = 0x7fff;
    for (c = 1; c <= 2; c++) {
        for (e = 0; e < 9; e++) {
            *(int32 *)&m = scaleCoordToLod(c, worldX);
            i = *(uint32 *)&m >> 0xc;
            r = m & 0xfff;
            *(int32 *)&m = scaleCoordToLod(c, worldY);
            k = *(uint32 *)&m >> 0xc;
            d = m & 0xfff;
            a = g_neighborSampling.gridX[e];
            b = g_neighborSampling.gridY[e];
            o = g_neighborSampling.lut[a] - r + 0x800;
            p = g_neighborSampling.lut[b] - d + 0x800;
            n = process3dg(c, i += a, k += b);
            if (n != -1) {
                g_curTileEntry = (struct TileSceneObject *)(f19_dseg + matrix3dt_2[c][n]);
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
                            lookupTileEntry(c, f, i, k) != 0) {
                            g = g_dynTileEntries[g_tileEntryIdx].shape;
                        }
                        if (q < nearestTile.dist) {
                            g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[g]);
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

void drawNearestTileObject(uint32 coord1, uint32 coord2, uint32 coord3) {
    int16 yOff, fracX, lod, fracY, subIdx, relX, relY, tileX, tileY, cell, xOff;
    uint32 scaled;

    *(char *)&g_posVisibleFlag = 0;
    nearestTile.dist = 0x7fff;
    lod = 4;
    scaled = scaleCoordToLod(lod, coord1);
    tileX = (int16)(scaled >> 12);
    fracX = (int16)scaled & 0xfff;
    scaled = scaleCoordToLod(lod, coord2);
    tileY = (int16)(scaled >> 12);
    fracY = (int16)scaled & 0xfff;
    g_viewPosZ = (int16)scaleCoordToLod(lod, coord3);
    xOff = 0x800 - fracX;
    yOff = 0x800 - fracY;
    g_viewPosX = fracX - 0x800;
    g_viewPosY = fracY - 0x800;
    cell = process3dg(lod, tileX, tileY);
    if (cell != -1) {
        g_curTileEntry = (struct TileSceneObject *)(f19_dseg + matrix3dt_2[lod][cell]);
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
        g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[nearestTile.entry->shape]);
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
int far transformAndCullObjectFar(int, int, int); /* sub_208D9 (seg001:0x9e9) */
void far projectSceneObject(char far *model, int yaw, int pitch, int roll,
                           int relX, int relY, int flag);   /* sub_20716 */

void projectObjects(int16 heading, int16 rangeGate, int32 worldX, int32 worldY, int32 worldZ) {
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
        scaled = scaleCoordToLod(g_curLod, worldX);
        tileX = (uint32)scaled >> 12;
        fracX = (int16)scaled & 0xfff;
        scaled = scaleCoordToLod(g_curLod, worldY);
        tileY = (uint32)scaled >> 12;
        fracY = (int16)scaled & 0xfff;
        scaled = scaleCoordToLod(g_curLod, worldZ);
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
                cell = process3dg(g_curLod, tileX + gridX, tileY + gridY);
                { static int dbg=-1; if(dbg<0)dbg=getenv("F19_DBG")?1:0;
                  if(dbg && cell!=-1) fprintf(stderr,"pobj lod=%d cell=%d n=%d tx=%d ty=%d\n",(int)g_curLod,(int)cell,(int)matrix3dt[g_curLod][cell],(int)(tileX+gridX),(int)(tileY+gridY)); }
                if (cell == -1) {
                    goto next_iter;
                }
                if (sampleIdx >= 4 || g_detailLevel >= 2) {
                    g_objColorBase = (g_detailLevel >= 2) ? 0 : ((uint8)g_curLod << 8);
                    g_curTileEntry = (struct TileSceneObject *)(f19_dseg + matrix3dt_2[g_curLod][cell]);
                    for (subIdx = 0; matrix3dt[g_curLod][cell] > subIdx; subIdx++) {
                        if (g_curTileEntry->shape & 0x80) {
                            g_modelStreamPtr = (char FAR *)(g_world3dData + lookupTileEntry(g_curLod, subIdx, tileX + gridX, tileY + gridY));
                            if (g_modelStreamPtr == (char FAR *)g_world3dData) {
                                g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[g_curTileEntry->shape & 0x7f]);
                            }
                        } else {
                            g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                        }
                        projectSceneObject((char *)g_modelStreamPtr, 0, 0, 0,
                                           g_curTileEntry->x,
                                           g_curTileEntry->y,
                                           g_curTileEntry->z);
                        g_curTileEntry++;
                        g_objColorBase++;
                    }
                } else {
                    if (g_curLod == 4) {
                        g_curTileEntry = (struct TileSceneObject *)(f19_dseg + matrix3dt_2[g_curLod][cell]);
                        g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                        g_objColorBase = 0x400;
                        projectSceneObject((char *)g_modelStreamPtr, 0, 0, 0,
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
uint32 scaleCoordToLod(int16 level, uint32 coord) {
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

int16 process3dg(int16 lod, int16 col, int16 row) {
    if (lod == 4) {
        col += 2;
        row += 2;
    }
    if (col < 0 || row < 0 || col >= g_lodGridDim[lod] || row >= g_lodGridDim[lod]) {
        return 0;
    }
    switch (lod) {
    case 4:
        return g_topLodGrid[col + (row << 3)];
    case 3:
        return buf1_3dg[col + (row << 4)];
    case 2:
        return buf2_3dg[(col & 3) + ((row & 3) << 2) + (process3dg(3, col >> 2, row >> 2) << 4)];
    case 1:
        return buf3_3dg[(col & 3) + ((row & 3) << 2) + (process3dg(2, col >> 2, row >> 2) << 4)];
    case 0:
        return buf4_3dg[(col & 3) + ((row & 3) << 2) + (process3dg(1, col >> 2, row >> 2) << 4)];
    }
}

/* ==== seg000:0x131e ==== */

int16 lookupTileEntry(int16 lod, int16 subIndex, int16 tileX, int16 tileY) {
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


void addTileEntry(struct TileObject *rec, int16 value, char tag) {
    rec->shapeOff = value;
    rec->flag = tag;
    memcpy(&g_dynTileEntries[g_tileEntryCount++], &rec->lod, 8);
    rec->entry->shape |= 0x80;
}
