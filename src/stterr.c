/* Terrain parsing */
// offsets based on F-15 SE2 v451.03 start.exe (unpacked) MD5: cf6e997ed4582cf82db6ec37d2b1a6fd
#include "struct.h"
#include "stdata.h"
#include "stterr.h"
#include "stmath.h"
#include "comm.h"
#include "log.h"
#include "const.h"

#include <memory.h>
#include <dos.h>
#include <string.h>
#include <stdio.h>

/* Terrain-search cell geometry: the quadtree cells are 0x1000 fine units on
 * a side at the search level; & 0xfff extracts the in-cell offset. */
enum {
    kTerrainCellShift = 12,
    kTerrainCellPitch = 0x1000,
    kTerrainCellHalf = 0x800,
    kTerrainCellMask = 0xfff,
    /* Level-1 coords are 4x finer than level-2: dist >>= 2 normalizes a
     * level-1 candidate for comparison, <<= 2 lifts the winning level-2
     * result back to level-1 units for worldX/worldY. */
    kTerrainLevelZoom = 2,
    /* Tile record model index is the low 7 bits; bit 7 is a render flag. */
    kTerrainModelMask = 0x7f,
};

/* Private helpers for this translation unit. */
int16 lookupGridCell(int16, int16, int16);

struct NearestTerrain *findNearestTerrain(int32 worldX, int32 worldY) {
    int16 tmp, dx, dist, rowOff, x1, level, dy, i, cellIdx, gridX, offsetY, y1, cell;
    int16 sy;
    int16 ty;
    uint32 fx;
    nearestTerrain.dist = 0x7fff;
    for (level = 1; level <= 2; level++) {
        for (i = 0; i < 9; i++) {
            fx = scaleCoordByLevel(level, worldX);
            gridX = fx >> kTerrainCellShift;
            x1 = (int16)fx & kTerrainCellMask;
            fx = scaleCoordByLevel(level, worldY);
            y1 = fx >> kTerrainCellShift;
            dy = (int16)fx & kTerrainCellMask;
            dx = dirDeltaX[i];
            rowOff = dirDeltaY[i];
            /* Neighbour cell pixel offset. The original indexed a table about its
               base by the cell delta (dx/rowOff in {-1,0,1}); in the DOS binary the
               negative indices aliased the tail of dirDeltaY to form the symmetric
               table {-0x2000,-0x1000,0,+0x1000,+0x2000}. The native equivalent of
               that for a one-cell delta is simply delta * 0x1000. */
            sy = dx * kTerrainCellPitch - x1 + kTerrainCellHalf;
            tmp = rowOff * kTerrainCellPitch - dy + kTerrainCellHalf;
            y1 += rowOff;
            cell = lookupGridCell(level, gridX += dx, y1);
            if (cell != -1) {
                tileDataPtr = terrainTilePtrs[level].entries[cell];
                for (cellIdx = 0; terrainTileCounts[level].entries[cell] > cellIdx; cellIdx++) {
                    /* Bit 7 enables a per-placement render override; the lower
                       bits identify the base model used for classification. */
                    const uint8 modelIndex = tileDataPtr->idx & kTerrainModelMask;
                    if (modelIndex < sizeof(objectTypeTable) && objectTypeTable[modelIndex] != 0) {
                        ty = tileDataPtr->buf3 + sy;
                        offsetY = tileDataPtr->buf4 + tmp;
                        dist = abs(ty) + abs(offsetY);
                        if (level == 1) {
                            dist >>= kTerrainLevelZoom;
                        } else {
                            ty <<= kTerrainLevelZoom;
                            offsetY <<= kTerrainLevelZoom;
                        }
                        if (dist < nearestTerrain.dist) {
                            nearestTerrain.level = (int8)level;
                            nearestTerrain.cellIdx = (int8)cellIdx;
                            nearestTerrain.gridX = (int8)gridX;
                            nearestTerrain.gridY = (int8)y1;
                            nearestTerrain.tilePtr = tileDataPtr;
                            nearestTerrain.objectType = modelIndex;
                            nearestTerrain.dist = dist;
                            nearestTerrain.worldX = ty + worldX;
                            nearestTerrain.worldY = offsetY + worldY;
                        }
                    }
                    tileDataPtr++;
                }
            }
        }
    }
    if (nearestTerrain.dist != 0x7fff) {
        return &nearestTerrain;
    } else
        return NULL;
}

int16 lookupGridCell(int16 level, int16 col, int16 row) {
    if (col < 0 || row < 0 || col >= gridLevelSize[level + 3] || row >= gridLevelSize[level + 3])
        return -1;
    switch (level) {
    case 4:
        return gridBuf1[col + (row << 2)];
    case 3:
        return gridBuf2[col + (row << 4)];
    case 2:
        return gridBuf3[(col & 3) + (((row & 3) << 2) + (lookupGridCell(3, col >> 2, row >> 2) << 4))];
    case 1:
        return gridBuf4[(col & 3) + (((row & 3) << 2) + (lookupGridCell(2, col >> 2, row >> 2) << 4))];
    default: // case 0
        return gridBuf5[(col & 3) + (((row & 3) << 2) + (lookupGridCell(1, col >> 2, row >> 2) << 4))];
    }
}
