/* Persistence is separate from input and UI. Profiles are deliberately small,
 * versioned text files; explicit environment bindings are applied after loading. */
#include "joystick_mapping.h"
#include <fstream>

static const char *actionNames[RAW_ACTION_COUNT] = {
    "cannon", "missile", "countermeasure", "weapon", "thrust_up", "thrust_down",
    "gear", "autopilot", "target", "view"
};

/* Reject out-of-range and duplicate assignments as a unit, not partially. */
static bool validMapping(int count, const int *buttons) {
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        if (buttons[i] < -1 || buttons[i] >= count) return false;
        for (int j = 0; j < i; ++j)
            if (buttons[i] >= 0 && buttons[i] == buttons[j]) return false;
    }
    return true;
}

/* SDL supplies a writable platform-specific directory. The optional directory
 * override is also used by tests so they never alter a user's real mappings. */
std::string joy_mappingPath(SDL_Joystick *joystick) {
    if (!joystick) return {};
    const char *overrideDir = SDL_getenv("F15_JOY_CONFIG_DIR");
    char *pref = overrideDir ? nullptr : SDL_GetPrefPath("f15se2-ex", "joystick");
    const std::string directory = overrideDir ? overrideDir : pref ? pref : "";
    SDL_free(pref);
    if (directory.empty() || !SDL_CreateDirectory(directory.c_str())) return {};
    char guid[33] = {};
    SDL_GUIDToString(SDL_GetJoystickGUID(joystick), guid, sizeof(guid));
    return directory + "/" + guid + "-" + std::to_string(SDL_GetNumJoystickAxes(joystick)) +
           "-" + std::to_string(SDL_GetNumJoystickButtons(joystick)) + ".txt";
}

/* Parse into a candidate first; missing, malformed, or future versions cannot
 * overwrite valid defaults. Stored button numbers are one-based, zero is off. */
bool joy_loadMapping(const std::string &path, int count, int *buttons) {
    std::ifstream file(path);
    std::string name;
    int version = 0;
    int candidate[RAW_ACTION_COUNT] = {};
    if (!(file >> name >> version) || name != "F15_JOYSTICK" || (version != 1 && version != 2)) return false;
    /* Version 1 had six actions. Preserve them and leave new actions unassigned
     * so they cannot collide with buttons already chosen by the user. */
    const int savedActions = version == 1 ? RAW_THRUST_DOWN + 1 : RAW_ACTION_COUNT;
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) candidate[i] = -1;
    for (int i = 0; i < savedActions; ++i) {
        int button = 0;
        if (!(file >> name >> button) || name != actionNames[i] || button < 0 || button > count) return false;
        candidate[i] = button - 1;
    }
    if (file >> name || !file.eof() || !validMapping(count, candidate)) return false;
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) buttons[i] = candidate[i];
    return true;
}

/* Rename within the same directory preserves the last saved mapping when a
 * write fails. Unique temporary names avoid clobbering another running game. */
bool joy_saveMapping(const std::string &path, int count, const int *buttons) {
    if (path.empty() || !validMapping(count, buttons)) return false;
    std::string data = "F15_JOYSTICK 2\n";
    for (int i = 0; i < RAW_ACTION_COUNT; ++i)
        data += std::string(actionNames[i]) + " " + std::to_string(buttons[i] + 1) + "\n";
    const std::string temporary = path + "." + std::to_string(SDL_GetPerformanceCounter()) + ".tmp";
    const bool saved = SDL_SaveFile(temporary.c_str(), data.data(), data.size()) &&
                       SDL_RenamePath(temporary.c_str(), path.c_str());
    if (!saved) SDL_RemovePath(temporary.c_str());
    return saved;
}
