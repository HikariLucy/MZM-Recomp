#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-$HOME/proyectos/Recomp/MZM-Recomp}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
GBARECOMP="${GBARECOMP:-$WORK/_m0/upstream/gbarecomp}"
BUILD="${GBARECOMP_BUILD:-$GBARECOMP/build-m0}"
ROM="${MZM_ROM:-$WORK/Metroid - Zero Mission (USA).gba}"
IMPORT="${MZM_IMPORT:-$WORK/_m0/evidence/m0.5/symbol-import}"
OUT="${MZM_GENERATED:-$REPO/generated}"
LOG_DIR="$REPO/.local"
LOG="$LOG_DIR/m1-generate.log"

mkdir -p "$LOG_DIR"
rm -rf "$OUT"
mkdir -p "$OUT"

for required in     "$BUILD/gba_recompile"     "$ROM"     "$REPO/configs/mzm-us.toml"     "$IMPORT/BMXE_symbols.toml"     "$IMPORT/imported_symbols.tsv"     "$IMPORT/imported_data_symbols.tsv"
do
    if [[ ! -e "$required" ]]; then
        echo "missing required input: $required" >&2
        exit 2
    fi
done

echo "=== M1A GENERATE MZM CORPUS ==="

"$BUILD/gba_recompile"     --rom "$ROM"     --config "$REPO/configs/mzm-us.toml"     --config "$IMPORT/BMXE_symbols.toml"     --symbols "$IMPORT/imported_symbols.tsv"     --data-symbols "$IMPORT/imported_data_symbols.tsv"     --out "$OUT"     --max-functions 65536     2>&1 | tee "$LOG"

grep -q 'undefined=0' "$LOG"
grep -Eq 'code_copies:[[:space:]]+5' "$LOG"
grep -q 'codegen shards: 16' "$LOG"

test -f "$OUT/recompiled.h"
test -f "$OUT/dispatch_table.cpp"

SHARDS="$(find "$OUT" -maxdepth 1 -type f -name 'recompiled_[0-9][0-9][0-9].cpp' | wc -l)"
if [[ "$SHARDS" -ne 16 ]]; then
    echo "expected 16 generated shards, found $SHARDS" >&2
    exit 3
fi

echo
echo "M1A generation PASS"
echo "generated=$OUT"
echo "shards=$SHARDS"
echo "log=$LOG"
