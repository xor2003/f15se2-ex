/* F-19 START globals - real named objects replacing the flat f19_dseg
 * blob.  Every word_/byte_/dword_ cell lives in f19stvars.c (generated
 * from the embedded START.EXE image); DOS offsets resolve through the
 * f19_stObjs table in f19seg.c.  commData/gameData are the dseg cells
 * that held far pointers to the comm/game blocks; word_2D066/word_2991C
 * map onto them via f19.h.  Remaining extern names get real objects here. */
#include "f19.h"
#include "f19stvars.h"

void *f19_commData;
void *f19_gameData;
uint8 f19_commBase[0x8000];   /* commData+0, gameData+0x120E */
int16 f19_lowFlags[4];
int16 *word_298EC;
int16 *word_2B942;                                       /* stmain.c */
int16 *g_vpParms;                                        /* stutil.c */
int16 *uiPage;                                           /* stpinp.c */
int16 *page1Num;                                         /* stpinp.c */
char str682E[0x400];                                     /* stutil.c */
char str6832[0x400];                                     /* stutil.c */
char str6834[0x400];                                     /* stutil.c */
char str6836[0x400];                                     /* stutil.c */
char str6838[0x400];                                     /* stutil.c */
uint8 briefTargs[0x400];                                 /* stutil.c */
uint8 missionKinds[0x400];                               /* stutil.c */
FILE *f19_fileHandle;                                    /* stgrid.c */
int16 g_viewCenterY2;                                    /* eg3dmap.c */
int16 *f19_nearestTerrainResult;                         /* stgen.c */

/* reset the START-side dseg objects to their image state, then wire the
 * native pointers DOS init code would have produced. */
void f19_dsegInit(void) {
    f19_segUseWorld(0);
    f19_stVarsReset();
    /* cells whose content is a dseg offset to the real table */
    g_vpParms = (int16 *)f19_dsegAt(f19_stSpace.m_word_26572);
    uiPage = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_stSpace.m_word_25480 + 0x262));
    page1Num = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_stSpace.m_word_25480 + 0x27A));
    /* The BSS far-ptr tables at dseg 0x7EA+ (menu name/count cells,
     * theater .spr table at 0x9B6, ...) are filled at runtime by
     * ovlF43_a+ovlF43_10d from the scenery0.exe string resource —
     * see f19file.c. */
}
