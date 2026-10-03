/* F-19 START globals - storage generated from f19ru reconstruction
 * extern decls. f19_dseg is the flat emulated data segment: every
 * word_/byte_/dword_ name in the sources is a typed lvalue macro into it
 * (offsets are the DOS dseg offsets, so aliasing and absolute refs behave
 * like the original image). commData/gameData are the dseg cells that
 * held the far pointers to the comm/game blocks; word_2D066/word_2991C
 * map onto them via f19.h. Remaining extern names get real objects here. */
#include "f19.h"

void *f19_commData;
void *f19_gameData;
uint8 f19_commBase[0x8000];   /* commData+0, gameData+0x120E */
int16 f19_lowFlags[4];
int16 *word_298EC;
uint8 f19_dseg[0x10000];
extern const uint8 f19_dseg_img[];      /* f19segdat.c: image bytes 0..0x98DF */

/* load the original file-backed dseg bytes (const tables, strings,
 * preinit words); the BSS range above 0x98E0 stays zero. */
extern int16 *g_vpParms, *uiPage, *page1Num;

void f19_dsegInit(void) {
    extern void *memcpy(void *, const void *, unsigned long);
    memcpy(f19_dseg, f19_dseg_img, 0x98E0);
    /* far/near pointer cells the ported files consume as native pointers */
    g_vpParms = (int16 *)(f19_dseg + *(uint16 *)(f19_dseg + 0x6572));
    uiPage   = (int16 *)(f19_dseg + *(uint16 *)(f19_dseg + 0x56E2));
    page1Num = (int16 *)(f19_dseg + *(uint16 *)(f19_dseg + 0x56FA));
    /* The BSS far-ptr tables at dseg 0x7EA+ (menu name/count cells,
     * theater .spr table at 0x9B6, ...) are filled at runtime by
     * ovlF43_a+ovlF43_10d from the scenery0.exe string resource —
     * see f19file.c. */
}

int16 *g_vpParms;                                        /* stutil.c */
char str682E[0x400];                                     /* stutil.c */
char str6832[0x400];                                     /* stutil.c */
char str6834[0x400];                                     /* stutil.c */
char str6836[0x400];                                     /* stutil.c */
char str6838[0x400];                                     /* stutil.c */
char *wldNameTab[256];
uint8 briefTargs[0x400];                                 /* stutil.c */
uint8 missionKinds[0x400];                               /* stutil.c */
int16 *word_2170A;                                       /* stobj.c */
int16 *word_2170C;                                       /* stobj.c */
uint8 *word_2BE50;                                       /* stutil.c */
FILE *f19_fileHandle;                                      /* stgrid.c */
int16 *word_21706;                                       /* stobj.c */
int16 *word_21708;                                       /* stobj.c */
int16 g_viewCenterY2;                                    /* eg3dmap.c */
uint8 *word_22320;                                       /* stobj.c */
uint8 *word_22322;                                       /* stobj.c */
int16 far *word_2B942;                                   /* stmain.c */
void * *word_2B386;                                      /* stmenu.c */
int16 *word_25F7C;                                       /* stmenu.c */
int16 *word_25D30;                                       /* stmenu.c */
int16 *word_25D48;                                       /* stmenu.c */
void * *word_2D276;                                      /* stmenu.c */
int16 *word_26050;                                       /* stmenu.c */
int16 *word_25F94;                                       /* stmenu.c */
int16 *word_25FAC;                                       /* stmenu.c */
int16 *word_262A8;                                       /* stmenu.c */
int16 *word_2607A;                                       /* stmenu.c */
int16 *word_26092;                                       /* stmenu.c */
int16 *word_263AE;                                       /* stmenu.c */
int16 *word_262C0;                                       /* stmenu.c */
int16 *word_262D8;                                       /* stmenu.c */
int16 *word_26480;                                       /* stmenu.c */
int16 *word_263C6;                                       /* stmenu.c */
int16 *word_26572;                                       /* stmenu.c */
int16 *word_2655A;                                       /* stmenu.c */
int16 *word_2681E;                                       /* stmenu.c */
char *word_26820[256];
int16 *word_25B2C;                                       /* stmenu.c */
int16 *word_25B44;                                       /* stmenu.c */
int16 *word_25CF6;                                       /* stmenu.c */
char *word_25454[256];
int16 *word_25014;                                       /* stmenu.c */
int16 *word_24FFC;                                       /* stmenu.c */
int16 *word_2542A;                                       /* stmenu.c */
char *word_2591C[256];
char *word_2592A[256];
/* sprParmsTab aliases f19_dseg (page-descriptor pool); see stmenu/stutil */
char *namePtrTab[256];
char *typeNameTab[256];
uint8 ringTypes[0x400];                                  /* stmap.c */
uint8 unitMarksOn;                                       /* stmap.c */
uint8 tileMarksOn;                                       /* stmap.c */
uint8 siteMarksOn;                                       /* stmap.c */
uint8 ringMode;                                          /* stmap.c */
int16 flag_29948;                                        /* stmap.c */
uint16 *word_27E50;                                      /* stmap.c */
uint8 f19_flightUnits[0x400];                                /* stgen.c */
uint8 f19_worldObjects[0x400];                               /* stgen.c */
int16 *f19_nearestTerrainResult;                             /* stgen.c */
uint8 f19_targets[0x400];                                    /* stgen.c */
uint8 f19_siteParms[0x400];                                  /* stgen.c */
uint8 f19_linkTab[0x400];                                    /* stgen.c */
int16 *uiPage;                                           /* stpinp.c */
int16 *page1Num;                                         /* stpinp.c */
