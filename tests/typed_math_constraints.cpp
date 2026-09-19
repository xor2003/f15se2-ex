#include "math/rotation.hpp"
#include "math/interpolation.hpp"
#include "math/flight_control.hpp"
#include "math/altitude.hpp"
#include "math/horizontal.hpp"
#include "math/airspeed.hpp"
#include "math/aerodynamics.hpp"
#include "math/propulsion.hpp"
#include <type_traits>
#include <utility>

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
#if defined(TEST_UNGUARDED_ALTITUDE_BOUNDARY)
#include "math/altitude_boundary.hpp"
#endif
#if defined(TEST_UNGUARDED_HORIZONTAL_BOUNDARY)
#include "math/horizontal_boundary.hpp"
#endif
#if defined(TEST_UNGUARDED_AIRSPEED_BOUNDARY)
#include "math/airspeed_boundary.hpp"
#endif
#if defined(TEST_UNGUARDED_PROPULSION_BOUNDARY)
#include "math/propulsion_boundary.hpp"
#endif

int main() {
    RotationMath<ModernBackend> math;
    Angle<ModernBackend> angle;
    auto matrix = cameraRotation(math, EulerAngles<ModernBackend>{angle, angle, angle});
    (void)(matrix * matrix);
    RollCommand<ModernBackend> roll;
    roll += RollCommand<ModernBackend>{};
    (void)roll.isZero();
    (void)AirspeedMath<ModernBackend>::verticalSample(FlightSpeed<ModernBackend>{});
    (void)AerodynamicsMath<ModernBackend>::pitchTrim(angle, math.cosine(angle));
    (void)AerodynamicsMath<ModernBackend>::belowStall(FlightSpeed<ModernBackend>{}, StallSpeed<ModernBackend>{});
    (void)PropulsionMath<ModernBackend>::limitForDamage(EngineThrust<ModernBackend>{}, 0);
    (void)PropulsionMath<ModernBackend>::targetSpeed({}, math.sine(angle), {}, {}, {}, LandingGear::Retracted);
    (void)AerodynamicsMath<ModernBackend>::stallThreshold(AerodynamicsMath<ModernBackend>::cornerSpeed({}, {}));
    const std::uint8_t bankTable[128]{};
    (void)AerodynamicsMath<ModernBackend>::loadResponse(
        AerodynamicsMath<ModernBackend>::bankLoad(angle, bankTable), {}, true);
    (void)AerodynamicsMath<FixedBackend>::bankLoad({}, bankTable);
    static_assert(std::is_same_v<decltype(PropulsionMath<ModernBackend>::advance(
        {}, {}, std::declval<SimulationStep<ModernBackend>>())), EngineThrust<ModernBackend>>);
    static_assert(std::is_same_v<decltype(AerodynamicsMath<ModernBackend>::stallResponse(
        {}, {}, StallSeverity::Normal, std::declval<SimulationStep<ModernBackend>>())), StallResponse<ModernBackend>>);
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
#elif defined(TEST_ALTITUDE_PRIMITIVE)
    FlightAltitude<FixedBackend> a = 100;
#elif defined(TEST_ALTITUDE_EXTRACTION)
    double raw = FlightAltitude<ModernBackend>{};
#elif defined(TEST_ALTITUDE_RENDER_MIX)
    FlightAltitude<ModernBackend> a = RenderHeight<ModernBackend>{};
#elif defined(TEST_ALTITUDE_RATE_MIX)
    FlightAltitude<FixedBackend> a = ClimbRate<FixedBackend>{};
#elif defined(TEST_ALTITUDE_BACKEND_MIX)
    FlightAltitude<FixedBackend> a = FlightAltitude<ModernBackend>{};
#elif defined(TEST_ALTITUDE_PRIMITIVE_STEP)
    (void)AltitudeMath<ModernBackend>::integrate({}, {}, 1.0);
#elif defined(TEST_ALTITUDE_POINTER)
    ClimbRate<FixedBackend> rate;
    short *raw = &rate;
#elif defined(TEST_COORDINATE_PRIMITIVE)
    ViewCoordinate<FixedBackend, ViewXAxis> point = 5;
#elif defined(TEST_COORDINATE_EXTRACTION)
    double raw = ViewCoordinate<ModernBackend, ViewYAxis>{};
#elif defined(TEST_COORDINATE_AXIS)
    ViewCoordinate<FixedBackend, ViewXAxis> point = ViewCoordinate<FixedBackend, ViewYAxis>{};
#elif defined(TEST_COORDINATE_BACKEND)
    ViewCoordinate<FixedBackend, ViewXAxis> point = ViewCoordinate<ModernBackend, ViewXAxis>{};
#elif defined(TEST_COORDINATE_ADD)
    (void)(ViewCoordinate<FixedBackend, ViewXAxis>{} + ViewCoordinate<FixedBackend, ViewXAxis>{});
#elif defined(TEST_DISPLACEMENT_AXIS)
    (void)(ViewCoordinate<FixedBackend, ViewXAxis>{} + ViewDisplacement<FixedBackend, ViewYAxis>{});
#elif defined(TEST_DISPLACEMENT_PRIMITIVE)
    ViewDisplacement<ModernBackend, ViewXAxis> delta(1.0);
#elif defined(TEST_HORIZONTAL_SPEED)
    HorizontalSpeed<ModernBackend> speed = 1.0;
#elif defined(TEST_THRUST_PRIMITIVE)
    EngineThrust<FixedBackend> thrust = 100;
#elif defined(TEST_FUEL_PRIMITIVE)
    FuelLoad<FixedBackend> fuel = 5000;
#elif defined(TEST_CORNER_PRIMITIVE)
    CornerSpeed<FixedBackend> corner = 100;
#elif defined(TEST_CORNER_EXTRACTION)
    double corner = CornerSpeed<ModernBackend>{};
#elif defined(TEST_CORNER_ALTITUDE)
    (void)AerodynamicsMath<FixedBackend>::cornerSpeed(RenderHeight<FixedBackend>{}, {});
#elif defined(TEST_CORNER_LOAD)
    (void)AerodynamicsMath<ModernBackend>::cornerSpeed({}, FuelLoad<ModernBackend>{});
#elif defined(TEST_CORNER_BACKEND)
    (void)AerodynamicsMath<ModernBackend>::cornerSpeed(FlightAltitude<FixedBackend>{}, {});
#elif defined(TEST_CORNER_STALL)
    (void)AerodynamicsMath<FixedBackend>::stallThreshold(FlightSpeed<FixedBackend>{});
#elif defined(TEST_CORNER_POINTER)
    CornerSpeed<FixedBackend> corner;
    short *raw = &corner;
#elif defined(TEST_LOAD_PRIMITIVE)
    FlightLoad<ModernBackend> load = 16.0;
#elif defined(TEST_FUEL_EXTRACTION)
    double fuel = FuelLoad<ModernBackend>{};
#elif defined(TEST_LOAD_STORAGE)
    (void)FlightLoad<FixedBackend>{}.value_;
#elif defined(TEST_TARGET_SPEED_ROLE)
    (void)PropulsionMath<FixedBackend>::targetSpeed({}, {}, FlightAltitude<FixedBackend>{}, {}, {}, LandingGear::Retracted);
#elif defined(TEST_TARGET_SPEED_BACKEND)
    (void)PropulsionMath<ModernBackend>::targetSpeed(EngineThrust<FixedBackend>{}, {}, {}, {}, {}, LandingGear::Retracted);
#elif defined(TEST_TARGET_SPEED_FUEL)
    (void)PropulsionMath<FixedBackend>::targetSpeed({}, {}, {}, FlightLoad<FixedBackend>{}, {}, LandingGear::Retracted);
#elif defined(TEST_TARGET_SPEED_GEAR)
    (void)PropulsionMath<ModernBackend>::targetSpeed({}, {}, {}, {}, {}, true);
#elif defined(TEST_THRUST_EXTRACTION)
    double thrust = EngineThrust<ModernBackend>{};
#elif defined(TEST_THRUST_BACKEND)
    using Invalid = decltype(PropulsionMath<ModernBackend>::advance(
        EngineThrust<FixedBackend>{}, {}, std::declval<SimulationStep<ModernBackend>>()));
#elif defined(TEST_THRUST_STEP)
    (void)PropulsionMath<ModernBackend>::advance({}, {}, 0.1);
#elif defined(TEST_THRUST_ROLE)
    EngineThrust<FixedBackend> thrust = FlightSpeed<FixedBackend>{};
#elif defined(TEST_THRUST_POINTER)
    EngineThrust<FixedBackend> thrust;
    short *raw = &thrust;
#elif defined(TEST_THRUST_STORAGE)
    (void)EngineThrust<FixedBackend>{}.value_;
#elif defined(TEST_STALL_PRIMITIVE)
    StallSpeed<FixedBackend> stall = 2700;
#elif defined(TEST_STALL_EXTRACTION)
    double stall = StallSpeed<ModernBackend>{};
#elif defined(TEST_STALL_ROLE)
    (void)AerodynamicsMath<FixedBackend>::belowStall(FlightSpeed<FixedBackend>{}, FlightSpeed<FixedBackend>{});
#elif defined(TEST_STALL_BACKEND)
    (void)AerodynamicsMath<FixedBackend>::belowStall(FlightSpeed<FixedBackend>{}, StallSpeed<ModernBackend>{});
#elif defined(TEST_STALL_SEVERITY)
    using InvalidSeverity = decltype(AerodynamicsMath<ModernBackend>::stallResponse(
        {}, {}, 1, std::declval<SimulationStep<ModernBackend>>()));
#elif defined(TEST_STALL_STEP)
    (void)AerodynamicsMath<ModernBackend>::stallResponse({}, {}, StallSeverity::Normal, 0.1);
#elif defined(TEST_STALL_POINTER)
    StallSpeed<FixedBackend> stall;
    short *raw = &stall;
#elif defined(TEST_LIFT_PRIMITIVE)
    (void)AerodynamicsMath<ModernBackend>::liftCorrection(1.0, 2.0);
#elif defined(TEST_TRIM_PRIMITIVE)
    (void)AerodynamicsMath<FixedBackend>::pitchTrim(1, 1);
#elif defined(TEST_TRIM_BACKEND)
    (void)AerodynamicsMath<FixedBackend>::pitchTrim(angle, math.cosine(angle));
#elif defined(TEST_TRIM_COEFFICIENT)
    (void)AerodynamicsMath<ModernBackend>::pitchTrim(angle, angle);
#elif defined(TEST_TRIM_FRACTION)
    (void)PoseInterpolation<ModernBackend>::linearOffset(angle, angle, 0.5);
#elif defined(TEST_AIRSPEED_PRIMITIVE)
    FlightSpeed<FixedBackend> speed = 10;
#elif defined(TEST_AIRSPEED_EXTRACTION)
    double speed = FlightSpeed<ModernBackend>{};
#elif defined(TEST_AIRSPEED_BACKEND)
    (void)AirspeedMath<ModernBackend>::verticalSample(FlightSpeed<FixedBackend>{});
#elif defined(TEST_AIRSPEED_RATE)
    FlightSpeed<ModernBackend> speed = Deceleration<ModernBackend>{};
#elif defined(TEST_AIRSPEED_STEP)
    (void)AirspeedMath<ModernBackend>::accelerate({}, {}, 1.0);
#elif defined(TEST_AIRSPEED_POINTER)
    FlightSpeed<FixedBackend> speed;
    int *raw = &speed;
#elif defined(TEST_AIRSPEED_SAMPLE)
    FlightSpeed<FixedBackend> speed = AirspeedSample<FixedBackend>{};
#elif defined(TEST_LOAD_RESPONSE_RAW)
    (void)AerodynamicsMath<FixedBackend>::loadResponse(16, 2, true);
#elif defined(TEST_TURN_RAW)
    (void)AerodynamicsMath<FixedBackend>::turnRate(16, 8100, 0, 1);
#elif defined(TEST_TURN_BACKEND)
    (void)AerodynamicsMath<FixedBackend>::turnRate({}, FlightSpeed<ModernBackend>{}, {}, {});
#elif defined(TEST_TURN_ROLE)
    (void)AerodynamicsMath<FixedBackend>::turnRate(FuelLoad<FixedBackend>{}, {}, {}, {});
#elif defined(TEST_TURN_AXIS)
    PitchCommand<FixedBackend> pitch = AerodynamicsMath<FixedBackend>::turnRate({}, {}, {}, {});
#elif defined(TEST_BANK_RAW)
    const std::uint8_t table[128]{};
    (void)AerodynamicsMath<FixedBackend>::bankLoad(42, table);
#elif defined(TEST_BANK_BACKEND)
    const std::uint8_t table[128]{};
    (void)AerodynamicsMath<FixedBackend>::bankLoad(Angle<ModernBackend>{}, table);
#elif defined(TEST_BANK_TABLE_SIZE)
    const std::uint8_t table[127]{};
    (void)AerodynamicsMath<FixedBackend>::bankLoad({}, table);
#elif defined(TEST_LOAD_EXTRACTION)
    int raw = FlightLoad<FixedBackend>{};
#elif defined(TEST_LOAD_ASSIGN)
    FlightLoad<FixedBackend> load;
    load = 16;
#elif defined(TEST_LOAD_RESPONSE_AXIS)
    (void)AerodynamicsMath<FixedBackend>::loadResponse({}, RollCommand<FixedBackend>{}, true);
#elif defined(TEST_LOAD_RESPONSE_BACKEND)
    (void)AerodynamicsMath<ModernBackend>::loadResponse(FlightLoad<FixedBackend>{}, {}, true);
#elif defined(TEST_LOAD_RESPONSE_ROLE)
    (void)AerodynamicsMath<FixedBackend>::loadResponse(FuelLoad<FixedBackend>{}, {}, true);
#elif defined(TEST_COORDINATE_POINTER)
    ViewCoordinate<FixedBackend, ViewXAxis> point;
    int *raw = &point;
#endif
    return 0;
}
