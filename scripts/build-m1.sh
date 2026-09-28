#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-$HOME/proyectos/Recomp/MZM-Recomp}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
GBARECOMP="${GBARECOMP:-$WORK/_m0/upstream/gbarecomp}"
BUILD_DIR="${MZM_BUILD_DIR:-$REPO/build-m1}"
JOBS="${MZM_BUILD_JOBS:-4}"

if [[ ! -f "$REPO/generated/recompiled.h" ]]; then
    echo "generated corpus not found; run scripts/generate-m1.sh first" >&2
    exit 2
fi

echo "=== M1A CONFIGURE HOST ==="

cmake     -S "$REPO"     -B "$BUILD_DIR"     -DGBARECOMP_ROOT="$GBARECOMP"     -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo
echo "=== M1A BUILD HOST ==="

cmake     --build "$BUILD_DIR"     --parallel "$JOBS"

echo
echo "=== M1A BUILD PRODUCTS ==="

if [[ -x "$BUILD_DIR/MZMRecomp" ]]; then
    file "$BUILD_DIR/MZMRecomp"
    ls -lh "$BUILD_DIR/MZMRecomp"
    echo "M1A host build PASS"
else
    echo "MZMRecomp executable was not produced" >&2
    exit 3
fi
