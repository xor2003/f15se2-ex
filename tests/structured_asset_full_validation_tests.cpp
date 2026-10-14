#include "shared/common.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern bool setGamePath(const char *path);

#if !defined(F15_ORIGINAL_ASSETS)
#define F15_ORIGINAL_ASSETS ""
#endif
#if !defined(F15_CONVERTED_ASSETS)
#define F15_CONVERTED_ASSETS ""
#endif
#if !defined(F15_ASSET_TOOL_COMMAND)
#define F15_ASSET_TOOL_COMMAND ""
#endif

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return value;
}

bool isStructured(const std::filesystem::path &path) {
    const std::string extension = upper(path.extension().string());
    return extension == ".WLD" || extension == ".3DT" || extension == ".3DG";
}

void setEnvironment(const char *name, const std::string *value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value->c_str() : "");
#else
    if (value) setenv(name, value->c_str(), 1);
    else unsetenv(name);
#endif
}

std::vector<unsigned char> readAsset(const std::string &logicalName) {
    SDL_IOStream *stream = openFile(logicalName.c_str(), 0);
    require(stream != nullptr, "cannot open " + logicalName);
    std::vector<unsigned char> bytes;
    unsigned char chunk[4096];
    size_t count;
    while ((count = SDL_ReadIO(stream, chunk, sizeof(chunk))) != 0) {
        bytes.insert(bytes.end(), chunk, chunk + count);
    }
    fileClose(stream);
    return bytes;
}

} // namespace

int main() {
    const std::filesystem::path originalRoot = std::filesystem::absolute(F15_ORIGINAL_ASSETS);
    const std::filesystem::path convertedRoot = std::filesystem::absolute(F15_CONVERTED_ASSETS);
    const std::string toolCommand = F15_ASSET_TOOL_COMMAND;
    const auto previousDirectory = std::filesystem::current_path();
    const auto isolatedRoot = std::filesystem::temp_directory_path() /
                              "f15se2-structured-full-validation";
    const auto legacyRoot = isolatedRoot / "legacy";
    const auto modernRoot = isolatedRoot / "modern";
    std::filesystem::remove_all(isolatedRoot);
    std::filesystem::create_directories(legacyRoot);
    std::filesystem::create_directories(modernRoot);
    // Neither read may discover the other format through fallback locations.
    // In particular, a missing replacement must fail, not compare legacy to itself.
    std::filesystem::current_path(isolatedRoot);
    setEnvironment("F15_ASSET_TOOL", &toolCommand);

    int compared = 0;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !isStructured(entry.path())) continue;
        const std::string logicalName =
            std::filesystem::relative(entry.path(), originalRoot).generic_string();
        const auto isolatedOriginal = legacyRoot / logicalName;
        std::filesystem::create_directories(isolatedOriginal.parent_path());
        std::filesystem::copy_file(entry.path(), isolatedOriginal);
        require(setGamePath(legacyRoot.string().c_str()), "legacy setGamePath failed");
        setEnvironment("F15_REPLACEMENT_ROOT", nullptr);
        const std::vector<unsigned char> legacy = readAsset(logicalName);
        const std::string replacementRoot = convertedRoot.string();
        require(setGamePath(modernRoot.string().c_str()), "modern setGamePath failed");
        setEnvironment("F15_REPLACEMENT_ROOT", &replacementRoot);
        const std::vector<unsigned char> replacement = readAsset(logicalName);
        require(replacement == legacy,
                "rebuilt structured bytes differ for " + logicalName);
        ++compared;
    }

    setEnvironment("F15_REPLACEMENT_ROOT", nullptr);
    setEnvironment("F15_ASSET_TOOL", nullptr);
    std::filesystem::current_path(previousDirectory);
    std::filesystem::remove_all(isolatedRoot);
    require(compared > 0, "no WLD/3DT/3DG assets were compared");
    std::cout << "structured_asset_full_validation_tests compared "
              << compared << " assets\n";
    return 0;
}
