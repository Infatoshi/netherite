#!/usr/bin/env python3
"""Write or check the pinned capture/window compositor residual baseline."""

import argparse
import json
from pathlib import Path

DEFAULT_BASELINE = Path(__file__).with_name("report") / "window_compose_baseline.json"


def measurement(path):
    gate = json.loads(Path(path).read_text())
    classes = gate.get("classes", {})
    unexplained = classes.get("UNEXPLAINED", {})
    coverage = gate.get("state", {}).get("coverage", {})
    return {
        "frames_checked": int(gate.get("frames_checked", 0)),
        "state_rows": int(coverage.get("ticks_run", 0)),
        "unexplained_frames": int(unexplained.get("frames", 0)),
        "unexplained_px": int(unexplained.get("px", 0)),
        # Every classified connected-component pixel, including accepted
        # semantic classes. This remains sensitive when the clean control has
        # no threshold-failing UNEXPLAINED cluster.
        "total_residual_px": sum(
            int(summary.get("px", 0)) for summary in classes.values()
        ),
        "failed_frames": len(gate.get("failed_frames", [])),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--tape", default="scenario_smoke_zombie_20260722T081735Z")
    parser.add_argument("--capture-gate", type=Path)
    parser.add_argument("--window-gate", type=Path)
    parser.add_argument("--compose", choices=("capture", "window"))
    parser.add_argument("--gate", type=Path)
    args = parser.parse_args()

    if args.write:
        if not args.capture_gate or not args.window_gate:
            parser.error("--write requires --capture-gate and --window-gate")
        data = {
            "version": 1,
            "tape": args.tape,
            "metric": (
                "pixel_gate connected-component pixels at DIFF_THRESH; "
                "total includes accepted semantic classes"
            ),
            "capture": measurement(args.capture_gate),
            "window": measurement(args.window_gate),
        }
        if data["capture"]["frames_checked"] <= 0 or data["window"]["frames_checked"] <= 0:
            raise SystemExit("refusing zero-frame baseline")
        if data["capture"]["state_rows"] <= 0 or data["window"]["state_rows"] <= 0:
            raise SystemExit("refusing zero-state-row baseline")
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_text(json.dumps(data, indent=2) + "\n")
        print(f"baseline written: {args.baseline}")
        print(json.dumps(data, indent=2))
        return 0

    if not args.compose or not args.gate:
        parser.error("checking requires --compose and --gate")
    baseline = json.loads(args.baseline.read_text())
    expected = baseline[args.compose]
    actual = measurement(args.gate)
    print(f"{args.compose} expected={expected}")
    print(f"{args.compose} actual={actual}")
    if actual != expected:
        delta = {
            key: actual[key] - expected[key]
            for key in expected
            if isinstance(expected[key], int) and key in actual
        }
        print(f"NEW RESIDUAL: delta={delta}")
        return 1
    print(f"{args.compose} baseline PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
