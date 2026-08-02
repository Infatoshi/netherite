"""Record a minecraftbench trajectory tape through the real game (oracle side).

Reads the PUBLIC tape format (minecraftbench.com benchmarks/rewrite/agent/SPEC.md), replays
it tick-by-tick through the NetheriteMod bridge, and writes the same keyframe artifacts the candidate
must produce: state_<t>.json, world_<t>.mcbd (raw body here; header wrapped by the caller or
--mcbd), frame_<t>.png. Only DERIVED data (dumps, frames, state) ever leaves this repo.

Usage:
  uv run --no-project python java/tape_record.py --tape TAPE.json --outdir DIR [--seed-override S]

Protocol per keyframe: finish step t, send one no-op step so inputs are quiescent, wait
FRAME_SETTLE for a fresh frame (fixed time + clouds off => any frame at a paused tick shows
identical world state), then frame + dumpblocks + obs.
"""

import argparse
import json
import struct
import time
from pathlib import Path

from qrl_client import NetheriteEnv

FRAME_SETTLE = 0.20  # seconds of realtime to let the renderer produce a post-tick frame

ACTION_FIELDS = ["forward", "back", "left", "right", "jump", "sneak", "sprint",
                 "attack", "use", "hotbar", "yaw", "pitch"]


def row_to_action(row):
    a = {}
    for i, f in enumerate(ACTION_FIELDS):
        v = int(row[i])
        if f == "hotbar":
            if v >= 0:
                a[f] = v
        elif v != 0:
            a[f] = v
    return a


def surface_y(raw, cx0, cz0, cx1, x, z):
    """Highest non-air y at column (x, z) in a raw dumpblocks body (no mcbd header)."""
    ncx = cx1 - cx0 + 1
    ci = (z // 16 - cz0) * ncx + (x // 16 - cx0)
    base = ci * 16 * 16 * 256 * 2
    lx, lz = x % 16, z % 16
    for y in range(255, -1, -1):
        off = base + ((y * 16 + lz) * 16 + lx) * 2
        if raw[off] or raw[off + 1]:
            return y
    raise SystemExit(f"no solid block in column ({x},{z})")


def write_mcbd_header(f, seed, cx0, cz0, cx1, cz1, dim=0):
    f.write(b"MCBDUMP1")
    f.write(struct.pack("<QiiiiII", seed & (2**64 - 1), cx0, cz0, cx1, cz1, dim, 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--seed-override", type=int, default=None,
                    help="record the tape at a different seed (time-seed evals)")
    args = ap.parse_args()

    tape = json.loads(Path(args.tape).read_text())
    assert tape["version"] == 1
    seed = args.seed_override if args.seed_override is not None else tape["seed"]
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    e = NetheriteEnv()
    e.s.settimeout(180)  # world launch + chunk load can stall the game thread far past 15s
    # fresh=True: force a brand-new world at this seed (otherwise the bridge re-uses whatever
    # world is loaded - autolaunch seed, stale save with prior edits - and the tape records
    # the wrong world; bitten on the first cross-launch repro attempt)
    o = e.reset({"seed": seed, "mode": "survival", "type": "default",
                 "structures": False, "fresh": True}, timeout=300.0)
    if not o.get("ok"):
        raise SystemExit(f"reset failed: {o}")

    # deterministic arena prep via the server command manager (runcmds - chat-sent
    # commands are silently dropped in the first ticks after reset; bitten)
    def runcmds(cmds, strict=True):
        resp = e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})
        if not resp.get("ok") or (strict and resp.get("failed")):
            raise SystemExit(f"arena prep commands failed: {resp} <- {cmds}")

    # gamerules FIRST, before any settle ticks run: random ticks use the unseeded world
    # RNG, and even the stabilization window is enough to flip a grass block or a leaf
    # decay flag between two creations of the same seed (measured: 2 blocks / 1.6M)
    runcmds(["/gamerule doDaylightCycle false",
             "/gamerule doMobSpawning false",
             "/gamerule doWeatherCycle false",
             "/gamerule doMobLoot false",
             "/gamerule doTileDrops false",
             "/gamerule randomTickSpeed 0",
             "/time set 1000",
             "/weather clear"])

    # vanilla's login placement fires 1-2s AFTER the world becomes joinable and re-teleports
    # the player onto the fuzzed spawn - anything pinned before that gets clobbered (bitten).
    # Step until the position holds still for 10 consecutive ticks.
    last, still = None, 0
    for _ in range(200):
        e.step({})
        o = e.obs()
        pos = (o["x"], o["y"], o["z"])
        still = still + 1 if pos == last else 0
        last = pos
        if still >= 10:
            break
    else:
        raise SystemExit("player position never stabilized after reset")

    # arena: worldgen animals wander on unseeded AI RNG, so they are removed - the graded
    # world is fresh worldgen minus entities, no drops, no random ticks, fixed time, clear
    # weather. Damage immunity for the prep flight: /tp does not reset fallDistance, so the
    # y-200 chunk-load hop otherwise lands with real fall damage (hideParticles=true keeps
    # pixels clean); cleared before the tape starts.
    cmds = ["/effect @p minecraft:resistance 30 4 true"]
    for item in tape.get("inventory", []):
        cmds.append(f"/replaceitem entity @p slot.hotbar.{item['slot']} "
                    f"{item['name']} {item['count']}")
    runcmds(cmds)
    # 1.11 selectors want CamelCase Player; /kill "fails" when nothing matches
    runcmds(["/kill @e[type=!Player]"], strict=False)
    for _ in range(30):  # let mob death animations fully despawn
        e.step({})

    # pin the start pose: vanilla spawn placement is NOT seed-pure (createSpawnPosition
    # walks the world's unseeded RNG - measured 3-4 block drift across creations of the
    # same seed), so the tape carries an explicit start column and y is defined as the
    # surface there: feet at (x+0.5, highest_nonair+1, z+0.5).
    st = tape["start"]
    sx, sz = int(st["x"]), int(st["z"])
    syaw, spitch = float(st.get("yaw", 0)), float(st.get("pitch", 0))
    # chunk-load hover at y 200: re-tp every 4 ticks so fallDistance never accumulates -
    # /tp does NOT reset it, and a long fall carried into the final tp is fatal (bitten:
    # dead player auto-respawned at the fuzzed spawn and the pin silently failed)
    for i in range(20):
        if i % 4 == 0:
            runcmds([f"/tp @p {sx + 0.5} 200 {sz + 0.5} {syaw} {spitch}"])
        e.step({})
    # stragglers in freshly loaded chunks; /kill "fails" when nothing matches, which is fine
    runcmds(["/kill @e[type=!Player]"], strict=False)
    for _ in range(25):
        e.step({})
    probe = outdir / "start_probe.raw"
    db = e._cmd({"cmd": "dumpblocks",
                 "action": {"radius": 1, "file": str(probe.resolve())}})
    if not db.get("ok"):
        raise SystemExit(f"start probe dump failed: {db}")
    ysurf = surface_y(probe.read_bytes(), db["cx0"], db["cz0"], db["cx1"], sx, sz)
    probe.unlink()
    # pin the respawn too: vanilla respawn re-rolls the spawn fuzz, so a mid-tape death
    # would diverge across runs (bitten: identical drowning, different respawn). With a
    # /spawnpoint the death itself becomes deterministic and replayable.
    runcmds([f"/tp @p {sx + 0.5} {ysurf + 1} {sz + 0.5} {syaw} {spitch}",
             f"/spawnpoint @p {sx} {ysurf + 1} {sz}"])
    for _ in range(10):  # settle on the ground
        e.step({})
    runcmds(["/effect @p clear"])
    for _ in range(5):
        e.step({})
    o = e.obs()
    if (abs(o["x"] - (sx + 0.5)) > 0.01 or abs(o["z"] - (sz + 0.5)) > 0.01
            or o.get("health") != 20.0):
        raise SystemExit(f"start pose not pinned: at ({o['x']},{o['y']},{o['z']}) "
                         f"hp={o.get('health')}, wanted ({sx + 0.5},{ysurf + 1},{sz + 0.5})")

    keyframe_every = int(tape.get("keyframe_every", 20))
    radius = int(tape.get("window_radius", 2))
    actions = tape["actions"]
    video_dir = outdir / "video"
    video_dir.mkdir(exist_ok=True)
    manifest = {"tape": Path(args.tape).name, "seed": seed,
                "ticks": len(actions), "keyframe_every": keyframe_every,
                "keyframes": [], "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%S")}

    t0 = time.time()
    for t, row in enumerate(actions):
        obs = e.step(row_to_action(row))
        # per-tick frame for the 20fps display video (never scored, no settle pause)
        e._cmd({"cmd": "frame",
                "action": {"file": str((video_dir / f"v_{t:05d}.png").resolve())}})
        if t % keyframe_every == 0:
            e.step({})  # quiescent inputs for the settle frame
            time.sleep(FRAME_SETTLE)
            fr = e._cmd({"cmd": "frame",
                         "action": {"file": str((outdir / f"frame_{t}.png").resolve())}})
            raw = outdir / f"world_{t}.raw"
            db = e._cmd({"cmd": "dumpblocks",
                         "action": {"radius": radius, "file": str(raw.resolve())}})
            if not (fr.get("ok") and db.get("ok")):
                raise SystemExit(f"keyframe {t} failed: frame={fr} dump={db}")
            mcbd = outdir / f"world_{t}.mcbd"
            with open(mcbd, "wb") as f:
                write_mcbd_header(f, seed, db["cx0"], db["cz0"], db["cx1"], db["cz1"])
                f.write(raw.read_bytes())
            raw.unlink()
            state = dict(obs)
            state["tick"] = t
            (outdir / f"state_{t}.json").write_text(json.dumps(state, sort_keys=True))
            manifest["keyframes"].append(t)
            if t % (keyframe_every * 10) == 0:
                print(f"tick {t}/{len(actions)} ({time.time()-t0:.0f}s)")

    manifest["wall_seconds"] = round(time.time() - t0, 1)
    (outdir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"recorded {len(manifest['keyframes'])} keyframes over {len(actions)} ticks "
          f"in {manifest['wall_seconds']}s -> {outdir}")
    e.close()


if __name__ == "__main__":
    main()
