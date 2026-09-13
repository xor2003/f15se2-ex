# Four-point mission options

Each route has exactly one start, primary target, secondary target and end,
in that order. Start and end may be the same base. Targets must be distinct
and cannot coincide with a base. Object coordinates come from the current
world, not old briefing annotations.

From the repository root:

```sh
python tools/f15assets/generate_routes.py \
  converted_assets_all/SVN/SVN.WLD.json \
  converted_assets_all/SVN/geography/route-options.json \
  /tmp/svn-route-options.json --count 20 --seed 1
```

Use a new output filename for each run. The command refuses to overwrite files.
The same input and seed produce the same options. Requesting more options than
the candidate pools allow reports an error instead of duplicating missions.
`max_leg_world_units` limits each leg in WLD map units, not kilometers.

For OSM locations, supply a saved Overpass JSON response with `--osm file.json`.
Nodes use their coordinates; ways and relations require `out center` results.
A candidate can use `{"osm": "way/217455233"}` instead of a world object slot.
The config supplies longitude/latitude corners in TL, TR, BL, BR order.
Locations outside the map are rejected. Missing way/relation centers are not
guessed. The reusable parser and projection live in `f15assets/osm_routes.py`;
the existing SVN base-placement tool uses them too.

Assign candidate roles yourself: OSM geography does not determine allegiance,
historical military use, or whether a location is a suitable game target.
Retain OpenStreetMap attribution and source references for imported data.

These outputs are **authoring options**, not automatically playable missions.
OSM-only points need world objects and terrain placements before use in game.
The current runtime consumes primary/secondary objective hints but still picks
departure/recovery bases itself. Exact four-point execution requires wiring
selected start/end slots into mission generation; route metadata alone does
not enforce them.
