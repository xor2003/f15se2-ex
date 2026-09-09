/* Startup-only raw-stick setup, drawn with the original game's bitmap font.
 * It owns no SDL window or event pump; normal quit/focus handling stays active. */
#include "joystick.h"
#include "input.h"
#include "gfx.h"
#include "const.h"
#include "shared/common.h"

/* Render one complete legacy page so normal expose/repaint also works. */
static void drawJoystickSetup(int selected) {
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
    drawStringCentered(page, "JOYSTICK SETUP", 0, 19, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTGRAY;
    drawStringCentered(page, "Assign your flight buttons", 0, 35, LOGICAL_WIDTH);

    static const char *labels[RAW_ACTION_COUNT] = {
        "Fire cannon", "Fire missile", "Chaff / flare", "Cycle weapon",
        "Increase thrust", "Decrease thrust"
    };
    for (int action = 0; action < RAW_ACTION_COUNT; ++action) {
        const int y = 56 + action * 13;
        page[2] = action == selected ? COLOR_WHITE : COLOR_LIGHTGRAY;
        drawStringAt(page, action == selected ? ">" : " ", 19, y);
        drawStringAt(page, labels[action], 32, y);
        char binding[24] = {};
        const int button = joy_rawBinding((RawAction)action);
        if (button < 0)
            SDL_snprintf(binding, sizeof(binding), "Not assigned");
        else
            SDL_snprintf(binding, sizeof(binding), "Button %d", button + 1);
        drawStringAt(page, binding, 210, y);
    }
    page[2] = COLOR_WHITE;
    drawStringCentered(page, "ENTER: CONTINUE", 0, 141, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTRED;
    page[6] = 0;
    drawStringCentered(page, "Move stick: select   Press button: assign", 0, 163, LOGICAL_WIDTH);
    drawStringCentered(page, "DEL: unassign   ESC: continue", 0, 175, LOGICAL_WIDTH);
    gfx_commitPage();
}

/* Show once at application startup. Bindings remain in memory for all missions.
 * A disconnected stick exits safely; menu button presses cannot fire weapons. */
void joy_showSetup(void) {
    if (joy_rawButtonCount() <= 0) return;
    const InputMode previousMode = input_getMode();
    input_setMode(INPUT_MODE_MENU);
    input_setJoystickSetup(true);
    gfx_setTextInputEnabled(false);
    input_ringReset();
    gfx_setDac(0);
    int selected = 0;
    bool released = joy_rawPressedButton() < 0;
    bool deleteHeld = false;
    bool finished = false;
    int stickZone = 0;
    Uint64 repeatAt = 0;
    drawJoystickSetup(selected);
    while (!finished && joy_rawButtonCount() > 0 && !input_quitRequested()) {
        input_pumpEvents();
        if (!input_hasFocus()) {
            input_ringReset();
            released = false;
            stickZone = 0;
            SDL_Delay(20);
            continue;
        }
        const bool *keys = SDL_GetKeyboardState(NULL);
        const bool deleteNow = keys[SDL_SCANCODE_DELETE];
        if (deleteNow && !deleteHeld) {
            joy_bindRawButton((RawAction)selected, -1);
        }
        deleteHeld = deleteNow;
        /* Either primary axis can select rows. Follow the larger deflection,
         * with a broad centre dead zone and delayed repeat to avoid jitter. */
        const int x = joy_rawMenuAxis(0);
        const int y = joy_rawMenuAxis(1);
        const int axis = SDL_abs(y) >= SDL_abs(x) ? y : x;
        const int zone = axis < -16000 ? -1 : axis > 16000 ? 1 : 0;
        const Uint64 now = SDL_GetTicks();
        if (zone && (zone != stickZone || now >= repeatAt)) {
            selected = (selected + zone + RAW_ACTION_COUNT) % RAW_ACTION_COUNT;
            repeatAt = now + (zone != stickZone ? 400 : 200);
        }
        stickZone = zone;
        while (input_keyWaiting()) {
            const uint16 key = input_readKey();
            if ((key & 255) == KEYCODE_ESC || (key & 255) == KEYCODE_ENTER) {
                finished = true;
            } else if (key == KEYCODE_UPARROW || key == KEYCODE_LEFTARROW) {
                selected = (selected + RAW_ACTION_COUNT - 1) % RAW_ACTION_COUNT;
            } else if (key == KEYCODE_DNARROW || key == KEYCODE_RIGHTARROW) {
                selected = (selected + 1) % RAW_ACTION_COUNT;
            }
        }
        const int pressed = joy_rawPressedButton();
        if (pressed < 0) released = true;
        else if (released && !finished) {
            joy_bindRawButton((RawAction)selected, pressed);
            released = false;
        }
        drawJoystickSetup(selected);
        SDL_Delay(20);
    }
    input_setJoystickSetup(false);
    input_setMode(previousMode);
    gfx_setTextInputEnabled(previousMode == INPUT_MODE_MENU);
    joy_resetFlightInput();
}
