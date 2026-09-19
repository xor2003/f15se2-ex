# Local branch layout

Reorganized on 2026-09-19 in f15se2-ex only. No history was rewritten.

| Branch namespace | Purpose |
| --- | --- |
| main | Clean upstream baseline, tracking origin/main |
| release/svn-multiplatform | Existing integrated desktop/Android/browser release |
| integration/latest | Release baseline plus reviewed new integration work |
| feature/* | Independent features and existing PR work |
| fix/* | Focused fixes and existing PR work |
| diagnostic/* | Reproduction and investigation work |
| archive/20260919/* tags | Preserved tips of superseded local branches |

The asset-customization-svn branch became feature/asset-customization-svn.
Its three committed patches are already present in the release branch as
cherry-picks; see review_asset_customization_svn.md. Uncommitted work and unrelated
feature branches are not automatically part of integration/latest.

Thirty local branches were renamed and 25 were archived as tags, leaving 33
local branches including the new integration branch and clean upstream main.
Existing remote names and PR heads were left unchanged. Renamed branches retain
their original upstream tracking configuration; use explicit push destinations
when updating an existing PR. Do not force-push to reconcile a local rename.

Original tips, worktree paths and every rename/archive action are recorded locally
in the common Git directory under maintenance/branches-before-20260919.json and
maintenance/branches-after-20260919.json. These audit files are machine-local.
Archive tags preserve commits, including old test branches, rather than relying
on reflog retention. For example, recover the former tests branch with:

```sh
git branch recovered-tests archive/20260919/tests
```

Ten clean obsolete worktrees and verified untracked CMake build directories were
removed. Dirty source worktrees, custom cockpit/model assets, recordings, profiles
and release bundles were retained. The f15se2-re repository was not modified.
The main f15se2-ex/build dependency source cache remains in use by the integration
build; do not remove it without reconfiguring that build.

Archive tags were also pushed to the my remote after the user requested backups
of the latest work. Existing dirty source was reviewed and saved in separate
checkpoint commits on feature/wasm-port, fix/dos-runtime, diagnostic/pr26-replay
and the asset feature branch. Generated outputs and local authoring/reference
assets remain on disk and are excluded via the common Git info/exclude file,
not committed or silently discarded. The f15se2-re dirty state is unchanged.

Keep feature integration separate
from branch housekeeping: retained branches may be alternate implementations or
PR splits, and must be reviewed before merging into integration/latest.
