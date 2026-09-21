#ifndef F15_SE2_EGFLIGHT
#define F15_SE2_EGFLIGHT
/* public interface of egflight.c */
#include "math/flight_control.hpp"
#include "math/airspeed.hpp"
#include "math/altitude.hpp"
#include "math/map_position.hpp"

/* Current compressed scene height: full-range from flight altitude under the
 * modern backend, the stored scene word under the fixed backend. */
f15::math::RenderHeight<f15::math::GameBackend> flightSceneHeight();
/* Current indicated airspeed in knots: fractional under the modern backend,
 * the stored display word under the fixed backend. */
f15::math::CornerSpeed<f15::math::GameBackend> flightKnots();
/* Current corner speed in indicated knots, captured where g_cornerSpeed is
 * written each tick. */
f15::math::CornerSpeed<f15::math::GameBackend> flightCornerSpeed();
/* Current coarse map position: fractional map units derived from the fine
 * view coordinates under the modern backend, the stored g_viewX_/g_viewY_
 * words under the fixed backend. */
f15::math::MapPosition<f15::math::GameBackend> flightMapPosition();
/* Stick input -> flight commands: full-resolution analog when a physical
 * stick is preferred under the modern backend, the byte curve otherwise. */
f15::math::FlightCommands<f15::math::GameBackend> flightInputCommands(bool preferAnalogStick);
void advanceFlightOrientation(const f15::math::RotationDeltas<f15::math::GameBackend> &deltas);
void advanceFlightAltitude();
void updateFlightLift();
bool correctFlightStall();
bool flightStallWarningRequired();
void accelerateFlightSpeed(f15::math::FlightSpeed<f15::math::GameBackend> target);
void brakeFlightSpeed();
void advanceFlightHorizontal(f15::math::HorizontalSpeed<f15::math::GameBackend> speed);

int16 isqrt(int16 value);
void computeTrackingCameraAngles(int32 targetX, int32 targetY, int16 targetAlt,
                                 int32 viewX, int32 viewY, int16 viewAlt,
                                 int16 *heading, int16 *pitch);
void UpdateThrottleState(void);
void drawFuelGauge(void);

#endif /* F15_SE2_EGFLIGHT */
