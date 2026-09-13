# Replacement-terrain collision tests

Run from the repository root with GCC or Clang. Keep assertions enabled.

```sh
c++ -std=c++17 -fsanitize=undefined -fno-sanitize-recover=all -I src \
  tools/f15assets/tests/terrain_collision.cpp -o /tmp/terrain-collision-test
/tmp/terrain-collision-test

c++ -std=c++17 -fsanitize=undefined -fno-sanitize-recover=all -I src \
  tools/f15assets/tests/terrain_cache_collision.cpp -o /tmp/terrain-cache-test
/tmp/terrain-cache-test \
  converted_assets_all/SVN/VN/cache/shape_08?_SVN_Terrain_*.glmesh
```

Both tests call the production collision helper with a minimal scene fixture.
The first checks a sloped triangle, outside points, water, unmarked geometry,
and dynamic-object exclusion. The second loads the generated caches and checks
points just below and above each non-flat hill triangle at LOD 3.

The cache test expects trusted, little-endian runtime caches and at least one
hill triangle per file. It is not a malformed-file parser test. Neither test
proves swept collision between frames, camera alignment, or gameplay takeoff.
