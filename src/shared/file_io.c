/*
 * file_io.c - shared file I/O, backed by SDL_IOStream.
 */

#include "inttype.h"
#include "asset_compare.h"
#include "structured_asset_cache.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <quickdigest5.hpp>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <string>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <memory>
#include <system_error>
#include <fstream>
#include <unordered_map>
#include <limits>

using namespace std;
namespace fs = std::filesystem;
fs::path gamePath = ".";
static vector<unique_ptr<vector<uint8>>> g_replacementIoBuffers;
static string g_customWorldScenario;
static string g_customWorldScenarioBase;
static string g_customCampaignSortieSelector;
static int g_customCampaignPrimaryTargetSlot = -1;
static int g_customCampaignSecondaryTargetSlot = -1;
static string g_customCampaignSortieTitle;
static string g_customCampaignSortieBriefing;
static vector<int> g_friendlyFlightSlots;
static vector<int> g_friendlyBaseSlots;
static string g_allegianceScenario;

int customCampaignAircraftFriendly(int flightSlot, int baseSlot) {
    if (g_allegianceScenario.empty() || g_allegianceScenario != g_customWorldScenario) return 0;
    const vector<int> &slots = baseSlot >= 0 ? g_friendlyBaseSlots : g_friendlyFlightSlots;
    const int slot = baseSlot >= 0 ? baseSlot : flightSlot;
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

/* Read a nonnegative slot list without accepting a partially malformed list. */
static vector<int> campaignSlotList(const string &json, const char *field) {
    const size_t key = json.find(string("\"") + field + "\"");
    if (key == string::npos) return {};
    size_t pos = json.find(':', key);
    if (pos == string::npos) return {};
    auto skipSpace = [&]() {
        while (pos < json.size() && isspace((unsigned char)json[pos])) ++pos;
    };
    ++pos;
    skipSpace();
    if (pos == json.size() || json[pos++] != '[') return {};
    skipSpace();
    vector<int> slots;
    if (pos < json.size() && json[pos] == ']') return slots;
    while (pos < json.size()) {
        if (!isdigit((unsigned char)json[pos])) break;
        int slot = 0;
        while (pos < json.size() && isdigit((unsigned char)json[pos])) {
            const int digit = json[pos++] - '0';
            if (slot > (std::numeric_limits<int>::max() - digit) / 10) return {};
            slot = slot * 10 + digit;
        }
        slots.push_back(slot);
        skipSpace();
        if (pos < json.size() && json[pos] == ']') return slots;
        if (pos == json.size() || json[pos++] != ',') break;
        skipSpace();
    }
    LogWarn(("asset replacement: invalid campaign slot list: %s", field));
    return {};
}

static string normalizeScenarioStem(const char *value) {
    if (!value || !value[0]) return "";
    fs::path scenarioPath{value};
    string stem;
    string ext = scenarioPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return std::toupper(c); });
    if (ext == ".JSON") {
        stem = scenarioPath.stem().stem().string();
    } else if (ext == ".WLD") {
        stem = scenarioPath.stem().string();
    } else if (scenarioPath.has_filename()) {
        stem = scenarioPath.filename().string();
    }
    std::transform(stem.begin(), stem.end(), stem.begin(),
        [](unsigned char c){ return std::toupper(c); });
    return stem;
}

static string normalizeCampaignId(const char *value) {
    if (!value || !value[0]) return "";
    fs::path campaignPath{value};
    string id;
    string filename = campaignPath.filename().string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
        [](unsigned char c){ return (char)std::toupper(c); });

    /* The public launcher/docs allow both `SVN` and `SVN/campaign.json`.
     * Scenario normalization deliberately strips double extensions such as
     * SVN.WLD.json; campaign normalization is different because campaign.json
     * is a manifest filename and the campaign id is its parent directory. */
    if (filename == "CAMPAIGN.JSON" && campaignPath.has_parent_path()) {
        id = campaignPath.parent_path().filename().string();
    } else if (campaignPath.has_stem()) {
        id = campaignPath.stem().string();
    } else if (campaignPath.has_filename()) {
        id = campaignPath.filename().string();
    }
    std::transform(id.begin(), id.end(), id.begin(),
        [](unsigned char c){ return (char)std::toupper(c); });
    return id;
}

void setCustomWorldScenario(const char *scenario) {
    g_customWorldScenario = normalizeScenarioStem(scenario);
}

int customWorldScenarioIs(const char *scenario) {
    return g_customWorldScenario == normalizeScenarioStem(scenario);
}

void setCustomWorldScenarioBase(const char *base) {
    g_customWorldScenarioBase = normalizeScenarioStem(base);
}

void setCustomCampaignSortie(const char *sortie) {
    g_customCampaignSortieSelector = sortie && sortie[0] ? string(sortie) : string();
}

static void clearCustomCampaignMissionHints(void) {
    g_customCampaignPrimaryTargetSlot = -1;
    g_customCampaignSecondaryTargetSlot = -1;
    g_customCampaignSortieTitle.clear();
    g_customCampaignSortieBriefing.clear();
}

int customCampaignPrimaryWorldObjectSlot(void) {
    return g_customCampaignPrimaryTargetSlot;
}

int customCampaignSecondaryWorldObjectSlot(void) {
    return g_customCampaignSecondaryTargetSlot;
}

const char *customCampaignSortieTitle(void) {
    return g_customCampaignSortieTitle.empty() ? NULL : g_customCampaignSortieTitle.c_str();
}

const char *customCampaignSortieBriefing(void) {
    return g_customCampaignSortieBriefing.empty() ? NULL : g_customCampaignSortieBriefing.c_str();
}

int customWorldScenarioBaseTheaterIndex(void) {
    const string base = g_customWorldScenarioBase;
    if (base.empty()) return -1;
    if (base == "LIBYA" || base == "LB") return 0;
    if (base == "GULF" || base == "PG" || base == "PERSIAN") return 1;
    if (base == "VN" || base == "VIETNAM") return 2;
    if (base == "ME") return 3;
    if (base == "NC" || base == "NCAPE") return 4;
    if (base == "CE" || base == "CEUROPE") return 5;
    if (base == "DS" || base == "DESERT") return 6;
    if (base == "JP") return 6;
    if (base == "NA") return 7;
    return -1;
}

/* Sets the directory to read game assets from, default is current dir */
bool setGamePath(const char* path) {
    if (!path) return true;
    gamePath = fs::path{path};
    if (!fs::is_directory(gamePath)) {
        const string msg = "Game asset directory does not exist: " + gamePath.string();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
            msg.c_str(), NULL);
        return false;
    }
    return true;
}

/* The game ships its assets with UPPERCASE 8.3 names (LABS.PIC), but the code
 * passes lowercase or mixed-case literals ("labs.pic", "HallFame"). DOS
 * filesystems were case-insensitive; a Linux filesystem is not.
 *
 * Resolve `filename` to the spelling that actually exists on disk: try the name
 * as given, then an all-uppercase form, then an all-lowercase form, and return
 * whichever exists. If none exists, return the original name. */
static string resolveCasePath(const char *filename, const bool require = false) {
    const fs::path filePath = gamePath / filename;
    if (fs::exists(filePath)) return filePath.string();

    string upperCase{filename};
    std::transform(upperCase.begin(), upperCase.end(), upperCase.begin(),
        [](unsigned char c){ return std::toupper(c); });
    const fs::path upperPath = gamePath / upperCase;
    if (fs::exists(upperPath)) return upperPath.string();

    string lowerCase{filename};
    std::transform(lowerCase.begin(), lowerCase.end(), lowerCase.begin(),
        [](unsigned char c){ return std::tolower(c); });
    const fs::path lowerPath = gamePath / lowerCase;
    if (fs::exists(lowerPath)) return lowerPath.string();

    return require ? "" : filePath.string();
}

static string upperString(string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){ return std::toupper(c); });
    return value;
}

static string replacementDirForLegacy(const fs::path &legacy) {
    string stem = upperString(legacy.stem().string());
    if (stem == "LB") return "LIBYA";
    if (stem == "PG") return "GULF";
    return stem;
}

static string lowerString(string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return value;
}

static fs::path caseMappedPath(fs::path path, string (*mapper)(string)) {
    fs::path out;
    for (const fs::path &part : path) {
        out /= mapper(part.string());
    }
    return out;
}

static string shellQuote(const string &value) {
    string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static void addReplacementCandidate(vector<fs::path> &candidates, const fs::path &path) {
    if (std::find(candidates.begin(), candidates.end(), path) == candidates.end()) {
        candidates.push_back(path);
    }
}

static string defaultAssetToolCommand(const fs::path &assetHint) {
    const char *tool = getenv("F15_ASSET_TOOL");
    const char *replacementRoot = getenv("F15_REPLACEMENT_ROOT");
    if (tool && tool[0]) return string(tool);

    /* Prefer a script path when the repo/tools folder is near the current
     * working directory. The module form below only works when the repository
     * root is already on Python's import path. */
    vector<fs::path> candidates;
    fs::path cursor = assetHint;
    if (!cursor.empty() && cursor.has_filename()) cursor = cursor.parent_path();
    while (!cursor.empty()) {
        if (cursor.filename() == "converted_assets_all") {
            addReplacementCandidate(candidates, cursor.parent_path() / "tools/f15assets/cli.py");
            break;
        }
        fs::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = cursor.parent_path();
    }
    if (replacementRoot && replacementRoot[0]) {
        fs::path replacementPath{replacementRoot};
        /* Explicit replacement roots often live outside the repo. Try likely
         * repo-adjacent tool paths before falling back to module import; packaged
         * builds can always set F15_ASSET_TOOL to bypass this heuristic. */
        addReplacementCandidate(candidates, replacementPath.parent_path() / "tools/f15assets/cli.py");
        addReplacementCandidate(candidates, replacementPath / "../tools/f15assets/cli.py");
        addReplacementCandidate(candidates, replacementPath / "tools/f15assets/cli.py");
        addReplacementCandidate(candidates, replacementPath / "converted_assets_all/../tools/f15assets/cli.py");
    }
    addReplacementCandidate(candidates, fs::path{"tools/f15assets/cli.py"});
    addReplacementCandidate(candidates, fs::path{"../tools/f15assets/cli.py"});
    addReplacementCandidate(candidates, fs::path{"../../tools/f15assets/cli.py"});
    for (const fs::path &candidate : candidates) {
        if (fs::exists(candidate)) return string("python3 ") + shellQuote(candidate.string());
    }
    return string("python3 -m tools.f15assets.cli");
}

static vector<fs::path> replacementSearchRoots(void) {
    vector<fs::path> roots;
    const char *replacementRootEnv = getenv("F15_REPLACEMENT_ROOT");
    const bool rootOnly = getenv("F15_REPLACEMENT_ROOT_ONLY") != nullptr;
    static int warnedInvalidReplacementRoot = 0;
    /* Explicit custom-pack root. This keeps original DOS assets in gamePath
     * while loading modern replacements from an external converted tree. The
     * env var may point either at converted_assets_all itself or at a parent
     * containing it; missing directories are ignored. The remaining roots
     * preserve older workflows: converted_assets_all beside the game path,
     * converted_assets_all in the current working directory, then direct
     * gamePath/current-directory replacement files. */
    if (replacementRootEnv && replacementRootEnv[0]) {
        fs::path replacementRoot{replacementRootEnv};
        std::error_code ec;
        int addedExplicitRoot = 0;
        if (fs::is_directory(replacementRoot, ec)) {
            addReplacementCandidate(roots, rootOnly && !g_customWorldScenario.empty()
                ? replacementRoot / g_customWorldScenario : replacementRoot);
            addedExplicitRoot = 1;
        }
        ec.clear();
        if (fs::is_directory(replacementRoot / "converted_assets_all", ec)) {
            const fs::path convertedRoot = replacementRoot / "converted_assets_all";
            addReplacementCandidate(roots, rootOnly && !g_customWorldScenario.empty()
                ? convertedRoot / g_customWorldScenario : convertedRoot);
            addedExplicitRoot = 1;
        }
        if (!addedExplicitRoot && !warnedInvalidReplacementRoot) {
            warnedInvalidReplacementRoot = 1;
            LogWarn(("asset replacement: F15_REPLACEMENT_ROOT does not name an existing replacement directory: %s", replacementRootEnv));
        }
    }
    if (rootOnly) return roots;
    addReplacementCandidate(roots, gamePath / "converted_assets_all");
    addReplacementCandidate(roots, fs::path{"converted_assets_all"});
    addReplacementCandidate(roots, gamePath);
    addReplacementCandidate(roots, fs::path{"."});
    return roots;
}

static vector<fs::path> recursiveReplacementSearchRoots(void) {
    vector<fs::path> roots;
    const char *replacementRootEnv = getenv("F15_REPLACEMENT_ROOT");
    const bool rootOnly = getenv("F15_REPLACEMENT_ROOT_ONLY") != nullptr;

    /* Recursive fallback is useful for converted asset trees that preserve
     * campaign/theater subdirectories, but it must not walk arbitrary gamePath
     * or process-current directories. Those roots can be large repos/home dirs
     * and may contain unrelated same-named files. */
    if (replacementRootEnv && replacementRootEnv[0]) {
        fs::path replacementRoot{replacementRootEnv};
        std::error_code ec;
        if (fs::is_directory(replacementRoot, ec)) {
            addReplacementCandidate(roots, rootOnly && !g_customWorldScenario.empty()
                ? replacementRoot / g_customWorldScenario : replacementRoot);
        }
        ec.clear();
        if (fs::is_directory(replacementRoot / "converted_assets_all", ec)) {
            const fs::path convertedRoot = replacementRoot / "converted_assets_all";
            addReplacementCandidate(roots, rootOnly && !g_customWorldScenario.empty()
                ? convertedRoot / g_customWorldScenario : convertedRoot);
        }
    }
    if (rootOnly) return roots;
    addReplacementCandidate(roots, gamePath / "converted_assets_all");
    addReplacementCandidate(roots, fs::path{"converted_assets_all"});
    return roots;
}

static string jsonStringField(const string &json, const string &key) {
    const string needle = string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == string::npos) return "";
    size_t end = pos + 1;
    string out;
    int escaped = 0;
    for (; end < json.size(); end++) {
        const char c = json[end];
        if (escaped) {
            out.push_back(c);
            escaped = 0;
            continue;
        }
        if (c == '\\') {
            escaped = 1;
            continue;
        }
        if (c == '"') return out;
        out.push_back(c);
    }
    return "";
}

static int jsonFirstStringInArrayField(const string &json, const string &key, string &out) {
    const string needle = string("\"") + key + "\"";
    size_t pos = json.find(needle);
    size_t scan;

    out.clear();
    if (pos == string::npos) return 0;
    pos = json.find(':', pos + needle.size());
    if (pos == string::npos) return 0;
    pos = json.find('[', pos + 1);
    if (pos == string::npos) return 0;
    scan = json.find('"', pos + 1);
    if (scan == string::npos) return 0;
    scan++;
    for (; scan < json.size(); scan++) {
        const char c = json[scan];
        if (c == '"') return !out.empty();
        if (c == '\\' && scan + 1 < json.size()) {
            scan++;
            out.push_back(json[scan]);
        } else {
            out.push_back(c);
        }
    }
    return 0;
}

static int jsonFirstStringInArrayFieldAfter(const string &json, size_t start, const string &key, string &out) {
    const string needle = string("\"") + key + "\"";
    size_t pos = json.find(needle, start);
    size_t scan;

    out.clear();
    if (pos == string::npos) return 0;
    pos = json.find(':', pos + needle.size());
    if (pos == string::npos) return 0;
    pos = json.find('[', pos + 1);
    if (pos == string::npos) return 0;
    scan = json.find('"', pos + 1);
    if (scan == string::npos) return 0;
    scan++;
    for (; scan < json.size(); scan++) {
        const char c = json[scan];
        if (c == '"') return !out.empty();
        if (c == '\\' && scan + 1 < json.size()) {
            scan++;
            out.push_back(json[scan]);
        } else {
            out.push_back(c);
        }
    }
    return 0;
}

static string jsonStringFieldAfter(const string &json, size_t start, const string &key) {
    const string needle = string("\"") + key + "\"";
    size_t pos = json.find(needle, start);
    size_t end;
    string out;
    int escaped = 0;

    if (pos == string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == string::npos) return "";
    for (end = pos + 1; end < json.size(); end++) {
        const char c = json[end];
        if (escaped) {
            out.push_back(c);
            escaped = 0;
            continue;
        }
        if (c == '\\') {
            escaped = 1;
            continue;
        }
        if (c == '"') return out;
        out.push_back(c);
    }
    return "";
}

static int jsonIntFieldAfter(const string &json, size_t start, const string &key, int fallback) {
    const string needle = string("\"") + key + "\"";
    size_t pos = json.find(needle, start);
    int sign = 1;
    int value = 0;
    int gotDigit = 0;

    if (pos == string::npos) return fallback;
    pos = json.find(':', pos + needle.size());
    if (pos == string::npos) return fallback;
    pos++;
    while (pos < json.size() && isspace((unsigned char)json[pos])) pos++;
    if (pos < json.size() && json[pos] == '-') {
        sign = -1;
        pos++;
    }
    while (pos < json.size() && isdigit((unsigned char)json[pos])) {
        const int digit = json[pos] - '0';
        if (value > (std::numeric_limits<int>::max() - digit) / 10) return fallback;
        gotDigit = 1;
        value = value * 10 + digit;
        pos++;
    }
    return gotDigit ? value * sign : fallback;
}

static int campaignObjectiveObjectSlot(const string &json, const string &objectiveId) {
    const size_t objectivesPos = json.find("\"mission_objectives\"");
    size_t idPos;

    if (objectiveId.empty() || objectivesPos == string::npos) return -1;
    idPos = objectivesPos;
    while ((idPos = json.find("\"id\"", idPos)) != string::npos) {
        string id;
        size_t valuePos = json.find(':', idPos + 4);
        if (valuePos == string::npos) return -1;
        valuePos = json.find('"', valuePos + 1);
        if (valuePos == string::npos) return -1;
        valuePos++;
        for (; valuePos < json.size(); valuePos++) {
            const char c = json[valuePos];
            if (c == '"') break;
            if (c == '\\' && valuePos + 1 < json.size()) {
                valuePos++;
                id.push_back(json[valuePos]);
            } else {
                id.push_back(c);
            }
        }
        if (id == objectiveId) return jsonIntFieldAfter(json, idPos, "object_slot", -1);
        idPos++;
    }
    return -1;
}

static size_t customCampaignSelectedSortiePos(const string &json) {
    const size_t sequencePos = json.find("\"sortie_sequence\"");
    const string selector = g_customCampaignSortieSelector;
    size_t pos;
    int numericSelector = 1;

    if (sequencePos == string::npos) return string::npos;
    /* Without an explicit sortie, let the original generator choose targets and bases. */
    if (selector.empty()) return string::npos;
    for (char c : selector) {
        if (!isdigit((unsigned char)c)) {
            numericSelector = 0;
            break;
        }
    }
    if (numericSelector) {
        pos = sequencePos;
        while ((pos = json.find("\"phase\"", pos)) != string::npos) {
            if (jsonIntFieldAfter(json, pos, "phase", -1) == atoi(selector.c_str())) {
                return pos;
            }
            pos++;
        }
    } else {
        pos = sequencePos;
        while ((pos = json.find("\"id\"", pos)) != string::npos) {
            if (jsonStringFieldAfter(json, pos, "id") == selector) {
                return pos;
            }
            pos++;
        }
    }
    LogWarn(("asset replacement: campaign sortie selector did not match any sortie: %s", selector.c_str()));
    return json.find('{', sequencePos);
}

static void loadCustomCampaignMissionHints(const string &json) {
    string primaryObjective;
    string secondaryObjective;
    size_t sortiePos;

    clearCustomCampaignMissionHints();
    /* Campaign WLD JSON is generated/packaged by tools/f15assets. The game
     * only needs the selected sortie's object slots as hints; the legacy
     * generator still validates object type, bases, range, and mission-table
     * compatibility before accepting them. */
    sortiePos = customCampaignSelectedSortiePos(json);
    if (sortiePos == string::npos) return;
    if (jsonFirstStringInArrayFieldAfter(json, sortiePos, "primary_objective_ids", primaryObjective)) {
        g_customCampaignPrimaryTargetSlot = campaignObjectiveObjectSlot(json, primaryObjective);
    }
    if (jsonFirstStringInArrayFieldAfter(json, sortiePos, "secondary_objective_ids", secondaryObjective)) {
        g_customCampaignSecondaryTargetSlot = campaignObjectiveObjectSlot(json, secondaryObjective);
    }
    /* The START briefing board is very small. package-campaign syncs edited
     * briefing Markdown files back into these WLD sortie fields so authors can customize
     * the visible title/briefing without hand-editing duplicated JSON. */
    g_customCampaignSortieTitle = jsonStringFieldAfter(json, sortiePos, "title");
    g_customCampaignSortieBriefing = jsonStringFieldAfter(json, sortiePos, "briefing");
}

int setCustomWorldCampaign(const char *campaign) {
    g_friendlyFlightSlots.clear();
    g_friendlyBaseSlots.clear();
    g_allegianceScenario.clear();
    const string campaignId = normalizeCampaignId(campaign);
    if (campaignId.empty()) {
        setCustomWorldScenario(nullptr);
        setCustomWorldScenarioBase(nullptr);
        clearCustomCampaignMissionHints();
        return 1;
    }

    const string lowerCampaign = lowerString(campaignId);
    vector<fs::path> candidates;

    clearCustomCampaignMissionHints();
    for (const fs::path &root : replacementSearchRoots()) {
        addReplacementCandidate(candidates, root / campaignId / "campaign.json");
        addReplacementCandidate(candidates, root / lowerCampaign / "campaign.json");
    }

    for (const fs::path &candidate : candidates) {
        if (!fs::exists(candidate)) continue;
        std::ifstream in(candidate);
        if (!in) continue;
        const string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        string scenario = jsonStringField(json, "scenario");
        string scenarioBase = jsonStringField(json, "scenario_base");
        if (scenario.empty()) scenario = jsonStringField(json, "id");
        if (scenarioBase.empty()) scenarioBase = jsonStringField(json, "base_theater");
        if (scenario.empty() || scenarioBase.empty()) {
            LogWarn(("asset replacement: campaign manifest missing scenario/base fields: %s", candidate.string().c_str()));
            return 0;
        }
        setCustomWorldScenario(scenario.c_str());
        setCustomWorldScenarioBase(scenarioBase.c_str());
        loadCustomCampaignMissionHints(json);
        g_friendlyFlightSlots = campaignSlotList(json, "friendly_flight_slots");
        g_friendlyBaseSlots = campaignSlotList(json, "friendly_base_slots");
        g_allegianceScenario = g_customWorldScenario;
        LogInfo(("asset replacement: selected campaign %s from %s (scenario=%s base=%s)",
                 campaignId.c_str(), candidate.string().c_str(), scenario.c_str(), scenarioBase.c_str()));
        return 1;
    }

    LogWarn(("asset replacement: campaign manifest not found for %s", campaignId.c_str()));
    return 0;
}

static void addRecursiveReplacementCandidates(vector<fs::path> &candidates,
                                              const fs::path &root,
                                              const string &filename) {
    std::error_code ec;
    const string filenameLower = lowerString(filename);
    if (!fs::is_directory(root, ec)) return;
    /* convert-all preserves campaign/theater subdirectories below
     * converted_assets_all. Keep direct candidates first, then use this bounded
     * fallback so runtime loading can find recursive conversion output. */
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (lowerString(entry.path().filename().string()) == filenameLower) {
            addReplacementCandidate(candidates, entry.path());
        }
    }
}

/* Resolve a modern replacement asset next to converted exports.
 *
 * `legacyFilename` is the original game filename ("LB.3D3", "Libya.wld",
 * "TITLE.PIC"). `modernExt` is the preferred modern extension, including dot
 * (".glb", ".json", ".png", ".wav"). The media file is authoritative when it
 * overlaps JSON sidecar metadata.
 */
int findReplacementAssetPath(const char *legacyFilename, const char *modernExt,
                             char *outPath, size_t outPathSize) {
    if (!legacyFilename || !modernExt || !outPath || outPathSize == 0) return 0;

    fs::path legacy{legacyFilename};
    const string stem = upperString(legacy.stem().string());
    const string ext = upperString(legacy.extension().string());
    const string dir = replacementDirForLegacy(legacy);
    const string modern = modernExt;
    const fs::path legacyParent = legacy.parent_path();

    vector<fs::path> candidates;
    vector<fs::path> replacementRoots = replacementSearchRoots();
    vector<fs::path> recursiveRoots = recursiveReplacementSearchRoots();

    const string nameWithLegacyExt = stem + ext + modern;
    const string nameWithoutLegacyExt = stem + modern;
    const string lowerNameWithLegacyExt = lowerString(nameWithLegacyExt);
    const string lowerNameWithoutLegacyExt = lowerString(nameWithoutLegacyExt);

    /* Custom scenario selection is a WLD-only redirect. START/EGAME can keep
     * asking for the normal theater WLD while the replacement layer supplies a
     * modern scenario JSON such as converted_assets_all/SVN/SVN.WLD.json. Other
     * theater files remain tied to the selected base theater. */
    if (ext == ".WLD" && modern == ".json" && !g_customWorldScenario.empty() &&
        (g_customWorldScenarioBase.empty() || stem == g_customWorldScenarioBase)) {
        const string scenario = g_customWorldScenario;
        const string lowerScenario = lowerString(scenario);
        for (const fs::path &root : replacementRoots) {
            addReplacementCandidate(candidates, root / (scenario + ".WLD.json"));
            addReplacementCandidate(candidates, root / (lowerScenario + ".wld.json"));
            addReplacementCandidate(candidates, root / scenario / (scenario + ".WLD.json"));
            addReplacementCandidate(candidates, root / lowerScenario / (lowerScenario + ".wld.json"));
            addReplacementCandidate(candidates, root / scenario / (scenario + ".json"));
            addReplacementCandidate(candidates, root / lowerScenario / (lowerScenario + ".json"));
        }
    }
    if (ext != ".WLD" && !g_customWorldScenario.empty()) {
        const string scenario = g_customWorldScenario;
        const string lowerScenario = lowerString(scenario);
        for (const fs::path &root : replacementRoots) {
            if (!legacyParent.empty()) {
                vector<fs::path> scopedParents;
                addReplacementCandidate(scopedParents, legacyParent);
                addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, upperString));
                addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, lowerString));
                for (const fs::path &scopedParent : scopedParents) {
                    const fs::path scenarioScopedRoot = root / scenario / scopedParent;
                    const fs::path lowerScenarioScopedRoot = root / lowerScenario / scopedParent;
                    /* Campaign packs may carry structured modern assets such
                     * as sounds/intro_music.asound.json. Prefer the selected
                     * campaign's scoped file directly instead of relying on
                     * recursive fallback over the entire converted tree. */
                    addReplacementCandidate(candidates, scenarioScopedRoot / nameWithLegacyExt);
                    addReplacementCandidate(candidates, scenarioScopedRoot / lowerNameWithLegacyExt);
                    addReplacementCandidate(candidates, scenarioScopedRoot / nameWithoutLegacyExt);
                    addReplacementCandidate(candidates, scenarioScopedRoot / lowerNameWithoutLegacyExt);
                    addReplacementCandidate(candidates, lowerScenarioScopedRoot / nameWithLegacyExt);
                    addReplacementCandidate(candidates, lowerScenarioScopedRoot / lowerNameWithLegacyExt);
                    addReplacementCandidate(candidates, lowerScenarioScopedRoot / nameWithoutLegacyExt);
                    addReplacementCandidate(candidates, lowerScenarioScopedRoot / lowerNameWithoutLegacyExt);
                }
            }
            if (stem.rfind("VOICE_CUE_", 0) == 0 && modern == ".wav") {
                addReplacementCandidate(candidates, root / scenario / "sounds" / (lowerString(stem) + ".wav"));
                addReplacementCandidate(candidates, root / lowerScenario / "sounds" / (lowerString(stem) + ".wav"));
            }
            if (stem.rfind("FONT_", 0) == 0 && (modern == ".ttf" || modern == ".otf" || modern == ".bdf" || modern == ".png")) {
                addReplacementCandidate(candidates, root / scenario / "fonts" / (lowerString(stem) + modern));
                addReplacementCandidate(candidates, root / lowerScenario / "fonts" / (lowerString(stem) + modern));
            }
            addReplacementCandidate(candidates, root / scenario / nameWithLegacyExt);
            addReplacementCandidate(candidates, root / scenario / lowerNameWithLegacyExt);
            addReplacementCandidate(candidates, root / scenario / nameWithoutLegacyExt);
            addReplacementCandidate(candidates, root / scenario / lowerNameWithoutLegacyExt);
            addReplacementCandidate(candidates, root / lowerScenario / nameWithLegacyExt);
            addReplacementCandidate(candidates, root / lowerScenario / lowerNameWithLegacyExt);
            addReplacementCandidate(candidates, root / lowerScenario / nameWithoutLegacyExt);
            addReplacementCandidate(candidates, root / lowerScenario / lowerNameWithoutLegacyExt);
        }
    }

    if (!legacyParent.empty()) {
        vector<fs::path> scopedParents;
        addReplacementCandidate(scopedParents, legacyParent);
        addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, upperString));
        addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, lowerString));
        for (const fs::path &root : replacementRoots) {
            for (const fs::path &scopedParent : scopedParents) {
                const fs::path scopedRoot = root / scopedParent;
                if (stem.rfind("VOICE_CUE_", 0) == 0 && modern == ".wav") {
                    addReplacementCandidate(candidates, scopedRoot / "sounds" / (lowerString(stem) + ".wav"));
                }
                if (stem.rfind("FONT_", 0) == 0 && (modern == ".ttf" || modern == ".otf" || modern == ".bdf" || modern == ".png")) {
                    addReplacementCandidate(candidates, scopedRoot / "fonts" / (lowerString(stem) + modern));
                }
                addReplacementCandidate(candidates, scopedRoot / dir / nameWithLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / dir / lowerNameWithLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / dir / nameWithoutLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / dir / lowerNameWithoutLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / nameWithLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / lowerNameWithLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / nameWithoutLegacyExt);
                addReplacementCandidate(candidates, scopedRoot / lowerNameWithoutLegacyExt);
            }
        }
    }
    for (const fs::path &root : replacementRoots) {
        if (stem.rfind("VOICE_CUE_", 0) == 0 && modern == ".wav") {
            addReplacementCandidate(candidates, root / "sounds" / (lowerString(stem) + ".wav"));
        }
        if (stem.rfind("FONT_", 0) == 0 && (modern == ".ttf" || modern == ".otf" || modern == ".bdf" || modern == ".png")) {
            addReplacementCandidate(candidates, root / "fonts" / (lowerString(stem) + modern));
        }
        addReplacementCandidate(candidates, root / dir / nameWithLegacyExt);
        addReplacementCandidate(candidates, root / dir / lowerNameWithLegacyExt);
        addReplacementCandidate(candidates, root / dir / nameWithoutLegacyExt);
        addReplacementCandidate(candidates, root / dir / lowerNameWithoutLegacyExt);
        addReplacementCandidate(candidates, root / nameWithLegacyExt);
        addReplacementCandidate(candidates, root / lowerNameWithLegacyExt);
        addReplacementCandidate(candidates, root / nameWithoutLegacyExt);
        addReplacementCandidate(candidates, root / lowerNameWithoutLegacyExt);
    }
    for (const fs::path &root : recursiveRoots) {
        addRecursiveReplacementCandidates(candidates, root, nameWithLegacyExt);
        addRecursiveReplacementCandidates(candidates, root, nameWithoutLegacyExt);
    }

    for (const fs::path &candidate : candidates) {
        if (!fs::exists(candidate)) continue;
        const string resolved = candidate.string();
        std::snprintf(outPath, outPathSize, "%s", resolved.c_str());
        return 1;
    }

    outPath[0] = 0;
    return 0;
}

/* Resolve an editable per-shape 3D replacement exported beside a .3D3/.WLD group.
 *
 * The converter writes shape files as `shape_###.glb` or
 * `shape_###_<derived label>.glb` directly in the extensionless asset directory
 * (for example `converted_assets_all/VN/shape_049_SAM_Radar.glb`).  The label is
 * only for humans; the stable lookup key is the original shape slot number.
 */
int findReplacementShapeModelPath(const char *containerLegacyFilename, int shapeId,
                                  const char *modernExt, char *outPath,
                                  size_t outPathSize) {
    static unordered_map<string, string> resultCache;
    if (!containerLegacyFilename || !modernExt || !outPath || outPathSize == 0) return 0;
    if (shapeId < 0 || shapeId > 999) return 0;

    const char *replacementRootEnv = getenv("F15_REPLACEMENT_ROOT");
    const char *replacementRootOnlyEnv = getenv("F15_REPLACEMENT_ROOT_ONLY");
    const string cacheKey = string(containerLegacyFilename) + '\n' +
        std::to_string(shapeId) + '\n' + modernExt + '\n' +
        gamePath.string() + '\n' + g_customWorldScenario + '\n' +
        g_customWorldScenarioBase + '\n' +
        (replacementRootEnv ? replacementRootEnv : "") + '\n' +
        (replacementRootOnlyEnv ? replacementRootOnlyEnv : "");
    const auto cached = resultCache.find(cacheKey);
    if (cached != resultCache.end()) {
        if (cached->second.empty() || cached->second.size() >= outPathSize) {
            outPath[0] = 0;
            return 0;
        }
        std::snprintf(outPath, outPathSize, "%s", cached->second.c_str());
        return 1;
    }
    const auto found = [&](const fs::path &path) {
        const string resolved = path.string();
        resultCache.emplace(cacheKey, resolved);
        if (resolved.size() >= outPathSize) {
            outPath[0] = 0;
            return 0;
        }
        std::snprintf(outPath, outPathSize, "%s", resolved.c_str());
        return 1;
    };

    fs::path legacy{containerLegacyFilename};
    const string dir = replacementDirForLegacy(legacy);
    const string lowerDir = lowerString(dir);
    const string modern = modernExt;
    const fs::path legacyParent = legacy.parent_path();
    char exactName[32];
    string lowerExactName;
    char prefix[32];
    std::snprintf(exactName, sizeof(exactName), "shape_%03d%s", shapeId, modern.c_str());
    lowerExactName = lowerString(exactName);
    std::snprintf(prefix, sizeof(prefix), "shape_%03d_", shapeId);
    vector<fs::path> replacementRoots = replacementSearchRoots();
    vector<fs::path> recursiveRoots = recursiveReplacementSearchRoots();

    vector<fs::path> roots;
    if (!g_customWorldScenario.empty()) {
        const string scenario = g_customWorldScenario;
        const string lowerScenario = lowerString(scenario);
        for (const fs::path &root : replacementRoots) {
            addReplacementCandidate(roots, root / scenario / dir);
            addReplacementCandidate(roots, root / scenario / lowerDir);
            addReplacementCandidate(roots, root / lowerScenario / dir);
            addReplacementCandidate(roots, root / lowerScenario / lowerDir);
        }
    }
    if (!legacyParent.empty()) {
        vector<fs::path> scopedParents;
        addReplacementCandidate(scopedParents, legacyParent);
        addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, upperString));
        addReplacementCandidate(scopedParents, caseMappedPath(legacyParent, lowerString));
        for (const fs::path &root : replacementRoots) {
            for (const fs::path &scopedParent : scopedParents) {
                addReplacementCandidate(roots, root / scopedParent / dir);
                addReplacementCandidate(roots, root / scopedParent / lowerDir);
                addReplacementCandidate(roots, root / scopedParent);
            }
        }
    }
    for (const fs::path &root : replacementRoots) {
        addReplacementCandidate(roots, root / dir);
        addReplacementCandidate(roots, root / lowerDir);
        addReplacementCandidate(roots, root);
    }
    for (const fs::path &convertedRoot : recursiveRoots) {
        std::error_code ec;
        if (!fs::is_directory(convertedRoot, ec)) continue;
        for (const fs::directory_entry &entry : fs::recursive_directory_iterator(convertedRoot, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            if (lowerString(entry.path().filename().string()) == lowerDir) {
                addReplacementCandidate(roots, entry.path());
            }
        }
    }

    for (const fs::path &root : roots) {
        if (modern == ".glmesh") {
            const fs::path cached = root / "cache" / exactName;
            if (fs::exists(cached)) {
                return found(cached);
            }
        }
        const fs::path exact = root / exactName;
        if (fs::exists(exact)) {
            return found(exact);
        }
        const fs::path lowerExact = root / lowerExactName;
        if (fs::exists(lowerExact)) {
            return found(lowerExact);
        }
        std::error_code ec;
        vector<fs::path> scanRoots;
        addReplacementCandidate(scanRoots, root);
        if (modern == ".glmesh") addReplacementCandidate(scanRoots, root / "cache");
        for (const fs::path &scanRoot : scanRoots) {
            if (!fs::is_directory(scanRoot, ec)) continue;
            for (const fs::directory_entry &entry : fs::directory_iterator(scanRoot, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                const fs::path path = entry.path();
                const string filename = path.filename().string();
                const string lowerFilename = lowerString(filename);
                if (lowerFilename != lowerExactName && lowerFilename.rfind(lowerString(prefix), 0) != 0) continue;
                if (lowerString(path.extension().string()) != lowerString(modern)) continue;
                return found(path);
            }
        }
    }

    resultCache.emplace(cacheKey, string());
    outPath[0] = 0;
    return 0;
}

static const char *preferredModernExtForLegacy(const char *filename) {
    if (!filename) return NULL;
    fs::path legacy{filename};
    const string ext = upperString(legacy.extension().string());
    if (ext == ".3D3") return ".glb";
    if (ext == ".PIC" || ext == ".SPR") return ".png";
    if (ext == ".WLD" || ext == ".3DT" || ext == ".3DG") return ".json";
    return NULL;
}

static const char *structuredFormatForLegacy(const char *filename) {
    if (!filename) return NULL;
    fs::path legacy{filename};
    const string ext = upperString(legacy.extension().string());
    if (ext == ".WLD") return "WLD";
    if (ext == ".3D3") return "3D3";
    if (ext == ".3DT") return "3DT";
    if (ext == ".3DG") return "3DG";
    return NULL;
}

static unique_ptr<vector<uint8>> readHostFileBytes(const string &path, size_t maxBytes) {
    FILE *fp = fopen(path.c_str(), "rb");
    unique_ptr<vector<uint8>> data;
    unsigned char chunk[4096];
    size_t got;

    if (!fp) return nullptr;
    data = make_unique<vector<uint8>>();
    while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        data->insert(data->end(), chunk, chunk + got);
        if (data->size() > maxBytes) {
            fclose(fp);
            return nullptr;
        }
    }
    if (ferror(fp)) {
        fclose(fp);
        return nullptr;
    }
    fclose(fp);
    return data;
}

static int structuredJsonHasProperty(const char *path, const char *key) {
    unique_ptr<vector<uint8>> data;
    string text;
    size_t pos = 0;

    if (!path || !key) return 0;
    data = readHostFileBytes(path, 1024 * 1024);
    if (!data) return 0;
    text.assign((const char *)data->data(), data->size());
    /* This is only a cheap bridge gate, not JSON parsing. For .3D3, default
     * minimized sidecars are metadata-only and must not invoke build-binary;
     * full bridge dumps include the quoted "model_data" property. The Python
     * tool remains the authority for parsing and rebuilding actual JSON. */
    while ((pos = text.find(key, pos)) != string::npos) {
        size_t scan = pos + strlen(key);
        while (scan < text.size() && isspace((unsigned char)text[scan])) scan++;
        if (scan < text.size() && text[scan] == ':') return 1;
        pos++;
    }
    return 0;
}

static void compareStructuredReplacementWithLegacy(const char *filename,
                                                   const vector<uint8> &replacementData,
                                                   const char *replacementPath) {
    unique_ptr<vector<uint8>> legacyData;
    string legacyPath;

    if (!assetCompareEnabled()) return;

    legacyPath = resolveCasePath(filename, true);
    if (legacyPath.empty()) {
        LogWarn(("asset replacement compare: legacy file missing for %s; replacement=%s", filename, replacementPath));
        return;
    }
    legacyData = readHostFileBytes(legacyPath, 1024 * 1024);
    if (!legacyData) {
        LogWarn(("asset replacement compare: failed to read legacy file %s for %s", legacyPath.c_str(), filename));
        return;
    }
    assetCompareStructuredBytes(
        filename,
        legacyData->data(),
        legacyData->size(),
        replacementData.data(),
        replacementData.size(),
        replacementPath
    );
}

static SDL_IOStream *openStructuredJsonReplacement(const char *filename) {
    char replacementPath[512];
    const char *fmt = structuredFormatForLegacy(filename);
    string command;
    FILE *pipe;
    unique_ptr<vector<uint8>> data;
    unsigned char chunk[4096];
    size_t got;
    int status;

    if (!fmt) return NULL;
    if (!findReplacementAssetPath(filename, ".json", replacementPath, sizeof(replacementPath))) {
        return NULL;
    }
    if (strcmp(fmt, "3D3") == 0 && !structuredJsonHasProperty(replacementPath, "\"model_data\"")) {
        LogInfo((
            "asset replacement: found minimized .3D3 JSON index for %s at %s; "
            "geometry uses GLB and the .3D3 byte stream remains the table/fallback source",
            filename,
            replacementPath
        ));
        return NULL;
    }

    data = make_unique<vector<uint8>>();
    if (f15assets::loadStructuredCache(replacementPath, *data)) {
        compareStructuredReplacementWithLegacy(filename, *data, replacementPath);
        SDL_IOStream *io = SDL_IOFromConstMem(data->data(), data->size());
        if (!io) return NULL;
        g_replacementIoBuffers.push_back(std::move(data));
        LogInfo(("asset replacement: loaded %s from compiled JSON cache %s", filename, replacementPath));
        return io;
    }

    /* WLD/3DT/3DG and full .3D3 dumps are structured legacy byte streams, not
     * media files, so JSON is the editable modern source. Until the game has
     * native JSON importers, rebuild the exact legacy byte stream here and feed
     * it to the proven old loader. */
    command = defaultAssetToolCommand(fs::path{replacementPath});
    command += " build-binary ";
    command += shellQuote(replacementPath);
    command += " --format ";
    command += fmt;

    pipe = popen(command.c_str(), "r");
    if (!pipe) {
        LogWarn(("asset replacement: failed to run JSON importer for %s; using legacy binary", filename));
        return NULL;
    }

    data = make_unique<vector<uint8>>();
    while ((got = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        data->insert(data->end(), chunk, chunk + got);
        if (data->size() > 1024 * 1024) {
            LogWarn(("asset replacement: JSON importer output too large for %s; using legacy binary", filename));
            pclose(pipe);
            return NULL;
        }
    }
    status = pclose(pipe);
    if (status != 0 || data->empty()) {
        LogWarn(("asset replacement: JSON importer failed for %s from %s; using legacy binary", filename, replacementPath));
        return NULL;
    }

    compareStructuredReplacementWithLegacy(filename, *data, replacementPath);

    SDL_IOStream *io = SDL_IOFromConstMem(data->data(), data->size());
    if (!io) {
        LogWarn(("asset replacement: failed to open rebuilt JSON bytes for %s; using legacy binary", filename));
        return NULL;
    }
    g_replacementIoBuffers.push_back(std::move(data));
    LogInfo(("asset replacement: loaded %s from JSON %s (%zu bytes)", filename, replacementPath, g_replacementIoBuffers.back()->size()));
    return io;
}

static void logPreferredReplacementIfPresent(const char *filename) {
    char replacementPath[512];
    const char *modernExt = preferredModernExtForLegacy(filename);
    fs::path legacy{filename ? filename : ""};
    const string ext = filename ? upperString(legacy.extension().string()) : "";
    if (!modernExt) return;
    if (ext == ".PIC" || ext == ".SPR" || ext == ".WLD" || ext == ".3DT" || ext == ".3DG") {
        return;
    }
    if (!findReplacementAssetPath(filename, modernExt, replacementPath, sizeof(replacementPath))) return;
    if (ext == ".3D3") {
        LogInfo((
            "asset replacement: found overview GLB for %s at %s; "
            "per-shape GLB replacement is handled by the OpenGL renderer and "
            "the active .3D3 stream remains available for shape slots/comparison",
            filename,
            replacementPath
        ));
    }
}

static int hasRequiredModernReplacement(const char *filename) {
    char replacementPath[512];
    fs::path legacy{filename ? filename : ""};
    const string ext = filename ? upperString(legacy.extension().string()) : "";

    if (!filename) return 0;
    if (ext == ".PIC" || ext == ".SPR") {
        return findReplacementAssetPath(filename, ".png", replacementPath, sizeof(replacementPath));
    }
    if (ext == ".WLD" || ext == ".3DT" || ext == ".3DG") {
        return findReplacementAssetPath(filename, ".json", replacementPath, sizeof(replacementPath));
    }
    if (ext == ".3D3") {
        if (!findReplacementAssetPath(filename, ".json", replacementPath, sizeof(replacementPath))) {
            return 0;
        }
        if (structuredJsonHasProperty(replacementPath, "\"model_data\"")) {
            return 1;
        }
        LogWarn((
            "asset replacement: %s has only minimized .3D3 JSON; original .3D3 is still required for shape tables",
            filename
        ));
        return 0;
    }
    if (upperString(legacy.filename().string()) == "F15DGTL.BIN") {
        return findReplacementAssetPath("voice_cue_000_sample0", ".wav", replacementPath, sizeof(replacementPath)) &&
               findReplacementAssetPath("voice_cue_001_sample4", ".wav", replacementPath, sizeof(replacementPath)) &&
               findReplacementAssetPath("voice_cue_002_sample2_variant0", ".wav", replacementPath, sizeof(replacementPath)) &&
               findReplacementAssetPath("voice_cue_003_sample2_variant1", ".wav", replacementPath, sizeof(replacementPath)) &&
               findReplacementAssetPath("voice_cue_004_sample2_variant2", ".wav", replacementPath, sizeof(replacementPath));
    }
    return 0;
}

/* Open an asset for reading. Returns the stream, or NULL on failure. */
SDL_IOStream *openFile(const char *filename, int mode) {
    (void)mode; /* the resident open service only distinguished read vs. write;
                 * every openFile caller in the game opens an asset for reading */
    if (SDL_IOStream *replacement = openStructuredJsonReplacement(filename)) {
        return replacement;
    }
    logPreferredReplacementIfPresent(filename);
    return SDL_IOFromFile(resolveCasePath(filename).c_str(), "rb");
}

/* Create or truncate a file for writing. Returns the stream, or NULL on failure. */
SDL_IOStream *createFile(const char *filename, int attr) {
    (void)attr;
    return SDL_IOFromFile(resolveCasePath(filename).c_str(), "wb");
}

void fileClose(SDL_IOStream *io) {
    if (io) SDL_CloseIO(io);
}

/* fread/fwrite work-alikes over SDL_IOStream: transfer `count` items of `size`
 * bytes and return the number of whole items moved, matching the stdio
 * semantics the game's call sites were written against. */
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *io) {
    if (!io || size == 0) return 0;
    return SDL_ReadIO(io, ptr, size * count) / size;
}

size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *io) {
    if (!io || size == 0) return 0;
    return SDL_WriteIO(io, ptr, size * count) / size;
}

/* Raw read into a host buffer. Shared with the PIC decoder (picimpl.c), which
 * streams the file 512 bytes at a time. Returns bytes read, or -1 on a null
 * stream. A negative count means "read the rest of the stream" (DOS passed
 * cx=0xFFFF; we resolve the exact remaining byte count so the request never
 * exceeds what the file holds, which SDL's backends handle inconsistently). */
int fileReadRaw(SDL_IOStream *io, void *dst, int count) {
    if (!io) return -1;
    if (count < 0) {
        const Sint64 size = SDL_GetIOSize(io);
        const Sint64 pos = SDL_TellIO(io);
        if (size < 0 || pos < 0 || pos >= size) return 0;
        count = (int)(size - pos);
    }
    return (int)SDL_ReadIO(io, dst, (size_t)count);
}

/* Print a '$'-terminated DOS string (INT 21h/09h) to the log. */
void dos_printstring(const char *str) {
    size_t len = 0;
    while (str[len] && str[len] != '$') len++;
    LogInfo(("%.*s", (int)len, str));
}

/* file_error.inc: Print error and exit */
void errorAndExit(const char *msg) {
    dos_printstring(msg);
    SDL_Quit();
    exit(1);
}

bool verifyGameAssets() {
    struct Asset {
        string md5, filename;
    };
    const vector<Asset> assets = {
        { "3e468dbc9dd2c25a5343e384656d4b87", "15flt.3d3" },
        { "82b6b193954a0abba22f5f8267291d14", "1.pic" },
        { "8a8d0d29a6789de4971a5381cb89a60c", "256left.pic" },
        { "b6651ea956bec71890bf90de9a31fb1d", "256pit.pic" },
        { "4c4704170d85e18c842e18b1f438d266", "256rear.pic" },
        { "d18609791beb5a4b6bbc3c95a49979ee", "256right.pic" },
        { "a48a7ec1de1637da2d132a0fab3b4894", "2.pic" },
        { "e488a9127ba89f636fd0151da599ddb1", "3.pic" },
        { "d81029a10c5bcb0705ac98b6cc75621d", "4.pic" },
        { "fad492070c3afb3a11f32ad266df428d", "adv.pic" },
        { "f6b8b7b27b1de44282ca04ea6d369ea4", "armpiece.pic" },
        { "e480cd3510d5895f399e996725a58f6a", "ce.3d3" },
        { "b726a340b000005fdf501d61eeb57bcd", "ce.3dg" },
        { "b082fb4983b97804bff002e5824fbc90", "ce.3dt" },
        { "bacf2b850fcc6b592f92f046c89b15c8", "ceurope.spr" },
        { "df0d81f834eb7d8eaefb6b14b9728244", "ce.wld" },
        { "ff1bba14e570245e94e5f1a6186422a3", "cockpit.pic" },
        { "a490a0bf84b2c36dedab4fee0372bd09", "dbicons.spr" },
        { "6e26bcc7228da3563f17c1c919e14349", "death.pic" },
        { "07ae72e86ae2c38cb7293f86cb108e93", "desk.pic" },
        { "9c66ea28fec0daf5e3f11c28b9d7ffea", "f15dgtl.bin" },
        { "ac3d782b7a7c446dcf3036d532a3dbae", "f15.spr" },
        { "887f5679e3a645b2082503ff20fb1aa5", "gulf.wld" },
        { "537bbf274d79ebaa616f55917d14bf19", "hiscore.pic" },
        { "0dcf2b5bba17badda50bdf4273285e99", "jp.3d3" },
        { "e2201bf2eebd8f12859c41676070c4cd", "jp.3dg" },
        { "210b69b49ea60248ecf229e3a35c7832", "jp.3dt" },
        { "e2252dea49554e9d51482e6ba0a22a5b", "jp.spr" },
        { "c499fd1f955fde158159b770eabbc11e", "jp.wld" },
        { "157e724e8daa31336fe9f06255bd73c4", "labs.pic" },
        { "0b8866ceaad15bc03bd7897bd84c66d5", "lb.3d3" },
        { "d10c1d6cc9ade429e6dd81d9f2efc9de", "lb.3dg" },
        { "73b3bfa86cd57a507a2e1591db45ed76", "lb.3dt" },
        { "789455e28757793fec416cc444477063", "left.pic" },
        { "fb234541a8fd588684c80417eebbf729", "libya.spr" },
        { "34ac7cabf7c200aaffe72cf548978fa8", "libya.wld" },
        { "335931f93a4a5bb88129ea28ced2ae8c", "me.3d3" },
        { "ad0dbc926be9758b623995ee70bb777b", "me.3dg" },
        { "0e5a2dc77f195a8e51976575e7160282", "me.3dt" },
        { "3f75ad96c4d9c61842d58e087394d968", "medal.pic" },
        { "8079c4abfa88c04aad08e82908b7554e", "me.spr" },
        { "1e9407d174654d59a2bd34d5fe052caa", "me.wld" },
        { "f8ec10b3de5dfd555748497cc5f88f63", "nc.3d3" },
        { "2534b73170ae55e00b1ecfa706d5e04e", "nc.3dg" },
        { "ace87d2c23046f0fecf50c04e847ab3c", "nc.3dt" },
        { "a019f6eade84d4d6a2c8034850979c14", "ncape.spr" },
        { "4ecabfb63f451c989a51feca22048e19", "nc.wld" },
        { "fff6a0e3d2d16af739d9a36eb413339f", "persian.spr" },
        { "570679af0f0bb2d44eb16226d557bbed", "pg.3d3" },
        { "4cfaf482d83f9a90fe30734e7a701918", "pg.3dg" },
        { "46ced2bc0128d95674e10521fee61cd4", "pg.3dt" },
        { "5527467253f038e296fc2519606e38aa", "photo.3d3" },
        { "fa3efcb64547c7fc73bc165b4d06bbf2", "promo.pic" },
        { "0bbc6acfeaef8c7988cd5e75aaaf6320", "rear.pic" },
        { "94190aa3fe2f7de2395b15dab9c04a53", "right.pic" },
        { "bd7a9c10dcd06fec62af1071a678ad7f", "title16.pic" },
        { "14c7e302d9ba0b3567196f43b2a914a6", "title640.pic" },
        { "3e59272aa51365d4c5dc156844f3d4e8", "vn.3d3" },
        { "f54adf1df9788e90e3408d8609ecd40e", "vn.3dg" },
        { "31a214ca09ac9c5ea8ab61c2c2358928", "vn.3dt" },
        { "17e00b718ea797eff553ffca1d3ba2bb", "vn.spr" },
        { "ed64b9313161e9889454f33f6816ffc9", "vn.wld" },
        { "0839cb62142b5d3e5058b596ad36fb32", "wall.pic" },
    };
    for (const Asset &a : assets) {
        const string pathStr = resolveCasePath(a.filename.c_str(), true);
        if (pathStr.empty()) {
            if (hasRequiredModernReplacement(a.filename.c_str())) {
                LogInfo(("asset verification: using modern replacement for missing legacy asset %s", a.filename.c_str()));
                continue;
            }
            const string msg = "Could not find asset file " + a.filename
                + " in game directory: " + gamePath.string();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
        if (QuickDigest5::fileToHash(pathStr) != a.md5) {
            if (hasRequiredModernReplacement(a.filename.c_str())) {
                LogInfo(("asset verification: using modern replacement instead of checksum-mismatched legacy asset %s", pathStr.c_str()));
                continue;
            }
            const string msg = "Checksum mismatch for asset file " + pathStr
                + ": expected " + a.md5;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Initialization failed",
                msg.c_str(), NULL);
            return false;
        }
    }
    return true;
}
