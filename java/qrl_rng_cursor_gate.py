#!/usr/bin/env python3
"""qrl_rng_cursor_gate.py - acceptance check for RNG cursor capture.

Vanilla consumes java.util.Random draws in a fixed order every server tick, and
magma has to consume the same number in the same order.  When it does not, the
symptom appears hundreds of ticks later as a physics or pixel divergence with no
visible cause.  This gate proves the machinery that turns that silent drift into
a named tick and a named stream.

java.util.Random *is* its 48-bit LCG state, so a snapshot at a tick boundary is
a cursor and two cursors bracketing a tick give the exact draw count that tick
consumed (verify/trace/rng_cursor.py).  The bridge captures four server streams
at ServerTickEvent.START - World.rand, Math's global RNG, Block.RANDOM, and the
int32 World.updateLCG - into a bounded ring, so a long session costs one dump
instead of one socket round-trip per tick.

The gate builds two independent views of the SAME run and requires them to
agree:

  reference  rng_cursor_locked read at each parked boundary, socket thread
  candidate  the ring record written on the server thread at that tick's START

Because both come from one run, the green path assumes nothing about world
determinism.  The negative control then burns exactly one real java.util.Random
draw on the live server at a chosen boundary (rng_burn_locked) and requires the
comparator to name that exact index, that exact stream, and a +1 draw delta.
A second, offline negative control perturbs a captured sidecar to prove the
same detector without a JVM.

Run it against a live headless client that already owns port 25575:

    bash java/start_vnc_client.sh          # wait for "[qrl] listening"
    uv run --no-project python java/qrl_rng_cursor_gate.py

Exit code 0 = every check passed.  The gate edits no blocks and moves no
entities; the only world effect is the ticks it steps and the RNG draws it
deliberately burns, which free-running play does constantly.
"""
import argparse
import json
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "verify", "trace"))
from qrl_client import NetheriteEnv
from rng_cursor import (
    STREAMS,
    draws_between,
    first_divergence,
    lcg_steps_between,
    parse_sidecar,
    profile,
    shift_stream,
)

FAILURES = []
CHECKS = 0


def check(label, ok, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  PASS  " if ok else "  FAIL  ") + label + (("   " + detail) if detail else ""))
    if not ok:
        FAILURES.append(label + (("   " + detail) if detail else ""))
    return ok


class Gate:
    """Thin wrapper over the bridge for the locked + RNG command set."""

    def __init__(self, env):
        self.env = env

    def _cmd(self, cmd, action=None, deadline=300.0):
        o = self.env._cmd({"cmd": cmd, "action": action or {}}, read_deadline=deadline)
        if not o.get("ok"):
            raise RuntimeError("{} -> {}".format(cmd, o.get("error", o)))
        return o

    def arm(self):
        return self._cmd("server_step_lock")

    def disarm(self):
        return self._cmd("server_step_unlock")

    def step(self, n):
        return self._cmd("step_server_locked", {"n": n})

    def cursor(self):
        return self._cmd("rng_cursor_locked")

    def burn(self, stream="world", n=1):
        return self._cmd("rng_burn_locked", {"stream": stream, "n": n})

    def capture(self, on=1, capacity=20000):
        return self._cmd("rng_capture", {"on": on, "capacity": capacity})

    def dump(self, path=None):
        return self._cmd("rng_dump", {"file": path} if path else {})


def cursor_rec(o):
    """Project a bridge cursor reply onto the sidecar record shape."""
    return {
        "seq": -1,
        "world_time": o.get("world_time", -1),
        "world_seed48": o["world_seed48"],
        "world_have_gaussian": o["world_have_gaussian"],
        "world_gaussian_bits": o["world_gaussian_bits"],
        "math_seed48": o["math_seed48"],
        "block_seed48": o["block_seed48"],
        "update_lcg": o["update_lcg"],
    }


def stepped_run(g, ticks, burn_at=None, burn_stream="world", burn_n=1,
                capacity=20000):
    """Step `ticks` ticks, collecting a locked-read reference and a ring capture.

    At each parked boundary we read the cursor over the socket, then permit one
    tick.  The bridge writes its ring record on the server thread immediately
    after the permit and before the tick body, so record i must equal the locked
    read taken at boundary i.  `burn_at` injects real draws after the reference
    read at that boundary, which is what the ring must then disagree about.
    """
    g.capture(on=1, capacity=capacity)
    reference = []
    for i in range(ticks):
        reference.append(cursor_rec(g.cursor()))
        if burn_at is not None and i == burn_at:
            g.burn(stream=burn_stream, n=burn_n)
        g.step(1)
    g.capture(on=0)
    d = g.dump()
    return reference, parse_sidecar(d["records"]), d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--ticks", type=int, default=40,
                    help="ticks per captured run")
    ap.add_argument("--burn-at", type=int, default=17,
                    help="boundary index for the live negative control")
    ap.add_argument("--tmp", default="/tmp/qrl_rng_cursor")
    args = ap.parse_args()
    os.makedirs(args.tmp, exist_ok=True)

    env = NetheriteEnv(port=args.port)
    g = Gate(env)

    st = env.stats()
    if not st.get("ok"):
        print(f"bridge is not in a world: {st}")
        return 2
    print(f"connected: {json.dumps({k: st[k] for k in list(st)[:6]})}")

    g.arm()

    # ------------------------------------------------- cursor read semantics
    print("\n[cursor] rng_cursor_locked is a stable, tick-free read")
    c_a = g.cursor()
    c_b = g.cursor()
    check("two back-to-back locked cursor reads are identical",
          all(c_a[k] == c_b[k] for k, _, _ in STREAMS),
          "world=0x{:012x}".format(c_a["world_seed48"]))
    check("locked cursor read consumes no server tick",
          g.step(0)["world_time"] == c_b["world_time"],
          "world_time={}".format(c_b["world_time"]))
    check("all four streams are readable (none reported unavailable)",
          all(c_a[k] >= 0 for k, _, _ in STREAMS if k != "update_lcg"),
          "world=0x{:012x} math=0x{:012x} block=0x{:012x} lcg={}".format(
              c_a["world_seed48"], c_a["math_seed48"], c_a["block_seed48"],
              c_a["update_lcg"]))

    # ------------------------------------- one real draw == one LCG step
    # The whole method rests on this: a java.util.Random call advances the
    # captured cursor by exactly one countable step on the LIVE JVM, not just in
    # the Python model.  Proven here against the real server before any gate
    # result is trusted.
    print("\n[burn] a real java.util.Random call is exactly one countable step")
    for n in (1, 5, 23):
        before = g.cursor()["world_seed48"]
        g.burn("world", n)
        after = g.cursor()["world_seed48"]
        got = draws_between(before, after)
        check(f"burning {n} world draw(s) advances the cursor by exactly {n}",
              got == n, f"recovered {got}")
    before = g.cursor()
    g.burn("block", 3)
    after = g.cursor()
    check("burning block draws moves ONLY the block stream",
          draws_between(before["block_seed48"], after["block_seed48"]) == 3
          and before["world_seed48"] == after["world_seed48"],
          "block +{} world moved={}".format(
              draws_between(before["block_seed48"], after["block_seed48"]),
              before["world_seed48"] != after["world_seed48"]))
    before = g.cursor()
    g.burn("lcg", 4)
    after = g.cursor()
    check("burning updateLCG advances the int32 stream by exactly 4",
          lcg_steps_between(before["update_lcg"], after["update_lcg"]) == 4,
          "recovered {}".format(
              lcg_steps_between(before["update_lcg"], after["update_lcg"])))

    # ------------------------------------------------------- GREEN PATH
    print(f"\n[green] {args.ticks}-tick capture: ring must match the locked reads")
    ref, ring, meta = stepped_run(g, args.ticks)
    check("ring captured one record per stepped tick",
          len(ring) == args.ticks, f"{len(ring)} records for {args.ticks} ticks")
    check("no ring overflow", meta["dropped"] == 0, f"dropped={meta['dropped']}")
    div = first_divergence(ref, ring)
    check("ring capture agrees with every locked read (no divergence)",
          div is None, "" if div is None else div.describe())

    prof = profile(ring)
    unreachable = [t for t in prof if not t.ok]
    total = sum(t.draws["world_rand"] or 0 for t in prof)
    lcgtot = sum(t.draws["update_lcg"] or 0 for t in prof)
    check("every world-stream transition is reachable (all draws accounted)",
          not unreachable,
          f"{len(unreachable)} unreachable of {len(prof)}")
    counts = [t.draws["world_rand"] for t in prof]
    print(f"        world draws/tick: total={total} min={min(counts)} "
          f"max={max(counts)} first10={counts[:10]}")
    print(f"        updateLCG steps/tick: total={lcgtot}")
    check("the run actually consumed world draws (capture is not inert)",
          total > 0, f"total={total}")

    sidecar = os.path.join(args.tmp, "green.jsonl")
    g.capture(on=1, capacity=args.ticks + 8)
    for _ in range(args.ticks):
        g.step(1)
    g.capture(on=0)
    dmp = g.dump(sidecar)
    check("rng_dump writes a sidecar file with a digest",
          os.path.exists(sidecar) and len(dmp["digest"]) == 16,
          "count={} digest={}".format(dmp["count"], dmp["digest"]))

    # --------------------------------------------- LIVE NEGATIVE CONTROL
    # Same construction as the green run, but one real java.util.Random draw is
    # burned on the live server at a known boundary AFTER the reference read.
    # The ring must then disagree at exactly that index and nowhere earlier.
    print(f"\n[negative-live] burn 1 real world draw at boundary {args.burn_at}")
    ref_n, ring_n, _ = stepped_run(g, args.ticks, burn_at=args.burn_at)
    div_n = first_divergence(ref_n, ring_n)
    check("the injected draw IS detected", div_n is not None)
    if div_n is not None:
        print("        " + div_n.describe().replace("\n", "\n        "))
        check("detected at exactly the burned boundary",
              div_n.index == args.burn_at,
              f"index {div_n.index}, expected {args.burn_at}")
        check("detected on the world_rand stream",
              div_n.stream == "world_rand", div_n.stream)
        check("classified as a 1-draw phase shift",
              "1 draw(s) AHEAD" in div_n.relation, div_n.relation)

    print("\n[negative-live] burn on updateLCG is attributed to that stream")
    ref_l, ring_l, _ = stepped_run(g, 12, burn_at=5, burn_stream="lcg", burn_n=1)
    div_l = first_divergence(ref_l, ring_l)
    check("updateLCG burn detected on the update_lcg stream",
          div_l is not None and div_l.stream == "update_lcg"
          and div_l.index == 5,
          "" if div_l is None else f"{div_l.stream} at index {div_l.index}")

    # ------------------------------------------ OFFLINE NEGATIVE CONTROL
    # Same detector, no JVM: perturb a captured sidecar the way a real
    # misalignment would (extra draw at tick k, stream stays offset after).
    print("\n[negative-offline] perturb a captured sidecar")
    k = len(ring) // 2
    bad = shift_stream(ring, k, "world_seed48", 1)
    div_o = first_divergence(ring, bad)
    check("offline +1 draw at tick k is detected at exactly k",
          div_o is not None and div_o.index == k and div_o.stream == "world_rand",
          "" if div_o is None else f"index {div_o.index} stream {div_o.stream}")
    if div_o is not None:
        check("offline control is classified as a pure 1-draw phase shift",
              "1 draw(s) AHEAD" in div_o.relation
              and div_o.draws_actual == div_o.draws_expected,
              "a persistent shift leaves per-tick counts equal "
              f"(expected={div_o.draws_expected} actual={div_o.draws_actual}); "
              f"{div_o.relation}")
    bad3 = shift_stream(ring, k, "block_seed48", 3)
    div_b = first_divergence(ring, bad3)
    check("offline block-stream perturbation is attributed to block_rand",
          div_b is not None and div_b.stream == "block_rand"
          and "3 draw(s) AHEAD" in div_b.relation,
          "" if div_b is None else f"{div_b.stream}: {div_b.relation}")
    check("an unperturbed sidecar compares clean against itself",
          first_divergence(ring, [dict(r) for r in ring]) is None)

    # ------------------------------------------------------------- guards
    print("\n[guards] locked RNG commands refuse an unparked server")
    g.disarm()
    bad_r = env._cmd({"cmd": "rng_cursor_locked", "action": {}})
    check("rng_cursor_locked errors when the gate is off",
          bad_r.get("ok") is False
          and "server_step_lock" in str(bad_r.get("error", "")),
          str(bad_r.get("error")))
    bad_r = env._cmd({"cmd": "rng_burn_locked", "action": {"n": 1}})
    check("rng_burn_locked errors when the gate is off",
          bad_r.get("ok") is False, str(bad_r.get("error")))
    # capture control is deliberately gate-free: it must work while recording a
    # normal free-running session, so it is valid with the lock released.
    free = env._cmd({"cmd": "rng_capture", "action": {"on": 0}})
    check("rng_capture works without the gate (free-running capture)",
          free.get("ok") is True, str(free))

    print(f"\n{CHECKS} checks, {len(FAILURES)} failures")
    for f in FAILURES:
        print(f"  FAILED: {f}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}")
        # Never leave the server parked or the ring armed because the harness blew up.
        try:
            s = socket.create_connection(("127.0.0.1", 25575), timeout=5)
            s.sendall(b'{"cmd":"rng_capture","action":{"on":0}}\n')
            s.recv(4096)
            s.sendall(b'{"cmd":"server_step_unlock","action":{}}\n')
            s.recv(4096)
            s.close()
            print("(gate released, capture disarmed)")
        except OSError:
            print("(could not reach the bridge to release the gate)")
        raise
