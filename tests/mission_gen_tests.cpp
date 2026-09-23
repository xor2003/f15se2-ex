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
