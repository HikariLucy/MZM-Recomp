# MZM Recompiled visual identity

The launcher uses the approved Helm Core concept board selected by the user:
`/home/hikarilucy/Descargas/MZM-Recompiled-Zero-Core/IMAGENREFERENCIA.png`.
Home shows its orange helmet, green visor, and luminous cyan orbit. The native
app icon uses the board's compact version of the same mark. Dark surfaces and
the warm energy action support the artwork.
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

`assets/icons/mzm-recompiled-source.png` is the approved compact crop.
Regenerate 16, 32, 48, 64, 128, 256 and 512 px PNGs, the SDL2 BMP, and a
self-contained SVG raster wrapper with `python3 scripts/render-icon.py`
(requires Pillow). `mzm-brand-helm-core.png` is the Home crop.
The launcher loads the BMP beside the executable. The Linux beta includes SVG,
PNG sizes, BMP and the `.desktop` template.

Game and BIOS images remain player supplied and are never packaged.

## Linux desktop template

`MZMRecompiled.desktop` uses `Exec=MZMRecomp` and `Icon=mzm-recompiled`. It is a
template for an installed application: place the binary on `PATH` and install
an icon named `mzm-recompiled` in the user's icon theme, or adjust both fields
to the actual installed locations. A portable directory should be started with
`./MZMRecomp` from that directory. Packaging does not register the desktop file.
