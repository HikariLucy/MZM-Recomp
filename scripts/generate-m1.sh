#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${REPO:-$(cd "$SCRIPT_DIR/.." && pwd)}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
GBARECOMP="${GBARECOMP_ROOT:-${GBARECOMP:-$HOME/proyectos/Recomp/GBARecomp-mzm-integration}}"
PIN="${MZM_GBARECOMP_PIN:-${PIN:-e0c7cb26c1f3814327ed7872f6e1c33bc7cccb21}}"
if [[ "$(git -C "$GBARECOMP" rev-parse HEAD)" != "$PIN" ]]; then
    echo "GBARecomp revision must be $PIN; found $(git -C "$GBARECOMP" rev-parse HEAD)" >&2
    exit 2
fi
if [[ -n "${GBARECOMP_BUILD:-}" ]]; then
    BUILD="$GBARECOMP_BUILD"
elif [[ -x "$GBARECOMP/build/gba_recompile" ]]; then
    BUILD="$GBARECOMP/build"
else
    BUILD="$GBARECOMP/build-m0"
fi
ROM="${MZM_ROM:-$WORK/Metroid - Zero Mission (USA).gba}"
IMPORT="${MZM_IMPORT:-$WORK/_m0/evidence/m0.5/symbol-import}"
OUT="${MZM_GENERATED:-$REPO/generated}"
LOG_DIR="$REPO/.local"
LOG="$LOG_DIR/m1-generate.log"
OVERLAY="$LOG_DIR/BMXE_symbols.toml"

mkdir -p "$LOG_DIR"
rm -rf "$OUT"
mkdir -p "$OUT"

for required in \
    "$BUILD/gba_recompile" \
    "$ROM" \
    "$REPO/configs/mzm-us.toml" \
    "$IMPORT/BMXE_symbols.toml" \
    "$IMPORT/imported_symbols.tsv" \
    "$IMPORT/imported_data_symbols.tsv"
do
    if [[ ! -e "$required" ]]; then
        echo "missing required input: $required" >&2
        exit 2
    fi
done

echo "=== M1A PREPARE REVIEWED NES SYMBOLS OVERLAY ==="
python3 "$REPO/scripts/prepare-nes-overlay.py" "$IMPORT/BMXE_symbols.toml" "$OVERLAY"

echo "=== M1A GENERATE MZM CORPUS ==="

"$BUILD/gba_recompile" \
    --rom "$ROM" \
    --config "$REPO/configs/mzm-us.toml" \
    --config "$OVERLAY" \
    --symbols "$IMPORT/imported_symbols.tsv" \
    --data-symbols "$IMPORT/imported_data_symbols.tsv" \
    --out "$OUT" \
    --max-functions 65536 \
    2>&1 | tee "$LOG"

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

# Assert verified NES entry points are present and data is suppressed
grep -q '0x087D8000u' "$OUT/dispatch_table.cpp"
grep -q '0x087D80D4u' "$OUT/dispatch_table.cpp"
grep -q '0x087D8124u' "$OUT/dispatch_table.cpp"
grep -q '0x087D812Cu' "$OUT/dispatch_table.cpp"
if grep -q '0x087D8004u' "$OUT/dispatch_table.cpp"; then
    echo "unexpected code generation at NES literal pool 0x087D8004" >&2
    exit 4
fi

echo
echo "M1A generation PASS"
echo "generated=$OUT"
echo "shards=$SHARDS"
echo "log=$LOG"
