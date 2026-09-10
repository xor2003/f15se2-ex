/*
 * SDLActivity looks up this exact unmangled symbol in libmain.so. Keep the
 * platform adapter separate from f15.c so the game's ordinary main() remains
 * identical on desktop and Android.
 */
extern int main(int argc, char **argv);

extern "C" __attribute__((visibility("default")))
int SDL_main(int argc, char **argv) {
    return main(argc, argv);
}
