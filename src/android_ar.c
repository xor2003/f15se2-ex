/*
 * Thread-safe bridge between the GLES render thread and Android's camera/sensor
 * view. The renderer publishes the game's pitch/roll; Java reads those values
 * while applying the real device attitude to the TextureView.
 */
#include "android_ar.h"

#include <SDL3/SDL.h>
#include <atomic>
#include <jni.h>

static std::atomic<int> g_cameraReady(0);
static std::atomic<float> g_gamePitch(0.0f);
static std::atomic<float> g_gameRoll(0.0f);
static std::atomic<float> g_deviceYawOffset(0.0f);

static float angleToRadians(int angle) {
    const float radiansPerUnit = 6.2831853071795864769f / 65536.0f;
    return (float)(int16_t)angle * radiansPerUnit;
}

int android_ar_requested(void) {
    const char *value = SDL_getenv("F15_AR");
    return value && (value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
                     value[0] == 't' || value[0] == 'T');
}

int android_ar_active(void) {
    return android_ar_requested() && g_cameraReady.load(std::memory_order_acquire);
}

void android_ar_setGameAttitude(int pitchAngle, int rollAngle) {
    g_gamePitch.store(angleToRadians(pitchAngle), std::memory_order_relaxed);
    g_gameRoll.store(angleToRadians(rollAngle), std::memory_order_relaxed);
}

int android_ar_adjustYaw(int yawAngle) {
    const float unitsPerRadian = 65536.0f / 6.2831853071795864769f;
    if (!android_ar_active()) return yawAngle;
    return yawAngle +
           (int)(g_deviceYawOffset.load(std::memory_order_relaxed) *
                 unitsPerRadian);
}

extern "C" JNIEXPORT void JNICALL
Java_org_f15se2_ex_ArCameraView_nativeSetCameraReady(JNIEnv *, jclass, jboolean ready) {
    g_cameraReady.store(ready == JNI_TRUE, std::memory_order_release);
}

extern "C" JNIEXPORT void JNICALL
Java_org_f15se2_ex_ArCameraView_nativeSetDeviceYaw(JNIEnv *, jclass, jfloat yaw) {
    g_deviceYawOffset.store(yaw, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_org_f15se2_ex_ArCameraView_nativeGetGameAttitude(JNIEnv *env, jclass,
                                                       jfloatArray attitude) {
    jfloat values[2] = {
        g_gamePitch.load(std::memory_order_relaxed),
        g_gameRoll.load(std::memory_order_relaxed),
    };
    if (attitude && env->GetArrayLength(attitude) >= 2)
        env->SetFloatArrayRegion(attitude, 0, 2, values);
}
