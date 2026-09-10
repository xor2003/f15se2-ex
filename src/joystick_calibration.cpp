#include "joystick_calibration.h"
#include "joystick.h"
#include "joystick_mapping.h"
#include "input.h"
#include "gfx.h"
#include "const.h"
#include "shared/common.h"
#include <sstream>

namespace {
constexpr int axisMinimum = -32768;
constexpr int axisMaximum = 32767;
constexpr int minimumTravel = 1024; // SDL signed-axis units; reject noise-only captures.
constexpr Uint32 pollMilliseconds = 20;
constexpr int buttonTop = 155;
constexpr int buttonBottom = 180;
constexpr int cancelLeft = 20;
constexpr int cancelRight = 95;
constexpr int nextLeft = 105;
constexpr int nextRight = 300;
enum Step { Center, Travel, Idle, Full };

bool valid(const JoystickCalibration &c) {
    for (int axis = 0; axis < 2; ++axis) {
        if (c.low[axis] < axisMinimum || c.low[axis] > axisMaximum ||
            c.high[axis] < axisMinimum || c.high[axis] > axisMaximum ||
            c.center[axis] < axisMinimum || c.center[axis] > axisMaximum ||
            c.center[axis] - c.low[axis] < minimumTravel ||
            c.high[axis] - c.center[axis] < minimumTravel) return false;
    }
    return c.throttleAxis == -1 ||
        (c.throttleAxis >= 2 && c.idle >= axisMinimum && c.idle <= axisMaximum &&
         c.full >= axisMinimum && c.full <= axisMaximum && SDL_abs(c.full - c.idle) >= minimumTravel);
}

bool save(const std::string &path, const JoystickCalibration &c) {
    std::ostringstream text;
    text << 1;
    for (int axis = 0; axis < 2; ++axis)
        text << ' ' << c.low[axis] << ' ' << c.center[axis] << ' ' << c.high[axis];
    text << ' ' << c.throttleAxis << ' ' << c.idle << ' ' << c.full << '\n';
    const std::string contents = text.str();
    const std::string temporary = path + ".tmp";
    const bool saved = SDL_SaveFile(temporary.c_str(), contents.data(), contents.size()) &&
                       SDL_RenamePath(temporary.c_str(), path.c_str());
    if (!saved) SDL_RemovePath(temporary.c_str());
    return saved;
}

void draw(Step step, const char *error, int x, int y, int throttle) {
    int pitch = 0;
    uint8 *pixels = gfx_pagePixels(0, &pitch);
    if (!pixels) return;
    for (int row = 0; row < LOGICAL_HEIGHT; ++row)
        SDL_memset(pixels + row * pitch, COLOR_BLACK, LOGICAL_WIDTH);
    int16 page[12] = {};
    page[6] = 1;
    page[2] = COLOR_LIGHTRED;
    drawStringCentered(page, "JOYSTICK CALIBRATION", 0, 20, LOGICAL_WIDTH);
    const char *instructions[] = {
        "Release stick to center, then NEXT.",
        "Move stick fully in ALL directions.",
        "Move throttle to IDLE, then NEXT.",
        "Move throttle to FULL, then SAVE."
    };
    page[2] = COLOR_WHITE;
    drawStringCentered(page, instructions[step], 0, 60, LOGICAL_WIDTH);
    char values[64] = {};
    SDL_snprintf(values, sizeof(values), "X %d  Y %d  T %d", x, y, throttle);
    drawStringCentered(page, values, 0, 85, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTRED;
    drawStringCentered(page, error, 0, 115, LOGICAL_WIDTH);
    page[2] = COLOR_WHITE;
    drawStringCentered(page, "CANCEL", cancelLeft, 163, cancelRight);
    drawStringCentered(page, step == Full ? "SAVE" : "NEXT", nextLeft, 163, nextRight);
    page[2] = COLOR_LIGHTGRAY;
    drawStringCentered(page, "Any stick button / ENTER: next", 0, 188, LOGICAL_WIDTH);
    gfx_commitPage();
}
}

int joy_correctAxis(const JoystickCalibration &c, int axis, int value) {
    if (!c.enabled || axis < 0 || axis > 1) return value;
    const int delta = value - c.center[axis];
    const int span = delta < 0 ? c.center[axis] - c.low[axis] : c.high[axis] - c.center[axis];
    // Widen before multiplying: displaced centres can exceed half the SDL range.
    const Sint64 scaled = (Sint64)delta * (delta < 0 ? 32768 : 32767) / span;
    return (int)SDL_clamp(scaled, (Sint64)axisMinimum, (Sint64)axisMaximum);
}

int joy_correctThrottle(const JoystickCalibration &c, int value) {
    return SDL_clamp((value - c.idle) * 100 / (c.full - c.idle), 0, 100);
}

void joy_loadCalibration(const std::string &path, int throttleAxis, JoystickCalibration &calibration) {
    size_t size = 0;
    char *bytes = static_cast<char *>(SDL_LoadFile(path.c_str(), &size));
    if (!bytes) return;
    if (size > 512) { SDL_free(bytes); return; }
    std::istringstream text(std::string(bytes, size));
    SDL_free(bytes);
    int version = 0;
    JoystickCalibration candidate;
    text >> version;
    for (int axis = 0; axis < 2; ++axis)
        text >> candidate.low[axis] >> candidate.center[axis] >> candidate.high[axis];
    text >> candidate.throttleAxis >> candidate.idle >> candidate.full;
    if (!text || version != 1 || candidate.throttleAxis != throttleAxis || !valid(candidate)) return;
    text >> std::ws;
    if (!text.eof()) return;
    candidate.enabled = true;
    calibration = candidate;
}

void joy_calibrationScreen(SDL_Joystick *joystick, int throttleAxis, JoystickCalibration &calibration) {
    if (!joystick || SDL_GetNumJoystickAxes(joystick) < 2) return;
    const SDL_JoystickID device = SDL_GetJoystickID(joystick);
    const std::string path = joy_mappingPath(joystick) + ".calibration";
    JoystickCalibration candidate;
    candidate.throttleAxis = throttleAxis;
    Step step = Center;
    bool released = false;
    const char *error = "";
    input_ringReset();
    while (!input_quitRequested()) {
        input_pumpEvents();
        // Hotplug can close the joystick during the pump; check its ID first.
        if (joy_rawDeviceId() != device) break;
        if (!input_hasFocus()) {
            released = false;
            input_ringReset();
            SDL_Delay(pollMilliseconds);
            continue;
        }
        int axes[2] = {SDL_GetJoystickAxis(joystick, 0), SDL_GetJoystickAxis(joystick, 1)};
        const int throttle = throttleAxis >= 0 ? SDL_GetJoystickAxis(joystick, throttleAxis) : 0;
        if (step == Travel) {
            for (int axis = 0; axis < 2; ++axis) {
                candidate.low[axis] = SDL_min(candidate.low[axis], axes[axis]);
                candidate.high[axis] = SDL_max(candidate.high[axis], axes[axis]);
            }
        }
        const int button = joy_rawPressedButton();
        bool next = button >= 0 && released;
        released = button < 0;
        bool cancel = false;
        while (input_keyWaiting()) {
            const uint16 key = input_readKey();
            if ((key & 255) == KEYCODE_ESC) cancel = true;
            if ((key & 255) == KEYCODE_ENTER) next = true;
            if (key == INPUT_KEY_MENU_POINTER) {
                int x = 0, y = 0;
                if (input_takeMenuPointer(&x, &y) && y >= buttonTop && y < buttonBottom) {
                    if (x >= cancelLeft && x < cancelRight) cancel = true;
                    if (x >= nextLeft && x < nextRight) next = true;
                }
            }
        }
        if (cancel || joy_rawDeviceId() != device) break;
        if (next) {
            error = "";
            if (step == Center) {
                for (int axis = 0; axis < 2; ++axis)
                    candidate.low[axis] = candidate.center[axis] = candidate.high[axis] = axes[axis];
                step = Travel;
            } else if (step == Travel && !valid(candidate)) {
                error = "Move farther in every direction.";
            } else if (step == Travel && throttleAxis >= 0) {
                step = Idle;
            } else if (step == Idle) {
                candidate.idle = throttle;
                step = Full;
            } else {
                if (step == Full) candidate.full = throttle;
                if (!valid(candidate)) error = "Throttle endpoints too close.";
                else if (!save(path, candidate)) error = "Save failed. Try again or CANCEL.";
                else { candidate.enabled = true; calibration = candidate; break; }
            }
        }
        draw(step, error, axes[0], axes[1], throttle);
        SDL_Delay(pollMilliseconds);
    }
    input_ringReset();
}
