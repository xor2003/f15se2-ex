# F-19 port — handoff brief

## Mission

Port the F-19 Stealth Fighter runtime into this repo (`/home/xor/games/f15se2-ex`,
the F-15 SE2 codebase). Preserve the original DOS executable's data-segment
bindings, rendering behavior, resource/file handling, and runtime semantics.
Byte-exact asm skeletons + incremental C ports, verified against the original
binary.

- Reconstruction source (analysis, lst/maps, decompiles): `/home/xor/games/f19ru/F19`
- Original F-19 runtime + assets (authoritative EN install): `/home/xor/games/f19/F19`
- This project (build + run): `/home/xor/games/f15se2-ex`
- Executable: `build/f15se2-ex`  (CMake target is **`f15se2`**, output name `f15se2-ex`)
- `build/` is a **symlink** → `/var/tmp/f15-builds/main-build`. Build from the real
  dir: `cmake --build /var/tmp/f15-builds/main-build -j4` or `cd build && make f15se2 -j4`.
  `make f15se2-ex` is a silent no-op — do **not** use it.

## RUN THE GAME / reproduce the bugs

```sh
cd /home/xor/games/f19/F19          # MUST run from here — assets load by relative path
DISPLAY=:0 /home/xor/games/f15se2-ex/build/f15se2-ex
```

Env test-hooks (all optional, read live each frame — see `egframe.c`/`stmain.c`):

```sh
F19_QS=1            # quick-start: skip title/menus straight into a generated mission
F19_POSX=0x8000     # world X  (view-space)
F19_POSY=0x8000     # world Y
F19_ALT=0x2000      # altitude
F19_PITCH=-0x0c00   # pitch (negative = nose down)
F19_HEAD=0x0        # heading
F19_KNOTS=          # airspeed
F19_DAY=1|0         # force day/night
F19_VM=0x84         # view mode (0=cockpit; 0x84/85/87/88/89/8B = external/chase)
F19_DUMP=1          # dump page surface -> /tmp/f19frame.bmp + /tmp/f19seq_*.bmp
```

A good "over the sea" checkpoint used during this work:
`F19_QS=1 F19_POSX=0x8000 F19_POSY=0x8000 F19_ALT=0x2000 F19_PITCH=-0x0c00`

## CURRENT BUGS (fix these — user-reported)

**CONFIRMED root cause for #1 and #2 — the RNG seed change in `f19stubs.c`:**

- `sub_150BA`: `return 0` → `return (int16)getTimeOfDay()`  (int 1Ah tick)
- `sub_140A3`:  `noop`     → `f19_seedRng()` = `f19_srand(sub_150BA())`

**Proof (A/B this session):**
- Seed = real tick (`getTimeOfDay`): plain launch started in an **external
  view** with the plane rendered **cyan/green** wireframe over the runway —
  nose-down / wrong state. Reproduce it and the start varies run-to-run.
- Seed pinned to 0 (`sub_150BA → return 0`): plain launch starts **level on
  the runway in cockpit view** — the normal takeoff state.

So the time-varying seed drives BOTH the bad start attitude and the shifting
plane color (pink → black → green across runs — classic non-deterministic
rand-driven color pick).

**What still needs deciding (the real fix):** the original ALSO reads int 1Ah
here, yet its plane stays black and its start is stable — so the faithful fix
is probably NOT "leave the seed pinned at 0" but rather to find why a
time-seeded stream diverges in the port:
(a) `sub_150BA` return width — int 1Ah AH=0 returns CX:DX; the code uses the
    DX low word. `getTimeOfDay()` may return a different scale/width than the
    original DX, so `f19_srand` gets a mismatched seed domain.
(b) the port's `f19_rand`/`f19_srand` LCG may differ from the original's, so
    the same seed maps to a different stream → different color/attitude.
(c) or the green is a separate `colorLut` issue (below).
Check `f19_rand`/`f19_srand` (egmath.c/stutil.c) against the original LCG, and
compare `sub_150BA`'s return to the original's int 1Ah DX word.

1. **Plane starts nose-down** — caused by the time-seed (see proof). Verify
   the real fix keeps a level start.
2. **Player plane GREEN instead of BLACK** — rand-driven color pick under the
   varying seed. If it persists with a stable seed, it's the `colorLut` rebind:
   check the airframe face index and where `colorLut` sends it.

3. **Sea must be BLUE** (the original requirement). Currently renders `0xb1`
   (56,56,170 = faded blue) over the water — that part works. Confirm it stays
   blue and the near sea reaches `0x81` (0,0,170 = pure). If the user still
   says not-blue, check whether what they see is sky-vs-sea or a coverage gap.

## Current working-tree state (test artifacts to reconcile)

- `src/f19/f19stubs.c`: `sub_150BA` is currently `return 0` — the pinned seed
  that gives a stable start. Decide the faithful fix (above) before committing.
- `src/f19/f19stubs.c`: `sub_140A3` calls `f19_seedRng()` (was noop).
- `src/f19/egframe.c`: added a `F19_VM` env hook that writes `g_viewMode`
  (external-view test hook; `0x84`/`0x85`/`0x87`/`0x88`/`0x89`/`0x8B` are the
  chase/external modes — `0x84` looked top-down, not a clean plane shot).
  Keep or remove; it's env-gated and harmless.

## What was fixed (the sea) — DO NOT regress

Root cause was a **split global**: F-19 code writes some globals into its
emulated dseg (via `f19egglobals.h` macros) while the *shared* renderer reads
separate `egdata.c` globals that stayed at defaults.

- `size3d3_3/4/5/6` (shared-vertex counts): the F-19 loader stored them into
  `m_g_modelOffsetTable` dseg cells; `transformModelVerticesFar`, `r3d_gl`
  `fillPools`, and `r3dmesh` read the `egdata.c` externs → stayed `0` → shared
  vertex models collapsed to `(0,0,0)` → the flat `±2048` terrain-surface quads
  (the sea) never projected → uncovered background (cyan/dark).
  **Fix:** `f19_setSharedVertCounts()` mirrors the counts into the shared
  globals after `load3D3()` reads them → `size3d3_3 = 144`.
  (`src/egdata.c`, `src/egdata.h`, `src/f19/eg3dload.c`)
- `colorLut`: was a fixed F-15 array; the shared rasterizer read it while F-19
  wrote `f19_egSpace.m_colorLut`. Now `colorLut` is a `uint8*`; default points
  at `s_f15ColorLut`, and `f19_egDsegLoad()` calls
  `egdata_setColorLut(f19_egSpace.m_colorLut)` so the renderer uses F-19's live
  table (which also carries the aliased LOD/clip control words at +0x10..0x3F).
  (`src/egdata.c`, `src/egdata.h`, `src/f19/f19egdata.c`)

**The split-global pattern is the thing to hunt for the green-plane bug:**
any global that F-19 writes via a `f19egglobals.h` dseg macro but the shared
`egdata.c`/`eg3drast.c`/`r3d_*.c` code reads as a normal extern is a candidate
mismatch. Check `git grep` for `#define X (*(…f19_egSpace…)` and cross-check
each against shared-reader externs. `buf3d3`, `g_modelVertX/Y/Z`,
`buf3d3_1/2/3` are already confirmed correctly shared.

## Other changes in this diff (keep)

- `gfx_dacRefreshUi()` + `s_dacLow16` snapshot in `gfx_impl.c`: re-apply the
  last DAC[0..15] write every 60 Hz tick, mirroring the original driver's ISR
  (fixed stale low-palette / washed UI colors). Called from
  `f19eg_advanceFrameTick` in `f19egsvc.c`.
- `f19stubs.c`: `sub_140A3` noop → `f19_seedRng()`; `sub_150BA` currently
  `return 0` (was `getTimeOfDay()`, pinned — see the confirmed seed bug).
  Affects RNG seeding / start state — touch with care.
- `stmain.c`: `CommData.detailLevel` at comm+0x32 seeded to `2` (max detail);
  `F19_QS` seeds `commData->f70` quick-start.
- `gfx_impl.c` `gfx_dumpPage()` + `F19_DUMP`: frame capture for headless tests.
- `egframe.c`: `F19_POS*`/`F19_ALT`/`F19_PITCH`/`F19_HEAD`/`F19_DAY` env hooks.

## Verification status

- `cppcheck` clean on all changed files; `clang-check` clean (one pre-existing
  `-Wwrite-strings` style warning, unrelated).
- **ASAN cannot run here**: `ulimit -v` hard-capped at 4 GB; AddressSanitizer
  needs a ~15 TB shadow reservation. Env limitation, not a code bug. Bounds are
  safe: `nRefs=144<255`, `nX=31`, `nY=32`, `nZ=8` all within vertex-table caps.
- Sea A/B verified: bridge OFF → out-window dominant `0xb0` (36,36,36 dark, no
  sea); ON → `0xb1` (56,56,170 blue). Near quads also produce `0x81` pure blue.
- Original-binary comparison via DOSBox is **not feasible**: `EGAME.EXE` does
  not start when launched directly (needs the proper F19 shell/loader).

## Palette reference (BMP palette is BGR)

- Sea-blue ramp: `0x81`(0,0,170)→`0x91`→`0xa1`→`0xb1`(56,56,170)→`0xc1`→`0xd1`
  →`0xe1`→`0xf1`(130,130,170). Odd low-nibble `1` = sea shades.
- `0xb0` = (36,36,36) grey — **not** a sea shade (low nibble 0); that's a
  different surface (sky/land), not broken sea.
- `0x3`,`0x83` = cyan (0,170,170) — the cyan the user complained about.
- Airframe should resolve to a near-black index; green/cyan = remap shifted.

## How to verify a fix

1. Build: `cd build && make f15se2 -j4` (real target; check `strings
   build/f15se2-ex | grep <marker>` + fresh timestamp — stale-object gotcha).
2. Run cockpit view over sea (checkpoint above) with `F19_DUMP=1`.
3. Analyze `/tmp/f19frame.bmp` pixels (BGR palette): classify out-window
   region, confirm sea=blue ramp, plane=near-black, no cyan/green.
4. `cppcheck`/`clang-check` the touched files.
5. Remove any temp `fprintf`/probe code before finalizing (all were removed;
   `git grep -n "fprintf\|getenv" src/f19 src/eg3drast.c` should show only the
   intentional env-hooks above).

## Russian localization (F19_LANG=ru)

- `F19_LANG=ru` enables the Boriss Software (1990) localization; unset/`en`
  keeps original English. Source assets: `/home/xor/games/f19ru/F19`.
- `src/fontdata_ru.h` — authentic Boriss Cyrillic font planes (fonts 0,1,3,4,5)
  extracted from RU `MGRAPHIC.EXE` (regenerate via the strided-row layout:
  256-stride planes; seg000:0 = file 0x200).
- `src/f19/f19_strings_ru.h` — 698-entry EN->RU table (sorted longest-key-first).
  Regenerate with `python3 tools/f19_rustrings.py`; EGAME pairs are an explicit
  positional map (Boriss repacking broke offset alignment), START/END pairs come
  from anchor alignment + curated OVERRIDES. Do not hand-edit the header.
- `src/f19/f19lang.c` — `f19_translate` substring-scan translator (longest
  match, ASCII-case-insensitive, mirrors all-uppercase EN to uppercase
  Cyrillic), `f19_textChars` codepoint count, `f19_langInit` installs the hook.
- `gfx_impl.c`: `g_stringTranslator` applied in `drawStringCore` +
  `gfx_getStringAdvanceUtf8`; `ruCyrillicSlot` maps UTF-8 Cyrillic to the
  Boriss KOI-7 glyph slots (А-Я at A B W G D E V Z I J K L M N O P R S T U
  F H C ^ [ ] _ Y X \ @ Q, lowercase +0x20). Invalid high bytes remain DOS
  color escapes (zero-advance, set color).
- `compat64/dos.h` `strupr` folds UTF-8 Cyrillic length-stably.
- Wrap paths (`stutil.c`, `endrawstr.c`) translate BEFORE wrapping and measure
  via `gfx_nextCodepointAdvance`; `f19_stringWidth`/`stringWidth` measure the
  translated text. `.WLD` site names stay English (identical data in Boriss).

## F-19 GL backend (F15_RENDER=opengl1)

F-19 3D now routes through the shared GL backend (`f19r3d.c` → `r3d_submit` →
`gl_submit`). Three fixes this session, all verified headless on Xvfb :99
(`F15_AUDIO=off`, window shots via `import -window root`, page dumps via
`F19_DUMP`):

- **Face/line visibility mask (nozzles fix)** — the GL draw used to render
  every decoded face double-sided; a z-buffer alone keeps faces the original
  *culls outright*, so the tail-cone covered the F-19's recessed engine
  nozzles (the `0x0d` fire-cycle faces that blink yellow↔red). Fix:
  `r3d_objTransformFar` (eg3drast.c) now also computes the
  `rotatePoint3d` `g_vtxSignMask` equivalent — same math: negated rel pos
  through the (transposed) `g_objOrientMatrix`, `dot = fn·dir` vs per-normal
  threshold toggling mask bits, on the FINEST LOD's normal table (the one
  `r3dmesh_decode` descends to). Plumbed via `GlSub.visMask`; `drawSub` gates
  faces on `1u << f->cullNormal` and wire lines on `line->visMask & mask` —
  matching `renderPrimitiveCommand`/`testVisibilityMask` exactly.
- **Retained-page dual-write (RUD/labels fix)** — under a GL vector frame
  (`r2d_vectorActive`), lines/sprites/glyph pixels went vector-only → wiped
  by the next `glClear`. One-shot draws (`drawAirspeedTape` rung lines on
  thrust change, `drawPanelModeText` "TRACKCAM AHEAD/REAR/..." labels) then
  vanished. Fix: `r2d_submitLine`/`r2d_submitImage` now ALSO call the
  software callback to bake the retained page (composited each frame →
  persists like DOS VRAM), and `drawStringCore` glyph paths write
  `dstRow[px]` before submitting the vector point. Per-frame fills keep
  using `fillSpanRectImmediate` (vector-only by design — never dirty the
  page). `r2d_submitPoint` stays vector-only; its text callers now write the
  page explicitly.
- **`insertOutlineEdges` crash + silhouette** — the transliterated
  `((const uint8 *)*p++)[0x9E8]` read host address `attr+0x9E8` (low memory
  → segfault under the `?`/`>`/`<`/`M` look-around views 0x41–0x44). DOS read
  `DS:[attr+0x9E8]` = `colorLut[attr]`; fixed to that. Under GL the
  silhouette span-fill lands inside the whole-screen show-through rect, so
  it also needed the F-15 `drawVectorShape` pattern: collect the vertex ring
  and `r2d_submitPoly` it.
- **TAC-map zoom black background** — `renderPrimitiveCommand`'s GL branch
  submitted map terrain faces as vector polys then returned early, never
  running the span fill → the retained page stayed black and the next
  composite wiped the one-frame poly. Now it submits the poly (native-res,
  this frame) AND falls through to the span fill (page bake, persists like
  DOS VRAM). The ring walk uses its own cursor `rp`; `p` still feeds the
  span path exactly once.
- **F4 DATABASE model vanishing** — `drawTargetView` mode≥2 calls
  (DATABASE panel, recon photo) are one-shot: under GL the subscene drew to
  the framebuffer for a single frame. Under GL, mode≥2 now runs the
  original SW sequence — `setup3DTransform`/`projectSceneObject`/
  `rasterize3DWorld` — baking the model into the retained page (the MFD
  rect is opaque page, so the composite keeps it). Modes 0/1 (live
  TrackCam, re-submitted every frame) keep the GL subscene.

`insertOutlineEdges` walk semantics: `attr, v0, v1, ..., v0, -1` per group —
each edge is prev→current, the ring repeats v0 to close (drop the dup for
GL_POLYGON).

## Notes / gotchas

- Under `F15_RENDER=software` the in-game 3D path is the **software span
  rasterizer** (`processSceneObject`→`transformVertexList`→
  `renderPrimitiveList` in `eg3drast.c`); under `opengl1` it goes through
  `r3d_gl.c`. Either way the same `colorLut`/`g_vtxSignMask` semantics apply.
  MFD/map `face cb=` logs come from `eg3dmap.c` `drawModelDisplayList`, a
  different path — don't confuse them.
- Widescreen 3D (`F15_WIDESCREEN`, default on) extends the world into the
  pillarbox side-bars — the red columns at the window edges in flight are
  terrain seen through the gaps, intended.
- `transformVertexList` uses `transformModelVerticesFar` (precomputed shared
  verts) when `g_offscreenRender==0` (detail≥1), else on-the-fly pool reads.
- Keep changes minimal and idiomatic; preserve dseg bindings; don't hardcode
  colors/fills — fix the data binding, not the symptom.
