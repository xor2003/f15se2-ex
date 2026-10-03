/*
 * desc_f15.c - game descriptor for F-15 Strike Eagle II.
 *
 * This is the reference descriptor: the engine's literals ARE the F-15 file
 * names, so the alias table is empty and the tables/menus reproduce the
 * values that used to be hardcoded in stdata.c/egdata.c/endata.c/hdsprite.c/
 * stmissn.c/stmain.c.
 */

#include "game.h"

int start_main(void);
int egame_main(void);
int end_main(void);

static const char *const f15Signatures[] = {
    "15flt.3d3",
    "f15.spr",
};

static const GameAssetFile f15Manifest[] = {
    { "3e468dbc9dd2c25a5343e384656d4b87", "15flt.3d3" },
    { "82b6b193954a0abba22f5f8267291d14", "1.pic" },
    { "8a8d0d29a6789de4971a5381cb89a60c", "256left.pic" },
    { "b6651ea956bec71890bf90de9a31fb1d", "256pit.pic" },
    { "4c4704170d85e18c842e18b1f438d266", "256rear.pic" },
    { "d18609791beb5a4b6bbc3c95a49979ee", "256right.pic" },
    { "a48a7ec1de1637da2d132a0fab3b4894", "2.pic" },
    { "e488a9127ba89f636fd0151da599ddb1", "3.pic" },
    { "d81029a10c5bcb0705ac98b6cc75621d", "4.pic" },
    { "fad492070c3afb3a11f32ad266df428d", "adv.pic" },
    { "f6b8b7b27b1de44282ca04ea6d369ea4", "armpiece.pic" },
    { "e480cd3510d5895f399e996725a58f6a", "ce.3d3" },
    { "b726a340b000005fdf501d61eeb57bcd", "ce.3dg" },
    { "b082fb4983b97804bff002e5824fbc90", "ce.3dt" },
    { "bacf2b850fcc6b592f92f046c89b15c8", "ceurope.spr" },
    { "df0d81f834eb7d8eaefb6b14b9728244", "ce.wld" },
    { "ff1bba14e570245e94e5f1a6186422a3", "cockpit.pic" },
    { "a490a0bf84b2c36dedab4fee0372bd09", "dbicons.spr" },
    { "6e26bcc7228da3563f17c1c919e14349", "death.pic" },
    { "07ae72e86ae2c38cb7293f86cb108e93", "desk.pic" },
    { "9c66ea28fec0daf5e3f11c28b9d7ffea", "f15dgtl.bin" },
    { "ac3d782b7a7c446dcf3036d532a3dbae", "f15.spr" },
    { "887f5679e3a645b2082503ff20fb1aa5", "gulf.wld" },
    { "537bbf274d79ebaa616f55917d14bf19", "hiscore.pic" },
    { "0dcf2b5bba17badda50bdf4273285e99", "jp.3d3" },
    { "e2201bf2eebd8f12859c41676070c4cd", "jp.3dg" },
    { "210b69b49ea60248ecf229e3a35c7832", "jp.3dt" },
    { "e2252dea49554e9d51482e6ba0a22a5b", "jp.spr" },
    { "c499fd1f955fde158159b770eabbc11e", "jp.wld" },
    { "157e724e8daa31336fe9f06255bd73c4", "labs.pic" },
    { "0b8866ceaad15bc03bd7897bd84c66d5", "lb.3d3" },
    { "d10c1d6cc9ade429e6dd81d9f2efc9de", "lb.3dg" },
    { "73b3bfa86cd57a507a2e1591db45ed76", "lb.3dt" },
    { "789455e28757793fec416cc444477063", "left.pic" },
    { "fb234541a8fd588684c80417eebbf729", "libya.spr" },
    { "34ac7cabf7c200aaffe72cf548978fa8", "libya.wld" },
    { "335931f93a4a5bb88129ea28ced2ae8c", "me.3d3" },
    { "ad0dbc926be9758b623995ee70bb777b", "me.3dg" },
    { "0e5a2dc77f195a8e51976575e7160282", "me.3dt" },
    { "3f75ad96c4d9c61842d58e087394d968", "medal.pic" },
    { "8079c4abfa88c04aad08e82908b7554e", "me.spr" },
    { "1e9407d174654d59a2bd34d5fe052caa", "me.wld" },
    { "f8ec10b3de5dfd555748497cc5f88f63", "nc.3d3" },
    { "2534b73170ae55e00b1ecfa706d5e04e", "nc.3dg" },
    { "ace87d2c23046f0fecf50c04e847ab3c", "nc.3dt" },
    { "a019f6eade84d4d6a2c8034850979c14", "ncape.spr" },
    { "4ecabfb63f451c989a51feca22048e19", "nc.wld" },
    { "fff6a0e3d2d16af739d9a36eb413339f", "persian.spr" },
    { "570679af0f0bb2d44eb16226d557bbed", "pg.3d3" },
    { "4cfaf482d83f9a90fe30734e7a701918", "pg.3dg" },
    { "46ced2bc0128d95674e10521fee61cd4", "pg.3dt" },
    { "5527467253f038e296fc2519606e38aa", "photo.3d3" },
    { "fa3efcb64547c7fc73bc165b4d06bbf2", "promo.pic" },
    { "0bbc6acfeaef8c7988cd5e75aaaf6320", "rear.pic" },
    { "94190aa3fe2f7de2395b15dab9c04a53", "right.pic" },
    { "bd7a9c10dcd06fec62af1071a678ad7f", "title16.pic" },
    { "14c7e302d9ba0b3567196f43b2a914a6", "title640.pic" },
    { "3e59272aa51365d4c5dc156844f3d4e8", "vn.3d3" },
    { "f54adf1df9788e90e3408d8609ecd40e", "vn.3dg" },
    { "31a214ca09ac9c5ea8ab61c2c2358928", "vn.3dt" },
    { "17e00b718ea797eff553ffca1d3ba2bb", "vn.spr" },
    { "ed64b9313161e9889454f33f6816ffc9", "vn.wld" },
    { "0839cb62142b5d3e5058b596ad36fb32", "wall.pic" },
    { NULL, NULL }
};

/* Base theater menu: four shipping theaters plus the scenario-disk submenu
 * entry. Menu row == position in this table; slot is the engine theater
 * index used for plh/wld/spr lookups. */
static const GameMenuEntry f15TheaterMenu[] = {
    { "Libya",         "Across the \"Line of Death\"",  0, NULL },
    { "Persian Gulf",  "Keeping the Sea Lanes Open",    1, NULL },
    { "Vietnam",       "America's Longest Air War",     2, NULL },
    { "Middle East",   "Eagles vs MiGs",                3, NULL },
    { "Other Areas",   "Insert your scenario disk",     GAME_MENU_SCENARIOS, NULL },
};

/* "Other Areas" submenu: rows are only selectable when <stem>.3d3 is found
 * (the scenario-disk probe); the last row returns to the theater menu. */
static const GameMenuEntry f15ScenarioMenu[] = {
    { "North Cape",     "Into the Soviet's Backyard",     4, "nc" },
    { "Central Europe", "Red Storm Raging",               5, "ce" },
    { "Desert Storm",   "The Fight for Kuwait",           6, "jp" },
    { "North Atlantic", "Defending the Iceland-UK Gap",   7, "na" },
    { "Other Areas",    "Select a built-in area",         GAME_MENU_BACK, NULL },
};

static const int8 f15CampaignOrder[] = { 0, 1, 2, 3 };

/* The slot tables stay in their original modules - the descriptor points at
 * them so stdata.c/egdata.c/endata.c remain the single source of truth. */
extern const char *plhFiles[];       /* stdata.c */
extern const char *worldFiles[];     /* stdata.c */
extern const char *theaterSprFiles[];/* endata.c */

/* extern: TU compiles as C++, where namespace-scope const would otherwise get
 * internal linkage and stay invisible to gamedesc.c's registry. */
extern const GameDesc g_gameDescF15SE2 = {
    GAMEID_F15SE2,
    "f15",
    "F-15 Strike Eagle II",
    "F15",
    f15Signatures, 2,
    f15Manifest,
    NULL /* aliases: F-15 names are the engine's canonical literals */,
    plhFiles,
    worldFiles,
    theaterSprFiles,
    f15TheaterMenu, 5,
    f15ScenarioMenu, 5,
    f15CampaignOrder, 4,
    1 /* hasHiResTitle */,
    "assets",
    "converted_assets_all",
    GAMEENGINE_MPS_CLASSIC,
    start_main, egame_main, end_main,
};
