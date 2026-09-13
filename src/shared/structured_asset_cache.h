#ifndef F15_STRUCTURED_ASSET_CACHE_H
#define F15_STRUCTURED_ASSET_CACHE_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace f15assets {

inline std::uint32_t cacheUint32(const unsigned char *bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
           (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

inline bool loadStructuredCache(const std::string &jsonPath,
                                std::vector<std::uint8_t> &output) {
    constexpr std::uint32_t maxBytes = 1024 * 1024;
    std::ifstream cache(jsonPath + ".runtime", std::ios::binary);
    unsigned char header[16] = {};
    if (!cache.read(reinterpret_cast<char *>(header), sizeof(header)) ||
        std::memcmp(header, "F15BIN1\0", 8) != 0) return false;
    const auto sourceBytes = cacheUint32(header + 8);
    const auto payloadBytes = cacheUint32(header + 12);
    if (sourceBytes == 0 || sourceBytes > maxBytes ||
        payloadBytes == 0 || payloadBytes > maxBytes) return false;

    std::ifstream source(jsonPath, std::ios::binary | std::ios::ate);
    if (!source || source.tellg() != std::streamoff(sourceBytes)) return false;
    source.seekg(0);
    std::string current(sourceBytes, '\0');
    std::string recorded(sourceBytes, '\0');
    if (!source.read(&current[0], sourceBytes) ||
        !cache.read(&recorded[0], sourceBytes) || current != recorded) return false;

    std::vector<std::uint8_t> payload(payloadBytes);
    if (!cache.read(reinterpret_cast<char *>(payload.data()), payloadBytes) ||
        cache.peek() != std::char_traits<char>::eof()) return false;
    output.swap(payload);
    return true;
}

} // namespace f15assets

#endif
