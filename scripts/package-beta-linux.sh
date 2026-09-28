#!/usr/bin/env bash
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${MZM_BUILD_DIR:-$REPO/build-m1}"
DEST="${MZM_PACKAGE_DIR:-$REPO/dist/MZMRecompiled-Beta}"

if [[ ! -x "$BUILD/MZMRecomp" ]]; then
    echo "Missing $BUILD/MZMRecomp; build first with scripts/build-m1.sh" >&2
    exit 2
fi
if [[ ! -d "$BUILD/assets/fonts" || ! -d "$BUILD/assets/img" ]]; then
    echo "Launcher assets are missing; build with MZM_RECOMP_UI=ON" >&2
    exit 2
fi
if ! file "$BUILD/MZMRecomp" | grep -q 'ELF 64-bit.*x86-64'; then
    echo "Expected a Linux x86_64 ELF executable" >&2
    exit 2
fi

mkdir -p "$(dirname "$DEST")"
STAGE="$(mktemp -d "$(dirname "$DEST")/.mzm-beta.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets/icons" "$STAGE/configs" "$STAGE/runtime"
mkdir -p "$STAGE/assets/img"
cp "$BUILD/MZMRecomp" "$STAGE/MZMRecomp"
cp -a "$BUILD/assets/fonts" "$STAGE/assets/"
cp "$REPO/recomp-ui/assets/common/fonts/NOTICE.md" "$STAGE/assets/fonts/NOTICE.md"
# Keep only shared status glyphs; the generic launcher brand/pad art is unused.
cp "$BUILD/assets/img"/verdict_*.tga "$STAGE/assets/img/"
cp "$REPO/assets/icons/mzm-recompiled.svg" "$STAGE/assets/icons/"
cp "$REPO/assets/icons/mzm-recompiled.bmp" "$STAGE/assets/icons/"
cp "$REPO/assets/icons/"mzm-recompiled-*.png "$STAGE/assets/icons/"
cp "$REPO/assets/linux/mzm-recompiled.desktop" "$STAGE/MZMRecompiled.desktop"
cp "$REPO/configs/mzm-us.toml" "$STAGE/configs/"
cp "$REPO/docs/BETA-TESTING.md" "$STAGE/README.md"
cat > "$STAGE/runtime/README.md" <<'EOF'
The beta uses system SDL2, OpenGL, and standard Linux runtime libraries.
No game data, BIOS, or generated source files are bundled here.
EOF

if find "$STAGE" -type f \( -iname '*.gba' -o -iname '*.agb' -o -iname '*.bin' \
     -o -iname '*.bios' -o -iname '*.sav' -o -iname '*rom.cfg' -o -iname '*bios.cfg' \) | grep -q .; then
    echo "Package rejected: game data or local state detected" >&2
    exit 3
fi
rm -rf "$DEST"
mv "$STAGE" "$DEST"
trap - EXIT
echo "Package ready: $DEST"
