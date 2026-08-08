#!/usr/bin/env python3
"""Capture and validate a real parked Java 1.11.2 Anvil save boundary."""

from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import socket
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
CAPSULE_SOURCE = ROOT / "magma" / "trace" / "state_capsule.py"
ANVIL_SOURCE = pathlib.Path(__file__).with_name("anvil_semantic.py")
REGISTRY_MANIFEST = pathlib.Path(__file__).with_name("registry_manifest.json")
SNAPSHOT_MANIFEST = "save_fork_manifest.json"
ORACLE_RESPONSE = "oracle_boundary.json"
CAPABILITY_REPORT = "capability_report.json"
HIDDEN_STATE = "hidden_state.json"
SCHEMA = "netherite.save_fork"
VERSION = 2
EXCLUDED_NAMES = {"session.lock"}


class SaveForkError(RuntimeError):
    pass


def request(port: int, command: str, action: dict | None = None) -> dict:
    message = {"cmd": command}
    if action is not None:
        message["action"] = action
    payload = json.dumps(message, separators=(",", ":")) + "\n"
    with socket.create_connection(("127.0.0.1", port), timeout=10.0) as sock:
        sock.settimeout(180.0)
        sock.sendall(payload.encode("utf-8"))
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    if not data:
        raise SaveForkError(f"empty response to {command}")
    try:
        response = json.loads(data.split(b"\n", 1)[0].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SaveForkError(f"invalid response to {command}: {exc}") from exc
    if not response.get("ok"):
        raise SaveForkError(f"{command} failed: {response}")
    return response


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def save_files(save_dir: pathlib.Path) -> list[pathlib.Path]:
    result = []
    for path in sorted(save_dir.rglob("*")):
        if path.name in EXCLUDED_NAMES:
            continue
        if path.is_symlink():
            raise SaveForkError(f"save contains a symlink: {path}")
        if path.is_file():
            result.append(path)
    return result


def file_manifest(save_dir: pathlib.Path) -> list[dict]:
    return [
        {
            "path": path.relative_to(save_dir).as_posix(),
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        }
        for path in save_files(save_dir)
    ]


def load_capsule_capabilities() -> dict[str, str]:
    # Loading state_capsule normally also imports its sibling nbt_codec.
    sys.path.insert(0, str(CAPSULE_SOURCE.parent))
    try:
        spec = importlib.util.spec_from_file_location(
            "netherite_state_capsule", CAPSULE_SOURCE
        )
        if spec is None or spec.loader is None:
            raise SaveForkError("could not load state_capsule.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return dict(module.CAPABILITIES_V2)
    finally:
        sys.path.pop(0)


def load_anvil_semantic():
    spec = importlib.util.spec_from_file_location(
        "netherite_anvil_semantic", ANVIL_SOURCE)
    if spec is None or spec.loader is None:
        raise SaveForkError("could not load anvil_semantic.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def capability_report(
    save_dir: pathlib.Path | None = None,
    hidden_state: dict | None = None,
) -> dict:
    capsule = load_capsule_capabilities()
    registry = json.loads(REGISTRY_MANIFEST.read_text())
    capsule_counts = collections.Counter(capsule.values())
    entity_counts = collections.Counter(
        row["status"] for row in registry["entities"]
    )
    tile_counts = collections.Counter(
        row["status"] for row in registry["tile_entities"]
    )
    unresolved = [
        {"surface": name, "capability": state}
        for name, state in sorted(capsule.items())
        if state != "exact"
    ]
    unresolved.extend(
        {
            "surface": f"entity:{row['id']}:{row['name']}",
            "capability": row["status"],
            "todo": row["todo"],
        }
        for row in registry["entities"]
        if row["status"] != "live_bounded"
    )
    unresolved.extend(
        {
            "surface": f"tile:{row['name']}",
            "capability": row["status"],
            "todo": row["todo"],
        }
        for row in registry["tile_entities"]
        if row["status"] != "live_bounded"
    )
    report = {
        "schema": "netherite.save_capabilities",
        "version": 1,
        "fail_closed": True,
        "capsule_v2_counts": dict(sorted(capsule_counts.items())),
        "entity_counts": dict(sorted(entity_counts.items())),
        "tile_entity_counts": dict(sorted(tile_counts.items())),
        "unresolved": unresolved,
    }
    if save_dir is not None:
        anvil = load_anvil_semantic()
        semantic = anvil.read_save(save_dir)
        fields = anvil.field_inventory(semantic)
        restore_counts = collections.Counter(
            row["native_restore"] for row in fields)
        report["observed_save"] = {
            "unique_field_paths": len(fields),
            "field_observations": sum(row["observations"] for row in fields),
            "native_restore_counts": dict(sorted(restore_counts.items())),
            "file_policy": semantic["file_policy"],
            "fields": fields,
        }
    if hidden_state is not None:
        if hidden_state.get("schema") != "qrl.hidden_state.v1":
            raise SaveForkError("unsupported hidden-state schema")
        anvil = load_anvil_semantic()
        fields = anvil.field_inventory({"load_inputs": hidden_state})
        for row in fields:
            row["todo"] = "HAR-03"
        report["observed_hidden"] = {
            "unique_field_paths": len(fields),
            "field_observations": sum(row["observations"] for row in fields),
            "native_restore_counts": {"reject": len(fields)},
            "fields": fields,
        }
    return report


def write_snapshot(
    source: pathlib.Path, output: pathlib.Path, oracle: dict,
    hidden_state: dict,
) -> None:
    if not source.is_dir():
        raise SaveForkError(f"Java save directory does not exist: {source}")
    if output.exists():
        raise SaveForkError(f"snapshot output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{output.name}.", dir=str(output.parent)
    ) as raw_temp:
        staging_root = pathlib.Path(raw_temp)
        staging = staging_root / "snapshot"
        shutil.copytree(
            source,
            staging / "save",
            ignore=shutil.ignore_patterns(*sorted(EXCLUDED_NAMES)),
        )
        files = file_manifest(staging / "save")
        hidden_payload = (
            json.dumps(hidden_state, indent=2, sort_keys=True) + "\n"
        ).encode()
        manifest = {
            "schema": SCHEMA,
            "version": VERSION,
            "format": oracle.get("format"),
            "source_folder": oracle.get("folder_name"),
            "player_ticks_existed": oracle.get("player_ticks_existed"),
            "total_world_time": oracle.get("total_world_time"),
            "dimensions": oracle.get("dimensions", []),
            "excluded": sorted(EXCLUDED_NAMES),
            "files": files,
            "hidden_state_sha256": hashlib.sha256(hidden_payload).hexdigest(),
        }
        (staging / SNAPSHOT_MANIFEST).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        )
        (staging / ORACLE_RESPONSE).write_text(
            json.dumps(oracle, indent=2, sort_keys=True) + "\n"
        )
        (staging / HIDDEN_STATE).write_bytes(hidden_payload)
        (staging / CAPABILITY_REPORT).write_text(
            json.dumps(
                capability_report(staging / "save", hidden_state),
                indent=2, sort_keys=True
            ) + "\n"
        )
        validate_snapshot(staging)
        staging.rename(output)


def validate_snapshot(snapshot: pathlib.Path) -> dict:
    try:
        manifest = json.loads((snapshot / SNAPSHOT_MANIFEST).read_text())
        oracle = json.loads((snapshot / ORACLE_RESPONSE).read_text())
        hidden_state = json.loads((snapshot / HIDDEN_STATE).read_text())
        report = json.loads((snapshot / CAPABILITY_REPORT).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SaveForkError(f"invalid snapshot metadata: {exc}") from exc
    if manifest.get("schema") != SCHEMA or manifest.get("version") != VERSION:
        raise SaveForkError("unsupported save-fork schema/version")
    if manifest.get("format") != "anvil-1.11.2":
        raise SaveForkError("snapshot is not a declared Java 1.11.2 Anvil save")
    if not oracle.get("ok") or oracle.get("format") != "anvil-1.11.2":
        raise SaveForkError("oracle boundary is incomplete")
    if hidden_state.get("schema") != "qrl.hidden_state.v1":
        raise SaveForkError("snapshot has no supported hidden continuation state")
    if manifest.get("hidden_state_sha256") != sha256(snapshot / HIDDEN_STATE):
        raise SaveForkError("hidden continuation hash mismatch")
    if report.get("fail_closed") is not True:
        raise SaveForkError("capability report is not fail-closed")
    observed = report.get("observed_save")
    if not isinstance(observed, dict) or not observed.get("fields"):
        raise SaveForkError("capability report has no observed save fields")
    if any(
        not row.get("native_restore") or not row.get("todo")
        for row in observed["fields"]
    ):
        raise SaveForkError("capability report contains an unclassified field")
    observed_hidden = report.get("observed_hidden")
    if not isinstance(observed_hidden, dict) or not observed_hidden.get("fields"):
        raise SaveForkError("capability report has no hidden-state fields")
    if any(
        not row.get("native_restore") or not row.get("todo")
        for row in observed_hidden["fields"]
    ):
        raise SaveForkError(
            "capability report contains an unclassified hidden-state field")
    save_dir = snapshot / "save"
    if not (save_dir / "level.dat").is_file():
        raise SaveForkError("snapshot has no level.dat")
    actual = file_manifest(save_dir)
    if manifest.get("files") != actual:
        raise SaveForkError("snapshot file size/hash manifest mismatch")
    if not any(row["path"].endswith(".mca") for row in actual):
        raise SaveForkError("snapshot has no Anvil region files")
    return manifest


def capture(port: int, output: pathlib.Path) -> None:
    locked = False
    try:
        request(port, "server_step_lock")
        locked = True
        capture_locked(port, output)
    finally:
        if locked:
            try:
                request(port, "server_step_unlock")
            except Exception as exc:
                print(f"warning: could not unlock Java oracle: {exc}", file=sys.stderr)
    manifest = validate_snapshot(output)
    print(
        "PASS Java save fork snapshot: "
        f"{len(manifest['files'])} files at tick "
        f"{manifest['player_ticks_existed']} -> {output}"
    )


def capture_locked(port: int, output: pathlib.Path) -> dict:
    """Capture from a gate the caller already owns, leaving it parked."""
    oracle = request(port, "save_world_locked")
    hidden_state = request(port, "hidden_state_locked")
    source = pathlib.Path(oracle["world_directory"])
    write_snapshot(source, output, oracle, hidden_state)
    validate_snapshot(output)
    return oracle


def capture_pair(port: int, first: pathlib.Path, second: pathlib.Path) -> None:
    """Export the same parked S0 twice without granting an intervening tick."""
    if first == second:
        raise SaveForkError("paired snapshot outputs must be different")
    if first.exists() or second.exists():
        raise SaveForkError("paired snapshot output already exists")
    locked = False
    try:
        request(port, "server_step_lock")
        locked = True
        for output in (first, second):
            oracle = request(port, "save_world_locked")
            hidden_state = request(port, "hidden_state_locked")
            source = pathlib.Path(oracle["world_directory"])
            write_snapshot(source, output, oracle, hidden_state)
    finally:
        if locked:
            try:
                request(port, "server_step_unlock")
            except Exception as exc:
                print(f"warning: could not unlock Java oracle: {exc}", file=sys.stderr)
    first_manifest = validate_snapshot(first)
    second_manifest = validate_snapshot(second)
    if first_manifest["player_ticks_existed"] != second_manifest["player_ticks_existed"]:
        raise SaveForkError("paired exports crossed a player tick boundary")
    print(
        "PASS paired Java S0 exports: "
        f"tick {first_manifest['player_ticks_existed']} -> {first}, {second}"
    )


def selftest() -> None:
    with tempfile.TemporaryDirectory(prefix="netherite_save_fork_") as raw:
        root = pathlib.Path(raw)
        source = root / "source"
        anvil = load_anvil_semantic()
        anvil._write_gzip_nbt(source / "level.dat", anvil._named_compound({
            "Data": anvil._compound({"Seed": anvil._long(7)})
        }))
        anvil._write_region(source / "region" / "r.0.0.mca", 0, 0, 4, 1)
        (source / "session.lock").write_bytes(b"volatile")
        oracle = {
            "ok": True,
            "format": "anvil-1.11.2",
            "folder_name": "selftest",
            "player_ticks_existed": 42,
            "total_world_time": 99,
            "dimensions": [{"id": 0, "name": "overworld"}],
            "authoritative": {"player_ticks_existed": 42},
        }
        snapshot = root / "snapshot"
        hidden_state = {
            "ok": True,
            "schema": "qrl.hidden_state.v1",
            "math_seed48": 3,
            "worlds": [],
        }
        write_snapshot(source, snapshot, oracle, hidden_state)
        validate_snapshot(snapshot)
        if (snapshot / "save" / "session.lock").exists():
            raise SaveForkError("volatile session.lock entered snapshot")
        hidden_path = snapshot / HIDDEN_STATE
        hidden_original = hidden_path.read_bytes()
        hidden_changed = json.loads(hidden_original)
        del hidden_changed["math_seed48"]
        hidden_path.write_text(json.dumps(hidden_changed, sort_keys=True))
        try:
            validate_snapshot(snapshot)
        except SaveForkError as exc:
            if "hidden continuation hash mismatch" not in str(exc):
                raise
        else:
            raise SaveForkError("deleted hidden exporter field passed validation")
        hidden_path.write_bytes(hidden_original)
        (snapshot / "save" / "level.dat").write_bytes(b"mutated")
        try:
            validate_snapshot(snapshot)
        except SaveForkError as exc:
            if "hash manifest mismatch" not in str(exc):
                raise
        else:
            raise SaveForkError("mutated save passed validation")
    print("PASS save-fork selftest: copy, capability, hash-negative")


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    capture_parser = sub.add_parser("capture")
    capture_parser.add_argument("output", type=pathlib.Path)
    capture_parser.add_argument("--port", type=int, default=25575)
    pair_parser = sub.add_parser("capture-pair")
    pair_parser.add_argument("first", type=pathlib.Path)
    pair_parser.add_argument("second", type=pathlib.Path)
    pair_parser.add_argument("--port", type=int, default=25575)
    validate_parser = sub.add_parser("validate")
    validate_parser.add_argument("snapshot", type=pathlib.Path)
    sub.add_parser("report")
    sub.add_parser("selftest")
    args = parser.parse_args()
    if args.command == "capture":
        capture(args.port, args.output.resolve())
    elif args.command == "capture-pair":
        capture_pair(args.port, args.first.resolve(), args.second.resolve())
    elif args.command == "validate":
        manifest = validate_snapshot(args.snapshot.resolve())
        print(f"PASS save-fork snapshot: {len(manifest['files'])} files")
    elif args.command == "report":
        print(json.dumps(capability_report(), indent=2, sort_keys=True))
    else:
        selftest()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, SaveForkError, ValueError) as exc:
        print(f"FAIL save-fork: {exc}", file=sys.stderr)
        raise SystemExit(1)
