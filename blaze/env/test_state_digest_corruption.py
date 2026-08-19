#!/usr/bin/env python3
"""Prove the default-on state-digest pass localizes a single-cell world corruption.

Two scenarios:

1. CPU (magma vs blaze): wrap blaze_parity_state so the Blaze world digest is
   XOR-flipped after a real fill - models a single-cell world mutation that
   updated the authoritative digest. verify_cpu.run_seed with state_digest=True
   must FAIL at INITIAL naming feature "world" and print both digests.

2. CUDA (CPU vs CUDA lanes): wrap blaze_parity_state_all so lane L's world
   digest is XOR-flipped. verify_cuda.run_chain must FAIL naming tick, lane L,
   feature "world", and both digests. Serial blaze_parity_state is also wrapped
   so the fallback path agrees.

A single-cell world change flips the world digest via bp_world_digest_replace
(XOR of the old and new cell tokens). XOR-flipping one bit of the digest is
the minimal observable of that class of corruption - enough to prove the gate
names the right lane and the world feature without touching tick kernels.

Run:
  cd blaze/env && uv run --no-project --with numpy --with torch \\
      python test_state_digest_corruption.py
"""
import contextlib
import io
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from verify_cpu import (  # noqa: E402
    FAILED,
    PARITY_SIZE,
    PARITY_WORLD_DIGEST_OFF,
    SNAPS,
    RL,
    run_seed,
)
import verify_cpu as Vcpu  # noqa: E402


def xor_world_digest(buf, at=0):
    """Flip the low bit of the packed world digest at record offset `at`.

    A single-cell world mutation updates this field via
    bp_world_digest_replace (XOR of old and new cell tokens). Flipping one
    bit is the minimal observable of that class of corruption.
    """
    off = at + PARITY_WORLD_DIGEST_OFF
    # ctypes create_string_buffer supports slice assign of bytes.
    (val,) = struct.unpack_from("<Q", buf, off)
    buf[off:off + 8] = struct.pack("<Q", val ^ 1)


def test_cpu_world_corruption():
    seed = 10
    snap = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
    acts_path = os.path.join(RL, "out", f"chain_actions_s{seed}.json")
    assert os.path.exists(snap) and os.path.exists(acts_path)

    real_blaze1 = Vcpu.Blaze1

    class CorruptBlaze(real_blaze1):
        def parity(self):
            super().parity()  # fill self.parity_buf via the real C path
            xor_world_digest(self.parity_buf)
            return Vcpu.ParityRecord(self.parity_buf.raw, "Blaze")

    Vcpu.Blaze1 = CorruptBlaze
    try:
        # Empty action list: INITIAL state-digest must fire before any step.
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            # run_seed returns VERIFIED / FAILED / BLOCKED (0 / 1 / 3).
            status = run_seed(seed, snap, [], "corruption selftest",
                              state_digest=True)
        out = buf.getvalue()
    finally:
        Vcpu.Blaze1 = real_blaze1

    print("--- CPU world-corruption selftest ---")
    print(out.rstrip())
    ok = status == FAILED
    detected = ok and ("world" in out) and ("STATE DIGEST" in out)
    has_both = "Magma: digest=" in out and "Blaze: digest=" in out
    if detected and has_both:
        print("[ok] CPU state digest named feature world with both digests")
        return 0
    print("[FAIL] CPU selftest did not localize world corruption")
    print(f"  ok={ok} detected={detected} has_both={has_both}")
    return 1


def test_cuda_world_corruption(lane=37):
    for mod in ("verify_cuda", "blaze"):
        sys.modules.pop(mod, None)
    import verify_cuda as V

    real_vec = V.VecBlaze
    flipped = {"n": 0}

    def patched(n, device=0, so_path=None, **kw):
        env = real_vec(n, device=device, so_path=so_path, **kw)
        if so_path != V.CUDA_SO:
            return env

        # Force _raw_abi to rebind after we wrap the symbols below; run_chain
        # calls _raw_abi which reads has_parity_all from the live lib.
        raw_all = getattr(env.lib, "blaze_parity_state_all", None)
        raw_one = env.lib.blaze_parity_state

        def flip_lane(buf, ln):
            xor_world_digest(buf, at=ln * PARITY_SIZE)
            flipped["n"] += 1

        if raw_all is not None:
            def parity_all(h, buf, _r=raw_all):
                rc = _r(h, buf)
                flip_lane(buf, lane)
                return rc
            env.lib.blaze_parity_state_all = parity_all

        def parity_one(h, ln, buf, _r=raw_one):
            rc = _r(h, ln, buf)
            if ln == lane:
                flip_lane(buf, 0)
            return rc
        env.lib.blaze_parity_state = parity_one
        return env

    V.VecBlaze = patched
    try:
        args = V.build_args([])
        args.chain = True
        # Keep the default state-digest path on; BOLR must still pass so the
        # failure is from state digests, not the cam plane.
        args.no_state_digest = False
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            result = V.run_chain(args)
        out = buf.getvalue()
    finally:
        V.VecBlaze = real_vec

    print("--- CUDA world-corruption selftest "
          f"(lane {lane}) ---")
    print(out.rstrip())
    fail_lines = [ln for ln in out.splitlines() if ln.startswith("FAIL")]
    fail_line = fail_lines[0] if fail_lines else "<no FAIL line>"
    detected = (not result) and f"lane {lane}" in fail_line and "world" in fail_line
    has_both = "cpu:  digest=" in out and "cuda: digest=" in out
    if detected and has_both and flipped["n"] > 0:
        print(f"[ok] CUDA state digest named lane {lane} feature world "
              f"({flipped['n']} flips applied)")
        return 0
    print("[FAIL] CUDA selftest did not localize world corruption")
    print(f"  result={result} fail_line={fail_line!r} "
          f"detected={detected} has_both={has_both} flips={flipped['n']}")
    return 1


def main():
    failures = 0
    failures += test_cpu_world_corruption()
    print()
    failures += test_cuda_world_corruption(lane=37)
    print()
    # Second lane at the edge of the 64-lane block.
    failures += test_cuda_world_corruption(lane=0)
    print()
    print("PASS: state-digest corruption selftests"
          if not failures else f"FAIL: {failures} case(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
