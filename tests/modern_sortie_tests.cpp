/*
 * Modern-backend sortie acceptance: runs the shared scripted sortie under
 * F15_MODERN_MATH with three layers of checking.
 *
 *   1. Modern golden pin — every hashed field, every tick, compared exactly
 *      against tests/goldens/sortie_fields_modern.trace. The modern backend
 *      is deterministic, so any behavioral change shows up as a diff and the
 *      golden is regenerated deliberately (`modern_sortie_tests record`).
 *
 *   2. Player-flight envelope vs fixed — the f.* fields track the fixed
 *      backend's golden within documented tolerances. The scripted inputs are
 *      identical, so the player's trajectory must stay inside the measured
 *      divergence envelope; a dead-end state (e.g. the near-pole knife-edge
 *      park) would leave it.
 *
 *   3. Schedule-driven discrete transitions — cockpit/mission state that the
 *      key schedule drives (gear, eject, autopilot, mission status, damage)
 *      must take the same ordered sequence of values, each transition within
 *      kShiftTicks. Threshold crossings legitimately fire earlier under
 *      fractional speed accumulation (the 350-knot auto-gear-up runs ~20
 *      ticks ahead), but a missing/extra/reordered transition fails.
 *
 * AI/projectile/object internals are intentionally NOT compared against the
 * fixed golden: over a 660-tick chaotic sortie they legitimately decorrelate
 * (a missile follows its launch attitude; at ~t390 the two backends' aircraft
 * already differ by ~1000 altitude units and ~3 deg of pitch). They are
 * pinned by layer 1 instead.
 *
 * --loop runs the pole-fold profile: layer-1 pin against
 * sortie_loop_fields_modern.trace plus the harness's non-degenerate loop
 * assertions. Layers 2/3 are skipped — trajectories decorrelate through the
 * pole band by design, and modern takes the analog input path.
 *
 * --stick runs the real-stick sortie (the profile whose fixed golden is a
 * pre-migration e28b9a4 oracle): layer-1 pin against
 * sortie_stick_fields_modern.trace plus the stick-input assertions.
 *
 * --land runs the recovery-landing profile: layer-1 pin against
 * sortie_land_fields_modern.trace plus the landing assertions (corridor
 * entry, auto-landing engaged, descent, Safe Landing / finalizeMission(0)).
 *
 * --wrap runs the theater-boundary profile: layer-1 pin against
 * sortie_wrap_fields_modern.trace plus the boundary assertions (eastward
 * advance, clamp contact, sustained pin under power, no leak past 0x7e00).
 */
#include "sortie_harness.hpp"

#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <unordered_map>

namespace {
using namespace sortie;

/* Timing window for discrete transitions: the measured worst case is the
 * 350-knot gear auto-retract firing ~20 ticks early under fractional speed
 * accumulation; 64 leaves margin without masking a missing or doubled event. */
constexpr int kShiftTicks = 64;

std::map<std::string, std::uint32_t> parseLine(const std::string &line, int &tick) {
    std::map<std::string, std::uint32_t> fields;
    std::istringstream in(line);
    in >> tick;
    std::string kv;
    while (in >> kv) {
        const auto eq = kv.find('=');
        if (eq != std::string::npos)
            fields[kv.substr(0, eq)] = std::stoul(kv.substr(eq + 1));
    }
    return fields;
}

/* Measured |modern - fixed| maxima on this scripted sortie, tolerated ~2x:
 *   fine pos 2838, alt units 2013, attitude words 612, speed units 697,
 *   knots 26, corner 4, stall 103, lift/trim 67, thrust 19, gees 1,
 *   control words 2, hud view 89. */
std::uint32_t envelopeTolerance(const std::string &name) {
    static const std::pair<const char *, std::uint32_t> table[] = {
        {"f.fineX", 8192}, {"f.fineY", 8192},
        {"f.viewZ", 4096}, {"f.alt", 4096}, {"f.apAlt", 4096},
        {"f.speed", 1500}, {"f.stall", 256}, {"f.corner", 16},
        {"f.knots", 64}, {"f.thrust", 64}, {"f.gees", 8},
        {"f.rollIn", 16}, {"f.pitchIn", 16},
        {"f.viewX", 256}, {"f.viewY", 256},
        {"f.fuel", 64},
    };
    for (const auto &e : table)
        if (name == e.first) return e.second;
    return 0;
}

bool isAngleField(const std::string &name) {
    return name == "f.head" || name == "f.pitch" || name == "f.roll" ||
           name == "f.lift" || name == "f.trim";
}
constexpr std::uint32_t kAngleTolerance = 2048; /* ~11 deg of wrap-safe arc */

/* Discrete fields driven by the schedule rather than the chaotic sim state. */
bool isScheduleDiscrete(const std::string &name) {
    return name == "f.gear" || name == "f.eject" || name == "f.ap" ||
           name == "f.flags" || name == "f.damage" || name == "f.gunHits" ||
           name == "f.setThrust" || name == "m.status" || name == "m.ended" ||
           name == "m.slow" || name == "m.corr" || name == "m.landT2";
}

std::uint32_t wrapDistance(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t d = a > b ? a - b : b - a;
    return std::min(d, 65536u - d);
}

int run(bool record, const char *recordPath, Profile profile) {
    const bool loop = profile == Profile::kLoop;
    const bool combat = profile == Profile::kCombat;
    const bool stick = profile == Profile::kStick;
    const bool land = profile == Profile::kLand;
    const bool wrap = profile == Profile::kWrap;
    /* The fixed envelope/discrete layers are sortie-only: the loop decorrelates
     * trajectories through the pole band, combat through AI retargeting,
     * the stick sortie through sustained deflection, the recovery leg
     * through approach-geometry drift, and the boundary run through
     * pre-clamp fine-position drift — all pin against their own modern
     * golden only. */
    const bool pinnedOnly = loop || combat || stick || land || wrap;
    const int ticks = ticksForProfile(profile);
    std::ofstream modernGolden;
    std::ifstream modernIn, fixedIn;
    const char *goldenName = loop ? "sortie_loop_fields_modern.trace"
                           : combat ? "sortie_combat_fields_modern.trace"
                           : stick ? "sortie_stick_fields_modern.trace"
                           : land ? "sortie_land_fields_modern.trace"
                           : wrap ? "sortie_wrap_fields_modern.trace"
                                    : "sortie_fields_modern.trace";
    if (record) {
        modernGolden.open(recordPath, std::ios::binary | std::ios::trunc);
        require(modernGolden.good(), "cannot open modern golden output");
        modernGolden << "sortie_fields_modern 1 seed " << kSeed << " ticks " << ticks << "\n";
    } else {
        modernIn.open((std::string(F15_GOLDEN_DIR) + "/" + goldenName).c_str(), std::ios::binary);
        require(modernIn.good(), "cannot open modern field golden (record with 'record <file> --loop|--combat|--stick')");
        if (!pinnedOnly) {
            fixedIn.open(F15_GOLDEN_DIR "/sortie_fields.trace", std::ios::binary);
            require(fixedIn.good(), "cannot open sortie_fields.trace");
        }
        std::string header;
        require(std::getline(modernIn, header).good() &&
                header.rfind("sortie_fields_modern 1", 0) == 0,
                "modern field golden header mismatch");
        if (!pinnedOnly)
            require(std::getline(fixedIn, header).good() &&
                    header.rfind("sortie_fields_trace 1", 0) == 0,
                    "fixed field golden header mismatch");
    }

    int pinFails = 0, envelopeFails = 0;
    std::map<std::string, std::uint64_t> maxDiff;
    std::map<std::string, int> maxTick;
    using Transitions = std::vector<std::pair<int, std::uint32_t>>;
    std::unordered_map<std::string, Transitions> fixedTrans, modernTrans;
    std::unordered_map<std::string, std::uint32_t> lastFixed, lastModern;
    LoopCheck loopCheck;
    CombatCheck combatCheck;
    StickCheck stickCheck;
    LandingCheck landCheck;
    BoundaryCheck boundaryCheck;

    for (int tick = 0; tick < ticks; ++tick) {
        runTick(tick, profile);
        const auto snap = snapFields();
        if (loop) loopObserve(loopCheck, tick);
        if (combat) combatObserve(combatCheck, tick);
        if (stick) stickObserve(stickCheck, tick);
        if (land) landingObserve(landCheck, tick);
        if (wrap) boundaryObserve(boundaryCheck, tick);

        if (record) {
            modernGolden << tick;
            for (const auto &f : snap) modernGolden << ' ' << f.first << '=' << f.second;
            modernGolden << '\n';
            continue;
        }

        std::string modernLine, fixedLine;
        require(std::getline(modernIn, modernLine).good(), "modern field golden ended early");
        int modernTick;
        const auto pinned = parseLine(modernLine, modernTick);
        require(modernTick == tick, "modern field golden tick order mismatch");
        std::map<std::string, std::uint32_t> expected;
        int fixedTick = tick;
        if (!pinnedOnly) {
            require(std::getline(fixedIn, fixedLine).good(), "fixed field golden ended early");
            expected = parseLine(fixedLine, fixedTick);
            require(fixedTick == tick, "fixed field golden tick order mismatch");
        }

        for (const auto &fv : snap) {
            /* Layer 1: modern golden pin — exact. */
            const auto pi = pinned.find(fv.first);
            require(pi != pinned.end(), "field missing from modern golden");
            if (pi->second != fv.second) {
                if (++pinFails <= 10)
                    std::fprintf(stderr, "tick %d %s: modern=%u golden=%u\n",
                                 tick, fv.first.c_str(), fv.second, pi->second);
            }

            /* The fixed envelope/discrete layers are sortie-only: the loop
             * decorrelates trajectories through the pole band by design, and
             * combat through AI retargeting — both pin against their own
             * modern golden only. */
            if (pinnedOnly) continue;
            const auto fi = expected.find(fv.first);
            require(fi != expected.end(), "field missing from fixed golden");

            /* Layer 2/3 dispatch. */
            if (isScheduleDiscrete(fv.first)) {
                auto &ft = fixedTrans[fv.first];
                auto &lf = lastFixed[fv.first];
                if (tick == 0 || fi->second != lf) ft.emplace_back(tick, fi->second);
                lf = fi->second;
                auto &mt = modernTrans[fv.first];
                auto &lm = lastModern[fv.first];
                if (tick == 0 || fv.second != lm) mt.emplace_back(tick, fv.second);
                lm = fv.second;
            } else if (isAngleField(fv.first) || envelopeTolerance(fv.first) > 0) {
                const std::uint64_t d = isAngleField(fv.first)
                    ? wrapDistance(fv.second, fi->second)
                    : (std::uint64_t)std::llabs((std::int64_t)(std::int32_t)fv.second -
                                                (std::int64_t)(std::int32_t)fi->second);
                if (d > maxDiff[fv.first]) { maxDiff[fv.first] = d; maxTick[fv.first] = tick; }
                const std::uint32_t tol = isAngleField(fv.first)
                    ? kAngleTolerance : envelopeTolerance(fv.first);
                if (d > tol) {
                    ++envelopeFails;
                    std::fprintf(stderr,
                                 "tick %d %s: modern=%u fixed=%u diff=%llu > tol=%u\n",
                                 tick, fv.first.c_str(), fv.second, fi->second,
                                 (unsigned long long)d, tol);
                }
            }
        }
    }
    if (loop) loopRequire(loopCheck);
    if (stick) stickRequire(stickCheck);
    if (combat) {
        /* Modern decorrelates from the fixed engagement — killSeen is observed
         * but not required (the fixed parity run pins the verified kill). */
        combatRequire(combatCheck, false);
        verifyWorldExport();
    }
    if (land) {
        landingRequire(landCheck);
        verifyWorldExport();
    }
    if (wrap) boundaryRequire(boundaryCheck);
    if (record) {
        require(modernGolden.good(), "modern golden write failed");
        std::fprintf(stderr, "recorded %d modern field ticks to %s\n", ticks, recordPath);
        return 0;
    }
    {
        std::string extra;
        require(!std::getline(modernIn, extra).good() &&
                (pinnedOnly || !std::getline(fixedIn, extra).good()), "field golden has extra ticks");
    }

    int discreteFails = 0;
    for (const auto &[name, ft] : fixedTrans) {
        const auto &mt = modernTrans[name];
        if (ft.size() != mt.size()) {
            ++discreteFails;
            std::fprintf(stderr, "%s: %zu fixed transitions vs %zu modern\n",
                         name.c_str(), ft.size(), mt.size());
            continue;
        }
        for (std::size_t i = 0; i < ft.size(); ++i) {
            if (ft[i].second != mt[i].second ||
                std::abs(ft[i].first - mt[i].first) > kShiftTicks) {
                ++discreteFails;
                std::fprintf(stderr,
                             "%s: transition %zu fixed(t=%d -> %u) modern(t=%d -> %u)\n",
                             name.c_str(), i, ft[i].first, ft[i].second,
                             mt[i].first, mt[i].second);
            }
        }
    }

    if (pinFails || envelopeFails || discreteFails) {
        std::fprintf(stderr, "modern sortie: %d pin + %d envelope + %d discrete failures\n",
                     pinFails, envelopeFails, discreteFails);
        return 1;
    }
    if (loop) {
        std::printf("modern loop: %d ticks pinned; pole band traversed "
                    "(max pitch magnitude %u words, altitude range %u)\n",
                    ticks, loopCheck.maxPitchMag,
                    loopCheck.altMax - loopCheck.altMin);
        return 0;
    }
    if (combat) {
        std::printf("modern combat: %d ticks pinned; mission import, AI "
                    "maneuver, alert and weapon paths all engaged\n",
                    ticks);
        return 0;
    }
    if (stick) {
        std::printf("modern stick: %d ticks pinned; stick input verified live "
                    "in both axes\n", ticks);
        return 0;
    }
    if (land) {
        std::printf("modern land: %d ticks pinned; corridor, auto-landing and "
                    "Safe Landing verified\n", ticks);
        return 0;
    }
    if (wrap) {
        std::printf("modern wrap: %d ticks pinned; theater bound held under "
                    "power\n", ticks);
        return 0;
    }
    std::printf("modern sortie: %d ticks pinned, envelope ok; worst vs fixed:\n", ticks);
    for (const char *k : {"f.fineX", "f.fineY", "f.alt", "f.head", "f.roll", "f.knots"})
        std::printf("  %-8s max %llu (tick %d)\n", k,
                    (unsigned long long)maxDiff[k], maxTick[k]);
    return 0;
}
}

int main(int argc, char **argv) {
    const bool loop = argc >= 2 && std::string(argv[argc - 1]) == "--loop";
    if (loop) --argc;
    const bool combat = argc >= 2 && std::string(argv[argc - 1]) == "--combat";
    if (combat) --argc;
    const bool stick = argc >= 2 && std::string(argv[argc - 1]) == "--stick";
    if (stick) --argc;
    const bool land = argc >= 2 && std::string(argv[argc - 1]) == "--land";
    if (land) --argc;
    const bool wrap = argc >= 2 && std::string(argv[argc - 1]) == "--wrap";
    if (wrap) --argc;
    const bool record = argc == 3 && std::string(argv[1]) == "record";
    require(record || argc == 1,
            "usage: modern_sortie_tests [record <out.trace>] [--loop | --combat | --stick | --land | --wrap]");
    const Profile profile = loop ? Profile::kLoop
                               : combat ? Profile::kCombat
                                        : stick ? Profile::kStick
                                                : land ? Profile::kLand
                                                       : wrap ? Profile::kWrap : Profile::kSortie;
    initSortie(profile);
    const int result = run(record, record ? argv[2] : nullptr, profile);
    teardown();
    return result;
}
