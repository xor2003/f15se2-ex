#include "math/rotation.hpp"
#include <type_traits>

using namespace f15::math;
static_assert(!std::is_constructible_v<Angle<FixedBackend>, int>);
static_assert(!std::is_constructible_v<Angle<ModernBackend>, double>);
static_assert(!std::is_convertible_v<Angle<ModernBackend>, double>);
static_assert(!std::is_constructible_v<Coefficient<FixedBackend>, int>);
static_assert(!std::is_constructible_v<Matrix3<ModernBackend>, std::array<double, 9>>);

#if defined(TEST_UNGUARDED_BOUNDARY)
#include "math/boundary.hpp"
#endif

int main() {
    RotationMath<ModernBackend> math;
    Angle<ModernBackend> angle;
    auto matrix = cameraRotation(math, EulerAngles<ModernBackend>{angle, angle, angle});
    (void)(matrix * matrix);
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
#endif
    return 0;
}
