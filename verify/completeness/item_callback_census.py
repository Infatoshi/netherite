#!/usr/bin/env python3
"""Join every registered item action override to pinned 1.11.2 source.

This is the fail-closed source census for ITEM-08. Behavioral promotion still
requires a positive/negative oracle fixture for every emitted family.
"""

from __future__ import annotations

import hashlib
import json
import pathlib

from callback_census import CensusError, classify, method_body, owner_path


HERE = pathlib.Path(__file__).resolve().parent
MANIFEST = HERE / "surface_registry_manifest.json"


def census() -> dict:
    manifest = json.loads(MANIFEST.read_text())
    families: dict[tuple[str, str], dict] = {}
    rows = 0
    for item in manifest["items"]:
        if item["id"] == 0:
            continue
        owners = item.get("callback_owners")
        if set(owners or {}) != set(item["callback_overrides"]):
            raise CensusError(
                f"item {item['id']} callback owner join is incomplete")
        for callback, owner in owners.items():
            rows += 1
            family = families.setdefault((owner, callback), {
                "owner": owner, "callback": callback, "items": []})
            family["items"].append({
                "id": item["id"], "name": item["name"]})
    output = []
    source_cache: dict[str, str] = {}
    for (owner, callback), family in sorted(families.items()):
        source = source_cache.setdefault(
            owner, owner_path(owner).read_text(encoding="utf-8"))
        body = method_body(source, callback)
        family["kind"] = classify(body)
        family["source_sha256"] = hashlib.sha256(body.encode()).hexdigest()
        output.append(family)
    return {
        "schema": "netherite.item_callback_census", "version": 1,
        "registry_items": len(manifest["items"]) - 1,
        "callback_rows": rows,
        "implementation_families": len(output), "families": output}


def main() -> int:
    print(json.dumps(census(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CensusError, KeyError, OSError, ValueError) as exc:
        print(f"FAIL item callback census: {exc}")
        raise SystemExit(1)
