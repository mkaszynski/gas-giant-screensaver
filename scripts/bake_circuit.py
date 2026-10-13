"""Run the offline route planner and record its world-bound asset checksums."""

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path("build/circuit-baker"))
    parser.add_argument("--output", type=Path, default=Path("assets/world"))
    args = parser.parse_args()
    subprocess.run([str(args.executable.resolve()), str(args.output)], check=True)
    cache = (args.output / "circuit.ggroute").read_bytes()
    magic, count = struct.unpack("<8sI", cache[:12])
    if magic != b"GGROUTE1" or len(cache) != 12 + count * 4:
        raise RuntimeError("Planner produced an invalid circuit cache")
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["circuit"] = {
        "format": "GGROUTE1",
        "points": count,
        "file_sha256": hashlib.sha256(cache).hexdigest(),
        "world_sha256": hashlib.sha256(
            (args.output / "moon.ggworld").read_bytes()
        ).hexdigest(),
        "relief_sha256": hashlib.sha256(
            (args.output / "relief.ggrelief").read_bytes()
        ).hexdigest(),
        "nominal_subplanet_angle_degrees": 59.5,
        "maximum_detour_m": 4992,
        "horizon_limit_degrees": 28,
        "horizon_bearing": "mean subplanet direction",
        "validation_samples": count * 2,
    }
    temporary = manifest_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(manifest_path)


if __name__ == "__main__":
    main()
