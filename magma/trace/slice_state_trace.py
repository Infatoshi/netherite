#!/usr/bin/env python3
"""Extract and renumber a contiguous suffix from a canonical JSONL trace."""

import argparse
import json
import pathlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--start", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    args = parser.parse_args()
    if args.start < 0 or args.count <= 0:
        parser.error("--start must be nonnegative and --count positive")
    rows = [
        json.loads(line)
        for line in pathlib.Path(args.input).read_text(
            encoding="utf-8").splitlines()
        if line.strip()
    ]
    end = args.start + args.count
    if end > len(rows):
        parser.error(
            f"requested rows {args.start}..{end - 1}, "
            f"but input has {len(rows)} rows"
        )
    suffix = rows[args.start:end]
    for tick, row in enumerate(suffix):
        if row.get("tick") != args.start + tick:
            parser.error("input tick fields are not contiguous")
        row["tick"] = tick
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "".join(
            json.dumps(row, separators=(",", ":")) + "\n"
            for row in suffix
        ),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
