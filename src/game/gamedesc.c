/*
 * gamedesc.c - game descriptor registry, asset-directory detection and the
 * active-game accessors the engine reads. Descriptor data lives in
 * desc_<id>.c; this file owns the lookup logic only.
 */

#include "game.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <filesystem>

namespace fs = std::filesystem;

/* Registry: one descriptor per supported game. Add new games here. */
extern const GameDesc g_gameDescF15SE2;
extern const GameDesc g_gameDescF19;

static const GameDesc *const g_registry[GAMEID_COUNT] = {
    &g_gameDescF15SE2,
    &g_gameDescF19,
};

static const GameDesc *g_active = NULL;

const GameDesc *gameActive(void) {
    return g_active;
}

const GameDesc *gameById(const char *cliId) {
    int i;
    if (!cliId) return NULL;
    for (i = 0; i < (int)GAMEID_COUNT; i++) {
        if (!SDL_strcasecmp(g_registry[i]->cliId, cliId)) return g_registry[i];
    }
    return NULL;
}

const char *gameIdList(void) {
    static char ids[128];
    int i;
    if (!ids[0]) {
        for (i = 0; i < (int)GAMEID_COUNT; i++) {
            if (i) strcat(ids, ", ");
            strcat(ids, g_registry[i]->cliId);
        }
    }
    return ids;
}

static int signatureFileExists(const fs::path &dir, const char *name) {
    char lower[64], upper[64];
    size_t j;
    std::error_code ec;
    /* Game files ship UPPERCASE on DOS media; accept the lowercase spelling too
     * so a converted/renamed install still detects. */
    if (fs::exists(dir / name, ec)) return 1;
    for (j = 0; name[j] && j < sizeof(lower) - 1; j++) {
        lower[j] = (char)tolower((unsigned char)name[j]);
        upper[j] = (char)toupper((unsigned char)name[j]);
    }
    lower[j] = upper[j] = 0;
    return fs::exists(dir / lower, ec) || fs::exists(dir / upper, ec);
}

static int gameSignaturesPresent(const GameDesc *desc, const fs::path &dir) {
    int i;
    for (i = 0; i < desc->signatureCount; i++) {
        if (!signatureFileExists(dir, desc->signatures[i])) return 0;
    }
    return 1;
}

int gameDetectInDir(const char *dir, GameId outIds[], int cap) {
    const fs::path root{dir ? dir : "."};
    int found = 0;
    int i;
    for (i = 0; i < (int)GAMEID_COUNT && found < cap; i++) {
        if (gameSignaturesPresent(g_registry[i], root)) {
            if (outIds) outIds[found] = (GameId)i;
            found++;
        }
    }
    return found;
}

const GameDesc *gameSelectForDir(const char *dir, const char *forcedId,
                                 char *errBuf, size_t errBufSize) {
    GameId found[GAMEID_COUNT];
    int count;
    int i;

    if (forcedId && forcedId[0]) {
        const GameDesc *forced = gameById(forcedId);
        if (!forced) {
            snprintf(errBuf, errBufSize,
                     "Unknown game id '%s' (supported: %s)", forcedId, gameIdList());
            return NULL;
        }
        if (!gameSignaturesPresent(forced, fs::path{dir ? dir : "."})) {
            snprintf(errBuf, errBufSize,
                     "Directory %s does not contain %s game assets "
                     "(expected e.g. %s)",
                     dir && dir[0] ? dir : ".", forced->displayName,
                     forced->signatures[0]);
            return NULL;
        }
        g_active = forced;
        return g_active;
    }

    count = gameDetectInDir(dir, found, GAMEID_COUNT);
    if (count == 1) {
        g_active = g_registry[found[0]];
        return g_active;
    }
    if (count == 0) {
        std::string sigHint;
        for (i = 0; i < (int)GAMEID_COUNT; i++) {
            const GameDesc *d = g_registry[i];
            int s;
            sigHint += "  ";
            sigHint += d->displayName;
            sigHint += ": ";
            for (s = 0; s < d->signatureCount; s++) {
                if (s) sigHint += " + ";
                sigHint += d->signatures[s];
            }
            if (i + 1 < (int)GAMEID_COUNT) sigHint += "\n";
        }
        snprintf(errBuf, errBufSize,
                 "No supported game assets found in %s.\n%s",
                 dir && dir[0] ? dir : ".", sigHint.c_str());
        return NULL;
    }

    {
        std::string names;
        for (i = 0; i < count; i++) {
            if (i) names += ", ";
            names += g_registry[found[i]]->displayName;
        }
        snprintf(errBuf, errBufSize,
                 "Directory %s contains assets for multiple games (%s).\n"
                 "Use --game-id (%s) to pick one.",
                 dir && dir[0] ? dir : ".", names.c_str(), gameIdList());
        return NULL;
    }
}

const char *gameFileName(const char *logical) {
    const GameDesc *g = g_active;
    const GameFileAlias *a;
    if (!g || !logical) return logical;
    for (a = g->aliases; a && a->logical; a++) {
        if (!SDL_strcasecmp(a->logical, logical)) return a->physical;
    }
    return logical;
}

static int clampTheater(int theater) {
    if (theater < 0) return 0;
    if (theater > 7) return 7;
    return theater;
}

const char *gamePlhFile(int theater) {
    return g_active ? g_active->plhFiles[clampTheater(theater)] : "lb.xxx";
}

const char *gameWorldFile(int theater) {
    return g_active ? g_active->worldFiles[clampTheater(theater)] : "Libya.wld";
}

const char *gameTheaterSprFile(int theater) {
    return g_active ? g_active->theaterSprFiles[clampTheater(theater)] : "libya.spr";
}


const GameMenuEntry *gameTheaterMenu(void) {
    return g_active ? g_active->theaterMenu : NULL;
}

int gameTheaterMenuCount(void) {
    return g_active ? g_active->theaterMenuCount : 0;
}

const GameMenuEntry *gameScenarioMenu(void) {
    return g_active ? g_active->scenarioMenu : NULL;
}

int gameScenarioMenuCount(void) {
    return g_active ? g_active->scenarioMenuCount : 0;
}

int gameTheaterRowForSlot(int theater) {
    const GameDesc *g = g_active;
    int i;
    if (!g) return 0;
    for (i = 0; i < g->theaterMenuCount; i++) {
        if (g->theaterMenu[i].slot == theater) return i;
    }
    for (i = 0; i < g->theaterMenuCount; i++) {
        if (g->theaterMenu[i].slot == GAME_MENU_SCENARIOS) return i;
    }
    return 0;
}

int gameCampaignNextTheater(int theater) {
    const GameDesc *g = g_active;
    int i;
    /* Slot not part of the campaign order: leave unchanged. */
    if (!g || !g->campaignOrder || g->campaignOrderCount <= 0) return theater;
    for (i = 0; i < g->campaignOrderCount; i++) {
        if (g->campaignOrder[i] == theater) {
            return (i + 1 < g->campaignOrderCount) ? g->campaignOrder[i + 1] : -1;
        }
    }
    return theater;
}

const int8 *gameCampaignOrder(int *count) {
    if (count) *count = g_active ? g_active->campaignOrderCount : 0;
    return g_active ? g_active->campaignOrder : NULL;
}

int gameHasHiResTitle(void) {
    return g_active ? g_active->hasHiResTitle : 0;
}

const char *gameBundledRoot(void) {
    return g_active ? g_active->bundledRoot : "assets";
}

const char *gameConvertedDir(void) {
    return g_active && g_active->convertedDir ? g_active->convertedDir
                                              : "converted_assets_all";
}

const char *gameDiskLabel(void) {
    return g_active ? g_active->diskLabel : "F15";
}
