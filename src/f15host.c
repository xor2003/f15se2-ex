/*
 * f15host.c - network process plumbing for the game executable.
 *
 * Single-executable hosting: the same binary acts as client (--connect),
 * dedicated server (--server) and one-shot host (--host = spawn a --server
 * child, wait for readiness, join it). Keeping the fork/exec/readiness
 * machinery here leaves f15.c close to upstream; see f15host.h for the API.
 */
#include "f15host.h"

#include "net/protocol.h" /* F15_NET_DEFAULT_PORT */
#include "inttype.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* fork/execl/dup2/readlink/read/usleep */
#include <sys/wait.h> /* waitpid */
#include <signal.h>   /* kill */
#include <fcntl.h>    /* open(O_RDONLY) for the readiness scan fd */
#ifdef __linux__
#include <sys/prctl.h> /* PR_SET_PDEATHSIG: child dies with the parent */
#endif

int f15ServerMain(int argc, char **argv); /* server/f15server.cpp, linked in */
void usage(int errcode);                  /* f15.c: missing-arg errors */

/* 20s for the child to boot assets and reach listen(); long enough for a
 * cold start on slow disks, short enough to fail fast on a bad port. */
#define HOST_READY_TIMEOUT_NS 20000000000ULL
/* Marker the server prints once listen() succeeded (f15server.cpp). */
#define HOST_READY_MARKER "listening on"
/* Readiness scan buffer; the tail window keeps the marker findable even
 * when it straddles a buffer-full boundary. */
#define HOST_SCAN_BUF 1024
#define HOST_SCAN_TAIL 64
#define HOST_POLL_US 50000 /* waitpid poll interval while output is quiet */

static pid_t hostChildPid = -1;
static const char *hostArgv0;

static void hostChildCleanup(void) {
    if (hostChildPid > 0) {
        kill(hostChildPid, SIGTERM);
        waitpid(hostChildPid, NULL, 0);
        hostChildPid = -1;
    }
}

/* Spawn this same executable as a headless server child and wait for its
 * "listening on :<port>" line on stderr. The child's stderr goes to a
 * tempfile, not a pipe: a pipe would SIGPIPE the child once we stop
 * reading (or stall it once full). Boot output is still echoed through.
 * Returns 1 once the port is accepting; on failure the child is reaped
 * here. Success leaves hostChildPid set for cleanup. */
int f15HostSpawn(const char *gameDir, const F15NetOpts *o) {
    char exePath[4096], portStr[16], logPath[] = "/tmp/f15hostXXXXXX";
    char buf[HOST_SCAN_BUF];
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
    snprintf(portStr, sizeof(portStr), "%d", o->hostPort);

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
        if (o->hostBind && *o->hostBind)
            execl(exePath, exePath, "--server", "--game",
                  gameDir ? gameDir : ".", "--port", portStr,
                  "--bind", o->hostBind, (char *)NULL);
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

    deadline = SDL_GetTicksNS() + HOST_READY_TIMEOUT_NS;
    while (!ready && SDL_GetTicksNS() < deadline) {
        /* regular file: read() returns 0 at EOF rather than blocking */
        n = read(scanFd, buf + used, sizeof(buf) - 1 - used);
        if (n > 0) {
            fwrite(buf + used, 1, (size_t)n, stderr); /* echo boot log */
            used += (size_t)n;
            buf[used] = 0;
            if (strstr(buf, HOST_READY_MARKER))
                ready = 1;
            /* keep a tail window so the marker can't straddle a full buf */
            if (used > sizeof(buf) - HOST_SCAN_TAIL) {
                memmove(buf, buf + used - HOST_SCAN_TAIL, HOST_SCAN_TAIL);
                used = HOST_SCAN_TAIL;
            }
            continue; /* drain while output flows */
        }
        if (waitpid(hostChildPid, &st, WNOHANG) == hostChildPid) {
            hostChildPid = -1;
            break;
        }
        usleep(HOST_POLL_US);
    }
    close(scanFd);
    if (!ready)
        hostChildCleanup();
    return ready;
}

const char *f15HostJoinAddr(char *buf, size_t bufSz, const F15NetOpts *o) {
    /* A --bind literal becomes the connect target too - the socket lives on
     * that interface, not on loopback; wildcard binds still join via
     * loopback. */
    const char *joinIp = "127.0.0.1";
    if (o->hostBind && *o->hostBind && strcmp(o->hostBind, "0.0.0.0") != 0 &&
        strcmp(o->hostBind, "::") != 0)
        joinIp = o->hostBind;
    snprintf(buf, bufSz, "%s:%d", joinIp, o->hostPort);
    return buf;
}

void f15NetOptsInit(F15NetOpts *o, const char *argv0) {
    memset(o, 0, sizeof(*o));
    o->pilotName = "Viper";
    o->hostPort = F15_NET_DEFAULT_PORT;
    o->serverIdx = -1;
    hostArgv0 = argv0;
}

int f15NetParseOpt(int argc, char **argv, int *i, F15NetOpts *o) {
    const char *optStr = argv[*i];
    if (strcmp(optStr, "--connect") == 0) {
        if (*i + 1 >= argc) { printf("Option requires an argument: --connect\n"); usage(1); }
        o->connectAddr = argv[++*i];
    } else if (strcmp(optStr, "--name") == 0) {
        if (*i + 1 >= argc) { printf("Option requires an argument: --name\n"); usage(1); }
        o->pilotName = argv[++*i];
    } else if (strcmp(optStr, "--host") == 0) {
        o->hostMode = 1;
    } else if (strcmp(optStr, "--bind") == 0) {
        if (*i + 1 >= argc) { printf("Option requires an argument: --bind\n"); usage(1); }
        o->hostBind = argv[++*i];
    } else if (strcmp(optStr, "--port") == 0) {
        if (*i + 1 >= argc) { printf("Option requires an argument: --port\n"); usage(1); }
        o->hostPort = atoi(argv[++*i]);
    } else if (strcmp(optStr, "--server") == 0) {
        o->serverIdx = *i;
        return 2; /* caller stops parsing: the rest belongs to the server */
    } else {
        return 0;
    }
    return 1;
}

void f15NetUsage(void) {
    printf("network options:\n"
           "  --connect HOST[:PORT]  Join a server (default port 27015)\n"
           "  --name NAME            Pilot name for --connect/--host\n"
           "  --host                 Start a local server and join it\n"
           "  --port N               Port for --host (default 27015)\n"
           "  --bind IP              Interface IP --host's server listens on\n"
           "                        (default: all interfaces)\n"
           "  --server [server-opts] Run headless as a dedicated server\n");
}

int f15ServerRun(int argc, char **argv, const F15NetOpts *o, const char *gameDir) {
    /* This process IS the dedicated server (also what --host spawns).
     * A --game given before --server is injected so flag order doesn't
     * matter. */
    char *sargv[32];
    int sn = 0, j;
    sargv[sn++] = argv[0];
    if (gameDir) {
        sargv[sn++] = (char *)"--game";
        sargv[sn++] = (char *)gameDir;
    }
    for (j = o->serverIdx + 1; j < argc && sn < 31; j++)
        sargv[sn++] = argv[j];
    return f15ServerMain(sn, sargv);
}
