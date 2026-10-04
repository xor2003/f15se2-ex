// F-19 (f19eg_* / f19_*) ported-routine behavior tests (LINK_CORE + headless).
//
// Exercises the F-19 EGAME + START ports against the linked core library:
// seg000 math helpers (sinMul/cosMul/computeBearing/rangeApprox/clamps/isqrt/
// rng), the attitude-angle extraction chain (signedRatio16/valueToAngle/
// complementAngle + buildRotationMatrixFar round-trip), the 3DG tile-map
// routines (scaleCoordToLod/process3dg/worldToTileIndex/computeTileBounds/
// addTileEntry/lookupTileEntry), the mission-clock formatter, and the START
// mission-generator helpers (calcBearing/rangeApprox/clampValue/
// formatGridRef/formatTimeStr).
//
// Globals the routines read through f19_dseg are poked at the ORIGINAL DOS
// dseg cells (verified against the dseg: offset column in
// lst/egame_en_ada.lst), so the tests pin the port's data-cell bindings as
// well as its code: a logical variable bound to the wrong cell fails loudly.
#include "inttype.h"
#include "dos.h"
#include "headless.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

// ---- F-19 flat-model state -------------------------------------------------
extern uint8 f19_dseg[];                 /* 64 KB flat data segment */
#include "f19/f19egglobals.h"            /* binding-pin tests: &g_* == true cell */
extern uint8 f19_commBase[];             /* comm/game shared block */
extern void *f19_commData;
extern void *f19_gameData;
void f19_egDsegLoad(void);               /* install EGAME dseg image */

// Shared (unprefixed) globals the F-19 routines use.
extern int16 g_viewCenterX;
extern int16 g_viewCenterY;
extern int16 g_clipMaxX;
extern int16 g_clipMaxY;
extern const int16 g_angleLut[];

// ---- F-19 START-side routines under test ----------------------------------
void f19_dsegInit(void);                  /* install the START data image */
void f19_printMission(void);              /* stutil.c: briefing -> state 4 */
int16 far ovlF43_a(int16 v);              /* scenery0.exe image loader */
void  far ovlF43_10d(int16 seg);          /* string-record -> dseg ptr splat */
#undef commData
#undef gameData
/* f19_commData/f19_gameData (the globals stutil/stmain's commData/gameData
 * macros resolve to) are already externed above. */
bool setGamePath(const char *path);       /* shared/file_io.c */

// ---- F-19 EGAME routines (f19egpfx.h renames in-TU refs to f19eg_*) --------
int16 f19eg_sinMul(int16 angle, int16 value);
int16 f19eg_cosMul(int16 angle, int16 value);
int16 f19eg_computeBearing(int16 deltaX, int16 deltaY);
int16 f19eg_rangeApprox(int16 deltaX, int16 deltaY);
int16 f19eg_clampRange(int16 value, int16 minVal, int16 maxVal);
int16 f19eg_clampValue(int16 value, int16 minVal, int16 maxVal);
int16 f19eg_signExtendByte(int16 v);
int16 f19eg_signOf(int16 value);
int16 f19eg_isqrt(int16 value);
int16 f19eg_randomRange(int16 maxVal);
uint16 f19eg_signedRatio16(int16 numerator, int16 denominator);
int16 f19eg_valueToAngle(int16 value);
int16 f19eg_complementAngle(int16 value);
void f19eg_computeAttitudeAngles(void);
uint32 f19eg_scaleCoordToLod(int16 level, uint32 coord);
int16 f19eg_process3dg(int16 lod, int16 col, int16 row);
int16 f19eg_lookupTileEntry(int16 lod, int16 subIndex, int16 tileX, int16 tileY);
void f19eg_worldToTileIndex(int16 worldX, int16 worldY, int16 *outCol, int16 *outRow);
void f19eg_computeTileBounds(int16 *minTileX, int16 *maxTileX, int16 *minTileY, int16 *maxTileY);
int16 f19eg_aspectScaleY(int16 y);
void f19eg_formatMissionClock(uint16 time);

// Shared, unprefixed.
int buildRotationMatrixFar(int16 *matrix, int angleX, int angleY, int angleZ);

#pragma pack(push, 1)
// Tag names must match eg3dmap.c and stay at global scope — C++
// name-mangles them into f19eg_addTileEntry's signature.
struct TileSceneObject {
    int16 x, y, z;
    uint8 shape;
};
struct TileObject {
    int16 id;
    int16 dist;
    int32 x;
    int32 y;
    TileSceneObject *entry;
    uint8 lod;
    uint8 subIndex;
    uint8 tileX;
    uint8 tileY;
    int16 shapeOff;
    uint8 flag;
    uint8 pad15;
};
#pragma pack(pop)
void f19eg_addTileEntry(TileObject *rec, int16 value, char tag);

// ---- F-19 START (mission-generator) routines -------------------------------
int16 f19_calcBearing(int16 dx, int16 dy);
int16 f19_rangeApprox(int16 deltaX, int16 deltaY);
int16 f19_clampValue(int16 v, int16 lo, int16 hi);
char *f19_formatGridRef(int16 wx, int16 wy, int16 theater);
void f19_formatTimeStr(char *buf, int16 v);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, or loop mechanics.
enum F19OriginalConstant : int {
    kAngleQuarterTurn = 0x4000,
    kAngleHalfTurn = 0x8000,
    kAngleThreeQuarterTurn = 0xC000,
    kRangeMax = 0x7FFF,

    // dseg cells for the tile-map routines (word_35010..word_3501C region and
    // the object-local pair), verified in lst/egame_en_ada.lst.
    kCellTileEntryIdx = 0x6320,   // word_35010: lookupTileEntry loop index
    kCellTileWorldSize = 0x6322,  // word_35012: 0x1000 >> tileZoomShift
    kCellTileGridDim = 0x6324,    // word_35014: 4 << (8 - lod*2)
    kCellMapOriginX = 0x6326,     // word_35016: drawMapTiles arg0 >> zoom
    kCellMapOriginY = 0x6328,     // word_35018: drawMapTiles arg1 >> zoom
    kCellObjLocalX = 0xAA4,       // word_2F794: in-tile object offset X
    kCellObjLocalY = 0xAA6,       // word_2F796: in-tile object offset Y

    kCellDynTileEntries = 0x8B56, // g_dynTileEntries: 8-byte override records
    kCellTileEntryCount = 0x664A, // dword_3533A (low word): g_tileEntryCount
    kCellTopLodGrid = 0x7F6E,     // 8x8 LOD-4 grid
    kCellBuf1_3dg = 0x6ECC,       // 16x16 LOD-3 grid
    kCellBuf2_3dg = 0x6C76,       // LOD-2 child grids (512 B)
    kCellBuf3_3dg = 0x6870,       // LOD-1 child grids (512 B)
    kCellBuf4_3dg = 0x666C,       // LOD-0 leaf grids (512 B)
    kCellOrientMatrix = 0x46A6,   // g_orientMatrix: 9 int16 rotation matrix
    kCellOurHead = 0x4700,        // g_ourHead
    kCellOurPitch = 0x4702,       // g_ourPitch
    kCellOurRoll = 0x4704,        // g_ourRoll
    kCellNameBuf = 0x65E6,        // g_nameBuf: clock string scratch
    kCellMissionTick = 0x6650,    // g_missionTick
    kCellNightMode = 0x4F1A,      // g_nightMode
    kCellBufCoordStr = 0x98CC,    // START grid-ref scratch
    kCellMissionTimeFlag = 0x44E4,// START missionTimeFlag
    kOffGameDataTheater = 0x38,   // GD.theater inside the gameData block

    kLodGridDimLod4 = 8,          // top grid is 8x8
    kLodGridDimLod3 = 16,         // buf1 grid is 16x16
    kTileEntryValue = 0x2345,
    kTileEntryShape = 0x22,
    kAttitudeMatrixWords = 9,
    kAttitudeTolerance = 0x40,    // ~1.4 deg of LUT quantization headroom
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

int16 dseg16(int off) { return *reinterpret_cast<int16 *>(f19_dseg + off); }
void setDseg16(int off, int v) { *reinterpret_cast<int16 *>(f19_dseg + off) = v; }

int sar32(int32 value, int count) {
    return value >= 0
        ? static_cast<int>(static_cast<uint32>(value) >> count)
        : -static_cast<int>((static_cast<uint32>(-value) + ((1u << count) - 1u)) >> count);
}

// Independent model of the original bearing curve (seg000:0xd29d).
int expectedBearing(int deltaX, int deltaY) {
    if (deltaX == 0) return deltaY > 0 ? 0 : kAngleHalfTurn;
    if (deltaY == 0) return deltaX > 0 ? kAngleQuarterTurn : kAngleThreeQuarterTurn;
    const int absX = std::abs(deltaX);
    const int absY = std::abs(deltaY);
    const bool swapped = absX > absY;
    const int32 numer = static_cast<int32>(swapped ? absY : absX) << 14;
    const int denom = swapped ? absX : absY;
    const int ratio = static_cast<int>(numer / denom);
    const int angle = static_cast<int16>(
        ((0x2800L - (((int32)std::abs(0x1333 - ratio) * 0xB00L) >> 14)) *
         static_cast<int32>(ratio)) >> 14);
    if (deltaX > 0) {
        return static_cast<int16>(deltaY > 0 ? (swapped ? kAngleQuarterTurn - angle : angle)
                              : (swapped ? angle + kAngleQuarterTurn : kAngleHalfTurn - angle));
    }
    return static_cast<int16>(deltaY > 0 ? (swapped ? angle + kAngleThreeQuarterTurn : -angle)
                          : (swapped ? kAngleThreeQuarterTurn - angle : angle + kAngleHalfTurn));
}

int expectedRangeApprox(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    const long dist = dx > dy ? static_cast<long>(dy >> 1) + dx
                              : static_cast<long>(dx >> 1) + dy;
    return dist > kRangeMax ? kRangeMax : static_cast<int>(dist);
}

// 8-byte override record at f19_dseg + 0x8B56 (struct DynTileOverride).
void writeTileEntry(int idx, int lod, int subIndex, int tileX, int tileY,
                    int16 value, int shape) {
    uint8 *e = f19_dseg + kCellDynTileEntries + idx * 8;
    e[0] = static_cast<uint8>(lod);
    e[1] = static_cast<uint8>(subIndex);
    e[2] = static_cast<uint8>(tileX);
    e[3] = static_cast<uint8>(tileY);
    *reinterpret_cast<int16 *>(e + 4) = value;
    e[6] = static_cast<uint8>(shape);
    e[7] = 0;
}

} // namespace

int main() {
    test_headless_init();
    f19_egDsegLoad();   /* install the EGAME data image (LOD dims, LUTs) */
    // Dummy video also brings up the event subsystem the START briefing test
    // feeds keys through (input_pumpEvents reads SDL events).
    require(SDL_Init(SDL_INIT_VIDEO), "SDL initializes headless");

    // ==== egmath.c: sinMul / cosMul (Q15 sin * value, rounded) ====
    require(f19eg_sinMul(0, 0x4000) == 0 && f19eg_cosMul(0, 0x4000) == 0x4000,
            "sinMul(0)=0, cosMul(0)=identity");
    require(f19eg_sinMul(kAngleQuarterTurn, 0x4000) == 0x4000 &&
            f19eg_cosMul(kAngleQuarterTurn, 0x4000) == 0,
            "sinMul(90deg) preserves, cosMul(90deg) zeroes");
    require(f19eg_sinMul(0x2000, 0x4000) == 0x2D41 &&
            f19eg_cosMul(0x2000, 0x4000) == 0x2D41,
            "45deg sin/cos goldens");
    require(f19eg_sinMul(0x0800, 0x4000) == 0x0C7D &&
            f19eg_sinMul(0x1555, 0x4000) == 0x1FFF &&
            f19eg_sinMul(0x2AAA, 0x4000) == 0x376C,
            "interpolated sin goldens");
    require(f19eg_cosMul(0x8000, 0x4000) == -0x3FFF &&
            f19eg_sinMul(0xC000, 0x4000) == -0x3FFF,
            "negative-side sin/cos goldens");
    require(f19eg_sinMul(0x2AAA, 0x1234) == 0x0FC3 &&
            f19eg_cosMul(0x1555, 0x1234) == 0x0FC4 &&
            f19eg_cosMul(0x0800, 0x1234) == 0x11DA,
            "non-unit value scaling goldens");
    require(f19eg_sinMul(0x2AAA, -0x2000) == -0x1BB6,
            "negative value sign propagation");

    // ==== egmath.c: computeBearing (approx atan2, curve 0x1333/0xB00) ====
    require(f19eg_computeBearing(0, 5) == 0 &&
            f19eg_computeBearing(0, -5) == (int16)kAngleHalfTurn &&
            f19eg_computeBearing(5, 0) == kAngleQuarterTurn &&
            f19eg_computeBearing(-5, 0) == (int16)kAngleThreeQuarterTurn,
            "computeBearing axis cases");
    for (auto d : {std::pair{100, 100}, {100, -100}, {-100, 100}, {-100, -100},
                   {300, 100}, {100, 300}, {-300, 100}, {1, 1000}, {1000, 1}, {7, 7}}) {
        require(f19eg_computeBearing(d.first, d.second) ==
                    (int16)expectedBearing(d.first, d.second),
                "computeBearing matches original curve");
    }
    require(f19eg_computeBearing(100, 100) == 0x204D &&
            f19eg_computeBearing(300, 100) == 0x32CA &&
            f19eg_computeBearing(-300, 100) == (int16)0xCD36,
            "computeBearing hard goldens");

    // ==== egmath.c: rangeApprox / clamps / sign helpers / isqrt / rng ====
    require(f19eg_rangeApprox(3, 4) == 5 && f19eg_rangeApprox(0, 0) == 0 &&
            f19eg_rangeApprox(-300, -400) == 550 &&
            f19eg_rangeApprox(0x7000, 0x7000) == kRangeMax,
            "rangeApprox max+min/2 with 0x7FFF cap");
    require(f19eg_clampRange(5, 0, 10) == 5 && f19eg_clampRange(50, 0, 10) == 10 &&
            f19eg_clampRange(-1, 0, 10) == 0 &&
            f19eg_clampRange((int16)0xC001, 0, 10) == 0 &&
            f19eg_clampRange((int16)0xC000, 0, 10) == 10 &&
            f19eg_clampRange((int16)0x8000, 0, 10) == 10,
            "clampRange incl. 0xC000 wrap-to-high rule");
    require(f19eg_clampValue(5, 0, 10) == 5 && f19eg_clampValue(-1, 0, 10) == 0 &&
            f19eg_clampValue(50, 0, 10) == 10 &&
            f19eg_clampValue((int16)0xC000, 0, 10) == 0,
            "clampValue plain clamp");
    require(f19eg_signExtendByte(0x7F) == 0x7F &&
            f19eg_signExtendByte(0x80) == -0x80 &&
            f19eg_signExtendByte(0xFF) == -1 &&
            f19eg_signExtendByte(0x1FF) == -1,
            "signExtendByte wraps low byte");
    require(f19eg_signOf(0) == 0 && f19eg_signOf(7) == 1 && f19eg_signOf(-7) == -1,
            "signOf");
    require(f19eg_isqrt(0) == 1 && f19eg_isqrt(3) == 1 && f19eg_isqrt(4) == 2 &&
            f19eg_isqrt(0x4000) == 0x80 && f19eg_isqrt(-0x4000) == 0x80,
            "isqrt Newton iteration");
    {
        // randomRange pulls one rand() per call; pre-draw the sequence then
        // reseed so the impl draws the same values.
        std::srand(4242);
        const int r1 = std::rand();
        const int r2 = std::rand();
        std::srand(4242);
        require(f19eg_randomRange(0x100) ==
                    (int16)((static_cast<int32>(r1) * 0x100) >> 15) &&
                f19eg_randomRange(0x40) ==
                    (int16)((static_cast<int32>(r2) * 0x40) >> 15) &&
                f19eg_randomRange(0) == 0,
                "randomRange consumes rand() stream, scales to max");
    }

    // ==== egflight.c: signedRatio16 / valueToAngle / complementAngle ====
    require((int16)f19eg_signedRatio16(0x2000, 0x7FFF) == 0x2000 &&
            (int16)f19eg_signedRatio16(0x1000, 0x4000) == 0x2000 &&
            (int16)f19eg_signedRatio16(-0x1000, 0x4000) == -0x2000 &&
            (int16)f19eg_signedRatio16(0x1000, -0x4000) == -0x2000 &&
            (int16)f19eg_signedRatio16(1, 3) == 0x2AAA,
            "signedRatio16 magnitude+sign");
    require(f19eg_signedRatio16(0x7FFF, 0x7FFF) == 0x8000,
            "signedRatio16 ratio 1.0 wraps to 0x8000 (16-bit limit)");
    require(f19eg_valueToAngle(0) == 0 && f19eg_valueToAngle(0x7FFF) == 0x4000 &&
            f19eg_valueToAngle(-0x7FFF) == -0x4000 &&
            f19eg_valueToAngle((int16)0x8000) == (int16)0xC000,
            "valueToAngle endpoints");
    require(f19eg_valueToAngle(0x324) == 0x0100 &&
            f19eg_valueToAngle(0x2000) == 0x0A4B &&
            f19eg_valueToAngle(0x4000) == 0x1555 &&
            f19eg_valueToAngle(0x5A82) == 0x2000 &&
            f19eg_valueToAngle(-0x2000) == -0x0A4B,
            "valueToAngle LUT interpolation goldens");
    require(f19eg_complementAngle(0) == 0x4000 &&
            f19eg_complementAngle(0x7FFF) == 0 &&
            f19eg_complementAngle(0x5A82) == 0x2000,
            "complementAngle = 90deg - asin");

    // ==== egflight.c: attitude round-trip through the rotation matrix ====
    {
        int16 *m = reinterpret_cast<int16 *>(f19_dseg + kCellOrientMatrix);
        for (auto [h, p, r] : {std::tuple{0x4000, 0x0000, 0x0000},
                               std::tuple{0x8400, 0x0000, 0x0000},
                               std::tuple{0x1234, 0x0400, 0x0000},
                               std::tuple{0x2000, 0x0800, 0x0400},
                               std::tuple{-0x3000, -0x0400, 0x1000}}) {
            buildRotationMatrixFar(m, h, p, r);
            f19eg_computeAttitudeAngles();
            // int16-wrapped angular deltas: 0x8400 == -0x7C00 mod 0x10000.
            const int dh = std::abs(static_cast<int16>(dseg16(kCellOurHead) - h));
            const int dp = std::abs(static_cast<int16>(dseg16(kCellOurPitch) - p));
            const int dr = std::abs(static_cast<int16>(dseg16(kCellOurRoll) - r));
            require(dh <= kAttitudeTolerance && dp <= kAttitudeTolerance &&
                        dr <= kAttitudeTolerance,
                    "matrix->angles round-trip recovers head/pitch/roll");
        }
    }

    // ==== eg3dmap.c: scaleCoordToLod (rounded LOD shifts) ====
    require(f19eg_scaleCoordToLod(4, 0x10000u) == ((0x10000u + 0x20u) >> 6) &&
            f19eg_scaleCoordToLod(3, 0x10000u) == ((0x10000u + 8u) >> 4) &&
            f19eg_scaleCoordToLod(2, 0x10000u) == ((0x10000u + 2u) >> 2) &&
            f19eg_scaleCoordToLod(1, 0x10000u) == 0x10000u &&
            f19eg_scaleCoordToLod(0, 0x10000u) == 0x20000u,
            "scaleCoordToLod rounded LOD shifts");

    // ==== eg3dmap.c: process3dg recursive tile-grid traversal ====
    for (int i = 0; i < 0x40; ++i) f19_dseg[kCellTopLodGrid + i] = 0;
    for (int i = 0; i < 0x100; ++i) f19_dseg[kCellBuf1_3dg + i] = 0;
    for (int i = 0; i < 0x200; ++i) {
        f19_dseg[kCellBuf2_3dg + i] = 0;
        f19_dseg[kCellBuf3_3dg + i] = 0;
        f19_dseg[kCellBuf4_3dg + i] = 0;
    }
    f19_dseg[kCellTopLodGrid + (1 + 2) + ((-1 + 2) << 3)] = 3;
    f19_dseg[kCellBuf1_3dg + 6 + (7 << 4)] = 3;
    f19_dseg[kCellBuf2_3dg + 1 + ((2 << 2) + (3 << 4))] = 3;
    f19_dseg[kCellBuf3_3dg + 3 + ((1 << 2) + (3 << 4))] = 3;
    f19_dseg[kCellBuf4_3dg + 2 + ((3 << 2) + (3 << 4))] = 9;
    require(f19eg_process3dg(4, 1, -1) == 3,
            "process3dg LOD 4 applies +2 top-grid offset");
    require(f19eg_process3dg(3, 6, 7) == 3, "process3dg LOD 3 reads buf1 grid");
    require(f19eg_process3dg(2, 25, 30) == 3,
            "process3dg LOD 2 recurses through LOD 3 parent");
    require(f19eg_process3dg(1, 103, 121) == 3,
            "process3dg LOD 1 recurses through LOD 2 parent");
    require(f19eg_process3dg(0, 414, 487) == 9,
            "process3dg LOD 0 recurses through LOD 1 parent");
    require(f19eg_process3dg(4, -3, 0) == 0 && f19eg_process3dg(0, 0x400, 0) == 0,
            "process3dg rejects out-of-bounds coordinates");

    // ==== eg3dmap.c: lookupTileEntry dynamic-override scan ====
    // g_tileEntryIdx is word_35010 at 0x6320; the port must not alias it
    // onto g_tileGridDim (word_35014 at 0x6324) or any other cell.
    setDseg16(kCellTileEntryCount, 2);
    setDseg16(kCellTileEntryIdx, 0);
    writeTileEntry(0, 1, 2, 3, 4, kTileEntryValue, kTileEntryShape);
    writeTileEntry(1, 5, 6, 7, 8, 0x4567, 0x33);
    require(f19eg_lookupTileEntry(1, 2, 3, 4) == kTileEntryValue,
            "lookupTileEntry finds matching override");
    require(f19eg_lookupTileEntry(9, 2, 3, 4) == 0,
            "lookupTileEntry misses nonmatching lod");
    require(dseg16(kCellTileEntryIdx) == -1,
            "lookupTileEntry leaves loop index -1 in word_35010 (0x6320)");

    // ==== eg3dmap.c: addTileEntry appends the 8-byte override ====
    // g_tileEntryCount is the low word of dword_3533A (0x664A) in the
    // original — the port must not park it (or g_gees) on a sibling cell.
    {
        TileSceneObject sceneObj = {1, 2, 3, 0x41};
        TileObject rec = {};
        rec.id = 0x111;
        rec.dist = 0x222;
        rec.entry = &sceneObj;
        rec.lod = 2;
        rec.subIndex = 5;
        rec.tileX = 6;
        rec.tileY = 7;
        rec.pad15 = 0xEE;
        setDseg16(kCellTileEntryCount, 2);
        f19eg_addTileEntry(&rec, kTileEntryValue, 0x5A);
        require(rec.shapeOff == kTileEntryValue && rec.flag == 0x5A &&
                dseg16(kCellTileEntryCount) == 3,
                "addTileEntry fills rec + bumps dword_3533A counter");
        const uint8 *e = f19_dseg + kCellDynTileEntries + 2 * 8;
        require(e[0] == 2 && e[1] == 5 && e[2] == 6 && e[3] == 7 &&
                *reinterpret_cast<const int16 *>(e + 4) == kTileEntryValue &&
                e[6] == 0x5A,
                "addTileEntry memcpy(&rec->lod,8) into slot 2");
        require(sceneObj.shape == (0x41 | 0x80),
                "addTileEntry marks entry->shape |= 0x80");
    }

    // ==== binding pins: the aircraft/view position is ONE dword pair ====
    // Original: word_37B1A/37B1C dword @ 0x8E2A (X) and word_38136/38138
    // dword @ 0x9446 (Y) — spawn writes them, the flight model integrates
    // them (seg000:2C64/2C96), the tacmap-pan keys adjust them, renderFrame
    // copies them into the camera eye. The port split them across names
    // but must keep both aliases on the true cells — its old 0x8E48 cell is
    // the original's byte_37B38 trail-record array.
    require(&g_worldX == reinterpret_cast<int32 *>(f19_dseg + 0x8E2A) &&
            &g_ViewX == reinterpret_cast<int32 *>(f19_dseg + 0x8E2A) &&
            &g_worldY == reinterpret_cast<int32 *>(f19_dseg + 0x9446) &&
            &g_ViewY == reinterpret_cast<int32 *>(f19_dseg + 0x9446),
            "world/view position aliases bind word_37B1A/word_38136 cells");

    // ==== binding pins: view-mode + tile coords in the 0x94E0 block ====
    // Original: word_381D0 @ 0x94E0 = view mode (cmp vs 0/0x88/0x89/0x8B,
    // test 80h external flag); word_381DE @ 0x94EE = viewX_; word_381EE @
    // 0x94FE = viewY_ (spawn computes 0x8000 - word_381EE). g_camExtFlag is
    // the byte view of viewMode's low byte.
    require(&g_viewMode == reinterpret_cast<int16 *>(f19_dseg + 0x94E0) &&
            &g_camExtFlag == reinterpret_cast<int8 *>(f19_dseg + 0x94E0) &&
            &g_viewX_ == reinterpret_cast<uint16 *>(f19_dseg + 0x94EE) &&
            &g_viewY_ == reinterpret_cast<uint16 *>(f19_dseg + 0x94FE),
            "viewMode 0x94E0 / viewX_ 0x94EE / viewY_ 0x94FE cells");

    // ==== eg3dmap.c: worldToTileIndex — origin cells are DISTINCT ====
    // Original: mapOriginX word_35016 (0x6326), mapOriginY word_35018
    // (0x6328), tileWorldSize word_35012 (0x6322). Sharing one cell makes X
    // and Y track the same origin — a regression this test must catch.
    g_viewCenterX = 100;
    g_viewCenterY = 200;
    setDseg16(kCellMapOriginX, 50);
    setDseg16(kCellMapOriginY, 70);
    setDseg16(kCellTileWorldSize, 16);
    {
        int16 col = 0x7777, row = 0x7777;
        f19eg_worldToTileIndex(0, 0, &col, &row);
        require(col == -3 && row == -12,
                "worldToTileIndex min corner uses distinct X/Y origins");
        f19eg_worldToTileIndex(300, 400, &col, &row);
        require(col == 15 && row == 21,
                "worldToTileIndex interior point");
        // The *4/3 row scaling is part of the contract (5/6 aspect pre-divide).
        f19eg_worldToTileIndex(116, 206, &col, &row);
        require(col == 4 && row == 4,
                "worldToTileIndex 4/3 row aspect term");
    }

    // ==== eg3dmap.c: computeTileBounds clamps to the grid ====
    // tileGridDim is word_35014 at 0x6324 — distinct from tileEntryIdx.
    g_clipMaxX = 0x13F;
    g_clipMaxY = 199;
    setDseg16(kCellTileGridDim, 64);
    {
        int16 minX = 9, maxX = 9, minY = 9, maxY = 9;
        f19eg_computeTileBounds(&minX, &maxX, &minY, &maxY);
        require(minX == 0 && minY == 0 && maxX == 16 && maxY == 4,
                "computeTileBounds clips negative + caps to grid dim");
        setDseg16(kCellTileGridDim, 8);
        f19eg_computeTileBounds(&minX, &maxX, &minY, &maxY);
        require(maxX == 7 && maxY == 4,
                "computeTileBounds caps at tileGridDim-1");
    }

    // ==== eg3dmap.c: aspectScaleY (5/6 -> 3/4 in F-19) ====
    require(f19eg_aspectScaleY(40) == 30 && f19eg_aspectScaleY(48) == 36 &&
            f19eg_aspectScaleY(-8) == -6,
            "aspectScaleY = y - y/4");

    // ==== egui.c: formatMissionClock (":HH:MM:SS" into g_nameBuf) ====
    setDseg16(kCellMissionTick, 0);
    setDseg16(kCellNightMode, 0);
    f19_dseg[kCellNameBuf] = 0;
    f19eg_formatMissionClock(4515);   // 2h30m + tick*2 sec
    require(std::strcmp(reinterpret_cast<char *>(f19_dseg + kCellNameBuf),
                        ";02:30:30") == 0,
            "formatMissionClock base ':'+night+1 glyph + HH:MM:SS");
    setDseg16(kCellNightMode, 1);
    setDseg16(kCellMissionTick, 10);
    f19eg_formatMissionClock(0);      // tick adds in; night shifts lead glyph
    require(std::strcmp(reinterpret_cast<char *>(f19_dseg + kCellNameBuf),
                        "<00:00:20") == 0,
            "formatMissionClock tick add + night glyph");

    // ==== stgen.c: START-side math + formatters ====
    require(f19_calcBearing(0, 5) == 0 && f19_calcBearing(5, 0) == 0x4000 &&
            f19_calcBearing(100, 100) == 0x204D &&
            f19_calcBearing(300, 100) == 0x32CA &&
            f19_calcBearing(-100, -100) == (int16)0xA04D,
            "f19_calcBearing same curve as EGAME computeBearing");
    require(f19_rangeApprox(3, 4) == 5 && f19_rangeApprox(0x7000, 0x7000) == kRangeMax,
            "f19_rangeApprox");
    require(f19_clampValue(5, 0, 10) == 5 && f19_clampValue(-1, 0, 10) == 0 &&
            f19_clampValue((int16)0xC000, 0, 10) == 10,
            "f19_clampValue bearing-wrap rule");

    // formatGridRef writes bufCoordStr (0x98CC) off gameData->theater.
    f19_gameData = f19_commBase + 0x120E;
    *reinterpret_cast<int16 *>(f19_commBase + 0x120E + kOffGameDataTheater) = 0;
    require(std::strcmp(f19_formatGridRef(0x800, 0x800, 0), "TD74") == 0,
            "formatGridRef Libya prefix + grid digits");
    require(std::strcmp(f19_formatGridRef(0x4000, 0x4000, 0), "UC65") == 0,
            "formatGridRef decade carry/letter bump");
    *reinterpret_cast<int16 *>(f19_commBase + 0x120E + kOffGameDataTheater) = 1;
    require(std::strcmp(f19_formatGridRef(0, 0, 1), "JZ09") == 0,
            "formatGridRef Persian Gulf prefix");
    require(f19_formatGridRef(0, 0, 1) == reinterpret_cast<char *>(f19_dseg + kCellBufCoordStr),
            "formatGridRef returns bufCoordStr");

    // formatTimeStr: "HH:MM", minutes floored to 5, hours+flag digit.
    {
        char buf[8];
        setDseg16(kCellMissionTimeFlag, 0);
        f19_formatTimeStr(buf, 4515);
        require(std::strcmp(buf, "12:30") == 0, "formatTimeStr HH:MM");
        setDseg16(kCellMissionTimeFlag, 1);
        f19_formatTimeStr(buf, 0);
        require(std::strcmp(buf, "20:00") == 0, "formatTimeStr flag digit");
    }

    // ==== stutil.c: Mission Targets (briefPage=2) briefing exit ====
    // Repro of "game stops when exiting Mission targets": the briefing-room
    // menu select sets byte_2C160=5 + word_2CA6C=2, the dispatcher calls
    // f19_printMission, and the routine must return with state 4 queued.
    {
        setGamePath("/home/xor/games/f19/F19");
        f19_dsegInit();                      /* START image (not the EGAME one) */
        f19_commData = f19_commBase;
        f19_gameData = f19_commBase + 0x120E;
        // scenery0.exe splats the far-ptr table the flight-plan page reads:
        // briefTextP at 0x99A, ROE text at 0x99E + roeIdx*4.
        const int16 gm = ovlF43_a(0x42);
        require(gm >= 0x10, "scenery0.exe loads for briefing pointer table");
        ovlF43_10d(gm);
        require(*reinterpret_cast<uint32 *>(f19_dseg + 0x99A) != 0,
                "briefTextP cell populated by string-table splat");
        require(*reinterpret_cast<uint32 *>(f19_dseg + 0x99E) != 0,
                "ROE text cell populated by string-table splat");
        // Waypoint indices the FLIGHT PLAN page prints; name table -> a scratch
        // string so strcat walks known memory.
        setDseg16(0xB94A, 3);                /* pathWpA */
        setDseg16(0xB95C, 7);                /* pathWpD */
        std::strcpy(reinterpret_cast<char *>(f19_dseg + 0xF000), "TEST SITE");
        for (int i = 0; i < 32; i++) setDseg16(0xCA70 + i * 2, 0xF000);
        setDseg16(0xCA6C, 2);                /* briefPage=2: "Mission Targets" */
        f19_dseg[0xC160] = 5;
        // The entry drain eats queued keys, so feed the exit read from a
        // delayed poster thread.
        std::thread poster([] {
            SDL_Delay(200);
            SDL_Event ev = {};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = SDL_SCANCODE_RETURN;
            ev.key.key = SDLK_RETURN;
            SDL_PushEvent(&ev);
        });
        f19_printMission();
        poster.join();
        require(f19_dseg[0xC160] == 4,
                "printMission(Mission Targets) exits to state 4");
    }

    std::cout << "f19_behavior_tests: all checks passed\n";
    return 0;
}
