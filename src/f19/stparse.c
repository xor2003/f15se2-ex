/* START.EXE — grid/terrain entry point + .3DT tile reader
 * (linker stparse.c; EN seg000:0x71f8 f19_parseGridTerrain, 0x720c f19_parseTerrain) */
#include "f19.h"
#include <stdio.h>

extern FILE *f19_fileHandle;
#define regnPlhPtr ((char *)(f19_dseg + *(uint16 *)(f19_dseg + 0x4DAC)))
#define terrainDirtyFlag (*(int16 *)(f19_dseg + 0x3D0E))
#define terrainSignature (*(int16 *)(f19_dseg + 0x3BBE))
#define terrainBuf1 ((uint16 *)(f19_dseg + 0x3BC0))
#define terrainTileCounts ((struct TerrainPtrTable *)(f19_dseg + 0x3BCA))
#define terrainTilePtrs ((struct TerrainPtrTable *)(f19_dseg + 0xCE28))
#define terrainTileBlock ((uint8 *)(f19_dseg + 0xA5C8))

extern void f19_parseGrid(void);                /* seg000:0x73df */
extern void f19_replaceExtension(char *path, char *ext);             /* seg000:0x7534 */
extern int16 f19_showMsgWaitKey(const char *m); /* seg000:0x7518 */

struct TerrainPtrTable { uint16 entries[32]; };  /* entries = dseg offsets */
void f19_parseTerrain(char *filename);

void f19_parseGridTerrain(void) {               /* seg000:0x71f8 */
    f19_parseGrid();
    f19_parseTerrain(regnPlhPtr);
    terrainDirtyFlag = 0;
}

void f19_parseTerrain(char *filename) {         /* seg000:0x720c */
    int16 tmp, level, tileOffset, entry;
    uint16 i;
    f19_replaceExtension(filename, ".3dT");
    if ((f19_fileHandle = fopen(filename, "rb")) == 0) {
        f19_showMsgWaitKey("Open Error on *.3DT, assuming new file !");
    }
    else {
        fread(&terrainSignature,2,1,f19_fileHandle);
        if (terrainSignature != 0x3131) {
            f19_showMsgWaitKey("Bad Tile file format.");
        }
        else {
            fread(terrainBuf1,2,5,f19_fileHandle);
                for (level = 0; level < 5; level++) {
                    if (terrainBuf1[level] > 0x20) {
                    f19_showMsgWaitKey("Too many tiles.");
                    return;
                }
                fread(&terrainTileCounts[level],2,terrainBuf1[level], f19_fileHandle);
            }
            tileOffset = 0;
            for (level = 0; level < 5; level = level + 1) {
                for (entry = 0; terrainBuf1[level] > entry; entry++) {
                    terrainTilePtrs[level].entries[entry] = (uint16)(0xA5C8 + tileOffset);
                    for (i = 0; i < terrainTileCounts[level].entries[entry]; i++) {
                        if (tileOffset > 0xdac) {
                            f19_showMsgWaitKey("Too much tile data");
                            return;
                        }
                        fread((uint8*)terrainTileBlock + tileOffset,2,1,f19_fileHandle);
                        fread((uint8*)terrainTileBlock + 2 + tileOffset,2,1,f19_fileHandle);
                        fread((uint8*)terrainTileBlock + 4 + tileOffset,2,1,f19_fileHandle);
                        fread(&tmp,2,1,f19_fileHandle);
                        *((uint8*)terrainTileBlock + 6 + tileOffset) = tmp;
                        tileOffset += 7;
                    }
                }
            }
        }
        fclose(f19_fileHandle);
    }
}
