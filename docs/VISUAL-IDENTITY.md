# MZM Recompiled visual identity

The launcher uses an original sci-fi navigation language: quiet dark surfaces,
cartography lines, a geometric M/orbit mark, and a single warm energy action.
PLAY leads Home; Game Data, Settings and About remain secondary. File status
always includes words as well as color.

## Tokens and typography

`src/mzm_theme.h` owns the palette and spacing. Main colors: background
`#070B12`, surface `#0D141F`, elevated `#121D2A`, energy `#FF7A1A`, cool
`#37B7C8`, text `#E7EDF3`, muted `#97A6B6`, success `#4CC38A`, warning
`#F4B942`, error `#E55757`. The button uses energy; the other controls stay
cool and restrained. The build already distributes Lato Latin Regular and
Bold through recomp-ui; the launcher uses these for body and headings.

## Mark and asset rules

`assets/icons/mzm-recompiled.svg` is the editable source. Regenerate 16, 32,
64, 128, 256 and 512 px PNGs plus the SDL2 BMP with `python3
scripts/render-icon.py` (requires librsvg and Pillow at generation time).
The launcher loads the BMP beside the executable. The Linux beta includes SVG,
PNG sizes, BMP and the `.desktop` template.

Never use Nintendo/Metroid logos, screenshots, sprites, maps, extracted ROM
assets, recognizable characters, or proprietary fonts in this identity. Game
and BIOS images remain player supplied and are never packaged.

## Linux desktop template

`MZMRecompiled.desktop` uses `Exec=MZMRecomp` and `Icon=mzm-recompiled`. It is a
template for an installed application: place the binary on `PATH` and install
an icon named `mzm-recompiled` in the user's icon theme, or adjust both fields
to the actual installed locations. A portable directory should be started with
`./MZMRecomp` from that directory. Packaging does not register the desktop file.
