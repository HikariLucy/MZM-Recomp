#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-$HOME/proyectos/Recomp/MZM-Recomp}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
GBARECOMP="${GBARECOMP:-$WORK/_m0/upstream/gbarecomp}"
RECOMP_UI="${RECOMP_UI_ROOT:-$REPO/recomp-ui}"
BUILD_DIR="${MZM_LAUNCHER_BUILD_DIR:-$REPO/build-launcher}"
BIOS_GENERATED="${MZM_BIOS_GENERATED:-$REPO/.local/generated-bios}"
JOBS="${MZM_BUILD_JOBS:-4}"

if [[ ! -f "$REPO/generated/recompiled.h" ]]; then
    echo "generated MZM corpus not found; run scripts/generate-m1.sh first" >&2
    exit 2
fi

if [[ ! -f "$BIOS_GENERATED/bios_recompiled.cpp" ]]; then
    echo "generated BIOS corpus not found; run scripts/generate-bios-m2.sh first" >&2
    exit 2
fi

if [[ ! -f "$RECOMP_UI/recomp_ui.cmake" ]]; then
    cat >&2 <<EOF
recomp-ui is not initialized at:
  $RECOMP_UI

Initialize the pinned submodule with:
  cd "$REPO"
  git submodule update --init --recursive recomp-ui
EOF
    exit 3
fi

if [[ ! -f "$GBARECOMP/CMakeLists.txt" ]]; then
    echo "GBARecomp checkout not found: $GBARECOMP" >&2
    exit 4
fi

echo "=== MZM PRODUCT SHELL: CONFIGURE ==="

cmake     -S "$REPO"     -B "$BUILD_DIR"     -DGBARECOMP_ROOT="$GBARECOMP"     -DGBARECOMP_GENERATED_BIOS_DIR="$BIOS_GENERATED"     -DRECOMP_UI_ROOT="$RECOMP_UI"     -DMZM_RECOMP_UI=ON     -DCMAKE_BUILD_TYPE=RelWithDebInfo

echo
echo "=== MZM PRODUCT SHELL: BUILD ==="

cmake     --build "$BUILD_DIR"     --parallel "$JOBS"

BIN="$BUILD_DIR/MZMRecomp"

if [[ ! -x "$BIN" ]]; then
    echo "launcher-enabled MZMRecomp executable was not produced" >&2
    exit 5
fi

echo
echo "=== MZM PRODUCT SHELL: VERIFY ==="
file "$BIN"
ls -lh "$BIN"

HELP="$("$BIN" --help)"
printf '%s\n' "$HELP"

grep -q 'MZMRecomp' <<<"$HELP"
grep -q 'graphical setup' <<<"$HELP"

echo
echo "MZM launcher build PASS"
echo "binary=$BIN"
echo
echo "Launch with:"
echo "  $BIN"
echo
echo "Force the setup screen with:"
echo "  $BIN --launcher"
