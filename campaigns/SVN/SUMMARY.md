# Li Si Cin: Soviet Vietnam Campaign

Campaign id: `SVN`
Base theater: `VN`
Run: `F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN`

## Premise

Soviet spy pilot Lisicin has stolen an F-15 plane and flies in Vietnam as Li Si Cin against USA air and naval targets.

## Sorties

- `opening_sead`: Open the radar corridor (suppress_hanoi_radar)
- `carrier_strike`: Strike the carrier group (strike_carrier_group, sweep_tonkin_patrol)
- `airbase_and_logistics`: Cut the air bridge (strike_da_nang_airbase, interdict_haiphong_logistics)

## Mission targets

- `start_soviet_hq` slot 0: Soviet forward HQ [soviet_vietnamese/base/defend] at unnamed WLD object `27256,4893`
- `strike_carrier_group` slot 6: USA carrier group [usa/naval/strike] at unnamed WLD object `28300,10979`
- `strike_da_nang_airbase` slot 7: Da Nang airbase [usa/airbase/strike] at unnamed WLD object `21358,20256`
- `suppress_hanoi_radar` slot 3: Hanoi radar net [usa_allied/air_defense/suppress] at unnamed WLD object `27269,4750`
- `interdict_haiphong_logistics` slot 4: Haiphong logistics [usa_allied/logistics/interdict] at unnamed WLD object `28384,6972`
- `sweep_tonkin_patrol` slot 5: Tonkin patrol [usa/air/sweep] at unnamed WLD object `28523,9132`

Runtime uses the selected sortie's primary and secondary objective slots as mission target hints:
`suppress_hanoi_radar, strike_carrier_group, sweep_tonkin_patrol, strike_da_nang_airbase, interdict_haiphong_logistics`. The legacy generator still validates mission-table compatibility, base distance, and fallback behavior.

## Mission target packages

- `target_set_hanoi_corridor`: Hanoi radar corridor package (search_radar, sam_launcher, aaa_screen)
- `target_set_tonkin_carrier`: Gulf of Tonkin carrier strike package (cap_patrol, escort_radar, carrier_deck)
- `target_set_southern_air_bridge`: Da Nang and Haiphong air-bridge package (haiphong_depot, da_nang_runway, fuel_storage)

## Route plan and real-world anchors

`SVN.WLD.json` contains `campaign_route_plan`, an authoring-only route layer that ties the first sorties to approximate Vietnam OSM anchors transformed into legacy WLD coordinates.

- `wp_hq` / `start_soviet_hq`: Gia Lam / Hanoi forward control (`27256,4893`).
- `wp_hanoi_radar` / `suppress_hanoi_radar`: Hanoi SAM radar belt (`27269,4750`).
- `wp_haiphong` / `interdict_haiphong_logistics`: Haiphong port logistics (`28384,6972`).
- `wp_tonkin_cap` / `sweep_tonkin_patrol`: Gulf of Tonkin CAP lane (`28523,9132`).
- `wp_carrier` / `strike_carrier_group`: Gulf of Tonkin carrier box (`28300,10979`).
- `wp_danang` / `strike_da_nang_airbase`: Da Nang airbase (`21358,20256`).

The map editor can draw these route legs with `Routes on` and synchronizes routed waypoint coordinates when you drag or numerically edit the matching object.

## Editable modern media

- `TITLE640.png`, `TITLE.png`, `DESK.png`, `WALL.png`, `HISCORE.png`, `ARMPIECE.png`: campaign-local high-resolution PNG screen art.
- `start/menu/arm/0.png` through `start/menu/arm/6.png`: transparent briefing pointer-arm cels for the native overlay path.
- `VN.png`: campaign-local high-resolution Vietnam theater/debrief map replacement.
- `sounds/*.wav`, `sounds/*.txt`, and `sounds/intro_music.asound.json`: radio cues and campaign intro music.
- `fonts/*.ttf`: scalable Cyrillic-capable font overrides.
- `15FLT/shape_###_*.glb`: campaign-local procedural GLB placeholders; replace individual slots from Blender as needed.
- `MISSION_TARGETS.md`: generated target-package checklist for objective slots, attack order, victory logic, and route anchors.
- `CUSTOM_ASSETS_README.md`: customizer guide for replacing PNG/WAV/font/GLB/scenario files.

## Edit workflow

1. Edit `SVN.WLD.json` in the map editor for target placement and objective ids.
2. Review `MISSION_TARGETS.md` after changing objectives or target packages.
3. Edit `briefings/*.md` for human-facing mission text; `package-campaign` syncs the title/summary back into the WLD fields used by the in-game START board.
4. Replace PNG/WAV/font/GLB files with reviewed custom media.
5. Run `python3 -m tools.f15assets.cli refresh-campaign-inventory . --pretty` from this directory to sync docs/inventory without zipping.
6. Run `python3 -m tools.f15assets.cli package-campaign . ../SVN.zip --pretty` from this directory to sync docs, refresh inventory, validate, and zip.
7. Launch with `--campaign SVN` for the full campaign, or `--scenario SVN --scenario-base VN` when testing only the custom WLD JSON redirect.

`--pretty` is accepted both globally and on `package-campaign`; omit it only when you intentionally want compact JSON.

See `campaign.json`, `SVN.WLD.json`, `MISSION_TARGETS.md`, `CUSTOM_ASSETS_README.md`, `briefings/*.md`, and `inventory.json` for editable details.
