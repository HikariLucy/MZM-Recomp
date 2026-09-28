#!/usr/bin/env bash
set -euo pipefail

REPO="${REPO:-$HOME/proyectos/Recomp/MZM-Recomp}"
WORK="${WORK:-$HOME/proyectos/Recomp/Metroid-ZeroMissionRecomp}"
BIOS="${GBA_BIOS:-$HOME/proyectos/Recomp/KH-CoM/bios/gba_bios.bin}"
ROM="${MZM_ROM:-$WORK/Metroid - Zero Mission (USA).gba}"
BIN="${MZM_BIN:-$REPO/build-m1/MZMRecomp}"
CONFIG="${MZM_CONFIG:-$REPO/configs/mzm-us.toml}"
SAVE="${MZM_SAVE:-${ROM%.gba}.sav}"

STAMP="$(date +%Y%m%d-%H%M%S)"
SESSION="$REPO/.local/m4-sessions/$STAMP"
LOG="$SESSION/runtime.log"

mkdir -p "$SESSION"

for required in "$BIN" "$BIOS" "$ROM" "$CONFIG"; do
    if [[ ! -e "$required" ]]; then
        echo "missing required input: $required" >&2
        exit 2
    fi
done

{
    echo "session=$STAMP"
    echo "binary=$BIN"
    echo "rom=$ROM"
    echo "bios=$BIOS"
    echo "config=$CONFIG"
    echo
    echo "ROM:"
    sha1sum "$ROM"
    echo "BIOS:"
    sha1sum "$BIOS"

    if [[ -f "$SAVE" ]]; then
        echo "SAVE_BEFORE:"
        stat -c 'size=%s modified=%y' "$SAVE"
        sha256sum "$SAVE"
    else
        echo "SAVE_BEFORE: absent"
    fi
} > "$SESSION/manifest-before.txt"

if [[ -f "$SAVE" ]]; then
    cp -a "$SAVE" "$SESSION/save-before.sav"
fi

echo "=== M4 STRICT-STATIC SESSION ==="
echo "evidence=$SESSION"
echo
echo "Play normally. Close the game when you reach a useful checkpoint or encounter a defect."
echo

set +e
GBARECOMP_STRICT_STATIC=1 "$BIN"     --bios "$BIOS"     --rom "$ROM"     --config "$CONFIG"     2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e

{
    echo "exit=$RC"
    if [[ -f "$SAVE" ]]; then
        echo "SAVE_AFTER:"
        stat -c 'size=%s modified=%y' "$SAVE"
        sha256sum "$SAVE"
    else
        echo "SAVE_AFTER: absent"
    fi
} > "$SESSION/manifest-after.txt"

if [[ -f "$SAVE" ]]; then
    cp -a "$SAVE" "$SESSION/save-after.sav"
fi

echo
echo "=== SESSION SUMMARY ==="
grep -E 'cpu_backend=|strict_static=|final_pc=|unmapped=|io_unhandled=|self_heal_coverage=|dispatch_misses=|interpreted_insns=|healed_native=' "$LOG" || true

echo
echo "=== SAVE HASHES ==="
grep -hE '^[0-9a-f]{64} '     "$SESSION/manifest-before.txt"     "$SESSION/manifest-after.txt"     2>/dev/null || true

echo
echo "session_dir=$SESSION"
echo "exit=$RC"

exit "$RC"
