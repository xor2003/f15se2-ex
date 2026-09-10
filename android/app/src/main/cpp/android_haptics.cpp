#include <stdint.h>

#if defined(__ANDROID__)
#include <jni.h>

#include <SDL3/SDL_system.h>
#endif

extern "C" void android_haptics_playerDamage(void) {
#if defined(__ANDROID__)
    const jlong damagePulseMs = 80;
    JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }

    /*
     * Use Android's vibrator service directly rather than coupling gameplay to
     * MainActivity. This keeps the Android integration optional and leaves the
     * desktop input/rendering paths unchanged.
     */
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getSystemService = activityClass == nullptr
        ? nullptr
        : env->GetMethodID(
              activityClass,
              "getSystemService",
              "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = env->NewStringUTF("vibrator");
    jobject vibrator =
        getSystemService == nullptr || serviceName == nullptr
            ? nullptr
            : env->CallObjectMethod(activity, getSystemService, serviceName);
    jclass vibratorClass =
        vibrator == nullptr ? nullptr : env->GetObjectClass(vibrator);
    jmethodID vibrate = vibratorClass == nullptr
        ? nullptr
        : env->GetMethodID(vibratorClass, "vibrate", "(J)V");

    if (vibrate != nullptr) {
        env->CallVoidMethod(vibrator, vibrate, damagePulseMs);
    }

    /*
     * A missing vibrator service is non-fatal. Clear a pending JNI lookup or
     * invocation exception so it cannot leak into SDL's later JNI calls.
     */
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (vibratorClass != nullptr) {
        env->DeleteLocalRef(vibratorClass);
    }
    if (vibrator != nullptr) {
        env->DeleteLocalRef(vibrator);
    }
    if (serviceName != nullptr) {
        env->DeleteLocalRef(serviceName);
    }
    if (activityClass != nullptr) {
        env->DeleteLocalRef(activityClass);
    }
#endif
}
