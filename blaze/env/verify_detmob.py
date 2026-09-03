#!/usr/bin/env python3
"""verify_detmob.py — M1 gate for detmob (det_entity_rng=1) mob AI.

Lockstep Magma-CPU vs Blaze-CPU PARY digest equality with det_entity_rng=1
(Java EntityAITasks + PathFinder A*). Fails closed if blaze_mob_ai_stats
reports zero findPath calls: a scene where no mob ever paths proves nothing
about the pathfinder.

The eight verify/tapes/scenario_detmob_*.jsonl files stay magma-only
(detmob_gate vs the Java oracle). They are not blaze-replayed here.
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "blaze" / "env"))
import verify_cpu  # noqa: E402


def main() -> int:
    argv = list(sys.argv[1:])
    if "--det-entity-rng" not in argv:
        argv.append("--det-entity-rng")
    if "--require-findpath" not in argv:
        argv.append("--require-findpath")

    orig_argv = sys.argv
    rc = 0
    try:
        sys.argv = [sys.argv[0]] + argv
        verify_cpu.main()
    except SystemExit as e:
        rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
    finally:
        sys.argv = orig_argv
    return rc


if __name__ == "__main__":
    sys.exit(main())
