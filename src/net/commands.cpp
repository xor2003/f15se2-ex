/*
 * commands.cpp - NetCmd <-> BIOS word adapter (plan §3).
 *
 * The wire carries semantic commands; each endpoint maps them to/from the
 * legacy keyboard words that stepFlightModel()/keyDispatch() understand.
 * The table is the single source of truth for both directions.
 */
#include <stdint.h>

#include "commands.h"
#include "egkeys.h"

struct CmdMap {
    uint8_t cmd;
    uint16_t scan;
};

static const CmdMap kCmdToScan[] = {
    {NC_THROTTLE_DOWN, SCAN_MINUS},
    {NC_THROTTLE_UP, SCAN_EQUAL},
    {NC_AFTERBURNER, SCAN_A},
    {NC_THROTTLE_MAX, SCAN_SHIFT_EQUAL},
    {NC_THROTTLE_CUT, SCAN_SHIFT_MINUS},
    {NC_BRAKE_TOGGLE, SCAN_B},
    {NC_GEAR_TOGGLE, SCAN_L},
    {NC_GUN_FIRE, SCAN_BACKSPACE},
    {NC_MISSILE_FIRE, SCAN_ENTER},
    {NC_WEAPON_SIDEWINDER, SCAN_S},
    {NC_WEAPON_AMRAAM, SCAN_M},
    {NC_WEAPON_GROUND, SCAN_G},
    {NC_TARGET_DESIGNATE, SCAN_T},
    {NC_FLARE, SCAN_F},
    {NC_CHAFF, SCAN_C},
    {NC_RADAR_RANGE, SCAN_R},
    {NC_MAP_ZOOM_IN, SCAN_Z},
    {NC_MAP_ZOOM_OUT, SCAN_X},
    {NC_AUTOPILOT_TOGGLE, SCAN_P},
    {NC_WAYPOINT_NEXT, SCAN_W},
    {NC_DIRECTOR_CYCLE, SCAN_D},
    {NC_ACCEL_TOGGLE, SCAN_ALT_A},
    {NC_VIEW_COCKPIT, SCAN_SPACEBAR},
    {NC_VIEW_FORWARD, SCAN_F1},
    {NC_VIEW_LEFT, SCAN_F2},
    {NC_VIEW_RIGHT, SCAN_F3},
    {NC_VIEW_REAR, SCAN_F4},
    {NC_VIEW_EXT_FOLLOW, SCAN_F5},
    {NC_VIEW_EXT_DYNAMIC, SCAN_F6},
    {NC_VIEW_EXT_SIDE, SCAN_F7},
    {NC_VIEW_MISSILE, SCAN_F8},
    {NC_VIEW_EXT_TARGET, SCAN_F9},
    {NC_VIEW_TARGET, SCAN_F10},
    {NC_EJECT, SCAN_ESCAPE},
    {NC_ABORT_MISSION, SCAN_ALT_Q},
    {NC_DETAIL_CYCLE, SCAN_ALT_D},
    {NC_NIGHT_TOGGLE, SCAN_ALT_N},
    {NC_TRAINING_TOGGLE, SCAN_ALT_T},
    {NC_SOUND_CYCLE, SCAN_ALT_V},
    {NC_KBDSENS_CYCLE, SCAN_ALT_K},
    {NC_JOY_CALIBRATE, SCAN_ALT_J},
    {NC_SCREENSHOT, SCAN_ALT_B},
    {NC_PAUSE, SCAN_ALT_P},
};

uint16_t netCmdToScan(uint8_t cmd) {
    for (size_t i = 0; i < sizeof(kCmdToScan) / sizeof(kCmdToScan[0]); i++) {
        if (kCmdToScan[i].cmd == cmd)
            return kCmdToScan[i].scan;
    }
    return 0;
}

uint8_t netScanToCmd(uint16_t scan) {
    for (size_t i = 0; i < sizeof(kCmdToScan) / sizeof(kCmdToScan[0]); i++) {
        if (kCmdToScan[i].scan == scan)
            return kCmdToScan[i].cmd;
    }
    return NC_NONE;
}

/* Commands that only affect local presentation (view selection, map zoom,
 * detail level, audio, calibration). Clients still send them - the server ctx
 * tracks them harmlessly - but the client ALSO applies them locally for its
 * own renderer. */
int netCmdIsLocalOnly(uint8_t cmd) {
    switch (cmd) {
    case NC_VIEW_COCKPIT:
    case NC_VIEW_FORWARD:
    case NC_VIEW_LEFT:
    case NC_VIEW_RIGHT:
    case NC_VIEW_REAR:
    case NC_VIEW_EXT_FOLLOW:
    case NC_VIEW_EXT_DYNAMIC:
    case NC_VIEW_EXT_SIDE:
    case NC_VIEW_MISSILE:
    case NC_VIEW_EXT_TARGET:
    case NC_VIEW_TARGET:
    case NC_DETAIL_CYCLE:
    case NC_NIGHT_TOGGLE:
    case NC_SOUND_CYCLE:
    case NC_KBDSENS_CYCLE:
    case NC_JOY_CALIBRATE:
    case NC_SCREENSHOT:
    case NC_PAUSE:
    case NC_MAP_ZOOM_IN:
    case NC_MAP_ZOOM_OUT:
        return 1;
    default:
        return 0;
    }
}
