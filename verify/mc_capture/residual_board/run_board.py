#!/usr/bin/env python3
"""Run residual board harnesses. Capture once, verify many.

Usage (repo root):
  uv run --no-project --with numpy --with pillow --with scipy \\
    python verify/mc_capture/residual_board/run_board.py
  ... run_board.py --only leaf_atlas_identity,face_shade_table
  ... run_board.py --open-only
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

BOARD_DIR = Path(__file__).resolve().parent
# Path: <repo>/verify/mc_capture/residual_board
for p in BOARD_DIR.parents:
    if (p / "magma").is_dir() and (p / "java").is_dir():
        REPO = p
        break

BOARD_JSON = BOARD_DIR / "board.json"
RESULTS_DIR = Path("/tmp/hard_scene_agents/board_results")


def run_harness(row: dict) -> dict:
    harness = BOARD_DIR / row["harness"]
    if not harness.is_file():
        return {
            "id": row["id"],
            "status": "FAIL",
            "reason": f"missing harness {harness}",
            "seconds": 0,
        }
    t0 = time.time()
    env = dict(**{k: v for k, v in __import__("os").environ.items()})
    env["PYTHONPATH"] = str(BOARD_DIR / "rows") + (
        (":" + env["PYTHONPATH"]) if env.get("PYTHONPATH") else ""
    )
    r = subprocess.run(
        [sys.executable, str(harness), "--board", str(BOARD_JSON), "--repo", str(REPO)],
        cwd=str(REPO),
        capture_output=True,
        text=True,
        env=env,
    )
    dt = time.time() - t0
    out = (r.stdout or "") + (r.stderr or "")
    # harness prints JSON last line or full stdout JSON
    status = "PASS" if r.returncode == 0 else "FAIL"
    detail = out.strip().splitlines()[-1] if out.strip() else f"exit {r.returncode}"
    try:
        # prefer last JSON object in stdout
        for line in reversed(out.splitlines()):
            line = line.strip()
            if line.startswith("{"):
                detail = json.loads(line)
                break
    except Exception:
        pass
    return {
        "id": row["id"],
        "title": row.get("title"),
        "board_status": row.get("status"),
        "status": status,
        "detail": detail,
        "seconds": round(dt, 3),
        "returncode": r.returncode,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="", help="comma ids")
    ap.add_argument("--open-only", action="store_true")
    ap.add_argument("--closed-only", action="store_true")
    args = ap.parse_args()
    board = json.loads(BOARD_JSON.read_text())
    rows = board["rows"]
    if args.only:
        want = set(args.only.split(","))
        rows = [r for r in rows if r["id"] in want]
    if args.open_only:
        rows = [r for r in rows if r.get("status") == "open"]
    if args.closed_only:
        rows = [r for r in rows if r.get("status") == "closed"]

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    results = []
    print(f"REPO={REPO}")
    print(f"running {len(rows)} rows...")
    for row in rows:
        print(f"  -> {row['id']} ...", flush=True)
        res = run_harness(row)
        results.append(res)
        print(f"     {res['status']} ({res['seconds']}s)", flush=True)
        (RESULTS_DIR / f"{row['id']}.json").write_text(json.dumps(res, indent=2))

    summary = {
        "n": len(results),
        "pass": sum(1 for r in results if r["status"] == "PASS"),
        "fail": sum(1 for r in results if r["status"] == "FAIL"),
        "results": results,
    }
    (RESULTS_DIR / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2, default=str))
    return 0 if summary["fail"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
