"""Export lossless APNGs from actual-scene capture reports at recorded timing.

Requires Pillow. APNG preserves small lighting fluctuations without GIF palette
quantization, video compression or frame interpolation. No screenshot pixels
are changed except an optional explicitly selected crop.
"""

import argparse
import hashlib
import json
from pathlib import Path
import statistics

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase", default="static")
    parser.add_argument("--settings", choices=("saved", "defaults", "both"), default="both")
    parser.add_argument("--channel", choices=("lit", "indirect", "both"), default="both")
    parser.add_argument("--crop", help="Optional x0,y0,x1,y1 rectangle, in capture pixels")
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    crop = tuple(map(int, args.crop.split(","))) if args.crop else None
    if crop and (len(crop) != 4 or crop[0] >= crop[2] or crop[1] >= crop[3]):
        parser.error("Crop must use x0,y0,x1,y1 with positive extents")
    args.output.mkdir(parents=True, exist_ok=True)
    for case in report["cases"]:
        if args.settings != "both" and case["settings"] != args.settings:
            continue
        if args.channel != "both" and case["channel"] != args.channel:
            continue
        selected = [frame for frame in case["frames"] if frame["phase"] == args.phase]
        if len(selected) < 2:
            continue
        frame_numbers = [frame["frame_before_draw"] for frame in selected]
        durations = [(b - a) * 1000 / 60 for a, b in zip(frame_numbers, frame_numbers[1:])]
        if any(duration <= 0 for duration in durations):
            raise RuntimeError("Capture frame numbers must increase")
        durations.append(statistics.median(durations))
        images = []
        for frame in selected:
            with Image.open(args.report.parent / frame["image"]) as source:
                image = source.convert("RGB")
                images.append(image.crop(crop) if crop else image)
        suffix = "-crop" if crop else ""
        target = args.output / f"{case['settings']}-{case['channel']}-{args.phase}{suffix}.png"
        images[0].save(target, format="PNG", save_all=True, append_images=images[1:],
                       duration=durations, loop=0, disposal=0, blend=0)
        with Image.open(target) as encoded:
            encoded_frames = encoded.n_frames
            encoded_index = 0
            encoded.seek(0)
            encoded_end = float(encoded.info.get("duration", sum(durations)))
            encoded_pixels = encoded.convert("RGB").tobytes()
            source_time = 0.0
            for index, expected in enumerate(images):
                # Pillow may merge identical consecutive screenshots into one
                # longer frame. Verify playback at each original frame's
                # midpoint, including a genuinely noise-free stationary control.
                midpoint = source_time + durations[index] * .5
                while midpoint >= encoded_end and encoded_index + 1 < encoded_frames:
                    encoded_index += 1
                    encoded.seek(encoded_index)
                    encoded_end += float(encoded.info["duration"])
                    encoded_pixels = encoded.convert("RGB").tobytes()
                if encoded_pixels != expected.tobytes():
                    raise RuntimeError(f"APNG changed screenshot pixels in frame {index}")
                source_time += durations[index]
            if abs(encoded_end - sum(durations)) > 1:
                raise RuntimeError("APNG changed capture sequence duration")
        metadata = {
            "source_report": str(args.report.resolve()), "phase": args.phase,
            "settings": case["settings"], "channel": case["channel"],
            "captures": len(selected), "encoded_frames": encoded_frames, "duration_ms": sum(durations),
            "frame_durations_ms": durations, "source_frame_numbers": frame_numbers,
            "crop": crop, "timing": "Actual submitted-frame gaps at a 60 Hz diagnostic trajectory",
            "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
            "lossless_pixel_verification": True,
        }
        target.with_suffix(".animation.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(json.dumps({"animation": str(target), "frames": len(images),
                          "duration_ms": sum(durations), "pixel_verified": True}))


if __name__ == "__main__":
    main()
