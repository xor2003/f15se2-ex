#include "math/rotation.hpp"
#include "math/interpolation.hpp"
#include "math/flight_control.hpp"
#include <type_traits>

using namespace f15::math;
static_assert(!std::is_constructible_v<Angle<FixedBackend>, int>);
static_assert(!std::is_constructible_v<Angle<ModernBackend>, double>);
static_assert(!std::is_convertible_v<Angle<ModernBackend>, double>);
static_assert(!std::is_constructible_v<Coefficient<FixedBackend>, int>);
static_assert(!std::is_constructible_v<Matrix3<ModernBackend>, std::array<double, 9>>);
static_assert(!std::is_constructible_v<RollCommand<FixedBackend>, int>);
static_assert(!std::is_convertible_v<PitchCommand<ModernBackend>, double>);
static_assert(!std::is_constructible_v<SimulationStep<ModernBackend>, double>);

#if defined(TEST_UNGUARDED_BOUNDARY)
#include "math/boundary.hpp"
#endif
#if defined(TEST_UNGUARDED_CONTROL_BOUNDARY)
#include "math/control_boundary.hpp"
#endif

int main() {
    RotationMath<ModernBackend> math;
    Angle<ModernBackend> angle;
    auto matrix = cameraRotation(math, EulerAngles<ModernBackend>{angle, angle, angle});
    (void)(matrix * matrix);
    RollCommand<ModernBackend> roll;
    roll += RollCommand<ModernBackend>{};
    (void)roll.isZero();
#if defined(TEST_PRIMITIVE_ANGLE)
    (void)math.sine(1.0);
#elif defined(TEST_PRIMITIVE_EULER)
    (void)math.rotation({1.0, 2.0, 3.0});
#elif defined(TEST_RAW_EXTRACTION)
    double value = angle;
#elif defined(TEST_BACKEND_MIXING)
    (void)math.sine(Angle<FixedBackend>{});
#elif defined(TEST_MATRIX_MIXING)
    (void)(matrix * Matrix3<FixedBackend>{});
#elif defined(TEST_QUANTITY_MIXING)
    (void)(angle + math.sine(angle));
#elif defined(TEST_PRIVATE_STORAGE)
    angle.value_ = 0;
#elif defined(TEST_MATRIX_INDEX)
    matrix[0] = 1.0;
#elif defined(TEST_MATRIX_POINTER)
    const double *raw = matrix;
#elif defined(TEST_PRIMITIVE_ASSIGN)
    angle = 1;
#elif defined(TEST_PRIMITIVE_UPDATE)
    angle += 1;
#elif defined(TEST_FRAME_FRACTION)
    (void)PoseInterpolation<ModernBackend>::interpolate({}, {}, 0.5);
#elif defined(TEST_ANGLE_POINTER)
    double *raw = &angle;
#elif defined(TEST_CONTROL_AXIS)
    RollCommand<FixedBackend> mixed = PitchCommand<FixedBackend>{};
#elif defined(TEST_CONTROL_BACKEND)
    RollCommand<FixedBackend> mixed = RollCommand<ModernBackend>{};
#elif defined(TEST_CONTROL_PRIMITIVE)
    PitchCommand<ModernBackend> pitch(1.0);
#elif defined(TEST_CONTROL_EXTRACTION)
    int raw = RollCommand<FixedBackend>{};
#elif defined(TEST_CONTROL_ADD)
    (void)(RollCommand<FixedBackend>{} + PitchCommand<FixedBackend>{});
#elif defined(TEST_CONTROL_ANGLE)
    (void)(angle + YawRate<ModernBackend>{});
#elif defined(TEST_CONTROL_STEP)
    (void)FlightControlMath<ModernBackend>::increments({}, {}, {}, 0.25);
#elif defined(TEST_CONTROL_POINTER)
    PitchCommand<FixedBackend> pitch;
    short *raw = &pitch;
#endif
    return 0;
}
