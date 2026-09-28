#!/usr/bin/env python3
"""Render platform icons from the approved Helm Core concept-board crop."""

import base64
import struct
from pathlib import Path
from PIL import Image

icons = Path(__file__).resolve().parents[1] / "assets/icons"
source = icons / "mzm-recompiled-source.png"
with Image.open(source) as original:
    icon = original.convert("RGBA")
    for size in (16, 32, 48, 64, 128, 256, 512):
        icon.resize((size, size), Image.Resampling.LANCZOS).save(
            icons / f"mzm-recompiled-{size}.png"
        )
    icon.resize((64, 64), Image.Resampling.LANCZOS).convert("RGB").save(
        icons / "mzm-recompiled.bmp"
    )

# PNG-backed ICO entries keep the approved pixels intact at every Explorer size.
sizes = (16, 32, 48, 64, 128, 256)
images = [(icons / f"mzm-recompiled-{size}.png").read_bytes() for size in sizes]
offset = 6 + 16 * len(images)
with (icons / "mzm-recompiled.ico").open("wb") as out:
    out.write(struct.pack("<HHH", 0, 1, len(images)))
    for size, data in zip(sizes, images):
        out.write(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                              1, 32, len(data), offset))
        offset += len(data)
    for data in images:
        out.write(data)

# Keep the SVG self-contained while preserving the approved raster artwork.
png = base64.b64encode(source.read_bytes()).decode("ascii")
(icons / "mzm-recompiled.svg").write_text(
    '<svg xmlns="http://www.w3.org/2000/svg" '
    'xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 512 512" '
    'role="img" aria-label="MZM Recompiled Helm Core icon">\n'
    f'  <image width="512" height="512" xlink:href="data:image/png;base64,{png}"/>\n'
    '</svg>\n'
)
