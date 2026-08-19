#!/usr/bin/env python3
"""qrl_lockstep_gate.py - acceptance check for the NetheriteMod server tick lockstep gate.

The integrated server runs on its own thread, so an ordinary bridge command
straddles zero, one, or two authoritative ticks. Two consequences:

  * a read is racy - back-to-back `getblocks` are not guaranteed to be the same
    world state, only "whatever the server happened to be at";
  * you cannot advance the world by an exact number of ticks.

`server_step_lock` parks the server thread inside ServerTickEvent.START.
`step_server_locked n` permits exactly n ticks and returns only once the server
has both executed them AND re-parked, so a following *_locked command reads or
writes a frozen world without being charged an extra tick.

Run it against a live headless client that already owns port 25575:

    bash java/start_vnc_client.sh          # wait for "[qrl] listening"
    uv run --no-project python java/qrl_lockstep_gate.py

Exit code 0 = every check passed. The script restores every block and player
field it touches before it disarms the gate.
"""
import argparse
import json
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qrl_client import NetheriteEnv

FAILURES = []
CHECKS = 0


def check(label, ok, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  PASS  " if ok else "  FAIL  ") + label + (("   " + detail) if detail else ""))
    if not ok:
        FAILURES.append(label + (("   " + detail) if detail else ""))
    return ok


def f64bits(v):
    """Raw IEEE-754 double word as the 16-hex-digit string the bridge speaks."""
    return "{:016x}".format(struct.unpack("<Q", struct.pack("<d", v))[0])


def f32bits(v):
    return "{:08x}".format(struct.unpack("<I", struct.pack("<f", v))[0])


def bits_to_f64(h):
    return struct.unpack("<d", struct.pack("<Q", int(h, 16)))[0]


class Gate:
    """Thin wrapper over the bridge for the locked command set."""

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

    def getblocks(self, box, path=None):
        a = dict(zip(("x0", "y0", "z0", "x1", "y1", "z1"), box))
        if path:
            a["file"] = path
        return self._cmd("getblocks_locked", a)

    def setblocks(self, blocks):
        return self._cmd("setblocks_locked", {"blocks": blocks})

    def setplayer(self, **kw):
        return self._cmd("setplayer_locked", kw)

    def getblocks_unlocked(self, box, path):
        a = dict(zip(("x0", "y0", "z0", "x1", "y1", "z1"), box))
        a["file"] = path
        return self._cmd("getblocks", a)


def read_dump(path):
    with open(path, "rb") as fh:
        return fh.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--burst", type=int, default=12,
                    help="reads per drift burst in the before/after comparison")
    ap.add_argument("--tmp", default="/tmp/qrl_lockstep")
    args = ap.parse_args()
    os.makedirs(args.tmp, exist_ok=True)

    env = NetheriteEnv(port=args.port)
    g = Gate(env)

    st = env.stats()
    if not st.get("ok"):
        print(f"bridge is not in a world: {st}")
        return 2
    print(f"connected: {json.dumps({k: st[k] for k in list(st)[:6]})}")

    # Work well away from the player so the fixture cannot perturb physics, but
    # inside the loaded chunk radius so the server actually owns the cells.
    arm0 = g.arm()
    px = int(arm0["player"]["x"])
    py = int(arm0["player"]["y"])
    pz = int(arm0["player"]["z"])
    g.disarm()
    tx, ty, tz = px + 6, py + 3, pz + 6
    box = (tx, ty, tz, tx + 1, ty + 1, tz + 1)   # 2x2x2 cuboid, 16 bytes
    print(f"player at ({px},{py},{pz}); fixture box {box}")

    # ---------------------------------------------------------------- BEFORE
    # Baseline: with the gate off, how many authoritative ticks slip past while
    # the bridge performs a fixed burst of reads? Bracketing the burst with the
    # gate gives an exact server-side measurement of the uncontrolled interval.
    print("\n[before] unlocked stepping: server ticks are uncontrolled")
    unlocked_drifts = []
    unlocked_pair_gap = []
    for _ in range(3):
        w0 = g.arm()["world_time"]
        g.disarm()
        for _ in range(args.burst):
            g.getblocks_unlocked(box, os.path.join(args.tmp, "unlocked.bin"))
        w1 = g.arm()["world_time"]
        g.disarm()
        unlocked_drifts.append(w1 - w0)
    # and: two back-to-back unlocked reads are not at the same server tick
    for _ in range(3):
        w0 = g.arm()["world_time"]
        g.disarm()
        g.getblocks_unlocked(box, os.path.join(args.tmp, "u_a.bin"))
        g.getblocks_unlocked(box, os.path.join(args.tmp, "u_b.bin"))
        w1 = g.arm()["world_time"]
        g.disarm()
        unlocked_pair_gap.append(w1 - w0)
    print(f"        {args.burst} unlocked reads advanced the server by "
          f"{unlocked_drifts} ticks (3 runs)")
    print(f"        2 back-to-back unlocked reads spanned {unlocked_pair_gap} ticks (3 runs)")
    check("unlocked burst drifts (no exact tick control)",
          any(d != 0 for d in unlocked_drifts),
          f"drifts={unlocked_drifts}")
    check("unlocked back-to-back reads are not tick-aligned",
          any(d != 0 for d in unlocked_pair_gap),
          f"spans={unlocked_pair_gap}")

    # ----------------------------------------------------------------- AFTER
    print("\n[after] server_step_lock: exact tick control")
    a = g.arm()
    check("server_step_lock parks the server", a.get("parked") is True,
          "completed={} world_time={}".format(a.get("completed"), a.get("world_time")))
    base_completed = a["completed"]
    w_park = a["world_time"]

    # The same burst, locked: zero ticks may pass.
    for _ in range(args.burst):
        g.getblocks(box)
    after_burst = g.step(0)
    check(f"{args.burst} locked reads advance the server by 0 ticks",
          after_burst["world_time"] == w_park,
          "world_time {} -> {}".format(w_park, after_burst["world_time"]))

    # step exactly 4
    s4 = g.step(4)
    check("step_server_locked(4) completes exactly 4 ticks",
          s4["completed"] == base_completed + 4 and s4["stepped"] == 4,
          "completed {} -> {}".format(base_completed, s4["completed"]))
    check("step_server_locked(4) advances world_time by exactly 4",
          s4["world_time"] == w_park + 4,
          "world_time {} -> {}".format(w_park, s4["world_time"]))
    check("server is re-parked after the step", s4.get("parked") is True)

    # exact counts across a sweep
    for n in (1, 2, 8, 20):
        before = g.step(0)
        got = g.step(n)
        check(f"step_server_locked({n}) advances world_time by exactly {n}",
              got["world_time"] == before["world_time"] + n,
              f"{before['world_time']} -> {got['world_time']}")

    # two back-to-back locked reads must be byte-identical
    f_a = os.path.join(args.tmp, "locked_a.bin")
    f_b = os.path.join(args.tmp, "locked_b.bin")
    r_a = g.getblocks(box, f_a)
    r_b = g.getblocks(box, f_b)
    check("getblocks_locked twice: identical digest",
          r_a["hash"] == r_b["hash"], "hash={}".format(r_a["hash"]))
    check("getblocks_locked twice: identical bytes",
          read_dump(f_a) == read_dump(f_b))
    check("getblocks_locked twice: same gate tick",
          r_a["gate_completed"] == r_b["gate_completed"]
          and r_a["world_time"] == r_b["world_time"],
          "completed={} world_time={}".format(r_a["gate_completed"], r_a["world_time"]))
    check("getblocks_locked consumes no tick",
          g.step(0)["world_time"] == r_b["world_time"])

    original = read_dump(f_a)
    print(f"        original fixture cuboid: {original.hex()}")

    # ------------------------------------------------ locked write, no tick
    # Place stone (id 1) at the low corner of the cuboid.
    stone = [[tx, ty, tz, 1, 0]]
    w_pre = g.step(0)["world_time"]
    sb = g.setblocks(stone)
    check("setblocks_locked reports the write", sb["set"] == 1)
    s0 = g.step(0)
    check("setblocks_locked consumes no tick", s0["world_time"] == w_pre,
          "world_time {} -> {}".format(w_pre, s0["world_time"]))

    f_c = os.path.join(args.tmp, "locked_c.bin")
    r_c = g.getblocks(box, f_c)
    placed = read_dump(f_c)
    first_cell = struct.unpack("<H", placed[0:2])[0]
    check("block is present after step 0 (id<<4|meta == 16)",
          first_cell == (1 << 4) | 0, f"first cell = 0x{first_cell:04x}")
    check("only the written cell changed",
          placed[2:] == original[2:],
          f"{placed.hex()} vs {original.hex()}")

    s_after4 = g.step(4)
    r_d = g.getblocks(box)
    check("step_server_locked(4) after the write advances exactly 4",
          s_after4["world_time"] == r_c["world_time"] + 4,
          "world_time {} -> {}".format(r_c["world_time"], s_after4["world_time"]))
    check("block survives 4 ticks", r_d["hash"] == r_c["hash"],
          "hash {} vs {}".format(r_c["hash"], r_d["hash"]))

    # ------------------------------------------------- IEEE-754 transport
    print("\n[bits] raw IEEE-754 float transport")
    p0 = g.step(0)["player"]
    saved = {k: p0[k] for k in ("x_bits", "y_bits", "z_bits", "vx_bits", "vy_bits",
                                "vz_bits", "yaw_bits", "pitch_bits",
                                "fall_distance_bits", "on_ground")}
    # Values chosen so their shortest decimal form is NOT their exact value when
    # naively reformatted: full 17-significant-digit doubles and a float that is
    # not representable in binary at all.
    vy = -0.0784000015258789
    vx = 0.30000000000000004      # 0.1 + 0.2
    vz = 1.0000000000000002       # 1 + 1ulp
    fall = 3.1400001
    sp = g.setplayer(vx_bits=f64bits(vx), vy_bits=f64bits(vy), vz_bits=f64bits(vz),
                     fall_distance_bits=f32bits(fall))
    rp = sp["player"]
    check("vx round-trips bit-exactly", rp["vx_bits"] == f64bits(vx),
          "{} vs {}".format(rp["vx_bits"], f64bits(vx)))
    check("vy round-trips bit-exactly", rp["vy_bits"] == f64bits(vy),
          "{} vs {}".format(rp["vy_bits"], f64bits(vy)))
    check("vz (1+1ulp) round-trips bit-exactly", rp["vz_bits"] == f64bits(vz),
          "{} vs {}".format(rp["vz_bits"], f64bits(vz)))
    check("fall_distance float32 round-trips bit-exactly",
          rp["fall_distance_bits"] == f32bits(fall),
          "{} vs {}".format(rp["fall_distance_bits"], f32bits(fall)))
    check("setplayer_locked consumes no tick",
          g.step(0)["world_time"] == sp["world_time"],
          "world_time {} -> {}".format(sp["world_time"], g.step(0)["world_time"]))

    # ------------------------------------------------------------- teardown
    print("\n[restore] putting the world back")
    orig_id = struct.unpack("<H", original[0:2])[0]
    g.setblocks([[tx, ty, tz, orig_id >> 4, orig_id & 0xF]])
    g.setplayer(vx_bits=saved["vx_bits"], vy_bits=saved["vy_bits"],
                vz_bits=saved["vz_bits"],
                fall_distance_bits=saved["fall_distance_bits"],
                on_ground=1 if saved["on_ground"] else 0)
    r_r = g.getblocks(box)
    check("restore returns the cuboid to its original digest",
          r_r["hash"] == r_a["hash"], "{} vs {}".format(r_r["hash"], r_a["hash"]))

    # ------------------------------------------------------ negative checks
    print("\n[guards] locked commands refuse an unparked server")
    g.disarm()
    bad = env._cmd({"cmd": "getblocks_locked",
                    "action": dict(zip(("x0", "y0", "z0", "x1", "y1", "z1"), box))})
    check("getblocks_locked errors when the gate is off",
          bad.get("ok") is False and "server_step_lock" in str(bad.get("error", "")),
          str(bad.get("error")))
    bad = env._cmd({"cmd": "step_server_locked", "action": {"n": 1}})
    check("step_server_locked errors when the gate is off",
          bad.get("ok") is False, str(bad.get("error")))

    # the server must be free-running again
    w0 = g.arm()["world_time"]
    g.disarm()
    time.sleep(0.6)
    w1 = g.arm()["world_time"]
    g.disarm()
    check("server free-runs again after unlock", w1 > w0,
          f"world_time {w0} -> {w1} over ~0.6s")

    print(f"\n{CHECKS} checks, {len(FAILURES)} failures")
    for f in FAILURES:
        print(f"  FAILED: {f}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}")
        # Never leave the server parked because the harness blew up.
        try:
            s = socket.create_connection(("127.0.0.1", 25575), timeout=5)
            s.sendall(b'{"cmd":"server_step_unlock","action":{}}\n')
            s.recv(4096)
            s.close()
            print("(gate released)")
        except OSError:
            print("(could not reach the bridge to release the gate)")
        raise
