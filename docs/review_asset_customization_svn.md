# Asset customization versus SVN multiplatform review

Reviewed 2026-09-19: asset branch `23ce13f` against
`release/svn-multiplatform` at `eddca41`. The asset branch is now named locally
`feature/asset-customization-svn`; its remote name remains unchanged.

## Main finding: the asset branch is not the latest combined version

Git reports three commits on the asset side and 107 on the release side since
their common ancestor. However, all three asset commits are already present in
release as patch-equivalent cherry-picks:

| Asset commit | Release equivalent | Change |
| --- | --- | --- |
| 6c0d54b | cff60fd | Campaign friendliness by flight/base slots |
| 9bb00a4 | d16a6c1 | Terrain classification bounds checks |
| 23ce13f | 257c329 | SVN replacement assets and attribution |

`git cherry -v release/svn-multiplatform 23ce13f` reports `-` for all three.
A merge-base diff alone exaggerates the new work because Git does not treat
cherry-picked commits as shared ancestry. There is no unintegrated patch among
these three commits. Do not replace the release tree with the older asset tree.

Concrete differences in that replacement would include:

* Dropping controls mapping, joystick configuration, gameplay options, browser
  and Android support, and their tests/build workflows.
* Losing initialization of missile fineX/fineY at ground launch in egthreat.c,
  allowing the next update to use a stale projectile position.
* Replacing the binary import pipe adapter in shared/file_io.c with text-mode
  popen, losing the release's portable binary-stream handling.
* Losing later SVN recovery-site and input fixes recorded on release.

`integration/latest` therefore starts from `eddca41`. It does not merge the
old asset branch back over the release or discard later fixes.

## Remaining review concerns shared by both branches

These are not new regressions relative to the release branch:

* campaignSlotList in shared/file_io.c locates a key with string::find instead
  of parsing the root JSON object. A nested same-named field can shadow the
  intended root field; invalid/overflowing lists often become an empty list.
  Add structured parsing and malformed/nested manifest tests before treating
  arbitrary third-party manifests as supported.
* Base-spawned aircraft allegiance is derived from mutable SimObject.objType.
  updateObjects changes that field when selecting targets and recovery bases.
  Verify the intended ownership rule: a plane should not accidentally change
  allegiance merely because its navigation destination changes. A stable spawn
  faction/base identity would be needed for that rule.

The committed changes were reviewed through exact diffs and surrounding source.
The code graph was stale/partially indexed, so it was not used to claim complete
coverage. Binary models, visual asset quality and platform-specific runtime
behavior were not exhaustively validated in this review.

The six uncommitted cockpit/converter files and untracked work in the original
asset checkout were preserved and are outside this committed-branch review.
They are not included in integration/latest. Dirty DOS, replay and WebAssembly
worktrees are also preserved separately.
