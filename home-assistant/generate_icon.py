#!/usr/bin/env python3
"""Render our deliberately simple SVG master to PNG using only the stdlib."""

from __future__ import annotations

import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

SIZE = 256
SCALE = 3


def _inside(x: float, y: float, polygon: list[tuple[float, float]]) -> bool:
    inside = False
    previous_x, previous_y = polygon[-1]
    for next_x, next_y in polygon:
        if (next_y > y) != (previous_y > y) and x < (
            (previous_x - next_x) * (y - next_y) / (previous_y - next_y) + next_x
        ):
            inside = not inside
        previous_x, previous_y = next_x, next_y
    return inside


def render(master: Path) -> bytes:
    """Accept only the polygons and rectangles used by this original artwork."""
    root = ET.parse(master).getroot()
    if root.attrib != {"width": "256", "height": "256", "viewBox": "0 0 256 256"}:
        raise ValueError("Expected the 256 x 256 SVG master without transformations")
    shapes = []
    for element in root:
        tag = element.tag.rsplit("}", 1)[-1]
        if tag == "title":
            continue
        color = bytes.fromhex(element.attrib["fill"].removeprefix("#"))
        if len(color) != 3:
            raise ValueError("Expected a six-digit RGB fill")
        if tag == "polygon" and set(element.attrib) == {"fill", "points"}:
            points = []
            for point in element.attrib["points"].split():
                x, y = point.split(",")
                points.append((float(x), float(y)))
        elif tag == "rect" and set(element.attrib) == {"fill", "x", "y", "width", "height"}:
            x, y = float(element.attrib["x"]), float(element.attrib["y"])
            width, height = float(element.attrib["width"]), float(element.attrib["height"])
            points = [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
        else:
            raise ValueError(f"Unsupported SVG shape or attributes: {tag}")
        shapes.append((points, color))
    rows = bytearray()
    for y in range(SIZE):
        rows.append(0)
        for x in range(SIZE):
            samples = []
            for sub_y in range(SCALE):
                for sub_x in range(SCALE):
                    sample_x = x + (sub_x + 0.5) / SCALE
                    sample_y = y + (sub_y + 0.5) / SCALE
                    for points, color in reversed(shapes):
                        if _inside(sample_x, sample_y, points):
                            samples.append(color)
                            break
            if samples:
                rows.extend(
                    round(sum(color[c] for color in samples) / len(samples)) for c in range(3)
                )
                rows.append(round(255 * len(samples) / (SCALE * SCALE)))
            else:
                rows.extend(b"\0\0\0\0")

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, level=9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    destination = root / "custom_components/mfi/brand/icon.png"
    for path in (
        root / "custom_components",
        root / "custom_components/mfi",
        destination.parent,
    ):
        if path.is_symlink():
            raise ValueError(f"Refusing symlink directory: {path}")
        path.mkdir(exist_ok=True)
    if destination.is_symlink():
        raise ValueError(f"Refusing symlink destination: {destination}")
    image = render(root / "home-assistant/assets/icon.svg")
    if destination.exists():
        if destination.read_bytes() != image:
            raise ValueError(
                "Existing icon differs; review and remove that file before regenerating"
            )
    else:
        with destination.open("xb") as stream:
            stream.write(image)
    print(f"Original icon: {destination}")


if __name__ == "__main__":
    main()
