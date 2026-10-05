/* START.EXE — quadtree terrain-grid lookup (linker-tree stterr.c lineage;
 * verified vs EN binary).  /Ot module (inlined case-epilogues). */
#include "f19.h"
#include "f19stvars.h"

#define gridBuf1 ((uint8 *)f19_stSpace.m_gridBuf1)
#define gridBuf2 ((uint8 *)f19_stSpace.m_gridBuf2)
#define gridBuf3 ((uint8 *)f19_stSpace.m_gridBuf3)
#define gridBuf4 ((uint8 *)f19_stSpace.m_gridBuf4)
#define gridBuf5 ((uint8 *)f19_stSpace.m_gridBuf5)
#define gridLevelSize ((int16 *)f19_stSpace.m_gridLevelSize)


/* seg000:0x70e0 — recursive quadtree descent through the 5 grid levels */
int16 f19_lookupGridCell(int16 level, int16 col, int16 row) {
    if (col < 0 || row < 0 || col >= gridLevelSize[level] ||
        row >= gridLevelSize[level])
        return -1;
    switch (level) {
    case 4:
        return gridBuf1[col + (row << 2)];
    case 3:
        return gridBuf2[col + (row << 4)];
    case 2:
        return gridBuf3[(col & 3) + (((row & 3) << 2) +
            (f19_lookupGridCell(3, col >> 2, row >> 2) << 4))];
    case 1:
        return gridBuf4[(col & 3) + (((row & 3) << 2) +
            (f19_lookupGridCell(2, col >> 2, row >> 2) << 4))];
    case 0:
        return gridBuf5[(col & 3) + (((row & 3) << 2) +
            (f19_lookupGridCell(1, col >> 2, row >> 2) << 4))];
    }
}
