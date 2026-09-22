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
#include "input.h"
#include "strand.h"
#include "gfx.h"
#include "egcode.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

void stepFlightModel();
void updateFrame();
void rebuildOrientation();

namespace sortie {
using namespace f15::math;

constexpr int kSortieTicks = 660;
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
 * regression path — with no weapons or autopilot phases. */
enum class Profile { kSortie, kLoop };

/* Discrete cockpit commands for this tick, pushed before the pump so the
 * translated BIOS word reaches the key ring stepFlightModel drains. */
inline void pushScheduleKeys(int tick, Profile profile) {
    if (tick < 40) pushKey(SDL_SCANCODE_EQUALS, SDLK_EQUALS);      // throttle ramp
    if (tick == 45) pushKey(SDL_SCANCODE_G, SDLK_G);              // gear up
    if (profile == Profile::kLoop) return;
    if (tick == 280) for (int i = 0; i < 16; ++i)                 // throttle cut
        pushKey(SDL_SCANCODE_MINUS, SDLK_MINUS);
    if (tick == 390) pushKey(SDL_SCANCODE_W, SDLK_W);             // weapon select
    if (tick == 392 || tick == 394) pushKey(SDL_SCANCODE_RETURN, SDLK_RETURN);   // fire missile
    if (tick == 400) pushKey(SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE);          // guns
    if (tick == 540) {
        pushKey(SDL_SCANCODE_G, SDLK_G);                          // gear down
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
    }
    /* kSortie intentionally stays centred: its golden was recorded while
     * byte writes were being absorbed by updateStick — i.e. it pins a
     * centered-stick sortie. Real stick coverage lives in kLoop. */
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

inline void dumpMission() {
    std::printf("mstatus=%d landT=%d mtick=%d nearR=%d tgtR=%d tgtB=%d ns=%d ended=%d landT2=%d corr=%d threat=%d dirDl=%d slow=%d apEng=%d\n",
                (int)g_missionStatus, (int)g_landingTimer.word(), (int)g_missionTick.word(),
                (int)g_nearestThreatRange, (int)g_targetRange, (int)legacy::signedAngle(g_targetBearing),
                (int)g_northSouthSign, (int)g_missionEndedFlag[0],
                commData ? (int)commData->landingType : -1, (int)g_inLandingCorridor,
                (int)g_closestThreatIndex, (int)g_directorEventDeadline.word(),
                (int)g_slowMotionMode, (int)g_autopilotEngaged);
}

inline void initSortie() {
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
    g_ViewX = legacy::viewX(5000);
    g_ViewY = legacy::viewY(-5000);
    g_ourHead = legacy::angleFromWord(4000);
    g_ourPitch = g_ourRoll = {};
    g_stallSpeed = {};
    g_liftForce = g_rollPitchTrim = {};
    g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;
    g_viewX_ = 5000;
    g_viewY_ = 60000;

    /* A few seeded contacts so threat/targeting/object paths engage. */
    g_planeCount = 12;
    /* No mission assets are loaded: point every name-table slot at an empty
     * string so placeString() (spawnEnemyAircraft, waypoints) stays valid. */
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

    rebuildOrientation();
}

inline void teardown() {
    gameData = nullptr;
    commData = nullptr;
    if (vjoyId()) SDL_DetachVirtualJoystick(vjoyId());
    gfx_videoShutdown();
    SDL_Quit();
}

/* Runs one scripted tick: scheduled keys + stick axis -> pump -> sim step.
 * The virtual axis is set before the pump so every reader this tick (the
 * pump's updateStick bookkeeping, then the step's own re-pump) sees it. */
inline void runTick(int tick, Profile profile = Profile::kSortie) {
    pushScheduleKeys(tick, profile);
    applyScheduleStick(tick, profile);
    input_pumpEvents();
    stepFlightModel();
    updateFrame();
    /* Mission init ran on tick 0 (deterministic seed via g_inputDisabled);
     * enable the scripted inputs from here on. */
    if (tick == 0) g_inputDisabled = 0;
}

} // namespace sortie
#endif
