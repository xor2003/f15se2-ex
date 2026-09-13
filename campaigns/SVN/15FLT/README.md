# SVN campaign-local aircraft GLB overrides

Each `shape_###_*.glb` keeps the 15FLT shape slot prefix used by runtime lookup, but the file contents are campaign-native procedural placeholders rather than converted original geometry.

The placeholders intentionally use simple colored surfaces plus line primitives for antennas/masts so Blender users can inspect and replace them easily. Replace any one file with a Blender-exported GLB as long as the `shape_###` prefix remains stable.

These files are campaign-local: they should affect `--campaign SVN` only, before falling back to shared `converted_assets_all/15FLT` or the original `15FLT.3D3`.

License note: these generated placeholders still need review before a free/CC asset distribution.
