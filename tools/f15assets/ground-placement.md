# Ground-model placement

Model size depends on both its vertex coordinates and its placement LOD:

| Placement | Fine world units per model unit |
| --- | ---: |
| LOD 1 | 1 |
| LOD 2 | 4 |

Do not resize a model to compensate for placing it at the wrong LOD. For
example, the original VN airbase uses LOD 2; its 512-unit runway half-length
therefore covers 2,048 fine world units.

Place `placement.json` beside the container's `.3DG.json` and `.3DT.json`:

```json
{
  "model_lods": {
    "21": 2
  }
}
```

Keys are model slots, not world-object indices. Unlisted models use LOD 1.
The ground-placement compiler currently accepts LOD 1 and LOD 2.

```sh
python tools/f15assets/place_svn_ground.py \
  --root converted_assets_all/SVN --container VN --world SVN.WLD.json
```

The compiler rebuilds LOD 1/2 ground placements from `world_objects`, preserves
LOD 3 terrain and LOD 4 geography, and accepts up to 256 tile patterns at
any rebuilt level. Existing hand-authored LOD 1/2 placements are replaced.

Campaigns using at most 32 patterns retain the original `.3DG` layout. Larger
grids use the `G2` signature (`0x3247`, little endian) and three 4,096-byte child
tables instead of three 512-byte tables. The 16-byte top grid and 256-byte
LOD 3 grid are unchanged. Each parent still selects a 16-byte child block.
Older game builds do not support `G2` files.

Both the briefing and flight loaders accept the extended format. Placement
storage is 64 KiB across all levels, independent of the pattern count. Rebuild
compiled JSON caches after changing these files; see `runtime-caches.md`.

## Coastline placeholders

OpenGL performs legacy visibility culling before drawing a replacement GLB.
`build_coast_tiles.py` therefore writes culling class 7 for its full-sized
terrain tiles. A zero-filled placeholder gives them point-sized culling bounds
and can make the coastline disappear even when the GLB loads successfully.
