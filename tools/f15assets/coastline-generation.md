# Coastline models

`build_coast_tiles.py` converts a GeoJSON land FeatureCollection into 16
coastline tiles. Each tile contains land and water triangles at sea level.
It does not generate elevation, roads, buildings, or a cockpit map image.
Use separate terrain models for hills and separate placements for ground sites.

## Generate or update

Run from the repository root with Python and Shapely 2.1 or newer:

```sh
python3 -m venv .venv-terrain
.venv-terrain/bin/pip install 'shapely>=2.1,<3'

.venv-terrain/bin/python tools/f15assets/build_coast_tiles.py \
  converted_assets_all/SVN/geography/land.geojson \
  converted_assets_all/SVN/VN/VN \
  --corners 97.843 15.059 106.060 23.514 105.932 7.412 114.149 16.180 \
  --first-shape 100 --replace-generated
```

Corners are longitude/latitude pairs in top-left, top-right, bottom-left,
bottom-right order. They describe the game map, not an axis-aligned bounding
box. The inverse bilinear projection maps them onto the game's four-by-four
LOD4 grid. Use nondegenerate, nonfolded map corners.

For a new campaign, omit `--replace-generated`. The selected 16 model slots,
LOD4 grid, and destination tiles must be empty. Existing lower-LOD models
and placements are preserved.

For an update, `--replace-generated` requires the existing grid, tile
placements, model names, and legacy culling records to match this generator's
output. It reuses their offsets. Edited placements are rejected rather than
silently overwritten. Edited GLB artwork in those generated slots **will be
replaced**; keep hand-authored models in different slots.

All destination records are checked before model writes begin. File writes
are not a transaction: an I/O or conversion failure can leave a partial
update. Generate in a copy of the campaign before replacing release assets.

## Runtime files

The generator writes `.glb` models, matching `cache/*.glmesh` files, and the
container's `.3D3.json`, `.3DG.json`, and `.3DT.json` tables. The legacy culling
record uses class 7 so the engine considers the whole terrain tile before
submitting its replacement mesh. A point-sized placeholder can make otherwise
valid terrain disappear.

After changing source JSON, rebuild the compiled runtime files:

```sh
.venv-terrain/bin/python tools/f15assets/compile_runtime_assets.py \
  converted_assets_all/SVN
```

See `runtime-caches.md` for the cache format. Refresh release inventory hashes
and preserve source attribution when packaging; the coastline generator does
not update manifests. SVN's land polygons come from Natural Earth public-domain
data, with provenance stored alongside the geographic source.

## Limits and tests

The generator currently uses 16 consecutive slots below 126; the last two
model slots are reserved for mission data. Shape IDs still use seven bits.
Tile-pattern IDs use a separate byte-sized namespace with up to 256 patterns;
larger child grids use the extended format described in `ground-placement.md`.

```sh
.venv-terrain/bin/python tools/f15assets/tests/test_coastline_regeneration.py
```

The tests check byte-identical reruns and refusal of occupied or edited tiles
without file changes. They do not test rendering, elevation collisions, or
recovery from disk failure. Inspect the resulting coastline in the main game
view as well as checking the generated files.
