/*
 * desc_f19.c - game descriptor for F-19 Stealth Fighter.
 *
 * F-19 is the direct ancestor of this engine: its PIC/SPR/WLD/3D3/3DG/3DT
 * formats are the same MicroProse revisions the existing loaders decode
 * (several theater files are byte-identical to F-15 SE2's).
 *
 * The engine code was written against F-15's file names, so F-19 support is
 * expressed as an alias table onto the F-19 install. F-19 ships four theaters
 * (Libya, Persian Gulf, North Cape, Central Europe); in the engine's 8-slot
 * model they occupy slots 0,1,4,5 and the unshipped slots are defensively
 * pointed at files F-19 does ship.
 *
 * F-19's own START flow (TASS menu state machine, mission generator) lives
 * in src/f19/ - the reconstruction of START.EXE. start_main below is the
 * real F-19 entry; egame runs the ported EGAME.EXE session under src/f19/
 * (F-15/F-19 logic stays separate). end still falls back to the F-15 module
 * until the END.EXE port lands.
 */

#include "game.h"

int f19_start_main(void);
int f19_egame_main(void);
int end_main(void);

static const char *const f19Signatures[] = {
    "f19.spr",
    "stflt.3d3",
};

static const GameAssetFile f19Manifest[] = {
    { "b95c418e696ff977f3e3022829ce67ae", "f19.spr" },
    { "012e004ecf89ece8ea2ead9c4494e593", "stflt.3d3" },
    { "222f70f85898fb248fe76a2b053c7251", "stflt117.3d3" },
    { "5527467253f038e296fc2519606e38aa", "photo.3d3" },
    { "5993be8af48dec234e8cfb7e9c131dfb", "title16.pic" },
    { "d11ccbfdad6b71b6f31f412f41697238", "adv.pic" },
    { "33bf8ae93b3e5316e98f4b16bd0ad61d", "cockpit.pic" },
    { "4cc05dc64129b30b4fe164164f0e0f33", "256pit.pic" },
    { "d246a3631d9ce6e4746415d4f360ec68", "dbicons.spr" },
    { "c5dd9230d19e27c75bf36d43e1e34c1c", "medal.pic" },
    { "ec1a76337430de510e163a3749dd9022", "clip.pic" },
    { "de362718594cbb0d44aff18445b78565", "tass.pic" },
    { "2da73b28c90a4039dcb3a702d56af413", "note.pic" },
    { "a856080d7d4a35c4067198680d62a34d", "grave.pic" },
    { "a0a2ebe761b15f56014c2fdb46717d1d", "flag.pic" },
    { "50a23dd329707b0b255d7014b778da0c", "roster.pic" },
    { "0ff3f60793efa8d4d2981b4f98915368", "maps.spr" },
    { "1111c1562fc5a12740a4c4e9cf81bc42", "medal.spr" },
    { "8b235908ca74b93885dd43f7744f94da", "arming.spr" },
    { "2acbef6bc8dd1e2db7bf5c053ad916c5", "arming.pic" },
    { "0b8866ceaad15bc03bd7897bd84c66d5", "lb.3d3" },
    { "d10c1d6cc9ade429e6dd81d9f2efc9de", "lb.3dg" },
    { "73b3bfa86cd57a507a2e1591db45ed76", "lb.3dt" },
    { "570679af0f0bb2d44eb16226d557bbed", "pg.3d3" },
    { "4cfaf482d83f9a90fe30734e7a701918", "pg.3dg" },
    { "46ced2bc0128d95674e10521fee61cd4", "pg.3dt" },
    { "f8ec10b3de5dfd555748497cc5f88f63", "nc.3d3" },
    { "2534b73170ae55e00b1ecfa706d5e04e", "nc.3dg" },
    { "ace87d2c23046f0fecf50c04e847ab3c", "nc.3dt" },
    { "e480cd3510d5895f399e996725a58f6a", "ce.3d3" },
    { "24e7ce3c6699b883ea8c39cc498bf863", "ce.3dg" },
    { "d5682a85a00517501a2cc3903b5274e2", "ce.3dt" },
    { "fd0a364247d43bd198f4ad4974a3aec1", "libya.wld" },
    { "ff400146da98357c33f2578c46dfd6b7", "gulf.wld" },
    { "4ecabfb63f451c989a51feca22048e19", "nc.wld" },
    { "d592b863f4b26d1726c1e04156309c97", "ce.wld" },
    { "fb234541a8fd588684c80417eebbf729", "libya.spr" },
    { "fff6a0e3d2d16af739d9a36eb413339f", "persian.spr" },
    { "a019f6eade84d4d6a2c8034850979c14", "ncape.spr" },
    { "bacf2b850fcc6b592f92f046c89b15c8", "ceurope.spr" },
    { "651bcf535ed50ffa7724c8751bec1a66", "roster.fil" },
    { NULL, NULL }
};

/* Engine literals -> F-19 install names. F-19 has no MPS-labs splash, no
 * hi-res EGA title, no side/rear cockpit views and no digitized speech blob;
 * those are handled by descriptor flags or simply go unmapped (the pic/audio
 * loaders tolerate a missing file). */
static const GameFileAlias f19Aliases[] = {
    { "f15.spr",       "f19.spr"      },
    { "15flt.3d3",     "stflt.3d3"    }, /* player model; STFLT117.3D3 (F-117A) is the alt airframe */
    { "wall.pic",      "clip.pic"     }, /* briefing board -> clipboard */
    { "labs.pic",      "tass.pic"     }, /* MPS-labs splash -> Pravda news splash */
    { "desk.pic",      "note.pic"     }, /* award ceremony desk -> notepad desk */
    { "death.pic",     "grave.pic"    }, /* KIA screen -> cemetery */
    { "promo.pic",     "flag.pic"     }, /* promotion ceremony -> flags */
    { "hiscore.pic",   "roster.pic"   }, /* pilot records -> duty roster */
    { NULL, NULL }
};

static const GameMenuEntry f19TheaterMenu[] = {
    { "Libya",           "Across the \"Line of Death\"",   0, NULL },
    { "Persian Gulf",    "Keeping the Sea Lanes Open",     1, NULL },
    { "North Cape",      "Into the Soviet's Backyard",     4, NULL },
    { "Central Europe",  "Red Storm Raging",               5, NULL },
    { NULL, NULL, 0, NULL }, /* F-19 ships no scenario disks: fifth row hidden */
};

static const int8 f19CampaignOrder[] = { 0, 1, 4, 5 };

/* Slots 2,3,6,7 don't exist in F-19; they point at real F-19 files so an
 * unreachable index can never hang the file-retry loops. */
static const char *const f19PlhFiles[8] = {
    "lb.xxx", "pg.xxx", "nc.xxx", "nc.xxx", "nc.xxx", "ce.xxx", "ce.xxx", "ce.xxx" };
static const char *const f19WorldFiles[8] = {
    "libya.wld", "gulf.wld", "nc.wld", "nc.wld", "nc.wld", "ce.wld", "ce.wld", "ce.wld" };
static const char *const f19TheaterSprFiles[8] = {
    "libya.spr", "persian.spr", "ncape.spr", "ncape.spr", "ncape.spr", "ceurope.spr", "ceurope.spr", "ceurope.spr" };

/* extern: TU compiles as C++, where namespace-scope const would otherwise get
 * internal linkage and stay invisible to gamedesc.c's registry. */
extern const GameDesc g_gameDescF19 = {
    GAMEID_F19,
    "f19",
    "F-19 Stealth Fighter",
    "F19",
    f19Signatures, 2,
    f19Manifest,
    f19Aliases,
    f19PlhFiles,
    f19WorldFiles,
    f19TheaterSprFiles,
    f19TheaterMenu, 5,
    NULL, 0,
    f19CampaignOrder, 4,
    0 /* hasHiResTitle: F-19 ships no TITLE640.PIC */,
    "assets/f19",
    /* F-19 conversions go under converted_assets_f19; the F-15 pack name must
     * not match, or its same-named PNGs (TITLE16/ADV/COCKPIT/...) would shadow
     * F-19's real PICs when the engine dir is the cwd. */
    "converted_assets_f19",
    GAMEENGINE_MPS_CLASSIC,
    /* start/egame = reconstructed F-19 START.EXE + EGAME.EXE (src/f19/).
     * end still points at the F-15 module pending the END.EXE port. */
    f19_start_main, f19_egame_main, end_main,
};
