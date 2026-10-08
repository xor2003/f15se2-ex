/* F-19 EGAME runtime data: the module entry swaps f19_dseg to the EGAME
 * image and wires the far-pointer cells that DOS init code would set. */
#include "f19eg.h"
#include "f19egvars.h"
#include <string.h>

uint8 f19_bda[0x500];         /* fake BIOS data area (0:4xx) */
int16 f19eg_seg004;           /* handle for the 3D world-data segment */

void f19_movedata(uint16 sseg, uint16 soff, uint16 dseg_, uint16 doff, uint16 n) {
    memmove(f19_segResolve(doff, dseg_),
            f19_segResolve(soff, sseg), n);
}

/* complete-type defs needed by the by-value realvars below; identical
 * layouts to the per-TU decls in the port sources. */
#pragma pack(push,1)
struct Proj3d {
    int32 x, y;
    int16 w;
    int32 z;
};
struct GaugeParams {
    int16 bufPtr, srcX, srcY, page, dstX, dstY, width, height;
};
struct TileSceneObject {
    int16 x, y, z;
    uint8 shape;
};
struct TileObject {
    int16 id; int16 dist; int32 x; int32 y; int16 entry;
    uint8 lod, subIndex, tileX, tileY; int16 shapeOff;
};
struct VpParms { int16 f[11]; };
#pragma pack()

uint8 *f19eg_farPointer;
int16 f19eg_g_autopilotEngaged;
int16 f19eg_g_clipMinX;
int16 f19eg_g_clipMinY;
struct TileSceneObject *f19eg_g_curTileEntry;
int16 f19eg_g_drawColor;
int16 *f19eg_g_mapTerrainMode;
int16 f19eg_g_mapY;
char *f19eg_g_nameTab[0x68];
struct TileObject *f19eg_g_nearestTileObj;
int16 *f19eg_g_pageBack;
int16 *f19eg_g_pageFront;
int16 *f19eg_g_pageOffscreen;
struct Proj3d f19eg_g_proj3d;
int16 *f19eg_g_targetViewParams;
int16 *f19eg_g_viewParams;
struct VpParms *f19eg_g_vpParms;
int16 f19eg_g_vtxX;
int16 f19eg_g_vtxY;
struct GaugeParams f19eg_gaugeSpriteParams;
char *f19eg_regnFile;
char *f19eg_regnName;

/* vertex-X table: the shared driver reads g_replayLog+0x600 — F-19's
 * g_modelVertX/g_modelVtxXTab data lands there via f19eg_vertexX. */
extern struct ReplayLog g_replayLog;   /* app's shared driver global */
int16 *const f19eg_vertexX = (int16 *)((char *)&g_replayLog + 0x600);

static void f19eg_ptrInit(void) {
    f19eg_farPointer = (uint8 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_axisInputAccum + 0x6C2));
    f19eg_g_mapTerrainMode = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0xB6));
    f19eg_g_nearestTileObj = (struct TileObject *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_buf2_3dg + 0x200));
    f19eg_g_pageBack = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0x86));
    f19eg_g_pageFront = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0x6E));
    f19eg_g_pageOffscreen = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0x9E));
    f19eg_g_targetViewParams = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0xCE));
    f19eg_g_viewParams = (int16 *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.f19_eggap_7C + 0x37C));
    f19eg_g_vpParms = (struct VpParms *)f19_dsegAt(*(uint16 *)((uint8 *)&f19_egSpace.m_g_weaponCells + 0x6E));
    f19eg_regnFile = (char *)f19_dsegAt(*(uint16 *)(uint8 *)&f19_egSpace.m_off_2EEE8);
    f19eg_regnName = (char *)f19_dsegAt(*(uint16 *)(uint8 *)&f19_egSpace.m_off_2EEE8);
}

/* EN string graft: the baked-in dseg image came from the RU build, whose
 * fixed-size name tables carry transliterated strings (Sajdwinder, Pingwin,
 * Fantom, ...). The EN binary's tables are record-identical, so the EN name
 * fields are poked over the RU slots after the image is loaded. */
static const struct {
    uint16 off;
    const char *s;
    uint8 n;
} f19eg_enStrings[] = {
    {0x4894, "None\000\000\000\000", 8},                                           /* None */
    {0x493C, "Hawk\000\000\000\000", 8},                                           /* Hawk */
    {0x494A, "Rapier\000\000", 8},                                                 /* Rapier */
    {0x4958, "Tiger\000\000\000", 8},                                              /* Tiger */
    {0x4966, "Seacat\000\000", 8},                                                 /* Seacat */
    {0x4A96, "F-4E\000\000\000 Phantom\000\000\000", 18},                          /* F-4E */
    {0x4AB6, "F-14\000\000\000 Tomcat\000\000\000\000", 18},                       /* F-14 */
    {0x4AF6, "An-72\000\000 Coaler\000\000\000\000", 18},                          /* An-72 */
    {0x4B56, "F-14\000\000\000 Tomcat\000\000\000\000", 18},                       /* F-14 */
    {0x4B76, "F-4E\000\000\000 Phantom\000\000\000", 18},                          /* F-4E */
    {0x4B96, "Yak-38\000 Forger\000\000\000\000", 18},                             /* Yak-38 */
    {0x4BF6, "F-5\000\000\000\000 Tiger\000\000\000\000\000", 18},                 /* F-5 */
    {0x4C16, "767\000\000\000\000 Boeing\000\000\000\000", 18},                    /* 767 */
    {0x4C36, "None\000\000\000\000", 8},                                           /* None */
    {0x4D0E, "Hawk\000\000\000\000", 8},                                           /* Hawk */
    {0x4D20, "Rapier\000\000", 8},                                                 /* Rapier */
    {0x4D32, "Tiger\000\000\000", 8},                                              /* Tiger */
    {0x4D44, "Seacat\000\000", 8},                                                 /* Seacat */
    {0x4DF8, "Penguin\000", 8},                                                    /* Penguin */
    {0x4E0A, "Harpoon\000", 8},                                                    /* Harpoon */
    {0x4EE2, "Equip.\000\000", 8},                                                 /* Equip. */
    {0x4F24, "AIM-9M\000\000\000\000Sidewinder\000\000", 22},                      /* AIM-9M */
    {0x4F72, "P3 ASM\000\000\000\000Penguin\000\000\000\000\000", 22},             /* P3 ASM */
    {0x4F8C, "AGM-86A\000\000\000Harpoon\000\000\000\000\000", 22},                /* AGM-86A */
    {0x4FA6, "AGM-65D\000\000\000Maverick\000\000\000\000", 22},                   /* AGM-65D */
    {0x4FC0, "GBU-12\000\000\000\000Paveway\000\000\000\000\000", 22},             /* GBU-12 */
    {0x4FDA, "Mk 20\000\000\000\000\000Rockeye\000\000\000\000\000", 22},          /* Mk 20 */
    {0x500E, "Mk 82-0\000\000\000Slick\000\000\000\000\000\000\000", 22},          /* Mk 82-0 */
    {0x5028, "Mk 82-1\000\000\000Snakeye\000\000\000\000\000", 22},                /* Mk 82-1 */
    {0x5042, "Mk 20\000\000\000\000\000Rockeye II\000\000", 22},                   /* Mk 20 */
    {0x505C, "Mk 122\000\000\000\000Fireeye\000\000\000\000\000", 22},             /* Mk 122 */
    {0x5076, "CBU-72\000\000\000\000Fuel-Air\000\000\000\000", 22},                /* CBU-72 */
    {0x5090, "Mk 35\000\000\000\000\000IN Cluster\000\000", 22},                   /* Mk 35 */
    {0x50AA, "ISC B-1\000\000\000Minelets\000\000\000\000", 22},                   /* ISC B-1 */
    {0x50C4, "135 mm\000\000\000\000Camera\000\000\000\000\000\000", 22},          /* 135 mm */
    {0x50DE, "1900lbs\000\000\000Extra Fuel\000\000", 22},                         /* 1900lbs */
    {0x50F8, "20 mm\000\000\000\000\000Guns\000\000\000\000\000\000\000\000", 22}, /* 20 mm */
    {0x5112, "Special\000\000\000Equip\000\000\000\000\000\000\000", 22},          /* Special */
};

void f19_egDsegLoad(void) {
    int i;
    f19_segUseWorld(1);
    f19_egVarsReset();
    for (i = 0; i < (int)(sizeof f19eg_enStrings / sizeof f19eg_enStrings[0]); i++)
        memcpy(f19_dsegAt(f19eg_enStrings[i].off), f19eg_enStrings[i].s,
               f19eg_enStrings[i].n);
    /* seg004: the DOS-allocated 3D world-data block (LB.3D3 stream at +0,
     * aircraft models at +0x7530) — 64 KB, persisted across missions. */
    if (f19eg_seg004 == 0)
        f19eg_seg004 = f19_allocSeg(0x1000);
    /* far cells DOS init would fill: commData at +0, gameData at +0x120E,
     * g_viewParamsFar at +0x120E, the BDA motor byte at 0:0x440. */
    f19_setFar(f19_dsegOff((f19_egSpace.m_bulletTracks + 0xFA)), f19_commBase);       /* dword_38B10 */
    f19_setFar(f19_dsegOff((f19_egSpace.m_geeStrBuf + 0x20)), f19_commBase + 0x120E); /* dword_354D0 */
    f19_setFar(f19_dsegOff((f19_egSpace.m_geeStrBuf + 0x12)), f19_bda + 0x440);       /* dword_354C2 */
    f19eg_ptrInit();
}
