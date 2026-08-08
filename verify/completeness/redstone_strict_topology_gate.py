#!/usr/bin/env python3
"""Lock the large connected and cross-chunk RED-02/RED-03 campaign."""

from __future__ import annotations

import json
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def main() -> int:
    receipt = json.loads((
        HERE / "redstone_strict_topology_receipt.json"
    ).read_text(encoding="utf-8"))
    require(receipt.get("schema") == "netherite.redstone_strict_topology"
            and receipt.get("version") == 1
            and receipt.get("todo") == ["RED-02", "RED-03"],
            "invalid strict redstone receipt identity")
    sizes = receipt.get("sizes", [])
    require(sizes[:64] == list(range(1, 65))
            and {96, 128, 160, 192, 224, 256, 257, 384, 512}
            <= set(sizes),
            "strict redstone size ladder changed")
    require((receipt.get("case_count"), receipt.get("pass"),
             receipt.get("fail"), receipt.get("retry_count"))
            == (73, 73, 0, 0),
            "strict redstone campaign is not a clean run")
    require(receipt.get("state_divergences") == 0
            and receipt.get("unrepresented_state_fields") == 0
            and receipt.get("state_features_per_case", 0) >= 34,
            "strict redstone state comparison lost coverage")
    require(receipt.get("cross_chunk_cases", 0) >= 60
            and receipt.get("maximum_chunks_in_one_case", 0) >= 7
            and receipt.get("maximum_component_size", 0) >= 512,
            "strict redstone campaign lost large cross-chunk coverage")
    require(len(receipt.get("summary_sha256", "")) == 64,
            "strict redstone summary digest is invalid")
    source = (ROOT / "magma/trace/run_oracle_matrix.py").read_text(
        encoding="utf-8")
    for token in ("redstone_fuzz_sizes", "generated connected",
                  "require_state_exact=True", "block_light_compare=True"):
        require(token in source, f"redstone generator lost {token!r}")
    print(
        "PASS strict redstone topology: 73/73 exact size-ladder cases, "
        "61 cross chunk, maximum 512 dust cells across seven chunks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as error:
        print(f"FAIL strict redstone topology: {error}")
        raise SystemExit(1)
