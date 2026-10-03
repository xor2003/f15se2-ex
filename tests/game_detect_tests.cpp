/* game_detect_tests.cpp - multi-game descriptor registry behavior:
 * asset-dir detection (signatures), ambiguity rejection, forced --game-id,
 * filename aliasing and the per-game accessors the engine reads.
 *
 * Fixtures are empty files: detection keys on file names only (manifest md5s
 * are exercised separately by verifyGameAssets at startup).
 */
#include "game/game.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

fs::path makeFixtureDir(const fs::path &root, const char *name) {
    fs::path dir = root / name;
    fs::create_directories(dir);
    return dir;
}

void touch(const fs::path &dir, const char *name) {
    std::ofstream out(dir / name, std::ios::binary);
    out.put('\0');
}

const char *requireSelect(const fs::path &dir, const char *forcedId = nullptr) {
    char err[512];
    const GameDesc *desc = gameSelectForDir(dir.string().c_str(), forcedId, err, sizeof(err));
    if (!desc) {
        std::cerr << "failed: gameSelectForDir rejected " << dir << ": " << err << '\n';
        std::exit(1);
    }
    return desc->cliId;
}

void requireReject(const fs::path &dir, const char *forcedId, const char *expectInMsg) {
    char err[512];
    const GameDesc *desc = gameSelectForDir(dir.string().c_str(), forcedId, err, sizeof(err));
    require(desc == nullptr, "gameSelectForDir unexpectedly accepted the directory");
    if (expectInMsg) {
        require(std::strstr(err, expectInMsg) != nullptr, "error text missing expected detail");
    }
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "f15se2_game_detect_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    /* --- detection matrix ------------------------------------------------ */
    const fs::path empty = makeFixtureDir(root, "empty");
    const fs::path f15 = makeFixtureDir(root, "f15");
    const fs::path f19 = makeFixtureDir(root, "f19");
    const fs::path mixed = makeFixtureDir(root, "mixed");

    touch(f15, "F15.SPR");
    touch(f15, "15FLT.3D3");
    touch(f19, "F19.SPR");
    touch(f19, "STFLT.3D3");
    for (const char *n : {"F15.SPR", "15FLT.3D3", "F19.SPR", "STFLT.3D3"}) touch(mixed, n);

    {
        GameId ids[GAMEID_COUNT];
        require(gameDetectInDir(empty.string().c_str(), ids, GAMEID_COUNT) == 0,
                "empty dir detected a game");
        require(gameDetectInDir(f15.string().c_str(), ids, GAMEID_COUNT) == 1 && ids[0] == GAMEID_F15SE2,
                "F-15 dir not detected as f15se2");
        require(gameDetectInDir(f19.string().c_str(), ids, GAMEID_COUNT) == 1 && ids[0] == GAMEID_F19,
                "F-19 dir not detected as f19");
        require(gameDetectInDir(mixed.string().c_str(), ids, GAMEID_COUNT) == 2,
                "mixed dir did not detect both games");

        /* Detection is case-insensitive: DOS media ships UPPERCASE but a
         * renamed install must still work. */
        const fs::path f19lower = makeFixtureDir(root, "f19lower");
        touch(f19lower, "f19.spr");
        touch(f19lower, "stflt.3d3");
        require(gameDetectInDir(f19lower.string().c_str(), ids, GAMEID_COUNT) == 1 && ids[0] == GAMEID_F19,
                "lowercase F-19 files not detected");
    }

    /* --- selection -------------------------------------------------------- */
    requireReject(empty, nullptr, "No supported game assets");
    require(std::strcmp(requireSelect(f15), "f15") == 0, "F-15 dir selected wrong game");
    require(gameActive() == gameById("f15"), "F-15 selection did not activate the descriptor");
    require(std::strcmp(requireSelect(f19), "f19") == 0, "F-19 dir selected wrong game");
    requireReject(mixed, nullptr, "--game-id");           /* ambiguity must fail, never silently pick */
    requireReject(mixed, nullptr, "F-19 Stealth Fighter");
    require(std::strcmp(requireSelect(mixed, "f19"), "f19") == 0, "forced f19 rejected on mixed dir");
    require(std::strcmp(requireSelect(mixed, "F19"), "f19") == 0, "game-id match not case-insensitive");
    require(std::strcmp(requireSelect(f15, "f15"), "f15") == 0, "forced f15 on its own dir rejected");
    requireReject(f15, "f19", "does not contain");        /* forced game missing signatures */
    requireReject(f15, "f117a", "Unknown game id");       /* unknown id reports the supported list */

    /* --- aliases: F-19 active -------------------------------------------- */
    require(std::strcmp(requireSelect(f19), "f19") == 0, "F-19 re-selection failed");
    require(std::strcmp(gameFileName("f15.spr"), "f19.spr") == 0, "f15.spr did not alias to f19.spr");
    require(std::strcmp(gameFileName("WALL.PIC"), "clip.pic") == 0, "alias lookup is not case-insensitive");
    require(std::strcmp(gameFileName("15FLT.3D3"), "stflt.3d3") == 0, "15flt.3d3 did not alias to stflt.3d3");
    require(std::strcmp(gameFileName("photo.3d3"), "photo.3d3") == 0, "unaliased name changed");
    require(std::strcmp(gameBundledRoot(), "assets/f19") == 0, "F-19 bundled root wrong");
    require(std::strcmp(gameDiskLabel(), "F19") == 0, "F-19 disk label wrong");
    require(gameHasHiResTitle() == 0, "F-19 must not take the 640x350 title path");

    /* F-19 slots: menu NC/CE map to engine slots 4/5 so the F-15 file tables
     * keep working; unlisted slots fall back to a real file (never NULL). */
    require(std::strcmp(gameWorldFile(4), "nc.wld") == 0, "F-19 slot 4 world wrong");
    require(std::strcmp(gamePlhFile(5), "ce.xxx") == 0, "F-19 slot 5 plh wrong");
    require(std::strcmp(gameTheaterSprFile(7), "ceurope.spr") == 0, "F-19 stray slot spr wrong");
    require(gameTheaterRowForSlot(4) == 2, "F-19 NC not on theater-menu row 2");
    require(gameTheaterRowForSlot(3) == 0, "F-19 unlisted slot should land on row 0");
    require(gameTheaterMenuCount() == 5 && gameTheaterMenu()[4].label == nullptr,
            "F-19 must hide the scenario submenu row");
    require(gameScenarioMenu() == nullptr, "F-19 ships no scenario disk");
    require(gameCampaignNextTheater(0) == 1, "F-19 campaign step 0->1 wrong");
    require(gameCampaignNextTheater(1) == 4, "F-19 campaign step 1->4 wrong");
    require(gameCampaignNextTheater(5) == -1, "F-19 campaign wrap wrong");
    require(gameCampaignNextTheater(3) == 3, "F-19 non-campaign slot must not advance");

    /* --- aliases: F-15 active (identity) ---------------------------------- */
    require(std::strcmp(requireSelect(f15), "f15") == 0, "F-15 re-selection failed");
    require(std::strcmp(gameFileName("wall.pic"), "wall.pic") == 0, "F-15 names must pass through");
    require(std::strcmp(gameBundledRoot(), "assets") == 0, "F-15 bundled root wrong");
    require(gameHasHiResTitle() != 0, "F-15 has the 640x350 title");
    require(gameTheaterRowForSlot(5) == 4, "F-15 scenario slot must land on Other Areas row");
    require(gameTheaterRowForSlot(2) == 2, "F-15 VN not on theater-menu row 2");
    const GameMenuEntry *scen = gameScenarioMenu();
    require(scen && gameScenarioMenuCount() == 5, "F-15 scenario menu missing");
    require(scen[0].slot == 4 && std::strcmp(scen[0].probeStem, "nc") == 0,
            "F-15 NC scenario probe wrong");
    require(scen[4].slot == GAME_MENU_BACK, "F-15 scenario menu lacks the back row");
    require(gameCampaignNextTheater(2) == 3, "F-15 campaign step 2->3 wrong");
    require(gameCampaignNextTheater(3) == -1, "F-15 campaign wrap wrong");
    require(gameCampaignNextTheater(7) == 7, "F-15 non-campaign slot must not advance");

    fs::remove_all(root);
    std::cout << "game_detect_tests: all passed\n";
    return 0;
}
