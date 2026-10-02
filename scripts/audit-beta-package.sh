#!/usr/bin/env bash
# Static hygiene/inspection audit of a Beta 3 package archive (Windows .zip or Linux .tar.gz).
# Prints PASS/FAIL lines; exit status 1 on any FAIL. Never runs the program.
set -uo pipefail
export LC_ALL=C
ARCHIVE="$1"; EXPECT_MZM_SHA="${2:-}"; EXPECT_GBA_SHA="${3:-}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
FAILS=0
ok()   { echo "  PASS  $*"; }
bad()  { echo "  FAIL  $*"; FAILS=$((FAILS+1)); }
chk()  { local msg="$1"; shift; if "$@"; then ok "$msg"; else bad "$msg"; fi; }
case "$ARCHIVE" in
  *.zip) PLAT=windows; unzip -q "$ARCHIVE" -d "$WORK" ;;
  *.tar.gz) PLAT=linux; tar -xzf "$ARCHIVE" -C "$WORK" ;;
  *) echo "unknown archive type"; exit 2 ;;
esac
ROOT="$WORK/$(ls "$WORK" | head -n1)"
echo "== $PLAT package: $(basename "$ARCHIVE")"
echo "   top-level entries: $(ls "$ROOT" | tr '\n' ' ')"

# --- forbidden content
none() { ! find "$ROOT" "$@" | grep -q .; }
chk "no ROM/BIOS files (*.gba, *.agb, *.bin, *.bios, *.rom)" none -type f \( -iname '*.gba' -o -iname '*.agb' -o -iname '*.bin' -o -iname '*.bios' -o -iname '*.rom' \)
chk "no save/state files" none -type f \( -iname '*.sav' -o -iname '*.srm' -o -iname '*.ss[0-9]' -o -iname '*.state*' \)
chk "no local state files (launcher.ini, rom.cfg, bios.cfg, keybinds.ini, config.ini)" none -type f \( -iname 'launcher.ini' -o -iname 'rom.cfg' -o -iname 'bios.cfg' -o -iname 'keybinds.ini' -o -iname 'config.ini' \)
chk "no generated source (recompiled_*, generated/)" none \( -iname 'recompiled_*' -o -iname 'generated' -o -iname '*.cpp' -o -iname '*.cc' -o -iname '*.c' \)
chk "no .local / build directories / git data" none \( -name '.local' -o -name 'build*' -o -name '.git*' -o -name CMakeFiles -o -name '*.log' \)
chk "no screenshots of the game (png/bmp outside assets/icons, img)" bash -c '! find "$0" -type f \( -iname "*.png" -o -iname "*.bmp" -o -iname "*.jpg" \) | grep -v "/assets/icons/" | grep -q .' "$ROOT"
chk "no private keys / tokens" bash -c '! grep -rIlE "BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|ghp_[A-Za-z0-9]{20}|AKIA[0-9A-Z]{16}|xox[bp]-" "$0" | grep -q .' "$ROOT"
# ELF files: only allowed in the linux package, and then only MZMRecomp itself
elfs=$(find "$ROOT" -type f -exec sh -c 'head -c4 "$1" | grep -q "^.ELF"' _ {} \; -print | sed "s|$ROOT/||")
if [[ $PLAT == windows ]]; then chk "no ELF files in the Windows package" test -z "$elfs"
else chk "only MZMRecomp is an ELF (no nested ELF)" test "$elfs" = "MZMRecomp"; fi

# --- path / identity leakage in text and binaries
leak_pat='/home/|/Users/|C:\\Users|proyectos|hikarilucy|Recomp-enhancements|/tmp/'
chk "no developer paths in text files" bash -c '! grep -rIlE "$1" "$0" | grep -q .' "$ROOT" "$leak_pat"
BIN="$ROOT/MZMRecomp"; [[ $PLAT == windows ]] && BIN="$ROOT/MZMRecomp.exe"
# also catches paths split across instruction immediates, so look at raw bytes too
hits=$(strings -a "$BIN" | grep -E '/home/|/Users/|C:\\Users|proyectos|hikarilucy|/tmp/' | grep -v 'tinyfd' | head -5)
raw=$(grep -caE '/home/hi|karilucy|/proyect|C:.Users' "$BIN" || true)
chk "no developer paths embedded in the executable" test -z "$hits"
[[ -n "$hits" ]] && echo "$hits" | sed 's/^/        /'
chk "no developer path fragments in the raw executable bytes (split literals)" test "${raw:-0}" = 0
src_hits=$(strings -a "$BIN" | grep -cE 'recompiled_[0-9]+\.cpp|/generated/')
chk "no generated-source file names embedded in the executable" test "$src_hits" = 0

# --- identity
sha_in_bin() { grep -qa -- "$1" "$BIN"; }
chk "'.local/' appears only as the documented generator-time NES payload reference or the user-facing ~/.local/state path" bash -c '! grep -rIn "\.local/" "$0" | grep -vE "~/\.local/state|XDG_STATE_HOME|nes-payload-usa\.bin" | grep -q .' "$ROOT"
if [[ -n "$EXPECT_MZM_SHA" ]]; then chk "executable embeds MZM SHA ${EXPECT_MZM_SHA:0:12}" sha_in_bin "$EXPECT_MZM_SHA"; fi
if [[ -n "$EXPECT_GBA_SHA" ]]; then chk "executable embeds GBARecomp SHA ${EXPECT_GBA_SHA:0:12}" sha_in_bin "$EXPECT_GBA_SHA"; fi
chk "executable embeds 'Beta 3'" sha_in_bin "Beta 3"
chk "BUILD-INFO.txt present and has no absolute paths" bash -c 'test -f "$0/BUILD-INFO.txt" && ! grep -qE "/home/|C:\\\\" "$0/BUILD-INFO.txt"' "$ROOT"
for d in README.md FEEDBACK.md KNOWN-ISSUES.md RELEASE-NOTES-BETA-3.md; do chk "$d present" test -f "$ROOT/$d"; done
chk "no 'Release Candidate' / 'RC' / '1.0' / 'stable' claims in the docs" bash -c '! grep -rIniE "release candidate|\bRC[ -]?[0-9]|\b1\.0\b|\bstable\b" "$0"/*.md "$0"/BUILD-INFO.txt | grep -v "AVAILABLE" | grep -q .' "$ROOT"
chk "docs do not offer ROM/BIOS downloads" bash -c '! grep -rIniE "download.*(rom|bios)|(rom|bios).*download" "$0"/*.md | grep -q .' "$ROOT"
chk "required assets present (fonts, helm core, icon, config)" bash -c 'cd "$0" && test -f assets/fonts/LatoLatin-Regular.ttf && test -f assets/fonts/LatoLatin-Bold.ttf && test -f assets/icons/mzm-brand-helm-core.png && test -f assets/icons/mzm-recompiled.bmp && test -f configs/mzm-us.toml && ls assets/img/verdict_*.tga >/dev/null' "$ROOT"

if [[ $PLAT == windows ]]; then
  echo "-- PE inspection"
  OBJ=x86_64-w64-mingw32-objdump; command -v $OBJ >/dev/null || OBJ=objdump
  chk "MZMRecomp.exe is PE32+ x86-64" bash -c '"$0" -f "$1" | grep -q "file format pei-x86-64"' "$OBJ" "$BIN"
  stack=$("$OBJ" -p "$BIN" | awk '/SizeOfStackReserve/ {print $2}'); echo "   SizeOfStackReserve=0x$stack"
  chk "PE stack reserve >= 8 MiB (GBARecomp host stack)" test $((16#$stack)) -ge 8388608
  echo "   subsystem: $("$OBJ" -p "$BIN" | awk '/^Subsystem/ {print $2,$3,$4}')"
  shopt -s nocaseglob
  declare -A seen bundled; queue=("$BIN"); missing=0
  while ((${#queue[@]})); do
    b="${queue[0]}"; queue=("${queue[@]:1}")
    for dll in $("$OBJ" -p "$b" | sed -n 's/^[[:space:]]*DLL Name: //p'); do
      key="${dll,,}"; [[ -n "${seen[$key]:-}" ]] && continue; seen[$key]=1
      real=$(ls "$ROOT" | grep -ix -- "$dll" | head -n1)
      if [[ -n "$real" ]]; then queue+=("$ROOT/$real"); echo "   import $dll -> bundled ($real)"; bundled["$key"]=1
        "$OBJ" -f "$ROOT/$real" | grep -q 'pei-x86-64' || { bad "bundled $dll is not PE32+ x86-64"; }
      else echo "   import $dll -> system"; fi
    done
  done
  # every non-bundled import must be a plain Windows system DLL
  for k in "${!seen[@]}"; do [[ -n "${bundled[$k]:-}" ]] && continue
    case "$k" in kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|comdlg32.dll|winmm.dll|imm32.dll|version.dll|setupapi.dll|opengl32.dll|ws2_32.dll|msvcrt.dll|ucrtbase.dll|shlwapi.dll|cfgmgr32.dll|api-ms-win-*) ;; *) bad "unexpected non-bundled import $k"; missing=1;; esac
  done
  chk "all imports are bundled or Windows system DLLs" test $missing = 0
  chk "no MSVC/UCRT redistributable needed beyond system (no vcruntime*/msvcp*)" bash -c '! ls "$0" | grep -iE "vcruntime|msvcp" | grep -q .' "$ROOT"
  chk "executable has a Win32 icon resource" bash -c '"$0" -x "$1" 2>/dev/null | grep -qi "\.rsrc"' "$OBJ" "$BIN"
else
  echo "-- ELF inspection"
  chk "MZMRecomp is Linux x86-64 ELF" bash -c 'file "$0" | grep -q "ELF 64-bit.*x86-64"' "$BIN"
  chk "no RPATH/RUNPATH" bash -c '! readelf -d "$0" | grep -Eqi "rpath|runpath"' "$BIN"
  echo "   needed: $(readelf -d "$BIN" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')"
fi
echo "== $PLAT audit: $([[ $FAILS == 0 ]] && echo PASS || echo "FAIL ($FAILS)")"
exit $((FAILS>0))
