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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL3/SDL.h>

#ifdef F15_NET
#include "net/protocol.h"
#include <unistd.h>   /* --host: fork/execl/dup2/readlink/read/usleep */
#include <sys/wait.h> /* waitpid */
#include <signal.h>   /* kill */
#include <fcntl.h>    /* open(O_RDONLY) for the readiness scan fd */
#ifdef __linux__
#include <sys/prctl.h> /* PR_SET_PDEATHSIG: child dies with the parent */
#endif
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
bool verifyGameAssets();
#ifdef F15_NET
int netClientMain(const char *hostPort, const char *name);
int f15ServerMain(int argc, char **argv); /* f15server.cpp - also linked in */
#endif

/* Graceful application shutdown, registered with the input pump as the
 * window-close (SDL_EVENT_QUIT) handler so closing the window quits from any
 * phase. Mirrors the normal teardown at the end of main(). */
static void app_quit(void) {
    joy_shutdown();
    r3d_shutdown();
    gfx_videoShutdown();
    exit(0);
}

#ifdef F15_NET
/* --host: pid of the spawned "--server" child; reaped on any exit path. */
static pid_t hostChildPid = -1;
static const char *hostArgv0;

static void hostChildCleanup(void) {
    if (hostChildPid > 0) {
        kill(hostChildPid, SIGTERM);
        waitpid(hostChildPid, NULL, 0);
        hostChildPid = -1;
    }
}

/* --host: spawn this same executable as a headless server child and wait
 * for its "listening on :<port>" line on stderr. The child's stderr goes to
 * a tempfile, not a pipe: a pipe would SIGPIPE the child once we stop
 * reading (or stall it once full). Boot output is still echoed through.
 * Returns 1 once the port is accepting; on failure the child is reaped
 * here. Success leaves hostChildPid set for cleanup. */
static int spawnHostServer(const char *gameDir, int port, const char *bindAddr) {
    char exePath[4096], portStr[16], logPath[] = "/tmp/f15hostXXXXXX";
    char buf[1024];
    size_t used = 0;
    ssize_t n;
    uint64_t deadline;
    int ready = 0, st, logFd, scanFd;

    n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n > 0)
        exePath[n] = 0;
    else {
        strncpy(exePath, hostArgv0 ? hostArgv0 : "f15se2-ex", sizeof(exePath) - 1);
        exePath[sizeof(exePath) - 1] = 0;
    }
    snprintf(portStr, sizeof(portStr), "%d", port);

    logFd = mkstemp(logPath);
    if (logFd < 0)
        return 0;
    /* second fd with an independent file offset for scanning; unlink keeps
     * the tempfile alive only via the open descriptions */
    scanFd = open(logPath, O_RDONLY);
    unlink(logPath);
    if (scanFd < 0) {
        close(logFd);
        return 0;
    }

    hostChildPid = fork();
    if (hostChildPid == 0) {
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM); /* never outlive the client */
        if (getppid() == 1)
            _exit(1); /* parent died before prctl took effect */
#endif
        close(scanFd);
        dup2(logFd, STDERR_FILENO);
        if (logFd != STDERR_FILENO)
            close(logFd);
        if (bindAddr && *bindAddr)
            execl(exePath, exePath, "--server", "--game",
                  gameDir ? gameDir : ".", "--port", portStr,
                  "--bind", bindAddr, (char *)NULL);
        else
            execl(exePath, exePath, "--server", "--game",
                  gameDir ? gameDir : ".", "--port", portStr, (char *)NULL);
        _exit(127);
    }
    close(logFd);
    if (hostChildPid < 0) {
        close(scanFd);
        return 0;
    }
    atexit(hostChildCleanup);

    deadline = SDL_GetTicksNS() + 20000000000ULL; /* 20s for asset boot */
    while (!ready && SDL_GetTicksNS() < deadline) {
        /* regular file: read() returns 0 at EOF rather than blocking */
        n = read(scanFd, buf + used, sizeof(buf) - 1 - used);
        if (n > 0) {
            fwrite(buf + used, 1, (size_t)n, stderr); /* echo boot log */
            used += (size_t)n;
            buf[used] = 0;
            if (strstr(buf, "listening on"))
                ready = 1;
            /* keep a tail window so the marker can't straddle a full buf */
            if (used > sizeof(buf) - 64) {
                memmove(buf, buf + used - 64, 64);
                used = 64;
            }
            continue; /* drain while output flows */
        }
        if (waitpid(hostChildPid, &st, WNOHANG) == hostChildPid) {
            hostChildPid = -1;
            break;
        }
        usleep(50000);
    }
    close(scanFd);
    if (!ready)
        hostChildCleanup();
    return ready;
}
#endif

void usage(int errcode) {
    printf("Usage: f15se2-ex [--help] [--nointro] [--game path] [network options]\n"
           "--nointro       Skip intro sequence\n"
           "--game path     Path to directory containing game assets, can also use\n"
           "                F15SE2_DIR env var, default is current directory\n"
#ifdef F15_NET
           "network options:\n"
           "  --connect HOST[:PORT]  Join a server (default port 27015)\n"
           "  --name NAME            Pilot name for --connect/--host\n"
           "  --host                 Start a local server and join it\n"
           "  --port N               Port for --host (default 27015)\n"
           "  --bind IP              Interface IP --host's server listens on\n"
           "                        (default: all interfaces)\n"
           "  --server [server-opts] Run headless as a dedicated server\n"
#endif
    );
    exit(errcode);
}

int main(int argc, char *argv[]) {
    int16 showIntro = 1;
    const char *netAddr = 0;
    const char *pilotName = "Viper";
    const char *gameDir = getenv("F15SE2_DIR");
#ifdef F15_NET
    char hostAddr[64];
    int serverIdx = -1, hostMode = 0;
    int hostPort = F15_NET_DEFAULT_PORT;
    const char *hostBind = 0;
    hostArgv0 = argv[0];
#endif
    log_set_app("f15");
    if (!setGamePath(gameDir)) goto shutdown;
    /* process cmdline args */
    for (int i = 1; i < argc; ++i) {
        const char* optStr = argv[i];
        if (strcmp(optStr, "--help") == 0) usage(0);
        else if (strcmp(optStr, "--nointro") == 0) showIntro = 0;
        else if (strcmp(optStr, "--game") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --game\n"); usage(1); }
            if (!setGamePath(argv[i + 1])) goto shutdown;
            gameDir = argv[i + 1];
            i++;
        }
#ifdef F15_NET
        else if (strcmp(optStr, "--connect") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --connect\n"); usage(1); }
            netAddr = argv[++i];
        }
        else if (strcmp(optStr, "--name") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --name\n"); usage(1); }
            pilotName = argv[++i];
        }
        else if (strcmp(optStr, "--host") == 0) hostMode = 1;
        else if (strcmp(optStr, "--bind") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --bind\n"); usage(1); }
            hostBind = argv[++i];
        }
        else if (strcmp(optStr, "--port") == 0) {
            if (i + 1 >= argc) { printf("Option requires an argument: --port\n"); usage(1); }
            hostPort = atoi(argv[++i]);
        }
        else if (strcmp(optStr, "--server") == 0) {
            serverIdx = i;
            break; /* everything after --server belongs to the server parser */
        }
#endif
        else {
            printf("Unrecognized option: '%s'\n", optStr);
            usage(1);
        }
    }

#ifdef F15_NET
    if (serverIdx > 0) {
        /* --server: this process IS the dedicated server (also what --host
         * spawns). A --game given before --server is injected so flag order
         * doesn't matter. */
        char *sargv[32];
        int sn = 0;
        sargv[sn++] = argv[0];
        if (gameDir) {
            sargv[sn++] = (char *)"--game";
            sargv[sn++] = (char *)gameDir;
        }
        for (int j = serverIdx + 1; j < argc && sn < 31; j++)
            sargv[sn++] = argv[j];
        return f15ServerMain(sn, sargv);
    }
    if (hostMode) {
        /* --host: spawn ourselves as a headless server child, wait for it to
         * listen, then join as a normal client. The child is reaped on every
         * exit path via the atexit hook registered by the spawn. A --bind
         * literal becomes the connect target too - the socket lives on that
         * interface, not on loopback; wildcard binds still join via loopback. */
        if (!spawnHostServer(gameDir, hostPort, hostBind)) {
            fprintf(stderr, "f15: --host: local server failed to start\n");
            goto shutdown;
        }
        {
            const char *joinIp = "127.0.0.1";
            if (hostBind && *hostBind && strcmp(hostBind, "0.0.0.0") != 0 &&
                strcmp(hostBind, "::") != 0)
                joinIp = hostBind;
            snprintf(hostAddr, sizeof(hostAddr), "%s:%d", joinIp, hostPort);
        }
        netAddr = hostAddr;
    }
#endif

    if (!verifyGameAssets()) goto shutdown;
    gfx_videoInit();
    game_init(showIntro);
    joy_init();
    input_setQuitHandler(app_quit);

#ifdef F15_NET
    if (netAddr) {
        /* Network client: skip START menus, fly under the server's authority. */
        log_set_app("netclient");
        netClientMain(netAddr, pilotName);
        goto shutdown;
    }
#endif

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
