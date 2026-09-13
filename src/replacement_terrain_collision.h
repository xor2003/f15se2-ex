#ifndef REPLACEMENT_TERRAIN_COLLISION_H
#define REPLACEMENT_TERRAIN_COLLISION_H

#include "eg3dgrid.h"
#include "r3d_replacement.h"

/* Heightfield triangles use runtime X/Y horizontally and positive Z upward.
 * Barycentric interpolation follows the visible mesh, including its valleys. */
static int belowTerrainTriangle(const float *a, const float *b, const float *c,
                                double x, double y, double z) {
    double determinant = (b[1] - c[1]) * (a[0] - c[0]) +
                         (c[0] - b[0]) * (a[1] - c[1]);
    double u, v, w, height;
    if (determinant == 0.0) return 0;
    u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / determinant;
    v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / determinant;
    w = 1.0 - u - v;
    if (u < 0.0 || v < 0.0 || w < 0.0) return 0;
    height = u * a[2] + v * b[2] + w * c[2];
    return z < height;
}

static int aircraftInsideReplacementTerrain(void) {
    enum { TILE_MODEL_UNITS = 4096, TRIANGLE_PRIMITIVE = 4,
           DYNAMIC_OBJECT_FLAG = 0x80 };
    int lod;
    /* Asset heightfields must fit their containing tile. Moving/destructible
     * objects retain the original collision path rather than becoming terrain. */
    for (lod = 1; lod <= 4; ++lod) {
        int fineUnitsPerModelUnit = 1 << (2 * (lod - 1));
        double x = (double)g_ViewX / fineUnitsPerModelUnit;
        double y = (double)g_ViewY / fineUnitsPerModelUnit;
        double z = (double)(uint16)g_viewZ / fineUnitsPerModelUnit;
        int column, row, gridOffset = lod == 4 ? 2 : 0;
        int tile, entry;
        if (x < 0 || y < 0) continue;
        column = (int)x / TILE_MODEL_UNITS;
        row = (int)y / TILE_MODEL_UNITS;
        if (column + gridOffset >= g_lodGridDim[lod] ||
            row + gridOffset >= g_lodGridDim[lod]) continue;
        tile = process3dg(lod, column, row);
        if (tile < 0) continue;
        x -= (column + 0.5) * TILE_MODEL_UNITS;
        y -= (row + 0.5) * TILE_MODEL_UNITS;
        for (entry = 0; entry < matrix3dt[lod][tile]; ++entry) {
            int shape = matrix3dt_2[lod][tile][entry].shape;
            R3DReplacementMesh *mesh;
            int primitive;
            if (shape & DYNAMIC_OBJECT_FLAG) continue;
            mesh = r3d_replacementMesh(regnStr, shape);
            if (!mesh) continue;
            for (primitive = 0; primitive < mesh->nPrims; ++primitive) {
                const R3DReplacementPrim *part = &mesh->prims[primitive];
                int vertex;
                if (part->mode != TRIANGLE_PRIMITIVE ||
                    !(part->sourceFlags & R3D_SURFACE_LAND)) continue;
                for (vertex = 0; vertex + 2 < part->nVerts; vertex += 3) {
                    const float *a = part->xyz + vertex * 3;
                    if (belowTerrainTriangle(a, a + 3, a + 6,
                            x - matrix3dt_2[lod][tile][entry].x,
                            y - matrix3dt_2[lod][tile][entry].y,
                            z - matrix3dt_2[lod][tile][entry].z)) return 1;
                }
            }
        }
    }
    return 0;
}

#endif
