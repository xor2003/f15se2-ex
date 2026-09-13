# Custom radar icons

Extract the non-aircraft radar sprites from a campaign's F15.png:

```sh
python3 tools/f15assets/extract_radar_sprites.py \
  converted_assets_all/SVN/F15.png converted_assets_all/SVN/flight/radar
```

Requires Pillow. Existing output files are never overwritten. The exporter keeps
source pixels, preserves explicit alpha, and converts legacy transparency to alpha.

Edit `flight/radar/atlas_01.png` through `atlas_15.png` independently. Their numbers
are atlas columns, not object IDs. They cover row 3 at logical Y=55, with 7x7 source
rectangles beginning at X=`column * 8 + 1` in the 320x200 atlas.

The OpenGL renderer fits each PNG into the existing radar footprint. Higher
resolution icons are supported; keep them square. Runway icons retain the existing
rotation behavior. Missing PNGs fall back to the F15 atlas. Software rendering
continues to use the atlas.

Aircraft still use `plane-level.png`, `plane-low.png`, `plane-high.png`, and
`self.png`. The atlas export does not overwrite these. No JSON sidecar is needed.
Restart the game after editing an icon so its cached texture is reloaded.
