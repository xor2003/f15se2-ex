#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
javac -d "$scratch/classes" \
    "$root/android/app/src/main/java/org/f15se2/ex/GameAssetInstaller.java" \
    "$root/android/tests/GameAssetInstallerTest.java"
mkdir "$scratch/data"
java -cp "$scratch/classes" org.f15se2.ex.GameAssetInstallerTest "$scratch/data"
