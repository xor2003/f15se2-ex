"""Compile editable structured assets so a release does not need Python at runtime."""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


MAX_BYTES = 1024 * 1024
FORMATS = {".WLD", ".3D3", ".3DG", ".3DT"}


def compile_asset(path):
    asset_format = path.with_suffix("").suffix.upper()
    source = path.read_bytes()
    if not 0 < len(source) <= MAX_BYTES:
        raise ValueError(f"Source exceeds runtime limit: {path}")
    if asset_format == ".3D3" and "model_data" not in json.loads(source):
        raise ValueError(f"Model container has no byte stream: {path}")
    cli = Path(__file__).with_name("cli.py")
    payload = subprocess.run(
        [sys.executable, str(cli), "build-binary", str(path),
         "--format", asset_format[1:]],
        check=True, stdout=subprocess.PIPE, timeout=30,
    ).stdout
    if not 0 < len(payload) <= MAX_BYTES:
        raise ValueError(f"Compiled asset exceeds runtime limit: {path}")
    if path.read_bytes() != source:
        raise ValueError(f"Source changed during conversion: {path}")
    destination = Path(str(path) + ".runtime")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(struct.pack("<8sII", b"F15BIN1\0", len(source), len(payload)))
            output.write(source)
            output.write(payload)
        os.replace(temporary, destination)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    paths = sorted(path for path in args.root.rglob("*.json")
                   if path.with_suffix("").suffix.upper() in FORMATS)
    if not paths:
        parser.error("No structured assets found")
    for path in paths:
        print(compile_asset(path))


if __name__ == "__main__":
    main()
