#!/usr/bin/env python3
"""Stage a real saved player-in-minecart relationship in parked Java."""

from __future__ import annotations

import argparse
import json
import math
import os
import pathlib
import subprocess
import sys
import time


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
START = ROOT / "java" / "start_oracle_instance.sh"
sys.path.insert(0, str(HERE))
import save_fork  # noqa: E402


class StageError(RuntimeError):
    pass


def _oracle(action: str, instance: int, seed: int,
            environment: dict[str, str]) -> None:
    command = ["bash", str(START), action, str(instance)]
    if action == "start":
        command.append(str(seed))
    result = subprocess.run(command, cwd=ROOT, env=environment, check=False)
    if result.returncode:
        raise StageError(
            f"oracle instance {action} failed with rc={result.returncode}")


def stage(source: pathlib.Path, output: pathlib.Path,
          instance: int, seed: int, save08: bool = False) -> None:
    save_fork.validate_snapshot(source)
    if output.exists():
        raise StageError(f"output already exists: {output}")
    run_root = output.parent / f".{output.name}.oracle-{os.getpid()}"
    environment = dict(os.environ)
    environment.update({
        "JAVA_HOME": "/usr/lib/jvm/java-8-openjdk-amd64",
        "ORACLE_POOL_OUT_ROOT": str(run_root),
        "ORACLE_POOL_SAVE_SOURCE": str(source / "save"),
        "ORACLE_POOL_USERNAME": "PoolPlayer0",
        "ORACLE_POOL_WORLD_TYPE": "flat",
        "ORACLE_POOL_WAIT": "1",
        "TMPDIR": str(ROOT / ".tmp"),
        "UV_CACHE_DIR": str(pathlib.Path.home() / ".cache" / "uv"),
    })
    port = int(environment.get("ORACLE_POOL_PORT_BASE", "25600")) + instance
    started = False
    locked = False
    try:
        _oracle("start", instance, seed, environment)
        started = True
        deadline = time.monotonic() + 120.0
        while True:
            try:
                observation = save_fork.request(port, "obs")
                if observation.get("ok") and "x" in observation:
                    break
            except save_fork.SaveForkError:
                pass
            if time.monotonic() >= deadline:
                raise StageError("cold Java reload did not produce a player")
            time.sleep(0.1)
        save_fork.request(port, "step_lock", {"wait_ms": 60000})
        save_fork.request(port, "server_step_lock")
        locked = True
        save_fork.request(port, "normalize_reload_locked")
        boundary = save_fork.request(port, "authoritative_state_locked")
        authoritative = boundary.get("authoritative") or {}
        if authoritative.get("entities"):
            raise StageError("source has pre-existing represented entities")
        base_x = math.floor(float(authoritative["x"])) + 4
        base_y = math.floor(float(authoritative["y"]))
        # Keep the staged boundary in the player's current chunk so the
        # passenger-order slice does not also exercise PlayerChunkMap's
        # cross-chunk watcher transition. The negative offset avoids the
        # SAVE-07 cactus at z=-99 in the reusable baseline.
        base_z = math.floor(float(authoritative["z"])) - 4
        blocks = []
        rail_offsets = range(-4, 13) if save08 else range(-2, 3)
        for dx in rail_offsets:
            blocks.extend([
                [base_x + dx, base_y - 1, base_z, 1, 0],
                [base_x + dx, base_y, base_z, 66, 1],
                [base_x + dx, base_y + 1, base_z, 0, 0],
                [base_x + dx, base_y + 2, base_z, 0, 0],
            ])
        placed = save_fork.request(port, "setblocks_locked", {
            "blocks": blocks,
        })
        if not placed.get("ok"):
            raise StageError(f"could not stage minecart track: {placed}")
        eid = 5201
        identity = "00000000-0000-0000-0000-000000005201"
        summoned = save_fork.request(port, "summon_locked", {
            "type": "minecart", "x": base_x + 0.5,
            "y": base_y + 0.0625, "z": base_z + 0.5,
            "mx": 0.2 if save08 else 0.0, "my": 0.0, "mz": 0.0,
            "eid": eid, "uuid": identity,
        })
        if not summoned.get("ok"):
            raise StageError(f"could not spawn rideable minecart: {summoned}")
        # Finalize chunk entity slices before creating the relationship. The
        # mount itself is staged at the saved pre-tick boundary.
        save_fork.request(port, "server_tick_locked")
        mounted = save_fork.request(port, "mount_player_locked", {"eid": eid})
        if not mounted.get("ok") or mounted.get("riding_eid") != eid:
            raise StageError(f"could not mount saved player: {mounted}")
        oracle = save_fork.request(port, "save_world_locked")
        hidden = save_fork.request(port, "hidden_state_locked")
        world = next(
            value for value in hidden["worlds"]
            if value.get("dim") == authoritative["dim"])
        player_uuid = next(
            row["uuid"] for row in world["entities"]
            if row.get("class") ==
                "net.minecraft.entity.player.EntityPlayerMP")
        player_row = next(
            row for row in world["entities"] if row["uuid"] == player_uuid)
        vehicle_row = next(
            row for row in world["entities"] if row["uuid"] == identity)
        if player_row.get("riding_uuid") != identity \
                or vehicle_row.get("passenger_uuids") != [player_uuid]:
            raise StageError("saved hidden passenger graph is not reciprocal")
        vehicle_vx = 0.0
        if save08:
            boundary = save_fork.request(
                port, "authoritative_state_locked")["authoritative"]
            cart = next(
                row for row in boundary["entities"] if row["eid"] == eid)
            vehicle_vx = float(cart.get("vx", 0.0))
            if cart.get("type") != "EntityMinecartEmpty" \
                    or cart.get("minecart_kind") != 0 \
                    or abs(vehicle_vx) < 1.0e-6:
                raise StageError(
                    "saved vehicle is not an exact moving minecart: "
                    + json.dumps(cart, sort_keys=True))
        save_fork.write_snapshot(
            pathlib.Path(oracle["world_directory"]), output, oracle, hidden)
        (output / "fixture_passenger.json").write_text(json.dumps({
            "schema": "netherite.completeness_fixture",
            "version": 1,
            "id": ("save08-moving-player-minecart" if save08
                   else "save03-player-minecart"),
            "todo": "SAVE-08" if save08 else "SAVE-03",
            "paired_boundary": {
                "left": ("moving-mounted-player-a" if save08
                         else "mounted-player-a"),
                "right": ("moving-mounted-player-b" if save08
                          else "mounted-player-b"),
                "same_tick": True,
            },
            "negative_control": {
                "mutation": ("zero the saved minecart velocity" if save08
                             else "remove the player riding edge"),
                "expected_path": ("$/entities/minecart/vx" if save08
                                  else "$/player_riding_eid"),
                "before": vehicle_vx if save08 else eid,
                "after": 0.0 if save08 else -1,
            },
            "horizons": [1, 2, 4, 8, 20] if save08 else [1, 20],
            "inputs": [{} for _ in range(20)],
            "comparisons": [
                {"family": "numeric", "required": True,
                 "minimum_observations": 21},
                {"family": "blocks", "required": True,
                 "minimum_observations": 3},
                {"family": "light", "required": True,
                 "minimum_observations": 3},
                {"family": "order", "required": True,
                 "minimum_observations": 21},
            ],
            "fixture": "player_minecart",
            "eid": eid,
            "uuid": identity,
        }, indent=2, sort_keys=True) + "\n")
    finally:
        if locked:
            try:
                save_fork.request(port, "server_step_unlock")
            except Exception:
                pass
        if started:
            _oracle("stop", instance, seed, environment)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--instance", type=int, default=99)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--save08", action="store_true")
    args = parser.parse_args()
    stage(args.source.resolve(), args.output.resolve(), args.instance,
          args.seed, args.save08)
    print(f"PASS staged real Java passenger save -> {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, StopIteration, ValueError, StageError,
            save_fork.SaveForkError) as exc:
        print(f"FAIL stage passenger fixture: {exc}", file=sys.stderr)
        raise SystemExit(1)
