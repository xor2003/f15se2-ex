#!/usr/bin/env python3
"""Stage SVN with source-matched runtime caches; no Python needed by players."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def stage(destination):
    source = ROOT / "campaigns" / "SVN"
    destination = Path(destination).resolve()
    if destination == source or source in destination.parents:
        raise ValueError("Stage outside the source campaign directory")
    shutil.copytree(source, destination, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("person-sources", "sources", ".comments",
                                                 "HallFame", "*.runtime", "cache"))
    tool = ROOT / "tools" / "f15assets" / "cli.py"
    for path in sorted(destination.rglob("*.json")):
        fmt = path.stem.rsplit(".", 1)[-1].upper()
        if fmt not in {"WLD", "3D3", "3DT", "3DG"}:
            continue
        payload = subprocess.check_output([sys.executable, str(tool), "build-binary",
                                           str(path), "--format", fmt])
        original = path.read_bytes()
        path.with_suffix(path.suffix + ".runtime").write_bytes(
            b"F15BIN1\0" + struct.pack("<II", len(original), len(payload)) + original + payload)
    for path in sorted(destination.rglob("*.glb")):
        if path.name == "cockpit.glb":
            continue
        cache = path.parent / "cache" / (path.stem + ".glmesh")
        cache.parent.mkdir(exist_ok=True)
        cache.write_bytes(subprocess.check_output(
            [sys.executable, str(tool), "build-glmesh", str(path)]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination")
    stage(parser.parse_args().destination)
