#!/usr/bin/env bash
# Package MZM Recompiled Beta 3 for Linux x86_64 from a completed Release build.
set -euo pipefail
export LC_ALL=C

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${MZM_BUILD_DIR:-$REPO/build-b3-linux}"
NAME=MZMRecompiled-Beta-3-Linux-x86_64
OUT="${MZM_PACKAGE_OUT:-$REPO/dist/public-beta-3}"
DEST="$OUT/$NAME"
ARCHIVE="$OUT/$NAME.tar.gz"
DOCS="$REPO/docs/beta3"

fail() { echo "Linux package rejected: $*" >&2; exit 2; }
[[ -x "$BUILD/MZMRecomp" ]] || fail "missing $BUILD/MZMRecomp"
file "$BUILD/MZMRecomp" | grep -q 'ELF 64-bit.*x86-64' || fail 'not a Linux x86_64 ELF'
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$BUILD/CMakeCache.txt" || fail 'not a Release build'
grep -q '^MZM_RECOMP_UI:BOOL=ON$' "$BUILD/CMakeCache.txt" || fail 'launcher (MZM_RECOMP_UI) is OFF'
grep -q '^MZM_PERF_PROFILE:BOOL=OFF$' "$BUILD/CMakeCache.txt" || fail 'profiling build'
[[ -d "$BUILD/assets/fonts" && -d "$BUILD/assets/img" ]] || fail 'launcher assets missing from build'
strings "$BUILD/MZMRecomp" | grep -q 'INITIAL SYSTEM CONFIGURATION' || fail 'launcher not linked into MZMRecomp'
readelf -d "$BUILD/MZMRecomp" | grep -Ei 'rpath|runpath' && fail 'binary has an RPATH/RUNPATH'
for doc in README.md FEEDBACK.md KNOWN-ISSUES.md RELEASE-NOTES-BETA-3.md; do
    [[ -f "$DOCS/$doc" ]] || fail "missing $DOCS/$doc"
done
GBARECOMP_ROOT="${MZM_GBARECOMP_ROOT:-$(sed -n 's/^GBARECOMP_ROOT:PATH=//p' "$BUILD/CMakeCache.txt")}"
[[ -f "$GBARECOMP_ROOT/LICENSE" ]] || fail 'GBARecomp checkout (LICENSE) not found'

mkdir -p "$OUT"
STAGE="$(mktemp -d "$OUT/.mzm-linux.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets/icons" "$STAGE/assets/img" "$STAGE/assets/fonts" "$STAGE/configs" \
    "$STAGE/THIRD-PARTY-LICENSES"
cp "$BUILD/MZMRecomp" "$STAGE/MZMRecomp"
for font in LatoLatin-Regular.ttf LatoLatin-Bold.ttf \
            NotoSansSymbols2-Regular.ttf OpenMoji-black-glyf.ttf; do
    cp "$BUILD/assets/fonts/$font" "$STAGE/assets/fonts/"
done
cp "$REPO/recomp-ui/assets/common/fonts/NOTICE.md" "$STAGE/assets/fonts/NOTICE.md"
cp "$BUILD/assets/img"/verdict_*.tga "$STAGE/assets/img/"
cp "$REPO/assets/icons/mzm-recompiled.svg" "$REPO/assets/icons/mzm-recompiled.bmp" \
   "$REPO/assets/icons/mzm-brand-helm-core.png" "$STAGE/assets/icons/"
cp "$REPO/assets/icons/"mzm-recompiled-[0-9]*.png "$STAGE/assets/icons/"
cp "$REPO/assets/linux/mzm-recompiled.desktop" "$STAGE/MZMRecompiled.desktop"
cp "$REPO/configs/mzm-us.toml" "$STAGE/configs/"
for doc in README.md FEEDBACK.md KNOWN-ISSUES.md RELEASE-NOTES-BETA-3.md; do
    cp "$DOCS/$doc" "$STAGE/$doc"
done
cp "$GBARECOMP_ROOT/LICENSE" "$STAGE/THIRD-PARTY-LICENSES/GBARecomp.txt"
cp "$REPO/recomp-ui/LICENSE" "$STAGE/THIRD-PARTY-LICENSES/recomp-ui.txt"
cp "$REPO/recomp-ui/src/third_party/imgui/LICENSE.txt" "$STAGE/THIRD-PARTY-LICENSES/Dear-ImGui.txt"
sed -n '1,51p' "$REPO/recomp-ui/src/third_party/tinyfiledialogs.c" \
    > "$STAGE/THIRD-PARTY-LICENSES/tinyfiledialogs.txt"
cp "$GBARECOMP_ROOT/external/arm-recomp-core/LICENSE" "$STAGE/THIRD-PARTY-LICENSES/arm-recomp-core.txt"
LICENSE_DIR="${MZM_WINDOWS_LICENSE_DIR:-}"
if [[ -n "$LICENSE_DIR" ]]; then
    for notice in SIL-OFL-1.1.txt CC-BY-SA-4.0.txt; do
        cp "$LICENSE_DIR/$notice" "$STAGE/THIRD-PARTY-LICENSES/$notice"
    done
fi

MZM_SHA="$(git -C "$REPO" rev-parse HEAD)"
GBA_SHA="$(git -C "$GBARECOMP_ROOT" rev-parse HEAD)"
UI_SHA="$(git -C "$REPO/recomp-ui" rev-parse HEAD)"
CXX_BIN="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' "$BUILD/CMakeCache.txt")"
cat > "$STAGE/BUILD-INFO.txt" <<INFO
MZM Recompiled Beta 3 - Public Runtime Test (Linux x86_64)
MZM commit:          $MZM_SHA$(git -C "$REPO" diff --quiet HEAD -- . 2>/dev/null || echo ' (+uncommitted changes)')
GBARecomp commit:    $GBA_SHA
recomp-ui commit:    $UI_SHA
Compiler:            $("$CXX_BIN" --version | head -n1)
Build type:          Release
Build date (UTC):    $(date -u +%Y-%m-%dT%H:%M:%SZ)
Target platform:     Linux x86_64 (ELF, dynamically linked: system SDL2, OpenGL, libstdc++)
MZMRecomp SHA-256:   $(sha256sum "$STAGE/MZMRecomp" | cut -d' ' -f1)
Strict static:       requested by the launcher (no interpreter fallback)
Linux runtime:       TESTED
Windows runtime:     see the Windows package (community validation requested)

The exact GBARecomp commit above is a local revision of the project owner's
GBARecomp work line; it is not guaranteed to exist on a public remote.
INFO

if find "$STAGE" -type f \( -iname '*.gba' -o -iname '*.agb' -o -iname '*.bin' \
    -o -iname '*.bios' -o -iname '*.sav' -o -iname '*.srm' -o -iname '*.ss[0-9]' \
    -o -iname '*rom.cfg' -o -iname '*bios.cfg' -o -iname 'launcher.ini' \
    -o -iname 'recompiled_*' -o -iname '*.elf' \) | grep -q .; then
    fail 'game data, generated source or local state in staging directory'
fi
rm -rf "$DEST" "$ARCHIVE"
mv "$STAGE" "$DEST"
trap - EXIT
chmod -R u+rwX,go+rX,go-w "$DEST"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='UTC 2026-01-01' \
    -C "$OUT" -czf "$ARCHIVE" "$NAME"
echo "Package ready: $DEST"
echo "Archive ready: $ARCHIVE"
