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
#include "egdata.h"
#include "shared/common.h"
#include "shared/blackbox.h"
#include "shared/blackbox_cli.h"
#include "game/game.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef F15_NET
#include "f15host.h" /* network options, --host spawn, --server dispatch */
#endif

const int RET_MENU = 0xc;
const int RET_DEBRIEFING = 0x23;

void game_init(const int16 showIntro); /* egmain.c (core lib; shared with f15server) */

/* In-process entry points for START.EXE / EGAME.EXE / END.EXE — three separate
 * DOS programs in the original, here linked into this single executable and
 * called directly. */
int start_main(void);
int egame_main(void);
int end_main(void);
bool setGamePath(const char *path);
const char *getGamePath(void);
bool verifyGameAssets();
#include "bundled_campaign.h"
#ifdef F15_NET
int netClientMain(const char *hostPort, const char *name); /* net/netclient.cpp */
#endif

/* Graceful application shutdown, registered with the input pump as the
 * window-close (SDL_EVENT_QUIT) handler so closing the window quits from any
 * phase. Mirrors the normal teardown at the end of main(). */
static void app_quit(void) {
    joy_shutdown();
    r3d_shutdown();
    gfx_videoShutdown();
    blackbox_shutdown();
    exit(0);
}

void usage(int errcode) {
    printf("Usage: f15se2-ex [--help] [--nointro] [--game path] [--game-id id] [--campaign id] [--campaign-sortie id-or-phase] [--scenario id] [--scenario-base stem]\n"
           "--nointro      Skip intro sequence\n"
           "--verbose      Enable verbose/debug logging (on DOS, written to F15.LOG)\n"
           "--game path    Path to directory containing game assets, can also use\n"
           "               F15SE2_DIR env var, default is current directory\n"
           "--game-id id   Force game selection when the directory holds assets for\n"
           "               more than one supported game; can also use\n"
           "               F15SE2_GAME_ID env var\n"
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
    blackbox_cliPrintUsage();
#ifdef F15_NET
    f15NetUsage();
#endif
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
    const char *gameDir = getenv("F15SE2_DIR");
    const char *gameIdArg = getenv("F15SE2_GAME_ID");
    const GameDesc *game;
    char gameErr[512];
    BlackboxCliOptions blackboxOptions;
#ifdef F15_NET
    F15NetOpts netOpts;
    char hostAddr[64];
    f15NetOptsInit(&netOpts, argv[0]);
#endif
    log_set_app("f15");
    blackbox_cliInit(&blackboxOptions);
    if (!setGamePath(gameDir)) goto shutdown;
    /* process cmdline args */
    for (int i = 1; i < argc; ++i) {
        const char* optStr = argv[i];
        if (strcmp(optStr, "--help") == 0) usage(0);
        else if (strcmp(optStr, "--nointro") == 0) showIntro = 0;
        else if (strcmp(optStr, "--verbose") == 0) log_set_verbose();
        else if (strcmp(optStr, "--game") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --game\n"); usage(1); }
            if (!setGamePath(argv[i + 1])) goto shutdown;
            gameDir = argv[i + 1];
            i++;
        }
        else if (strcmp(optStr, "--game-id") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --game-id\n"); usage(1); }
            gameIdArg = argv[i + 1];
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
        else if (int parsed = blackbox_cliParseOption(&blackboxOptions, argc, argv, &i); parsed != 0) {
            if (parsed < 0) usage(1);
        }
#ifdef F15_NET
        else {
            int rc = f15NetParseOpt(argc, argv, &i, &netOpts);
            if (rc == 2)
                break; /* everything after --server belongs to the server parser */
            if (rc == 0) {
                printf("Unrecognized option: '%s'\n", optStr);
                usage(1);
            }
        }
#else
        else {
            printf("Unrecognized option: '%s'\n", optStr);
            usage(1);
        }
#endif
    }
#ifdef F15_BLACKBOX_AUTO_RECORD
    /* Debug builds collect a reproducible report by default. Explicit blackbox
     * modes parsed above always take precedence over this local fallback. */
    blackbox_cliApplyDebugDefaults(&blackboxOptions);
#endif
    if (!blackbox_cliStart(&blackboxOptions)) goto shutdown;

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

#ifdef F15_NET
    if (netOpts.serverIdx > 0)
        return f15ServerRun(argc, argv, &netOpts, gameDir);
    if (netOpts.hostMode) {
        /* --host: spawn ourselves as a headless server child, wait for it to
         * listen, then join as a normal client. The child is reaped on every
         * exit path via the atexit hook registered by the spawn. */
        if (!f15HostSpawn(gameDir, &netOpts)) {
            fprintf(stderr, "f15: --host: local server failed to start\n");
            goto shutdown;
        }
        netOpts.connectAddr = f15HostJoinAddr(hostAddr, sizeof(hostAddr), &netOpts);
    }
#endif

    /* Game selection: --game-id/F15SE2_GAME_ID forces a game; otherwise the
     * asset dir is probed for each game's signature files. Exactly one match
     * selects it; zero or several is an error. The descriptor supplies the
     * asset manifest, filename aliases, theater tables and the module entry
     * points used below. */
    game = gameSelectForDir(getGamePath(), gameIdArg, gameErr, sizeof(gameErr));
    if (!game) {
        printf("%s\n", gameErr);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed", gameErr, NULL);
        goto shutdown;
    }
    log_set_app(game->cliId);
    LogInfo(("game: running %s", game->displayName));

    /* Descriptor -> engine render flags: the renderer reads plain globals so
     * it never links the game registry (unit tests pull the renderer without
     * the descriptor tables). */
    g_skyFlatOnly = (int16)game->flatSkyOnly;

    if (!verifyGameAssets()) goto shutdown;
    gfx_videoInit();
    game_init(showIntro);
    joy_init();
    input_setQuitHandler(app_quit);
    joy_showSetup();

#ifdef F15_NET
    if (netOpts.connectAddr) {
        /* Network client: skip START menus, fly under the server's authority. */
        log_set_app("netclient");
        netClientMain(netOpts.connectAddr, netOpts.pilotName);
        goto shutdown;
    }
#endif

    while (true) {
        int err;
        log_set_app("start");
        blackbox_logPhase("start");
        err = game->start();
        log_set_app(game->cliId);
        if (err != RET_MENU) break;

        log_set_app("egame");
        blackbox_logPhase("egame");
        err = game->egame();
        log_set_app(game->cliId);
        if (err == 0) break;

        log_set_app("end");
        blackbox_logPhase("end");
        err = game->end();
        log_set_app(game->cliId);
        if (err != RET_DEBRIEFING) break;
    }

shutdown:
    joy_shutdown();
    r3d_shutdown();
    gfx_videoShutdown();
    blackbox_shutdown();
    return 0;
}
