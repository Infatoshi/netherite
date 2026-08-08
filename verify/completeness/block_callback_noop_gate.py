#!/usr/bin/env python3
"""Lock every intentional no-op in the strict block-callback census."""

from __future__ import annotations

import json
import pathlib
import subprocess

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    report = census()
    strict_callbacks = {
        "breakBlock", "dropBlockAsItemWithChance", "neighborChanged",
        "onBlockActivated", "onBlockPlacedBy", "randomTick", "updateTick",
    }
    families = {
        (family["owner"].rsplit(".", 1)[-1], family["callback"]):
            {block["id"] for block in family["blocks"]}
        for family in report["families"]
        if family["kind"] == "constant"
            and family["callback"] in strict_callbacks
    }
    receipt = json.loads(
        (HERE / "block_callback_noop_receipt.json").read_text())
    require(receipt.get("schema") == "netherite.block_callback_noop_receipt"
            and receipt.get("version") == 1,
            "invalid no-op callback receipt header")
    actual_rows = [tuple(row) for row in receipt.get("rows", [])]
    actual = {(owner, callback) for owner, callback, _ in actual_rows}
    expected = set(families)
    require(actual == expected,
            f"constant callback drift: missing={sorted(expected-actual)}, "
            f"extra={sorted(actual-expected)}")
    for owner, callback, block in actual_rows:
        require(block in families[(owner, callback)],
                f"receipt block {block} does not own {owner}.{callback}")
    stair_ids = (53, 67, 108, 109, 114, 128, 134, 135, 136, 156,
                 163, 164, 180, 203)
    delegates = {
        (owner, callback, tuple(ids)) for owner, callback, ids in
        receipt.get("effective_delegate_rows", [])
    }
    require(delegates == {
                ("BlockStairs", "breakBlock", stair_ids),
                ("BlockStairs", "onBlockActivated", stair_ids),
                ("BlockStairs", "updateTick", stair_ids),
            }, "effective delegate no-op receipt changed")
    census_ids = {
        family["callback"]: tuple(
            block["id"] for block in family["blocks"])
        for family in report["families"]
        if family["owner"].endswith(".BlockStairs")
            and family["callback"] in {
                "breakBlock", "onBlockActivated", "updateTick"}
    }
    require(census_ids == {
                "breakBlock": stair_ids,
                "onBlockActivated": stair_ids,
                "updateTick": stair_ids,
            }, "registered stair delegate identities changed")

    java = (ROOT / "java/Minecraft/src/main/java/qrl/"
            "BlockCallbackNoopGolden.java").read_text()
    for owner, callback in expected:
        require(owner in java and callback in java,
                f"Java no-op executor lost {owner}.{callback}")

    runtime_test = (ROOT / "magma/game/test_runtime.c").read_text()
    noop_test = (ROOT / "magma/game/test_block_callback_noop.c").read_text()
    rand_test = (ROOT / "magma/game/test_randtick.c").read_text()
    for token in (
            "registry no-op random callback preserves state",
            "registry no-op random callback consumes no RNG"):
        require(token in rand_test,
                f"native random no-op regression lost {token!r}")
    for token in (
            "destroyed structure void remains entity-free during movement",
            "all shulker colors and facings drop one tagged",
            "non-watched neighbor edit does not start an observer pulse"):
        require(token in runtime_test,
                f"native no-op regression lost {token!r}")
    for token in (
            "ordinary stair activation delegates to false model callback",
            "ordinary stair update delegates to empty model callback"):
        require(token in noop_test,
                f"native delegate regression lost {token!r}")
    subprocess.run(
        ["make", "-C", str(ROOT / "magma"),
         "game/test_block_callback_noop"],
        check=True, stdout=subprocess.DEVNULL)
    subprocess.run(
        [str(ROOT / "magma/game/test_block_callback_noop")], check=True,
        stdout=subprocess.DEVNULL)

    for owner, callback, _ in delegates:
        require(owner in java and callback in java,
                f"Java delegate executor lost {owner}.{callback}")

    print("PASS block callback no-ops: all 12 constant implementations and "
          "3 effective stair delegates execute in real Java and retain "
          "native mutation controls")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL block callback no-ops: {exc}")
        raise SystemExit(1)
