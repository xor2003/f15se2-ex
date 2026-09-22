#pragma once

#include "inttype.h"

/* Unit scales for the sams[] and aircraftTypes[] spec tables — the packed
 * fields store game values in compact table units (struct.h documents each
 * raw formula); these helpers name the conversions at call sites. Every
 * expression is arithmetically identical to the original code. */
namespace f15 {

/* Sam.maxSpeed stores projectile cruise speed << 6 (sub-unit fraction bits). */
inline int16 specProjSpeed(int16 maxSpeed) { return (int16)(maxSpeed >> 6); }

/* Sam.lockRange converts to the map units the speed word measures (x8) —
 * used in range*scaling/speed time-to-target estimates for projectile ttl. */
inline int32 specLockRangeUnits(int16 lockRange) { return (int32)lockRange << 3; }

/* Sam.turnRate clamps heading corrections to +/- turnRate * 0x80. */
inline int32 specYawClamp(int16 turnRate) { return (int32)turnRate * 0x80; }

/* Pitch corrections are asymmetric: missiles dive up to turnRate << 11 but
 * climb only turnRate << 9. */
inline int32 specPitchDiveLimit(int16 turnRate) { return (int32)turnRate << 0xb; }
inline int32 specPitchClimbLimit(int16 turnRate) { return (int32)turnRate << 9; }

/* aircraftTypes[].maneuverability clamps the AI's roll command word
 * (x0x1000) and the per-tick rollCmd-vs-bank lead — how far the bank word
 * may chase the command in one step (x256). */
inline int32 specRollCmdClamp(int16 maneuverability) { return (int32)maneuverability * 0x1000; }
inline int32 specBankStepClamp(int16 maneuverability) { return (int32)maneuverability * 256; }

} // namespace f15
