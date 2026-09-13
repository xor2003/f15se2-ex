# Campaign model generation

The existing script names and default paths remain compatible with SVN.
Both Blender generators accept `--root`, `--container` and `--name-prefix`
for other campaigns. They do not choose factions or base ownership.

```sh
blender -b --python tools/f15assets/generate_svn_terrain.py -- \
  --root converted_assets_all/SVN --container VN --name-prefix SVN \
  --config converted_assets_all/SVN/terrain-generation-preview.json
```

The optional JSON contains `snow_line_meters`, `snow_transition_meters`
and `meters_per_model_unit`. Command-line values override JSON settings.
Omit the snow line and config to regenerate vegetation-only terrain.
Snow avoids steep slopes; regeneration preserves the heightfield geometry.

The preview uses 16 feet per model unit at LOD 3, or 4.8768 meters.
This linear conversion applies below the game's altitude compression threshold
of 8,192 feet. Do not reuse it blindly for taller terrain or another LOD.
The preview is a visual experiment, not a claim about Vietnam's snow cover.

Ground landmarks are generated with:

```sh
blender -b --python tools/f15assets/generate_svn_ground.py -- \
  --root converted_assets_all/SVN --container VN --name-prefix SVN
```

The campaign must already contain its model container and free-assets manifest.
These generators retain engine-compatible slots: airfield 21, radar 38,
carrier 90, supply dump 93, and four terrain variants at 80 through 83.
They are not arbitrary slot remappers. The footprint references likewise
remain those of the original Vietnam container.

Original Vietnam sea-base records 35 and 36 use model 90 with the carrier
flag, and refer to USS Constellation and USS Kitty Hawk. A campaign decides
whether such ships are friendly, hostile, or usable departure/recovery bases.
Changing a filename or material does not change those gameplay flags.

After generating GLBs, rebuild their `.glmesh` caches with
`f15assets.validation_model3d.glb_to_glmesh_bytes` and refresh existing inventory
hashes. The terrain generator does not modify terrain placement grids.
