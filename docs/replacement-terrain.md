# Replacement terrain

Terrain collision is opt-in through glTF extras. Set `f15_surface` to `land`
on a mesh, material, or triangle primitive. Primitive values override material
values, which override mesh values. In Blender, add this custom property to the
mesh data and enable **Custom Properties** when exporting glTF.

The GLB converter preserves the surface tag in the GLMESH cache. Land collision
uses the transformed visual triangles; it does not depend on campaign names,
model slots, camera direction, or rendering detail. Unmarked models retain their
existing behavior. Regenerate caches after adding surface metadata.

Use heightfields, with runtime X/Y horizontal and positive Z upward. The complete
heightfield must fit within its containing 4096-unit terrain tile after placement.
It must be a static tile object, not a moving or destructible object. These checks
are not a general closed-mesh collider: caves and overhangs need another collision
representation.

`water` and `runway` tags are also preserved, but their gameplay integration is
not implemented yet. They do not create coastlines or change landing corridors.
The existing difficulty and crash rules still determine the result of a collision.
