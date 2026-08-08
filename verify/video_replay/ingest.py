#!/usr/bin/env python3
"""Create a reviewable video-replay workspace from a local recording."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


SCHEMA = 1


def run_json(command):
    proc = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(proc.stdout)


def probe_video(path):
    data = run_json([
        "ffprobe", "-v", "error", "-show_streams", "-show_format",
        "-of", "json", str(path),
    ])
    videos = [s for s in data.get("streams", [])
              if s.get("codec_type") == "video"]
    if not videos:
        raise ValueError(f"no video stream: {path}")
    stream = videos[0]
    fps_text = stream.get("avg_frame_rate") or stream.get("r_frame_rate")
    num, den = (int(v) for v in fps_text.split("/"))
    if den == 0:
        raise ValueError(f"invalid frame rate {fps_text!r}")
    duration = float(data["format"]["duration"])
    return {
        "duration_s": duration,
        "fps_num": num,
        "fps_den": den,
        "width": int(stream["width"]),
        "height": int(stream["height"]),
        "video_codec": stream.get("codec_name"),
    }


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def init_workspace(args):
    video = pathlib.Path(args.video).expanduser().resolve()
    if not video.is_file():
        raise ValueError(f"video does not exist: {video}")
    out = pathlib.Path(args.out).expanduser().resolve()
    if out.exists() and any(out.iterdir()) and not args.force:
        raise ValueError(f"workspace is not empty: {out} (use --force)")
    out.mkdir(parents=True, exist_ok=True)
    frames_dir = out / "frames"
    frames_dir.mkdir(exist_ok=True)

    probe = probe_video(video)
    manifest = {
        "schema": SCHEMA,
        "run_id": args.run_id,
        "source": {
            "path": str(video),
            "sha256": sha256(video),
            **probe,
        },
        "game": {
            "edition": "java",
            "version": args.version,
            "seed": int(args.seed),
            "difficulty": args.difficulty,
            "mod_status": args.mod_status,
        },
        "timing": {
            "video_duration_s": probe["duration_s"],
            "rta_s": args.rta,
            "igt_s": args.igt,
            "video_to_game_anchors": [],
        },
        "extraction": {"target_interval_s": args.interval},
        "terminal": args.terminal,
    }
    write_json(out / "manifest.json", manifest)

    pattern = frames_dir / "target_%06d.jpg"
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i",
        str(video), "-vf", f"fps=1/{args.interval}:start_time=0",
        "-q:v", "3", str(pattern),
    ], check=True)
    frames = sorted(frames_dir.glob("target_*.jpg"))
    with (out / "targets.jsonl").open("w", encoding="utf-8") as stream:
        header = {"schema": SCHEMA, "kind": "header",
                  "run_id": args.run_id}
        stream.write(json.dumps(header, sort_keys=True) + "\n")
        fps = probe["fps_num"] / probe["fps_den"]
        for index, frame_path in enumerate(frames):
            video_s = index * args.interval
            target = {
                "kind": "target", "id": index, "video_s": video_s,
                "frame": round(video_s * fps), "game_tick": None,
                "clock": "unknown",
                "image": str(frame_path.relative_to(out)),
                "hard": {}, "soft": {}, "events": [], "confidence": {},
                "status": "unreviewed",
            }
            stream.write(json.dumps(target, sort_keys=True) + "\n")
    for name in ("segments.jsonl", "actions.jsonl", "divergences.jsonl"):
        (out / name).touch()
    print(json.dumps({"workspace": str(out), "targets": len(frames),
                      "duration_s": probe["duration_s"]}, sort_keys=True))


def parser():
    root = argparse.ArgumentParser()
    sub = root.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init")
    init.add_argument("--video", required=True)
    init.add_argument("--out", required=True)
    init.add_argument("--run-id", required=True)
    init.add_argument("--seed", required=True, type=int)
    init.add_argument("--version", default="1.11.2")
    init.add_argument("--difficulty", default="unknown")
    init.add_argument("--mod-status", choices=("vanilla", "modded", "unknown"),
                      default="unknown")
    init.add_argument("--igt", type=float)
    init.add_argument("--rta", type=float)
    init.add_argument("--interval", type=float, default=5.0)
    init.add_argument("--terminal", default="ender_dragon_defeated")
    init.add_argument("--force", action="store_true")
    return root


def main():
    args = parser().parse_args()
    try:
        if args.command == "init":
            if args.interval <= 0:
                raise ValueError("--interval must be positive")
            init_workspace(args)
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"video ingest: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
