#!/usr/bin/env python3
"""Shrink one failing redstone .blocks topology against the live Java oracle."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
MATRIX = ROOT / "magma" / "trace" / "run_oracle_matrix.py"
FIRST_DIVERGENCE = re.compile(
    r"^\s*(\S+)\s+DIVERGES @tick (\d+) field=([^ ]+)", re.MULTILINE)


class ReduceError(RuntimeError):
    pass


def read_rows(path: pathlib.Path) -> list[tuple[int, int, int, int, int]]:
    rows: list[tuple[int, int, int, int, int]] = []
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        text = raw.split("#", 1)[0].strip()
        if not text:
            continue
        try:
            row = tuple(int(value) for value in text.split())
        except ValueError as exc:
            raise ReduceError(f"{path}:{number}: non-integer row") from exc
        if len(row) != 5:
            raise ReduceError(f"{path}:{number}: expected x y z id meta")
        rows.append(row)  # type: ignore[arg-type]
    if not rows:
        raise ReduceError("fixture is empty")
    return rows


def valid(rows: list[tuple[int, int, int, int, int]]) -> bool:
    return (any(row[3] == 55 for row in rows)
            and any(row[3] in (123, 124) for row in rows))


def write_rows(path: pathlib.Path,
               rows: list[tuple[int, int, int, int, int]]) -> None:
    path.write_text(
        "# minimized Java/native redstone first-divergence fixture\n"
        + "".join("%d %d %d %d %d\n" % row for row in rows))


def ddmin(rows: list[tuple[int, int, int, int, int]], predicate) \
        -> list[tuple[int, int, int, int, int]]:
    granularity = 2
    while len(rows) > 2:
        chunk = max(1, (len(rows) + granularity - 1) // granularity)
        changed = False
        for start in range(0, len(rows), chunk):
            candidate = rows[:start] + rows[start + chunk:]
            if not valid(candidate):
                continue
            if predicate(candidate):
                rows = candidate
                granularity = max(2, granularity - 1)
                changed = True
                break
        if changed:
            continue
        if granularity >= len(rows):
            break
        granularity = min(len(rows), granularity * 2)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--port", type=int, default=25600)
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()
    if args.output.exists():
        raise ReduceError(f"output already exists: {args.output}")
    rows = read_rows(args.fixture)
    if not valid(rows):
        raise ReduceError("fixture must contain dust and a lamp")
    work_root = ROOT / ".tmp" / "redstone-reduce"
    work_root.mkdir(parents=True, exist_ok=True)
    attempt = 0
    fingerprint: str | None = None

    def run(candidate: list[tuple[int, int, int, int, int]]) -> bool:
        nonlocal attempt, fingerprint
        attempt += 1
        with tempfile.TemporaryDirectory(
                prefix=f"attempt-{attempt:04d}-", dir=work_root) as raw:
            work = pathlib.Path(raw)
            fixture = work / "candidate.blocks"
            out = work / "matrix"
            write_rows(fixture, candidate)
            env = dict(os.environ)
            env.setdefault("UV_CACHE_DIR", str(pathlib.Path.home() / ".cache/uv"))
            env.setdefault("TMPDIR", str(ROOT / ".tmp"))
            result = subprocess.run([
                "uv", "run", "--no-project", "python", str(MATRIX),
                "--instances", "1", "--port-base", str(args.port),
                "--only-redstone-fuzz",
                "--redstone-fuzz-fixture", str(fixture),
                "--skip-random", "--skip-mining", "--skip-survival",
                "--skip-fire", "--skip-combat",
                "--case-timeout", str(args.timeout), "--out", str(out),
            ], cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
               stderr=subprocess.STDOUT, check=False)
            summary_path = out / "summary.json"
            if not summary_path.is_file():
                raise ReduceError(
                    f"attempt {attempt} infrastructure failure:\n{result.stdout[-2000:]}")
            summary = json.loads(summary_path.read_text())
            case = summary["cases"][0]
            log = pathlib.Path(case["log"]).read_text(errors="replace")
            match = FIRST_DIVERGENCE.search(log)
            observed = "|".join(match.groups()) if match else None
            if fingerprint is None:
                if case["status"] != "parity-fail" or observed is None:
                    raise ReduceError("initial fixture has no state first divergence")
                fingerprint = observed
                print(f"fingerprint={fingerprint}", flush=True)
            reproduces = case["status"] == "parity-fail" and observed == fingerprint
            print(
                f"attempt={attempt} rows={len(candidate)} "
                f"status={case['status']} fingerprint={observed}", flush=True)
            return reproduces

    if not run(rows):
        raise ReduceError("initial fixture did not reproduce")
    minimized = ddmin(rows, run)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_rows(args.output, minimized)
    print(
        f"PASS redstone reduction: {len(rows)} -> {len(minimized)} rows, "
        f"{attempt} oracle attempts -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ReduceError, ValueError) as exc:
        print(f"FAIL redstone reduction: {exc}")
        raise SystemExit(1)
