#!/usr/bin/env python3
"""Fail closed when a strict ledger promotion loses its retained receipt."""

from __future__ import annotations

import json
import pathlib
import re


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "strict_closure_receipts.json"
LEDGER = ROOT / "docs" / "COMPLETENESS.md"
ROW_RE = re.compile(r"\| `([A-Z]+-\d+)` \| (.*?) \| (.*?) \|\s*$")


class StrictClosureError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise StrictClosureError(message)


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    require(manifest.get("schema") == "netherite.strict_closure_receipts"
            and manifest.get("version") == 2,
            "invalid strict-closure manifest header")
    canonical_open_raw = manifest.get("canonical_open", [])
    require(isinstance(canonical_open_raw, list),
            "canonical_open must be a list")
    canonical_open = set(canonical_open_raw)
    closures = manifest.get("closures")
    require(isinstance(closures, list) and closures,
            "strict-closure manifest is empty")
    todos: set[str] = set()
    ledger_rows = {}
    for line in LEDGER.read_text(encoding="utf-8").splitlines():
        match = ROW_RE.fullmatch(line)
        if match:
            ledger_rows[match.group(1)] = match.group(2) + " " + match.group(3)
    require(len(ledger_rows) == 92,
            f"strict ledger has {len(ledger_rows)} rows, expected 92")
    gate_script = (HERE / "gate.sh").read_text(encoding="utf-8")
    for row in closures:
        todo = row.get("todo")
        require(isinstance(todo, str) and todo not in todos,
                f"invalid or duplicate strict closure {todo!r}")
        todos.add(todo)
        for field in ("gate", "receipt", "conclusion"):
            require(isinstance(row.get(field), str) and row[field],
                    f"{todo} lacks {field}")
        gate = ROOT / row["gate"]
        receipt = ROOT / row["receipt"]
        require(gate.is_file() and receipt.is_file(),
                f"{todo} lost retained gate or receipt")
        require(row["gate"] in gate_script,
                f"{todo} evidence gate is not in completeness/gate.sh")
        require(todo not in canonical_open,
                f"canonical owner {todo} cannot also be promoted")
    closure_by_todo = {row["todo"]: row for row in closures}
    for todo, row in closure_by_todo.items():
        owner = row.get("residual_owner")
        seen = {todo}
        while owner is not None and owner not in canonical_open:
            require(owner in closure_by_todo,
                    f"{todo} has invalid residual owner {owner!r}")
            require(owner not in seen,
                    f"{todo} has a residual-owner cycle through {owner}")
            seen.add(owner)
            owner = closure_by_todo[owner].get("residual_owner")
    base_done = {
        todo for todo, text in ledger_rows.items() if "**DONE" in text
    }
    base_bounded = set(ledger_rows) - base_done
    require(len(base_done) == 38,
            f"receipt baseline changed from 38 rows to {len(base_done)}")
    require(len(todos) + len(canonical_open) == len(base_bounded),
            "promotions plus canonical owners changed the bounded-row count")
    require(not (todos & canonical_open)
            and todos | canonical_open == base_bounded,
            "promotions and canonical owners do not partition bounded rows")
    print("PASS strict closure receipts: "
          f"{len(todos)} promoted rows ({', '.join(sorted(todos))})")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, TypeError, ValueError,
            StrictClosureError) as error:
        print(f"FAIL strict closure receipts: {error}")
        raise SystemExit(1)
