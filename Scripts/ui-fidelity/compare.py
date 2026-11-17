#!/usr/bin/env python3
"""Compare a captured editor window against the Figma reference frame.

  compare.py <capture.png> <out-dir> [--reference reference/editor-3_2503.png]

The window is cropped below the macOS title bar, scaled to logical pixels, and compared with the
1600x1000 Figma frame in regions anchored to the nearest window corner (the editor's chrome is
fixed-size, so anchored regions line up regardless of window size). Per region it prints the mean
absolute colour error (0-255, lower is better) and the share of pixels off by more than 24.
Side-by-side images (reference | capture | difference) are written to <out-dir>.
"""
import argparse, json, os, sys
import numpy as np
from PIL import Image

# name, anchor, (x0, y0, x1, y1) in Figma frame coordinates (1600x1000)
REGIONS = [
    ("menu-bar",        "tl", (0,    0,   620,  28)),
    ("level-tabs",      "tl", (0,    28,  620,  58)),
    ("main-toolbar",    "tl", (0,    58,  900,  100)),
    ("outliner",        "tr", (1217, 100, 1600, 400)),
    ("details-header",  "tr", (1217, 400, 1600, 640)),
    ("content-browser", "bl", (0,    684, 900,  974)),
    ("status-bar",      "bl", (0,    974, 900,  1000)),
]
FIGMA_W, FIGMA_H = 1600, 1000

def content_top(img):
    """First row (below the title bar) whose centre pixels are dark UI, not the white title bar."""
    a = np.asarray(img.convert("L"), dtype=np.int32)
    col = a[:, a.shape[1] // 2 - 40: a.shape[1] // 2 + 40].mean(axis=1)
    for y in range(len(col)):
        if col[y] < 90:
            return y
    return 0

def crop_region(img, anchor, rect, w, h):
    x0, y0, x1, y1 = rect
    if anchor[1] == "r": x0, x1 = x0 - FIGMA_W + w, x1 - FIGMA_W + w
    if anchor[0] == "b": y0, y1 = y0 - FIGMA_H + h, y1 - FIGMA_H + h
    x0, y0 = max(0, x0), max(0, y0)
    return img.crop((x0, y0, min(w, x1), min(h, y1)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture"); ap.add_argument("out")
    ap.add_argument("--reference", default=os.path.join(os.path.dirname(__file__), "reference", "editor-3_2503.png"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    info = json.load(open(args.capture + ".json"))[0]
    cap = Image.open(args.capture).convert("RGB")
    scale = cap.width / info["w"]
    top = content_top(cap)
    cap = cap.crop((0, top, cap.width, cap.height))
    lw, lh = round(cap.width / scale), round(cap.height / scale)
    cap = cap.resize((lw, lh), Image.LANCZOS)
    ref = Image.open(args.reference).convert("RGB")
    cap.save(os.path.join(args.out, "capture-logical.png"))
    print(f"window content {lw}x{lh} logical (scale {scale:g}, title bar {round(top/scale)}px)")

    rows, total = [], 0.0
    for name, anchor, rect in REGIONS:
        r = crop_region(ref, anchor, rect, FIGMA_W, FIGMA_H)
        c = crop_region(cap, anchor, rect, lw, lh)
        w, h = min(r.width, c.width), min(r.height, c.height)
        r, c = r.crop((0, 0, w, h)), c.crop((0, 0, w, h))
        d = np.abs(np.asarray(r, dtype=np.int32) - np.asarray(c, dtype=np.int32))
        mae = float(d.mean()); bad = float((d.max(axis=2) > 24).mean() * 100)
        total += mae; rows.append((name, mae, bad))
        pad = 6
        sheet = Image.new("RGB", (w * 3 + pad * 2, h), (255, 0, 255))
        sheet.paste(r, (0, 0)); sheet.paste(c, (w + pad, 0))
        sheet.paste(Image.fromarray(np.clip(d * 3, 0, 255).astype("uint8")), (2 * (w + pad), 0))
        sheet.save(os.path.join(args.out, f"region-{name}.png"))
    print(f"{'region':<18}{'MAE':>8}{'off>24 %':>11}")
    for n, m, b in rows: print(f"{n:<18}{m:>8.1f}{b:>11.1f}")
    print(f"{'mean':<18}{total/len(rows):>8.1f}")

if __name__ == "__main__":
    sys.exit(main())
