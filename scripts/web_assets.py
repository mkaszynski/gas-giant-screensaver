#!/usr/bin/env python3
"""Stage native assets by resource group; no second browser asset definition."""
import argparse
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def manifest():
    groups = {"base": ["assets/giant.png"]}
    for name in ("terrain", "world"):
        groups[name] = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "assets" / name).rglob("*")
                              if p.is_file() and p.suffix in (".png", ".ggworld", ".ggrelief", ".ggflight", ".ggoptics"))
    groups["galaxy"] = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "assets/sky").glob("*.ggsky"))
    groups["stars"] = ["assets/sky/stars.bin"]
    return groups


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    groups = manifest()
    for paths in groups.values():
        for path in paths:
            target = args.output / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, target)
    # Serve the original credits/licenses alongside each asset, without
    # allocating them in the scene's virtual filesystem.
    for path in (ROOT / "assets").rglob("*"):
        if path.is_file() and path.suffix in (".md", ".txt", ".json"):
            target = args.output / path.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    (args.output / "assets.json").write_text(json.dumps(groups, indent=2) + "\n")
