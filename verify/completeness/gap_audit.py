#!/usr/bin/env python3
"""Report strict closure separately from bounded implementation coverage."""

from __future__ import annotations

import argparse
import collections
import json
import pathlib
import re
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LEDGER = ROOT / "docs" / "COMPLETENESS.md"
REGISTRY = HERE / "registry_manifest.json"
SURFACES = HERE / "surface_registry_manifest.json"
PRIORITY = HERE / "gap_priority.json"
CLOSURE_QUEUE = HERE / "closure_queue.json"
STRICT_CLOSURES = HERE / "strict_closure_receipts.json"
ROW_RE = re.compile(
    r"\| `([A-Z]+-\d+)` \| (.*?) \| (.*?) \|\s*$")


class GapAuditError(RuntimeError):
    pass


def row_status(gap: str, strict_exit: str) -> str:
    text = gap + " " + strict_exit
    if "**DONE" in text:
        return "done"
    if "LIVE BOUNDED" in text:
        return "bounded"
    if "LIVE PARTIAL" in text:
        return "partial"
    return "open"


def first_sentence(value: str) -> str:
    value = value.replace("**", "").replace("`", "")
    return re.split(r"(?<=[.!?])\s+", value, maxsplit=1)[0]


def load_rows() -> list[dict[str, str]]:
    rows = []
    for line_number, line in enumerate(
            LEDGER.read_text(encoding="utf-8").splitlines(), 1):
        match = ROW_RE.fullmatch(line)
        if not match:
            continue
        todo, gap, strict_exit = match.groups()
        rows.append({
            "todo": todo,
            "status": row_status(gap, strict_exit),
            "summary": first_sentence(gap),
            "line": str(line_number),
        })
    ids = [row["todo"] for row in rows]
    if len(rows) != 92 or len(ids) != len(set(ids)):
        raise GapAuditError(
            f"expected 92 unique strict rows, got {len(rows)} rows and "
            f"{len(set(ids))} unique IDs")
    return rows


def load_json(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise GapAuditError(f"{path.name} is not an object")
    return value


def ownership_counts() -> collections.Counter[str]:
    counts: collections.Counter[str] = collections.Counter()
    registry = load_json(REGISTRY)
    for family in ("entities", "tile_entities"):
        for row in registry[family]:
            counts[row["todo"]] += 1
    surfaces = load_json(SURFACES)
    for family in (
            "blocks", "items", "crafting_recipes", "smelting_recipes",
            "potions", "potion_types", "loot_tables", "containers",
            "guis"):
        for row in surfaces[family]:
            counts[row["todo"]] += 1
            if row.get("secondary_todo"):
                counts[row["secondary_todo"]] += 1
    return counts


def audit() -> dict[str, Any]:
    rows = load_rows()
    by_id = {row["todo"]: row for row in rows}
    strict_closures = load_json(STRICT_CLOSURES)
    if strict_closures.get("schema") != "netherite.strict_closure_receipts" \
            or strict_closures.get("version") != 2:
        raise GapAuditError("invalid strict closure receipt header")
    closure_rows = strict_closures.get("closures")
    if not isinstance(closure_rows, list):
        raise GapAuditError("strict closure receipts are not a list")
    promoted = {
        row["todo"] for row in closure_rows
    }
    if len(promoted) != len(closure_rows):
        raise GapAuditError("duplicate strict closure receipt")
    unknown_promotions = sorted(promoted - set(by_id))
    if unknown_promotions:
        raise GapAuditError(
            f"strict receipts promote unknown TODOs: {unknown_promotions}")
    for row in rows:
        if row["todo"] in promoted:
            if row["status"] != "bounded":
                raise GapAuditError(
                    f"strict receipt for non-bounded {row['todo']}")
            row["status"] = "done"
            row["promotion"] = "retained_receipt"
    ownership = ownership_counts()
    unknown = sorted(set(ownership) - set(by_id))
    if unknown:
        raise GapAuditError(f"registry surfaces own unknown TODOs: {unknown}")

    priority = load_json(PRIORITY)
    if priority.get("schema") != "netherite.strict_gap_priority" \
            or priority.get("version") != 1:
        raise GapAuditError("invalid priority manifest header")
    priority_ids = [entry["todo"] for entry in priority["priorities"]]
    if len(priority_ids) != len(set(priority_ids)):
        raise GapAuditError("duplicate priority TODO")
    for entry in priority["priorities"]:
        todo = entry["todo"]
        if todo not in by_id:
            raise GapAuditError(f"priority references unknown TODO {todo}")
        if by_id[todo]["status"] == "done":
            raise GapAuditError(f"completed TODO remains prioritized: {todo}")
        for field in ("reason", "closure"):
            if not isinstance(entry.get(field), str) or not entry[field]:
                raise GapAuditError(f"priority {todo} lacks {field}")

    closure_queue = load_json(CLOSURE_QUEUE)
    if closure_queue.get("schema") != 1:
        raise GapAuditError("invalid closure queue header")
    queue_ids = closure_queue.get("queue")
    if not isinstance(queue_ids, list) or not all(
            isinstance(todo, str) for todo in queue_ids):
        raise GapAuditError("closure queue is not a string list")
    if len(queue_ids) != len(set(queue_ids)):
        raise GapAuditError("duplicate closure queue TODO")
    expected_queue = {
        row["todo"] for row in rows
        if row["status"] in ("open", "partial")
    }
    if set(queue_ids) != expected_queue:
        missing = sorted(expected_queue - set(queue_ids))
        extra = sorted(set(queue_ids) - expected_queue)
        raise GapAuditError(
            f"closure queue mismatch: missing={missing}, extra={extra}")

    registry = load_json(REGISTRY)
    entity_status = collections.Counter(
        row["status"] for row in registry["entities"])
    tile_status = collections.Counter(
        row["status"] for row in registry["tile_entities"])
    status_counts = collections.Counter(row["status"] for row in rows)
    return {
        "schema": "netherite.strict_gap_audit",
        "version": 1,
        "strict_rows": len(rows),
        "strict_status": dict(sorted(status_counts.items())),
        "strict_closed_fraction": status_counts["done"] / len(rows),
        "receipt_promotions": len(promoted),
        "canonical_open": strict_closures.get("canonical_open", []),
        "registry": {
            "entities": len(registry["entities"]),
            "entity_status": dict(sorted(entity_status.items())),
            "tile_entities": len(registry["tile_entities"]),
            "tile_status": dict(sorted(tile_status.items())),
        },
        "priorities": [{
            **entry,
            "status": by_id[entry["todo"]]["status"],
            "owned_registry_surfaces": ownership[entry["todo"]],
            "ledger_line": int(by_id[entry["todo"]]["line"]),
        } for entry in priority["priorities"]],
        "closure_queue": [{
            "rank": rank,
            "todo": todo,
            "status": by_id[todo]["status"],
            "ledger_line": int(by_id[todo]["line"]),
        } for rank, todo in enumerate(queue_ids, 1)],
        "rows": [{
            **row,
            "line": int(row["line"]),
            "owned_registry_surfaces": ownership[row["todo"]],
        } for row in rows],
    }


def markdown(report: dict[str, Any]) -> str:
    statuses = report["strict_status"]
    registry = report["registry"]
    lines = [
        "# Strict completeness audit",
        "",
        f"Strict rows: {report['strict_rows']}; done: {statuses.get('done', 0)}; "
        f"bounded: {statuses.get('bounded', 0)}; "
        f"partial: {statuses.get('partial', 0)}; "
        f"open: {statuses.get('open', 0)}; binary closure: "
        f"{report['strict_closed_fraction'] * 100.0:.1f}%.",
        "",
        f"Registry identity coverage: {registry['entities']} entities "
        f"{registry['entity_status']}; {registry['tile_entities']} tiles "
        f"{registry['tile_status']}.",
        "",
        "The registry number measures classified live boundaries. The strict "
        "number requires arbitrary-state continuation, events, pixels, and "
        "performance evidence. They are intentionally not interchangeable.",
        "",
        "## Immediate order",
        "",
        "| Rank | TODO | State | Owned rows | Why now |",
        "|---:|---|---|---:|---|",
    ]
    for rank, entry in enumerate(report["priorities"], 1):
        lines.append(
            f"| {rank} | `{entry['todo']}` | {entry['status']} | "
            f"{entry['owned_registry_surfaces']} | {entry['reason']} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--check", action="store_true",
                        help="validate only and print a compact PASS")
    args = parser.parse_args()
    report = audit()
    if args.check:
        status = report["strict_status"]
        print(
            "PASS strict gap audit: "
            f"{status.get('done', 0)}/{report['strict_rows']} done, "
            f"{len(report['closure_queue'])} queued, "
            f"{len(report['priorities'])} immediate priorities, "
            f"{report['registry']['entities']} entities, "
            f"{report['registry']['tile_entities']} tiles")
    elif args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(markdown(report), end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GapAuditError, KeyError, OSError, TypeError, ValueError) as exc:
        print(f"FAIL strict gap audit: {exc}")
        raise SystemExit(1)
