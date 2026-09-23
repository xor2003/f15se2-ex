/* Characterization of runGenerator() (src/stgen.c) — the START-side mission
 * generator. The production path feeds it parsed .wld/.3dG/.3dT tables; here a
 * synthetic world is seeded directly into the same globals so the real
 * generator runs end-to-end without file IO:
 *
 *   - terrainGrid: all clear (no impassable cells).
 *   - Quadtree (gridBuf2..4 + terrainTilePtrs/Counts/Block): a uniform
 *     two-tile world — L2 cell residues 0..7 map to tile model 5, residues
 *     8..15 to tile model 9 — so findNearestTerrain snaps every placement to
 *     a 0x80-aligned cell centre and the two mission targets can resolve to
 *     distinct object types (required by the non-DS retry gate).
 *   - worldObjects: regular objects at non-cell-centre coords (eligible for
 *     occupant assignment), bases with targetFlags 0x601 spread over the map,
 *     and scratch slots 1/2 pre-seeded unitType=1 so a snapped empty cell is
 *     an acceptable secondary target for missionPick==0.
 *   - flightUnits: 8 units exercising WAYPOINTED / LONG_RANGE / TRACKED_SITE /
 *     plain branches.
 *
 * The produced mission state (targets, mission globals, comm loadout, placed
 * flight units, mutated world objects) is dumped as a stable text trace and
 * compared against the committed golden so any behavioural change in target
 * selection, snapping, distance/bearing accumulation or unit placement is
 * caught byte-for-byte.
 *
 * Scenarios 0-2 run the synthetic world across missionPick 0/-1/1. Scenarios
 * 3-4 re-seed every world table from the real SVN theater:
 * campaigns/SVN/SVN.WLD.json (objects, flight units, terrain grid, type
 * table, names) plus campaigns/SVN/VN/VN.3DG/.3DT.json (the real sparse
 * quadtree). Scenario 4 produces a complete real mission; scenario 3 pins
 * the degenerate path where the fixed pick-0 table coords miss all populated
 * terrain and targetIdx stays -1 — including runGenerator's unguarded
 * worldObjects[-1] read into missionTargetX (a genuine production quirk; an
 * ASAN build will flag it as a real finding, not a test bug).
 *
 *   mission_gen_tests                 compare against the committed golden
 *   mission_gen_tests record <file>   write a fresh trace
 *   mission_gen_tests dump            print the trace to stdout
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "inttype.h"
#include "struct.h"
#include "comm.h"
#include "stdata.h"
#include "strand.h"
#include "model_limits.h"

extern void runGenerator();

#ifndef F15_GOLDEN_DIR
#define F15_GOLDEN_DIR "goldens"
#endif
#ifndef F15_CAMPAIGN_DIR
#define F15_CAMPAIGN_DIR "../campaigns"
#endif

namespace {

void require(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "mission_gen_tests: %s\n", msg);
        std::exit(1);
    }
}

Game g_game;
GameComm g_comm;
char g_nameBuf[100][16];

void seedWorld() {
    memset(&g_game, 0, sizeof(g_game));
    memset(&g_comm, 0, sizeof(g_comm));
    g_game.theater = 1;
    g_game.difficulty = 2;
    g_game.isCampaignMission = 0;
    gameData = &g_game;
    commData = &g_comm;

    difficultySaved = 2;
    theaterSaved = 1;
    flag4Saved = 0;
    nightMissionFlag = 0;
    escortMissionFlag = 0;
    playerStartLoc = 15;
    missionDistAccum = 0;
    missionMidX = missionMidY = 0;
    missionTargetX = missionTargetY = 0;
    missionTarget2X = missionTarget2Y = 0;
    missionBase2X = missionBase2Y = 0;
    baseXPrecise = baseYPrecise = 0;

    /* Clear terrain — every random placement passes the impassable check. */
    memset(terrainGrid, 0, sizeof(terrainGrid));

    /* Uniform two-tile quadtree: every L3 cell -> block 0; L2 cell residues
     * 0..7 -> cell entry 0 (tile model 5), residues 8..15 -> entry 1 (tile
     * model 9). Level 1 mirrors its L2 parent's tile so the finer level's
     * closer hit reports the same model. */
    memset(gridBuf1, 0, 17);
    memset(gridBuf2, 0, 0x100);
    memset(gridBuf3, 0, TERRAIN_CHILD_GRID_BYTES);
    memset(gridBuf4, 0, TERRAIN_CHILD_GRID_BYTES);
    memset(gridBuf5, 0, TERRAIN_CHILD_GRID_BYTES);
    for (int i = 0; i < 16; ++i)
        gridBuf3[i] = i < 8 ? 0 : 1;
    for (int i = 0; i < 16; ++i) {
        gridBuf4[i] = 0;       /* block 0 -> entry 0 */
        gridBuf4[16 + i] = 1;  /* block 1 -> entry 1 */
    }
    memset(terrainTilePtrs, 0, 5 * sizeof(struct TerrainPtrTable));
    memset(terrainTileCounts, 0, 5 * sizeof(struct TerrainCountTable));
    memset(terrainTileBlock, 0, 64 * sizeof(struct TerrainTile));
    /* Level 1 and 2 entries 0/1 -> tile block entries. */
    terrainTilePtrs[1].entries[0] = &terrainTileBlock[0];
    terrainTilePtrs[1].entries[1] = &terrainTileBlock[1];
    terrainTileCounts[1].entries[0] = 1;
    terrainTileCounts[1].entries[1] = 1;
    terrainTilePtrs[2].entries[0] = &terrainTileBlock[0];
    terrainTilePtrs[2].entries[1] = &terrainTileBlock[1];
    terrainTileCounts[2].entries[0] = 1;
    terrainTileCounts[2].entries[1] = 1;
    terrainTileBlock[0].idx = 5;
    terrainTileBlock[1].idx = 9;

    /* Model table: model 5 -> tensionMask 1 group, model 9 -> tensionMask 2;
     * seeded-object model indices also match group 1 so any picked target can
     * receive a mission type. */
    for (int m = 0; m < BUF7SIZE; ++m)
        objectTypeTable[m] = 1;
    objectTypeTable[9] = 2;

    memset(targets, 0, 2 * sizeof(struct Target));
    targets[0].targetIdx = targets[1].targetIdx = -1;

    memset(worldObjects, 0, 0x4B * sizeof(struct WorldObject));
    for (int i = 0; i < 0x64; ++i) {
        std::snprintf(g_nameBuf[i], 16, "unit%02d", i);
        wldOffsets[i] = g_nameBuf[i];
    }
    /* Scratch slots the generator fills for unmatched snap points. unitType=1
     * makes a snapped empty cell an acceptable secondary for missionPick==0
     * (the production path leaves this at whatever parseWorld loaded). */
    worldObjects[1].unitType = 1;
    worldObjects[2].unitType = 1;

    /* Regular objects [3, worldObjectCount): off cell centres so the
     * findOrPlaceItem coord-match only ever triggers on seeded objects —
     * deliberately kept clear of the targetCoords snap points. */
    worldObjectCount = 12;
    for (uint16 i = 3; i < worldObjectCount; ++i) {
        worldObjects[i].x_coord = (uint16)(0x1400 + i * 0x531);
        worldObjects[i].y_coord = (uint16)(0x2200 + i * 0x451);
        worldObjects[i].targetFlags = 0x001;
        worldObjects[i].unitRef = 1;
        worldObjects[i].unitType = (int16)(i % 3 + 1);
        worldObjects[i].objectIdx = (int16)(20 + i);
        worldObjects[i].patrolCount = 0;
    }
    /* Bases [worldObjectCount, readItemSize): flags 0x601 (base + large),
     * spread over the whole map so at least one lands within 0x7000 of any
     * target; index 15 is the playerStartLoc base for TRACKED_SITE units. */
    readItemSize = 40;
    for (int i = (int)worldObjectCount; i < readItemSize; ++i) {
        int k = i - (int)worldObjectCount;
        worldObjects[i].x_coord = (uint16)(0x400 + (k % 7) * 0x1100 + (k / 7) * 0x300);
        worldObjects[i].y_coord = (uint16)(0x1000 + (k % 4) * 0x1400 + (k / 4) * 0x500);
        worldObjects[i].targetFlags = 0x601;
        worldObjects[i].unitRef = 1;
        worldObjects[i].objectIdx = (int16)(30 + i);
    }
    worldObjects[20].targetFlags |= 0x100; /* one airbase variant */

    groundUnitCount = 6;
    worldObjects[3].unitType = 1;
    worldObjects[4].unitType = 2;
    worldObjects[5].unitType = 3;

    memset(flightUnits, 0, 0x13 * sizeof(struct FlightUnit));
    flightUnitCount = 8;
    flightUnits[0].flags = SIMOBJ_WAYPOINTED;
    flightUnits[1].flags = SIMOBJ_WAYPOINTED | SIMOBJ_LONG_RANGE;
    flightUnits[2].flags = SIMOBJ_TRACKED_SITE;
    flightUnits[3].flags = 0;
    for (int i = 0; i < flightUnitCount; ++i)
        flightUnits[i].planeType = (int16)(i % 19);
}

/* ---- campaigns/SVN/SVN.WLD.json loading ----
 * The generated schema is flat: arrays of objects with int/string fields.
 * These helpers scan it directly — objects are extracted brace-balanced so
 * nested annotation blocks (e.g. scenario_real_world_anchor) can't confuse
 * the field lookup. */

std::string readTextFile(const char *path) {
    std::ifstream f(path, std::ios::binary);
    require(f.good(), "cannot open SVN.WLD.json — check F15_CAMPAIGN_DIR");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/* Byte offset just past `"key"`'s colon, or npos. */
size_t jsonFieldPos(const std::string &s, const char *key, size_t from = 0) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat, from);
    if (p == std::string::npos) return p;
    p = s.find(':', p + pat.size());
    if (p == std::string::npos) return p;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    return p;
}

long jsonInt(const std::string &s, const char *key, long dflt = 0) {
    size_t p = jsonFieldPos(s, key);
    if (p == std::string::npos) return dflt;
    return std::strtol(s.c_str() + p, nullptr, 10);
}

std::string jsonString(const std::string &s, const char *key) {
    size_t p = jsonFieldPos(s, key);
    if (p == std::string::npos || s[p] != '"') return "";
    ++p;
    std::string out;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) ++p;
        out += s[p++];
    }
    return out;
}

/* Every `{...}` block inside the named top-level array, brace-balanced and
 * string-aware. */
std::vector<std::string> jsonObjectArray(const std::string &json, const char *key) {
    std::vector<std::string> out;
    size_t p = jsonFieldPos(json, key);
    require(p != std::string::npos && json[p] == '[', "missing json array");
    ++p;
    while (p < json.size()) {
        while (p < json.size() && json[p] != '{' && json[p] != ']') ++p;
        if (p >= json.size() || json[p] == ']') break;
        size_t start = p, depth = 0;
        bool inStr = false;
        for (; p < json.size(); ++p) {
            char c = json[p];
            if (inStr) {
                if (c == '\\') ++p;
                else if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) { ++p; break; }
        }
        out.push_back(json.substr(start, p - start));
    }
    return out;
}

void decodeBase64(const std::string &in, unsigned char *out, size_t outSize) {
    static signed char tab[256];
    static bool init = false;
    if (!init) {
        memset(tab, -1, sizeof(tab));
        const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) tab[(unsigned char)A[i]] = (signed char)i;
        init = true;
    }
    int acc = 0, bits = 0;
    size_t n = 0;
    for (char ch : in) {
        signed char v = tab[(unsigned char)ch];
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < outSize) out[n++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    require(n == outSize, "base64 field size mismatch");
}

/* Flat int array (`"key": [1,2,...]`) into a byte/word buffer. */
void jsonIntArray(const std::string &json, const char *key, unsigned char *out, size_t count) {
    size_t p = jsonFieldPos(json, key);
    require(p != std::string::npos && json[p] == '[', "missing json int array");
    ++p;
    for (size_t i = 0; i < count; ++i) {
        while (p < json.size() && (json[p] == ' ' || json[p] == ',' || json[p] == '\n' || json[p] == '\r' || json[p] == '\t')) ++p;
        require(p < json.size() && json[p] != ']', "json int array too short");
        char *end = nullptr;
        long v = std::strtol(json.c_str() + p, &end, 10);
        require(end != json.c_str() + p, "json int array parse failure");
        p = (size_t)(end - json.c_str());
        out[i] = (unsigned char)v;
    }
}

std::string g_svnLabel[0x4B];

/* Real VN.3DG quadtree + VN.3DT tile tables: sparse real coverage — only six
 * level-1 cells carry placements (SAM Radar / Supply Dump classes), level-2
 * tiles are invisible to the type-table gate, so placements resolve only
 * near populated terrain, exactly like production SVN generation. */
void seedSvnQuadtree() {
    const std::string base = std::string(F15_CAMPAIGN_DIR) + "/SVN/VN/VN";
    const std::string gridJson = readTextFile((base + ".3DG.json").c_str());
    const std::string tileJson = readTextFile((base + ".3DT.json").c_str());

    memset(gridBuf1, 0, 17);
    memset(gridBuf2, 0, 0x100);
    memset(gridBuf3, 0, TERRAIN_CHILD_GRID_BYTES);
    memset(gridBuf4, 0, TERRAIN_CHILD_GRID_BYTES);
    memset(gridBuf5, 0, TERRAIN_CHILD_GRID_BYTES);
    jsonIntArray(gridJson, "level4_top_grid", gridBuf1, 16);
    jsonIntArray(gridJson, "level3_grid", gridBuf2, 0x100);
    jsonIntArray(gridJson, "level2_subgrid", gridBuf3, LEGACY_TERRAIN_CHILD_GRID_BYTES);
    jsonIntArray(gridJson, "level1_subgrid", gridBuf4, LEGACY_TERRAIN_CHILD_GRID_BYTES);
    jsonIntArray(gridJson, "level0_subgrid", gridBuf5, LEGACY_TERRAIN_CHILD_GRID_BYTES);

    memset(terrainTilePtrs, 0, 5 * sizeof(struct TerrainPtrTable));
    memset(terrainTileCounts, 0, 5 * sizeof(struct TerrainCountTable));
    memset(terrainTileBlock, 0, 512 * sizeof(struct TerrainTile));
    const auto levels = jsonObjectArray(tileJson, "levels");
    require(levels.size() == 5, "VN.3DT levels count mismatch");
    size_t blockUsed = 0;
    for (const auto &levelJson : levels) {
        const int level = (int)jsonInt(levelJson, "level");
        for (const auto &slot : jsonObjectArray(levelJson, "objects")) {
            const int tileIndex = (int)jsonInt(slot, "tile_index");
            const auto objs = jsonObjectArray(slot, "objects");
            if (objs.empty()) continue;
            require(tileIndex < (int)TERRAIN_TILE_PATTERN_CAPACITY, "tile_index out of range");
            require(blockUsed + objs.size() <= 512, "terrainTileBlock overflow");
            terrainTilePtrs[level].entries[tileIndex] = &terrainTileBlock[blockUsed];
            terrainTileCounts[level].entries[tileIndex] = (uint16)objs.size();
            for (const auto &o : objs) {
                terrainTileBlock[blockUsed].buf3 = (uint16)jsonInt(o, "x");
                terrainTileBlock[blockUsed].buf4 = (int16)jsonInt(o, "y");
                terrainTileBlock[blockUsed].buf5 = (int16)jsonInt(o, "z");
                terrainTileBlock[blockUsed].idx = (uint8)jsonInt(o, "shape_word");
                ++blockUsed;
            }
        }
    }
}

/* Seed every world table from the real SVN theater data: objects, airbases,
 * flight units, terrain grid, object-type table and names from SVN.WLD.json,
 * plus the real VN.3DG/.3DT quadtree. */
void seedWorldFromSvn() {
    const std::string path = std::string(F15_CAMPAIGN_DIR) + "/SVN/SVN.WLD.json";
    const std::string json = readTextFile(path.c_str());

    memset(&g_game, 0, sizeof(g_game));
    memset(&g_comm, 0, sizeof(g_comm));
    g_game.theater = 2; /* vn.wld */
    g_game.difficulty = 2;
    g_game.isCampaignMission = 0;
    gameData = &g_game;
    commData = &g_comm;

    difficultySaved = 2;
    theaterSaved = 2;
    flag4Saved = 0;
    nightMissionFlag = 0;
    escortMissionFlag = 0;
    playerStartLoc = 27; /* first real airbase object */
    missionDistAccum = 0;
    missionMidX = missionMidY = 0;
    missionTargetX = missionTargetY = 0;
    missionTarget2X = missionTarget2Y = 0;
    missionBase2X = missionBase2Y = 0;
    baseXPrecise = baseYPrecise = 0;

    decodeBase64(jsonString(json, "terrain_grid"), (unsigned char *)terrainGrid, sizeof(terrainGrid));
    decodeBase64(jsonString(json, "mission_object_type_table"), (unsigned char *)objectTypeTable, BUF7SIZE);

    seedSvnQuadtree();

    memset(targets, 0, 2 * sizeof(struct Target));
    targets[0].targetIdx = targets[1].targetIdx = -1;

    memset(worldObjects, 0, 0x4B * sizeof(struct WorldObject));
    readItemSize = (int)jsonInt(json, "read_item_size");
    worldObjectCount = (uint16)jsonInt(json, "world_object_count");
    groundUnitCount = (int)jsonInt(json, "ground_unit_count");
    require(readItemSize <= 0x4B, "SVN read_item_size exceeds table");
    const auto objs = jsonObjectArray(json, "world_objects");
    require((int)objs.size() == readItemSize, "SVN world_objects count mismatch");
    for (int i = 0; i < readItemSize; ++i) {
        worldObjects[i].unitRef = (uint16)jsonInt(objs[i], "unitRef");
        worldObjects[i].unitType = (int16)jsonInt(objs[i], "unitType");
        worldObjects[i].objectIdx = (int16)jsonInt(objs[i], "objectIdx");
        worldObjects[i].x_coord = (uint16)jsonInt(objs[i], "x_coord");
        worldObjects[i].y_coord = (uint16)jsonInt(objs[i], "y_coord");
        worldObjects[i].targetFlags = (int16)jsonInt(objs[i], "targetFlags");
        worldObjects[i].occupantType = (int16)jsonInt(objs[i], "occupantType");
        worldObjects[i].patrolCount = (int16)jsonInt(objs[i], "patrolCount");
        g_svnLabel[i] = jsonString(objs[i], "legacy_label");
        if (g_svnLabel[i].empty()) g_svnLabel[i] = "unnamed";
        wldOffsets[i] = g_svnLabel[i].data();
    }
    for (int i = readItemSize; i < 0x64; ++i)
        wldOffsets[i] = (char *)"unused";

    memset(flightUnits, 0, 0x13 * sizeof(struct FlightUnit));
    flightUnitCount = (int)jsonInt(json, "flight_unit_count");
    require(flightUnitCount <= 0x13, "SVN flight_unit_count exceeds table");
    const auto fus = jsonObjectArray(json, "flight_units");
    require((int)fus.size() == flightUnitCount, "SVN flight_units count mismatch");
    for (int i = 0; i < flightUnitCount; ++i) {
        flightUnits[i].waypointIdx = (int16)jsonInt(fus[i], "waypointIdx");
        flightUnits[i].x = (uint16)jsonInt(fus[i], "x");
        flightUnits[i].y = (uint16)jsonInt(fus[i], "y");
        flightUnits[i].altitude = (uint16)jsonInt(fus[i], "altitude");
        flightUnits[i].heading = (int16)jsonInt(fus[i], "heading");
        flightUnits[i].pitch = (int16)jsonInt(fus[i], "pitch");
        flightUnits[i].roll = (int16)jsonInt(fus[i], "roll");
        flightUnits[i].planeType = (int16)jsonInt(fus[i], "planeType");
        flightUnits[i].flags = (int16)jsonInt(fus[i], "flags");
        flightUnits[i].maxSpeed = (int16)jsonInt(fus[i], "maxSpeed");
        flightUnits[i].fuel = (uint16)jsonInt(fus[i], "fuel");
    }
}

void dumpWorld(std::ostream &out, int scenario, int pick, uint32 seed) {
    out << "scenario " << scenario << " missionPick=" << pick << " seed=" << seed << "\n";
    for (int t = 0; t < 2; ++t) {
        const Target &tg = targets[t];
        char coord[7];
        memcpy(coord, tg.coord, 6);
        coord[6] = 0;
        out << "target" << t << " missionType=" << tg.missionType
            << " targetIdx=" << tg.targetIdx << " baseIdx=" << tg.baseIdx
            << " missionCode=" << tg.missionCode << " missionNum=" << tg.missionNum
            << " coord=\"" << coord << "\" distance=" << tg.distance << "\n";
    }
    out << "baseXPrecise=" << baseXPrecise << " baseYPrecise=" << baseYPrecise
        << " missionMidX=" << missionMidX << " missionMidY=" << missionMidY
        << " missionTargetX=" << missionTargetX << " missionTargetY=" << missionTargetY
        << " missionTarget2X=" << missionTarget2X << " missionTarget2Y=" << missionTarget2Y
        << " missionBase2X=" << missionBase2X << " missionBase2Y=" << missionBase2Y
        << " missionDistAccum=" << missionDistAccum
        << " nightMissionFlag=" << nightMissionFlag << "\n";
    out << "comm weaponType=" << g_comm.weaponType[0] << ","
        << g_comm.weaponType[1] << "," << g_comm.weaponType[2]
        << " weaponCount=" << g_comm.weaponCount[0] << ","
        << g_comm.weaponCount[1] << "," << g_comm.weaponCount[2] << "\n";
    for (int i = 0; i < flightUnitCount; ++i) {
        const FlightUnit &u = flightUnits[i];
        out << "unit" << i << " x=" << u.x << " y=" << u.y << " alt=" << u.altitude
            << " hdg=" << u.heading << " fuel=" << u.fuel << " spd=" << u.maxSpeed
            << " wp=" << u.waypointIdx << " flags=0x" << std::hex << u.flags << std::dec << "\n";
    }
    /* Scratch targets + occupant-assigned objects are the observable world
     * mutations. */
    for (int i = 1; i <= 2; ++i) {
        const WorldObject &o = worldObjects[i];
        out << "obj" << i << " x=" << o.x_coord << " y=" << o.y_coord
            << " obj=0x" << std::hex << o.objectIdx << std::dec
            << " flags=0x" << std::hex << o.targetFlags << std::dec
            << " unitType=" << o.unitType << " occ=" << o.occupantType
            << " patrol=" << o.patrolCount << " unitRef=" << o.unitRef << "\n";
    }
    int occupants = 0;
    for (int i = 3; i < readItemSize; ++i)
        if (worldObjects[i].occupantType || worldObjects[i].patrolCount)
            ++occupants;
    out << "occupants=" << occupants << "\n";
}

std::string buildTrace(uint32 seed) {
    std::ostringstream out;
    out << "mission_gen_trace 1\n";
    /* missionPick 0/1 exercise two targetCoords tables; -1 the fully random
     * placement path. */
    static const int picks[] = {0, -1, 1};
    for (int scenario = 0; scenario < 3; ++scenario) {
        int pick = picks[scenario];
        uint32 s = seed + (uint32)scenario * 977;
        seedWorld();
        missionPick = pick;
        gameSrand(s);
        runGenerator();
        dumpWorld(out, scenario, pick, s);
    }
    /* Same two paths on the real SVN theater tables. */
    static const int svnPicks[] = {0, -1};
    for (int scenario = 0; scenario < 2; ++scenario) {
        int pick = svnPicks[scenario];
        uint32 s = seed + 7919 + (uint32)scenario * 977;
        seedWorldFromSvn();
        missionPick = pick;
        gameSrand(s);
        runGenerator();
        dumpWorld(out, 3 + scenario, pick, s);
    }
    return out.str();
}

} // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && std::strcmp(argv[1], "record") == 0) {
        require(argc >= 3, "record requires an output path");
        std::ofstream f(argv[2], std::ios::binary | std::ios::trunc);
        require(f.good(), "cannot open output path");
        f << buildTrace(7);
        f.close();
        std::fprintf(stderr, "recorded mission generation trace to %s\n", argv[2]);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "dump") == 0) {
        std::fputs(buildTrace(7).c_str(), stdout);
        return 0;
    }
    std::ifstream golden(F15_GOLDEN_DIR "/mission_gen.trace", std::ios::binary);
    require(golden.good(), "cannot open golden trace (record it first)");
    std::string expected((std::istreambuf_iterator<char>(golden)),
                         std::istreambuf_iterator<char>());
    std::string actual = buildTrace(7);
    if (actual != expected) {
        std::fprintf(stderr, "mission_gen_tests: golden mismatch\n--- expected ---\n%s--- actual ---\n%s\n",
                     expected.c_str(), actual.c_str());
        return 1;
    }
    std::fprintf(stderr, "mission_gen_tests: golden match (%zu bytes)\n", actual.size());
    return 0;
}
