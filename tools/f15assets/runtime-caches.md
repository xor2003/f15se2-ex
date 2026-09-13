# Packaging structured assets

Compile the campaign after finishing edits to its world, grid, tile, and model
JSON files:

```sh
python tools/f15assets/compile_runtime_assets.py converted_assets_all/SVN
```

Ship each JSON file together with its adjacent `.json.runtime` file. The game
uses these caches without invoking Python. GLB models also need their existing
`.glmesh` caches for converter-free loading.

Editing JSON, including whitespace, invalidates its compiled cache. Regenerate
caches before packaging. The loader falls back to the Python converter when a
cache is absent, stale, or malformed; do not rely on that fallback in a release.

## Structured cache format

All integer fields are unsigned, little-endian:

| Field | Size |
| --- | ---: |
| Magic `F15BIN1` followed by a zero byte | 8 bytes |
| Source JSON byte count | 4 bytes |
| Compiled payload byte count | 4 bytes |
| Exact source JSON bytes | Source count |
| Compiled legacy-format bytes | Payload count |

Both counts must be nonzero and at most 1 MiB. The loader compares the stored
source byte-for-byte with the current JSON and rejects truncated files or extra
trailing bytes. This detects stale caches; it is not an authenticity check.
Only distribute caches built from trusted campaign sources.

The compiler replaces each cache atomically. A later conversion failure may
leave earlier files updated, so a successful complete run is required before
packaging. JSON remains the editable source of truth.
