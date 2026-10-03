/*
 * game.h - multi-game support: per-game descriptor registry, asset-directory
 * detection, filename aliasing and per-game data tables.
 *
 * The engine in this tree is the MicroProse "classic" shared engine used by
 * F-15 Strike Eagle II, F-19 Stealth Fighter and their near relatives (F-117A).
 * Each supported title is described by a static GameDesc in desc_<id>.c:
 * signature files used for detection, the md5 manifest for asset validation,
 * a logical->physical filename alias table (the code was written against the
 * F-15 file names; other games map them onto their own), the per-theater file
 * tables and the theater menu content.
 *
 * Adding a new game means adding a desc_<id>.c, registering it in the
 * gamedesc.c registry, and (for games outside the classic engine family) the
 * matching loaders. Game code outside this module should never test GameId;
 * behavior differences belong in the descriptor.
 */

#ifndef MPSIM_GAME_H
#define MPSIM_GAME_H

#include "inttype.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAMEID_F15SE2 = 0,
    GAMEID_F19,
    GAMEID_COUNT
} GameId;

/* Which engine family a game's data/logic targets. Only the classic
 * MicroProse engine in this tree exists today; entries let detection describe
 * recognized-but-unsupported games instead of silently ignoring them. */
typedef enum {
    GAMEENGINE_MPS_CLASSIC = 0 /* F-15 SE2 / F-19 / F-117A file-format family */
} GameEngine;

/* Sentinel theater-menu slots. */
#define GAME_MENU_SCENARIOS (-2) /* open the scenario-disk submenu */
#define GAME_MENU_BACK      (-3) /* return to the parent menu */

typedef struct {
    const char *md5;      /* expected md5 (hex), NULL = presence check only */
    const char *filename; /* physical file name as it ships on disk */
} GameAssetFile;

typedef struct {
    const char *logical;  /* name as coded in the engine (F-15 lineage names) */
    const char *physical; /* on-disk name for this game; same string = no remap */
} GameFileAlias;

typedef struct {
    const char *label;     /* menu row text; NULL = row disabled/empty */
    const char *desc;      /* menu description line */
    int16 slot;            /* engine theater slot 0-7, GAME_MENU_SCENARIOS or GAME_MENU_BACK */
    const char *probeStem; /* scenario rows: stem probed as "<stem>.3d3"; NULL = always offered */
} GameMenuEntry;

typedef struct GameDesc {
    GameId id;
    const char *cliId;       /* --game-id value, e.g. "f19" */
    const char *displayName; /* "F-19 Stealth Fighter" */
    const char *diskLabel;   /* inserted into "Please insert <x> Disk A/B" prompts */

    /* All signature files must exist for the game to be detected in a dir. */
    const char *const *signatures;
    int signatureCount;

    /* Required asset manifest: verified for presence and md5 at startup.
     * Terminated by an entry with filename == NULL. */
    const GameAssetFile *manifest;

    /* Logical->physical filename remap, terminated by logical == NULL. */
    const GameFileAlias *aliases;

    /* Per-theater-slot tables, indexed by gameData->theater (0-7).
     * Slots a game doesn't ship must still hold a file that exists so stray
     * indexing can't hang the retry loops. Pointers to 8-element arrays. */
    const char *const *plhFiles;        /* region stem placeholder names, e.g. "lb.xxx" */
    const char *const *worldFiles;      /* .wld campaign files */
    const char *const *theaterSprFiles; /* debrief map sprite sheets */

    const GameMenuEntry *theaterMenu;   /* top-level THEATER menu rows */
    int theaterMenuCount;
    const GameMenuEntry *scenarioMenu;  /* "Other Areas" rows, NULL = no submenu */
    int scenarioMenuCount;

    /* Theater slots cycled through by campaign progression, in order. */
    const int8 *campaignOrder;
    int campaignOrderCount;

    int hasHiResTitle;          /* TITLE640.PIC (640x350 EGA title) exists */
    const char *bundledRoot;    /* bundled HD-art dir: "assets" or "assets/f19" */
    /* Converted-asset pack dir name, searched beside the game dir and in the
     * cwd. Per-game so one game's converted tree never shadows another game's
     * same-named assets (F-15: "converted_assets_all"; F-19: "converted_assets_f19"). */
    const char *convertedDir;

    GameEngine engine;

    /* The game's program: the three original DOS sub-programs as in-process
     * entry points. While a game's own module is not ported yet, the
     * descriptor points at another game's module (documented per game). */
    int (*start)(void);
    int (*egame)(void);
    int (*end)(void);
} GameDesc;

/* Detection and activation (gamedesc.c). */

/* Scan `dir` for supported games. Returns the number found (0..cap), filling
 * outIds with each game whose signature files are all present. */
int gameDetectInDir(const char *dir, GameId outIds[], int cap);

/* Resolve which game to run for `dir`. `forcedId` is the --game-id /
 * F15SE2_GAME_ID value or NULL for auto-detection. On success returns the
 * descriptor and makes it active; on failure returns NULL and fills errBuf
 * with a user-facing message. */
const GameDesc *gameSelectForDir(const char *dir, const char *forcedId,
                                 char *errBuf, size_t errBufSize);

const GameDesc *gameActive(void);           /* NULL until gameSelectForDir wins */
const GameDesc *gameById(const char *cliId);
const char *gameIdList(void);               /* "f15se2, f19" for error text */

/* Alias lookup: returns the physical name for a logical file name, or the
 * input unchanged when no alias applies. Case-insensitive. */
const char *gameFileName(const char *logical);

/* Per-slot file accessors; theater is clamped to 0-7. */
const char *gamePlhFile(int theater);
const char *gameWorldFile(int theater);
const char *gameTheaterSprFile(int theater);

const GameMenuEntry *gameTheaterMenu(void);
int gameTheaterMenuCount(void);
const GameMenuEntry *gameScenarioMenu(void);
int gameScenarioMenuCount(void);

/* Top-menu row whose slot matches `theater`, or the row tagged
 * GAME_MENU_SCENARIOS for scenario slots not directly listed, or 0. */
int gameTheaterRowForSlot(int theater);

/* Next theater slot in the game's campaign order after `theater`; -1 when the
 * order wraps (caller bumps difficulty and restarts at campaignOrder[0]), or
 * `theater` itself when it isn't part of the order (leave unchanged). */
int gameCampaignNextTheater(int theater);
const int8 *gameCampaignOrder(int *count); /* order list for wrap restart */

int gameHasHiResTitle(void);
const char *gameBundledRoot(void);
const char *gameConvertedDir(void);
const char *gameDiskLabel(void);

#ifdef __cplusplus
}
#endif

#endif /* MPSIM_GAME_H */
