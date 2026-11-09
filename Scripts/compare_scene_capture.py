#!/usr/bin/env python3
"""Compare opaque RGB8 Vulkan screenshot-layer captures in declared regions.

Use VK_SCREENSHOT_FORMAT=UNORM. Opaque alpha is implicitly 255, equivalent to
RGBA8 comparison. The contract permits at most 0.5% changed pixels per region,
where a change is any channel differing by more than 4/255. No automatic masks,
registration, rescaling or tolerance widening are performed. A newly captured
reference proves repeatability only, until a reviewer accepts its visual content.
"""

import argparse
import hashlib
import json
from pathlib import Path

MAX_PIXELS = 32 * 1024 * 1024
MAX_BYTES = MAX_PIXELS * 3 + 4096
MAX_LOG_BYTES = 16 * 1024 * 1024


def validate_capture_log(path):
    """Pixel equality cannot qualify a capture made after validation failure."""
    with Path(path).open("rb") as source:
        data = source.read(MAX_LOG_BYTES + 1)
    if not data or len(data) > MAX_LOG_BYTES:
        raise ValueError("missing or oversized capture validation log")
    text = data.decode("utf-8", errors="strict").lower()
    if any(marker in text for marker in ("[error]", "vuid-", "vk_not_ready", "validation error")):
        raise ValueError(f"capture validation errors in {path}")
    if "engine destroyed" not in text:
        raise ValueError("capture log does not show completed engine shutdown")


def read_ppm(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("capture exceeds the bounded image profile")
    position = 0

    def token():
        nonlocal position
        while position < len(data):
            if position >= 4096:
                raise ValueError("oversized PPM header")
            if data[position:position + 1] == b"#":
                end = data.find(b"\n", position)
                if end < 0:
                    raise ValueError("unterminated PPM header comment")
                position = end + 1
            elif data[position:position + 1].isspace():
                position += 1
            else:
                break
        start = position
        while position < min(len(data), 4097) and not data[position:position + 1].isspace() and data[position:position + 1] != b"#":
            position += 1
        if position == start or position > 4096:
            raise ValueError("invalid/oversized PPM header")
        return data[start:position]

    if token() != b"P6":
        raise ValueError("expected binary RGB P6 capture")
    try:
        width, height, maximum = int(token()), int(token()), int(token())
    except ValueError as error:
        raise ValueError("invalid PPM dimensions/channel range") from error
    if width < 1 or height < 1 or width * height > MAX_PIXELS or maximum != 255:
        raise ValueError("expected bounded, nonempty RGB8 capture")
    # Consume the header separator, never strip whitespace-valued pixel bytes.
    if position >= len(data) or not data[position:position + 1].isspace():
        raise ValueError("missing PPM raster separator")
    position += 2 if data[position:position + 2] == b"\r\n" else 1
    pixels = data[position:]
    if len(pixels) != width * height * 3:
        raise ValueError("truncated or overlong PPM raster")
    return width, height, pixels


def compare_region(reference, candidate, rectangle):
    width, height, left = reference
    if reference[:2] != candidate[:2]:
        raise ValueError("capture dimensions differ; rescaling is forbidden")
    x, y, extent_x, extent_y = rectangle
    if x < 0 or y < 0 or extent_x < 1 or extent_y < 1 or x + extent_x > width or y + extent_y > height:
        raise ValueError("comparison region is empty or outside the capture")
    right = candidate[2]
    changed = 0
    maximum = 0
    for row in range(y, y + extent_y):
        start = (row * width + x) * 3
        end = start + extent_x * 3
        a, b = left[start:end], right[start:end]
        if a == b:
            continue
        for offset in range(0, len(a), 3):
            delta = max(abs(a[offset + channel] - b[offset + channel]) for channel in range(3))
            maximum = max(maximum, delta)
            changed += delta > 4
    count = extent_x * extent_y
    return {"rectangle": list(rectangle), "pixels": count, "changed_pixels": changed,
            "changed_fraction": changed / count, "maximum_channel_delta": maximum,
            "passed": changed * 1000 <= count * 5}


def parse_region(text):
    try:
        name, coordinates = text.split(":", 1)
        rectangle = tuple(int(number) for number in coordinates.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected name:x,y,width,height") from error
    if not name or len(rectangle) != 4:
        raise argparse.ArgumentTypeError("expected name:x,y,width,height")
    return name, rectangle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--region", action="append", required=True, type=parse_region,
                        help="Predeclare viewport and important object/material regions: name:x,y,width,height")
    parser.add_argument("--validation-log", action="append", type=Path, default=[],
                        help="Check each completed capture process's engine log; required for qualification evidence")
    arguments = parser.parse_args()
    try:
        if len({name for name, _ in arguments.region}) != len(arguments.region):
            raise ValueError("duplicate region names")
        for path in arguments.validation_log:
            validate_capture_log(path)
        reference, candidate = read_ppm(arguments.reference), read_ppm(arguments.candidate)
        results = {name: compare_region(reference, candidate, rectangle) for name, rectangle in arguments.region}
        report = {"capture_space": "RGB8_UNORM; opaque alpha=255", "channel_tolerance": 4,
                  "maximum_changed_fraction": .005, "regions": results,
                  "width": reference[0], "height": reference[1],
                  "reference_pixel_sha256": hashlib.sha256(reference[2]).hexdigest(),
                  "candidate_pixel_sha256": hashlib.sha256(candidate[2]).hexdigest(),
                  "validation_logs_checked": [str(path) for path in arguments.validation_log],
                  "reference_review": "required; same-build repeatability is not an approved golden"}
        print(json.dumps(report, indent=2, allow_nan=False))
        return 0 if all(result["passed"] for result in results.values()) else 1
    except (ValueError, OSError) as error:
        parser.exit(2, f"invalid scene capture: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
