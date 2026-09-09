/*
 * joystick.h - SDL3 gamepad / joystick lifecycle (see joystick.c).
 *
 * The game-facing readers (readCalibratedJoystick, pollJoystick,
 * misc_readJoystick) are declared with the rest of the game API in
 * egcode.h / stcode.h / slot.h; these three are the native plumbing only.
 */
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <SDL3/SDL.h>

/* Open SDL's gamepad subsystem and bind the first connected device. */
void joy_init(void);
void joy_shutdown(void);

/* Process device hotplug; called for every event by both SDL event pumps. */
void joy_handleEvent(const SDL_Event *ev);

/* True when a mapped gamepad (not a raw joystick) is the active device, so the
 * named-button flight bindings in eginput.c have known button meanings. */
bool joy_isGamepad(void);

/* True when any device (gamepad or raw joystick) is currently bound. */
bool joy_connected(void);

/* Current state of a gamepad button (false unless a gamepad is active). */
bool joy_button(SDL_GamepadButton b);

/* Current gamepad axis value, -32768..32767 (0 unless a gamepad is active). */
Sint16 joy_axisRaw(SDL_GamepadAxis a);

/* Raw-stick priority bindings; command polling is once per flight step. */
enum RawAction { RAW_CANNON, RAW_MISSILE, RAW_COUNTERMEASURE, RAW_WEAPON,
                 RAW_THRUST_UP, RAW_THRUST_DOWN, RAW_ACTION_COUNT };
/* Setup accesses only the active raw device, never a mapped gamepad. */
int joy_rawButtonCount(void);
int joy_rawPressedButton(void);
/* Calibrated primary axes for setup navigation; zero if unavailable. */
Sint16 joy_rawMenuAxis(int axis);
int joy_rawBinding(RawAction action);
void joy_bindRawButton(RawAction action, int button);
void joy_showSetup(void);
/* Persist the current raw device; false leaves the previous saved file intact. */
bool joy_saveRawMapping(void);
SDL_JoystickID joy_rawDeviceId(void);
bool joy_rawActive(void);
void joy_resetFlightInput(void);
Uint16 joy_flightCommand(int selectedWeapon);
/* Changed thrust percentage (0..100), or -1 when no lever update is pending. */
int joy_throttleChange(void);

#endif /* JOYSTICK_H */
