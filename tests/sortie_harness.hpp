/*
 * Shared scripted-sortie harness.
 *
 * Drives the real flight loop — input_pumpEvents -> stepFlightModel ->
 * updateFrame — through a deterministic scripted sortie. Used by:
 *   sortie_parity_tests   fixed backend, per-tick hash vs the e28b9a4 golden
 *   modern_sortie_tests   modern backend, per-field tolerance compare vs the
 *                         fixed field golden (tests/goldens/sortie_fields.trace)
 *
 * Determinism: no wall-clock input reaches the sim. The timer IRQ is never
 * installed (timerPump no-ops), gameRand() is seeded, discrete commands go
 * through SDL key events into the BIOS ring, and stick deflection arrives as
 * a virtual joystick axis — primaryAxes -> updateAxes -> joyAxes -> the byte
 * curve in stepFlightModel (the same path real hardware takes).
 */
#ifndef F15_TEST_SORTIE_HARNESS_HPP
#define F15_TEST_SORTIE_HARNESS_HPP

#include "headless.h"

#include "math/legacy_airspeed.hpp"
#include "math/aerodynamics.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
#include "egdata.h"
#include "egflight.h"
#include "egkeys.h"
#include "comm.h"
#include "endtypes.h"
#include "input.h"
#include "strand.h"
#include "gfx.h"
#include "egcode.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

void stepFlightModel();
void updateFrame();
void renderFrame();
void r3d_init();
void r3d_shutdown();
void rebuildOrientation();

/* ---- START-side mission state (stdata.c). A real mission load fills these;
 * kCombat seeds them directly and runs the real initMissionStrings() ->
 * worldImportToEgame() import instead of hand-writing the EGAME tables.
 * Declared at file scope so they bind to the C-compiled globals. ---- */
extern struct WorldObject worldObjects[];
extern struct FlightUnit flightUnits[];
extern struct Target targets[];
extern char terrainGrid[];
extern uint8 wldReadBuf1[];
extern uint8 wldReadBuf7[];
extern uint8 wldReadBuf8[];
extern char wldReadBuf11[];
extern int readItemSize;
extern int16 groundUnitCount;
extern uint16 worldObjectCount;
extern int flightUnitCount;
extern int16 missionDistAccum;
extern int16 escortMissionFlag;
extern int16 missionMidX, missionMidY;
extern unsigned int missionTargetX, missionTargetY;
extern int16 missionTarget2X, missionTarget2Y, missionBase2X, missionBase2Y;
void initMissionStrings();
void worldExportToEnd(void);
/* END-side debrief globals (endata.c) written by worldExportToEnd. */
extern int16 worldWaypointCount;
extern uint8 worldRouteTable[];
extern int16 worldRouteCount;
extern uint16 worldSamCount;
extern uint8 worldSamTable[];
extern char unitTypeTable[];
extern uint8 worldUnitFlags[];
extern char worldStringBuf[];
extern uint8 gridFlags[];
extern int16 worldGridSize;
extern uint8 worldMiscHeader[];
extern struct WeaponDataBlock weaponDataBlock;
extern TargetBlock targetBlock;
extern uint8 flightDataBuf[0x600];

namespace sortie {
using namespace f15::math;

constexpr int kSortieTicks = 660;
/* The combat profile runs longer: the AGM-65 fired at ~t480 needs ~110 more
 * ticks to close on the primary target — the intercept outcome is part of
 * the coverage, not just the launch. */
constexpr int kCombatTicks = 900;
/* The recovery approach flies a downwind-to-final pattern that takes ~3000
 * ticks from midfield — so the fixture starts the player on final, 0x400
 * map words up the extended centerline heading inbound. The corridor ->
 * auto-land -> Safe Landing chain still runs end to end; ~900 ticks reach
 * landingType 3 on fixed, 1600 leaves modern drift margin. */
constexpr int kLandTicks = 1600;
/* The boundary profile has two phases: ticks 0-500 start at mapX 0x7c00
 * heading east into the 0x7e00 bound (~250 ticks to contact), then a
 * re-seed at tick 500 starts at mapY 0x7b00 heading +mapY into the
 * mirrored 0x7d00 bound (~150 to contact). The X snap and the mirrored
 * Y snap are the two distinct confine-block code paths — the min ends
 * share the same clamp bodies, so one bound per axis covers them. */
constexpr int kWrapTicks = 1000;
constexpr std::uint32_t kSeed = 0x51e7u;



inline void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "sortie: %s\n", message); std::exit(1); }
}

inline void pushKey(SDL_Scancode scancode, SDL_Keycode key) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = scancode;
    ev.key.key = key;
    SDL_PushEvent(&ev);
}

/* Scenario profiles. kSortie is the original all-phase profile; kLoop is a
 * pure-aerobatics profile that forces a full vertical loop — the pole fold
 * regression path — with no weapons or autopilot phases; kCombat loads a
 * synthetic mission through the real worldxfer import (enemy interceptors,
 * target sites, name pool, waypoints) and flies it under autopilot while the
 * weapon designate/fire keys run; kLand reuses that imported mission but
 * seeds the mission-complete flags and waypointIndex=3 so the recovery
 * guidance (recoveryApproach/recoveryBank/recoveryAttitude/recoveryThrust)
 * flies the whole corridor approach into "Safe Landing"; kWrap flies the
 * player into the east theater bound so the per-tick position clamp
 * (egframe.c confine block) engages for hundreds of ticks; kStall idles
 * the throttle and pulls up until belowStall, then lets
 * correctFlightStall's nose-drop and the dive recover it. */
enum class Profile { kSortie, kLoop, kCombat, kStick, kLand, kWrap, kStall };

constexpr int ticksForProfile(Profile profile) {
    return profile == Profile::kLand ? kLandTicks
         : profile == Profile::kWrap ? kWrapTicks
         : profile == Profile::kCombat ? kCombatTicks : kSortieTicks;
}

/* Discrete cockpit commands for this tick, pushed before the pump so the
 * translated BIOS word reaches the key ring stepFlightModel drains. */
inline void pushScheduleKeys(int tick, Profile profile) {
    if (tick < 40) pushKey(SDL_SCANCODE_EQUALS, SDLK_EQUALS);      // throttle ramp
    /* G is the Maverick slot key, not gear (L is) — this press selected a
     * ground weapon; the label was wrong but the pinned goldens recorded it. */
    if (tick == 45) pushKey(SDL_SCANCODE_G, SDLK_G);
    if (profile == Profile::kLoop) return;
    /* kWrap is hands-off after the shared ramp: a straight powered run at
     * the east theater bound. */
    if (profile == Profile::kWrap) return;
    if (profile == Profile::kStall) {
        /* Idle throttle through the stall, back up once the dive has
         * rebuilt airspeed — the pull-out needs the engine spooled. */
        if (tick >= 40 && tick < 80) pushKey(SDL_SCANCODE_MINUS, SDLK_MINUS);
        if (tick >= 300 && tick < 360) pushKey(SDL_SCANCODE_EQUALS, SDLK_EQUALS);
        return;
    }
    if (profile == Profile::kCombat) {
        if (tick == 60) pushKey(SDL_SCANCODE_L, SDLK_L);            // gear up (real key)
        /* Autopilot altitude-hold steers to waypoints[1] (the primary target,
         * where the seeded interceptors patrol); it survives key presses —
         * only g_autopilotEngaged clears. S/M/G select weapon slots, T
         * designates, RETURN fires, BACKSPACE guns. */
        if (tick == 90) pushKey(SDL_SCANCODE_P, SDLK_P);            // autopilot on
        if (tick == 140) pushKey(SDL_SCANCODE_R, SDLK_R);           // radar range
        if (tick == 160) pushKey(SDL_SCANCODE_A, SDLK_A);           // afterburner
        if (tick == 220) pushKey(SDL_SCANCODE_S, SDLK_S);           // AIM-9M slot
        if (tick == 230) pushKey(SDL_SCANCODE_T, SDLK_T);           // designate air
        if (tick == 240 || tick == 243) pushKey(SDL_SCANCODE_RETURN, SDLK_RETURN);
        if (tick == 300) pushKey(SDL_SCANCODE_M, SDLK_M);           // AIM-120 slot
        if (tick == 310) pushKey(SDL_SCANCODE_T, SDLK_T);
        if (tick == 320 || tick == 325) pushKey(SDL_SCANCODE_RETURN, SDLK_RETURN);
        if (tick >= 380 && tick <= 392 && (tick & 3) == 0)
            pushKey(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);        // gun burst
        if (tick == 430) pushKey(SDL_SCANCODE_W, SDLK_W);           // waypoint -> 2
        if (tick == 460) pushKey(SDL_SCANCODE_G, SDLK_G);           // AGM-65 slot
        if (tick == 470) pushKey(SDL_SCANCODE_T, SDLK_T);           // designate ground
        if (tick == 480 || tick == 485) pushKey(SDL_SCANCODE_RETURN, SDLK_RETURN);
        if (tick == 540) pushKey(SDL_SCANCODE_W, SDLK_W);           // waypoint -> 3
        if (tick == 600) pushKey(SDL_SCANCODE_B, SDLK_B);           // airbrake
        return;
    }
    if (profile == Profile::kLand) {
        /* Autopilot on, then hands-off: waypointIndex==3 (seeded) routes the
         * guidance through recoveryApproach — it steers to the recovery base,
         * manages descent/brakes/thrust, and lands itself. Gear stays down
         * the whole run (it starts down on the runway). */
        if (tick == 60) pushKey(SDL_SCANCODE_P, SDLK_P);
        /* Chop throttle on touchdown, like a player would: when "Safe
         * Landing" sets g_landingDoneFlag it also clears the altitude hold,
         * so the recovery block stops writing g_setThrust — and its last
         * command (35) leaves the engine pushing. Fixed's truncated speed
         * accumulation stays under the 1-knot timer gate long enough to
         * finalize, but modern's fractional creep re-accelerates off the
         * runway and out of the corridor. MINUS presses are overridden every
         * tick while the hold is live, then drive setThrust to 0 within four
         * presses once it clears. */
        if (tick >= 360 && tick <= 640 && (tick & 7) == 0)
            pushKey(SDL_SCANCODE_MINUS, SDLK_MINUS);
        return;
    }
    if (tick == 280) for (int i = 0; i < 16; ++i)                 // throttle cut
        pushKey(SDL_SCANCODE_MINUS, SDLK_MINUS);
    if (tick == 390) pushKey(SDL_SCANCODE_W, SDLK_W);             // waypoint cycle
    if (tick == 392 || tick == 394) pushKey(SDL_SCANCODE_RETURN, SDLK_RETURN);   // fire missile
    if (tick == 400) pushKey(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);          // guns
    if (tick == 540) {
        pushKey(SDL_SCANCODE_G, SDLK_G);                          // weapon slot 2
        pushKey(SDL_SCANCODE_B, SDLK_B);                          // airbrake
    }
    if (tick >= 560 && (tick & 7) == 0) pushKey(SDL_SCANCODE_MINUS, SDLK_MINUS);
    if (tick == 600) pushKey(SDL_SCANCODE_P, SDLK_P);             // autopilot on
}

/* Stick input arrives through a virtual joystick — the only injection seam
 * that survives the real input path. kbhit() inside stepFlightModel re-pumps
 * events and updateStick() re-derives g_joyRawX/Y from SDL_GetKeyboardState,
 * so a direct write to g_joyRaw or joyAxes is overwritten before the byte
 * curve runs; pushed key events never reach SDL_GetKeyboardState at all. A
 * virtual device is read by the same updateAxes() path as real hardware on
 * every pump, so its axis state persists by construction — and it keeps the
 * full 0..255 byte range the keyboard's three positions cannot express. */
inline SDL_JoystickID &vjoyId() { static SDL_JoystickID id = 0; return id; }

/* Inverse of joystick.c's axisByte(): byte = 0x80 + (raw*127)/32768 with a
 * |raw|<8000 deadzone around centre. Bytes inside the deadzone band
 * (0x62..0x9e) are unreachable and quantize to centre. */
inline Sint16 axisForByte(std::uint8_t byte) {
    if (byte >= 0x62 && byte <= 0x9e) return 0;
    const int off = (int)byte - 0x80;
    int raw = off > 0 ? (off * 32768 + 126) / 127 : -(((-off) * 32768 + 126) / 127);
    auto produced = [](int r) {
        int v = 0x80 + (r * 127) / 32768;
        return v < 0 ? 0 : v > 255 ? 255 : v;
    };
    while (produced(raw) < (int)byte) ++raw;
    while (produced(raw) > (int)byte) --raw;
    return (Sint16)raw;
}

inline void applyScheduleStick(int tick, Profile profile) {
    std::uint8_t roll = 0x80, pitch = 0x80;
    if (profile == Profile::kLoop) {
        /* Sustained pull (stick back = nose up): through the vertical and
         * over the top — the pole-fold path. */
        if (tick >= 200 && tick < 470) pitch = 0xda;
    } else if (profile == Profile::kStick) {
        /* Moderate stick deflections that keep the airframe airborne: full
         * throws spiral into terrain inside a few dozen ticks. The golden for
         * this profile was recorded on the pre-migration commit e28b9a4 with
         * a virtual stick — an oracle for real stick input, not HEAD state. */
        if (tick >= 60 && tick < 140) pitch = 0xa8;               // climb
        else if (tick >= 140 && tick < 220) roll = 0xa8;          // banked turn
        else if (tick >= 220 && tick < 300) roll = 0x58;          // counter-turn
        else if (tick >= 300 && tick < 360) pitch = 0x58;         // push over
        else if (tick >= 360 && tick < 430) pitch = 0xa8;         // pull out
        else if (tick >= 430 && tick < 540) roll = 0xa8;          // sustained turn
        else if (tick >= 540 && tick < 640) pitch = 0x58;         // descend
    } else if (profile == Profile::kStall) {
        /* Gentle climb-bleed into an ordinary low-speed stall (a hard
         * pull gives an accelerated stall — the G-load raises the corner
         * speed past the airspeed — whose dive modern cannot recover
         * inside 5000 altitude). Centred 280-300 isolates the
         * correctFlightStall nose-drop; the pull from 300 arrests the
         * dive once speed is back. */
        if (tick >= 60 && tick < 280) pitch = 0xa8;
        else if (tick >= 300 && tick < 500) pitch = 0xa0;
    }
    /* kSortie intentionally stays centred: its golden was recorded while
     * byte writes were being absorbed by updateStick — i.e. it pins a
     * centered-stick sortie. Real stick coverage lives in kLoop/kStick. */
    SDL_Joystick *stick = SDL_GetJoystickFromID(vjoyId());
    if (stick) {
        SDL_SetJoystickVirtualAxis(stick, 0, axisForByte(roll));
        SDL_SetJoystickVirtualAxis(stick, 1, axisForByte(pitch));
    }
}

/* Same FNV-1a-style byte mix as blackbox_diag (kept local: the diag hashers
 * are static internals, and an identical mix makes traces comparable). */
inline std::uint32_t hashAdd(std::uint32_t hash, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        hash ^= value & 0xffu;
        hash *= 16777619u;
        value >>= 8;
    }
    return hash;
}

struct FlightFields {
    std::uint32_t fineX, fineY;
    std::uint32_t viewZ, head, pitch, roll, speed, altitude;
    std::uint32_t knots, corner, stall, rollIn, pitchIn, lift, trim;
    std::uint32_t thrust, setThrust, fuel, gees, viewX, viewY;
    std::uint32_t apEngaged, apAlt, planeFlags, gearArmed, eject, gunHits, damage;
};

inline FlightFields readFlight() {
    FlightFields f{};
    f.fineX = (std::uint32_t)legacy::fineUnits(g_ViewX);
    f.fineY = (std::uint32_t)legacy::fineUnits(g_ViewY);
    f.viewZ = (std::uint16_t)g_viewZ;
    f.head = (std::uint16_t)legacy::signedAngle(g_ourHead);
    f.pitch = (std::uint16_t)legacy::signedAngle(g_ourPitch);
    f.roll = (std::uint16_t)legacy::signedAngle(g_ourRoll);
    f.speed = (std::uint32_t)legacy::speedUnits(g_velocity);
    f.altitude = legacy::altitudeUnits(g_altitude);
    f.knots = (std::uint16_t)legacy::knotsUnits(g_knots);
    f.corner = (std::uint16_t)legacy::knotsUnits(g_cornerSpeed);
    f.stall = (std::uint32_t)legacy::Airspeeds::stall(g_stallSpeed);
    f.rollIn = (std::uint32_t)legacy::rollInput(g_rollInput);
    f.pitchIn = (std::uint32_t)legacy::pitchInput(g_pitchInput);
    f.lift = (std::uint16_t)legacy::signedAngle(g_liftForce);
    f.trim = (std::uint16_t)legacy::signedAngle(g_rollPitchTrim);
    f.thrust = (std::uint16_t)legacy::thrustUnits(g_thrust);
    f.setThrust = (std::uint16_t)f15::math::legacy::thrustUnits(g_setThrust);
    f.fuel = (std::uint16_t)f15::math::legacy::fuelUnits(g_fuelRemaining);
    f.gees = (std::uint32_t)legacy::loadSixteenths(g_gees);
    f.viewX = (std::uint16_t)g_viewX_;
    f.viewY = (std::uint16_t)g_viewY_;
    f.apEngaged = (std::uint16_t)g_autopilotEngaged;
    f.apAlt = (std::uint32_t)legacy::Altitudes::render(g_autopilotAltitude);
    f.planeFlags = (std::uint16_t)g_playerPlaneFlags;
    f.gearArmed = (std::uint16_t)g_gearDownArmed;
    f.eject = (std::uint16_t)g_ejectState;
    f.gunHits = (std::uint16_t)g_gunHits;
    f.damage = (std::uint16_t)g_damageTakenFlag;
    return f;
}

inline void dumpFlight(const FlightFields &f) {
    std::printf("fineX=%u fineY=%u viewZ=%u head=%u pitch=%u roll=%u speed=%u alt=%u\n"
                "knots=%u corner=%u stall=%u rollIn=%u pitchIn=%u lift=%u trim=%u\n"
                "thrust=%u setThrust=%u fuel=%u gees=%u viewX=%u viewY=%u ap=%u apAlt=%u\n"
                "flags=%u gear=%u eject=%u gunHits=%u damage=%u\n",
                f.fineX, f.fineY, f.viewZ, f.head, f.pitch, f.roll, f.speed, f.altitude,
                f.knots, f.corner, f.stall, f.rollIn, f.pitchIn, f.lift, f.trim,
                f.thrust, f.setThrust, f.fuel, f.gees, f.viewX, f.viewY, f.apEngaged, f.apAlt,
                f.planeFlags, f.gearArmed, f.eject, f.gunHits, f.damage);
}

inline std::uint32_t hashFlight() {
    const FlightFields &f = readFlight();
    std::uint32_t h = 0x811c9dc5u;
    for (std::uint32_t v : {f.fineX, f.fineY, f.viewZ, f.head, f.pitch, f.roll, f.speed,
                            f.altitude, f.knots, f.corner, f.stall, f.rollIn, f.pitchIn,
                            f.lift, f.trim, f.thrust, f.setThrust, f.fuel, f.gees,
                            f.viewX, f.viewY, f.apEngaged, f.apAlt, f.planeFlags,
                            f.gearArmed, f.eject, f.gunHits, f.damage})
        h = hashAdd(h, v);
    return h;
}

inline std::uint32_t hashCamera() {
    std::uint32_t h = 0x811c9dc5u;
    h = hashAdd(h, (std::uint32_t)g_camEyeX);
    h = hashAdd(h, (std::uint32_t)g_camEyeY);
    h = hashAdd(h, (std::uint16_t)g_camEyeZ);
    h = hashAdd(h, (std::uint16_t)g_camEyeFracX);
    h = hashAdd(h, (std::uint16_t)g_camEyeFracY);
    h = hashAdd(h, (std::uint16_t)g_camEyeFracZ);
    h = hashAdd(h, (std::uint16_t)g_viewMode);
    return h;
}

inline std::uint32_t hashObjects() {
    std::uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 8; ++i) {
        const auto &p = g_planeTable.planes[i];
        h = hashAdd(h, p.mapX);
        h = hashAdd(h, p.mapY);
        h = hashAdd(h, (std::uint16_t)p.active);
        h = hashAdd(h, (std::uint16_t)p.flags);
        h = hashAdd(h, (std::uint16_t)p.alertLevel);
        h = hashAdd(h, (std::uint16_t)p.threatTimer);
    }
    for (int i = 0; i < 8; ++i) {
        const auto &o = g_simObjects[i];
        h = hashAdd(h, o.posX);
        h = hashAdd(h, o.posY);
        h = hashAdd(h, (std::uint32_t)o.worldX);
        h = hashAdd(h, (std::uint32_t)o.worldY);
        h = hashAdd(h, (std::uint16_t)o.alt);
        h = hashAdd(h, (std::uint16_t)o.speed);
        h = hashAdd(h, o.flags.w);
        h = hashAdd(h, (std::uint16_t)o.heading.w);
    }
    for (int i = 0; i < 8; ++i) {
        h = hashAdd(h, (std::uint32_t)legacy::fineWord(bulletTracks[i].posX));
        h = hashAdd(h, (std::uint32_t)legacy::fineWord(bulletTracks[i].posY));
        h = hashAdd(h, (std::uint32_t)(std::int32_t)bulletTracks[i].alt);
    }
    return h;
}

inline std::uint32_t hashWeapons() {
    std::uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 12; ++i) {
        const auto &w = g_projectiles[i];
        h = hashAdd(h, w.mapX);
        h = hashAdd(h, w.mapY);
        h = hashAdd(h, (std::uint16_t)w.alt);
        h = hashAdd(h, (std::uint16_t)w.ttl.word());
        h = hashAdd(h, (std::uint16_t)w.speed);
        h = hashAdd(h, (std::uint16_t)w.targetRef);
        h = hashAdd(h, (std::uint16_t)w.weaponIdx);
    }
    h = hashAdd(h, (std::uint16_t)g_bulletTrackCount);
    return h;
}

inline std::uint32_t hashMission() {
    std::uint32_t h = 0x811c9dc5u;
    h = hashAdd(h, (std::uint16_t)g_missionStatus);
    h = hashAdd(h, (std::uint16_t)g_landingTimer.word());
    h = hashAdd(h, (std::uint16_t)g_missionTick.word());
    h = hashAdd(h, (std::uint16_t)g_nearestThreatRange);
    h = hashAdd(h, (std::uint16_t)g_targetRange);
    h = hashAdd(h, (std::uint16_t)legacy::signedAngle(g_targetBearing));
    h = hashAdd(h, (std::uint16_t)g_northSouthSign);
    h = hashAdd(h, (std::uint16_t)g_missionEndedFlag[0]);
    h = hashAdd(h, (std::uint16_t)(commData ? commData->landingType : 0));
    h = hashAdd(h, (std::uint16_t)g_inLandingCorridor);
    h = hashAdd(h, (std::uint16_t)g_closestThreatIndex);
    h = hashAdd(h, (std::uint16_t)g_directorEventDeadline.word());
    h = hashAdd(h, (std::uint16_t)g_slowMotionMode);
    return h;
}

/* Every hashed quantity as a named value — the field golden for the modern
 * tolerance compare. Names are stable across backends; the compare table in
 * modern_sortie_tests maps name patterns to tolerance classes. */
using FieldList = std::vector<std::pair<std::string, std::uint32_t>>;

inline FieldList snapFields() {
    FieldList out;
    auto add = [&](const char *name, std::uint32_t v) { out.emplace_back(name, v); };
    const auto f = readFlight();
    add("f.fineX", f.fineX); add("f.fineY", f.fineY); add("f.viewZ", f.viewZ);
    add("f.head", f.head); add("f.pitch", f.pitch); add("f.roll", f.roll);
    add("f.speed", f.speed); add("f.alt", f.altitude); add("f.knots", f.knots);
    add("f.corner", f.corner); add("f.stall", f.stall);
    add("f.rollIn", f.rollIn); add("f.pitchIn", f.pitchIn);
    add("f.lift", f.lift); add("f.trim", f.trim);
    add("f.thrust", f.thrust); add("f.setThrust", f.setThrust); add("f.fuel", f.fuel);
    add("f.gees", f.gees); add("f.viewX", f.viewX); add("f.viewY", f.viewY);
    add("f.ap", f.apEngaged); add("f.apAlt", f.apAlt);
    add("f.flags", f.planeFlags); add("f.gear", f.gearArmed);
    add("f.eject", f.eject); add("f.gunHits", f.gunHits); add("f.damage", f.damage);
    add("c.eyeX", (std::uint32_t)g_camEyeX); add("c.eyeY", (std::uint32_t)g_camEyeY);
    add("c.eyeZ", (std::uint16_t)g_camEyeZ);
    add("c.eyeFracX", (std::uint16_t)g_camEyeFracX);
    add("c.eyeFracY", (std::uint16_t)g_camEyeFracY);
    add("c.eyeFracZ", (std::uint16_t)g_camEyeFracZ);
    add("c.viewMode", (std::uint16_t)g_viewMode);
    char name[32];
    for (int i = 0; i < 8; ++i) {
        const auto &p = g_planeTable.planes[i];
        std::snprintf(name, sizeof(name), "p%d.mapX", i); add(name, p.mapX);
        std::snprintf(name, sizeof(name), "p%d.mapY", i); add(name, p.mapY);
        std::snprintf(name, sizeof(name), "p%d.active", i); add(name, (std::uint16_t)p.active);
        std::snprintf(name, sizeof(name), "p%d.flags", i); add(name, (std::uint16_t)p.flags);
        std::snprintf(name, sizeof(name), "p%d.alert", i); add(name, (std::uint16_t)p.alertLevel);
        std::snprintf(name, sizeof(name), "p%d.threatT", i); add(name, (std::uint16_t)p.threatTimer);
    }
    for (int i = 0; i < 8; ++i) {
        const auto &o = g_simObjects[i];
        std::snprintf(name, sizeof(name), "o%d.posX", i); add(name, o.posX);
        std::snprintf(name, sizeof(name), "o%d.posY", i); add(name, o.posY);
        std::snprintf(name, sizeof(name), "o%d.worldX", i); add(name, (std::uint32_t)o.worldX);
        std::snprintf(name, sizeof(name), "o%d.worldY", i); add(name, (std::uint32_t)o.worldY);
        std::snprintf(name, sizeof(name), "o%d.alt", i); add(name, (std::uint16_t)o.alt);
        std::snprintf(name, sizeof(name), "o%d.speed", i); add(name, (std::uint16_t)o.speed);
        std::snprintf(name, sizeof(name), "o%d.flags", i); add(name, o.flags.w);
        std::snprintf(name, sizeof(name), "o%d.heading", i); add(name, (std::uint16_t)o.heading.w);
    }
    for (int i = 0; i < 8; ++i) {
        std::snprintf(name, sizeof(name), "b%d.posX", i);
        add(name, (std::uint32_t)legacy::fineWord(bulletTracks[i].posX));
        std::snprintf(name, sizeof(name), "b%d.posY", i);
        add(name, (std::uint32_t)legacy::fineWord(bulletTracks[i].posY));
        std::snprintf(name, sizeof(name), "b%d.alt", i);
        add(name, (std::uint32_t)(std::int32_t)bulletTracks[i].alt);
    }
    for (int i = 0; i < 12; ++i) {
        const auto &w = g_projectiles[i];
        std::snprintf(name, sizeof(name), "w%d.mapX", i); add(name, w.mapX);
        std::snprintf(name, sizeof(name), "w%d.mapY", i); add(name, w.mapY);
        std::snprintf(name, sizeof(name), "w%d.alt", i); add(name, (std::uint16_t)w.alt);
        std::snprintf(name, sizeof(name), "w%d.ttl", i); add(name, (std::uint16_t)w.ttl.word());
        std::snprintf(name, sizeof(name), "w%d.speed", i); add(name, (std::uint16_t)w.speed);
        std::snprintf(name, sizeof(name), "w%d.target", i); add(name, (std::uint16_t)w.targetRef);
        std::snprintf(name, sizeof(name), "w%d.weapon", i); add(name, (std::uint16_t)w.weaponIdx);
    }
    add("w.count", (std::uint16_t)g_bulletTrackCount);
    add("m.status", (std::uint16_t)g_missionStatus);
    add("m.landT", (std::uint16_t)g_landingTimer.word());
    add("m.mtick", (std::uint16_t)g_missionTick.word());
    add("m.nearR", (std::uint16_t)g_nearestThreatRange);
    add("m.tgtR", (std::uint16_t)g_targetRange);
    add("m.tgtB", (std::uint16_t)legacy::signedAngle(g_targetBearing));
    add("m.ns", (std::uint16_t)g_northSouthSign);
    add("m.ended", (std::uint16_t)g_missionEndedFlag[0]);
    add("m.landT2", (std::uint16_t)(commData ? commData->landingType : 0));
    add("m.corr", (std::uint16_t)g_inLandingCorridor);
    add("m.threat", (std::uint16_t)g_closestThreatIndex);
    add("m.dirDl", (std::uint16_t)g_directorEventDeadline.word());
    add("m.slow", (std::uint16_t)g_slowMotionMode);
    return out;
}

/* Non-degenerate proof for the loop profile: the stick reached the sim and
 * the aircraft entered the pole band. Guards the input-injection seam —
 * the earlier byte-write and held-key attempts each produced a level-flight
 * "loop" that never deflected. */
struct LoopCheck {
    bool pitchInputSeen = false;
    std::uint32_t maxPitchMag = 0;
    std::uint32_t altMin = ~0u, altMax = 0;
};
inline void loopObserve(LoopCheck &c, int tick) {
    const auto f = readFlight();
    const std::uint32_t p = f.pitch;
    const std::uint32_t mag = std::min(p, 65536u - p);
    if (mag > c.maxPitchMag) c.maxPitchMag = mag;
    if (tick >= 200 && tick < 470 && (std::int32_t)f.pitchIn != 0) c.pitchInputSeen = true;
    if (f.altitude < c.altMin) c.altMin = f.altitude;
    if (f.altitude > c.altMax) c.altMax = f.altitude;
}
inline void loopRequire(const LoopCheck &c) {
    require(c.pitchInputSeen, "loop: stick input never reached the sim");
    require(c.maxPitchMag >= 0x3000, "loop: never entered the pole band");
    require(c.altMax - c.altMin >= 4000, "loop: no climb");
}

/* The stick profile's deflections must actually reach the sim in both axes —
 * a schedule that lands inside the joystick deadzone (|raw|<8000, bytes
 * 0x62..0x9e) records level flight and would pin nothing. */
struct StickCheck {
    bool pitchSeen = false, rollSeen = false;
};
inline void stickObserve(StickCheck &c, int tick) {
    const auto f = readFlight();
    const bool pitchWindow = (tick >= 60 && tick < 140) || (tick >= 300 && tick < 430) ||
                             (tick >= 540 && tick < 640);
    const bool rollWindow = (tick >= 140 && tick < 300) || (tick >= 430 && tick < 540);
    if (pitchWindow && (std::int32_t)f.pitchIn != 0) c.pitchSeen = true;
    if (rollWindow && (std::int32_t)f.rollIn != 0) c.rollSeen = true;
}
inline void stickRequire(const StickCheck &c) {
    require(c.pitchSeen, "stick: pitch deflection never reached the sim");
    require(c.rollSeen, "stick: roll deflection never reached the sim");
}

inline void dumpMission() {
    std::printf("mstatus=%d landT=%d mtick=%d nearR=%d tgtR=%d tgtB=%d ns=%d ended=%d landT2=%d corr=%d threat=%d dirDl=%d slow=%d apEng=%d\n",
                (int)g_missionStatus, (int)g_landingTimer.word(), (int)g_missionTick.word(),
                (int)g_nearestThreatRange, (int)g_targetRange, (int)legacy::signedAngle(g_targetBearing),
                (int)g_northSouthSign, (int)g_missionEndedFlag[0],
                commData ? (int)commData->landingType : -1, (int)g_inLandingCorridor,
                (int)g_closestThreatIndex, (int)g_directorEventDeadline.word(),
                (int)g_slowMotionMode, (int)g_autopilotEngaged);
}

/* A small synthetic strike mission: home base south-center, primary target NE,
 * interceptors patrolling near it (objType steers them to a planeTable entry),
 * a striker on a bomb run, and a waypoint-flagged site the retarget scan skips.
 * Map words, inside the [0x100,0x7e00] theater clamp. */
inline void seedCombatMission() {
    memset(worldObjects, 0, sizeof(struct WorldObject) * 0x4B);
    memset(flightUnits, 0, sizeof(struct FlightUnit) * 0x13);
    memset(terrainGrid, 0, 256);
    memset(wldReadBuf7, 0, 0x64);
    memset(wldReadBuf8, 0, 0x64);
    memset(wldReadBuf11, 0, 0x2EE);

    readItemSize = 6;
    worldObjectCount = 6;
    const WorldObject kObjects[6] = {
        {1, 0x2800, 0x2800, 1, 0x100, 0, 0, 1}, /* home base (view anchor) */
        {2, 0x3400, 0x3400, 1, 0x000, 0, 0, 2}, /* primary target */
        {3, 0x1c00, 0x4000, 1, 0x200, 0, 0, 3},
        {4, 0x4400, 0x2000, 1, 0x100, 0, 0, 4}, /* recovery base */
        {5, 0x5000, 0x5000, 1, 0x400, 0, 0, 5}, /* waypoint-flagged */
        {6, 0x3000, 0x1c00, 1, 0x000, 0, 0, 6},
    };
    for (int i = 0; i < 6; ++i) worldObjects[i] = kObjects[i];

    flightUnitCount = 7;
    groundUnitCount = 7;
    const auto unit = [](int16 tgt, uint16 x, uint16 y, uint16 alt, int16 hdg,
                         int16 spec, int16 flags, int16 spd) {
        FlightUnit u{};
        u.waypointIdx = tgt;
        u.x = x; u.y = y; u.altitude = alt;
        u.xPrecise = (int32)x << 5;
        u.yPrecise = (int32)y << 5;
        u.heading = hdg; u.pitch = 0; u.roll = 0;
        u.planeType = spec; u.flags = flags;
        u.maxSpeed = spd; u.fuel = 4000;
        return u;
    };
    const int kFighter = SIMOBJ_ACTIVE | SIMOBJ_ALIVE | SIMOBJ_ENEMY_AIR | SIMOBJ_INTERCEPTOR;
    /* [0] sits on the outbound track (the autopilot leg runs toward
     * decreasing mapY) at the player's cruise altitude: the player passes
     * within ~100 map words of it, so updateTargetLock's auto-acquire — and
     * the A2A missile fired into that lock — has a real lock/pursuit/intercept
     * to exercise. The proximity kill wants (alt gap)>>5 + map range < ~22:
     * a low target just gets dove under, and an evading interceptor opens
     * too much pursuit lag — so it's a co-altitude patrol plane (ENEMY_AIR,
     * no INTERCEPTOR) holding a steady head-on line. */
    flightUnits[0] = unit(1, 0x2890, 0x2400, 3780, (int16)0x8000, 0,
                          SIMOBJ_ACTIVE | SIMOBJ_ALIVE | SIMOBJ_ENEMY_AIR, 280);
    /* [1] is overwritten by the wingman seed in mission init — leave a husk. */
    flightUnits[1] = unit(1, 0x2800, 0x2800, 0, 0, 0, 0, 0);
    flightUnits[2] = unit(3, 0x3a00, 0x2c00, 2800, (int16)0xc000, 2, kFighter, 420);
    flightUnits[3] = unit(1, 0x4800, 0x4800, 1500, (int16)0x4000, 15,
                          SIMOBJ_ACTIVE | SIMOBJ_ALIVE | SIMOBJ_ENEMY_AIR, 300);
    flightUnits[4] = unit(2, 0x1400, 0x1400, 3000, 0, 6, kFighter, 500);
    flightUnits[5] = unit(5, 0x3200, 0x3600, 300, (int16)0x2000, 16,
                          SIMOBJ_ACTIVE | SIMOBJ_ALIVE | SIMOBJ_TRACKED_SITE, 60);
    flightUnits[6] = unit(4, 0x2400, 0x4400, 2400, (int16)0xe000, 3, kFighter, 460);

    memset(targets, 0, sizeof(struct Target) * 2);
    targets[0].missionType = 2; targets[0].targetIdx = 1; targets[0].baseIdx = 0;
    targets[0].missionCode = 1; targets[0].missionNum = 0;
    strcpy(targets[0].coord, "B2");
    targets[0].distance = 120;
    targets[1].missionType = 2; targets[1].targetIdx = 2; targets[1].baseIdx = 3;
    targets[1].missionCode = 2; targets[1].missionNum = 1;
    strcpy(targets[1].coord, "C4");
    targets[1].distance = 90;

    missionMidX = 0x2c00; missionMidY = 0x2c00;
    missionTargetX = 0x3400; missionTargetY = 0x3400;
    missionTarget2X = 0x1c00; missionTarget2Y = 0x4000;
    missionBase2X = 0x2800; missionBase2Y = 0x2800;
    missionDistAccum = 200;
    escortMissionFlag = -1;

    wldReadBuf1[0] = 1; wldReadBuf1[1] = 2;
    wldReadBuf7[1] = 2; wldReadBuf7[2] = 2; wldReadBuf7[3] = 1;
    /* The pool is a sequence of NUL-separated names — memcpy (not strcpy) so
     * the embedded terminators survive. */
    static const char kNamePool[] =
        "HOME PLATE\0TARGET ALPHA\0DEPOT ROW\0CARRIER\0CHECKPOINT\0FACTORY";
    memcpy(wldReadBuf11, kNamePool, sizeof(kNamePool));

    /* The real mission-start path: worldImportToEgame() folds the START-side
     * arrays into g_planeTable/g_simObjects/waypoints (with the documented
     * +2-byte field shift), then the string pool parse fills
     * g_targetNameTable and g_ViewX/Y anchor on targets[0].baseIdx. */
    initMissionStrings();
}

/* Non-degenerate proof for the combat profile: the import ran, AI objects
 * maneuvered, and the alert/weapon loop engaged. Guards the same way
 * LoopCheck does — an empty "combat" sortie must fail, not pin. */
struct CombatCheck {
    bool importSeen = false;
    bool autopilotSeen = false;
    bool aiMoved = false;
    bool alertSeen = false;
    bool weaponSeen = false;
    bool pursuitSeen = false;   /* a projectile tracked a live targetRef */
    bool lifecycleSeen = false; /* launch -> ttl expiry on one slot */
    bool airLockSeen = false;   /* updateTargetLock acquired an air contact */
    bool killSeen = false;      /* a live sim object gained SIMOBJ_DESTROYED */
    bool projLive[12] = {};
    int16 seedX[8] = {};
    int16 seedY[8] = {};
    int16 ammoSeed = -1;
    int16 gunSeed = -1;
};
inline void combatObserve(CombatCheck &c, int tick) {
    if (tick == 1) {
        c.importSeen = (g_planeCount == 6 && g_groundUnitCount == 7 &&
                        waypoints[1].mapX == 0x3400 && g_targetNameTable[1][0] != '\0');
        for (int i = 0; i < 8; ++i) {
            c.seedX[i] = (int16)g_simObjects[i].posX;
            c.seedY[i] = (int16)g_simObjects[i].posY;
        }
    }
    if (!g_autopilotAltitude.isZero()) c.autopilotSeen = true;
    if (!g_threatActiveTimer.isZero() || g_activeThreatCount > 0) c.alertSeen = true;
    if (tick > 2) {
        for (int i = 0; i < 8; ++i) {
            if (g_simObjects[i].flags.w & SIMOBJ_DESTROYED) c.killSeen = true;
            if ((g_simObjects[i].flags.w & SIMOBJ_ALIVE) &&
                (g_simObjects[i].posX != (uint16)c.seedX[i] ||
                 g_simObjects[i].posY != (uint16)c.seedY[i])) c.aiMoved = true;
        }
    }
    if (c.ammoSeed < 0 && missleSpec[0].ammo > 0) {
        c.ammoSeed = missleSpec[0].ammo + missleSpec[1].ammo + missleSpec[2].ammo;
        c.gunSeed = g_gunAmmo;
    }
    if (missleSpec[0].ammo + missleSpec[1].ammo + missleSpec[2].ammo < c.ammoSeed ||
        g_gunAmmo < c.gunSeed || !g_projectiles[0].ttl.isZero()) c.weaponSeen = true;
    for (int i = 0; i < 12; ++i) {
        if (!g_projectiles[i].ttl.isZero()) {
            c.projLive[i] = true;
            /* Player missiles guide on targetLock (the g_airTargetLock/
             * g_groundTargetLock snapshot at launch); enemy shots carry a
             * nonzero targetRef. Either marks a guided pursuit. */
            if (g_projectiles[i].targetRef != 0 ||
                g_projectiles[i].targetLock >= 0) c.pursuitSeen = true;
        } else if (c.projLive[i]) c.lifecycleSeen = true;
    }
    if (g_airTargetLock >= 0 && g_airTargetLock < 0x80) c.airLockSeen = true;
}
/* requireKill: the fixed backend's pinned engagement kills the on-track
 * interceptor every run. The modern trajectory legitimately decorrelates —
 * whether its shot connects depends on where the drift lands, so killSeen is
 * observed but only required where it's deterministic. */
inline void combatRequire(const CombatCheck &c, bool requireKill = true) {
    require(c.importSeen, "combat: worldxfer import did not populate the tables");
    require(c.autopilotSeen, "combat: autopilot altitude-hold never engaged");
    require(c.aiMoved, "combat: no AI object ever moved");
    require(c.alertSeen, "combat: threat alert never engaged");
    require(c.weaponSeen, "combat: no weapon ever left the rail");
    require(c.pursuitSeen, "combat: no projectile ever tracked a target");
    require(c.lifecycleSeen, "combat: no projectile completed its ttl lifecycle");
    require(c.airLockSeen, "combat: updateTargetLock never acquired an air contact");
    if (requireKill)
        require(c.killSeen, "combat: no air target was destroyed");
}

/* Non-degenerate proof for the landing profile: the recovery guidance
 * actually flew the corridor — landing checks live in updateFrame, so no
 * render pass is needed. landed is the outcome: "Safe Landing" ->
 * finalizeMission(0) -> landingType 3. */
struct LandingCheck {
    bool importSeen = false;
    bool corridorSeen = false;  /* inside the recovery-base corridor */
    bool autoLandSeen = false;  /* g_autoLandingActive: guidance handed off */
    bool descentSeen = false;   /* recovery leg actually descended */
    bool landed = false;
    int16 altPeak = -1;
};
inline void landingObserve(LandingCheck &l, int tick) {
    if (tick == 1) {
        l.importSeen = (g_planeCount == 6 && g_groundUnitCount == 7 &&
                        waypoints[1].mapX == 0x3400 && g_targetNameTable[1][0] != '\0' &&
                        waypointIndex == 3);
    }
    const int16 alt = (int16)legacy::altitudeUnits(g_altitude);
    if (alt > l.altPeak) l.altPeak = alt;
    if (g_inLandingCorridor != 0) l.corridorSeen = true;
    if (g_autoLandingActive != 0) l.autoLandSeen = true;
    /* The player starts on the runway, so descent means "below the cruise
     * peak", not below the start. */
    if (l.altPeak > 500 && alt < l.altPeak - 300) l.descentSeen = true;
    if (commData && commData->landingType == 3 &&
        g_landingDoneFlag != 0 && g_missionEndedFlag[0] != 0) l.landed = true;
}
inline void landingRequire(const LandingCheck &l) {
    require(l.importSeen, "land: worldxfer import / recovery leg not seeded");
    require(l.corridorSeen, "land: never entered the recovery corridor");
    require(l.autoLandSeen, "land: auto-landing never engaged");
    require(l.descentSeen, "land: recovery leg never descended");
    require(l.landed, "land: no Safe Landing / finalizeMission(0)");
}

/* Non-degenerate proof for the theater-boundary profile. Phase 1 (ticks
 * 0-500) flies east into the 0x7e00 X bound; phase 2 (ticks 500+) flies
 * +mapY into the mirrored 0x7d00 Y bound. Each arm proves: the plane
 * reached its bound, the clamp pinned it (never past the bound word
 * post-updateFrame), it held under sustained power, the per-frame
 * nearest-base scan ranged a deep-band base through the 16-bit ring
 * wrap, and the retarget consumed the band coordinates. The Y arm also
 * pins the mirrored snap: at mapY 0x7d00 the fine coord must sit LOW
 * ((0x8000-0x7d00)<<5 = 0x6000) — a missing mirror would leave ~0xFA000. */
struct BoundaryCheck {
    bool approachedX = false, approachedY = false;
    bool edgeSeenX = false, edgeSeenY = false;
    bool speedKeptX = false, speedKeptY = false;
    bool seamSeenX = false, seamSeenY = false;
    bool wrapSeenX = false, wrapSeenY = false;
    bool waypointMovedX = false, waypointMovedY = false;
    bool snapSeenX = false, snapMirrorSeen = false;
    int clampedTicksX = 0, clampedTicksY = 0;
    int16 maxX = 0, maxY = 0;
};
inline void boundaryObserve(BoundaryCheck &b, int) {
    /* g_viewX_/g_viewY_ are refreshed from flightMapPosition() at the top
     * of updateFrame and overwritten by the clamp when a bound is hit —
     * so they are the post-clamp map words at observe time. */
    const int16 mx = g_viewX_;
    const int16 my = g_viewY_;
    if (mx > b.maxX) b.maxX = mx;
    if (my > b.maxY) b.maxY = my;
    if (mx > 0x7c40) b.approachedX = true;
    if (my > 0x7b40) b.approachedY = true;
    if (mx >= 0x7e00) {
        b.edgeSeenX = true;
        ++b.clampedTicksX;
        if (legacy::speedUnits(g_velocity) > 0x100) b.speedKeptX = true;
        /* The X snap pins fineX to 0x7e00<<5 exactly. */
        const std::uint32_t fx = (std::uint32_t)legacy::fineUnits(g_ViewX);
        if (fx >= 0x7e00u * 32 - 0x800 && fx <= 0x7e00u * 32)
            b.snapSeenX = true;
        /* While pinned at the east bound, the seeded base at mapX 0xff00
         * is 0x8100 words ahead — past int16 range — so the scan's
         * ~0x7f00 answer can only come through the 16-bit ring wrap. */
        if (g_closestThreatIndex == 4) b.seamSeenX = true;
        if (g_closestThreatIndex == 4 &&
            (int)g_nearestThreatRange >= 0x7e80 &&
            (int)g_nearestThreatRange <= 0x7fe0)
            b.wrapSeenX = true;
        if (waypoints[3].mapX == 0xff00 && waypoints[3].mapY == 0x4000)
            b.waypointMovedX = true;
    }
    if (my >= 0x7d00) {
        b.edgeSeenY = true;
        ++b.clampedTicksY;
        if (legacy::speedUnits(g_velocity) > 0x100) b.speedKeptY = true;
        /* The mirrored snap pins fineY LOW: (0x8000-0x7d00)<<5 = 0x6000. */
        const std::uint32_t fy = (std::uint32_t)legacy::fineUnits(g_ViewY);
        if (fy >= 0x5000 && fy <= 0x6000) b.snapMirrorSeen = true;
        /* Base 5 at mapY 0xff00: raw delta 0x8200, wrapped ~0x7e00. */
        if (g_closestThreatIndex == 5) b.seamSeenY = true;
        if (g_closestThreatIndex == 5 &&
            (int)g_nearestThreatRange >= 0x7d80 &&
            (int)g_nearestThreatRange <= 0x7f80)
            b.wrapSeenY = true;
        if (waypoints[3].mapX == 0x4000 && waypoints[3].mapY == 0xff00)
            b.waypointMovedY = true;
    }
}
inline void boundaryRequire(const BoundaryCheck &b) {
    require(b.approachedX, "wrap: never advanced east from the 0x7c00 seed");
    require(b.approachedY, "wrap: never advanced +mapY from the 0x7b00 seed");
    require(b.edgeSeenX, "wrap: never reached the 0x7e00 theater bound");
    require(b.edgeSeenY, "wrap: never reached the 0x7d00 theater bound");
    require(b.clampedTicksX >= 60, "wrap: X-boundary contact not sustained");
    require(b.clampedTicksY >= 60, "wrap: Y-boundary contact not sustained");
    require(b.speedKeptX && b.speedKeptY,
            "wrap: velocity died at a bound (clamp killed the plane "
            "instead of pinning position)");
    require(b.maxX <= 0x7e00, "wrap: position leaked past the X bound");
    require(b.maxY <= 0x7d00, "wrap: position leaked past the Y bound");
    require(b.snapSeenX, "wrap: fineX never pinned at the 0x7e00 snap value");
    require(b.snapMirrorSeen, "wrap: fineY never pinned at the mirrored "
                              "snap value (0x6000) — mirror transform lost?");
    require(b.seamSeenX, "wrap: nearest-base scan never picked the "
                         "X-band base (index 4) at the bound");
    require(b.wrapSeenX, "wrap: X-band base range not the wrapped "
                         "ring distance (expected ~0x7f00, not the "
                         "0x7fff cap an unwrapped scan would hit)");
    require(b.waypointMovedX, "wrap: waypoints[3] never retargeted to the "
                              "X-band base");
    require(b.seamSeenY, "wrap: nearest-base scan never picked the "
                         "Y-band base (index 5) at the bound");
    require(b.wrapSeenY, "wrap: Y-band base range not the wrapped "
                         "ring distance (expected ~0x7e00, not the "
                         "0x7fff cap an unwrapped scan would hit)");
    require(b.waypointMovedY, "wrap: waypoints[3] never retargeted to the "
                              "Y-band base");
}

/* Non-degenerate proof for the stall profile: the plane must actually
 * enter the stall regime (belowStall while airborne), the production
 * correctFlightStall response must drop the nose (pitch goes negative
 * while below stall with a centred stick — no other pitch authority),
 * and the dive must recover speed above the threshold without a crash. */
struct StallCheck {
    bool entrySeen = false;
    bool noseDropSeen = false;
    bool recoveredSeen = false;
    bool lostAirframe = false;
    int stallTicks = 0;
};
inline void stallObserve(StallCheck &s, int) {
    const bool below = AerodynamicsMath<GameBackend>::belowStall(
        g_velocity, g_stallSpeed);
    if (below) {
        s.entrySeen = true;
        ++s.stallTicks;
        /* Stick is centred through the stall window (pull ends at 280):
         * a negative pitch here can only be the correctFlightStall
         * nose-drop, not a stick command. */
        if (legacy::signedAngle(g_ourPitch) < -0x200) s.noseDropSeen = true;
    } else if (s.stallTicks > 0) {
        s.recoveredSeen = true;
    }
    if (g_ejectState != 0 || legacy::altitudeUnits(g_altitude) == 0)
        s.lostAirframe = true;
}
inline void stallRequire(const StallCheck &s) {
    require(s.entrySeen, "stall: never entered the stall regime "
                         "(belowStall while airborne)");
    require(s.stallTicks >= 20, "stall: stall regime not sustained");
    require(s.noseDropSeen, "stall: correctFlightStall nose-drop never "
                            "dropped the nose below the horizon");
    require(s.recoveredSeen, "stall: never recovered above the threshold");
    require(!s.lostAirframe, "stall: airframe lost (eject/ground) before "
                            "recovery");
}

/* worldExportToEnd round-trip: run the real EGAME -> END debrief export after
 * the sortie and check every block against the live tables — including the
 * reversed +2-byte unitRef shift (plane i re-exports the lead / previous
 * plane's secondaryNameIndex). */
inline void verifyWorldExport() {
    worldExportToEnd();
    require(worldObjectCount == 6, "export: plane count");
    require(worldSamCount == 7, "export: unit count");
    require(worldWaypointCount == (int16)(1 | (2 << 8)), "export: waypoint count");
    require(worldRouteCount == g_planeScanCount, "export: route count");
    require(worldGridSize == 200, "export: distance accumulator");
    for (int i = 0; i < 6; ++i) {
        const MapTarget &p = g_planeTable.planes[i];
        require(worldObjects[i].x_coord == p.mapX && worldObjects[i].y_coord == p.mapY &&
                worldObjects[i].unitType == p.active && worldObjects[i].targetFlags == p.flags &&
                worldObjects[i].occupantType == p.alertLevel &&
                worldObjects[i].patrolCount == p.threatTimer &&
                worldObjects[i].objectIdx == p.nameIndex,
                "export: plane record field mismatch");
        const uint16 ref = (i == 0) ? (uint16)g_planeTable.nameIndexLead
                                    : (uint16)g_planeTable.planes[i - 1].secondaryNameIndex;
        require(worldObjects[i].unitRef == ref, "export: shifted unitRef mismatch");
    }
    require(memcmp(worldSamTable, g_simObjects, 7 * sizeof(struct SimObject)) == 0,
            "export: sim object block");
    require(memcmp(&weaponDataBlock, waypoints, 16) == 0, "export: waypoint block");
    require(memcmp(&targetBlock, g_targetSlots, sizeof(TargetBlock)) == 0,
            "export: target slots");
    require(memcmp(worldStringBuf, g_stringPool, 750) == 0, "export: string pool");
    require(memcmp(unitTypeTable, g_shapeTargetCategory, 100) == 0, "export: categories");
    require(memcmp(worldUnitFlags, g_tileKillTally, 100) == 0, "export: kill tally");
    require(memcmp(gridFlags, g_mapCellFlags, 256) == 0, "export: grid flags");
    const int16 padlockWord = (int16)(worldMiscHeader[0] | (worldMiscHeader[1] << 8));
    require(padlockWord == g_padlockAircraft, "export: padlock slot");
}

inline void initSortie(Profile profile = Profile::kSortie) {
    test_headless_init();
    require(SDL_Init(SDL_INIT_GAMEPAD), "initialize SDL events");
    input_setMode(INPUT_MODE_FLIGHT);
    input_setJoystickSetup(false);
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    require(SDL_PushEvent(&focus), "focus flight input");
    input_pumpEvents();

    gfx_videoInit();
    gfx_setMode13();
    setupInstrumentLayoutFar();

    /* Static: gameData/commData must stay valid after initSortie returns. */
    static Game game{};
    static GameComm comm{};
    gameData = &game;
    commData = &comm;
    game.difficulty = 2;
    game.unk4 = 2;
    /* Non-empty loadout so the scheduled fire keys reach fireMissile(): the
     * mission-init block copies these into missleSpec[].ammo. */
    for (int i = 0; i < 3; ++i) {
        comm.weaponType[i] = (uint16)(i + 1);
        comm.weaponCount[i] = 4;
    }

    gameSrand(kSeed);

    /* Virtual 2-axis stick: attached before the pump so the JOYSTICK_ADDED
     * hotplug event opens it through joy_open() like real hardware. Kept
     * centred until the schedule deflects it — kSortie never deflects, so
     * input_preferGamepad() stays false and its byte path is unchanged. */
    SDL_VirtualJoystickDesc stickDesc;
    SDL_INIT_INTERFACE(&stickDesc);
    stickDesc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
    stickDesc.name = "sortie-stick";
    stickDesc.naxes = 2;
    vjoyId() = SDL_AttachVirtualJoystick(&stickDesc);
    require(vjoyId() != 0, "attach virtual joystick");

    g_initPhase = 1;               /* first updateFrame runs mission init */
    g_frameRateScaling = f15::math::SimRate::fromWord(15);
    frameTick = f15::math::Ticks::fromWord(1);
    /* seedRng() (inside initFrameRandom, mission init) clock-seeds unless input
     * is disabled; pin the deterministic path for the init tick, then enable. */
    g_inputDisabled = 1;
    g_rngSeed = (int16)kSeed;
    g_thrust = legacy::thrustFromUnits(35);
    g_setThrust = f15::math::legacy::thrustFromUnits(5);
    g_fuelRemaining = f15::math::legacy::fuelFromUnits(9000);
    g_viewZ = 4000;
    g_groundAltitude = {};
    g_altitude = legacy::altitudeFromUnits(4000);
    g_velocity = legacy::speedFromUnits(8100);
    g_knots = AirspeedMath<GameBackend>::knots(300);
    g_playerPlaneFlags = 0;        /* gear down on the runway */
    g_gearDownArmed = 1;
    g_cornerSpeed = AirspeedMath<GameBackend>::knots(100);
    g_kbdSensitivity = 2;
    g_gunHits = g_hudVisible = 0;
    g_ejectState = g_autoCrashDive = g_currentWeaponType = 0;
    g_autopilotEngaged = 0;
    g_autopilotAltitude = {};
    g_missionTick = f15::math::TickDuration{};
    g_waypointBearing = legacy::angleFromWord(0);
    waypointIndex = 1;
    if (profile == Profile::kCombat || profile == Profile::kLand) {
        /* The mission import places the player's start (view anchors on
         * planes[targets[0].baseIdx]); don't re-pin g_ViewX/Y here. */
        seedCombatMission();
        if (profile == Profile::kCombat) {
            /* Combat runs renderFrame() per tick (updateTargetLock lives in
             * the render path): register a real rasterizer. Software always
             * claims; it draws into the dummy video's frame buffer. */
            r3d_init();
        } else {
            /* kLand: difficulty 0 is what the corridor check gates on
             * (g_missionStatus==0); it also gives the airborne mission start
             * (altitude 2000, speed 8100) a recovery leg wants. The base
             * needs flags & 0x500 plus & 0x201 (and not 0x800) to be a usable
             * landing field. The 0x6000/waypointIndex=3 seeds go in runTick —
             * the initPhase block on tick 0 resets both. */
            game.difficulty = 0;
            g_planeTable.planes[g_targetSlots[1].viewIndex].flags |= 0x601;
        }
    } else {
        g_ViewX = legacy::viewX(5000);
        g_ViewY = legacy::viewY(-5000);
        g_viewX_ = 5000;
        g_viewY_ = 60000;
    }
    g_ourHead = legacy::angleFromWord(4000);
    g_ourPitch = g_ourRoll = {};
    g_stallSpeed = {};
    g_liftForce = g_rollPitchTrim = {};
    g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;

    if (profile != Profile::kCombat && profile != Profile::kLand) {
        /* A few seeded contacts so threat/targeting/object paths engage.
         * kCombat/kLand keep the worldxfer-imported tables instead — this
         * block would overwrite their plane flags/counts. */
        g_planeCount = 12;
        /* No mission assets are loaded: point every name-table slot at an
         * empty string so placeString() (spawnEnemyAircraft, waypoints) stays
         * valid. */
        g_stringPool[0] = '\0';
        for (int i = 0; i < MODEL_SLOT_CAPACITY; ++i) g_targetNameTable[i] = g_stringPool;
        g_groundUnitCount = 4;
        g_planeTable.planes[1].mapX = 12000;
        g_planeTable.planes[1].mapY = 20000;
        g_planeTable.planes[1].active = 1;
        g_planeTable.planes[1].flags = 0x100;
        g_planeTable.planes[2].mapX = 40000;
        g_planeTable.planes[2].mapY = 10000;
        g_planeTable.planes[2].active = 1;
        g_planeTable.planes[2].flags = 0x200;
        g_planeTable.planes[3].mapX = 8000;
        g_planeTable.planes[3].mapY = 40000;
        g_planeTable.planes[3].active = 1;
        g_planeTable.planes[3].flags = 0x100;
        for (int i = 0; i < 4; ++i) {
            g_simObjects[i].posX = (uint16)(9000 + i * 4000);
            g_simObjects[i].posY = (uint16)(15000 + i * 3000);
            f15::math::legacy::objectLinearSet(g_simObjectAlt[i], g_simObjects[i].alt,
                                               (int16)(200 + i * 50));
            f15::math::legacy::objectAttitudeSet(g_simObjectHeading[i], g_simObjects[i].heading.w,
                                                 f15::math::legacy::angleFromWord((int16)(i * 8192)));
            f15::math::legacy::objectLinearSet(g_simObjectSpeed[i], g_simObjects[i].speed, 40);
            g_simObjects[i].spec = 1;
            g_simObjects[i].flags.w = 0x100;
        }
    }

    rebuildOrientation();
}

inline void teardown() {
    gameData = nullptr;
    commData = nullptr;
    if (vjoyId()) SDL_DetachVirtualJoystick(vjoyId());
    r3d_shutdown();
    gfx_videoShutdown();
    SDL_Quit();
}

/* Runs one scripted tick: scheduled keys -> pump -> stick axis -> sim step.
 * The virtual axis is set after the pump, matching the tag-side harness that
 * recorded the stick oracle; the value persists on the device until the next
 * set, so every reader (this pump's bookkeeping, the step's own re-pump, and
 * all later ticks) sees the current schedule position. */
inline void runTick(int tick, Profile profile = Profile::kSortie) {
    pushScheduleKeys(tick, profile);
    input_pumpEvents();
    applyScheduleStick(tick, profile);
    stepFlightModel();
    updateFrame();
    if (profile == Profile::kWrap && tick == 0) {
        /* Airborne start near the east theater bound: the confine block in
         * updateFrame (clampRange to [0x100,0x7e00]x[0x200,0x7d00], fine coord
         * snapped to the cell edge) is the path under test — it fires only at
         * spawn in the other profiles. Heading 0x4000 = +mapX. */
        g_altitude = legacy::altitudeFromUnits(2000);
        g_viewZ = 2000;
        g_velocity = legacy::speedFromUnits(8100);
        g_ViewX = legacy::viewX(0x7c00 * 32);
        g_ViewY = legacy::viewY((0x8000 - 0x4000) * 32);
        g_ourHead = legacy::angleFromWord((int16)0x4000);
        rebuildOrientation();
        /* A usable base deep in the unreachable band at mapX 0xff00: the
         * raw delta 0x8100 does not fit int16, so the nearest-base scan at
         * egframe.c:261 can only produce its ~0x7f00 range through the
         * 16-bit ring wrap — 0x7fff (cap) would prove the wrap never ran.
         * It then retargets waypoints[3] and respawns the base's ground
         * contacts at the seam-adjacent coordinates. */
        g_planeTable.planes[4].mapX = 0xff00;
        g_planeTable.planes[4].mapY = 0x4000;
        g_planeTable.planes[4].active = 1;
        g_planeTable.planes[4].flags = 0x601;
    }
    if (profile == Profile::kWrap && tick == 500) {
        /* Phase 2: midfield at mapY 0x7b00 heading 0x8000 = +mapY (the
         * same word the land final uses — the mirrored axis makes the
         * compass read backwards). The south bound snap is the mirrored
         * transform g_ViewY = viewY((0x8000-cy)<<5). */
        g_ViewX = legacy::viewX(0x4000 * 32);
        g_ViewY = legacy::viewY((0x8000 - 0x7b00) * 32);
        g_ourHead = legacy::angleFromWord((int16)0x8000);
        rebuildOrientation();
        /* The nearest-base retarget only fires when the winning INDEX
         * changes — a second band base (index 5) must outbid base 4, so
         * base 4 retires and base 5 sits at mapY 0xff00: raw delta 0x8200,
         * again only expressible through the ring wrap (~0x7e00). */
        g_planeTable.planes[4].flags = 0;
        g_planeTable.planes[4].active = 0;
        g_planeTable.planes[5].mapX = 0x4000;
        g_planeTable.planes[5].mapY = 0xff00;
        g_planeTable.planes[5].active = 1;
        g_planeTable.planes[5].flags = 0x601;
    }
    if (profile == Profile::kStall && tick == 0) {
        /* Airborne just above the static stall threshold (~2780 units):
         * idle throttle plus the climb-bleed crosses belowStall, where
         * correctFlightStall applies its nose-drop each tick — the path
         * under test. Altitude 6000 leaves dive-recovery room: modern's
         * fractional dive loses ~1800 more than fixed's. */
        g_altitude = legacy::altitudeFromUnits(6000);
        g_viewZ = 6000;
        g_velocity = legacy::speedFromUnits(3200);
        g_ViewX = legacy::viewX(0x4000 * 32);
        g_ViewY = legacy::viewY((0x8000 - 0x4000) * 32);
        g_ourHead = legacy::angleFromWord((int16)0x4000);
        rebuildOrientation();
    }
    if (profile == Profile::kLand && tick == 0) {
        /* Pretend both targets were destroyed — the recovery leg is the path
         * under test, not the kill chain that sets these flags. Applied after
         * tick 0's updateFrame because the g_initPhase mission-init block
         * resets playerPlaneFlags and waypointIndex. */
        g_playerPlaneFlags |= 0x6000;
        waypointIndex = 3;
        /* Start on final: 0x400 map words up the extended centerline (the
         * base sits at 0x4400,0x2000; the ns=-1 recovery direction approaches
         * from lower mapY) heading 0x8000 — the bearing the last verified run
         * held through the corridor. Skipping the downwind pattern keeps the
         * profile compact while the corridor -> auto-land -> Safe Landing
         * chain still runs end to end, and a short final leaves the modern
         * backend little room to drift below the glide before the corridor
         * box (|dx|<=8, |dy|<=30 around the base). View coords are fine
         * units (map x 32), Y mirrored against 0x8000. */
        g_ViewX = legacy::viewX(0x4400 * 32);
        g_ViewY = legacy::viewY((0x8000 - 0x1c00) * 32);
        g_ourHead = legacy::angleFromWord((int16)0x8000);
        /* The Euler word alone isn't authoritative: the flight step derives
         * heading from the orientation matrix, which still holds the old
         * attitude. Rebuild it so the seeded heading actually sticks. */
        rebuildOrientation();
    }
    /* kCombat also runs a render frame per tick: production calls
     * renderFrame() from gameMainLoop after the sim steps, and
     * updateTargetLock() — the air-target scan that acquires locks —
     * lives inside it. Without this the lock path never runs in the
     * harness (g_targetRange stays 0 the whole sortie). */
    if (profile == Profile::kCombat) renderFrame();
    /* Mission init ran on tick 0 (deterministic seed via g_inputDisabled);
     * enable the scripted inputs from here on. */
    if (tick == 0) g_inputDisabled = 0;
}

} // namespace sortie
#endif
