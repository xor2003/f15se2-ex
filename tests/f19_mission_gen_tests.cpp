/* Characterization of f19_missionGenerate() (src/f19/stgen.c) — the F-19
 * START-side mission generator: f19_parseWorld(.wld) + f19_parseGridTerrain
 * (.3DG/.3DT quadtree) + f19_runGenerator(target pick, site scoring, comm
 * export). Replaces the throwaway Python replay harness (gen_check.py): each
 * vector here is a real GENLOG capture from the live game — the rngState at
 * genInput plus the produced mission/target records — so the generator is
 * pinned byte-for-byte against observed behavior, not a self-golden.
 *
 * Setup mirrors the printMission case in f19_behavior_tests.cpp:
 * setGamePath(<f19 dir>) + f19_dsegInit() (START image) + comm/game pointers
 * bound to f19_commBase(+0x120E). gameData fields are the START-side GD view:
 * theater +0x38, isCampaignMission +0x3a, flags3c +0x3c (u8), difficulty +0x3e.
 * Outputs read back from the comm block (GameComm view): missionRange +0x2e,
 * missionKind[4] +0x38, missionStat[4] +0x40 — and f19_targets at dseg 0xB946
 * (stride 0x12: kind +0, objIdx +2, siteObj +4, siteIdx +8, name[6] +0xa).
 *
 * Requires the original F-19 asset dir (LIBYA.WLD/LB.3DG/LB.3DT, GULF.WLD/
 * PG.3DG/PG.3DT) — same hardcoded-path convention as f19_behavior_tests.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "inttype.h"
#include "f19/f19seg.h"       /* f19_dsegAt */
#include "headless.h"

extern uint8 f19_commBase[];  /* comm/game shared block */
extern void *f19_commData;
extern void *f19_gameData;
void f19_dsegInit(void);      /* install the START data image (world 0) */
void f19_missionGenerate(void);
bool setGamePath(const char *path);
extern uint32 f19_rngState;   /* START LCG state (orig dseg:0x7a50) */

namespace {

void require(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "f19_mission_gen_tests: %s\n", msg);
        std::exit(1);
    }
}

/* GD/GameComm/Target field views by raw offset (the layouts stgen.c sees). */
constexpr int kGdOff       = 0x120E; /* gameData inside f19_commBase */
constexpr int kGdTheater   = 0x38;
constexpr int kGdCampaign  = 0x3a;
constexpr int kGdFlags3c   = 0x3c;   /* u8 */
constexpr int kGdDiff      = 0x3e;
constexpr int kCommRange   = 0x2e;
constexpr int kCommKind    = 0x38;   /* int16[4] */
constexpr int kCommStat    = 0x40;   /* int16[4] */
constexpr int kTargetsSeg  = 0xB946; /* f19_stSpace.m_f19_targets */
constexpr int kTargetSize  = 0x12;

struct TargetExp { int16 kind, objIdx, siteObj, siteIdx; char name[5]; };
struct Vector {
    uint32 rngState;
    int16 theater, campaign, flags3c, difficulty;
    uint16 missionRange;
    int16 missionKind[4], missionStat[4];
    TargetExp target[2];
};

/* Captured via F19_GENLOG on the live port (theater/campaign/flags3c/diff
 * as logged; rngState = LCG state at f19_missionGenerate entry). */
const Vector kVectors[] = {
    { 0x29e2c0, 0,0,0,0, 0x2300, {1,16,5,0}, {3,1,2,4},
      {{7,21,34,50,"UC92"}, {1,2,34,13,"UC53"}} },
    { 0x2d26bd, 0,0,0,0, 0x21c0, {1,10,9,0}, {3,3,3,4},
      {{5,20,34,48,"TC87"}, {2,2,34,14,"TC97"}} },
    { 0x306aba, 0,0,0,0, 0x2010, {1,16,9,0}, {3,1,3,4},
      {{8,21,34,55,"UC92"}, {1,2,34,21,"VC26"}} },
    { 0x33aeb7, 0,0,0,0, 0x2450, {1,16,9,0}, {3,1,3,4},
      {{6,21,34,52,"UC92"}, {1,2,34,15,"VC37"}} },
    { 0x14941b8, 0,0,0,0, 0x1910, {1,16,5,0}, {3,1,2,4},
      {{6,24,34,52,"VC37"}, {1,2,34,13,"VC16"}} },
    { 0x36f2b4, 0,0,0,0, 0x1f70, {1,16,9,0}, {3,1,3,4},
      {{6,21,34,52,"UC92"}, {1,2,34,25,"VC13"}} },
    { 0x3a36b1, 0,0,0,0, 0x2490, {1,16,5,0}, {3,1,2,4},
      {{8,21,34,55,"UC92"}, {1,2,34,28,"VC37"}} },
    { 0x3d7aae, 0,0,0,0, 0x25e0, {1,16,5,0}, {3,1,2,4},
      {{8,21,34,55,"UC92"}, {1,2,35,21,"VC47"}} },
    { 0x40beab, 0,0,0,0, 0x2330, {1,16,5,0}, {3,1,2,4},
      {{5,21,34,48,"UC92"}, {1,2,34,28,"UC43"}} },
    /* campaign pick on the gulf theater — exercises the flags3c&1
     * findOrPlaceItem target path instead of the world-table pick. */
    { 0x59, 1,1,1,1, 0x1f50, {19,6,5,0}, {1,2,2,4},
      {{3,25,59,3,"JZ67"}, {2,2,52,14,"JZ53"}} },
};

int16 gdRd16(int off) { return *reinterpret_cast<int16 *>(f19_commBase + kGdOff + off); }
void gdWr16(int off, int16 v) { *reinterpret_cast<int16 *>(f19_commBase + kGdOff + off) = v; }
int16 commRd16(int off) { return *reinterpret_cast<int16 *>(f19_commBase + off); }

} // namespace

int main() {
    test_headless_init();
    require(setGamePath("/home/xor/games/f19/F19"), "F-19 asset dir resolves");

    int failures = 0;
    for (size_t vi = 0; vi < sizeof(kVectors) / sizeof(kVectors[0]); ++vi) {
        const Vector &v = kVectors[vi];

        /* Fresh START image + comm block each run: the generator mutates
         * targets/comm state and parseWorld fully re-reads the .wld tables. */
        std::memset(f19_commBase, 0, 0x2000);
        f19_dsegInit();
        f19_commData = f19_commBase;
        f19_gameData = f19_commBase + kGdOff;

        gdWr16(kGdTheater,  v.theater);
        gdWr16(kGdCampaign, v.campaign);
        f19_commBase[kGdOff + kGdFlags3c] = (uint8)v.flags3c;
        gdWr16(kGdDiff,     v.difficulty);
        f19_rngState = v.rngState;

        f19_missionGenerate();

        char msg[256];
        bool ok = true;
        auto chk = [&](bool c, const char *what, long got, long want) {
            if (!c) {
                ok = false;
                std::snprintf(msg, sizeof msg, "  %s: got 0x%lx want 0x%lx",
                              what, got, want);
                std::fprintf(stderr, "vec %zu (state=%x): %s\n", vi, v.rngState, msg);
            }
        };
        chk((uint16)commRd16(kCommRange) == v.missionRange, "missionRange",
            (uint16)commRd16(kCommRange), v.missionRange);
        for (int i = 0; i < 4; ++i) {
            chk(commRd16(kCommKind + i * 2) == v.missionKind[i], "missionKind[i]",
                commRd16(kCommKind + i * 2), v.missionKind[i]);
            chk(commRd16(kCommStat + i * 2) == v.missionStat[i], "missionStat[i]",
                commRd16(kCommStat + i * 2), v.missionStat[i]);
        }
        for (int t = 0; t < 2; ++t) {
            const uint8 *tp = (const uint8 *)f19_dsegAt(kTargetsSeg + t * kTargetSize);
            const TargetExp &e = v.target[t];
            chk(*(const int16 *)(tp + 0) == e.kind,    "target.kind",    *(const int16 *)(tp + 0), e.kind);
            chk(*(const int16 *)(tp + 2) == e.objIdx,  "target.objIdx",  *(const int16 *)(tp + 2), e.objIdx);
            chk(*(const int16 *)(tp + 4) == e.siteObj, "target.siteObj", *(const int16 *)(tp + 4), e.siteObj);
            chk(*(const int16 *)(tp + 8) == e.siteIdx, "target.siteIdx", *(const int16 *)(tp + 8), e.siteIdx);
            if (std::memcmp(tp + 0xa, e.name, 4) != 0) {
                ok = false;
                std::fprintf(stderr, "vec %zu: target%d.name got %.4s want %.4s\n",
                             vi, t, (const char *)(tp + 0xa), e.name);
            }
        }
        if (!ok) ++failures;
    }

    require(failures == 0, "mission generation vectors diverged");
    std::printf("f19_mission_gen_tests: all %zu vectors matched\n",
                sizeof(kVectors) / sizeof(kVectors[0]));
    return 0;
}
