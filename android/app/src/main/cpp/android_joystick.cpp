#include <SDL3/SDL.h>
#include <jni.h>

/* Android apps cannot inspect evdev files. Ask InputDevice for axis metadata;
 * SDL continues to own input values, normalization, and device lifetime. */
int android_joystickThrottleAxis(SDL_Joystick *joystick) {
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
    return axis >= 2 && axis < SDL_GetNumJoystickAxes(joystick) ? axis : -1;
}
