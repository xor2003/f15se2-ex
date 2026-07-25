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
static std::atomic<float> g_devicePitchOffset(0.0f);
static std::atomic<float> g_deviceRollOffset(0.0f);
static std::atomic<float> g_flightYawCenter(0.0f);
static std::atomic<float> g_flightPitchCenter(0.0f);
static std::atomic<float> g_flightRollCenter(0.0f);
static std::atomic<float> g_flightGamePitchCenter(0.0f);
static std::atomic<float> g_flightGameRollCenter(0.0f);
static std::atomic<int> g_flightGameCenterPending(0);
static std::atomic<float> g_lookPitchOrigin(0.0f);
static std::atomic<float> g_lookRollOrigin(0.0f);
static std::atomic<float> g_swipePitch(0.0f);
static std::atomic<int> g_lookMode(0);
static float g_smoothFlightRoll = 0.0f;
static float g_smoothFlightPitch = 0.0f;
static float g_previousGameRoll = 0.0f;
static float g_previousGamePitch = 0.0f;
static float g_gameRollRate = 0.0f;
static float g_gamePitchRate = 0.0f;

static const float PI = 3.14159265358979323846f;
static const float FLIGHT_MAX_TILT = 40.0f * PI / 180.0f;
/*
 * Leave enough slack around the requested attitude to avoid alternating
 * corrections as the flight model and phone sensor settle on opposite sides
 * of the target. The wider proportional range also makes the final approach
 * less abrupt without changing the maximum requested pitch or bank.
 */
static const float ATTITUDE_ERROR_DEAD_ZONE = 2.5f * PI / 180.0f;
static const float ATTITUDE_FULL_STICK_ERROR = 32.0f * PI / 180.0f;
static const float ATTITUDE_FULL_DAMPING_RATE = 4.0f * PI / 180.0f;
static const float LOOK_MAX_PITCH = 70.0f * PI / 180.0f;
static const float STICK_DEFLECTION = 56.0f;

static float clampFloat(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Return the shortest signed angular difference. Sensor-relative yaw and roll
 * wrap at +/-pi; subtracting them directly would produce a full-turn spike. */
static float angleDifference(float angle, float origin) {
    float difference = angle - origin;
    while (difference > PI) difference -= 2.0f * PI;
    while (difference < -PI) difference += 2.0f * PI;
    return difference;
}

/* Convert target-attitude error to proportional stick command. Once the
 * aircraft reaches the handset's angle the command returns to centre. */
static float attitudeErrorToStick(float value) {
    float magnitude = value < 0.0f ? -value : value;
    if (magnitude <= ATTITUDE_ERROR_DEAD_ZONE) return 0.0f;
    magnitude = (magnitude - ATTITUDE_ERROR_DEAD_ZONE) /
                (ATTITUDE_FULL_STICK_ERROR - ATTITUDE_ERROR_DEAD_ZONE);
    magnitude = clampFloat(magnitude, 0.0f, 1.0f);
    return value < 0.0f ? -magnitude : magnitude;
}

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

/* Diagnostic AR flights may deliberately command extreme attitudes while
 * measuring sensor axes. Keep that opt-in behavior out of normal gameplay. */
int android_ar_preventCrashes(void) {
    const char *value = SDL_getenv("F15_NO_CRASH");
    return value && value[0] != '0';
}

void android_ar_setGameAttitude(int pitchAngle, int rollAngle) {
    const float pitch = angleToRadians(pitchAngle);
    const float roll = angleToRadians(rollAngle);
    const int recenter =
        g_flightGameCenterPending.exchange(0, std::memory_order_acq_rel);
    g_gamePitch.store(pitch, std::memory_order_relaxed);
    g_gameRoll.store(roll, std::memory_order_relaxed);
    /*
     * Flight mode begins before the legacy flight model initializes its
     * attitude. Capture the aircraft centre from the first authoritative
     * flight-model update rather than from stale menu/previous-flight state.
     */
    if (recenter) {
        g_flightGamePitchCenter.store(pitch, std::memory_order_relaxed);
        g_flightGameRollCenter.store(roll, std::memory_order_relaxed);
        g_gamePitchRate = 0.0f;
        g_gameRollRate = 0.0f;
    } else {
        g_gamePitchRate = angleDifference(pitch, g_previousGamePitch);
        g_gameRollRate = angleDifference(roll, g_previousGameRoll);
    }
    g_previousGamePitch = pitch;
    g_previousGameRoll = roll;
}

void android_ar_adjustView(int *yawAngle, int *pitchAngle, int *rollAngle) {
    const float unitsPerRadian = 65536.0f / 6.2831853071795864769f;
    static float smoothPitch = 0.0f;
    static float smoothRoll = 0.0f;
    float targetPitch = g_swipePitch.load(std::memory_order_relaxed);
    float targetRoll = 0.0f;

    if (!android_ar_active() || !yawAngle || !pitchAngle || !rollAngle) return;

    /*
     * Turning the handset right must turn the virtual camera right. Screen
     * geometry consequently moves left, hence the subtraction from game yaw.
     */
    *yawAngle -= (int)(angleDifference(
                           g_deviceYawOffset.load(std::memory_order_relaxed),
                           g_flightYawCenter.load(std::memory_order_relaxed)) *
                       unitsPerRadian);

    if (g_lookMode.load(std::memory_order_acquire)) {
        targetPitch += angleDifference(
            g_devicePitchOffset.load(std::memory_order_relaxed),
            g_lookPitchOrigin.load(std::memory_order_relaxed));
        targetRoll = angleDifference(
            g_deviceRollOffset.load(std::memory_order_relaxed),
            g_lookRollOrigin.load(std::memory_order_relaxed));
    }
    targetPitch = clampFloat(targetPitch, -LOOK_MAX_PITCH, LOOK_MAX_PITCH);

    /* Smooth both entering and leaving look mode so the cockpit view does not snap. */
    smoothPitch += (targetPitch - smoothPitch) * 0.22f;
    smoothRoll += (targetRoll - smoothRoll) * 0.22f;
    *pitchAngle += (int)(smoothPitch * unitsPerRadian);
    *rollAngle += (int)(smoothRoll * unitsPerRadian);
}

void android_ar_getFlightAxes(uint8 *rollAxis, uint8 *pitchAxis) {
    float deviceRoll;
    float devicePitch;
    float targetRoll;
    float targetPitch;
    float rollCommand;
    float pitchCommand;
    int rollValue;
    int pitchValue;
    if (!rollAxis || !pitchAxis || !android_ar_active() ||
        g_lookMode.load(std::memory_order_acquire)) {
        if (rollAxis) *rollAxis = 0x80;
        if (pitchAxis) *pitchAxis = 0x80;
        return;
    }

    deviceRoll = clampFloat(angleDifference(
        g_deviceRollOffset.load(std::memory_order_relaxed),
        g_flightRollCenter.load(std::memory_order_relaxed)),
        -FLIGHT_MAX_TILT, FLIGHT_MAX_TILT);
    devicePitch = clampFloat(angleDifference(
        g_devicePitchOffset.load(std::memory_order_relaxed),
        g_flightPitchCenter.load(std::memory_order_relaxed)),
        -FLIGHT_MAX_TILT, FLIGHT_MAX_TILT);

    targetRoll = g_flightGameRollCenter.load(std::memory_order_relaxed) +
                 deviceRoll;
    /* The screen top moving away is a negative aircraft pitch target. */
    targetPitch = g_flightGamePitchCenter.load(std::memory_order_relaxed) -
                  devicePitch;
    rollCommand = attitudeErrorToStick(angleDifference(
        targetRoll, g_gameRoll.load(std::memory_order_relaxed)));
    pitchCommand = attitudeErrorToStick(angleDifference(
        targetPitch, g_gamePitch.load(std::memory_order_relaxed)));

    /*
     * Brake the aircraft's angular motion before it crosses the requested
     * attitude. A proportional-only virtual stick alternated between steering
     * and centre because the legacy flight model retained roll/pitch momentum.
     */
    rollCommand -= clampFloat(
        g_gameRollRate / ATTITUDE_FULL_DAMPING_RATE, -0.65f, 0.65f);
    pitchCommand -= clampFloat(
        g_gamePitchRate / ATTITUDE_FULL_DAMPING_RATE, -0.65f, 0.65f);
    rollCommand = clampFloat(rollCommand, -1.0f, 1.0f);
    pitchCommand = clampFloat(pitchCommand, -1.0f, 1.0f);

    /* Smooth only the controller output; the target remains the exact handset
     * angle and is not integrated as a turn-rate command. */
    g_smoothFlightRoll +=
        (rollCommand - g_smoothFlightRoll) * 0.16f;
    g_smoothFlightPitch +=
        (pitchCommand - g_smoothFlightPitch) * 0.16f;
    rollValue = 0x80 + (int)(g_smoothFlightRoll * STICK_DEFLECTION);
    /* Positive attitude error uses the same raw-axis direction as arrow down. */
    pitchValue = 0x80 + (int)(g_smoothFlightPitch * STICK_DEFLECTION);
    *rollAxis = (uint8)clampFloat((float)rollValue, 0x26, 0xda);
    *pitchAxis = (uint8)clampFloat((float)pitchValue, 0x26, 0xda);
}

/* Make the handset's current pose neutral when a flight begins. Menu handling
 * can rotate the device substantially, so the process-start pose is not a
 * useful flight-control centre. */
void android_ar_recenterFlight(void) {
    g_flightYawCenter.store(
        g_deviceYawOffset.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    g_flightPitchCenter.store(
        g_devicePitchOffset.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    g_flightRollCenter.store(
        g_deviceRollOffset.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    /* The authoritative aircraft attitude is published after flight-model
     * initialization; defer that half of recentering until then. */
    g_flightGameCenterPending.store(1, std::memory_order_release);
    g_swipePitch.store(0.0f, std::memory_order_relaxed);
    g_smoothFlightRoll = 0.0f;
    g_smoothFlightPitch = 0.0f;
}

void android_ar_setLookMode(int active) {
    if (active) {
        g_lookPitchOrigin.store(
            g_devicePitchOffset.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        g_lookRollOrigin.store(
            g_deviceRollOffset.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        g_smoothFlightRoll = 0.0f;
        g_smoothFlightPitch = 0.0f;
        g_lookMode.store(1, std::memory_order_release);
    } else {
        g_lookMode.store(0, std::memory_order_release);
    }
}

void android_ar_addSwipePitch(float normalizedDelta) {
    float value = g_swipePitch.load(std::memory_order_relaxed);
    /* Dragging upward looks upward; one screen height spans roughly 120 degrees. */
    value -= normalizedDelta * (2.0f * PI / 3.0f);
    g_swipePitch.store(
        clampFloat(value, -LOOK_MAX_PITCH, LOOK_MAX_PITCH),
        std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_org_f15se2_ex_ArCameraView_nativeSetCameraReady(JNIEnv *, jclass, jboolean ready) {
    g_cameraReady.store(ready == JNI_TRUE, std::memory_order_release);
}

extern "C" JNIEXPORT void JNICALL
Java_org_f15se2_ex_ArCameraView_nativeSetDeviceAttitude(
    JNIEnv *, jclass, jfloat yaw, jfloat pitch, jfloat roll) {
    g_deviceYawOffset.store(yaw, std::memory_order_relaxed);
    g_devicePitchOffset.store(pitch, std::memory_order_relaxed);
    g_deviceRollOffset.store(roll, std::memory_order_relaxed);
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
