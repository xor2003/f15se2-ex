#include "shared/common.h"
#include "r3d_gl.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <utility>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

/* file_io helpers not surfaced through common.h. */
extern int fileReadRaw(SDL_IOStream *io, void *dst, int count);
extern void errorAndExit(const char *msg);
extern bool setGamePath(const char *path);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, or sentinel resets.
enum FileIoOriginalConstant : int {
    kDosReadMode = 0,
    kDosDefaultCreateAttr = 0,
    kReadWholeRemainingStream = -1,
    kNullStreamError = -1,
    kZeroSizedTransferResult = 0,
    kUpperDatItemSize = 2,
    kUpperDatItemCount = 2,
    kUpperDatBytesRead = 4,
    kUpperDatRemainingBytes = 2,
    kReplacementItemSize = 1,
    kReplacementItemCount = 2,
    kRawItemSize = 1,
    kRawItemCount = 3,
    kNullTransferItemSize = 0,
    kNullTransferItemCount = 5,
    kSingleByte = 1,
    kErrorExitStatus = 1,
    kScratchBufferBytes = 8,
    kReplacementPathBufferBytes = 512,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

std::string readWholeFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeBinaryFile(const std::filesystem::path &path, const std::string &bytes) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void putLe32(std::string &out, unsigned value) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}

void putLe16(std::string &out, unsigned value) {
    for (int i = 0; i < 2; i++) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}

void putF32(std::string &out, float value) {
    static_assert(sizeof(float) == 4, "GLMESH fixture assumes IEEE float32");
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&value);
    out.append(reinterpret_cast<const char *>(p), 4);
}

std::string minimalGlmeshFixture() {
    std::string out;
    out.append("F15GLM3", 7);
    out.push_back('\0');
    out.append(32, '0'); // source GLB MD5 placeholder; standalone cache path does not require matching a GLB.
    putLe32(out, 1);     // primitive count
    putLe32(out, 1);     // GL_LINES
    putLe32(out, 2);     // two vertices
    putLe32(out, 2);     // source_kind=line
    putLe32(out, 0);     // source_index
    putLe32(out, 13);    // source_color
    putLe32(out, 1);     // source_order_sensitive
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    return out;
}

void appendGlmeshPrimitive(std::string &out, unsigned mode, unsigned nVerts,
                           unsigned sourceKind, unsigned sourceIndex,
                           unsigned sourceColor) {
    putLe32(out, mode);
    putLe32(out, nVerts);
    putLe32(out, sourceKind);
    putLe32(out, sourceIndex);
    putLe32(out, sourceColor);
    putLe32(out, 1); // source_order_sensitive; required for strict test-time proof.
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
}

std::string compareableGlmeshFixture() {
    std::string out;
    out.append("F15GLM3", 7);
    out.push_back('\0');
    out.append(32, '0');
    putLe32(out, 2); // one face primitive plus one line primitive

    appendGlmeshPrimitive(out, 4, 3, 1, 0, 12);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
    putF32(out, 0.0f);

    appendGlmeshPrimitive(out, 1, 2, 2, 0, 13);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    putF32(out, 1.0f);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    return out;
}

std::string compareableLegacyModelFixture() {
    std::string out;
    out.push_back('\0'); // render mode
    out.push_back('\0'); // MODEL opcode: zero normals, no sort flag
    out.push_back('\3'); // three inline vertices
    for (const auto &[x, y, z] : {std::tuple<int, int, int>{0, 0, 0},
                                  std::tuple<int, int, int>{1, 0, 0},
                                  std::tuple<int, int, int>{0, 1, 0}}) {
        putLe16(out, 0); // visibility mask
        putLe16(out, static_cast<unsigned>(x));
        putLe16(out, static_cast<unsigned>(y));
        putLe16(out, static_cast<unsigned>(z));
    }
    out.push_back('\3'); // edges
    for (const auto &[a, b] : {std::pair<int, int>{0, 1},
                               std::pair<int, int>{1, 2},
                               std::pair<int, int>{2, 0}}) {
        putLe16(out, 0);
        out.push_back(static_cast<char>(a));
        out.push_back(static_cast<char>(b));
    }
    out.push_back('\2'); // primitive commands
    out.push_back('\1'); // face opcode
    out.push_back('\3'); // edge count
    out.push_back('\0');
    out.push_back('\1');
    out.push_back('\2');
    out.push_back('\14'); // raw face color 12
    out.push_back('\0');  // line opcode
    putLe16(out, 0);
    out.push_back('\0');  // edge index
    out.push_back('\15'); // raw line color 13
    return out;
}

} // namespace

int main() {
    const auto oldCwd = std::filesystem::current_path();
    const auto testDir = std::filesystem::temp_directory_path() / "f15se2-ex-file-io-tests";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);
    std::filesystem::current_path(testDir);

    {
        std::ofstream out("UPPER.DAT", std::ios::binary);
        out << "abcdef";
    }

    // openFile resolves DOS-style uppercase asset names case-insensitively.
    SDL_IOStream *input = openFile("upper.dat", kDosReadMode);
    require(input != nullptr, "openFile resolves DOS-style uppercase asset names");
    char buf[kScratchBufferBytes] = {};
    require(fileRead(buf, kUpperDatItemSize, kUpperDatItemCount, input) == kUpperDatItemCount,
            "fileRead returns whole item count");
    require(std::string(buf, kUpperDatBytesRead) == "abcd", "fileRead transfers requested bytes");
    require(fileReadRaw(input, buf, kReadWholeRemainingStream) == kUpperDatRemainingBytes,
            "fileReadRaw negative count reads remaining stream bytes");
    fileClose(input);

    // createFile resolves the existing spelling and truncates it for writing.
    SDL_IOStream *output = createFile("upper.dat", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile resolves existing path case-insensitively");
    const char replacement[] = "XY";
    require(fileWrite(replacement, kReplacementItemSize, kReplacementItemCount, output) == kReplacementItemCount,
            "fileWrite returns whole item count");
    fileClose(output);
    require(readWholeFile(testDir / "UPPER.DAT") == "XY",
            "createFile overwrites the existing uppercase spelling");

    // createFile creates new files; fileWrite persists a host buffer.
    output = createFile("newfile.bin", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile creates new files");
    char raw[] = {'1', '2', '3'};
    require((int)fileWrite(raw, kRawItemSize, kRawItemCount, output) == kRawItemCount,
            "fileWrite writes host buffers");
    fileClose(output);
    require(readWholeFile(testDir / "newfile.bin") == "123", "fileWrite persisted bytes");

    const auto replacementRoot = testDir / "replacement_pack";
    const auto convertedRoot = replacementRoot / "converted_assets_all";
    std::filesystem::create_directories(convertedRoot);
#if !defined(_WIN32)
    setenv("F15_REPLACEMENT_ROOT", replacementRoot.string().c_str(), 1);
#endif

    writeBinaryFile(convertedRoot / "TITLE.png", "PNG");
    writeBinaryFile(convertedRoot / "GLOBAL_ONLY.png", "GLOBAL_PNG");
    writeBinaryFile(convertedRoot / "fonts" / "font_3.bdf", "BDF");
    writeBinaryFile(convertedRoot / "fonts" / "font_1.ttf", "GLOBAL_TTF");
    writeBinaryFile(convertedRoot / "sounds" / "voice_cue_000_sample0.wav", "WAV");
    writeBinaryFile(convertedRoot / "VN" / "shape_049_SAM_Radar.glb", "GLB");
    writeBinaryFile(convertedRoot / "VN" / "cache" / "shape_050.glmesh", minimalGlmeshFixture());

    char replacementPath[kReplacementPathBufferBytes] = {};
    require(findReplacementAssetPath("title.pic", ".png", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "TITLE.png",
            "findReplacementAssetPath resolves PNG replacements from converted_assets_all");
    require(findReplacementAssetPath("font_3", ".bdf", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "font_3.bdf",
            "findReplacementAssetPath resolves BDF font replacements");
    require(findReplacementAssetPath("font_1", ".ttf", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "font_1.ttf",
            "findReplacementAssetPath resolves TTF font replacements");
    require(findReplacementAssetPath("voice_cue_000_sample0", ".wav", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "voice_cue_000_sample0.wav",
            "findReplacementAssetPath resolves separate WAV cue replacements");
    require(findReplacementShapeModelPath("vn.3d3", 49, ".glb", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "shape_049_SAM_Radar.glb",
            "findReplacementShapeModelPath resolves labelled per-shape GLB replacements");
    require(findReplacementShapeModelPath("vn.3d3", 50, ".glmesh", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "shape_050.glmesh",
            "findReplacementShapeModelPath resolves generated cache GLMESH replacements");

    writeBinaryFile(testDir / "unrelated_tree" / "UNRELATED.png", "NOT_A_REPLACEMENT");
    require(!findReplacementAssetPath("UNRELATED.PIC", ".png", replacementPath, sizeof(replacementPath)),
            "findReplacementAssetPath does not recursively scan arbitrary game/current directories");

    int primitiveCount = 0;
    require(r3dgl_testLoadReplacementMesh("vn.3d3", 50, &primitiveCount) &&
                primitiveCount == 1,
            "GL backend runtime loader parses standalone GLMESH replacement cache");

    writeBinaryFile(convertedRoot / "VN" / "cache" / "shape_052.glmesh", compareableGlmeshFixture());
    const std::string legacyModel = compareableLegacyModelFixture();
    require(r3dgl_testCompareReplacementMesh(
                "vn.3d3",
                52,
                reinterpret_cast<const unsigned char *>(legacyModel.data()),
                legacyModel.size()),
            "GL backend test seam compares replacement GLMESH against decoded legacy 3D shape");

    for (int shape = 100; shape < 230; shape++) {
        writeBinaryFile(convertedRoot / "VN" / "cache" /
                            ("shape_" + std::to_string(shape) + ".glmesh"),
                        minimalGlmeshFixture());
        primitiveCount = 0;
        require(r3dgl_testLoadReplacementMesh("vn.3d3", shape, &primitiveCount) &&
                    primitiveCount == 1,
                "GL backend replacement mesh cache grows past the old fixed-size limit");
    }
#if !defined(_WIN32)
    const auto fakeTool = testDir / "fake_asset_tool.py";
    {
        std::ofstream tool(fakeTool);
        tool << "#!/usr/bin/env python3\n"
                "import hashlib\n"
                "import struct\n"
                "import sys\n"
                "def put32(v):\n"
                "    return struct.pack('<I', v)\n"
                "def putf(v):\n"
                "    return struct.pack('<f', v)\n"
                "if len(sys.argv) > 1 and sys.argv[1] == 'build-glmesh':\n"
                "    with open(sys.argv[2], 'rb') as f:\n"
                "        source_md5 = hashlib.md5(f.read()).hexdigest().encode('ascii')\n"
                "    # Emit the smallest renderable F15GLM3 cache. The runtime\n"
                "    # uses this generated cache as an implementation detail for\n"
                "    # editable GLB replacements, so tests should cover this path\n"
                "    # separately from direct standalone .glmesh loading.\n"
                "    out = bytearray(b'F15GLM3\\0')\n"
                "    out.extend(source_md5)\n"
                "    out.extend(put32(1))\n"
                "    out.extend(put32(1))\n"
                "    out.extend(put32(2))\n"
                "    out.extend(put32(2))\n"
                "    out.extend(put32(0))\n"
                "    out.extend(put32(13))\n"
                "    out.extend(put32(1))\n"
                "    out.extend(putf(1.0) + putf(0.0) + putf(0.0) + putf(1.0))\n"
                "    out.extend(putf(0.0) + putf(0.0) + putf(0.0))\n"
                "    out.extend(putf(1.0) + putf(0.0) + putf(0.0))\n"
                "    sys.stdout.buffer.write(out)\n"
                "else:\n"
                "    sys.stdout.buffer.write(b'JSONBRIDGE')\n";
    }
    std::filesystem::permissions(
        fakeTool,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add);
    setenv("F15_ASSET_TOOL", fakeTool.string().c_str(), 1);

    writeBinaryFile(convertedRoot / "VN" / "shape_051_Custom.glb", "GLB_SOURCE");
    primitiveCount = 0;
    require(r3dgl_testLoadReplacementMesh("vn.3d3", 51, &primitiveCount) &&
                primitiveCount == 1 &&
                std::filesystem::exists(convertedRoot / "VN" / "cache" / "shape_051_Custom.glmesh"),
            "GL backend runtime loader builds and caches GLMESH from editable GLB replacement");

    writeBinaryFile("LIBYA.WLD", "LEGACYWLD");
    writeBinaryFile(convertedRoot / "LIBYA" / "LIBYA.WLD.json", "{\"format\":\"WLD\"}\n");
    input = openFile("Libya.wld", kDosReadMode);
    require(input != nullptr, "openFile opens structured JSON replacement stream");
    char jsonBridgeBuf[16] = {};
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "openFile reads rebuilt structured JSON replacement bytes");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "openFile prefers WLD JSON replacement over legacy file");
    fileClose(input);

    writeBinaryFile("VN.WLD", "LEGACYVN");
    writeBinaryFile("CE.WLD", "LEGACYCE");
    writeBinaryFile(convertedRoot / "SVN" / "TITLE.png", "CAMPAIGN_PNG");
    writeBinaryFile(convertedRoot / "SVN" / "TITLE640.png", "CAMPAIGN_TITLE640_PNG");
    writeBinaryFile(convertedRoot / "SVN" / "DESK.png", "CAMPAIGN_DESK_PNG");
    writeBinaryFile(convertedRoot / "SVN" / "SVN.WLD.json", "{\"format\":\"WLD\",\"campaign\":{\"id\":\"SVN\"}}\n");
    writeBinaryFile(convertedRoot / "SVN" / "fonts" / "font_1.ttf", "CAMPAIGN_TTF");
    writeBinaryFile(convertedRoot / "SVN" / "sounds" / "voice_cue_000_sample0.wav", "CAMPAIGN_WAV");
    writeBinaryFile(convertedRoot / "15FLT" / "shape_010_Global.glb", "GLOBAL_AIRCRAFT_GLB");
    writeBinaryFile(convertedRoot / "15FLT" / "shape_011.glb", "GLOBAL_EXACT_AIRCRAFT_GLB");
    writeBinaryFile(convertedRoot / "SVN" / "15FLT" / "shape_010_Campaign.glb", "CAMPAIGN_AIRCRAFT_GLB");
    writeBinaryFile(convertedRoot / "SVN" / "15FLT" / "shape_011_Campaign.glb", "CAMPAIGN_NAMED_AIRCRAFT_GLB");
    writeBinaryFile(convertedRoot / "SVN" / "campaign.json",
                    "{\"format\":\"F15SE2_CAMPAIGN\",\"id\":\"SVN\","
                    "\"runtime_selection\":{\"scenario\":\"SVN\",\"scenario_base\":\"VN\"},"
                    "\"mission_objectives\":["
                    "{\"id\":\"primary_one\",\"object_slot\":3},"
                    "{\"id\":\"secondary_one\",\"object_slot\":4}],"
                    "\"sortie_sequence\":[{\"primary_objective_ids\":[\"primary_one\"],"
                    "\"secondary_objective_ids\":[\"secondary_one\"]}]}\n");
    require(setCustomWorldCampaign("SVN") != 0,
            "setCustomWorldCampaign selects scenario/base from campaign manifest");
    setCustomWorldCampaign(nullptr);
    require(setCustomWorldCampaign("SVN/campaign.json") != 0,
            "setCustomWorldCampaign accepts the documented campaign manifest path form");
    require(customCampaignPrimaryWorldObjectSlot() == 3 &&
                customCampaignSecondaryWorldObjectSlot() == 4,
            "setCustomWorldCampaign loads first-sortie mission target slots from campaign manifest");
    require(findReplacementAssetPath("title.pic", ".png", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).parent_path().filename() == "SVN",
            "campaign-local PNG replacement overrides global PNG while campaign is selected");
    require(findReplacementAssetPath("TITLE640.PIC", ".png", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "TITLE640.png" &&
                std::filesystem::path(replacementPath).parent_path().filename() == "SVN",
            "campaign-local high-resolution TITLE640 PNG replacement resolves while campaign is selected");
    require(findReplacementAssetPath("Desk.Pic", ".png", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "DESK.png" &&
                std::filesystem::path(replacementPath).parent_path().filename() == "SVN",
            "campaign-local full-page PIC PNG replacement resolves case-insensitively while campaign is selected");
    require(findReplacementAssetPath("font_1", ".ttf", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).parent_path().filename() == "fonts" &&
                std::filesystem::path(replacementPath).parent_path().parent_path().filename() == "SVN",
            "campaign-local TTF font replacement overrides global font while campaign is selected");
    require(findReplacementAssetPath("voice_cue_000_sample0", ".wav", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).parent_path().filename() == "sounds" &&
                std::filesystem::path(replacementPath).parent_path().parent_path().filename() == "SVN",
            "campaign-local WAV cue replacement overrides global cue while campaign is selected");
    require(findReplacementShapeModelPath("15FLT.3D3", 10, ".glb", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "shape_010_Campaign.glb" &&
                std::filesystem::path(replacementPath).parent_path().parent_path().filename() == "SVN",
            "campaign-local aircraft GLB replacement overrides shared aircraft GLB while campaign is selected");
    require(findReplacementShapeModelPath("15FLT.3D3", 11, ".glb", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).filename() == "shape_011_Campaign.glb",
            "campaign-local named aircraft GLB overrides a shared exact-name GLB");

    setenv("F15_REPLACEMENT_ROOT_ONLY", "1", 1);
    require(findReplacementAssetPath("title.pic", ".png", replacementPath, sizeof(replacementPath)) &&
                std::filesystem::path(replacementPath).parent_path().filename() == "SVN",
            "root-only campaign mode resolves campaign-local assets");
    require(!findReplacementAssetPath("GLOBAL_ONLY.PIC", ".png", replacementPath, sizeof(replacementPath)),
            "root-only campaign mode does not fall back outside the campaign directory");
    input = openFile("VN.WLD", kDosReadMode);
    require(input != nullptr, "openFile opens selected custom WLD scenario JSON stream");
    std::memset(jsonBridgeBuf, 0, sizeof(jsonBridgeBuf));
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "openFile reads rebuilt selected custom WLD scenario bytes");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "custom WLD scenario redirects legacy theater WLD to selected scenario JSON");
    fileClose(input);

    input = openFile("CE.WLD", kDosReadMode);
    require(input != nullptr, "openFile still opens non-base WLD legacy stream");
    char legacyWorldBuf[16] = {};
    require(fileReadRaw(input, legacyWorldBuf, kReadWholeRemainingStream) == 8,
            "non-base WLD is not redirected to selected custom scenario");
    require(std::string(legacyWorldBuf, 8) == "LEGACYCE",
            "custom WLD scenario base limits redirect to the selected theater");
    fileClose(input);
    setCustomWorldScenario(nullptr);
    setCustomWorldScenarioBase(nullptr);
    require(customCampaignPrimaryWorldObjectSlot() == 3,
            "direct scenario reset leaves campaign mission hints untouched until campaign reset");
    setCustomWorldCampaign(nullptr);
    require(customCampaignPrimaryWorldObjectSlot() == -1 &&
                customCampaignSecondaryWorldObjectSlot() == -1,
            "clearing campaign selection clears mission target hints");

    writeBinaryFile(convertedRoot / "ALT" / "ALT.WLD.json", "{\"format\":\"WLD\",\"campaign\":{\"id\":\"ALT\"}}\n");
    writeBinaryFile(convertedRoot / "ALT" / "campaign.json",
                    "{\"format\":\"F15SE2_CAMPAIGN\",\"id\":\"ALT\",\"base_theater\":\"VN\"}\n");
    require(setCustomWorldCampaign("ALT") != 0,
            "setCustomWorldCampaign accepts top-level id/base_theater manifest fields");
    input = openFile("VN.WLD", kDosReadMode);
    require(input != nullptr, "openFile opens fallback-field selected campaign WLD stream");
    std::memset(jsonBridgeBuf, 0, sizeof(jsonBridgeBuf));
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "fallback-field campaign manifest still redirects WLD loads");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "fallback-field campaign manifest selects scenario and base theater");
    fileClose(input);
    setCustomWorldCampaign(nullptr);

    require(setCustomWorldCampaign("MISSING") == 0,
            "setCustomWorldCampaign reports missing campaign manifest");

    writeBinaryFile("VN.3DT", "LEGACY3DT");
    writeBinaryFile(convertedRoot / "VN" / "VN.3DT.json", "{\"format\":\"3DT\"}\n");
    input = openFile("VN.3DT", kDosReadMode);
    require(input != nullptr, "openFile opens 3DT JSON replacement stream");
    std::memset(jsonBridgeBuf, 0, sizeof(jsonBridgeBuf));
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "openFile reads rebuilt 3DT JSON replacement bytes");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "openFile prefers 3DT JSON replacement over legacy file");
    fileClose(input);

    writeBinaryFile("VN.3DG", "LEGACY3DG");
    writeBinaryFile(convertedRoot / "VN" / "VN.3DG.json", "{\"format\":\"3DG\"}\n");
    input = openFile("VN.3DG", kDosReadMode);
    require(input != nullptr, "openFile opens 3DG JSON replacement stream");
    std::memset(jsonBridgeBuf, 0, sizeof(jsonBridgeBuf));
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "openFile reads rebuilt 3DG JSON replacement bytes");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "openFile prefers 3DG JSON replacement over legacy file");
    fileClose(input);

    writeBinaryFile("FULL.3D3", "LEGACYFULL3D3");
    writeBinaryFile(convertedRoot / "FULL" / "FULL.3D3.json", "{\"format\":\"3D3\",\"model_data\":[]}\n");
    input = openFile("FULL.3D3", kDosReadMode);
    require(input != nullptr, "openFile opens full 3D3 JSON replacement stream");
    std::memset(jsonBridgeBuf, 0, sizeof(jsonBridgeBuf));
    require(fileReadRaw(input, jsonBridgeBuf, kReadWholeRemainingStream) == 10,
            "openFile reads rebuilt full 3D3 JSON replacement bytes");
    require(std::string(jsonBridgeBuf, 10) == "JSONBRIDGE",
            "openFile prefers full 3D3 JSON replacement when model_data is present");
    fileClose(input);

    writeBinaryFile("LB.3D3", "LEGACY3D3");
    writeBinaryFile(convertedRoot / "LIBYA" / "LB.3D3.json", "{\"format\":\"3D3\"}\n");
    input = openFile("LB.3D3", kDosReadMode);
    require(input != nullptr, "openFile falls back to legacy 3D3 when JSON index is minimized");
    char legacy3dBuf[16] = {};
    require(fileReadRaw(input, legacy3dBuf, kReadWholeRemainingStream) == 9,
            "openFile reads legacy 3D3 bytes when minimized JSON has no model_data");
    require(std::string(legacy3dBuf, 9) == "LEGACY3D3",
            "minimized 3D3 JSON does not replace the table/fallback byte stream");
    fileClose(input);
#endif

    // Null/zero-size guards: the I/O helpers reject bad streams before dereferencing.
    require(fileReadRaw(nullptr, buf, kSingleByte) == kNullStreamError,
            "fileReadRaw reports null stream");
    require(fileRead(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileRead handles null/zero-size reads");
    require(fileWrite(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileWrite handles null/zero-size writes");
    fileClose(nullptr);

    // errorAndExit prints its '$'-terminated message and exits with the fatal status.
    // fork/waitpid observe the child's exit code, so this subtest is POSIX-only.
#if !defined(_WIN32)
    {
        const pid_t pid = fork();
        require(pid >= 0, "test should be able to fork for errorAndExit behavior");
        if (pid == 0) {
            char fatalMessage[] = {'F', 'A', 'T', 'A', 'L', '$', 0};
            errorAndExit(fatalMessage);
            std::exit(0); /* must not be reached */
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid,
                "test should be able to wait for errorAndExit child process");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kErrorExitStatus,
                "errorAndExit preserves the original fatal exit status");
    }
#else
    std::cout << "file_io_behavior_tests: skipping errorAndExit fork check on Windows\n";
#endif

    std::filesystem::current_path(oldCwd);
#if !defined(_WIN32)
    unsetenv("F15_REPLACEMENT_ROOT");
    unsetenv("F15_REPLACEMENT_ROOT_ONLY");
    unsetenv("F15_ASSET_TOOL");
#endif
    std::filesystem::remove_all(testDir);

    std::cout << "file_io_behavior_tests passed\n";
    return 0;
}
