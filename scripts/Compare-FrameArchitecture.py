"""Compare matched hidden room benchmarks and their deterministic PNG captures.

Usage: python scripts/Compare-FrameArchitecture.py BEFORE AFTER --output report.json
Requires Pillow for decoded pixel comparison. No renderer process is launched.
"""

import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

from PIL import Image


PHASES = ("main", "room_no_shadows", "room_static_shadows", "room_moving_player")
TIMINGS = (
    "update_ms", "shadow_preparation_ms", "receiver_preparation_ms",
    "render_submission_ms", "wall_frame_ms", "gpu_ms", "backend_ms",
)
COUNTERS = ("draws", "shadow_draws", "faces", "cascades", "point_hits",
            "directional_hits", "scene_models", "game_models")


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[int((len(ordered) - 1) * fraction)]


def load(directory):
    summary = json.loads((directory / "room-summary.json").read_text(encoding="utf-8-sig"))
    rows = [[] for _ in PHASES]
    with (directory / "room-frames.csv").open(newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            rows[int(row["phase"])].append(row)
    result = {}
    for name, phase_rows in zip(PHASES, rows):
        if not phase_rows:
            raise ValueError(f"{directory}: phase {name} has no measured frames")
        timings = {}
        for metric in TIMINGS:
            values = [float(row[metric]) for row in phase_rows]
            timings[metric] = {"mean": statistics.mean(values), "p50": percentile(values, .5),
                               "p95": percentile(values, .95), "p99": percentile(values, .99)}
        result[name] = {"samples": len(phase_rows), "timings": timings,
                        "counters": {key: statistics.mean(float(row[key]) for row in phase_rows)
                                     for key in COUNTERS},
                        "memory": {key: summary[name][key] for key in
                                   ("shadow_bytes", "texture_bytes", "model_bytes")}}
    return summary, result


def compare(before_dir, after_dir):
    before_summary, before = load(before_dir)
    after_summary, after = load(after_dir)
    for key in ("viewport", "world_origin", "combined_models", "combined_points",
                "combined_directionals", "sample_count", "warmup_minimum"):
        if before_summary[key] != after_summary[key]:
            raise ValueError(f"Workload mismatch in {key}: {before_summary[key]} vs {after_summary[key]}")
    result = {"before": str(before_dir), "after": str(after_dir), "phases": {}, "images": [],
              "notes": ["GPU clocks and background desktop activity are uncontrolled.",
                        "Hidden Scene/Game submissions exclude full editor UI, physics, AI and animated crowds.",
                        "GPU query values are asynchronous phase samples; per-frame CPU/GPU rows are not synchronized.",
                        "PNG readbacks replay the final measured pose after timing; captures do not enter timing samples."]}
    for phase in PHASES:
        deltas = {}
        for metric in TIMINGS:
            a, b = before[phase]["timings"][metric], after[phase]["timings"][metric]
            deltas[metric] = {stat: {"absolute_ms": b[stat] - a[stat],
                                    "percent": (b[stat] / a[stat] - 1) * 100 if a[stat] else None}
                              for stat in a}
        result["phases"][phase] = {"before": before[phase], "after": after[phase], "timing_deltas": deltas}
        for view in ("scene", "game"):
            name = f"{phase}-{view}.png"
            a_path, b_path = before_dir / name, after_dir / name
            if not a_path.is_file() or not b_path.is_file():
                result["images"].append({"name": name, "available": False})
                continue
            with Image.open(a_path) as a_image, Image.open(b_path) as b_image:
                if a_image.size != b_image.size:
                    raise ValueError(f"Image dimensions differ: {name}")
                a, b = a_image.convert("RGBA").tobytes(), b_image.convert("RGBA").tobytes()
                differences = [abs(x - y) for x, y in zip(a, b)]
                changed_pixels = sum(any(differences[index:index + 4]) for index in range(0, len(a), 4))
                result["images"].append({"name": name, "available": True, "size": a_image.size,
                    "identical_rgba": a == b, "changed_pixels": changed_pixels,
                    "changed_pixels_percent": changed_pixels * 400 / len(a),
                    "maximum_channel_difference": max(differences),
                    "mean_absolute_channel_difference": statistics.mean(differences),
                    "before_rgba_sha256": hashlib.sha256(a).hexdigest(),
                    "after_rgba_sha256": hashlib.sha256(b).hexdigest()})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.before, args.after)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for phase, data in result["phases"].items():
        wall_a, wall_b = (data[key]["timings"]["wall_frame_ms"]["mean"] for key in ("before", "after"))
        gpu_a, gpu_b = (data[key]["timings"]["gpu_ms"]["mean"] for key in ("before", "after"))
        print(f"{phase}: wall {wall_a:.4f} -> {wall_b:.4f} ms; GPU {gpu_a:.4f} -> {gpu_b:.4f} ms")
    available = [entry for entry in result["images"] if entry["available"]]
    print(f"Images: {sum(entry['identical_rgba'] for entry in available)}/{len(available)} identical RGBA; "
          f"{len(result['images']) - len(available)} missing pairs")


if __name__ == "__main__":
    main()
