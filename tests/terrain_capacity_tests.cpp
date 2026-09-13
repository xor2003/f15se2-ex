#include "inttype.h"
#include "struct.h"
#include "egdata.h"
#include "stdata.h"
#include "const.h"

#include <filesystem>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <fstream>
#include <iostream>

extern bool setGamePath(const char *);
extern void load3DT(char *);
extern void parseTerrain(char *);
extern void parseGrid();
extern void load3DG();

int main(int argc, char **argv) {
    if (argc > 2) {
        std::cerr << "Usage: terrain_capacity [EMPTY_TEST_DIRECTORY]\n";
        return 2;
    }
    const bool temporary = argc == 1;
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = temporary
        ? std::filesystem::temp_directory_path() / ("f15-terrain-capacity-" + std::to_string(timestamp))
        : std::filesystem::path(argv[1]);
    if (temporary && !std::filesystem::create_directory(directory)) return 2;
    if (!temporary) std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        bool enabled;
        ~Cleanup() {
            if (enabled) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{directory, temporary};
    if (!std::filesystem::is_empty(directory)) {
        std::cerr << "Test directory must be empty\n";
        return 2;
    }
    if (!setGamePath(directory.string().c_str())) return 3;

    const int maximumPlacements = MAX_TILE_DATA / sizeof(TileSceneObject);
    for (const int placementCount : {5000, maximumPlacements}) {
        const int tileIndex = placementCount == maximumPlacements
            ? TERRAIN_TILE_PATTERN_CAPACITY - 1 : 0;
        std::ofstream file(directory / "capacity.3dt", std::ios::binary);
        auto writeWord = [&](unsigned value) {
            file.put(value & 255);
            file.put((value >> 8) & 255);
        };
        writeWord(SIGNATURE_3DT);
        for (int lod = 0; lod < 5; ++lod) writeWord(lod == 0 ? tileIndex + 1 : 0);
        for (int tile = 0; tile <= tileIndex; ++tile)
            writeWord(tile == tileIndex ? placementCount : 0);
        for (int index = 0; index < placementCount; ++index) {
            writeWord(index);
            writeWord(123);
            writeWord(456);
            writeWord(80);
        }
        file.close();
        if (!file) return 4;

        char name[] = "capacity.3dt";
        load3DT(name);
        parseTerrain(name);
        if (matrix3dt[0][tileIndex] != placementCount) return 5;
        if (terrainTileCounts[0].entries[tileIndex] != placementCount) return 5;
        for (int index = 0; index < placementCount; ++index) {
            const auto &object = matrix3dt_2[0][tileIndex][index];
            const auto &missionObject = terrainTilePtrs[0].entries[tileIndex][index];
            if (missionObject.buf3 != index || missionObject.buf4 != 123 ||
                missionObject.buf5 != 456 || missionObject.idx != 80) return 7;
            if (object.x != index || object.y != 123 || object.z != 456 ||
                object.shape != 80) {
                std::cerr << "Incorrect placement " << index << '\n';
                return 6;
            }
        }
        std::cout << "PASS: tile " << tileIndex << ", " << placementCount << " placements, "
                  << placementCount * sizeof(TileSceneObject) << " decoded bytes\n";
    }
    for (const int childBytes : {TERRAIN_CHILD_GRID_BYTES, LEGACY_TERRAIN_CHILD_GRID_BYTES}) {
        std::ofstream file(directory / "zz.3dg", std::ios::binary);
        const unsigned signature = childBytes == TERRAIN_CHILD_GRID_BYTES
            ? EXTENDED_TERRAIN_GRID_SIGNATURE : SIGNATURE_3DG;
        file.put(signature & 255);
        file.put(signature >> 8);
        for (int index = 0; index < 16 + 256; ++index) file.put(0);
        for (int level = 0; level < 3; ++level)
            for (int index = 0; index < childBytes; ++index)
                file.put((index + level + 1) % 256);
        file.close();
        if (!file) return 8;
        char gridName[] = "zz.3dg";
        regnPlhPtr = gridName;
        parseGrid();
        std::remove_reference_t<decltype(*gameData)> gameState{};
        gameData = &gameState;
        std::strcpy(regnStr, gridName);
        load3DG();
        const uint8 *tables[] = {gridBuf3, gridBuf4, gridBuf5};
        const uint8 *flightTables[] = {buf2_3dg, buf3_3dg, buf4_3dg};
        for (int level = 0; level < 3; ++level) {
            for (int index = 0; index < TERRAIN_CHILD_GRID_BYTES; ++index) {
                const int expected = index < childBytes ? (index + level + 1) % 256 : 0;
                if (tables[level][index] != expected || flightTables[level][index] != expected) {
                    std::cerr << "Incorrect grid entry " << level << ':' << index << '\n';
                    return 9;
                }
            }
        }
        std::cout << "PASS: mission and flight grids, " << childBytes << " bytes per child table\n";
    }
}
