/* Private replacement-model helpers, included after the legacy rasterizer
 * globals. This keeps new GLB code separate without exporting the software
 * renderer's private projection state or moving original routines. */

typedef struct ReplacementCameraVertex {
    long x;
    long y;
    long depth;
} ReplacementCameraVertex;

/* Map a replacement RGB color to the nearest active software palette entry. */
static int replacementNearestPaletteIndex(float r, float g, float b) {
    int rr = (int)(r * 255.0f + 0.5f);
    int gg = (int)(g * 255.0f + 0.5f);
    int bb = (int)(b * 255.0f + 0.5f);
    if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; else if (bb > 255) bb = 255;
    return gfx_nearestPaletteIndexRgb8((uint8)rr, (uint8)gg, (uint8)bb);
}

/* Resolve a replacement primitive color through the legacy palette and shade rules. */
static int replacementPrimitiveColor(const R3DReplacementPrim *prim) {
    int i{};
    int base = replacementNearestPaletteIndex(prim->rgba[0], prim->rgba[1], prim->rgba[2]);

    /* Apply the engine's distance shading to material colors. Converter proof
     * metadata must not affect drawing: stripping GLB extras in an editor must
     * leave the same geometry and materials looking the same. */
    for (i = 0; i < 16; i++) {
        if (base == colorLut[i]) return colorLut[i] + g_objShade;
    }
    return base;
}

/* Transform one replacement vertex into legacy camera-space fixed-point coordinates. */
static int transformReplacementVertex(const R3DReplacementPrim *prim, int srcVert,
                                       const int16 *combined, long camBase, long camX, long camY,
                                       ReplacementCameraVertex *out) {
    int shift = 8 - 2 * g_curLod;
    long scaleDiv = (shift > 0) ? (1L << shift) : 1L;
    float x = prim->xyz[srcVert * 3];
    float y = prim->xyz[srcVert * 3 + 1];
    float z = prim->xyz[srcVert * 3 + 2];
    float cameraX = (2.0f * ((float)combined[0] * x + (float)combined[3] * z + (float)combined[6] * y) + (float)camBase) / scaleDiv;
    float cameraY = (2.0f * ((float)combined[1] * x + (float)combined[4] * z + (float)combined[7] * y) + (float)camX) / scaleDiv;
    float cameraDepth = (2.0f * ((float)combined[2] * x + (float)combined[5] * z + (float)combined[8] * y) + (float)camY) / scaleDiv;
    /* Scratch/projection values have a signed 32-bit fixed-point range even
     * on hosts with 64-bit long. Check before casting: out-of-range float to
     * integer conversion is undefined. These comparisons also reject NaN/Inf.
     * Division by the power-of-two LOD scale precedes conversion so a large
     * intermediate that becomes representable after scaling remains usable. */
    if (!(cameraX >= -2147483648.0f && cameraX < 2147483648.0f &&
          cameraY >= -2147483648.0f && cameraY < 2147483648.0f &&
          cameraDepth >= -2147483648.0f && cameraDepth < 2147483648.0f)) return 0;
    out->x = (long)cameraX;
    out->y = (long)cameraY;
    out->depth = (long)cameraDepth;
    return 1;
}

/* Project one camera-space replacement vertex into the legacy viewport. */
static int projectReplacementCameraVertex(const ReplacementCameraVertex *v, int slot) {
    VCAMX(slot) = v->x;
    VCAMY(slot) = v->y;
    setVtxDepth(slot >> 2, v->depth);
    if (HI16(v->depth) < 1) return 0;
    projectVertexToScreen(slot >> 2);
    return 1;
}

/* Intersect a replacement edge with the legacy near clipping plane. */
static ReplacementCameraVertex replacementNearIntersection(const ReplacementCameraVertex *behind,
                                                          const ReplacementCameraVertex *front) {
    ReplacementCameraVertex out{};
    int div = HI16(front->depth) - HI16(behind->depth);
    int cx{};
    long delta, prod;

    if (div <= 0) {
        out = *front;
        out.depth = 65536L;
        return out;
    }

    /* Match clipEdgeNearPlane's fixed-point interpolation instead of using a
     * separate high-precision path. This keeps software replacement GLB clipping
     * behavior aligned with legacy .3D3 edge clipping and avoids compiler helper
     * calls for floating point or 64-bit arithmetic in this rasterizer TU. */
    /* The formula needs front's low depth word. Cast through int16 to mirror the
     * legacy vproj.in[].num low word without aliasing ReplacementCameraVertex. */
    cx = (int)(udiv32by16_full((((unsigned long)(HI16(front->depth) - 1)) << 16) |
                                   (uint16)(int16)front->depth,
                               (unsigned)div) >> 1);

    delta = front->x - behind->x;
    prod = imul16(HI16(delta) + LOCARRY(delta), cx) << 1;
    out.x = front->x - prod;
    delta = front->y - behind->y;
    prod = imul16(HI16(delta) + LOCARRY(delta), cx) << 1;
    out.y = front->y - prod;
    out.depth = 65536L;
    return out;
}

/* Return whether a replacement camera-space vertex is in front of the near plane. */
static int replacementVertexInFront(const ReplacementCameraVertex *v) {
    return HI16(v->depth) >= 1;
}

/* Clip one replacement line segment against the legacy near plane. */
static int clipReplacementLineNear(ReplacementCameraVertex *a, ReplacementCameraVertex *b) {
    int aIn = replacementVertexInFront(a);
    int bIn = replacementVertexInFront(b);
    if (!aIn && !bIn) return 0;
    if (!aIn) *a = replacementNearIntersection(a, b);
    else if (!bIn) *b = replacementNearIntersection(b, a);
    return 1;
}

/* Clip one replacement polygon against the legacy near plane without reordering vertices. */
static int clipReplacementPolygonNear(const ReplacementCameraVertex *in, int inCount,
                                      ReplacementCameraVertex *out) {
    ReplacementCameraVertex cur[5]{};
    int curCount = inCount;
    int i, outCount = 0;
    for (i = 0; i < inCount; i++) cur[i] = in[i];
    for (i = 0; i < curCount; i++) {
        ReplacementCameraVertex a = cur[i];
        ReplacementCameraVertex b = cur[(i + 1) % curCount];
        int aIn = replacementVertexInFront(&a);
        int bIn = replacementVertexInFront(&b);
        if (aIn && bIn) {
            out[outCount++] = b;
        } else if (aIn && !bIn) {
            out[outCount++] = replacementNearIntersection(&b, &a);
        } else if (!aIn && bIn) {
            out[outCount++] = replacementNearIntersection(&a, &b);
            out[outCount++] = b;
        }
    }
    return outCount;
}

/* Project a clipped replacement polygon into the software rasterizer point buffer. */
static int projectReplacementPolygon(const ReplacementCameraVertex *poly, int n, int *points) {
    int i{};
    if (n < 3 || n > 4) return 0;
    for (i = 0; i < n; i++) {
        if (!projectReplacementCameraVertex(&poly[i], i * 4)) return 0;
        points[i * 2] = (int16)vtxScratch.vproj.x.v[i];
        points[i * 2 + 1] = (int16)vtxScratch.vproj.y.v[i];
    }
    return 1;
}

/* Rasterize replacement polygons and lines through the original software drawing paths. */
static void drawReplacementMesh(R3DReplacementMesh *mesh) {
    int16 combined[9]{};
    long camBase, camX, camY;
    int i{};

    if (!mesh) return;
    /* projectReplacementSceneObject follows the same cull/LOD/sort path as
     * projectSceneObject. At this point immediate and sorted objects have the
     * exact object-origin camera state the legacy software renderer would use.
     * Build only the orientation*view matrix here, then draw GLB/GLMESH
     * primitives through the existing software span/line rasterizers. */
    {
        int orv = g_objTransform[1] | g_objTransform[2] | g_objTransform[3];
        int al = (orv | (orv >> 8)) & 0xff;
        g_objHasRotation = (uint8)al;
        if (al == 0) {
            for (i = 0; i < 9; i++) combined[i] = g_viewRotMatrix[i];
        } else {
            buildInverseRotationMatrix(g_objOrientMatrix,
                                       g_objTransform[1], g_objTransform[2], g_objTransform[3]);
            multiplyMatrix3x3(g_objOrientMatrix, g_viewRotMatrix, combined);
        }
    }
    camBase = g_camBaseX;
    camX = JOIN32(g_camTransXLo, g_camTransXHi);
    camY = JOIN32(g_camTransYLo, g_camTransYHi);

    for (i = 0; i < mesh->nPrims; i++) {
        R3DReplacementPrim *prim = &mesh->prims[i];
        int color = replacementPrimitiveColor(prim);
        int v{};
        if (prim->mode == 4) {
            for (v = 0; v + 2 < prim->nVerts; v += 3) {
                ReplacementCameraVertex tri[3], clipped[5]{};
                int points[8]{};
                int n{};
                if (!transformReplacementVertex(prim, v, combined, camBase, camX, camY, &tri[0]) ||
                    !transformReplacementVertex(prim, v + 1, combined, camBase, camX, camY, &tri[1]) ||
                    !transformReplacementVertex(prim, v + 2, combined, camBase, camX, camY, &tri[2])) continue;
                n = clipReplacementPolygonNear(tri, 3, clipped);
                if (!projectReplacementPolygon(clipped, n, points)) continue;
                drawPolygonOutline(color, n, points, color);
            }
        } else if (prim->mode == 1) {
            gfx_setColor((unsigned char)color);
            for (v = 0; v + 1 < prim->nVerts; v += 2) {
                ReplacementCameraVertex a, b;
                if (!transformReplacementVertex(prim, v, combined, camBase, camX, camY, &a) ||
                    !transformReplacementVertex(prim, v + 1, combined, camBase, camX, camY, &b)) continue;
                if (!clipReplacementLineNear(&a, &b)) continue;
                if (!projectReplacementCameraVertex(&a, 0)) continue;
                if (!projectReplacementCameraVertex(&b, 4)) continue;
                g_lineX1 = (int16)vtxScratch.vproj.x.v[0];
                g_lineY1 = (int16)vtxScratch.vproj.y.v[0];
                g_lineX2 = (int16)vtxScratch.vproj.x.v[1];
                g_lineY2 = (int16)vtxScratch.vproj.y.v[1];
                drawClipLineGlobal();
            }
        } else if (prim->mode == 0) {
            gfx_setColor((unsigned char)color);
            for (v = 0; v < prim->nVerts; v++) {
                ReplacementCameraVertex point{};
                if (!transformReplacementVertex(prim, v, combined, camBase, camX, camY, &point)) continue;
                if (!projectReplacementCameraVertex(&point, 0)) continue;
                g_lineX1 = g_lineX2 = (int16)vtxScratch.vproj.x.v[0];
                g_lineY1 = g_lineY2 = (int16)vtxScratch.vproj.y.v[0];
                drawClipLineGlobal();
            }
        }
    }
}

