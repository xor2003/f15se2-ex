#include <SDL3/SDL.h>
#include <jni.h>
#include <cmath>

/* Android apps cannot inspect evdev files. Ask InputDevice for axis metadata;
 * only throttle samples bypass SDL's potentially overflowing Sint16 conversion. */
static SDL_JoystickID throttleDevice = 0;
static int throttleAxis = -1;

int android_joystickThrottleAxis(SDL_Joystick *joystick) {
    throttleDevice = 0;
    throttleAxis = -1;
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (!env) return -1;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return -1;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetStaticMethodID(
        cls, "joystickThrottleAxis", "(IILjava/lang/String;I)I") : nullptr;
    const char *deviceName = SDL_GetJoystickName(joystick);
    jstring name = method && deviceName ? env->NewStringUTF(deviceName) : nullptr;
    int axis = -1;
    if (name) {
        axis = env->CallStaticIntMethod(cls, method,
            (jint)SDL_GetJoystickVendor(joystick),
            (jint)SDL_GetJoystickProduct(joystick), name,
            (jint)SDL_GetNumJoystickAxes(joystick));
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SDL_Log("Android joystick metadata unavailable; leaving throttle unassigned");
        axis = -1;
    }
    if (name) env->DeleteLocalRef(name);
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    if (axis >= 2 && axis < SDL_GetNumJoystickAxes(joystick)) {
        throttleDevice = SDL_GetJoystickID(joystick);
        throttleAxis = axis;
    }
    return throttleAxis;
}

bool android_joystickThrottleValue(SDL_Joystick *joystick, int axis, double *value) {
    // Explicit overrides selecting another axis retain SDL's normal path.
    if (SDL_GetJoystickID(joystick) != throttleDevice || axis != throttleAxis) {
        *value = SDL_GetJoystickAxis(joystick, axis);
        return true;
    }
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if (!env) return false;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetStaticMethodID(
        cls, "joystickThrottleValue", "()D") : nullptr;
    double sample = NAN;
    if (method) sample = env->CallStaticDoubleMethod(cls, method);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        sample = NAN;
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    if (!std::isfinite(sample)) return false;
    *value = sample;
    return true;
}
