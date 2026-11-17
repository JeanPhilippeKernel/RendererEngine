#!/usr/bin/env python3
"""Stack the Figma reference over the capture for one region, scaled up.

  zoom.py <out-dir> x0 y0 x1 y1 [--anchor tl|tr|bl|br] [--factor 2] [--name zoom]

Coordinates are Figma-frame coordinates (1600x1000). For anchors other than tl they are measured
from that corner of the frame, exactly as compare.py does, so the same numbers work for both.
Reads <out-dir>/capture-logical.png written by compare.py; writes <out-dir>/<name>.png.
"""
import argparse, os
from PIL import Image

FW, FH = 1600, 1000
ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("rect", nargs=4, type=int)
ap.add_argument("--anchor", default="tl"); ap.add_argument("--factor", type=int, default=2)
ap.add_argument("--name", default="zoom")
ap.add_argument("--reference", default=os.path.join(os.path.dirname(__file__), "reference", "editor-3_2503.png"))
a = ap.parse_args()
cap = Image.open(os.path.join(a.out, "capture-logical.png")).convert("RGB")
ref = Image.open(a.reference).convert("RGB")
x0, y0, x1, y1 = a.rect
def crop(img, w, h):
    cx0, cx1, cy0, cy1 = x0, x1, y0, y1
    if a.anchor[1] == "r": cx0, cx1 = x0 - FW + w, x1 - FW + w
    if a.anchor[0] == "b": cy0, cy1 = y0 - FH + h, y1 - FH + h
    return img.crop((max(0, cx0), max(0, cy0), min(w, cx1), min(h, cy1)))
r, c = crop(ref, FW, FH), crop(cap, cap.width, cap.height)
w, h = min(r.width, c.width), min(r.height, c.height)
r, c = r.crop((0, 0, w, h)), c.crop((0, 0, w, h))
f = a.factor
sheet = Image.new("RGB", (w * f, h * f * 2 + 6), (255, 0, 255))
sheet.paste(r.resize((w * f, h * f), Image.LANCZOS), (0, 0))
sheet.paste(c.resize((w * f, h * f), Image.LANCZOS), (0, h * f + 6))
path = os.path.join(a.out, a.name + ".png"); sheet.save(path); print(path)
