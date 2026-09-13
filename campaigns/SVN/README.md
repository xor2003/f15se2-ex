# SVN custom campaign pack

This directory is a modern F-15 SE2 EX campaign replacement pack.

Run:

```bash
F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN
```

Or from this campaign directory:

```bash
./run_campaign.sh /path/to/F15_GAME
./run_campaign.sh /path/to/F15_GAME carrier_strike
./run_campaign.sh /path/to/F15_GAME 3
```

Set `F15_EXE=/path/to/f15se2-ex` if the executable is not in the current directory. Set `F15_CAMPAIGN_SORTIE` or pass the optional second argument to select a sortie by id or phase.

The manifest redirects `VN.WLD` to `SVN.WLD.json` while this campaign is selected.

## Custom world loading

Use the campaign wrapper for normal play because it selects the WLD, campaign-local media, sortie briefing, and mission target hints together:

```bash
F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN
F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN --campaign-sortie carrier_strike
```

Use the lower-level scenario loader when testing only the custom WLD JSON redirection against the base theater:

```bash
F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --scenario SVN --scenario-base VN
```

Equivalent environment variables:

```bash
F15_REPLACEMENT_ROOT=converted_assets_all F15_WORLD_SCENARIO=SVN F15_WORLD_SCENARIO_BASE=VN ./f15se2-ex --game /path/to/F15_GAME
```

`F15_REPLACEMENT_ROOT` must point at the directory containing this campaign directory, not at the campaign directory itself.

Launch metadata:

- argv: `--campaign SVN`
- replacement root: `converted_assets_all`
- raw scenario argv: `--scenario SVN --scenario-base VN`

Runtime modes:

- `campaign`: `--campaign SVN` - Full campaign
- `campaign_sortie`: `--campaign SVN --campaign-sortie <sortie-id-or-phase>` - Full campaign, selected sortie
- `raw_scenario`: `--scenario SVN --scenario-base VN` - Raw custom WLD scenario

Editable files:

- `SVN.WLD.json`: authoritative world/scenario data.
- `MISSION_TARGETS.md`: generated mission objective and target-package checklist.
- `CUSTOM_ASSETS_README.md`: customizer guide for replacing modern media/source files.
- `SUMMARY.md`: short human-readable campaign overview.
- `inventory.json`: generated file inventory for review/distribution tooling.

## Installing this ZIP on another setup

Inspect the package before extracting:

```bash
python3 tools/f15assets/cli.py inspect-campaign-package converted_assets_all/SVN.zip
```

Install into a replacement asset root:

```bash
python3 tools/f15assets/cli.py install-campaign-package converted_assets_all/SVN.zip /path/to/converted_assets_all --replace
```

Run after install:

```bash
F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN
```

Use `--dry-run` on `install-campaign-package` to preview extraction without changing files.

Modern media policy:

- Images: PNG source, fit_original_game_rectangle scaling.
- Sounds: individual WAV cue files.
- Fonts: TTF/OTF, then BDF/PNG fallback.
- Models: per-shape GLB files.

Objective capability map:
- `custom_world_scenario`: provided via `SVN.WLD.json`, `campaign.json`.
- `soviet_vietnam_campaign`: provided via `campaign.json`, `SUMMARY.md`, `MISSION_TARGETS.md`.
- `modern_png_images`: provided via `TITLE640.png`, `DESK.png`, `HISCORE.png`, `ARMPIECE.png`, ... (13 total).
- `modern_wav_radio`: provided via `sounds/voice_cue_000_sample0.wav`, `sounds/voice_cue_001_sample4.wav`, `sounds/voice_cue_002_sample2_variant0.wav`, `sounds/voice_cue_003_sample2_variant1.wav`, ... (5 total).
- `modern_asound_music`: provided via `sounds/intro_music.asound.json`.
- `modern_fonts`: provided via `fonts/font_1.ttf`, `fonts/font_3.ttf`, `fonts/font_4.ttf`.
- `modern_glb_models`: provided via `15FLT/shape_000_LiSiCin_Stolen_F15.glb`, `15FLT/shape_001_Tonkin_Carrier_Target.glb`, `15FLT/shape_002_DaNang_Strike_Jet.glb`, `15FLT/shape_003_Hanoi_Radar.glb`, ... (23 total).
- `mission_targets`: provided via `campaign.json`, `MISSION_TARGETS.md`, `SVN.WLD.json`.

Verification status:
- `package_validation`: passed
  Evidence command: `python3 tools/f15assets/cli.py package-campaign converted_assets_all/SVN converted_assets_all/SVN.zip --pretty`
  Recorded at: `2026-09-12T18:59:21+00:00`
- `capability_artifact_validation`: all provided objective_capabilities artifacts must exist and be campaign-relative
- `wld_json_buildability`: checked by campaign manifest validation
- `replacement_loadability_validation`: passed
  Evidence command: `python3 tools/f15assets/cli.py validate-replacements /home/xor/games/f15 converted_assets_all --loadability-only`
  Recorded at: `2026-07-18T01:05:16+00:00`
- `runtime_launch_validation`: passed
  Evidence command: `SDL_VIDEODRIVER=dummy timeout 8s ./play_svn.sh /home/xor/games/f15`
  Recorded at: `2026-07-18T09:54:31+00:00`
- `full_asset_replacement_validation`: not_recorded_by_generator
- notes: This block records generator/package-level evidence only. A release should also record a game build, launch, and replacement validation result after running them.

Runtime contract:

Modern-editable now:
- campaign WLD JSON selection via --campaign/--scenario
- selected campaign sortie objective slots as runtime mission target hints
- selected campaign sortie title on the START mission briefing board
- selected campaign sortie briefing text on the START mission briefing board
- campaign-local PNG art replacements
- campaign-local high-resolution briefing room PNGs for native overlay
- campaign-local high-resolution theater map PNGs for debrief/native overlay
- campaign-local WAV cue replacements
- campaign-local ASOUND intro music stream JSON
- campaign-local TTF/OTF/BDF/PNG font overrides
- campaign-local per-shape GLB visual model replacements
- Markdown briefing text for authoring tools

Still legacy-driven or fallback-backed:
- aircraft flight model, weapons, AI, and hardcoded behavior tables
- deep mission generation rules beyond selected-sortie objective hints and WLD object/table data
- software 3D backend model drawing for GLB replacements
- unsupported original binary assets not yet mapped to modern replacements

The pack is intentionally media-first and editable; original game data remains the fallback for behavior not represented by modern assets yet.

Asset license review:

- status: `required_before_free_asset_distribution`
- notes: Replace placeholders with reviewed CC/free assets before distributing a free custom asset pack.

Campaign art:
- `TITLE640.png` replaces `TITLE640.PIC` (1448x1086 RGB888, target 640x350, source `PNG`, runtime `high_resolution_truecolor_title`).
- `DESK.png` replaces `DESK.PIC` (1585x992 RGB888, target 320x200, source `PNG`, runtime `truecolor_page_backdrop`).
- `HISCORE.png` replaces `HISCORE.PIC` (1586x992 RGB888, target 320x200, source `PNG`, runtime `truecolor_page_backdrop`).
- `ARMPIECE.png` replaces `ARMPIECE.PIC` (1586x992 RGBA8888, target 320x200, source `PNG`, runtime `truecolor_page_backdrop`).
- `VN.png` replaces `VN.SPR` (2240x1400 RGBA8888, target 224x168, source `PNG`, runtime `high_resolution_truecolor_map`).
- `start/menu/person/room.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/0.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/1.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/2.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/3.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/4.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/5.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).
- `start/menu/person/6.png` replaces `WALL.PIC and ARMPIECE.PIC briefing composition` (1600x1000 RGBA8888, target 320x200, source `PNG`, runtime `complete_briefing_person_set`).

Campaign radio cues:
- `sounds/voice_cue_000_sample0.wav` replaces `voice_cue_000_sample0`; script `sounds/voice_cue_000_sample0.txt`, source `WAV PCM`, runtime `campaign_local_wav_radio_cue` (7850 Hz, mono unsigned PCM8 radio at project sample rate).
- `sounds/voice_cue_001_sample4.wav` replaces `voice_cue_001_sample4`; script `sounds/voice_cue_001_sample4.txt`, source `WAV PCM`, runtime `campaign_local_wav_radio_cue` (7850 Hz, mono unsigned PCM8 radio at project sample rate).
- `sounds/voice_cue_002_sample2_variant0.wav` replaces `voice_cue_002_sample2_variant0`; script `sounds/voice_cue_002_sample2_variant0.txt`, source `WAV PCM`, runtime `campaign_local_wav_radio_cue` (7850 Hz, mono unsigned PCM8 radio at project sample rate).
- `sounds/voice_cue_003_sample2_variant1.wav` replaces `voice_cue_003_sample2_variant1`; script `sounds/voice_cue_003_sample2_variant1.txt`, source `WAV PCM`, runtime `campaign_local_wav_radio_cue` (7850 Hz, mono unsigned PCM8 radio at project sample rate).
- `sounds/voice_cue_004_sample2_variant2.wav` replaces `voice_cue_004_sample2_variant2`; script `sounds/voice_cue_004_sample2_variant2.txt`, source `WAV PCM`, runtime `campaign_local_wav_radio_cue` (7850 Hz, mono unsigned PCM8 radio at project sample rate).

Campaign intro music:
- `sounds/intro_music.asound.json` replaces `sounds/intro_music.asound.json` (generated_asound_intro_music, source `F15_ASOUND_MUSIC JSON`, runtime `campaign_local_asound_intro_music`).

Fonts:
- `fonts/font_1.ttf` installed from `--font`; replace it with another TTF/OTF to change campaign text rendering.
- `fonts/font_3.ttf` installed from `--font`; replace it with another TTF/OTF to change campaign text rendering.
- `fonts/font_4.ttf` installed from `--font`; replace it with another TTF/OTF to change campaign text rendering.

Aircraft GLB replacements:
- slot 000: `15FLT/shape_000_LiSiCin_Stolen_F15.glb` copied from `https://opengameart.org/content/fighter-jets#f15_eagle`, runtime `campaign_local_shape_glb`.
- slot 001: `15FLT/shape_001_Tonkin_Carrier_Target.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 002: `15FLT/shape_002_DaNang_Strike_Jet.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 003: `15FLT/shape_003_Hanoi_Radar.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 004: `15FLT/shape_004_MiG23_Far.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 005: `15FLT/shape_005_Tonkin_CAP_Radar.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 006: `15FLT/shape_006_SVN_Custom_Aircraft_006.glb` copied from `https://opengameart.org/content/fighter-jets#f15_eagle`, runtime `campaign_local_shape_glb`.
- slot 007: `15FLT/shape_007_SVN_Custom_Aircraft_007.glb` copied from `https://opengameart.org/content/fighter-jets#f15_eagle`, runtime `campaign_local_shape_glb`.
- slot 008: `15FLT/shape_008_SVN_Custom_Aircraft_008.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 009: `15FLT/shape_009_SVN_Custom_Aircraft_009.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 010: `15FLT/shape_010_MiG21_Escort.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 011: `15FLT/shape_011_SVN_Custom_Aircraft_011.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 012: `15FLT/shape_012_SVN_Custom_Aircraft_012.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 013: `15FLT/shape_013_SVN_Custom_Aircraft_013.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 014: `15FLT/shape_014_SVN_Custom_Aircraft_014.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 015: `15FLT/shape_015_MiG19_Patrol.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 016: `15FLT/shape_016_MiG17_Fresco.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 017: `15FLT/shape_017_SVN_Custom_Aircraft_017.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 018: `15FLT/shape_018_SVN_Custom_Aircraft_018.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 019: `15FLT/shape_019_SVN_Custom_Aircraft_019.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 020: `15FLT/shape_020_MiG29_Su27.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 021: `15FLT/shape_021_SVN_Custom_Aircraft_021.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.
- slot 022: `15FLT/shape_022_F4_Phantom_Opponent.glb` copied from `procedural_svn_campaign_generator`, runtime `campaign_local_shape_glb`.

Mission objective starter plan:
- `start_soviet_hq` slot 0: Soviet forward HQ [friendly] - Player staging area and campaign anchor for Lisicin / Li Si Cin.
- `strike_carrier_group` slot 6: USA carrier group [primary] - Main naval strike target in the Gulf of Tonkin.
- `strike_da_nang_airbase` slot 7: Da Nang airbase [primary] - Airbase strike target for runway, fuel, and aircraft concentration.
- `suppress_hanoi_radar` slot 3: Hanoi radar net [primary] - SAM/radar suppression target protecting North Vietnam.
- `interdict_haiphong_logistics` slot 4: Haiphong logistics [secondary] - Depot, bridge, port, or logistics interdiction target.
- `sweep_tonkin_patrol` slot 5: Tonkin patrol [secondary] - USA patrol aircraft / CAP encounter area.
Edit object placement and objective labels in the WLD JSON or map editor.

Route plan:
- `SVN.WLD.json` includes `campaign_route_plan` with approximate OSM-derived anchors.
- `wp_hq`: Gia Lam / Hanoi forward control at `27256,4893`.
- `wp_hanoi_radar`: Hanoi SAM radar belt at `27269,4750`.
- `wp_haiphong`: Haiphong port logistics at `28384,6972`.
- `wp_tonkin_cap`: Gulf of Tonkin CAP lane at `28523,9132`.
- `wp_carrier`: Gulf of Tonkin carrier box at `28300,10979`.
- `wp_danang`: Da Nang airbase at `21358,20256`.
- The map editor can show these legs with `Routes on`; dragging or numerically editing a routed object updates the matching waypoint/annotation coordinates.

Starter sortie sequence:
- `opening_sead`: Open the radar corridor (suppress_hanoi_radar) - Destroy or suppress the Hanoi radar net so Lisicin can move deeper into the theater.
- `carrier_strike`: Strike the carrier group (strike_carrier_group, sweep_tonkin_patrol) - Attack the USA carrier group while fighter patrols are displaced over the Gulf of Tonkin.
- `airbase_and_logistics`: Cut the air bridge (strike_da_nang_airbase, interdict_haiphong_logistics) - Hit Da Nang and Haiphong support targets to reduce USA sortie tempo.

Launch a specific sortie:
- `Open the radar corridor`: `./run_campaign.sh /path/to/F15_GAME opening_sead` or `./run_campaign.sh /path/to/F15_GAME 1`
- `Strike the carrier group`: `./run_campaign.sh /path/to/F15_GAME carrier_strike` or `./run_campaign.sh /path/to/F15_GAME 2`
- `Cut the air bridge`: `./run_campaign.sh /path/to/F15_GAME airbase_and_logistics` or `./run_campaign.sh /path/to/F15_GAME 3`

Mission target packages:
- `target_set_hanoi_corridor`: Hanoi radar corridor package - SEAD / ingress corridor opening.
- `target_set_tonkin_carrier`: Gulf of Tonkin carrier strike package - fighter sweep followed by anti-ship strike.
- `target_set_southern_air_bridge`: Da Nang and Haiphong air-bridge package - logistics interdiction plus airbase strike.
Each package defines target elements, attack order, and victory logic for editor/future runtime mission logic. The map editor shows matching packages in the selected-object details panel, highlights related package objectives on the map, and lets you edit package role, attack order, and victory JSON.

Editable briefing files:
- `briefings/01_opening_sead.md` for sortie `opening_sead`.
- `briefings/02_carrier_strike.md` for sortie `carrier_strike`.
- `briefings/03_airbase_and_logistics.md` for sortie `airbase_and_logistics`.

Edit workflow:

1. Edit WLD placement/objectives in `tools/f15assets/map_editor.html`.
2. Review generated `MISSION_TARGETS.md` for objective slots, target packages, attack order, and victory logic.
3. Edit `briefings/*.md` for mission text; packaging syncs each Markdown title/summary into the selected sortie fields shown in-game.
4. Replace PNG/WAV/font/GLB media files as needed.
5. Run `python3 -m tools.f15assets.cli refresh-campaign-inventory converted_assets_all/SVN --pretty` when you want to sync docs/inventory without zipping.
6. Run `python3 -m tools.f15assets.cli package-campaign converted_assets_all/SVN converted_assets_all/SVN.zip --pretty` to sync docs, refresh inventory, validate, and zip.
7. Optionally run full replacement validation with the command below.

`--pretty` is accepted both globally and on `package-campaign`; omit it only when you intentionally want compact JSON.

Validation:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --loadability-only
```
