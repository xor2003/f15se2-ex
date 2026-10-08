/* START.EXE — grid-file parsing (linker-tree stgrid.c lineage;
 * verified vs EN binary). */
#include "f19.h"
#include "f19stvars.h"
#include <stdio.h>

extern FILE *f19_fileHandle;
#define regnPlhPtr ((char *)f19_dsegAt(f19_stSpace.m_regnPlhPtr))
#define gridSignature (*(uint16 *)((uint8 *)f19_stSpace.m_terrainTileCounts + 320))
#define gridValidFlag (*(int16 *)((uint8 *)f19_stSpace.m_terrainTileCounts + 326))
#define gridBuf1 ((uint8 *)f19_stSpace.m_gridBuf1)
#define gridBuf2 ((uint8 *)f19_stSpace.m_gridBuf2)
#define gridBuf3 ((uint8 *)f19_stSpace.m_gridBuf3)
#define gridBuf4 ((uint8 *)f19_stSpace.m_gridBuf4)
#define gridBuf5 ((uint8 *)f19_stSpace.m_gridBuf5)

extern void mystrcpy(char *d, const char *s);      /* seg000:0x5120 */
extern int16 f19_showMsgWaitKey(const char *m);        /* seg000:0x7518 */
extern void sub_151D4(void *d, int8 v, int16 n);   /* seg000:0x51d4 memset */

void f19_replaceExtension(char *path, char *ext);


void f19_parseGrid(void) {                        /* seg000:0x73df */
    int16 i, n;    /* i gets the post-loop j store; n unused in the frame */
    register int16 j;
    f19_replaceExtension(regnPlhPtr, ".3DG");
    if ((f19_fileHandle = fopen(regnPlhPtr, "rb")) == 0) {
        f19_showMsgWaitKey("Open error on grid file");
        j = 0;
        do {
            gridBuf1[j] = j;
            j++;
        } while (j < 0x10);
        i = j;   /* emits the observed mov [bp-2],si register flush */
        sub_151D4(gridBuf2, 0, 0x100);
        sub_151D4(gridBuf3, 0, 0x200);
        sub_151D4(gridBuf4, 0, 0x200);
        sub_151D4(gridBuf5, 0, 0x200);
        gridValidFlag = 0;
        return;
    }
    fread(&gridSignature, 2, 1, f19_fileHandle);
    if (gridSignature != 0x3232) {
        f19_showMsgWaitKey("Bad grid file for region");
    }
    else {
        fread(gridBuf1, 1, 0x10, f19_fileHandle);
        fread(gridBuf2, 1, 0x100, f19_fileHandle);
        fread(gridBuf3, 1, 0x200, f19_fileHandle);
        fread(gridBuf4, 1, 0x200, f19_fileHandle);
        fread(gridBuf5, 1, 0x200, f19_fileHandle);
    }
    fclose(f19_fileHandle);
}

/* seg000:0x7534 - `s` is a phantom register param (callers pass 2 args;
 * its home [bp+8] is never touched).  It binds si without a local slot,
 * and `path = s` emits the observed `mov [bp+4],si` writeback. */
void f19_replaceExtension(char *path, char *ext) {
    register char *s;
    for (s = path; *s != '.';) {
        if (*s == 0) break;
        s++;
    }
    path = s;
    mystrcpy(path, ext);
}

