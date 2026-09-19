#include "joystick.h"
#include "joystick_axes.h"
#include "joystick_mapping.h"
#include "shared/common.h"
#include "input.h"
#include "egkeys.h"
#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "stcode.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include "slot.h"
#include "const.h"
#define F15_MATH_BOUNDARY_ACCESS
#include "math/control_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS

#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace {

static SDL_Joystick *setupStick = nullptr;

void pressContinueOnDraw(const char *text, int, int, int, int) {
    if (setupStick && SDL_strcmp(text, "> CONTINUE <") == 0) {
        SDL_SetJoystickVirtualButton(setupStick, 0, true);
        setupStick = nullptr;
    }
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << ": " << SDL_GetError() << '\n';
        std::exit(1);
    }
}

void pushKey(SDL_Scancode scancode, SDL_Keycode keycode) {
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    event.key.key = keycode;
    require(SDL_PushEvent(&event), "queue keyboard event");
}

void focus(bool active) {
    SDL_Event event = {};
    event.type = active ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    require(SDL_PushEvent(&event), "queue focus event");
    input_pumpEvents();
}

struct Stick {
    SDL_JoystickID id = 0;
    SDL_Joystick *handle = nullptr;

    Stick(int axes, int buttons, bool gamepad = false) {
        SDL_VirtualJoystickDesc desc = {};
        SDL_INIT_INTERFACE(&desc);
        desc.type = gamepad ? SDL_JOYSTICK_TYPE_GAMEPAD : SDL_JOYSTICK_TYPE_FLIGHT_STICK;
        desc.naxes = axes;
        desc.nbuttons = buttons;
        desc.vendor_id = 0x1234;
        desc.product_id = 0xabcd;
        desc.name = "F15 joystick test";
        id = SDL_AttachVirtualJoystick(&desc);
        require(id != 0, "attach virtual joystick");
        handle = SDL_OpenJoystick(id);
        require(handle != nullptr, "open virtual joystick");
        // Bind the fixture before pumping device arrivals, even when the test
        // machine also has a physical joystick connected.
        SDL_Event added = {};
        added.type = gamepad ? SDL_EVENT_GAMEPAD_ADDED : SDL_EVENT_JOYSTICK_ADDED;
        added.jdevice.which = id;
        joy_handleEvent(&added);
        joy_init();
        input_setMode(INPUT_MODE_FLIGHT);
        input_setJoystickSetup(false);
        focus(true);
        require(joy_isGamepad() == gamepad, "fixture uses requested input backend");
    }

    ~Stick() {
        joy_shutdown();
        SDL_CloseJoystick(handle);
        require(SDL_DetachVirtualJoystick(id), "detach virtual joystick");
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    }

    void button(int index, bool down) {
        require(SDL_SetJoystickVirtualButton(handle, index, down), "set virtual button");
        SDL_UpdateJoysticks();
        input_pumpEvents();
    }

    void axis(int index, Sint16 value) {
        require(SDL_SetJoystickVirtualAxis(handle, index, value), "set virtual axis");
        SDL_UpdateJoysticks();
        input_pumpEvents();
    }
};

void requirePhysicalStick(double roll, double pitch) {
    using namespace f15::math;
    using Boundary = ControlBoundary<ModernBackend>;
    const AnalogResponse identity{Boundary::radiansPerSecond<RollAxis>(1),
        Boundary::radiansPerSecond<PitchAxis>(1), Boundary::radiansPerSecond<PitchAxis>(1), 0};
    const auto commands = FlightControlMath<ModernBackend>::fromAnalog(joy_physicalStick(), identity);
    require(std::abs(Boundary::radiansPerSecond(commands.roll) - roll) < 1e-14 &&
            std::abs(Boundary::radiansPerSecond(commands.pitch) - pitch) < 1e-14,
            "physical stick preserves normalized precision on both axes");
}

void legacyAxisSamples() {
    // Literal observations define the old byte interface independently of the
    // converter. Include both deadzone edges and asymmetric signed endpoints.
    struct Sample { Sint16 raw; int byte; };
    const Sample samples[] = {
        {-32768, 1}, {-32767, 2}, {-16384, 65},
        {-8257, 96}, {-8256, 97}, {-8001, 97}, {-8000, 97},
        {-7999, 128}, {-1, 128}, {0, 128}, {1, 128}, {7999, 128},
        {8000, 159}, {8001, 159}, {8256, 159}, {8257, 160},
        {16384, 191}, {32766, 254}, {32767, 254}
    };
    for (bool gamepad : {false, true}) {
        Stick stick(gamepad ? SDL_GAMEPAD_AXIS_COUNT : 2,
                    gamepad ? SDL_GAMEPAD_BUTTON_COUNT : 2, gamepad);
        for (const auto &sample : samples) {
            for (int axis = 0; axis < 2; ++axis) {
                stick.axis(1 - axis, 0);
                stick.axis(axis, sample.raw);
                const double normalized = sample.raw / (sample.raw < 0 ? 32768.0 : 32767.0);
                requirePhysicalStick(axis == 0 ? normalized : 0, axis == 1 ? normalized : 0);
                const int x = axis == 0 ? sample.byte : 128;
                const int y = axis == 1 ? sample.byte : 128;
                require(readCalibratedJoystick() == (x | (y << 8)),
                        "legacy signed axis sample returns expected packed bytes");
                require(joyAxes[0] == x && joyAxes[1] == y,
                        "legacy reader updates both shared axes");
                pollJoystick();
                require(joyAxes[0] == x && joyAxes[1] == y,
                        "flight polling preserves axis conversion");
                input_setMode(INPUT_MODE_MENU);
                pollJoystick();
                require(joyAxes[0] == 128 && joyAxes[1] == 128,
                        "menu polling centers held axes to avoid duplicate navigation");
                input_setMode(INPUT_MODE_FLIGHT);
            }
        }
    }
    requirePhysicalStick(0, 0);
}

void calibratedPhysicalStick() {
    std::string path;
    {
        Stick stick(2, 2);
        path = joy_mappingPath(stick.handle) + ".calibration";
        std::ofstream profile(path);
        profile << "2\n-20000 123 24000\n-25000 -456 18000\n-1 32767 -32768\n";
        require(profile.good(), "write physical stick calibration fixture");
    }
    {
        Stick stick(2, 2);
        stick.axis(0, 123);
        stick.axis(1, -456);
        requirePhysicalStick(0, 0);
        stick.axis(0, 124);
        stick.axis(1, -457);
        requirePhysicalStick(1.0 / 23877, -1.0 / 24544);
        stick.axis(0, -32768);
        stick.axis(1, 32767);
        requirePhysicalStick(-1, 1);
        stick.axis(0, 24000);
        stick.axis(1, -25000);
        requirePhysicalStick(1, -1);
    }
    std::filesystem::remove(path);
}

void defaultsAndRemapping() {
    for (int count : {1, 2, 4, 6}) {
        Stick stick(2, count);
        for (int action = 0; action < RAW_ACTION_COUNT; ++action)
            require(joy_rawBinding((RawAction)action) == (action < count ? action : -1),
                    "default order preserves upstream fire buttons and respects button count");
        stick.button(0, true);
        require(misc_readJoystick(0) != 0 && misc_readJoystick(1) == 0,
                "first button fires cannon, not missiles");
        stick.button(0, false);
        if (count > 1) {
            stick.button(1, true);
            require(misc_readJoystick(1) != 0 && misc_readJoystick(0) == 0,
                    "second button fires missiles, not cannon");
            stick.button(1, false);
            joy_bindRawButton(RAW_CANNON, 1);
            require(joy_rawBinding(RAW_CANNON) == 1 && joy_rawBinding(RAW_MISSILE) == 0,
                    "assigning occupied button swaps actions");
        }
        joy_bindRawButton(RAW_CANNON, -1);
        require(joy_rawBinding(RAW_CANNON) == -1, "unassign action");
        joy_bindRawButton(RAW_CANNON, count);
        require(joy_rawBinding(RAW_CANNON) == -1, "reject nonexistent button");
        require(joy_throttleChange() == -1, "two-axis stick has no throttle");
    }
}

void flightCommands() {
    Stick stick(2, 6);
    stick.button(2, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_C, "countermeasure starts with chaff");
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "held countermeasure is not repeated");
    stick.button(2, false);
    stick.button(2, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_F, "next countermeasure is flare");
    stick.button(2, false);
    for (int weapon = 0; weapon < 3; ++weapon) {
        stick.button(3, true);
        const int expected[] = {SCAN_M, SCAN_G, SCAN_S};
        require(joy_flightCommand(weapon, VIEW_COCKPIT) == expected[weapon], "cycle follows actual selected weapon");
        stick.button(3, false);
    }
    stick.button(4, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_EQUAL, "button five raises thrust without lever");
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "thrust repeat waits initially");
    SDL_Delay(260);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_EQUAL, "held thrust button repeats");
    stick.button(4, false);
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "release stops thrust repeat");
    stick.button(5, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_MINUS, "button six lowers thrust");
    stick.button(5, false);

    // The game flushes keyboard events; raw commands must survive that flush.
    stick.button(2, true);
    stick.button(3, true);
    input_ringReset();
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_C, "first pending command survives BIOS-ring reset");
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_M, "second simultaneous action survives next step");
    focus(false);
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0 && !joy_rawActive(), "unfocused stick does not control flight");
    focus(true);
}

void autopilotAndViews() {
    Stick stick(2, 10);
    stick.button(6, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_L, "gear uses existing keyboard command");
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "held gear button does not toggle repeatedly");
    stick.button(6, false);
    stick.button(7, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_P, "autopilot uses existing keyboard command");
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "held autopilot button does not toggle repeatedly");
    stick.button(7, false);
    stick.button(8, true);
    require(joy_flightCommand(0, VIEW_COCKPIT) == SCAN_T, "next target uses existing keyboard command");
    require(joy_flightCommand(0, VIEW_COCKPIT) == 0, "held target button does not cycle repeatedly");
    stick.button(8, false);
    const int views[] = {VIEW_COCKPIT, VIEW_EXT_FOLLOW, VIEW_EXT_DYNAMIC, VIEW_EXT_SIDE, VIEW_TARGET};
    const int commands[] = {SCAN_F5, SCAN_F6, SCAN_F7, SCAN_SPACEBAR, SCAN_SPACEBAR};
    for (int i = 0; i < 5; ++i) {
        stick.button(9, true);
        require(joy_flightCommand(0, views[i]) == commands[i], "view cycle follows current camera");
        require(joy_flightCommand(0, views[i]) == 0, "view switch is edge-triggered");
        stick.button(9, false);
    }
}

void throttle() {
#ifdef __linux__
    const unsigned char simple[] = {ABS_X, ABS_Y, ABS_THROTTLE};
    const unsigned char extended[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_THROTTLE};
    const unsigned char twist[] = {ABS_X, ABS_Y, ABS_RZ};
    const unsigned char hats[] = {ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y, ABS_THROTTLE};
    require(joy_linuxThrottleIndex(simple, 3) == 2, "generic three-axis throttle metadata");
    require(joy_linuxThrottleIndex(extended, 5) == 4, "throttle need not be third axis");
    require(joy_linuxThrottleIndex(twist, 3) == -1, "rudder metadata does not mean throttle");
    require(joy_linuxThrottleIndex(hats, 5) == 2, "joydev hats do not shift SDL analog indices");
    require(joy_linuxThrottleIndex(simple, 0) == -1, "missing metadata has no throttle");
#endif
    {
        Stick stick(3, 6);
        stick.axis(2, -32768);
        require(joy_throttleChange() == -1, "unknown third axis is not guessed as throttle");
        require(joy_rawBinding(RAW_THRUST_UP) == 4, "unknown extra axis retains thrust buttons");
    }
    // SDL virtual devices have no kernel metadata; use the explicit assignment
    // to exercise the identical runtime throttle path on every CI platform.
    SDL_setenv_unsafe("F15_JOY_THROTTLE_AXIS", "3", 1);
    {
        Stick stick(3, 6);
        require(joy_rawBinding(RAW_THRUST_UP) == -1 && joy_rawBinding(RAW_THRUST_DOWN) == -1,
                "configured lever leaves thrust buttons unassigned");
        stick.axis(2, 32767);
        require(joy_throttleChange() == 0, "lever idle endpoint");
        require(joy_throttleChange() == -1, "stationary lever does not overwrite keyboard thrust");
        stick.axis(2, -32768);
        require(joy_throttleChange() == 100, "lever full-thrust endpoint");
        stick.axis(2, 0);
        require(joy_throttleChange() == 50, "lever midpoint");
        stick.axis(2, 100);
        require(joy_throttleChange() == -1, "resting lever noise is ignored");
    }
    SDL_setenv_unsafe("F15_JOY_THROTTLE_AXIS", "3", 1);
    SDL_setenv_unsafe("F15_JOY_THROTTLE_INVERT", "0", 1);
    {
        Stick stick(3, 4);
        stick.axis(2, -32768);
        require(joy_throttleChange() == 0, "explicit axis supports opposite polarity");
        stick.axis(2, 32767);
        require(joy_throttleChange() == 100, "opposite polarity full thrust");
    }
    SDL_setenv_unsafe("F15_JOY_THROTTLE_AXIS", "0", 1);
    {
        Stick stick(3, 6);
        require(joy_throttleChange() == -1 && joy_rawBinding(RAW_THRUST_UP) == 4,
                "explicitly disabling throttle restores button fallback");
    }
    SDL_unsetenv_unsafe("F15_JOY_THROTTLE_AXIS");
    SDL_unsetenv_unsafe("F15_JOY_THROTTLE_INVERT");
}

void menuAndSetup() {
    Stick stick(2, 4);
    input_setMode(INPUT_MODE_MENU);
    input_ringReset();
    stick.axis(1, 24000);
    require(input_keyWaiting() && input_readKey() == KEYCODE_DNARROW, "stick selects menu row");
    stick.axis(1, 0);
    stick.axis(0, -24000);
    require(input_keyWaiting() && input_readKey() == KEYCODE_LEFTARROW, "stick selects menu column");
    stick.axis(0, 0);
    stick.button(0, true);
    require(input_keyWaiting() && input_readKey() == SCAN_ENTER, "first menu button confirms");
    require(!input_keyWaiting(), "held menu button does not confirm repeatedly");
    stick.button(0, false);
    stick.button(1, true);
    require(input_keyWaiting() && input_readKey() == SCAN_ESCAPE, "second menu button goes back");
    stick.button(1, false);

    input_setJoystickSetup(true);
    stick.axis(1, 24000);
    stick.button(0, true);
    require(!input_keyWaiting(), "setup suppresses menu translation of stick and buttons");
    require(joy_rawPressedButton() == 0 && joy_rawMenuAxis(1) == 24000,
            "physical controls remain readable during setup");
    pushKey(SDL_SCANCODE_DOWN, SDLK_DOWN);
    require(input_keyWaiting() && input_readKey() == KEYCODE_DNARROW,
            "keyboard still selects during setup");
    stick.axis(1, 0);
    input_setJoystickSetup(false);
    require(!input_keyWaiting(), "held assignment button does not confirm next menu");
    stick.button(0, false);
    stick.button(0, true);
    require(input_keyWaiting() && input_readKey() == SCAN_ENTER,
            "menu input resumes after setup and release");
    stick.button(0, false);

    // Real setup loop must default to Continue and exit with a joystick only.
    setupStick = stick.handle;
    g_textRecorder = pressContinueOnDraw;
    joy_showSetup();
    g_textRecorder = nullptr;
    require(setupStick == nullptr, "Continue is selected by default");
    int pitch = 0;
    const uint8 *pixels = gfx_pagePixels(0, &pitch);
    require(pixels && pixels[8 * pitch + 8] == COLOR_LIGHTRED,
            "actual setup screen renders legacy border headlessly");
    require(!input_keyWaiting(), "setup consumes its continue key");
    stick.button(0, false);
}

void persistence(const std::filesystem::path &directory) {
    const std::string path = (directory / "roundtrip.txt").string();
    int original[RAW_ACTION_COUNT] = {2, 0, 1, 3, -1, -1, -1, -1, -1, -1};
    int loaded[RAW_ACTION_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    for (int i = RAW_VIEW + 1; i < RAW_ACTION_COUNT; ++i) original[i] = loaded[i] = -1;
    require(joy_saveMapping(path, 4, original), "save complete mapping");
    require(joy_loadMapping(path, 4, loaded), "load complete mapping");
    for (int i = 0; i < RAW_ACTION_COUNT; ++i)
        require(loaded[i] == original[i], "roundtrip preserves bindings and disabled actions");
    original[0] = -1;
    require(joy_saveMapping(path, 4, original) && joy_loadMapping(path, 4, loaded) && loaded[0] == -1,
            "replacement save updates existing file");
    original[0] = 0;
    require(!joy_saveMapping(path, 4, original), "duplicate assignments are not saved");
    require(joy_loadMapping(path, 4, loaded) && loaded[0] == -1,
            "rejected save preserves previous profile");

    const char *invalid[] = {
        "F15_JOYSTICK 2\n", "F15_JOYSTICK 1\ncannon 1\n",
        "F15_JOYSTICK 1\ncannon 999999999999999999999\n",
        "F15_JOYSTICK 1\ncannon 1\nmissile 1\ncountermeasure 3\nweapon 4\nthrust_up 0\nthrust_down 0\n",
        "F15_JOYSTICK 1\ncannon 5\nmissile 2\ncountermeasure 3\nweapon 4\nthrust_up 0\nthrust_down 0\n"
    };
    for (const char *contents : invalid) {
        std::ofstream(path) << contents;
        require(!joy_loadMapping(path, 4, loaded), "invalid profile rejected");
        require(loaded[0] == -1 && loaded[1] == 0, "invalid load leaves caller mapping unchanged");
    }
    require(!joy_saveMapping((directory / "missing" / "mapping.txt").string(), 4, loaded),
            "unwritable path reports save failure");
    {
        Stick stick(2, 7);
        joy_bindRawButton(RAW_CANNON, 6);
        require(joy_saveRawMapping(), "save active device mapping");
    }
    {
        Stick stick(2, 7);
        require(joy_rawBinding(RAW_CANNON) == 6, "reopening device restores mapping");
    }
    SDL_setenv_unsafe("F15_JOY_CANNON", "1", 1);
    {
        Stick stick(2, 7);
        require(joy_rawBinding(RAW_CANNON) == 0, "explicit environment overrides saved mapping");
    }
    SDL_unsetenv_unsafe("F15_JOY_CANNON");
    {
        Stick stick(2, 8);
        require(joy_rawBinding(RAW_CANNON) == 0, "different control count gets separate profile");
    }
}

void overridesAndGamepad() {
    SDL_setenv_unsafe("F15_JOY_CANNON", "2", 1);
    SDL_setenv_unsafe("F15_JOY_MISSILE", "1", 1);
    SDL_setenv_unsafe("F15_JOY_COUNTERMEASURE", "0", 1);
    SDL_setenv_unsafe("F15_JOY_WEAPON", "invalid", 1);
    {
        Stick stick(2, 4);
        require(joy_rawBinding(RAW_CANNON) == 1 && joy_rawBinding(RAW_MISSILE) == 0,
                "environment indices are one-based");
        require(joy_rawBinding(RAW_COUNTERMEASURE) == -1, "zero disables a binding");
        require(joy_rawBinding(RAW_WEAPON) == 3, "malformed assignment preserves default");
    }
    SDL_unsetenv_unsafe("F15_JOY_CANNON");
    SDL_unsetenv_unsafe("F15_JOY_MISSILE");
    SDL_unsetenv_unsafe("F15_JOY_COUNTERMEASURE");
    SDL_unsetenv_unsafe("F15_JOY_WEAPON");
    {
        Stick pad(SDL_GAMEPAD_AXIS_COUNT, SDL_GAMEPAD_BUTTON_COUNT, true);
        pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        require(misc_readJoystick(0) != 0, "gamepad right trigger still fires cannon");
        pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
        require(misc_readJoystick(1) != 0, "gamepad left trigger still fires missile");
        require(joy_rawButtonCount() == 0 && joy_throttleChange() == -1,
                "raw defaults do not affect mapped gamepad");
        // Controls setup also serves keyboards now; tested separately below.
    }
}

} // namespace

int main() {
    test_headless_init();
    const auto directory = std::filesystem::temp_directory_path() /
        ("f15-joystick-tests-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(directory);
    SDL_setenv_unsafe("F15_JOY_CONFIG_DIR", directory.string().c_str(), 1);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "initialize SDL");
    gfx_videoInit();
    gfx_initState();
    gfx_setMode13();
    legacyAxisSamples();
    calibratedPhysicalStick();
    defaultsAndRemapping();
    flightCommands();
    autopilotAndViews();
    throttle();
    menuAndSetup();
    overridesAndGamepad();
    persistence(directory);
    gfx_videoShutdown();
    SDL_Quit();
    SDL_unsetenv_unsafe("F15_JOY_CONFIG_DIR");
    std::filesystem::remove_all(directory);
    std::cout << "joystick_behavior_tests passed\n";
}
