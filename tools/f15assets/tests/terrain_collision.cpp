#include <cassert>
#include "inttype.h"
#include "r3d_replacement.h"
struct TerrainEntry { int x, y, z, shape; };
int32 g_ViewX = 0, g_ViewY = 0;
int16 g_viewZ = 0;
int g_lodGridDim[5] = {0, 0, 0, 16, 0};
int matrix3dt[5][32] = {};
TerrainEntry *matrix3dt_2[5][32] = {};
const char *regnStr = "arbitrary-campaign";
R3DReplacementMesh *testMesh = nullptr;
R3DReplacementMesh *r3d_replacementMesh(const char *, int) { return testMesh; }
int16 process3dg(int16, int16, int16) { return 0; }
#include "replacement_terrain_collision.h"
int main() {
    float triangle[] = {0,0,10, 10,0,20, 0,10,10};
    R3DReplacementPrim primitive = {};
    primitive.mode = 4;
    primitive.nVerts = 3;
    primitive.sourceFlags = R3D_SURFACE_LAND;
    primitive.xyz = triangle;
    R3DReplacementMesh mesh = {};
    mesh.nPrims = 1;
    mesh.prims = &primitive;
    testMesh = &mesh;
    TerrainEntry entry = {0,0,0,17};
    matrix3dt[3][0] = 1;
    matrix3dt_2[3][0] = &entry;
    g_ViewX = g_ViewY = (2048 + 2) * 16;
    g_viewZ = 11 * 16;
    assert(aircraftInsideReplacementTerrain());
    g_viewZ = 13 * 16;
    assert(!aircraftInsideReplacementTerrain());
    g_viewZ = 11 * 16;
    g_ViewX = g_ViewY = (2048 + 9) * 16;
    assert(!aircraftInsideReplacementTerrain());
    g_ViewX = g_ViewY = (2048 + 2) * 16;
    primitive.sourceFlags = R3D_SURFACE_WATER;
    assert(!aircraftInsideReplacementTerrain());
    primitive.sourceFlags = 0;
    assert(!aircraftInsideReplacementTerrain());
    primitive.sourceFlags = R3D_SURFACE_LAND;
    entry.shape |= 0x80;
    assert(!aircraftInsideReplacementTerrain());
}
