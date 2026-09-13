#ifndef F15_JOYSTICK_CALIBRATION_H
#define F15_JOYSTICK_CALIBRATION_H

#include <SDL3/SDL.h>
#include <string>

struct JoystickCalibration {
    bool enabled = false;
    int low[2] = {-32768, -32768};
    int center[2] = {0, 0};
    int high[2] = {32767, 32767};
    int throttleAxis = -1;
    double idle = 32767;
    double full = -32768;
};

int joy_correctAxis(const JoystickCalibration &calibration, int axis, int value);
int joy_correctThrottle(const JoystickCalibration &calibration, double value);
void joy_loadCalibration(const std::string &path, int throttleAxis, JoystickCalibration &calibration);
void joy_calibrationScreen(SDL_Joystick *joystick, int throttleAxis, JoystickCalibration &calibration);

#endif
