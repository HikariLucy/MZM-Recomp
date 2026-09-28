#!/usr/bin/env python3
"""Regenerate the distributable icons from the original SVG (librsvg + Pillow)."""

import ctypes
from pathlib import Path
from PIL import Image

repo = Path(__file__).resolve().parents[1]
source = repo / "assets/icons/mzm-recompiled.svg"
out = source.parent
rsvg = ctypes.CDLL("librsvg-2.so.2")
cairo = ctypes.CDLL("libcairo.so.2")
rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
rsvg.rsvg_handle_render_cairo.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
rsvg.rsvg_handle_render_cairo.restype = ctypes.c_int
cairo.cairo_image_surface_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
cairo.cairo_image_surface_create.restype = ctypes.c_void_p
cairo.cairo_create.argtypes = [ctypes.c_void_p]
cairo.cairo_create.restype = ctypes.c_void_p
cairo.cairo_scale.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
cairo.cairo_surface_write_to_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
rsvg.g_object_unref.argtypes = [ctypes.c_void_p]

handle = rsvg.rsvg_handle_new_from_file(str(source).encode(), None)
if not handle:
    raise SystemExit(f"Cannot read {source}")
try:
    for size in (16, 32, 64, 128, 256, 512):
        surface = cairo.cairo_image_surface_create(0, size, size)
        context = cairo.cairo_create(surface)
        cairo.cairo_scale(context, size / 256, size / 256)
        if not rsvg.rsvg_handle_render_cairo(handle, context):
            raise SystemExit("SVG rendering failed")
        png = out / f"mzm-recompiled-{size}.png"
        cairo.cairo_surface_write_to_png(surface, str(png).encode())
        cairo.cairo_destroy(context)
        cairo.cairo_surface_destroy(surface)
    # SDL2's BMP loader needs no additional runtime image library.
    Image.open(out / "mzm-recompiled-64.png").convert("RGB").save(
        out / "mzm-recompiled.bmp")
finally:
    rsvg.g_object_unref(handle)
