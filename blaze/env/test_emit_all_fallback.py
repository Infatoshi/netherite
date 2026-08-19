#!/usr/bin/env python3
"""Prove the batched-emit fast path never changes the chain gate's verdict.

verify_cuda.py's lanes_match now emits all lanes in one blaze_emit_all call and
memcmps the block; only on a mismatch does it fall through to the original
per-lane blaze_emit loop that produces the first-diff report. The risk that
buys is a fast path that disagrees with the slow one -- a divergence reported
at the wrong lane/field, or (worse) not reported at all.

This test injects a deliberate ONE-BYTE corruption into a chosen lane's
emitted record, consistently across both emit entry points, and runs the real
run_chain twice: once with the fast path live, once with no_emit_all=True
forcing the pre-change code path. The two transcripts must be byte-identical,
including the FAIL line's lane and field.

Run:
  cd blaze/env && uv run --no-project --with torch --with numpy \
      python test_emit_all_fallback.py
"""
import contextlib
import ctypes
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

BIN_SIZE = 14628          # verify_cpu.BIN_SIZE; asserted against the ABI below


def run_chain_with_corruption(lane, offset, no_emit_all):
    """One run_chain, with lane `lane`'s record corrupted at byte `offset`.

    The corruption is applied identically to blaze_emit_all (which fills the
    whole nl-lane block) and to per-lane blaze_emit, so both code paths are
    looking at the same fabricated reality.
    """
    for mod in ("verify_cuda", "verify_cpu", "blaze"):
        sys.modules.pop(mod, None)
    import verify_cuda as V

    assert V.BIN_SIZE == BIN_SIZE, (V.BIN_SIZE, BIN_SIZE)
    real_vec = V.VecBlaze

    def patched(n, device=0, so_path=None, **kw):
        kw = dict(kw)
        kw["no_emit_all"] = no_emit_all
        env = real_vec(n, device=device, so_path=so_path, **kw)
        if so_path != V.CUDA_SO:
            return env

        raw_all = getattr(env.lib, "blaze_emit_all", None)
        raw_one = env.lib.blaze_emit

        def flip(buf, at):
            b = (ctypes.c_char * 1).from_buffer(buf, at)
            b[0] = bytes([b[0][0] ^ 0xFF])

        if raw_all is not None:
            def emit_all(h, want, buf, _r=raw_all):
                rc = _r(h, want, buf)
                flip(buf, lane * BIN_SIZE + offset)
                return rc
            env.lib.blaze_emit_all = emit_all

        def emit_one(h, ln, want, buf, _r=raw_one):
            rc = _r(h, ln, want, buf)
            if ln == lane:
                flip(buf, offset)
            return rc
        env.lib.blaze_emit = emit_one
        return env

    V.VecBlaze = patched
    args = V.build_args([])
    args.chain = True
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        result = V.run_chain(args)
    return result, buf.getvalue()


def main():
    # (lane, byte offset). Offsets chosen to land in different BOLR fields and
    # at both ends of the lane block, so a wrong-lane or wrong-field report
    # cannot slip through.
    cases = [
        (0, 8),                       # lane 0,      -> field "tick"
        (1, 40),                      # lane 1,      -> field "pitch"
        (37, 128),                    # middle lane, -> field "inv_counts"
        (63, BIN_SIZE - 1),           # last lane,   -> last byte of "edge"
    ]
    failures = 0
    for lane, offset in cases:
        fast_rc, fast_out = run_chain_with_corruption(lane, offset, False)
        slow_rc, slow_out = run_chain_with_corruption(lane, offset, True)
        same = (fast_rc == slow_rc) and (fast_out == slow_out)
        detected = not fast_rc
        fail_line = next((ln for ln in fast_out.splitlines()
                          if ln.startswith("FAIL")), "<no FAIL line>")
        status = "ok" if (same and detected) else "MISMATCH"
        print(f"[{status}] lane {lane:2d} byte {offset:5d}: {fail_line}")
        if not same:
            print("  fast path transcript differs from per-lane transcript:")
            for a, b in zip(fast_out.splitlines(), slow_out.splitlines()):
                if a != b:
                    print(f"    fast: {a}\n    slow: {b}")
            failures += 1
        elif not detected:
            print("  corruption was NOT detected by either path")
            failures += 1

    # Control: with no corruption the gate must still pass on both paths.
    print()
    for no_all in (False, True):
        for mod in ("verify_cuda", "verify_cpu", "blaze"):
            sys.modules.pop(mod, None)
        import verify_cuda as V
        real_vec = V.VecBlaze

        def patched(n, device=0, so_path=None, **kw):
            kw = dict(kw)
            kw["no_emit_all"] = no_all
            return real_vec(n, device=device, so_path=so_path, **kw)

        V.VecBlaze = patched
        args = V.build_args([])
        args.chain = True
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = V.run_chain(args)
        tag = "per-lane" if no_all else "batched "
        print(f"[{'ok' if rc else 'FAIL'}] clean run, {tag}: "
              f"{buf.getvalue().strip().splitlines()[-1]}")
        if not rc:
            failures += 1

    print("\n" + ("PASS: fast path and per-lane path agree on every case"
                  if not failures else f"FAIL: {failures} case(s)"))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
