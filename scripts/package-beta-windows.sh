#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C  # objdump output is parsed; locale must not translate it

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${MZM_WINDOWS_BUILD_DIR:-$REPO/build-windows}"
DEST="${MZM_WINDOWS_PACKAGE_DIR:-$REPO/dist/public-beta-3/MZMRecompiled-Beta-3-Windows-x64}"
# MZM_WINDOWS_SMOKE: PASS only after a real Windows x86_64 run of the smoke
# list. The default records that no Windows run has happened yet.
SMOKE="${MZM_WINDOWS_SMOKE:-UNVERIFIED (community validation requested)}"
ZIP="$DEST.zip"
DOCS="$REPO/docs/beta3"
EXE="$BUILD/MZMRecomp.exe"
DLL_DIR="${MZM_WINDOWS_DLL_DIR:-$BUILD}"
LICENSE_DIR="${MZM_WINDOWS_LICENSE_DIR:-}"

fail() { echo "Windows package rejected: $*" >&2; exit 2; }
OBJDUMP="${MZM_WINDOWS_OBJDUMP:-}"
if [[ -z "$OBJDUMP" ]]; then
    if command -v x86_64-w64-mingw32-objdump >/dev/null; then
        OBJDUMP=x86_64-w64-mingw32-objdump
    else
        OBJDUMP=objdump
    fi
fi
command -v "$OBJDUMP" >/dev/null || fail "$OBJDUMP is required to audit PE imports"
command -v rg >/dev/null || fail 'rg is required to audit packaged configuration'
command -v zip >/dev/null || fail 'zip is required'
command -v unzip >/dev/null || fail 'unzip is required to verify the archive'
[[ -f "$EXE" ]] || fail "missing $EXE"
"$OBJDUMP" -f "$EXE" | grep -q 'file format pei-x86-64' \
    || fail "$EXE is not PE32+ x86-64"
[[ -d "$BUILD/assets/fonts" ]] || fail 'launcher fonts missing from build'
[[ -d "$BUILD/assets/img" ]] || fail 'recomp-ui status glyphs missing from build'
[[ -f "$BUILD/assets/icons/mzm-brand-helm-core.png" ]] || fail 'Helm Core missing from build'
[[ -f "$REPO/assets/icons/mzm-recompiled.ico" ]] || fail 'Explorer icon missing'

mkdir -p "$(dirname "$DEST")"
STAGE="$(mktemp -d "$(dirname "$DEST")/.mzm-windows.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets/icons" "$STAGE/assets/img" "$STAGE/assets/fonts" "$STAGE/configs" \
    "$STAGE/THIRD-PARTY-LICENSES"
cp "$EXE" "$STAGE/MZMRecomp.exe"
for font in LatoLatin-Regular.ttf LatoLatin-Bold.ttf \
            NotoSansSymbols2-Regular.ttf OpenMoji-black-glyf.ttf; do
    [[ -f "$BUILD/assets/fonts/$font" ]] || fail "missing launcher font $font"
    cp "$BUILD/assets/fonts/$font" "$STAGE/assets/fonts/"
done
cp "$REPO/recomp-ui/assets/common/fonts/NOTICE.md" "$STAGE/assets/fonts/NOTICE.md"
cp "$BUILD/assets/img"/verdict_*.tga "$STAGE/assets/img/"
cp "$REPO/assets/icons/mzm-recompiled.bmp" "$STAGE/assets/icons/"
cp "$REPO/assets/icons/mzm-recompiled.ico" "$STAGE/assets/icons/"
cp "$REPO/assets/icons/mzm-brand-helm-core.png" "$STAGE/assets/icons/"
cp "$REPO/configs/mzm-us.toml" "$STAGE/configs/"
for doc in README.md FEEDBACK.md KNOWN-ISSUES.md RELEASE-NOTES-BETA-3.md; do
    [[ -f "$DOCS/$doc" ]] || fail "missing $DOCS/$doc"
    cp "$DOCS/$doc" "$STAGE/$doc"
done
GBARECOMP_ROOT="${MZM_GBARECOMP_ROOT:-}"
[[ -n "$GBARECOMP_ROOT" && -f "$GBARECOMP_ROOT/LICENSE" ]] \
    || fail 'set MZM_GBARECOMP_ROOT to the exact linked GBARecomp checkout (LICENSE required)'
cp "$GBARECOMP_ROOT/LICENSE" "$STAGE/THIRD-PARTY-LICENSES/GBARecomp.txt"
cp "$REPO/recomp-ui/LICENSE" "$STAGE/THIRD-PARTY-LICENSES/recomp-ui.txt"
cp "$REPO/recomp-ui/src/third_party/imgui/LICENSE.txt" \
    "$STAGE/THIRD-PARTY-LICENSES/Dear-ImGui.txt"
sed -n '1,51p' "$REPO/recomp-ui/src/third_party/tinyfiledialogs.c" \
    > "$STAGE/THIRD-PARTY-LICENSES/tinyfiledialogs.txt"
# rbengine/recomp-net are compiled only into the optional netplay adapter.
DEPS=(arm-recomp-core)
if grep -q '^GBARECOMP_NETPLAY:BOOL=ON' "$BUILD/CMakeCache.txt" 2>/dev/null; then
    DEPS+=(rbengine recomp-net)
fi
for dependency in "${DEPS[@]}"; do
    [[ -f "$GBARECOMP_ROOT/external/$dependency/LICENSE" ]] \
        || fail "missing GBARecomp dependency license: $dependency"
    cp "$GBARECOMP_ROOT/external/$dependency/LICENSE" \
        "$STAGE/THIRD-PARTY-LICENSES/$dependency.txt"
done
[[ -n "$LICENSE_DIR" ]] || fail 'set MZM_WINDOWS_LICENSE_DIR to verified redistribution notices'
for notice in SIL-OFL-1.1.txt CC-BY-SA-4.0.txt; do
    [[ -f "$LICENSE_DIR/$notice" ]] || fail "missing font license $notice in MZM_WINDOWS_LICENSE_DIR"
    cp "$LICENSE_DIR/$notice" "$STAGE/THIRD-PARTY-LICENSES/$notice"
done

# Inspect the executable and each copied runtime DLL. Only OS DLLs are omitted.
is_system_dll() {
    local name="${1,,}"
    [[ "$name" =~ ^(api-ms-win-|ext-ms-win-) ]] && return 0
    case "$name" in
        kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|comdlg32.dll|comctl32.dll|winmm.dll|imm32.dll|version.dll|setupapi.dll|uuid.dll|dinput8.dll|opengl32.dll|ws2_32.dll|msvcrt.dll|ucrtbase.dll|bcrypt.dll|crypt32.dll|shlwapi.dll|ntdll.dll|secur32.dll|rpcrt4.dll|hid.dll|cfgmgr32.dll|winspool.drv|dwmapi.dll|dxgi.dll|d3d11.dll|d3d12.dll|powrprof.dll|iphlpapi.dll|wtsapi32.dll|userenv.dll) return 0 ;;
    esac
    return 1
}

declare -A SEEN=()
QUEUE=("$STAGE/MZMRecomp.exe")
while ((${#QUEUE[@]})); do
    binary="${QUEUE[0]}"
    QUEUE=("${QUEUE[@]:1}")
    while IFS= read -r dll; do
        key="${dll,,}"
        is_system_dll "$key" && continue
        [[ -n "${SEEN[$key]:-}" ]] && continue
        SEEN[$key]=1
        source=""
        for candidate in "$BUILD/$dll" "$DLL_DIR/$dll"; do
            if [[ -f "$candidate" ]]; then source="$candidate"; break; fi
        done
        [[ -n "$source" ]] || fail "missing imported DLL $dll; set MZM_WINDOWS_DLL_DIR"
        "$OBJDUMP" -f "$source" | grep -q 'file format pei-x86-64' \
            || fail "imported DLL $dll is not PE32+ x86-64"
        [[ -n "$LICENSE_DIR" && -f "$LICENSE_DIR/$dll.LICENSE.txt" ]] \
            || fail "missing redistribution license $dll.LICENSE.txt in MZM_WINDOWS_LICENSE_DIR"
        cp "$source" "$STAGE/$dll"
        cp "$LICENSE_DIR/$dll.LICENSE.txt" "$STAGE/THIRD-PARTY-LICENSES/$dll.LICENSE.txt"
        QUEUE+=("$STAGE/$dll")
    done < <("$OBJDUMP" -p "$binary" | sed -n 's/^[[:space:]]*DLL Name: //p')
done

if find "$STAGE" -type f \( -iname '*.gba' -o -iname '*.agb' -o -iname '*.bin' \
    -o -iname '*.bios' -o -iname '*.rom' -o -iname 'rom' -o -iname 'bios' \
    -o -iname '*.sav' -o -iname '*.srm' -o -iname '*.state*' \
    -o -iname 'recompiled_*.cpp' -o -iname 'recompiled_*.c' \
    -o -iname 'recompiled_*.h' -o -iname '*rom.cfg' -o -iname '*bios.cfg' \
    -o -iname 'launcher.ini' -o -iname '*keybinds.ini' \) | grep -q .; then
    fail 'ROM, BIOS, save, generated source, or local state in staging directory'
fi
if find "$STAGE" -type d -iname 'generated' | grep -q .; then
    fail 'generated source directory in staging directory'
fi
if rg -l -i --pcre2 '(/home/[^/[:space:]]+|C:[/\\]Users[/\\][^/\\[:space:]]+)' \
    "$STAGE/configs" --glob '*.toml' --glob '*.ini' --glob '*.cfg' | grep -q .; then
    fail 'local private path in packaged configuration'
fi

MZM_SHA="$(git -C "$REPO" rev-parse HEAD)"
GBA_SHA="$(git -C "$GBARECOMP_ROOT" rev-parse HEAD)"
UI_SHA="$(git -C "$REPO/recomp-ui" rev-parse HEAD)"
CXX_BIN="${MZM_WINDOWS_CXX:-x86_64-w64-mingw32-g++}"
STACK_RESERVE="$("$OBJDUMP" -p "$EXE" | awk '/SizeOfStackReserve/ {print $2}')"
cat > "$STAGE/BUILD-INFO.txt" <<INFO
MZM Recompiled Beta 3 - Public Runtime Test (Windows x86_64)
MZM commit:          $MZM_SHA$(git -C "$REPO" diff --quiet HEAD -- . 2>/dev/null || echo ' (+uncommitted changes)')
GBARecomp commit:    $GBA_SHA
recomp-ui commit:    $UI_SHA
Compiler:            $("$CXX_BIN" --version | head -n1) ($("$CXX_BIN" -dumpmachine))
Build type:          Release
Build date (UTC):    $(date -u +%Y-%m-%dT%H:%M:%SZ)
Target platform:     Windows x86_64 (PE32+), cross-built on Linux
MZMRecomp.exe SHA-256: $(sha256sum "$STAGE/MZMRecomp.exe" | cut -d' ' -f1)
PE stack reserve:    0x$STACK_RESERVE
Strict static:       requested by the launcher (no interpreter fallback)
Windows runtime:     $SMOKE
Linux runtime:       TESTED
Bundled DLLs:        $(cd "$STAGE" && ls *.dll | tr '\n' ' ')

The exact GBARecomp commit above is a local revision of the project owner's
GBARecomp work line; it is not guaranteed to exist on a public remote.
See README.md and KNOWN-ISSUES.md.
INFO

rm -rf "$DEST"
mv "$STAGE" "$DEST"
trap - EXIT
tmp_zip="$ZIP.tmp"
rm -f "$tmp_zip"
(cd "$(dirname "$DEST")" && zip -q -r "$tmp_zip" "$(basename "$DEST")")
unzip -tq "$tmp_zip" >/dev/null || fail 'ZIP integrity check failed'
mv "$tmp_zip" "$ZIP"
echo "Windows runtime: $SMOKE"
echo "ROM: NO; BIOS: NO; SAVE: NO; GENERATED: NO; LOCAL PATHS: NO"
echo "Package ready: $DEST"
echo "ZIP ready: $ZIP"
