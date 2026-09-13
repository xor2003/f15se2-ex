#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPLACEMENT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
GAME_DIR=${1:-${F15_GAME_DIR:-$SCRIPT_DIR}}
SORTIE=${2:-${F15_CAMPAIGN_SORTIE:-}}
REPO_ROOT=$(CDPATH= cd -- "$REPLACEMENT_ROOT/.." 2>/dev/null && pwd || printf "%s\n" "$PWD")
EXE=${F15_EXE:-}

if [ -z "$EXE" ]; then
    for candidate in \
        "$REPO_ROOT/build/f15se2-ex" \
        "$REPO_ROOT/f15se2-ex" \
        "$PWD/f15se2-ex" \
        "f15se2-ex"
    do
        if command -v "$candidate" >/dev/null 2>&1; then
            EXE=$candidate
            break
        fi
    done
fi

if [ -z "$EXE" ]; then
    echo "could not find f15se2-ex executable" >&2
    echo "set F15_EXE=/path/to/f15se2-ex" >&2
    exit 2
fi

if [ -z "${F15_ASSET_TOOL:-}" ] && [ -f "$REPO_ROOT/tools/f15assets/cli.py" ]; then
    export F15_ASSET_TOOL="python3 $REPO_ROOT/tools/f15assets/cli.py"
fi

set -- "$EXE" --game "$GAME_DIR" --campaign SVN
if [ -n "$SORTIE" ]; then
    set -- "$@" --campaign-sortie "$SORTIE"
fi
F15_REPLACEMENT_ROOT="$REPLACEMENT_ROOT" F15_REPLACEMENT_ROOT_ONLY=1 exec "$@"
