#!/usr/bin/env python3
"""Summarize the measured Python chunk from an NVTX-segmented nsys trace."""

import argparse
import json
import subprocess
from pathlib import Path


PHASES = ("rollout", "gae", "update")


def report(trace, name, nvtx_filter=None):
    cmd = [
        "nsys",
        "stats",
        "--quiet",
        "--report",
        name,
        "--format",
        "json:nsec",
        "--output",
        "-",
    ]
    if nvtx_filter:
        cmd += ["--filter-nvtx", nvtx_filter]
    cmd.append(str(trace))
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def field(row, prefix):
    matches = [value for key, value in row.items() if key.startswith(prefix)]
    if len(matches) != 1:
        raise RuntimeError(f"expected one {prefix!r} field in {sorted(row)}")
    return matches[0]


def interval_union_ns(rows):
    intervals = sorted(
        (int(field(row, "Start")),
         int(field(row, "Start")) + int(field(row, "Duration")))
        for row in rows
    )
    total = 0
    end = None
    for start, stop in intervals:
        if end is None or start > end:
            total += stop - start
            end = stop
        elif stop > end:
            total += stop - end
            end = stop
    return total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    ranges = report(args.trace, "nvtx_pushpop_trace")
    projected_ranges = report(args.trace, "nvtx_gpu_proj_trace")
    summary = {
        "schema": "netherite.cgraph-ceiling.v1",
        "trace": str(args.trace),
        "method": (
            "third NVTX phase instance (two warmups, then measured chunk); "
            "GPU busy is the union of kernel/memop intervals from "
            "cuda_gpu_trace inside each CPU phase range"
        ),
        "phases": {},
    }
    for phase in PHASES:
        name = f"cgraph_ceiling/{phase}"
        # Nsight renders the unnamed/default NVTX domain as a leading colon.
        matching = [
            row for row in ranges if row.get("Name", "").lstrip(":") == name
        ]
        if len(matching) != 3:
            raise RuntimeError(f"{name}: expected 3 ranges, found {len(matching)}")
        phase_range = sorted(matching, key=lambda row: int(field(row, "Start")))[2]
        projected = [
            row for row in projected_ranges
            if row.get("Name", "").lstrip(":") == name
        ]
        if len(projected) != 3:
            raise RuntimeError(
                f"{name}: expected 3 projected ranges, found {len(projected)}"
            )
        projected_range = sorted(
            projected, key=lambda row: int(field(row, "Projected Start"))
        )[2]
        window_ns = int(field(projected_range, "Projected Duration"))
        cpu_window_ns = int(field(phase_range, "Duration"))
        operations = report(args.trace, "cuda_gpu_trace", f"{name}/2")
        busy_ns = interval_union_ns(operations)
        idle_ns = window_ns - busy_ns
        if idle_ns < 0:
            raise RuntimeError(f"{name}: GPU union exceeds CPU window")
        summary["phases"][phase] = {
            "projected_window_ms": window_ns / 1e6,
            "cpu_range_ms": cpu_window_ns / 1e6,
            "gpu_busy_ms": busy_ns / 1e6,
            "idle_ms": idle_ns / 1e6,
            "gpu_operations": len(operations),
        }

    graphable = summary["phases"]["rollout"]["idle_ms"] + \
        summary["phases"]["update"]["idle_ms"]
    total = sum(
        row["projected_window_ms"] for row in summary["phases"].values()
    )
    summary["graph_reachable_idle_ms"] = graphable
    summary["profiled_phase_window_ms"] = total
    summary["graph_reachable_idle_fraction"] = graphable / total
    summary["keep_bar_fraction"] = 0.02
    summary["proceed"] = summary["graph_reachable_idle_fraction"] >= 0.02

    encoded = json.dumps(summary, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
