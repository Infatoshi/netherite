#!/usr/bin/env python3
"""Run the integrated native runtime in isolated parallel soak lanes."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import re
import subprocess
import time


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_BINARY = ROOT / "magma" / "game" / "test_runtime"
DEFAULT_WORK = ROOT / ".tmp" / "native_soak"
DEFAULT_RECEIPT = ROOT / ".tmp" / "native_soak_receipt.json"


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def time_value(text: str, label: str) -> int:
    match = re.search(
        rf"^\s*{re.escape(label)}:\s*(\d+)\s*$", text, re.M)
    if not match:
        raise RuntimeError(f"resource report lost {label!r}")
    return int(match.group(1))


def run_lane(binary: pathlib.Path, work: pathlib.Path,
             lane: int, core: int) -> dict[str, object]:
    lane_dir = work / f"lane_{lane:02d}"
    lane_dir.mkdir(parents=True, exist_ok=True)
    output_path = lane_dir / "output.log"
    resource_path = lane_dir / "resource.txt"
    environment = os.environ.copy()
    environment.update({
        "MAGMA_ITEM_SPAWN_LIMIT": "256",
        "TMPDIR": str(lane_dir),
    })
    started = time.monotonic()
    with output_path.open("wb") as output:
        result = subprocess.run([
            "taskset", "-c", str(core), "nice", "-n", "10",
            "/usr/bin/time", "-v", "-o", str(resource_path),
            str(binary),
        ], cwd=lane_dir, env=environment, stdout=output,
           stderr=subprocess.STDOUT, check=False)
    elapsed = time.monotonic() - started
    output_text = output_path.read_text(encoding="utf-8", errors="replace")
    resource_text = resource_path.read_text(encoding="utf-8", errors="replace")
    return {
        "lane": lane,
        "core": core,
        "exit_code": result.returncode,
        "elapsed_seconds": round(elapsed, 3),
        "runtime_pass": output_text.rstrip().endswith("runtime: PASS"),
        "output_sha256": sha256(output_path),
        "peak_rss_kib": time_value(
            resource_text, "Maximum resident set size (kbytes)"),
        "major_faults": time_value(
            resource_text, "Major (requiring I/O) page faults"),
        "swaps": time_value(resource_text, "Swaps"),
        "voluntary_context_switches": time_value(
            resource_text, "Voluntary context switches"),
        "involuntary_context_switches": time_value(
            resource_text, "Involuntary context switches"),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lanes", type=int, default=32)
    parser.add_argument("--first-core", type=int, default=0)
    parser.add_argument("--binary", type=pathlib.Path, default=DEFAULT_BINARY)
    parser.add_argument("--work", type=pathlib.Path, default=DEFAULT_WORK)
    parser.add_argument("--receipt", type=pathlib.Path, default=DEFAULT_RECEIPT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    binary = args.binary.resolve()
    work = args.work.resolve()
    receipt = args.receipt.resolve()
    available = sorted(os.sched_getaffinity(0))
    selected = [core for core in available if core >= args.first_core]
    if args.lanes < 1 or len(selected) < args.lanes:
        raise RuntimeError(
            f"requested {args.lanes} lanes but only {len(selected)} cores fit")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"missing executable {binary}")
    work.mkdir(parents=True, exist_ok=True)
    started_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(
            max_workers=args.lanes) as executor:
        futures = [executor.submit(
            run_lane, binary, work, lane, selected[lane])
            for lane in range(args.lanes)]
        lanes = [future.result() for future in futures]
    wall_seconds = time.monotonic() - started
    lanes.sort(key=lambda lane: int(lane["lane"]))
    payload = {
        "schema": "netherite.native_parallel_soak",
        "version": 1,
        "started_utc": started_utc,
        "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "binary": str(binary.relative_to(ROOT)),
        "binary_sha256": sha256(binary),
        "lane_count": len(lanes),
        "wall_seconds": round(wall_seconds, 3),
        "client_seconds": round(sum(
            float(lane["elapsed_seconds"]) for lane in lanes), 3),
        "aggregate_peak_rss_kib": sum(
            int(lane["peak_rss_kib"]) for lane in lanes),
        "lanes": lanes,
    }
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    passed = all(lane["exit_code"] == 0 and lane["runtime_pass"]
                 for lane in lanes)
    print(f"{'PASS' if passed else 'FAIL'} native parallel soak: "
          f"{len(lanes)} lanes, {payload['client_seconds'] / 3600:.2f} "
          f"client-hours, {payload['aggregate_peak_rss_kib'] / 1048576:.2f} "
          f"GiB summed peak RSS, {wall_seconds:.1f}s wall")
    print(f"receipt: {receipt}")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL native parallel soak: {error}")
        raise SystemExit(1)
