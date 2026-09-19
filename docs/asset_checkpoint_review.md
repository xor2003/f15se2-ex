# Asset and cockpit checkpoint review

Reviewed 2026-09-19 against the previously committed asset-customization-svn tip.
This preserves existing local cockpit rendering, generator, starter-art and
backlog edits. It is not merged into integration/latest.

## Changes

* Cabin availability allows world rendering behind forward, side and rear views.
  Side/rear cockpit composition restores the full scene and uses an off-center
  projection based on the existing virtual-screen mapping.
* Cockpit generation adds solid interior geometry, optional console textures and
  geometry/binding/determinism tests.
* Starter-art generation moves blur and noise work to smaller images. This changes
  texture frequency/appearance as well as performance; comments claiming identical
  appearance are not a pixel-equivalence guarantee.
* The adjusted gameplay backlog remains a design document, not implemented work.

## Validation and limits

The native build completed after regenerating its stale CMake build files.
All five CockpitGeneratorTest cases passed. The full Python discovery run executed
49 tests: three coastline failures came from missing Shapely, and two subtest
errors in the ground-grid test came from a fixture missing terrain_grid. No smoke
converter or cockpit-generator test failed in that run.

CTest passed 31 of 34 tests. The failures were:

* file_io_behavior_tests: first-sortie campaign mission target slots assertion.
* asset_runtime_loader_validation_tests: VN shape 95 recovery carrier lacks the
  source metadata/color expected by the comparison.
* asset_replacement_full_validation: SVN inventory hash/size mismatch for
  run_campaign.sh in the local replacement pack.

The failed code paths/assets were not edited by this checkpoint. Their status
is reported rather than silently changing test expectations or regenerating
custom user assets. A GL visual playthrough, including different aspect ratios
and side/rear views, has not been performed for this checkpoint. Native build
success and generator tests do not establish rendering correctness.
