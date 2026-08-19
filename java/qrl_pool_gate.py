#!/usr/bin/env python3
"""qrl_pool_gate.py - acceptance check for the isolated Java oracle pool.

A pool of N clients is only useful if two things are true, and neither is
obvious from "N processes started":

  1. ISOLATION IS COMPLETE. Every instance has its own display, bridge port,
     game dir and save tree, so the same deterministic scenario run
     concurrently on all N produces BIT-IDENTICAL results. If a shared
     resource leaked - one saves/ dir, one options.txt, one qrl_launch.json -
     the digests diverge or a launch fails the world lock.
  2. FAILURE IS CONTAINED. One wedged instance must be detected, reaped and
     replaced without perturbing its siblings, and the replacement must be
     indistinguishable from the original.

The scenario is lockstep-stepped (wave-1 `server_step_lock`), so nothing here
depends on wall-clock timing: `step_server_locked n` advances exactly n
authoritative ticks and re-parks, and every read is taken on a frozen server.
World time is compared as a DELTA - instances boot seconds apart, so their
absolute totalWorldTime differs by design; what must match is how much the
world moved per step.

Unlike the single-client gates this one does NOT restore the world it touches:
every pool instance owns a throwaway save under its own run dir, which is the
whole point of the isolation.

    uv run --no-project python java/qrl_pool_gate.py -n 4

Exit 0 = every check passed. The pool is fully stopped on the way out (pass
`--keep` to leave it running).
"""
import argparse
import json
import os
import signal
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oracle_pool import (  # noqa: E402
    OraclePool, pid_alive, pid_rss_bytes, pids_in_group, probe_in_world,
)
from qrl_client import NetheriteEnv  # noqa: E402

FAILURES = []
CHECKS = 0

# A fixed cuboid near the flat-world spawn: 8 x 13 x 8 = 832 cells spanning
# bedrock through open air. Anchored to world coordinates, not to the player,
# so the scenario says the same thing on every instance.
BOX = (-4, 0, -4, 3, 12, 3)
# Deterministic mutation with real tick work behind it: three sand blocks left
# hanging in the air. They become falling-block entities, land, and the cuboid
# digest after the fall is a much stronger cross-instance claim than a static
# setblocks would be (it depends on entity ticking and scheduled-tick order).
SAND = 12
STONE = 1
FIXTURE = [[0, 10, 0, SAND, 0], [1, 10, 1, SAND, 0], [2, 10, 2, SAND, 0],
           [-3, 5, -3, STONE, 0]]
STEPS = (4, 16, 40)


def check(label, ok, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  PASS  " if ok else "  FAIL  ") + label + (("   " + detail) if detail else ""))
    if not ok:
        FAILURES.append(label + (("   " + detail) if detail else ""))
    return ok


class Scenario:
    """The identical deterministic run every instance executes."""

    def __init__(self, port):
        self.env = NetheriteEnv(port=port)

    def _cmd(self, cmd, action=None):
        o = self.env._cmd({"cmd": cmd, "action": action or {}}, read_deadline=300.0)
        if not o.get("ok"):
            raise RuntimeError("%s -> %s" % (cmd, o.get("error", o)))
        return o

    def _getblocks(self):
        return self._cmd("getblocks_locked",
                         dict(zip(("x0", "y0", "z0", "x1", "y1", "z1"), BOX)))

    def run(self):
        arm = self._cmd("server_step_lock")
        base = arm["world_time"]
        out = [("worldgen", 0, self._getblocks()["hash"])]
        self._cmd("setblocks_locked", {"blocks": FIXTURE})
        r = self._getblocks()
        out.append(("placed", r["world_time"] - base, r["hash"]))
        for n in STEPS:
            self._cmd("step_server_locked", {"n": n})
            r = self._getblocks()
            out.append(("step%d" % n, r["world_time"] - base, r["hash"]))
        self._cmd("server_step_unlock")
        return out

    def close(self):
        try:
            self.env.close()
        except OSError:
            pass


def run_scenario(inst):
    s = Scenario(inst.port)
    try:
        return s.run()
    finally:
        s.close()


def fmt(rows):
    return " ".join("%s@+%d=%s" % (label, dt, h) for label, dt, h in rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", "--instances", type=int, default=4)
    ap.add_argument("--root", default=None)
    ap.add_argument("--port-base", type=int, default=25600)
    ap.add_argument("--display-base", type=int, default=100)
    ap.add_argument("--seed", type=int, default=917351)
    ap.add_argument("--keep", action="store_true",
                    help="leave the pool running after the gate")
    ap.add_argument("--wedge", type=int, default=None,
                    help="instance to SIGSTOP for the containment proof "
                         "(default: the last one)")
    args = ap.parse_args()

    kw = {"count": args.instances, "port_base": args.port_base,
          "display_base": args.display_base, "seed": args.seed,
          "world_type": "flat"}
    if args.root:
        kw["root"] = args.root
    pool = OraclePool(**kw)
    victim_idx = args.instances - 1 if args.wedge is None else args.wedge

    # ------------------------------------------------------------ pool boot
    print("[boot] %d isolated instances" % args.instances)
    # Every instance starts from an empty saves/, so the worldgen digest is a
    # claim about worldgen and not about whatever a previous run left behind -
    # and the replaced instance (which is always wiped) is comparable to the
    # originals.
    for inst in pool.instances:
        inst.wipe_world()
    t0 = time.monotonic()
    ready = pool.start()
    boot_wall = time.monotonic() - t0
    check("every instance reached in-world readiness",
          len(ready) == args.instances,
          "%d/%d in %.1fs" % (len(ready), args.instances, boot_wall))
    total_rss = 0
    for inst in pool.instances:
        rss = inst.rss_bytes() + pid_rss_bytes(inst.xvfb_pid)
        total_rss += rss
        print("        instance=%d port=%d display=%s ready=%.1fs rss=%.0f MiB"
              % (inst.idx, inst.port, inst.display, ready[inst.idx],
                 rss / 1048576.0))
    print("        pool wall %.1fs, total RSS %.2f GiB (%.2f GiB/instance)"
          % (boot_wall, total_rss / 1073741824.0,
             total_rss / 1073741824.0 / args.instances))

    check("every instance owns a distinct bridge port",
          len({i.port for i in pool.instances}) == args.instances)
    check("every instance owns a distinct X display",
          len({i.display for i in pool.instances}) == args.instances)
    check("every instance owns a distinct game dir",
          len({i.run_dir for i in pool.instances}) == args.instances)
    check("every instance owns a distinct process group",
          len({i.pgid for i in pool.instances}) == args.instances)
    check("no instance allocated the single-client port 25575",
          all(i.port != 25575 for i in pool.instances))

    # --------------------------------------------------------- isolation
    print("\n[isolation] same lockstep scenario on all %d, concurrently"
          % args.instances)
    t0 = time.monotonic()
    results = pool.broadcast(run_scenario)
    span = time.monotonic() - t0
    errs = {k: v for k, v in results.items() if isinstance(v, Exception)}
    check("scenario completed on every instance", not errs, str(errs))
    if errs:
        return report()
    for idx in sorted(results):
        print("        instance=%d %s" % (idx, fmt(results[idx])))
    print("        %d concurrent runs in %.1fs" % (args.instances, span))

    ref = results[0]
    check("worldgen digest is identical on every instance",
          len({r[0][2] for r in results.values()}) == 1,
          "hash=%s" % ref[0][2])
    check("every stage digest is identical on every instance",
          all(results[i] == ref for i in results),
          "%d distinct result vectors"
          % len({json.dumps(r) for r in results.values()}))
    check("world_time deltas are identical on every instance",
          len({tuple(dt for _, dt, _ in r) for r in results.values()}) == 1,
          "deltas=%s" % (tuple(dt for _, dt, _ in ref),))
    check("the scenario actually changed the world",
          ref[0][2] != ref[1][2], "%s -> %s" % (ref[0][2], ref[1][2]))
    check("the sand fell (digest moves again over the stepped ticks)",
          ref[1][2] != ref[-1][2], "%s -> %s" % (ref[1][2], ref[-1][2]))

    # ---------------------------------------------------------- containment
    victim = pool.instances[victim_idx]
    others = [i for i in pool.instances if i.idx != victim_idx]
    print("\n[containment] wedging instance %d with SIGSTOP" % victim_idx)
    game = victim.game_pid()
    check("victim's game JVM located", bool(game), "pid=%s" % game)
    sibling_pids = {i.idx: sorted(pids_in_group(i.pgid)) for i in others}
    os.kill(game, signal.SIGSTOP)

    deadline = time.monotonic() + 60.0
    detected = False
    while time.monotonic() < deadline:
        if [i.idx for i in pool.wedged()] == [victim_idx]:
            detected = True
            break
        time.sleep(0.5)
    check("pool detects exactly the wedged instance",
          detected, "wedged=%s" % [i.idx for i in pool.wedged()])
    check("the wedged instance's port still ACCEPTS connections",
          port_accepts(victim.port),
          "a listening socket is not readiness - the backlog outlives the JVM")
    check("but it cannot serve an observation",
          not probe_in_world(victim.port))

    print("        siblings keep working while %d is stopped" % victim_idx)
    live = {}
    for inst in others:
        live[inst.idx] = run_scenario(inst)
    check("siblings still reach in-world readiness",
          all(i.ready() for i in others))
    # The scenario mutates the world, so a second pass legitimately starts from
    # the first pass's end state - its opening digest is the first pass's
    # closing digest. What the containment claim needs is that the siblings
    # still agree with EACH OTHER, bit for bit, while a peer is wedged.
    sib_ref = live[others[0].idx]
    check("siblings still agree with each other while a peer is wedged",
          all(live[i.idx] == sib_ref for i in others),
          "; ".join("%d:%s" % (i.idx, fmt(live[i.idx])) for i in others))
    check("the second pass continues from the first pass's end state",
          sib_ref[0][2] == ref[-1][2],
          "%s == %s" % (sib_ref[0][2], ref[-1][2]))

    print("        reaping and replacing instance %d" % victim_idx)
    old_pids = sorted(pids_in_group(victim.pgid))
    t0 = time.monotonic()
    replace_seconds = pool.replace(victim)
    print("        replacement ready in %.1fs (pool.replace took %.1fs)"
          % (replace_seconds, time.monotonic() - t0))
    check("a SIGSTOP-ed instance is actually killed (SIGCONT before SIGTERM)",
          not any(pid_alive(p) for p in old_pids),
          "old pids %s" % old_pids)
    check("the replacement is a fresh process group",
          victim.pgid not in old_pids and victim.boots == 2,
          "pgid=%d boots=%d" % (victim.pgid, victim.boots))
    check("no sibling process was signalled",
          all(sorted(pids_in_group(i.pgid)) == sibling_pids[i.idx] for i in others),
          "; ".join("%d:%s->%s" % (i.idx, sibling_pids[i.idx],
                                   sorted(pids_in_group(i.pgid))) for i in others))

    fresh = run_scenario(victim)
    print("        instance=%d (replaced) %s" % (victim_idx, fmt(fresh)))
    check("the replacement reproduces the reference result bit-for-bit",
          fresh == ref, fmt(fresh))

    # ------------------------------------------------------------ shutdown
    if not args.keep:
        print("\n[shutdown] stopping the pool")
        all_pids = []
        for inst in pool.instances:
            all_pids += pids_in_group(inst.pgid)
            if inst.xvfb_pid:
                all_pids.append(inst.xvfb_pid)
        pool.stop_all(verbose=False)
        time.sleep(1.0)
        check("every pool process is gone after stop_all",
              not any(pid_alive(p) for p in all_pids),
              "survivors=%s" % [p for p in all_pids if pid_alive(p)])
        check("no pool port is still listening",
              not any(port_accepts(i.port) for i in pool.instances))
    return report()


def port_accepts(port, host="127.0.0.1"):
    try:
        with socket.create_connection((host, port), timeout=1.0):
            return True
    except OSError:
        return False


def report():
    print("\n%d checks, %d failures" % (CHECKS, len(FAILURES)))
    for f in FAILURES:
        print("  FAILED: " + f)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
