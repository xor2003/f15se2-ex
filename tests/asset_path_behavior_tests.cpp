#include "shared/asset_path.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::string &value) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", value.c_str());
#else
    if (value.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto testRoot =
        std::filesystem::temp_directory_path() / "f15se2-ex-asset-path-tests";
    std::filesystem::remove_all(testRoot);
    std::filesystem::create_directories(testRoot / "Fonts");
    std::ofstream(testRoot / "WALL.PNG", std::ios::binary) << "png";
    std::ofstream(testRoot / "Fonts" / "FONT_1.BDF", std::ios::binary) << "bdf";

    char path[1024] = {};
    setReplacementRoot("");
    require(!findAssetReplacement("WALL.png", path, sizeof(path)),
            "lookup is disabled without an explicit replacement root");

    setReplacementRoot(testRoot.string());
    require(findAssetReplacement("wall.png", path, sizeof(path)),
            "lookup matches DOS-style uppercase names");
    require(std::filesystem::path(path).filename() == "WALL.PNG",
            "lookup returns the actual filesystem spelling");

    require(findAssetReplacement("fonts/font_1.bdf", path, sizeof(path)),
            "lookup matches every nested path component case-insensitively");
    require(!findAssetReplacement("../WALL.PNG", path, sizeof(path)),
            "lookup rejects parent traversal");
    require(!findAssetReplacement(testRoot.string().c_str(), path, sizeof(path)),
            "lookup rejects absolute paths");

    char shortPath[2] = {'x', '\0'};
    require(!findAssetReplacement("WALL.PNG", shortPath, sizeof(shortPath)),
            "lookup rejects an undersized output buffer");
    require(shortPath[0] == '\0', "failed lookup clears the output buffer");
    require(!findAssetReplacement("missing.png", path, sizeof(path)),
            "lookup rejects missing files");
    require(path[0] == '\0', "missing lookup clears the output buffer");

    require(!findAssetReplacement("Fonts", path, sizeof(path)),
            "a directory cannot replace a file");
    require(!findAssetReplacement(nullptr, path, sizeof(path)),
            "null input is rejected");
    require(!findAssetReplacement("WALL.PNG", nullptr, sizeof(path)),
            "null output is rejected");

#if !defined(_WIN32)
    /* Windows hosts need privileges for symlinks; exercise boundary behavior
     * on POSIX, including a sibling whose name shares the root prefix. */
    const auto outside = std::filesystem::path(testRoot.string() + "-outside");
    std::filesystem::create_directories(outside);
    std::ofstream(outside / "OUT.PNG", std::ios::binary) << "outside";
    std::filesystem::create_symlink(outside / "OUT.PNG", testRoot / "ESCAPE.PNG");
    std::filesystem::create_directory_symlink(outside, testRoot / "ESCAPE");
    std::filesystem::create_symlink(testRoot / "WALL.PNG", testRoot / "ALIAS.PNG");
    require(!findAssetReplacement("escape.png", path, sizeof(path)),
            "a file symlink cannot escape the root");
    require(!findAssetReplacement("escape/out.png", path, sizeof(path)),
            "a directory symlink cannot escape the root");
    require(findAssetReplacement("alias.png", path, sizeof(path)),
            "symlinks within the root remain usable");
    std::filesystem::remove_all(outside);
#endif

    /* Only case-sensitive filesystems can hold both spellings. */
    if (!std::filesystem::exists(testRoot / "wall.png")) {
        std::ofstream(testRoot / "wall.png", std::ios::binary) << "duplicate";
        require(!findAssetReplacement("wall.png", path, sizeof(path)),
                "ambiguous DOS spellings are rejected");
        require(path[0] == '\0', "ambiguous lookup clears the output buffer");
    }

    setReplacementRoot("");
    std::filesystem::remove_all(testRoot);
    std::cout << "asset_path_behavior_tests passed\n";
    return 0;
}
