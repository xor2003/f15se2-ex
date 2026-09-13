# Custom 3D cockpit

Place `cockpit.glb` in the selected campaign directory, for example
`converted_assets_all/SVN/cockpit.glb`. The OpenGL renderer loads it automatically
when entering cockpit view. `F15_3D_COCKPIT=0` disables it. Missing or unsupported
models retain the flat cockpit. Software rendering is unchanged.

Create the initial dashboard from the existing artwork:

```sh
python3 tools/f15assets/create_cockpit.py \
  converted_assets_all/SVN/256PIT.png converted_assets_all/SVN/cockpit.glb
cmake --build build --target f15se2 -j4
```

Import the GLB into Blender, edit geometry and the embedded texture, and export as
GLB with UVs and materials. Use static triangles and embedded PNG textures; skins,
animations, required extensions, texture transforms and external image files are
not supported. Materials are unlit to preserve the painted cockpit's appearance.

Keep these material names to receive live game instruments. Each surface uses
UV coordinates from `(0,0)` at top-left to `(1,1)` at bottom-right:

| Material | Live content |
| --- | --- |
| `display_map` | Left map |
| `display_radar` | Middle radar |
| `display_target` | Right target camera |
| `indicator_R`, `indicator_I` | Threat warning lights |
| `indicator_B`, `indicator_L` | Air brake and landing gear lights |
| `weapons` | Weapon and ammunition strip |
| `throttle`, `fuel` | Throttle and fuel gauges |

These names belong to materials, not objects. Move or reshape their surfaces to
fit another cockpit. Other material names use their GLB base colour and texture.
No JSON sidecar is required.

The current renderer has a fixed camera at the origin, looking along negative Z,
with positive Y up and a 60-degree vertical field of view. The generator includes
an equivalent `pilot` camera for Blender preview; moving that camera does not yet
change the game camera. Move the model relative to the origin instead.

The generator also reads sibling `256LEFT.png`, `256RIGHT.png`, and `256REAR.png`.
It builds side consoles, a floor, seat, rear bulkhead, controls and canopy framing.
Rear artwork shows an exterior pilot view, so only metal/fabric sections are used
on the rear structure; it is not treated as a photograph of the inside rear wall.
Cabin dimensions and hidden surfaces are an approximation, not an exact aircraft
reconstruction from these pictures.

Space selects the front cockpit, F2 looks left, F3 right, and F4 behind. All four
views render the same GLB with a rotated camera. F1 remains the unobstructed forward
view. Live feeds are captured at window resolution from the existing instrument pass.
Mouse/touch hit regions still use the original layout, so moving controls in the
model does not move their input regions. A different aircraft can replace the mesh
and art, but new instrument behavior and continuous head tracking are not implemented.

At startup, look for `cockpit3d: loaded ...; 3D cockpit enabled`. This indicates a
loaded model; rendering occurs at the end of each cockpit frame, before swapping
the window buffers.
