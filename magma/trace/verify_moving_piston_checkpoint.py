#!/usr/bin/env python3
"""Verify magma continuation from a Java mid-motion piston checkpoint."""

import argparse
import json
import os
import pathlib
import subprocess
import sys

import state_capsule


HERE = pathlib.Path(__file__).resolve().parent


def read_rows(path: pathlib.Path) -> list[dict]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", required=True, type=pathlib.Path)
    parser.add_argument("--progress", required=True, type=pathlib.Path)
    parser.add_argument("--settled", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.start = args.start.resolve()
    args.progress = args.progress.resolve()
    args.settled = args.settled.resolve()
    args.out = args.out.resolve()

    start_rows = read_rows(args.start / "java_state.jsonl")
    progress_rows = read_rows(args.progress / "java_state.jsonl")
    settled_rows = read_rows(args.settled / "java_state.jsonl")
    if len(start_rows) != 1 or len(progress_rows) < 2 \
            or len(settled_rows) < 3:
        raise SystemExit("oracle cases do not contain the required 1/2/3 ticks")
    checkpoint = start_rows[0]
    if checkpoint.get("moving_pistons_complete") is not True \
            or not checkpoint.get("moving_pistons"):
        raise SystemExit("start case did not capture complete moving-piston state")

    initial_manifest = json.loads(
        (args.start / "state_capsule" / "manifest.json").read_text(
            encoding="utf-8"))
    box = initial_manifest["blocks"]["box"]
    args.out.mkdir(parents=True, exist_ok=True)
    checkpoint_path = args.out / "checkpoint_state.json"
    checkpoint_path.write_text(
        json.dumps(checkpoint, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    capsule = args.out / "capsule"
    state_capsule.create_capsule(
        checkpoint_path,
        args.start / "java_blocks.bin",
        box,
        capsule,
        seed=0,
        source_engine="minecraft-java",
        source_version="1.11.2",
    )

    command = [
        sys.executable,
        str(HERE / "trace_runtime.py"),
        "--tape", str(args.progress / "tape.txt"),
        "--spawn-file", str(args.start / "c_spawn.txt"),
        "--seed", "0",
        "--platform", "21",
        "--world-time", "6000",
        "--capsule", str(capsule),
        "--script-out", str(args.out / "resume_script.jsonl"),
        "--raw-state", str(args.out / "resume_raw.jsonl"),
        "--state", str(args.out / "resume_state.jsonl"),
        "--blocks-out", str(args.out / "resume_blocks.bin"),
        "--blocks-box", *(str(value) for value in box),
        "--skip-build",
    ]
    subprocess.run(command, cwd=HERE.parent, env=os.environ.copy(), check=True)

    resumed = read_rows(args.out / "resume_state.jsonl")
    checks = {
        "checkpoint_tiles_present": len(checkpoint["moving_pistons"]) == 2,
        "next_half_step": (
            len(resumed) >= 1
            and resumed[0].get("moving_pistons")
                == progress_rows[1].get("moving_pistons")
        ),
        "settled_tile_state": (
            len(resumed) >= 2
            and resumed[1].get("moving_pistons")
                == settled_rows[2].get("moving_pistons")
        ),
        "settled_blocks": (
            (args.out / "resume_blocks.bin").read_bytes()
                == (args.settled / "java_blocks.bin").read_bytes()
        ),
    }
    payload = {
        "status": "pass" if all(checks.values()) else "fail",
        "checks": checks,
        "box": box,
        "checkpoint_tiles": checkpoint["moving_pistons"],
        "expected_next_tiles": progress_rows[1].get("moving_pistons"),
        "actual_next_tiles": (
            resumed[0].get("moving_pistons") if resumed else None),
    }
    (args.out / "summary.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    lines = [
        "# Moving-piston checkpoint regression",
        "",
        f"Status: {payload['status']}",
        "",
        "| Check | Result |",
        "|---|---|",
        *(f"| {name} | {'pass' if passed else 'FAIL'} |"
          for name, passed in checks.items()),
        "",
    ]
    (args.out / "summary.md").write_text(
        "\n".join(lines), encoding="utf-8")
    print(f"moving-piston checkpoint: {payload['status']} -> {args.out}")
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
