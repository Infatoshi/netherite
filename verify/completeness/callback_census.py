#!/usr/bin/env python3
"""Reduce the block callback registry to distinct Java implementations.

This is a census, not a parity pass. It deliberately fails if a reflected
owner/method cannot be joined to the pinned 1.11.2 source, preventing a broad
registry row from being mistaken for behavioral evidence.
"""

from __future__ import annotations

import hashlib
import argparse
import collections
import json
import pathlib
import re


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = HERE / "surface_registry_manifest.json"
SOURCE = ROOT / "java" / "oracle-src"
CHECKED = HERE / "block_callback_census_manifest.json"


class CensusError(RuntimeError):
    pass


def method_body(source: str, name: str) -> str:
    matches = list(re.finditer(
        r"\b(?:public|protected)\s+[^{};]+?\b"
        + re.escape(name) + r"\s*\(", source))
    bodies: list[str] = []
    for match in matches:
        brace = source.find("{", match.end())
        semi = source.find(";", match.end(), brace if brace >= 0 else None)
        if brace < 0 or (semi >= 0 and semi < brace):
            continue
        depth = 0
        in_string = False
        escape = False
        for at in range(brace, len(source)):
            char = source[at]
            if in_string:
                if escape:
                    escape = False
                elif char == "\\":
                    escape = True
                elif char == '"':
                    in_string = False
                continue
            if char == '"':
                in_string = True
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    bodies.append(source[brace + 1:at])
                    break
    if not bodies:
        raise CensusError(f"method source missing: {name}")
    # Callback overloads in 1.11.2 either share an implementation or are
    # bridge methods. Preserve every distinct body in the fingerprint.
    normalized = sorted({re.sub(r"\s+", " ", body).strip()
                         for body in bodies})
    return "\n".join(normalized)


def owner_path(owner: str) -> pathlib.Path:
    top_level = owner.split("$", 1)[0]
    path = SOURCE / pathlib.Path(*top_level.split(".")).with_suffix(".java")
    if not path.is_file():
        raise CensusError(f"callback owner source missing: {owner}")
    return path


def classify(body: str) -> str:
    compact = re.sub(r"\s+", " ", body).strip()
    statements = [part.strip() for part in compact.split(";") if part.strip()]
    if len(statements) <= 2 and any(token in compact for token in (
            "modelBlock.", "modelState.", "super.")):
        return "delegate"
    if compact in ("", "return false;", "return true;"):
        return "constant"
    return "direct"


def census() -> dict:
    manifest = json.loads(MANIFEST.read_text())
    families: dict[tuple[str, str], dict] = {}
    callback_rows = 0
    for block in manifest["blocks"]:
        owners = block.get("callback_owners")
        if set(owners or {}) != set(block["callback_overrides"]):
            raise CensusError(
                f"block {block['id']} callback owner join is incomplete")
        for callback, owner in owners.items():
            callback_rows += 1
            key = owner, callback
            family = families.setdefault(key, {
                "owner": owner,
                "callback": callback,
                "blocks": [],
            })
            family["blocks"].append({
                "id": block["id"], "name": block["name"]})
    source_cache: dict[str, str] = {}
    output = []
    for (owner, callback), family in sorted(families.items()):
        source = source_cache.setdefault(
            owner, owner_path(owner).read_text(encoding="utf-8"))
        body = method_body(source, callback)
        family["kind"] = classify(body)
        family["source_sha256"] = hashlib.sha256(
            body.encode()).hexdigest()
        output.append(family)
    return {
        "schema": "netherite.block_callback_census",
        "version": 1,
        "registry_blocks": len(manifest["blocks"]),
        "callback_rows": callback_rows,
        "implementation_families": len(output),
        "families": output,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    result = census()
    if args.update:
        CHECKED.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    if args.check:
        checked = json.loads(CHECKED.read_text(encoding="utf-8"))
        if checked != result:
            raise CensusError(
                "block callback census differs from checked manifest")
        kinds = collections.Counter(
            family["kind"] for family in result["families"])
        print(
            "PASS block callback census: "
            f"{result['registry_blocks']} blocks, "
            f"{result['callback_rows']} override rows, "
            f"{result['implementation_families']} implementation families, "
            f"{dict(sorted(kinds.items()))}")
        return 0
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CensusError, KeyError, OSError, ValueError) as exc:
        print(f"FAIL block callback census: {exc}")
        raise SystemExit(1)
