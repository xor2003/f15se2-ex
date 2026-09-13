# SVN custom asset guide

This pack is a full modern-format customization test campaign.

Current fiction hook: the F-15 was stolen by Soviet pilot Lisicin; Vietnamese allies call him Li Si Cin.

## Fresh one-command build

```bash
python3 tools/f15assets/cli.py build-soviet-vietnam-pack /path/to/F15_GAME converted_assets_all --package --pretty
```

Package validation runs by default when `--package` is used. Use `--skip-package-validation` only for draft ZIPs; use `--validate` for full replacement validation.

## Graphics

- Replace campaign-local PNG files directly: `TITLE640.png`, `TITLE.png`, `DESK.png`, `WALL.png`, `HISCORE.png`, `ARMPIECE.png`, and theater map `VN.png`.
- PNGs may be higher resolution and truecolor; runtime scales them into the original game rectangles instead of treating source pixels as game-space size.
- Generated metadata records both source pixels (`width`/`height`) and in-game logical target size (`target_width`/`target_height`).
- Current target rectangles: `TITLE640.png` -> 640x350, page/backdrop PNGs -> 320x200, `VN.png` theater map -> 224x168.
- `runtime_path` and `legacy_limits_removed` in `campaign.json` tell tools that these PNGs are not constrained to original PIC/SPR resolution or palette size.
- Replace `start/menu/arm/*.png` for high-resolution transparent briefing pointer-arm cels.

## Audio

- Replace `sounds/voice_cue_*.wav` one-by-one for radio cues.
- Edit matching UTF-8 `sounds/voice_cue_*.txt` scripts so package docs describe the new cue text.
- Fresh SVN builds use local `espeak-ng`/`espeak` Russian TTS automatically when available; otherwise they fall back to deterministic radio-tone placeholders.
- TTS-generated WAVs are post-processed to mono unsigned 8-bit PCM at `7850` Hz with light radio static when the TTS command emits a readable WAV.
- Pass `--radio-dir /path/to/wavs` or `--radio-tts-command '...'` to force reviewed recorded or TTS cue sources.
- Replace `sounds/intro_music.asound.json` to customize campaign intro music.

## Models

- Replace `15FLT/shape_###_*.glb` files from Blender or another GLB authoring tool.
- Keep the `shape_###` prefix stable; runtime slot lookup depends on it.
- Delete stale generated `.glmesh` caches if you edit GLB files manually outside the helper commands.

## Fonts

- Replace `fonts/font_1.ttf`, `fonts/font_3.ttf`, and `fonts/font_4.ttf` with Cyrillic-capable TTF/OTF files.
- Runtime uses scalable fonts where the text path supports TTF/OTF, with legacy bitmap fonts as fallback.

## Scenario and mission targets

- Edit `SVN.WLD.json` for world objects, object placement, route anchors, and scenario metadata.
- Launch the full campaign with `--campaign SVN`; this also selects the base theater and campaign-local media.
- Launch just the custom world JSON with `--scenario SVN --scenario-base VN` when testing WLD placement without the campaign wrapper.
- Equivalent low-level environment variables are `F15_WORLD_SCENARIO` and `F15_WORLD_SCENARIO_BASE`; both still require `F15_REPLACEMENT_ROOT` to point at the converted/custom asset root.
- Review `MISSION_TARGETS.md` after changing target slots or sortie packages.
- Edit `briefings/*.md`; `package-campaign` syncs Markdown title/summary back into the WLD fields displayed on the START mission board.

## Capability map and validation

- `campaign.json` has an `objective_capabilities` block that maps the requested custom-campaign features to concrete files.
- Every provided capability lists campaign-relative `artifacts`; package validation rejects missing, absolute, or `..` artifact paths.
- Use this block as the machine-readable checklist for launchers, installers, and release review.
- If you add a new modern source file type or remove a generated file, update `objective_capabilities` before packaging.
- After running the game or full replacement validation externally, record that evidence with `record-campaign-verification` instead of hand-editing `verification_status`.
- `passed` and `failed` records require the matching `--*-command` argument; use `not_applicable` only when a validation category genuinely does not apply.

Example after external validation:

```bash
python3 tools/f15assets/cli.py record-campaign-verification converted_assets_all/SVN --runtime-launch passed --runtime-launch-command './f15se2-ex --game /path/to/F15_GAME --campaign SVN' --full-asset-validation passed --full-asset-validation-command 'python3 tools/f15assets/cli.py validate-replacements /path/to/F15_GAME converted_assets_all'
```

## Free/CC redistribution checklist

- Review or replace generated PNG art before distributing a free asset pack.
- Review or replace generated procedural GLBs with reviewed Blender-exported models if you need clean licensing provenance.
- Review or replace generated/TTS WAV radio cues; keep `sounds/*.txt` scripts aligned with the final audio.
- Use fonts whose licenses explicitly allow redistribution with the campaign.
- Keep `campaign.json`, `inventory.json`, and this guide updated before publishing a package.

## Repackage

From this campaign directory:

```bash
python3 "${F15SE2_EX_ROOT:-../..}/tools/f15assets/cli.py" package-campaign . ../SVN.zip --pretty
```

From the repository root:

```bash
python3 tools/f15assets/cli.py package-campaign converted_assets_all/SVN converted_assets_all/SVN.zip --pretty
```

Run package validation by omitting `--skip-validation`; use `--skip-validation` only for draft packages.

## Install packaged ZIP

Inspect before installing:

```bash
python3 tools/f15assets/cli.py inspect-campaign-package converted_assets_all/SVN.zip
```

Install into another converted/custom asset root:

```bash
python3 tools/f15assets/cli.py install-campaign-package converted_assets_all/SVN.zip /path/to/converted_assets_all --replace
```

Then run with:

```bash
F15_REPLACEMENT_ROOT=/path/to/converted_assets_all ./f15se2-ex --game /path/to/F15_GAME --campaign SVN
```
