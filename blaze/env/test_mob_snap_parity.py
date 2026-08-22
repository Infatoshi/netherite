#!/usr/bin/env python3
"""BP_MOBS digest parity: magma-CPU vs blaze-CPU on static loaded state.

Blaze does not step mobs. Compares INITIAL PARY only (zero actions).
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import verify_cpu as vc  # noqa: E402

S14 = os.path.join(REPO, "verify/fixtures/port/s14_t0_r48_no_liquid.bsnp")
TEST_BIN = os.path.join(REPO, "out/blaze/rl/test_mob_snapshot")


def _run(snap, seed, label):
    info = {}
    status = vc.run_seed_parity(
        seed, snap, [], label, ["mobs"],
        strict_capabilities=True, require_evidence=True, result=info)
    print(f"  {label}: status={status} exact_ticks={info.get('exact_ticks')}")
    return status


def main():
    if not os.path.isfile(vc.BIN):
        print(f"missing magma_game: {vc.BIN}", file=sys.stderr)
        return vc.BLOCKED
    if not os.path.isfile(vc.SO):
        print(f"missing blaze_cpu.so: {vc.SO}", file=sys.stderr)
        return vc.BLOCKED
    if not os.path.isfile(TEST_BIN):
        print(f"missing {TEST_BIN} (make -C blaze/rl test-capture)",
              file=sys.stderr)
        return vc.BLOCKED
    if not os.path.isfile(S14):
        print(f"missing fixture {S14}", file=sys.stderr)
        return vc.BLOCKED

    statuses = []
    print("zero-mob v2 (s14 fixture, n_mobs=0)")
    statuses.append(_run(S14, 14, "zero-mob v2"))

    with tempfile.TemporaryDirectory(prefix="mobsnap_parity_") as tmp:
        empty = os.path.join(tmp, "empty_v3.bsnp")
        pop = os.path.join(tmp, "pop_v3.bsnp")
        cmd = [TEST_BIN, "--from", S14, "--write-empty", empty,
               "--write-pop", pop]
        print("fixture writer:", " ".join(cmd))
        subprocess.check_call(cmd)
        print("zero-mob v3 (same world, trailer n_mobs=0)")
        statuses.append(_run(empty, 14, "zero-mob v3"))
        print("populated v3 (synthetic zombie slot 1 + creeper slot 4)")
        statuses.append(_run(pop, 14, "populated v3"))

    if any(s == vc.FAILED for s in statuses):
        print("FAILED: BP_MOBS digest mismatch")
        return vc.FAILED
    if any(s == vc.BLOCKED for s in statuses):
        print("BLOCKED: BP_MOBS digest parity could not run")
        return vc.BLOCKED
    print("VERIFIED: BP_MOBS magma-CPU == blaze-CPU (zero-mob v2/v3 + populated)")
    return vc.VERIFIED


if __name__ == "__main__":
    sys.exit(main())
