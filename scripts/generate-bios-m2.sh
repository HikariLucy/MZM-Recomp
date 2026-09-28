#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-$HOME/proyectos/Recomp/MZM-Recomp}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
GBARECOMP="${GBARECOMP:-$WORK/_m0/upstream/gbarecomp}"
BUILD="${GBARECOMP_BUILD:-$GBARECOMP/build-m0}"
BIOS="${GBA_BIOS:-$HOME/proyectos/Recomp/KH-CoM/bios/gba_bios.bin}"
OUT="${MZM_BIOS_GENERATED:-$REPO/.local/generated-bios}"
LOG="$REPO/.local/m2-bios-generate.log"
CONFIG="$GBARECOMP/bios/gba_bios.toml"
EXPECTED_SHA1="300c20df6731a33952ded8c436f7f186d25d3492"

mkdir -p "$REPO/.local"

for required in "$BUILD/gba_recompile" "$BIOS" "$CONFIG"; do
    if [[ ! -e "$required" ]]; then
        echo "missing required input: $required" >&2
        exit 2
    fi
done

ACTUAL_SHA1="$(sha1sum "$BIOS" | awk '{print $1}')"
if [[ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]]; then
    echo "BIOS SHA-1 mismatch" >&2
    echo "expected=$EXPECTED_SHA1" >&2
    echo "actual=$ACTUAL_SHA1" >&2
    exit 3
fi

rm -rf "$OUT"
mkdir -p "$OUT"

echo "=== M2A GENERATE RECOMPILED BIOS ==="
echo "bios=$BIOS"
echo "sha1=$ACTUAL_SHA1"
echo "out=$OUT"

"$BUILD/gba_recompile"     --bios "$BIOS"     --config "$CONFIG"     --out "$OUT"     --max-functions 4096     2>&1 | tee "$LOG"

test -s "$OUT/bios_recompiled.cpp"
test -s "$OUT/bios_recompiled.h"
test -s "$OUT/bios_dispatch_table.cpp"

echo
echo "M2A BIOS generation PASS"
echo "generated=$OUT"
echo "log=$LOG"
