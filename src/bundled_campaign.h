#pragma once

#include <filesystem>
#include <SDL3/SDL.h>

// Explicit data/campaign selections must never inherit bundled replacements.
static inline bool selectBundledCampaign(int argc, char **argv) {
    if (getenv("F15SE2_DIR") || getenv("F15_CAMPAIGN") ||
        getenv("F15_WORLD_SCENARIO") || getenv("F15_REPLACEMENT_ROOT")) return false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--game") || !strcmp(argv[i], "--campaign") ||
            !strcmp(argv[i], "--scenario")) return false;
    }
    const char *base = SDL_GetBasePath();
    if (!base) return false;
    const std::filesystem::path root = std::filesystem::path(base) / "campaigns";
    if (!std::filesystem::exists(root / "SVN" / "campaign.json")) return false;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "F15_REPLACEMENT_ROOT", root.string().c_str(), true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "F15_REPLACEMENT_ROOT_ONLY", "1", true);
    return setGamePath((root / "SVN").string().c_str());
}
