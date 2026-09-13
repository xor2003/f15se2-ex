# Redraw sprites without moving them

## Redraw the body and arm together

### Recommended runtime format: complete people

The GPU briefing renderer accepts a room-only background and seven complete
person poses. Put these PNGs in `start/menu/person/` under your replacement root
(for SVN: `converted_assets_all/SVN/start/menu/person/`):

```text
room.png    Opaque room and empty briefing board; no person or menu text
0.png       Transparent complete person pointing at menu row 1
1.png       Transparent complete person pointing at menu row 2
2.png       Transparent complete person pointing at menu row 3
3.png       Transparent complete person pointing at menu row 4
4.png       Transparent complete person pointing at menu row 5
5.png       Transparent complete person in transition
6.png       Transparent complete person resting
```

Every image must have the same full-scene canvas dimensions. Keep the feet,
body placement and board aligned across poses, but redraw the whole body and arms
freely. Nothing needs to fit ARMPIECE rectangles. PNG alpha controls transparency;
white and black clothing are both allowed. Menu text remains drawn by the game.
The person is drawn over that text, so keep the board readable except where the
pointer intentionally crosses it. Room and person use the same screen transform.

There is no sidecar and no rebuild step: edit the pose PNGs directly and restart
the game. All eight images must load, otherwise the whole set falls back to the
existing WALL/ARMPIECE path. Software rendering still uses that existing path.
Do not delete WALL.png or ARMPIECE.png; they remain the fallback assets.
The built-in asset fallback directory is `assets/start/menu/person/`.

To start from the current art, make a grayscale body mask at WALL.png resolution:
white includes the body, black excludes the room, gray softens the outline. Also
paint a clean room background with the person removed. Automatic color-keying
cannot separate black clothing reliably or reconstruct the hidden background.
Then export single-image complete poses:

```sh
python3 tools/f15assets/briefing_person.py WALL.png ARMPIECE.png body-mask.png room-only.png person-export
```

The exporter keeps existing arm alpha, combines it with the masked body, and
produces the eight runtime PNGs at a common resolution without downsampling.
These are starting points for redrawing; the old arm/body seam may still need
painting over. Existing combined scene previews below can be used as references.

### Legacy split-atlas workflow

```sh
python3 tools/f15assets/arm_composite.py export converted_assets_all/SVN/WALL.png converted_assets_all/SVN/ARMPIECE.png sprite-work/body-arm
```

Open an `arm_XX.ora` in Krita or GIMP: it is one image containing the body and
the arm at the actual menu position. The matching `arm_XX-combined.png` is a
flattened reference for viewing or drawing tools. Use the ORA for rebuilding:
flattening loses the hidden body/background needed for arm animation.

Keep the two layers named `Arm` and `Body and room`, their normal blend mode,
and the canvas size. Redraw the joint while viewing both layers. The arm must
stay within its original sprite rectangle. Export uses a common integer scale
at least as large as both input resolutions; no art is downsampled.

```sh
python3 tools/f15assets/arm_composite.py rebuild sprite-work/body-arm sprite-work/body-arm/arm_01.ora rebuilt-body-arm --pose 1
```

This writes `WALL.png` and `ARMPIECE.png` without changing the current game assets.
The wall is shared by every pose; check its connection against all arms before
installing it. A rebuild edits one pose only. To edit another cumulatively,
export again using the newly rebuilt pair. Poses 1-7 are the menu sequence;
pose 0 is also included from the game's table. Medals and other atlas pixels
are retained. Explicit alpha is required for transparent custom arms; opaque
black is not silently removed.

## Edit individual atlas crops

Requires Python 3.9+ and Pillow. Run from the repository root:

```sh
python3 tools/f15assets/sprite_atlas.py extract converted_assets_all/SVN/F15.png sprite-work/F15
python3 tools/f15assets/sprite_atlas.py extract converted_assets_all/SVN/ARMPIECE.png sprite-work/ARMPIECE
python3 tools/f15assets/sprite_atlas.py extract converted_assets_all/SVN/DBICONS.png sprite-work/DBICONS
```

Each directory contains named sprite PNGs, `atlas.json` with exact rectangles,
`layout-preview.png` with crop outlines, and two reference files. Do not edit
`base.png`, `original.png`, or the manifest. Unlisted atlas regions are preserved.
F15 includes known radar, reticle, explosion and strip crops; this is not a
complete semantic description of every pixel in the sheet.

Redraw individual sprite files in an image editor. Keep their canvas dimensions,
object position, and orientation. Do not trim transparent margins, rotate to a
different heading, or let an image generator repack the sheet. For generation,
use the extracted sprite as a composition reference and place the result on its
exact original canvas in an editor. The tool cannot infer a correct aircraft
heading or arm joint from newly drawn artwork.

Existing PNG transparency is preserved. White is never a transparency key.
Optional `--black-transparent` removes exact black everywhere, including inside
objects: use it only for sheets deliberately drawn with black as the key color.
Do not use it on opaque DBICONS panels or arm artwork with black clothing.
An RGBA PNG is preferable for new transparent artwork. This tool does not change
the game's runtime transparency handling.

```sh
python3 tools/f15assets/sprite_atlas.py rebuild sprite-work/F15 F15.rebuilt.png
python3 tools/f15assets/sprite_atlas.py rebuild sprite-work/ARMPIECE ARMPIECE.rebuilt.png
python3 tools/f15assets/sprite_atlas.py rebuild sprite-work/DBICONS DBICONS.rebuilt.png
```

Inspect the result before replacing the corresponding runtime PNG. Existing
outputs are never overwritten. With no edits and no transparency conversion,
rebuilding copies the original PNG byte-for-byte, including its palette.
After edits, output is RGBA at the original atlas resolution; there is no
downsampling or palette quantization.

Some F15 crops overlap. Only changed pixels are applied, so an unchanged crop
cannot undo an edit in another one. Conflicting edits to the same atlas pixel
stop rebuilding and identify both files. Undo one conflicting edit to resolve it.
Transparent edits replace pixels rather than blending over the old sprite.

Source contracts: ARMPIECE rectangles come from `src/stdata.c`; DBICONS uses
`popupSpriteX/Y` in `src/endata.c` and the 48x40 popup size in `src/endtypes.h`.
F15 crops follow sprite draws in `src/egui.c`, `src/eghudr.c`, and `src/egtarget.c`.
Rectangle coordinates use the game's 320x200 logical sheet and map shared edges
to the same actual pixel boundary even for noninteger-resolution replacements.
