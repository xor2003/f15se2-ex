#include "controls.h"
#include "input.h"
#include "egkeys.h"
#include "egtypes.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include "shared/common.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << "\n"; std::exit(1); }
}

static void pressEnter(const char *text, int, int, int, int) {
    if (SDL_strcmp(text, "> CONTINUE <")) return;
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    event.key.key = SDLK_RETURN;
    SDL_PushEvent(&event);
    g_textRecorder = nullptr;
}

int main() {
    test_headless_init();
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL init");
    const auto dir = std::filesystem::temp_directory_path() /
        ("f15-controls-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(dir);
    SDL_setenv_unsafe("F15_JOY_CONFIG_DIR", dir.string().c_str(), 1);
    controls_resetKeyboard();
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        const RawAction action = (RawAction)i;
        const ControlAction &info = controls_action(action);
        require(info.name && *info.name && info.label && *info.label, "every action has metadata");
        if (info.key)
            require(controls_translateKey(info.key, info.modifiers, 0) == info.command, "original keyboard defaults");
    }
    require(controls_translateKey(SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE, 0) == SCAN_ENTER, "keypad Enter alias");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_P, SDL_KMOD_NONE), "swap keys");
    require(controls_translateKey(SDL_SCANCODE_P, SDL_KMOD_NONE, 0) == SCAN_L, "P is gear");
    require(controls_translateKey(SDL_SCANCODE_L, SDL_KMOD_NONE, 0) == SCAN_P, "L is autopilot");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE), "clear key");
    require(controls_translateKey(SDL_SCANCODE_P, SDL_KMOD_NONE, SCAN_P) == 0, "displaced default suppressed");
    require(!controls_bindKey(RAW_GEAR, SDL_SCANCODE_RETURN, SDL_KMOD_ALT), "fullscreen reserved");
    require(!controls_bindKey(RAW_GEAR, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE), "modifier alone rejected");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_F12, SDL_KMOD_CTRL), "arbitrary physical key");
    require(controls_translateKey(SDL_SCANCODE_F12, SDL_KMOD_RCTRL, 0) == SCAN_L, "normalize right modifier");
    input_setMode(INPUT_MODE_FLIGHT);
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_F12;
    event.key.mod = SDL_KMOD_LCTRL;
    SDL_PushEvent(&event);
    require(input_keyWaiting() && input_readKey() == SCAN_L, "real input pump applies mapping");
    controls_beginCapture(RAW_GEAR);
    event.key.scancode = SDL_SCANCODE_LSHIFT;
    event.key.mod = SDL_KMOD_LSHIFT;
    require(controls_captureKey(event.key) && controls_capturing(), "capture waits for key");
    event.key.scancode = SDL_SCANCODE_F11;
    require(controls_captureKey(event.key) && !controls_capturing(), "capture assigns chord");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "captured chord acts");
    controls_beginCapture(RAW_GEAR);
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    controls_captureKey(event.key);
    require(!controls_capturing(), "escape cancels capture");
    const std::string path = controls_keyboardPath();
    require(controls_saveKeyboard(path), "save keyboard");
    controls_resetKeyboard();
    require(controls_translateKey(SDL_SCANCODE_L, SDL_KMOD_NONE, 0) == SCAN_L, "reset restores defaults");
    require(controls_loadKeyboard(path), "load keyboard");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "roundtrip");
    std::ofstream(path) << "F15_KEYBOARD 1\ncannon 999999 0\n";
    require(!controls_loadKeyboard(path), "malformed file rejected");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "failed load preserves mapping");
    require(!controls_saveKeyboard((dir / "missing" / "keyboard.txt").string()), "failed save reported");
    gfx_videoInit();
    gfx_initState();
    gfx_setMode13();
    input_setMode(INPUT_MODE_MENU);
    g_textRecorder = pressEnter;
    joy_showSetup();
    require(g_textRecorder == nullptr, "keyboard-only setup defaults to Continue");
    require(controls_loadKeyboard(path), "setup saves valid defaults after invalid profile");
    gfx_videoShutdown();
    SDL_Quit();
    SDL_unsetenv_unsafe("F15_JOY_CONFIG_DIR");
    std::filesystem::remove_all(dir);
    std::cout << "controls_behavior_tests passed\n";
}
