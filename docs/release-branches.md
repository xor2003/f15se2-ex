# Fork branches and releases

`release/svn-multiplatform` is the consolidated release branch. Android phone,
Android TV, Windows, and browser packages should come from the same tagged
revision after their platform checks pass.

Keep development on feature branches and merge tested changes into this branch.
Send independent upstream fixes from branches based on upstream `main`.

| Branch | Purpose |
| --- | --- |
| `asset-customization-svn` | SVN campaign and replacement asset runtime |
| `android-tv-integration` | Existing Android phone and TV integration |
| `wasm-upstream` | Browser platform support, upstream PR #50 |
| `joystick-configuration` | Controls mapping, upstream PR #47 |
| `mouse-menu-selection` | Mouse menu selection, upstream PR #48 |
| `gameplay-options-menu` | Gameplay options, upstream PR #39 |
| `perf-render-hot-paths` | Renderer optimizations, upstream PR #53 |
| `release-dead-sections` | Release linker settings, upstream PR #52 |
| `ci-quality-checks` | Automated checks, upstream PR #54 |
| `nightly-desktop-packages` | Desktop packaging, upstream PR #55 |
| `fix-pic-decoder-bounds` | PIC decoder bounds, upstream PR #56 |

The older `asset-runtime-*` branches preserve the earlier split proposals.
They are not release branches. Historical preview tags preserve old binaries;
do not move them to new commits.

## Release acceptance

The intended default data set is SVN. Users must also be able to select their
original game data and play the original campaigns. Test the original data path
before publishing a consolidated release.

SVN data is maintained separately from engine source. Package the runtime files
and their attribution together; exclude pilot saves, development backups, and
original proprietary game data. The current campaign metadata still includes
assets marked for license review, so release provenance must be completed before
labeling the entire pack freely redistributable.

Platform merges, default data selection, and packaged launch tests are release
work in progress. A branch existing does not mean all four packages are ready.
