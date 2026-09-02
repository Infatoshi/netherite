#!/usr/bin/env python3
"""verify_detmob.py — M1 gate verifier for detmob (det_entity_rng=1) mob AI.

Verifies:
1. Lockstep Magma-CPU vs Blaze-CPU PARY digest equality over a 64-tick chain
   with det_entity_rng=1 enabled (Java EntityAITasks + PathFinder A*).
2. Bit-exact detmob tape scenarios (panic, passive, hostile ambient) against
   the Oracle Minecraft 1.11.2 reference traces via detmob_gate.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "blaze" / "env"))
import verify_cpu


def run_detmob_tapes() -> bool:
    tapes = [
        REPO / "verify" / "tapes" / "scenario_detmob_panic_20260821T170933Z.jsonl",
        REPO / "verify" / "tapes" / "scenario_detmob_passive_20260821T152220Z.jsonl",
        REPO / "verify" / "tapes" / "scenario_detmob_hostile_ambient_20260821T181540Z.jsonl",
    ]
    gate_py = REPO / "verify" / "trace" / "detmob_gate.py"
    for tape in tapes:
        if not tape.exists():
            print(f"BLOCKED: missing tape {tape}")
            return False
        cmd = [sys.executable, str(gate_py), str(tape)]
        res = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True)
        if res.returncode != 0:
            print(f"FAILED detmob tape {tape.name}:\n{res.stdout}\n{res.stderr}")
            return False
        for line in res.stdout.strip().splitlines():
            if "PASS" in line or "tape" in line:
                print(f"  {line}")
    return True


def main() -> int:
    argv = list(sys.argv[1:])
    if "--det-entity-rng" not in argv:
        argv.append("--det-entity-rng")

    # 1. Run chain lockstep digest comparison
    orig_argv = sys.argv
    rc = 0
    try:
        sys.argv = [sys.argv[0]] + argv
        verify_cpu.main()
    except SystemExit as e:
        rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
    finally:
        sys.argv = orig_argv

    if rc != 0:
        return rc

    # 2. Run detmob scenario tapes
    print("\n--- Verifying detmob scenario tapes ---")
    if not run_detmob_tapes():
        return 1

    print("\nVERIFIED: mobs_det M1 lockstep bit-equal digests and detmob tapes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
