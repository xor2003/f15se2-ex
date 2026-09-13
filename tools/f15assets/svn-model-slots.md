# SVN flight model slots

Run `python tools/f15assets/repair_svn_models.py` from the repository root.
It rebuilds campaign-local GLBs and their runtime caches. The old
`fix_svn_player_aircraft.py` entry point delegates to the same implementation.

The runtime uses X right, Y forward, Z up. The original starter aircraft were
authored with Z forward and Y up. Convert axes before fitting length; do not
apply Blender object rotation a second time to already transformed geometry.
The cache compiler corrects triangle winding for reflected node transforms.

Aircraft are uniformly scaled to the original longitudinal extent and aligned
to its bounding-box center. Width and height retain the source proportions.
Player slots 6 and 7 are kept intact so their body and landing gear stay aligned.
Slot 0 uses the saved upright player body, fitted to its own reference slot.

The 15FLT container is not an aircraft-only catalogue:

| Slots | Runtime role |
| --- | --- |
| 1, 13, 19 | Missile visuals |
| 15 | Bomb |
| 3, 17 | Smoke particles |
| 14 | Falling equipment/parachute |
| 5, 21 | Flat aircraft silhouettes |

These slots receive generated geometry in the corresponding reference bounds,
not aircraft or radar placeholders. Historical filenames are retained to keep
manifest paths valid; the numeric slot prefix determines runtime selection.
New projectile, particle, parachute and silhouette geometry is CC0.

The repair tool needs reference aircraft GLBs in `converted_assets_all/15FLT`
when generating assets. Those reference files are not needed by the game and
must not be copied into the free release. Generated SVN GLBs and caches are
self-contained. Existing imported aircraft retain their source licensing.

Ground models belong to the VN container, not 15FLT. Keep their reference
footprint fit and runway anchor. A carrier or depot that fits the reference
width may be shorter than the original: stretching its length independently
would distort its proportions.
