#!/usr/bin/env python3
"""Lock exact record insertion through BlockJukebox activation."""

from __future__ import annotations

import json
import pathlib

from callback_census import census


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main() -> int:
    receipt = json.loads((HERE / "block_callback_jukebox_receipt.json").read_text())
    if receipt.get("schema") != "netherite.block_callback_jukebox_receipt" \
            or receipt.get("version") != 1 or receipt.get("pass") != 1 \
            or receipt.get("state_divergences") != 0 \
            or len(receipt.get("campaign_summary_sha256", "")) != 64 \
            or receipt.get("case") != "ordinary_player_insert_record_13_seed_0" \
            or receipt.get("excluded") != "ordinary_player_eject_record_13_seed_0":
        raise RuntimeError("invalid jukebox callback receipt")
    covered = {(row[0], row[1]) for row in receipt.get("families", [])}
    if covered != {("BlockJukebox", "onBlockActivated")}:
        raise RuntimeError("jukebox callback family changed")
    families = {(family["owner"].rsplit(".", 1)[-1], family["callback"]) for family in census()["families"]}
    if not covered <= families:
        raise RuntimeError("jukebox callback census owner changed")
    if "ordinary_player_insert_record_13" not in (ROOT / "magma/trace/run_oracle_matrix.py").read_text():
        raise RuntimeError("jukebox oracle case missing")
    if "loaded_order" not in (ROOT / "magma/game/script.c").read_text():
        raise RuntimeError("static-container state order missing")
    print("PASS jukebox callback: exact record insertion activation")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL jukebox callback: {exc}")
        raise SystemExit(1)
