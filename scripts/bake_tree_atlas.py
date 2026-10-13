#!/usr/bin/env python3
"""Numerically draw cached conifer/deciduous foliage, without photographic inputs."""

import argparse
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt
from pathlib import Path


def build_tree_atlas():
    rng = np.random.default_rng(7218)
    im = Image.new("RGBA", (4096, 1024))
    draw = ImageDraw.Draw(im)
    for variant in range(8):
        kind = variant // 4
        cx = 256 + variant * 512
        draw.line(
            [(cx, 1023), (cx - 5, 520), (cx + 3, 140 if not kind else 250)],
            fill=(64, 58, 43, 255),
            width=10,
        )
        if kind == 0:
            for y in np.linspace(90, 880, 85):
                t = (y - 90) / 800
                span = 8 + 185 * t**0.85
                for side in (-1, 1):
                    end = (
                        cx + side * span * rng.uniform(0.7, 1.05),
                        y + rng.uniform(12, 35),
                    )
                    draw.line([(cx, y), end], fill=(59, 70, 43, 255), width=3)
                    for j in range(int(span * 1.1)):
                        u = rng.random()
                        x = cx + (end[0] - cx) * u
                        v = y + (end[1] - y) * u
                        length = rng.uniform(3, 16) * (1 - u * 0.4)
                        shade = int(rng.uniform(48, 100))
                        draw.line(
                            [(x, v), (x + side * length * 0.7, v - length)],
                            fill=(int(shade * 0.65), shade, int(shade * 0.43), 255),
                            width=int(rng.integers(1, 4)),
                        )
        else:
            for branch in range(18):
                y = rng.uniform(230, 690)
                ex = cx + rng.uniform(-170, 170)
                ey = y - rng.uniform(20, 140)
                draw.line([(cx, y + 120), (ex, ey)], fill=(68, 61, 43, 255), width=5)
            for j in range(2700):
                x = rng.normal(cx, 85)
                y = rng.normal(430, 145)
                if ((x - cx) / 195) ** 2 + ((y - 390) / 305) ** 2 > 1:
                    continue
                radius = rng.uniform(3, 14)
                shade = int(rng.uniform(52, 117))
                draw.ellipse(
                    (x - radius, y - radius, x + radius, y + radius),
                    fill=(int(shade * 0.55), shade, int(shade * 0.35), 255),
                )
    im = im.resize((2048, 512), Image.Resampling.LANCZOS)
    # Transparent texels contain neighboring color to avoid dark filtered fringes.
    p = np.asarray(im).copy()
    _, idx = distance_transform_edt(p[:, :, 3] == 0, return_indices=True)
    p[:, :, :3] = p[idx[0], idx[1], :3]
    return p


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, default=Path("assets/world/trees.png"))
    a = p.parse_args()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(build_tree_atlas()).save(a.output)


if __name__ == "__main__":
    main()
