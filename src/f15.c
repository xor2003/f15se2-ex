/*
 * This is the main game executable for F15 SE II.
 *
 * The original was apparently coded in assembly (and included a convoluted anti-debugging & copy protection scheme);
 * this is a purposefully divergent implementation that does not try to be faithful to the original binary.
 *
 * It also subsumes the functionality of SU.EXE (SetUp), whose purpose was to ask the user which graphics adapter and
 * sound device they wanted. We only support VGA (MCGA) and no sound, so there are no queries: the COMM values the
 * sub-programs read are simply populated with the VGA / no-sound configuration.
 *
 * The purpose of this executable is to do some initial setup and then operate the game main loop, running
 * START, EGAME and END in sequence until the user decides to exit.
 */

#include "inttype.h"
#include "log.h"
#include "comm.h"
#include "offsets.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "joystick.h"
#include "r3d.h"
#include "input.h"
#include "shared/common.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL3/SDL.h>

const int RET_MENU = 0xc;
const int RET_DEBRIEFING = 0x23;

struct GameComm commBuffer;
struct Game gameBuffer;

void game_init(const int16 showIntro) {
    commData = &commBuffer;
    gameData = &gameBuffer;

    commData->needSplash = showIntro;
    commData->setupUseJoy = 0;
    commData->setupDetail = 4; /* 4 = extended detail: full LOD + long-range draw distance */

    gfx_initState();
    gfx_setMode13();
    r3d_init();

    /* F15.SPR (radar/tac-map/HUD sprite sheet) lives in a sprite-buffer image, not
     * a page. Allocated once here; START decodes f15.spr into it and EGAME blits
     * from it via gfxBufPtr (the handle). */
    commData->gfxInitResult = (int16)gfx_allocSpriteBuf();
}

/* In-process entry points for START.EXE / EGAME.EXE / END.EXE — three separate
 * DOS programs in the original, here linked into this single executable and
 * called directly. */
int start_main(void);
int egame_main(void);
int end_main(void);
bool setGamePath(const char *path);
bool verifyGameAssets();
#include "bundled_campaign.h"

/* Graceful application shutdown, registered with the input pump as the
 * window-close (SDL_EVENT_QUIT) handler so closing the window quits from any
 * phase. Mirrors the normal teardown at the end of main(). */
static void app_quit(void) {
    joy_shutdown();
    r3d_shutdown();
    gfx_videoShutdown();
    exit(0);
}

void usage(int errcode) {
    printf("Usage: f15se2-ex [--help] [--nointro] [--game path] [--campaign id] [--campaign-sortie id-or-phase] [--scenario id] [--scenario-base stem]\n"
           "--nointro      Skip intro sequence\n"
           "--game path    Path to directory containing game assets, can also use\n"
           "               F15SE2_DIR env var, default is current directory\n"
           "--campaign id  Load campaign.json from replacement assets, for example\n"
           "               SVN/campaign.json; can also use F15_CAMPAIGN env var\n"
           "--campaign-sortie id-or-phase\n"
           "               Select campaign sortie by sortie id or numeric phase; can\n"
           "               also use F15_CAMPAIGN_SORTIE env var\n"
           "--scenario id  Load a custom WLD JSON scenario from replacement assets,\n"
           "               for example SVN/SVN.WLD.json; can also use\n"
           "               F15_WORLD_SCENARIO env var\n"
           "--scenario-base stem\n"
           "               Only redirect this legacy WLD stem, for example VN; can\n"
           "               also use F15_WORLD_SCENARIO_BASE env var\n");
    exit(errcode);
}

int main(int argc, char *argv[]) {
    int16 showIntro = 1;
    int campaignSet = 0;
    int scenarioSet = 0;
    int scenarioBaseSet = 0;
    const char *campaignArg = NULL;
    const char *campaignSortieArg = NULL;
    const char *scenarioArg = NULL;
    const char *scenarioBaseArg = NULL;
    log_set_app("f15");
    if (!setGamePath(getenv("F15SE2_DIR"))) goto shutdown;
    /* process cmdline args */
    for (int i = 1; i < argc; ++i) {
        const char* optStr = argv[i];
        if (strcmp(optStr, "--help") == 0) usage(0);
        else if (strcmp(optStr, "--nointro") == 0) showIntro = 0;
        else if (strcmp(optStr, "--game") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --game\n"); usage(1); }
            if (!setGamePath(argv[i + 1])) goto shutdown;
            i++;
        }
        else if (strcmp(optStr, "--campaign") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --campaign\n"); usage(1); }
            campaignArg = argv[i + 1];
            campaignSet = 1;
            i++;
        }
        else if (strcmp(optStr, "--campaign-sortie") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --campaign-sortie\n"); usage(1); }
            campaignSortieArg = argv[i + 1];
            i++;
        }
        else if (strcmp(optStr, "--scenario") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --scenario\n"); usage(1); }
            scenarioArg = argv[i + 1];
            scenarioSet = 1;
            i++;
        }
        else if (strcmp(optStr, "--scenario-base") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --scenario-base\n"); usage(1); }
            scenarioBaseArg = argv[i + 1];
            scenarioBaseSet = 1;
            i++;
        }
        else {
            printf("Unrecognized option: '%s'\n", optStr);
            usage(1);
        }
    }

    if (selectBundledCampaign(argc, argv)) {
        campaignArg = "SVN";
        campaignSet = 1;
    }

    /* Resolve env defaults after --game has selected the asset directory.
     * Explicit CLI values win over environment. Explicit scenario/base values
     * also intentionally override values loaded from a campaign manifest. */
    if (campaignSortieArg) setCustomCampaignSortie(campaignSortieArg);
    else if (getenv("F15_CAMPAIGN_SORTIE")) setCustomCampaignSortie(getenv("F15_CAMPAIGN_SORTIE"));
    if (campaignSet) {
        if (!setCustomWorldCampaign(campaignArg)) goto shutdown;
    } else if (getenv("F15_CAMPAIGN") && !setCustomWorldCampaign(getenv("F15_CAMPAIGN"))) {
        goto shutdown;
    }
    if (scenarioSet) setCustomWorldScenario(scenarioArg);
    else if (getenv("F15_WORLD_SCENARIO")) setCustomWorldScenario(getenv("F15_WORLD_SCENARIO"));
    if (scenarioBaseSet) setCustomWorldScenarioBase(scenarioBaseArg);
    else if (getenv("F15_WORLD_SCENARIO_BASE")) setCustomWorldScenarioBase(getenv("F15_WORLD_SCENARIO_BASE"));

    if (!verifyGameAssets()) goto shutdown;
    gfx_videoInit();
    game_init(showIntro);
    joy_init();
    input_setQuitHandler(app_quit);
    joy_showSetup();

    while (true) {
        int err;
        log_set_app("start");
        err = start_main();
        log_set_app("f15");
        if (err != RET_MENU) break;

        log_set_app("egame");
        err = egame_main();
        log_set_app("f15");
        if (err == 0) break;

        log_set_app("end");
        err = end_main();
        log_set_app("f15");
        if (err != RET_DEBRIEFING) break;
    }

shutdown:
    joy_shutdown();
    r3d_shutdown();
    gfx_videoShutdown();
    return 0;
}
