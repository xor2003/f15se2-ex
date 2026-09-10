/* Startup controls editor in the original bitmap style. Input.c remains the
 * sole SDL event pump; flight assignments never change menu navigation. */
#include "joystick.h"
#include "controls.h"
#include "input.h"
#include "gfx.h"
#include "const.h"
#include "shared/common.h"

static const int visibleRows = 8;
static const int continueRow = RAW_ACTION_COUNT;
static const int resetRow = RAW_ACTION_COUNT + 1;

/* Scroll the action table, keeping Continue and Reset visible on every page. */
static void drawJoystickSetup(int selected, int first, bool keyboard, bool saveFailed) {
    int pitch = 0;
    uint8 *pixels = gfx_pagePixels(0, &pitch);
    if (!pixels) return;
    for (int y = 0; y < LOGICAL_HEIGHT; ++y)
        SDL_memset(pixels + y * pitch, COLOR_BLACK, LOGICAL_WIDTH);
    int16 page[12] = {};
    page[2] = COLOR_LIGHTRED;
    page[6] = 1;
    gfx_setDrawColor(COLOR_LIGHTRED);
    gfx_drawLine(8, 8, 311, 8);
    gfx_drawLine(311, 8, 311, 191);
    gfx_drawLine(311, 191, 8, 191);
    gfx_drawLine(8, 191, 8, 8);
    drawStringCentered(page, "CONTROLS SETUP", 0, 18, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTGRAY;
    drawStringAt(page, keyboard ? "> Keyboard" : "Keyboard", 170, 35);
    drawStringAt(page, keyboard ? "Joystick" : "> Joystick", 258, 35);
    for (int row = 0; row < visibleRows && first + row < RAW_ACTION_COUNT; ++row) {
        const RawAction action = (RawAction)(first + row);
        const int y = 49 + row * 11;
        page[2] = action == selected ? COLOR_WHITE : COLOR_LIGHTGRAY;
        drawStringAt(page, action == selected ? ">" : " ", 14, y);
        drawStringAt(page, controls_action(action).label, 23, y);
        /* Keep the columns bounded with the readable font. The selected
         * binding is also shown in full below the list, including modifiers. */
        const std::string key = controls_keyName(action);
        const std::string shortKey = key.size() > 11 ? key.substr(0, 10) + ">" : key;
        drawStringAt(page, shortKey.c_str(), 170, y);
        char button[16] = {};
        const int binding = joy_rawBinding(action);
        if (binding < 0) SDL_strlcpy(button, "-", sizeof(button));
        else SDL_snprintf(button, sizeof(button), "%d", binding + 1);
        drawStringAt(page, button, 270, y);
    }
    if (selected < RAW_ACTION_COUNT) {
        page[2] = COLOR_WHITE;
        const std::string binding = "Key: " + controls_keyName((RawAction)selected);
        drawStringCentered(page, binding.c_str(), 0, 138, LOGICAL_WIDTH);
    }
    page[2] = selected == continueRow ? COLOR_WHITE : COLOR_LIGHTGRAY;
    drawStringCentered(page, selected == continueRow ? "> CONTINUE <" : "CONTINUE", 0, 151, LOGICAL_WIDTH);
    page[2] = selected == resetRow ? COLOR_WHITE : COLOR_LIGHTGRAY;
    drawStringCentered(page, selected == resetRow ? "> RESET DEFAULTS <" : "RESET DEFAULTS", 0, 162, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTRED;
    drawStringCentered(page, saveFailed ? "Save failed. Continue to play unsaved." :
                       controls_capturing() ? "Press key / chord. ESC cancels." :
                       "ENTER: edit  DEL: clear  ESC: finish", 0, 177, LOGICAL_WIDTH);
    gfx_commitPage();
}

/* Keyboard setup is available without a joystick. A hotplugged device keeps
 * its own profile; losing a stick does not prevent continuing with keys. */
void joy_showSetup(void) {
    controls_resetKeyboard();
    const std::string keyboardPath = controls_keyboardPath();
    controls_loadKeyboard(keyboardPath);
    const InputMode previousMode = input_getMode();
    input_setMode(INPUT_MODE_MENU);
    input_setJoystickSetup(true);
    gfx_setTextInputEnabled(false);
    input_ringReset();
    /* Pilot selection and mission menus use palette 1. Palette 0 gives these
     * same color indices different RGB values, unlike the surrounding menus. */
    gfx_setDac(1);
    SDL_JoystickID device = joy_rawDeviceId();
    int selected = continueRow;
    int first = 0;
    bool keyboard = true;
    bool released = joy_rawPressedButton() < 0;
    bool deleteHeld = false;
    bool finished = false;
    bool saveFailed = false;
    int stickZone = 0;
    Uint64 repeatAt = 0;
    drawJoystickSetup(selected, first, keyboard, saveFailed);
    while (!finished && !input_quitRequested()) {
        const bool wasCapturing = controls_capturing();
        input_pumpEvents();
        if (joy_rawDeviceId() != device) {
            device = joy_rawDeviceId();
            released = false;
        }
        if (!input_hasFocus()) {
            controls_beginCapture((RawAction)-1);
            input_ringReset();
            released = false;
            stickZone = 0;
            SDL_Delay(20);
            continue;
        }
        bool activate = false;
        bool continueRequested = false;
        const bool deleteNow = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_DELETE];
        if (!wasCapturing && !controls_capturing()) {
            if (deleteNow && !deleteHeld && selected < RAW_ACTION_COUNT) {
                if (keyboard) controls_bindKey((RawAction)selected, SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE);
                else joy_bindRawButton((RawAction)selected, -1);
            }
            const int x = joy_rawMenuAxis(0), y = joy_rawMenuAxis(1);
            const int axis = SDL_abs(y) >= SDL_abs(x) ? y : x;
            const int zone = axis < -16000 ? -1 : axis > 16000 ? 1 : 0;
            const Uint64 now = SDL_GetTicks();
            if (zone && (zone != stickZone || now >= repeatAt)) {
                selected = (selected + zone + RAW_ACTION_COUNT + 2) % (RAW_ACTION_COUNT + 2);
                repeatAt = now + (zone != stickZone ? 400 : 200);
            }
            stickZone = zone;
            while (input_keyWaiting()) {
                const uint16 key = input_readKey();
                if ((key & 255) == KEYCODE_ESC) continueRequested = true;
                else if ((key & 255) == KEYCODE_ENTER) activate = true;
                else if (key == KEYCODE_UPARROW) selected = (selected + RAW_ACTION_COUNT + 1) % (RAW_ACTION_COUNT + 2);
                else if (key == KEYCODE_DNARROW) selected = (selected + 1) % (RAW_ACTION_COUNT + 2);
                else if (key == KEYCODE_LEFTARROW) keyboard = true;
                else if (key == KEYCODE_RIGHTARROW) keyboard = false;
            }
            const int pressed = joy_rawPressedButton();
            if (pressed < 0) released = true;
            else if (released) {
                if (selected >= RAW_ACTION_COUNT) activate = true;
                else {
                    keyboard = false;
                    joy_bindRawButton((RawAction)selected, joy_rawBinding((RawAction)selected) == pressed ? -1 : pressed);
                }
                released = false;
            }
            if (activate) {
                if (selected == continueRow) continueRequested = true;
                else if (selected == resetRow) {
                    controls_resetKeyboard();
                    joy_resetRawMapping();
                    saveFailed = false;
                } else if (keyboard) {
                    controls_beginCapture((RawAction)selected);
                    input_ringReset();
                }
            }
        } else {
            /* A captured Enter or arrow must not also activate or move a row. */
            input_ringReset();
            released = joy_rawPressedButton() < 0;
        }
        deleteHeld = deleteNow;
        if (continueRequested) {
            const bool keysSaved = saveFailed || controls_saveKeyboard(keyboardPath);
            const bool stickSaved = saveFailed || !joy_rawDeviceId() || joy_saveRawMapping();
            finished = keysSaved && stickSaved;
            saveFailed = !finished;
            selected = continueRow;
        }
        if (selected < RAW_ACTION_COUNT) {
            if (selected < first) first = selected;
            if (selected >= first + visibleRows) first = selected - visibleRows + 1;
        }
        drawJoystickSetup(selected, first, keyboard, saveFailed);
        SDL_Delay(20);
    }
    controls_beginCapture((RawAction)-1);
    input_setJoystickSetup(false);
    input_setMode(previousMode);
    gfx_setTextInputEnabled(previousMode == INPUT_MODE_MENU);
    joy_resetFlightInput();
}
