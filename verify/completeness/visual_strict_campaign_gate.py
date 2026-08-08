#!/usr/bin/env python3
"""Lock the measured VIS-05/VIS-07 registry and focused-pixel campaign."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
CORE_SOURCES = [
    "verify/ui_entities/run_oracle_gate.sh",
    "verify/ui_entities/run_strict_focused_suite.sh",
    "verify/ui_entities/entity_oracle_candidate.c",
    "magma/game/entity_render.c",
    "magma/game/frame_capture.c",
    "magma/game/item_render.c",
    "magma/assets/blockmodels.c",
]


def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)


def source_bundle() -> str:
    runner = (ROOT / "verify/ui_entities/run_oracle_gate.sh").read_text()
    measures = sorted(set(re.findall(r'python "\$DIR/([^\"]+\.py)"', runner)))
    paths = [ROOT / relative for relative in CORE_SOURCES]
    paths.extend(ROOT / "verify/ui_entities" / name for name in measures)
    digest = hashlib.sha256()
    for path in paths:
        require(path.is_file(), f"missing visual source {path.relative_to(ROOT)}")
        line = hashlib.sha256(path.read_bytes()).hexdigest()
        digest.update(f"{line}  {path.relative_to(ROOT)}\n".encode())
    return digest.hexdigest()


def main() -> int:
    receipt = json.loads(
        (HERE / "visual_strict_campaign_receipt.json").read_text())
    surfaces = json.loads((HERE / "surface_registry_manifest.json").read_text())
    registry = json.loads((HERE / "registry_manifest.json").read_text())
    lanes = receipt["focused_pixel_lanes"]
    require(receipt["schema"] == "netherite.visual_strict_campaign"
            and receipt["version"] == 1 and receipt["result"] == "pass",
            "visual strict receipt header changed")
    require(len(lanes) == len(set(lanes)) == receipt["focused_pixel_pass"] == 27,
            "focused visual lane set changed")
    suite = (ROOT / "verify/ui_entities/run_strict_focused_suite.sh").read_text()
    for lane in lanes:
        mode = "ENTITY_GATE_" + lane.upper() + "_ONLY"
        require(mode in suite, f"focused lane is not executable: {lane}")
    require(len(registry["entities"]) == receipt["entity_registry_identities"] == 81,
            "entity visual registry changed")
    require(len(registry["tile_entities"]) == receipt["tile_registry_identities"] == 24,
            "tile visual registry changed")
    require(len(surfaces["blocks"]) == receipt["block_registry_identities"] == 236,
            "block visual registry changed")
    require(len(surfaces["items"]) == receipt["item_registry_identities"] == 392,
            "item visual registry changed")
    require(source_bundle() == receipt["source_bundle_sha256"],
            "visual source bundle changed; rerun and re-review the 27-lane campaign")
    subprocess.run(
        ["uv", "run", "--no-project", "--with", "pillow", "--with", "numpy",
         "python", "verify/ui_entities/validate_ui_entities_goldens.py",
         "--goldens", "verify/ui_entities/goldens"],
        cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["bash", "magma/game/test_item_render.sh"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["make", "-C", "magma", "test-model-oracle"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    print("PASS VIS strict campaign: 81 entities, 24 tiles, 236 blocks, "
          "392 items, and all 27 focused Java/native pixel lanes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, subprocess.CalledProcessError,
            json.JSONDecodeError) as error:
        print(f"FAIL VIS strict campaign: {error}")
        raise SystemExit(1)
