# F-15 Asset Customization Guide

This guide describes the practical edit workflow for converted assets. The
rule is simple: edit the modern media file whenever it can represent the data,
and touch JSON only for tables, ids, placement, or metadata that the media file
cannot store.

Converted metadata should be portable. Sidecars should use original asset names
or paths relative to the converted folder, not absolute developer-machine paths.

## 1. Convert the original game folder

For the Soviet Vietnam custom campaign pack, the quickest path is one command:

```bash
python3 -m tools.f15assets.cli build-soviet-vietnam-pack /path/to/F15_GAME converted_assets_all
```

This converts original assets, generates `SVN/SVN.WLD.json`,
`SVN/campaign.json`, high-resolution truecolor PNG starter art, Russian radio
scripts/placeholders or local TTS WAVs, campaign-local `sounds/intro_music.asound.json`,
procedural campaign-local `SVN/15FLT/shape_###*.glb` aircraft placeholders,
editable Markdown sortie briefings, `SVN/SUMMARY.md`, `SVN/inventory.json`,
launch metadata, and source-free loadability hooks. Add `--font`,
`--aircraft-glb`, `--radio-dir`, or `--radio-tts-command` to install custom
media while building the pack. Add `--no-starter-music` if you want no generated
campaign intro music replacement.

Use `--package` to create `SVN.zip`. Package validation runs by default and
records package evidence in the campaign manifest; use
`--skip-package-validation` only for draft ZIPs.

Example with Cyrillic TTF and local `espeak-ng` Russian radio cue generation:

```bash
python3 -m tools.f15assets.cli build-soviet-vietnam-pack /path/to/F15_GAME converted_assets_all --font /path/to/cyrillic.ttf --radio-tts-command 'espeak-ng -v ru -s 145 -p 35 -w "{output}" "{text}"'
```

Readable TTS WAV output is normalized to the same simple format expected by the
legacy digitized-radio path: mono unsigned 8-bit PCM at 7850 Hz, with light
deterministic radio static. If you pass `--radio-dir`, your reviewed WAV files
are copied as authored instead.

For a plain conversion without the custom campaign scaffold, run:

Run from the repository root:

```bash
python3 -m tools.f15assets.cli convert-all /path/to/F15_GAME converted_assets_all
```

Use your installed DOS game folder for `/path/to/F15_GAME`. The output folder is
the folder the game replacement loader and editor tools should use.
`convert-all` recurses through the game folder so campaign/theater subdirectory
assets are converted too.
Default output is minimized for customization. Use `--include-image-json`,
`--include-3d3-model-data`, `--include-metadata`, or `--include-raw-blob` only
when you need reverse-engineering diagnostics.

To run the game with replacements stored outside the original game folder, set:

```bash
F15_REPLACEMENT_ROOT=/path/to/converted_assets_all
```

The runtime then searches that folder for PNG, JSON, BDF, WAV, MP3, OGG, GLB, and GLMESH
replacements before falling back to the usual converted/output locations. If
you point `F15_REPLACEMENT_ROOT` at the parent folder instead, a
`converted_assets_all` child is also searched. A missing replacement root is
reported once, ignored, and the usual fallback locations are still searched.
If the game is launched from a directory where the repository `tools` folder is
not discoverable, also set:

```bash
F15_ASSET_TOOL="python3 /path/to/f15se2-ex/tools/f15assets/cli.py"
```

That bridge command is used only when the runtime must rebuild JSON tables or
generated GLMESH caches from editable modern files.

## 2. Check replacement equivalence before editing

Run this once after conversion if you want proof that the unmodified converted
files still load like the original assets:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --allow-custom-glb-differences
```

For strict converter regression proof before editing, use:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --require-all --strict-original-proof
```

After editing PNG, WAV, font, JSON, or GLB assets, use loadability validation:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --loadability-only
```
For source-free custom campaign packs, this also validates `campaign.json`,
its referenced WLD JSON, generated PNG/WAV/font/model files, sortie/objective
references, and `route_plan` / `campaign_route_plan` waypoint consistency.
`inspect-campaign-package` and `verify-campaign-package` also report route
waypoint/sortie-route counts and reject packages whose route legs reference
missing waypoints or disagree with the packaged WLD route plan.
`inspect-campaign-package --json` also includes `verification_summary`, which
records whether runtime launch, source-free replacement loadability, and full
asset replacement validation have been recorded after external execution.
`install-campaign-package` runs the same archive checks before extraction, so a
broken route package is rejected without partially installing a campaign folder.
For a source-free custom pack without the original game folder, the first path
may be a missing placeholder when `--loadability-only` is used. In that mode the
validator checks modern files directly from `converted_assets_all` and skips
original-equivalence proof. The output path may be either the `converted_assets_all`
folder itself or a parent folder containing it; the validator prints the
normalized directory when it descends to the child folder. Do not combine
source-free validation with `--require-all`; completeness checks need the
original asset inventory. If no replacement files are found, the validator
prints a warning so a wrong output path is visible.
Do not combine `--loadability-only` with `--strict-original-proof` or
`--require-source-proof`; those flags are for unmodified converter proof.
`--loadability-only` already permits custom GLB content differences, so do not
combine it with `--allow-custom-glb-differences`.

The validator compares original loader output with converted PNG, WAV, BDF/PNG,
JSON, GLB, and GLMESH replacement data where implemented.
For sound-only custom packs without `F15DGTL.BIN`, validation still parses and
checks the separate cue WAVs; it cannot prove byte-equivalence to the original
blob because there is no original blob to compare.
For source-free world/table packs, `--loadability-only` also rebuilds
`*.WLD.json`, `*.3DT.json`, and `*.3DG.json` files found under the converted
output folder even when the original binary tables are absent.
For source-free image packs, `--loadability-only` also checks extra PNG files
under the converted output folder for runtime-loadable dimensions and embedded
palette requirements for indexed PNGs. Original-equivalence still requires the
matching original `PIC`/`SPR` file.
For source-free 3D packs, `--loadability-only` also checks extra `.glb` and
`.glmesh` files under the converted output folder for parseability and non-empty
renderable primitives. Exact primitive/order/color proof still requires the
matching original `.3D3` file.
It also sanity-checks source-free minimized `.3D3.json` index files for
parseable slot metadata, monotonic offsets, and valid `model_data_size`.
For source-free font packs, `--loadability-only` also parses extra
`fonts/font_*.bdf` files and checks that glyph rows and advance widths are
usable for all 96 runtime glyphs from U+0020 through U+007F. It also checks
`fonts/font_*.png` atlases for the current runtime font ids whose cell
dimensions are known. Built-in font equivalence still requires the in-repo
captured font tables.

Use equivalence checks before editing. After intentional customization,
differences from the original asset are expected for the edited file, so
loadability validation is the useful check for customized packs.
The normal command treats generated `cache/*.glmesh` and GLB source proof extras
as optional diagnostics. `--allow-custom-glb-differences` also treats intentional
per-shape GLB geometry changes as warnings. `--strict-original-proof` requires
generated caches and source proof metadata.

Comparison is intentionally not done while playing. Use
`validate-replacements` or CTest before editing when you need old-vs-modern
proof; use `--loadability-only` after intentional customization.

## 3. 3D models: edit GLB in Blender

Authoritative file:

```text
converted_assets_all/<group>/shape_###_<name>.glb
```

Examples:

```text
converted_assets_all/15FLT/shape_010.glb
converted_assets_all/VN/shape_049_SAM_Radar.glb
converted_assets_all/PHOTO/shape_000.glb
```

Normal Blender workflow:

1. Import the per-shape `.glb` into Blender.
2. Edit geometry, line objects, point objects, and materials.
3. Export back to the same `.glb` path.
4. Keep the `shape_###` number in the filename unchanged.
5. Start the game with the OpenGL backend; it rebuilds `cache/*.glmesh`
   automatically if the cache is missing or stale.

The filename label after `shape_###` is only for humans. Runtime lookup is
case-tolerant and uses the stable slot number prefix.

Current limitation: GLB model replacement is implemented in the OpenGL backend.
The software 3D backend still renders the original `.3D3` stream, and the
current loader still reads original `.3D3` bytes for shape-slot tables, PHOTO
append ranges, comparison, and fallback. The minimized `.3D3.json` index now
validates those slot fields, but the game does not yet use it as the runtime
source for GLB-only/free-asset packs.

Do not edit these for normal model customization:

```text
converted_assets_all/<group>/<group>.3D3.json
converted_assets_all/<group>/cache/*.glmesh
```

`.3D3.json` is a lightweight index/manifest. It preserves shape slots and source
container metadata, but GLB geometry and GLB primitive extras override it. Bulk
conversion omits bulky `.3D3` `model_data` base64 by default; validation still
compares the minimized shape offset table and `model_data_size` against the
original `.3D3`. Request bulky `model_data` only with
`convert-tree --include-3d3-model-data` for reverse-engineering dumps or for
the legacy-byte-stream bridge. Those full dumps duplicate original model data
and are not the normal customization surface.
`cache/*.glmesh` is generated runtime data derived from GLB and can be deleted.
Default conversion does not pre-generate it; use `--include-glmesh-cache` only
when you want caches ahead of first runtime load. The runtime and validator
parse these caches strictly, so malformed or stale caches are rejected instead
of being treated as an alternate editable model source.

Keep these constraints:

- Preserve the shape slot number. World objects reference shape ids.
- Shared aircraft models live in `converted_assets_all/15FLT/shape_###*.glb`.
  Campaign-local aircraft overrides live under
  `converted_assets_all/<campaign>/15FLT/shape_###*.glb` and are preferred only
  while that campaign is selected. Keep the `shape_###` prefix stable in either
  location. Theater directories replace ground/world shapes; campaign-local
  `15FLT/` is the scoped way to replace aircraft visuals without changing every
  campaign.
- The game currently maps aircraft behavior, names, weapons, speed, and AI from
  its aircraft type tables and mission/world data. Replacing a `15FLT` GLB
  changes the rendered model for that slot; it does not by itself change flight
  model, faction, weapons, or mission generation. For a Soviet-in-Vietnam
  campaign, use WLD/mission edits for scenario placement and use selected
  `15FLT/shape_###*.glb` swaps for the aircraft visuals.

## 4. Campaign sounds and intro music

Radio and effects customizations use separate OGG, MP3, or WAV files:

```text
converted_assets_all/<campaign>/sounds/voice_cue_*.{ogg,mp3,wav}
```

Campaign intro music can be supplied as:

```text
converted_assets_all/<campaign>/sounds/intro_music.ogg
converted_assets_all/<campaign>/sounds/intro_music.mp3
```

OGG is preferred over MP3. If neither exists, the Soviet Vietnam starter pack
also generates:

```text
converted_assets_all/SVN/sounds/intro_music.asound.json
```

The compressed file is decoded once and mixed by the existing game audio
callback. It needs no JSON sidecar. Cue lookup uses OGG, then MP3, then WAV;
the ASOUND JSON remains the final intro fallback. For ordinary spoken Russian
radio, replace individual cue files while preserving their basename and any
matching `.txt` script.

## 5. Campaign theater maps

Campaign-local theater map PNGs such as `converted_assets_all/SVN/VN.png` replace
the matching legacy `.SPR` map. In the OpenGL/native-overlay debrief path, the
PNG is loaded as RGBA and scaled into the original 224x168 map viewport, so
customizers can use higher resolution and truecolor without being reduced to the
legacy sprite palette. The indexed fallback path still exists for software/legacy
rendering.
- Validate intentional aircraft GLB swaps with
  `validate-replacements --allow-custom-glb-differences` or
  `--loadability-only`; strict original proof is expected to warn/fail after
  intentional model edits.
- `PHOTO/shape_###.glb` files replace target-view photo models. The legacy
  loader appends those models to the theater shape table, but the OpenGL
  replacement lookup maps the appended slots back to `PHOTO/shape_###`.
- Preserve line-only details such as antennas, masts, deck lines, runway lines, and ship rails unless you intentionally remove them.
- Preserve coplanar faces and draw-sensitive overlaps when visual fidelity matters. The converter stores source primitive metadata so validation can detect dropped or reordered primitives.
- Use per-shape GLBs for editing. The combined `<group>.3D3.glb` is mainly for overview and inspection.
- GLB `extras` are diagnostic metadata, not the drawing source. If Blender or
	  another editor strips primitive extras, the model can still render from GLB
	  geometry/materials, but strict `validate-replacements` has less proof that
	  source order/raw colors still match the original.

When JSON is needed:

- Rename or document shape slots in metadata.
- Inspect source offsets or original shape order.
- Debug conversion issues.

When JSON is not needed:

- Changing mesh vertices.
- Changing face colors/materials.
- Adding/removing Blender geometry inside the same shape file.
- Re-exporting the same shape from Blender.

## 4. World and mission data: edit WLD JSON

Authoritative file:

```text
converted_assets_all/<world>/<world>.WLD.json
```

Use the map editor for placement edits:

```text
tools/f15assets/map_editor.html
```

To start a Soviet-in-Vietnam custom campaign scaffold from converted Vietnam
data:

```bash
python3 -m tools.f15assets.cli new-campaign converted_assets_all/VN/VN.WLD.json converted_assets_all
```

Optional one-command font install:

```bash
python3 -m tools.f15assets.cli new-campaign converted_assets_all/VN/VN.WLD.json converted_assets_all --font /path/to/cyrillic-font.ttf
```

Optional one-command aircraft visual override:

```bash
python3 -m tools.f15assets.cli new-campaign converted_assets_all/VN/VN.WLD.json converted_assets_all --aircraft-glb 10=/path/to/mig21.glb
```

Optional one-command Russian radio install:

```bash
python3 -m tools.f15assets.cli new-campaign converted_assets_all/VN/VN.WLD.json converted_assets_all --radio-dir /path/to/russian-radio-wavs
```

The radio directory should contain files using the generated cue names, for
example `voice_cue_000_sample0.wav`. These files replace the generated
radio-style placeholders and are recorded in `campaign.json` with their source
paths.

Optional direct TTS generation:

```bash
python3 -m tools.f15assets.cli new-campaign converted_assets_all/VN/VN.WLD.json converted_assets_all --radio-tts-command 'espeak-ng -v ru -s 145 -p 35 -w "{output}" "{text}"'
```

The command template is run once per cue. It can use `{text}` for the Russian
line, `{output}` for the target WAV path, `{cue}` for the cue id, and `{script}`
for the script text path. `--radio-dir` is applied after TTS, so hand-recorded
files can override generated speech.

This creates `converted_assets_all/SVN/SVN.WLD.json`,
`converted_assets_all/SVN/campaign.json`, and `converted_assets_all/SVN/README.md`.
The manifest is the modern custom-pack entry point for launchers/editor tooling;
its `launch` block gives the normal `--campaign SVN` invocation, while
`runtime_selection` stores the lower-level WLD redirect details. At runtime,
`--campaign` also selects the manifest's base theater before mission generation,
so the player does not need to manually choose Vietnam for `SVN`. The WLD JSON
remains the authoritative editable world/scenario data; the
per-campaign README summarizes replaceable art, sound, font, and aircraft files.
Use `python3 -m tools.f15assets.cli list-campaigns converted_assets_all` to
discover installed campaign manifests, their launch examples, and the current
runtime/full-replacement validation status columns.
`campaign.json` and `SVN.WLD.json` both declare
`F15SE2_MODERN_CAMPAIGN_WLD` schema version `1` for scenario-authoring fields.
`SVN/inventory.json` lists campaign-pack files, their roles, source-of-truth
status, license-review status, byte sizes, and SHA-256 hashes for
packaging/review workflows. `validate-replacements --loadability-only` checks
those hashes when present.
After editing campaign files, package the campaign. This synchronizes duplicated
manifest fields from WLD/briefing sources, rewrites README/SUMMARY, refreshes
inventory hashes, validates media metadata, and writes the ZIP:

```bash
python3 -m tools.f15assets.cli package-campaign converted_assets_all/SVN SVN.zip
```

For a docs/inventory preview without zipping, run:

```bash
python3 -m tools.f15assets.cli refresh-campaign-inventory converted_assets_all/SVN
```

The ZIP contains `SVN/package.json`, which records whether shared external
assets were included or intentionally left external.
Use `verify-campaign-package` before sharing or installing a ZIP. It checks the
archive top-level layout, unsafe paths, duplicate entries, package/inventory
metadata, file hashes/sizes, launcher metadata, and external-asset declarations.
`install-campaign-package` uses the same archive inspection before extracting,
then performs only install-specific overwrite and post-install manifest checks.

Inspect a ZIP before installing:

```bash
python3 -m tools.f15assets.cli inspect-campaign-package SVN.zip
```

Inspection checks required manifests, inventory hashes, byte sizes, and included
shared-asset references before installation.

For a scriptable pass/fail gate:

```bash
python3 -m tools.f15assets.cli verify-campaign-package SVN.zip
```

If the manifest references shared assets outside `SVN/`, such as installed
`15FLT/shape_###*.glb` aircraft, packaging fails by default. Use
`--include-external-assets` to include those shared files in the ZIP. Use
`--allow-external-assets` only for a campaign-only ZIP when those shared assets
will be distributed separately.

To install that ZIP into another converted asset root:

```bash
python3 -m tools.f15assets.cli install-campaign-package SVN.zip converted_assets_all
```

Preview without extracting:

```bash
python3 -m tools.f15assets.cli install-campaign-package SVN.zip converted_assets_all --dry-run
```

Dry-run also reports existing campaign directories or shared `15FLT/` assets
that would require `--replace`; it does not extract or overwrite anything.

Installation validates the campaign manifest, inventory hashes, and referenced
shared aircraft files. A campaign-only ZIP that references `15FLT/` assets will
install only if those shared files are already present in the target converted
asset root.

Installing fails if the campaign directory already exists. Use `--replace` only
when you intentionally want to overwrite the existing installed campaign. If the
ZIP includes shared `15FLT/` assets, `--replace` also controls overwriting those
shared files. Installation restores executable mode on `run_campaign.sh`.
The WLD contains campaign metadata for Soviet spy pilot Lisicin, known to
Vietnamese allies as Li Si Cin after stealing an F-15 plane, target annotations,
starter USA target names, objective ids, and a first-pass sortie sequence
covering Hanoi SEAD, a Tonkin carrier strike, and Da Nang/Haiphong
interdiction. It preserves loadable WLD tables, so you can immediately open it
in the map editor, move/copy/delete objects, then run
`validate-replacements --loadability-only` against the custom pack. Plane
visuals for this campaign are separate `15FLT/shape_###*.glb` swaps.
When an object is selected, the map editor lets you edit the objective id,
priority, faction, domain, action, target type, threat, desired effect, loadout,
success criteria, radio cue ids, and intent. It also shows sortie phases that
reference that objective, so target placement and campaign flow can be edited
together. Renaming an objective updates the sortie objective references in the
exported WLD JSON.
Human-facing briefing text lives in `converted_assets_all/SVN/briefings/*.md`.
Edit those Markdown files for mission presentation; keep objective ids aligned
with `campaign.json` and `SVN.WLD.json`.
Load `SVN/campaign.json` in the map editor instead of only `SVN.WLD.json` when
you want selected-object sortie cards to show the linked briefing file paths.

By default the scaffold also creates campaign-local high-resolution truecolor
PNG art for title/menu and pilot/briefing screens:

```text
converted_assets_all/SVN/TITLE.png
converted_assets_all/SVN/TITLE640.png
converted_assets_all/SVN/DESK.png
converted_assets_all/SVN/WALL.png
converted_assets_all/SVN/HISCORE.png
converted_assets_all/SVN/ARMPIECE.png
```

These files replace the matching legacy PIC names while the campaign is active.
`TITLE640.png` is the actual main title replacement for `Title640.Pic`;
`TITLE.png` is a generic title/menu alias. Edit or replace the PNGs directly;
the runtime fits them into the original screen rectangles instead of making
high-resolution art appear oversized. `TITLE640.png` is preserved through the
hi-res title path as truecolor RGBA when it is not an indexed PNG; other legacy
full-page PIC replacements can also preserve truecolor RGBA source resolution as
a page backdrop, with later legacy indexed UI/text composited over it. The
native-overlay briefing room also prefers campaign-local `WALL.png` as an RGBA
window-filling backdrop. Optional transparent pointer-arm cels can be supplied as
`converted_assets_all/SVN/start/menu/arm/0.png` through `6.png`; if they are
absent, the legacy arm sprite behavior remains the fallback.
Most other sprite replacements still load into the original indexed game buffers.
Keep `TITLE640.png` in the original 640:350 aspect ratio when possible; the
runtime preserves truecolor source resolution and fits it into the original title
rectangle, so oversized custom art does not become a giant UI surface.
`campaign.json` also records `media_policy.images`, declaring PNG as the source
of truth and `fit_original_game_rectangle` as the scaling contract. Launcher and
editor tooling should use that policy rather than assuming legacy 320x200
indexed art limits.
Campaign validation reads each campaign-art PNG header and checks that the
manifest width, height, and color model match the actual file. After replacing
high-resolution art, run `refresh-campaign-inventory` so the human docs,
inventory hashes, and manifest-derived metadata stay synchronized.
`package-campaign` runs the same campaign manifest/media validation before it
writes a ZIP. Use `--skip-validation` only for an intentionally broken draft.
Campaign WAV cue entries are checked the same way: validation reads the RIFF/WAVE
header and verifies declared channels, sample rate, and duration against the
actual custom radio file.
Campaign intro music is `sounds/intro_music.asound.json`. Validation checks the
`F15_ASOUND_MUSIC` header, positive tick rate, all 12 intro/release voice
streams, byte-range values, and ASOUND zero-note stream terminators.
Installed font overrides are checked for a plausible TTF/OTF/BDF/PNG container
matching their file extension, so a bad copied file fails validation before the
game tries to render text.
Installed aircraft entries are checked for unique slots and valid GLB v2 files
with at least one mesh. Converter-specific GLB extras are not required for custom
Blender replacements; slot number and geometry are the runtime contract.
`campaign.json` also records `runtime_contract`: which parts are modern-editable
now and which behavior is still legacy-driven or fallback-backed. Treat that as
the practical boundary when building free/custom asset packs.
Validation also checks that `world`, `base_theater`, `runtime_selection`, and
`launch` agree with the campaign id. If you rename a campaign, rename the WLD
JSON and refresh the manifest instead of editing only one field.
When a campaign is selected, the runtime reads the selected sortie's
`primary_objective_ids` and `secondary_objective_ids`, maps them through
`mission_objectives[].object_slot`, and uses those slots as preferred mission
targets. The original generator still validates the selected objects against the
WLD object table, mission table, base search, distance checks, and fallback logic.
The default selected sortie is the first `sortie_sequence` entry. Use
`--campaign-sortie opening_sead`, `--campaign-sortie 2`, or
`F15_CAMPAIGN_SORTIE` to select by sortie id or numeric phase. The START mission
briefing board also uses the selected sortie's `title` as the mission header
while keeping the legacy takeoff/primary/secondary target layout.
It also records `asset_license_review`. The generated art and radio files are
starter placeholders; replace or review them, and review any user-supplied
fonts/models/voice files, before distributing a free CC/custom asset pack.
By default the scaffold also creates high-resolution campaign-local starter art,
generated radio-style cues, and procedural campaign-local GLB aircraft
placeholders:

```text
converted_assets_all/SVN/TITLE.png
converted_assets_all/SVN/DESK.png
converted_assets_all/SVN/VN.png
converted_assets_all/SVN/15FLT/shape_###_*.glb
converted_assets_all/SVN/sounds/voice_cue_*.wav
converted_assets_all/SVN/sounds/voice_cue_*.txt
```

The PNGs are truecolor and higher resolution than the original screens; runtime
loading scales them into the original game rectangles. The WAV files are
procedural radio placeholders, not real speech. The matching `.txt` files
contain Russian radio-style cue scripts and English glosses. The GLBs are simple
colored procedural surfaces with line primitives for antennas/masts; they are
intended to be opened in Blender and replaced, not treated as final art. Replace
the WAVs with recorded Russian radio voice lines later using the same filenames,
edit the scripts as needed, or pass `--no-starter-art`, `--no-starter-radio`, or
`--no-starter-aircraft` when creating the campaign.
For Cyrillic-capable UI text, copy a TTF/OTF font into the optional campaign font
override slots listed in `campaign.json`: `SVN/fonts/font_1.ttf`,
`SVN/fonts/font_3.ttf`, and `SVN/fonts/font_4.ttf`. TTF/OTF are preferred for
Unicode; BDF or PNG atlas files with the same `font_<id>` stem remain valid for
bitmap-font workflows. Passing `--font` to `new-campaign` copies one local
TTF/OTF into the normal campaign font slots automatically.
Passing `--aircraft-glb SLOT=PATH` to `new-campaign` replaces that one
procedural campaign-local `SVN/15FLT` shape slot and records the user-supplied
file in `campaign.json`. The slot number remains the runtime lookup key. Use
the separate `install-aircraft-glb` command only when you intentionally want a
global/shared `converted_assets_all/15FLT` swap.

The generated `mission_plan.objectives[]` list is the editable mission design
outline. It defines the starter Soviet HQ, USA carrier group, Da Nang airbase,
Hanoi radar net, Haiphong logistics target, and Tonkin patrol area. Each entry
has a stable objective id, priority, role, intent, starter `object_slot`, target
type, threat level, desired effect, suggested loadout, radio cue ids, and
success criteria. These are authoring fields for the map editor, campaign
briefings, and future mission tooling; they do not need to match legacy WLD byte
tables exactly.
The matching `world_objects[]` and `target_annotations[]` entries repeat the
objective id, priority, target type, threat, intended effect, loadout, success
criteria, and radio cues so editor tools can keep labels and target meaning
attached while you move, copy, delete, or replace map objects.
`map_editor.html` displays those objective ids, priorities, and intent strings
plus target type, threat level, desired effect, and suggested loadout in the
selected-object details when a WLD JSON has `mission_plan.objectives[]`.
The scaffold also stores `campaign.base_theater` and `runtime_selection` so the
pack records how the game should select it. The base theater defaults to the
template filename, for example `VN.WLD.json` becomes `VN`; pass
`--base-theater` to `new-campaign` if you intentionally want another legacy WLD
stem. `--campaign` uses this value to skip the normal theater picker and load
the matching modern scenario deterministically.

Run the game with that custom WLD scenario selected:

```bash
F15_REPLACEMENT_ROOT=converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN
```

The generated campaign directory also contains a helper launcher:

```bash
converted_assets_all/SVN/run_campaign.sh /path/to/F15_GAME
```

Set `F15_EXE=/path/to/f15se2-ex` when running the launcher from somewhere that
does not have `./f15se2-ex` in the current directory.

`--campaign SVN` reads `SVN/campaign.json` inside the replacement root and
redirects the manifest's base WLD (`VN.WLD`) to `SVN/SVN.WLD.json`. You can also
set `F15_CAMPAIGN=SVN`. For manual/debug use, `--scenario SVN --scenario-base VN`
or `F15_WORLD_SCENARIO=SVN F15_WORLD_SCENARIO_BASE=VN` bypasses the manifest.
This is a WLD-only redirect scoped to the selected base theater; related
terrain/model files still load by their normal theater names or modern
replacements.
While a campaign is selected, campaign-local PNG/WAV/font/model replacements are
preferred before global replacements, so `SVN/TITLE.png`, `SVN/sounds/*.wav`,
`SVN/fonts/*`, and `SVN/15FLT/shape_###*.glb` affect only that campaign run.

To replace one aircraft visual slot with a custom GLB:

```bash
python3 -m tools.f15assets.cli install-aircraft-glb my_mig21.glb converted_assets_all --slot 10 --label MiG21
```

The command copies the GLB into `converted_assets_all/15FLT/`, preserves the
stable `shape_###` slot prefix, and removes stale generated `cache/*.glmesh`
files for that slot.

World JSON is the source of truth for:

- object coordinates;
- object shape ids;
- target flags;
- takeoff/landing/base-like records;
- flight-unit templates;
- terrain and mission classification tables;
- name strings.

GLB files do not store world placement. If you move a SAM site, runway, carrier,
or target on the theater map, edit the WLD JSON through the editor.

Runtime note: the current loader rebuilds a legacy `.WLD` byte stream from this
JSON and feeds it to the existing game loader. JSON is still the editable source
because world data is structured table data, not a media file.

## 5. Terrain tables: edit 3DT and 3DG JSON carefully

Authoritative files:

```text
converted_assets_all/<group>/<file>.3DT.json
converted_assets_all/<group>/<file>.3DG.json
```

Use these for terrain/tile placement and lookup data. These formats are numeric
tables, not artwork. Keep record order stable unless you understand the runtime
lookup path.

Runtime note: as with WLD, the current loader rebuilds legacy bytes from JSON
before calling the existing table loaders.

## 6. Pictures and sprites: edit PNG

Authoritative files:

```text
converted_assets_all/<asset>.png
```

PNG is authoritative for:

- pixels;
- source dimensions;
- embedded palette.

PIC/SPR JSON is only decode/index metadata and is not emitted by default by
`convert-tree`/`convert-all`. For normal art changes, edit the PNG directly.
Replacement PNGs may be higher-resolution than the original and may be
truecolor; the runtime fits them into the original game destination rectangle so
menus, cockpits, and sprites do not become giant. Full-page truecolor PIC
replacements are kept as RGBA backdrops at source resolution for presentation,
while later indexed legacy drawing is scaled/composited on top in the original
320x200 coordinate space. Sprite replacements still use indexed game buffers,
so truecolor sprite pixels are mapped to the active game palette at load time.
Indexed PNG with an embedded palette is still the safest self-contained
replacement when you want exact palette control.

Use `ctest -R asset_replacement_full_validation` or
`python3 -m tools.f15assets.cli validate-replacements` to compare indexed PNG
replacements against the original PIC/SPR pixel indices and active legacy DAC
palette. This exact proof intentionally requires indexed PNG at the original
dimensions. Use `--loadability-only` after custom high-resolution or truecolor
art edits. These checks are test-time tools, not runtime game modes.

`TITLE640.PIC` is exported as a 640x350 PNG. It is also loaded as PNG at runtime
when present, using the hi-res title path rather than the normal 320x200 page
surface. Indexed `TITLE640.png` updates the active palette like other indexed
PNG replacements; truecolor `TITLE640.png` is preserved at source resolution and
presented as RGBA inside the original 640x350 title rectangle, so the title
screen can use more than the legacy EGA/VGA palette without losing source detail.

## 7. Fonts: edit TTF/OTF or BDF

Authoritative files:

```text
converted_assets_all/fonts/font_<id>.ttf
converted_assets_all/fonts/font_<id>.otf
converted_assets_all/fonts/font_<id>.bdf
```

Preferred workflow:

1. Use `font_<id>.ttf` or `font_<id>.otf` for normal scalable-font replacement.
2. Use `font_<id>.bdf` only when you need hand-edited pixel glyphs and metrics.
3. Ignore JSON for normal customization; `font_<id>.json` is optional metadata
   emitted only with `--include-metadata`.

TTF/OTF is preferred for Unicode/localization because customizers can use normal
font editors and do not need sidecar files. Runtime loads TTF/OTF first when the
build has FreeType, then BDF, then legacy `font_<id>.png` atlases. Leave
`font_0.*` absent to keep the original tiny HUD font. JSON cannot override font
glyph data.

## 8. Sounds: edit separate cue files

Authoritative files:

```text
converted_assets_all/sounds/voice_cue_*.ogg
converted_assets_all/sounds/voice_cue_*.mp3
converted_assets_all/sounds/voice_cue_*.wav
```

For each basename, runtime precedence is OGG, MP3, then WAV. MP3 and OGG may use
their native sample rate and channels; the runtime decodes them to its internal
mono cue stream. `sounds/f15dgtl_raw.wav` is not exported by default. It is only a full-blob
reference export when `export-sounds --include-raw-blob` is used. Customizers
do not need it. The current runtime replacement path uses the separate
`voice_cue_*.wav` files for playback.

WAV files are authoritative for:

- sample bytes;
- sample rate;
- channel count;
- bit depth.

Use mono unsigned 8-bit PCM for maximum compatibility with the current loader.
The current runtime cue loader accepts mono unsigned 8-bit PCM cue WAVs and uses
their sample bytes and RIFF sample rate as replacements for the matching ASOUND
cue ranges. The default exported sample rate is `7850` Hz; changing a cue WAV's
sample rate intentionally changes playback speed. JSON keeps cue ranges and
driver metadata; do not edit JSON for normal sample replacement. Cue JSON,
driver sidecars, and `sounds.json` are not exported by default; use
`export-sounds --include-metadata` only when you need reverse-engineering notes.
Rerunning the exporter without metadata/raw flags removes stale optional sound
sidecars, leaving the cue WAVs as the normal customization files.
`--loadability-only` also parses extra `sounds/voice_cue_*.wav` files in a
source-free pack, but the current runtime playback path uses only the recovered
known cue filenames exported by `export-sounds`.

## 9. What the game loads first

When a modern replacement exists, the runtime should prefer it:

| Asset class | Preferred replacement |
| --- | --- |
| 3D shape | per-shape `.glb`; `cache/*.glmesh` is generated from it |
| PIC/SPR image | `.png` |
| WLD/3DT/3DG table | `.json` rebuilt through `build-binary` |
| Font | `.bdf` for glyphs and metrics; `.png` atlas fallback for glyph pixels only |
| Digitized sound cue | separate `.wav` |

If a replacement is missing or invalid, the loader should fall back to the
original game asset.

## 10. Files you can usually ignore

```text
cache/*.glmesh
default *.3D3.json for geometry-only edits; full model_data dumps only for bridge/reverse-engineering work
*.PIC.json / *.SPR.json for pixel-only edits; request them only with --include-image-json for decoder diagnostics
fonts/font_<id>.json for normal glyph edits; request it only with --include-metadata for glyph/index diagnostics
optional sounds/sounds.json for sample-only edits
```

These files are useful for validation, indexing, or loader bridges, but they are
not the normal authoring surface when the modern media file already represents
the data.

## 11. Future free custom asset packs

The long-term target is a replacement pack that can contain only freely licensed
custom assets, for example Creative Commons models, images, fonts, and sounds.
That is a packaging goal, not the current guarantee.

Current state:

- Original game assets are still required as fallback data.
- Default minimized `.3D3.json` plus per-shape GLB still need an original
  `.3D3` byte stream for shape-slot tables and software renderer fallback. A
  full `.3D3.json` bridge dump can replace those bytes, but it duplicates
  original model data and is not the desired free/minimized asset path.
- Some replacement paths still bridge through original-compatible binary layouts.
- Runtime comparison uses original assets as the reference for equivalence.

Future pack requirements:

- Every loaded asset class must have a modern replacement loader.
- Missing original assets must not be needed for supported replacement packs.
- The pack should include license metadata for every media file.
- Sidecars should stay portable and should not contain local absolute paths.
- Generated caches such as `cache/*.glmesh` should be reproducible from the
  editable source files and should not be the legal/source artifact.

It also shows editable `mission_target_sets` in the selected-object details panel, highlights related package objectives on the map, and lets customizers change package role, attack order, and victory JSON while placing targets. `SVN.WLD.json` is authoritative for mission edits; briefing Markdown is authoritative for human-facing sortie title/summary/radio-cue metadata; `refresh-campaign-inventory` and `package-campaign` sync duplicated mission summaries back into `campaign.json` before hashing or zipping.
