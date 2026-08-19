#!/usr/bin/env python3
"""qrl_capsule_gate.py - acceptance check for the Java->capsule->magma state capsule.

A block cuboid is not a save state.  Two worlds can read back byte-identical from
getblocks_locked and still diverge on the next tick, because the state that
decides what happens next is not in the block array:

  tile entities   a furnace 3 ticks from finishing a smelt and one that just lit
                  are the same block id with the same metadata.
  scheduled ticks WorldServer's pending queue, ordered by
                  (scheduledTime, priority, tickEntryID).
  torch burnout   BlockRedstoneTorch.toggles, a static per-World list of
                  (pos, totalWorldTime).  Eight toggles at one position inside 60
                  ticks burns the torch out for 160.  It is in no block, no
                  metadata, and no chunk NBT - only the JVM heap.

This gate proves three things, in this order:

  1. the capsule CAPTURES that hidden state at a parked lockstep boundary, and
     tags every field class exact / captured_only / unavailable in a
     tamper-evident capability ledger;
  2. restoring the `exact` classes into magma reproduces Java's continuation
     tick-for-tick over a meaningful horizon;
  3. the ledger guards something real - when a field the gate depends on is not
     `exact`, the gate REFUSES (exit 3, naming the field) rather than running,
     and if you force past the refusal the continuation actually diverges.

Point 3 is the reason the other two are worth anything.  A continuation "proof"
over a field the capsule never carried is a proof about nothing, and it prints
PASS just as happily.

The flagship hidden state is the redstone torch, and this gate is honest about
what it can do with it.  Java's burnout history is captured for real (this gate
drives a torch to burnout and reads back the eight toggles plus the 160-tick
recovery callback), but magma/ has no redstone at all - blaze/core/
block_props_table.h:10 lists it as CUT - so the ledger marks it captured_only and
the strict torch gate refuses by name.  The green-path continuation instead runs
on TileEntityFurnace's burn/cook counters, which magma does simulate
(magma/game/furnace_live.c) and which are hidden in exactly the same way.

Design ported from bluecoconut's PR #5; see verify/trace/state_capsule.py for the
field-by-field deltas and why each one differs here.

Run it against a live headless client that already owns port 25575:

    bash java/start_vnc_client.sh          # wait for "[qrl] listening"
    make -C magma magma_game
    uv run --no-project python java/qrl_capsule_gate.py

Exit code 0 = every check passed, 1 = a check failed, 2 = the bridge is not in a
world.  The gate builds its scenes in a cleared box and restores the original
cuboid bytes before it finishes; the [restore] checks prove the restoration.
"""
import argparse
import itertools
import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "verify", "trace"))
import state_capsule as sc
from qrl_client import NetheriteEnv

FAILURES = []
CHECKS = 0
#: Set once the bridge is connected. The bridge serves ONE client at a time, so
#: a crash handler that opens a second socket is refused while this one is still
#: open - the release has to go back down the same connection.
LIVE_ENV = None

FURNACE_NBT = ('{BurnTime:1200,CookTime:0,CookTimeTotal:200,'
               'Items:[{Slot:0b,id:"minecraft:iron_ore",Count:8b},'
               '{Slot:1b,id:"minecraft:coal",Count:8b}]}')
FURNACE_FIELD = "tile_entities.furnace_smelt_state"
TORCH_FIELD = "world.redstone_torch_toggle_history"


def check(label, ok, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  PASS  " if ok else "  FAIL  ") + label + (("   " + detail) if detail else ""))
    if not ok:
        FAILURES.append(label + (("   " + detail) if detail else ""))
    return ok


class Gate:
    """Thin wrapper over the bridge for the locked + capsule command set."""

    def __init__(self, env):
        self.env = env

    def _cmd(self, cmd, action=None, deadline=300.0):
        o = self.env._cmd({"cmd": cmd, "action": action or {}},
                          read_deadline=deadline)
        if not o.get("ok"):
            raise RuntimeError("{} -> {}".format(cmd, o.get("error", o)))
        return o

    def arm(self):     return self._cmd("server_step_lock")
    def disarm(self):  return self._cmd("server_step_unlock")
    def step(self, n): return self._cmd("step_server_locked", {"n": n})
    def getblocks(self, box, path=None):
        a = dict(box)
        if path:
            a["file"] = str(path)
        return self._cmd("getblocks_locked", a)
    def setblocks(self, blocks):
        return self._cmd("setblocks_locked", {"blocks": blocks})
    def capsule(self, box, path=None):
        a = dict(box)
        if path:
            a["blocks_file"] = str(path)
        return self._cmd("capsule_dump_locked", a)
    def run(self, text):
        return self._cmd("cmd", {"text": text})


def box_cells(box):
    return [(x, y, z)
            for y in range(box["y0"], box["y1"] + 1)
            for z in range(box["z0"], box["z1"] + 1)
            for x in range(box["x0"], box["x1"] + 1)]


def decode(raw, box):
    """Little-endian u16 id<<4|meta, y-major then z then x -> [x,y,z,id,meta]."""
    values = struct.unpack(f"<{len(raw) // 2}H", raw)
    nx = box["x1"] - box["x0"] + 1
    nz = box["z1"] - box["z0"] + 1
    out = []
    for i, v in enumerate(values):
        x = box["x0"] + i % nx
        y = box["y0"] + i // (nx * nz)
        z = box["z0"] + (i // nx) % nz
        out.append([x, y, z, v >> 4, v & 0xF])
    return out


def clear_box(g, box):
    g.setblocks([[x, y, z, 0, 0] for x, y, z in box_cells(box)])


def furnace_trace_row(entry):
    """The four hidden counters plus the observable smelt output."""
    return (entry["burn_time"], entry["cook_time"], entry["total_cook_time"],
            sum(i["count"] for i in entry["items"] if i["slot"] == 2))


def magma_trace(path):
    rows = []
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        f = row.get("furnace")
        rows.append(None if not f else
                    (f["burn"], f["cook"], f["cook_total"], f["output"][1]))
    return rows


def run_magma(capsule_dir, out_dir, ticks):
    """Emit the capsule's tick-zero restore events and run magma headless."""
    script = out_dir / "restore.jsonl"
    state = out_dir / "magma_state.jsonl"
    count = sc.emit_magma(capsule_dir, script)
    binary = os.path.join(REPO, "magma", "magma_game")
    proc = subprocess.run(
        [binary, "--headless", "--world", "superflat", "--seed", "0",
         "--ticks", str(ticks), "--script", str(script),
         "--state-out", str(state), "--render", "off", "--pace", "unlimited"],
        capture_output=True, text=True, check=False)
    return count, proc, state


def capsule_cli(*args):
    return subprocess.run(
        [sys.executable, os.path.join(REPO, "verify", "trace", "state_capsule.py"),
         *args], capture_output=True, text=True, check=False)


# ---- scenes ----------------------------------------------------------------

def torch_scene(g, tmp, anchor):
    """Drive a redstone torch to burnout and capsule the hidden state.

    The scene is restored in a `finally`: this gate writes real blocks into the
    shared run/ world, and a mid-scene exception must not leave a redstone torch
    standing in it.
    """
    tx, ty, tz = anchor
    box = {"x0": tx - 1, "y0": ty - 1, "z0": tz - 1,
           "x1": tx + 1, "y1": ty + 1, "z1": tz + 1}
    original = g.getblocks(box, tmp / "torch_orig.bin")
    orig_raw = (tmp / "torch_orig.bin").read_bytes()
    try:
        clear_box(g, box)
        # Stone support with a lit floor-standing torch (meta 5) on top.
        g.setblocks([[tx, ty - 1, tz, 1, 0], [tx, ty, tz, 76, 5]])
        g.step(4)

        fixture = pathlib.Path(REPO) / "verify" / "trace" / "fixtures" / \
            "redstone_torch_floor_burnout.sequence"
        rows = sc.parse_sequence(fixture.read_text(encoding="utf-8"))
        now = 0
        for row in rows:
            if row["tick"] > now:
                g.step(row["tick"] - now)
                now = row["tick"]
            g.setblocks([[tx + row["dx"], ty + row["dy"], tz + row["dz"],
                          row["block"], row["meta"]]])
        g.step(6)

        dump = g.capsule(box, tmp / "torch_blocks.u16le")
        g.getblocks({"x0": tx, "y0": ty, "z0": tz,
                     "x1": tx, "y1": ty, "z1": tz}, tmp / "torch_cell.bin")
        torch_id = struct.unpack(
            "<H", (tmp / "torch_cell.bin").read_bytes())[0] >> 4
        at_torch = [t for t in dump["redstone_torch_toggles"]
                    if (t["x"], t["y"], t["z"]) == (tx, ty, tz)]
    finally:
        g.setblocks(decode(orig_raw, box))
    restored = g.getblocks(box)["hash"] == original["hash"]
    return dump, restored, at_torch, torch_id, len(rows)


def furnace_scene(g, tmp, anchor, warmup, horizon):
    """Light a furnace, run it `warmup` ticks, capsule it, then trace `horizon`.

    The capsule is taken mid-smelt on purpose: burn_time and cook_time are both
    partway through, so a restore that ignores them cannot possibly land the
    smelt-completion tick in the right place.
    """
    fx, fy, fz = anchor
    box = {"x0": fx - 1, "y0": fy, "z0": fz - 1,
           "x1": fx + 1, "y1": fy + 1, "z1": fz + 1}
    original = g.getblocks(box, tmp / "furnace_orig.bin")
    orig_raw = (tmp / "furnace_orig.bin").read_bytes()
    try:
        clear_box(g, box)
        g.disarm()
        # /setblock carries the TileEntity NBT; setblocks_locked cannot.
        # CookTimeTotal must be explicit - readFromNBT does not derive it, and a
        # furnace loaded with CookTimeTotal=0 never completes a smelt (cookTime
        # is incremented BEFORE the == comparison, so it can never equal zero)
        # and just counts up forever. That degenerate state was reached by
        # accident while building this gate and is worth the belt-and-braces.
        g.run(f"/setblock {fx} {fy} {fz} minecraft:furnace 2 replace {FURNACE_NBT}")
        g.arm()
        g.step(warmup)

        dump = g.capsule(box, tmp / "furnace_blocks.u16le")
        trace = []
        for _ in range(horizon):
            g.step(1)
            entries = g.capsule(box)["tile_entities"]
            trace.append(furnace_trace_row(entries[0]) if entries else None)
    finally:
        # Clear the furnace first so its TileEntity is destroyed, then put the
        # original cells back.
        g.setblocks([[fx, fy, fz, 0, 0]])
        g.setblocks(decode(orig_raw, box))
    restored = g.getblocks(box)["hash"] == original["hash"]
    return dump, trace, restored, box


# ---- main ------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=25575)
    ap.add_argument("--tmp", default="/tmp/qrl_capsule_gate")
    ap.add_argument("--warmup", type=int, default=60,
                    help="ticks to run the furnace before the capsule")
    ap.add_argument("--horizon", type=int, default=160,
                    help="continuation ticks compared on both sides")
    args = ap.parse_args()

    tmp = pathlib.Path(args.tmp)
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True, exist_ok=True)

    global LIVE_ENV
    env = NetheriteEnv(port=args.port)
    LIVE_ENV = env
    st = env.stats()
    if not st.get("ok"):
        print(f"bridge is not in a world: {st}")
        return 2
    print(f"connected: {json.dumps({k: st[k] for k in list(st)[:6]})}")
    g = Gate(env)

    armed = g.arm()
    player = armed["player"]
    px, py, pz = int(player["x"]), int(player["y"]), int(player["z"])
    print(f"        player at {px},{py},{pz}; gate completed={armed['completed']}")

    # ---- capture ----------------------------------------------------------
    print("\n[capture] one pre-tick capsule off the parked WorldServer")
    probe = {"x0": px + 3, "y0": py, "z0": pz + 3,
             "x1": px + 5, "y1": py + 2, "z1": pz + 5}
    d1 = g.capsule(probe, tmp / "probe.bin")
    d2 = g.capsule(probe)
    check("capsule_dump_locked returns the capsule schema",
          d1.get("schema") == sc.SCHEMA and d1.get("phase") == "pre_tick",
          f"{d1.get('schema')} {d1.get('phase')}")
    check("two reads at the same parked boundary are byte-identical",
          d1["hash"] == d2["hash"], f"{d1['hash']} vs {d2['hash']}")
    check("the capsule read consumes no tick",
          d1["time"]["total_time"] == d2["time"]["total_time"],
          f"total_time {d1['time']['total_time']}")
    check("block payload length matches the declared box",
          (tmp / "probe.bin").stat().st_size == d1["cells"] * 2,
          f"{(tmp / 'probe.bin').stat().st_size} bytes for {d1['cells']} cells")
    check("all three hidden-state classes were readable",
          d1["captured_scheduled_ticks"] and d1["captured_tile_entities"]
          and d1["captured_torch_toggles"],
          f"sched={d1['captured_scheduled_ticks']} "
          f"te={d1['captured_tile_entities']} "
          f"torch={d1['captured_torch_toggles']}")
    check("RNG cursors ride the wave-2 four-stream snapshot",
          all(k in d1["world_rng"] for k in
              ("world_seed48", "math_seed48", "block_seed48", "update_lcg")),
          f"world_seed48={d1['world_rng']['world_seed48']}")

    # ---- torch: the flagship hidden state ---------------------------------
    print("\n[torch] redstone torch burnout - state in no block and no NBT")
    t_dump, t_restored, t_toggles, t_id, t_rows = torch_scene(
        g, tmp, (px - 6, py, pz - 6))
    check("burnout drive sequence parsed from the PR fixture format",
          t_rows == 16, f"{t_rows} rows")
    check("eight toggles were recorded at the torch position",
          len(t_toggles) == 8, f"{len(t_toggles)} toggles")
    check("toggle history is chronological",
          all(a["time"] <= b["time"]
              for a, b in itertools.pairwise(t_toggles)),
          f"t={t_toggles[0]['time']}..{t_toggles[-1]['time']}")
    span = t_toggles[-1]["time"] - t_toggles[0]["time"] if t_toggles else -1
    check("all eight land inside vanilla's 60-tick prune window",
          0 <= span < 60, f"span {span} ticks")
    check("the torch actually burned out (lit 76 -> unlit 75)",
          t_id == 75, f"block id {t_id}")
    recovery = [s for s in t_dump["scheduled_ticks"] if s["block"] in (75, 76)]
    check("the 160-tick burnout recovery callback was captured",
          len(recovery) == 1
          and recovery[0]["time"] - t_toggles[-1]["time"] == 160,
          f"due {recovery[0]['time'] if recovery else None} = last toggle "
          f"{t_toggles[-1]['time'] if t_toggles else None} + 160")
    check("scheduled entry carries the priority/order tiebreak fields",
          recovery and set(recovery[0]) ==
          {"x", "y", "z", "block", "time", "priority", "order"},
          f"priority={recovery[0]['priority']} order={recovery[0]['order']}"
          if recovery else "")
    check("the torch scene was restored to its original digest", t_restored)

    torch_capsule = sc.create_capsule(
        t_dump, (tmp / "torch_blocks.u16le").read_bytes(), tmp / "torch_capsule")

    # ---- ledger -----------------------------------------------------------
    print("\n[ledger] capability classes are a contract, not a comment")
    f_dump, j_trace, f_restored, _f_box = furnace_scene(
        g, tmp, (px + 6, py, pz + 6), args.warmup, args.horizon)
    check("the furnace scene was restored to its original digest", f_restored)
    capsule = sc.create_capsule(
        f_dump, (tmp / "furnace_blocks.u16le").read_bytes(), tmp / "capsule")
    manifest, _ = sc.validate_capsule(capsule)
    caps = manifest["capabilities"]
    check("furnace smelt state is exact (magma restores it)",
          caps[FURNACE_FIELD] == sc.EXACT, caps[FURNACE_FIELD])
    check("torch burnout history is captured_only (magma has no redstone)",
          caps[TORCH_FIELD] == sc.CAPTURED_ONLY, caps[TORCH_FIELD])
    check("aggregate RNG cursors are captured_only, per wave-2",
          caps["world.rng_cursors"] == sc.CAPTURED_ONLY,
          caps["world.rng_cursors"])
    check("audio is unavailable and stays that way",
          caps["audio.sound_events"] == sc.UNAVAILABLE,
          caps["audio.sound_events"])
    check("every ledger value is one of the three declared classes",
          all(v in sc.CLASSES for v in caps.values()),
          f"{len(caps)} field classes")
    check("the ledger covers the whole registry",
          set(caps) == set(sc.BASE_CAPABILITIES),
          f"{len(caps)} entries")

    tampered = tmp / "tampered"
    shutil.copytree(capsule, tampered)
    doc = json.loads((tampered / sc.MANIFEST_FILE).read_text(encoding="utf-8"))
    doc["capabilities"][TORCH_FIELD] = sc.EXACT
    (tampered / sc.MANIFEST_FILE).write_text(
        json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    tamper = capsule_cli("validate", "--capsule", str(tampered))
    check("hand-promoting a class in the manifest is rejected",
          tamper.returncode == 2 and "altered" in tamper.stderr,
          tamper.stderr.strip().splitlines()[-1] if tamper.stderr else "")

    corrupt = tmp / "corrupt"
    shutil.copytree(capsule, corrupt)
    raw = (corrupt / sc.BLOCK_FILE).read_bytes()
    # Flip a bit rather than zeroing: the box is mostly air, so writing 0x00
    # over byte 0 is a no-op and the "corruption" would silently validate.
    (corrupt / sc.BLOCK_FILE).write_bytes(
        bytes([raw[0] ^ 0xFF]) + raw[1:])
    bad = capsule_cli("validate", "--capsule", str(corrupt))
    check("a corrupted block payload fails its sha256",
          bad.returncode == 2 and "sha256 mismatch" in bad.stderr,
          bad.stderr.strip().splitlines()[-1] if bad.stderr else "")

    # ---- refuse-if-incomplete --------------------------------------------
    print("\n[refuse] a gate that needs a non-exact field must not run")
    torch_gate = capsule_cli("validate", "--capsule", str(torch_capsule),
                             "--require", TORCH_FIELD)
    check("the strict torch gate is BLOCKED with exit 3",
          torch_gate.returncode == 3, f"rc={torch_gate.returncode}")
    check("the refusal names the missing capability",
          TORCH_FIELD in torch_gate.stderr
          and "captured_only" in torch_gate.stderr,
          torch_gate.stderr.strip().splitlines()[-1] if torch_gate.stderr else "")
    check("it is a refusal, not a silent skip and not a fake PASS",
          "BLOCKED" in torch_gate.stderr and not torch_gate.stdout.strip(),
          f"stdout={torch_gate.stdout.strip()!r}")
    furnace_gate = capsule_cli("validate", "--capsule", str(capsule),
                               "--require", FURNACE_FIELD)
    check("the furnace gate's dependency IS exact, so it runs",
          furnace_gate.returncode == 0, furnace_gate.stdout.strip())
    complete = capsule_cli("validate", "--capsule", str(capsule),
                           "--require-complete")
    check("the PR's global --require-complete still refuses a partial capsule",
          complete.returncode == 3 and "world.rng_cursors" in complete.stderr,
          f"rc={complete.returncode}")

    # ---- restore + green continuation ------------------------------------
    print("\n[restore] Java -> capsule -> magma, one direction")
    events, proc, state_path = run_magma(capsule, tmp, args.horizon)
    check("magma accepted the emitted tick-zero restore events",
          proc.returncode == 0,
          f"rc={proc.returncode}" if proc.returncode == 0 else
          f"rc={proc.returncode} {proc.stderr.strip().splitlines()[-1][:160]}")
    check("the restore emits a bounded event list",
          events == 27, f"{events} events")
    m_trace = magma_trace(state_path)
    check("magma produced one state row per continuation tick",
          len(m_trace) == args.horizon, f"{len(m_trace)} rows")
    seed = f_dump["tile_entities"][0]
    check("magma's furnace starts from the captured hidden counters",
          m_trace[0] is not None
          and m_trace[0][0] == seed["burn_time"] - 1
          and m_trace[0][1] == seed["cook_time"] + 1
          and m_trace[0][2] == seed["total_cook_time"],
          f"capsule burn={seed['burn_time']} cook={seed['cook_time']} "
          f"total={seed['total_cook_time']} -> magma t0 {m_trace[0]}")

    print("\n[green] mid-run continuation, Java and magma stepped in lockstep")
    matched = sum(1 for a, b in zip(j_trace, m_trace) if a == b)
    first_bad = next((i for i, (a, b) in enumerate(zip(j_trace, m_trace))
                      if a != b), None)
    check("every continuation tick matches (burn, cook, total, output)",
          matched == args.horizon and first_bad is None,
          f"{matched}/{args.horizon} ticks"
          + (f", first divergence at {first_bad}: java={j_trace[first_bad]} "
             f"magma={m_trace[first_bad]}" if first_bad is not None else ""))
    smelt = [i for i in range(1, len(j_trace))
             if j_trace[i][3] > j_trace[i - 1][3]]
    check("the horizon contains a real smelt-completion event",
          len(smelt) >= 1,
          f"output increments at tick {smelt[0]}" if smelt else "none")
    if smelt:
        i = smelt[0]
        check("magma completes the smelt on the same tick, not just eventually",
              m_trace[i] == j_trace[i] and m_trace[i - 1] == j_trace[i - 1],
              f"tick {i}: java {j_trace[i]} magma {m_trace[i]}")
        check("the completion tick is far from the capsule boundary",
              i > 100, f"{i} ticks after restore")

    # ---- negative control: drop the field --------------------------------
    print("\n[negative] drop the hidden state and the ledger must catch it")
    dropped = sc.create_capsule(
        f_dump, (tmp / "furnace_blocks.u16le").read_bytes(),
        tmp / "dropped", drop=(FURNACE_FIELD,))
    d_manifest, _ = sc.validate_capsule(dropped)
    check("dropping the field downgrades it to unavailable",
          d_manifest["capabilities"][FURNACE_FIELD] == sc.UNAVAILABLE,
          d_manifest["capabilities"][FURNACE_FIELD])
    refused = capsule_cli("validate", "--capsule", str(dropped),
                          "--require", FURNACE_FIELD)
    check("the strict furnace gate is now BLOCKED with exit 3",
          refused.returncode == 3, f"rc={refused.returncode}")
    check("the refusal names the dropped field and its class",
          FURNACE_FIELD in refused.stderr and "unavailable" in refused.stderr,
          refused.stderr.strip().splitlines()[-1] if refused.stderr else "")

    print("\n[negative] force past the refusal - the continuation must diverge")
    d_events, d_proc, d_state = run_magma(dropped, tmp / "dropped_run",
                                          args.horizon)
    check("the dropped capsule emits no furnace restore at all",
          d_events < events, f"{d_events} events vs {events}")
    check("magma still runs (so divergence is physics, not a crash)",
          d_proc.returncode == 0, f"rc={d_proc.returncode}")
    d_trace = magma_trace(d_state)
    d_matched = sum(1 for a, b in zip(j_trace, d_trace) if a == b)
    d_first = next((i for i, (a, b) in enumerate(zip(j_trace, d_trace))
                    if a != b), None)
    check("forced past the refusal, the continuation really does diverge",
          d_first is not None,
          f"first divergence at tick {d_first}: java={j_trace[d_first]} "
          f"magma={d_trace[d_first]}" if d_first is not None else "NO DIVERGENCE")
    check("the divergence is immediate, not a slow drift",
          d_first == 0, f"first bad tick {d_first}")
    check("the ledger was therefore guarding something real",
          d_matched < matched, f"{d_matched}/{args.horizon} ticks match "
          f"without the field vs {matched}/{args.horizon} with it")

    # ---- guards -----------------------------------------------------------
    print("\n[guards] the capsule command refuses an unparked server")
    g.disarm()
    bad = env._cmd({"cmd": "capsule_dump_locked", "action": probe})
    check("capsule_dump_locked errors when the gate is off",
          bad.get("ok") is False
          and "server_step_lock" in str(bad.get("error", "")),
          str(bad.get("error")))

    print(f"\n{CHECKS} checks, {len(FAILURES)} failures")
    for f in FAILURES:
        print(f"  FAILED: {f}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}")
        # Never leave the server parked because the harness blew up. The scene
        # restores are already in `finally` blocks; this only releases the gate.
        released = False
        if LIVE_ENV is not None:
            try:
                LIVE_ENV._cmd({"cmd": "server_step_unlock", "action": {}})
                released = True
            except (OSError, RuntimeError, ValueError) as unlock_exc:
                print(f"(live unlock failed: {unlock_exc})")
        if not released:
            try:
                s = socket.create_connection(("127.0.0.1", 25575), timeout=5)
                s.sendall(b'{"cmd":"server_step_unlock","action":{}}\n')
                s.recv(4096)
                s.close()
                released = True
            except OSError:
                pass
        print("(gate released)" if released
              else "(could not reach the bridge to release the gate)")
        raise
