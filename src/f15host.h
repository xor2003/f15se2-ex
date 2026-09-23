#ifndef F15_SE2_F15HOST_H
#define F15_SE2_F15HOST_H
/*
 * f15host.h - network entry points of the game executable (f15host.c).
 *
 * The game binary doubles as a dedicated server (--server) and as a
 * one-shot host (--host: spawn a local server child, join it as a normal
 * client). main() only sees F15NetOpts and three calls; the process
 * plumbing lives in f15host.c.
 */
#include <stddef.h>

/* Parsed network command line. Defaults come from f15NetOptsInit. */
typedef struct {
    const char *connectAddr; /* --connect HOST[:PORT], NULL = offline */
    const char *pilotName;   /* --name (default "Viper") */
    const char *hostBind;    /* --bind IP for --host's server (NULL = all) */
    int hostPort;            /* --port for --host (default F15_NET_DEFAULT_PORT) */
    int hostMode;            /* --host given */
    int serverIdx;           /* argv index of --server, -1 = not given */
} F15NetOpts;

/* Apply defaults and remember argv[0] (fallback exe path for the --host
 * child spawn). */
void f15NetOptsInit(F15NetOpts *o, const char *argv0);

/* Parse one network option at argv[*i]. Returns 0 when argv[*i] is not a
 * network option, 1 when consumed (a value advances *i), and 2 for --server
 * (the caller stops parsing; the rest belongs to the server parser).
 * Exits via usage(1) on a missing argument. */
int f15NetParseOpt(int argc, char **argv, int *i, F15NetOpts *o);

/* Print the "network options:" block of --help (called inside usage()). */
void f15NetUsage(void);

/* --server dispatch: rebuild argv for f15ServerMain (a --game seen before
 * --server is injected so flag order does not matter) and return its exit
 * code. */
int f15ServerRun(int argc, char **argv, const F15NetOpts *o, const char *gameDir);

/* --host: spawn this same executable as a headless server child and wait
 * for its readiness line. Returns nonzero once the port accepts; the child
 * is reaped on failure here and on every later exit via an atexit hook. */
int f15HostSpawn(const char *gameDir, const F15NetOpts *o);

/* Address the local client should join after a successful f15HostSpawn: a
 * --bind literal (the socket lives on that interface), else loopback. */
const char *f15HostJoinAddr(char *buf, size_t bufSz, const F15NetOpts *o);

#endif /* F15_SE2_F15HOST_H */
