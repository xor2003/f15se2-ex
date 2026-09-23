/*
 * server_main.cpp - thin standalone entry for the dedicated server binary.
 *
 * The server itself lives in f15server.cpp and is ALSO linked into f15se2-ex
 * so a single game executable can run `f15se2-ex --server` (and --host, which
 * spawns that mode as a child). This file just keeps the separate f15server
 * binary for dedicated/headless deployments.
 */

int f15ServerMain(int argc, char **argv);

int main(int argc, char **argv) { return f15ServerMain(argc, argv); }
