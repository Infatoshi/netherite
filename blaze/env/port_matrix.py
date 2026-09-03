#!/usr/bin/env python3
"""Fail-closed Magma-to-Blaze subsystem matrix runner."""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from collections import Counter
from collections.abc import Callable, Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

import yaml

VERIFIED = "VERIFIED"
BLOCKED = "BLOCKED"
FAILED = "FAILED"
STATUSES = (VERIFIED, BLOCKED, FAILED)
TIERS = ("m1", "m2")
M2_KERNELS = ("raw", "warp", "scalar")
SOURCE_GROUPS = ("magma", "shared", "blaze")
FEATURES = frozenset(
    (
        "player",
        "dig",
        "inventory",
        "items",
        "world",
        "crafting",
        "containers",
        "furnaces",
        "fluids",
        "random_ticks",
        "falling_blocks",
        "mobs",
        "projectiles",
        "explosions",
        "portals",
        "dimensions",
        "dragon",
        "weather",
        "xp",
        "victory",
        "chests",
        "boats",
        "elytra",
        "observations",
    )
)
CONFIG_PATH = Path(__file__).with_name("port_matrix.yaml")
REPO_ROOT = Path(__file__).resolve().parents[2]
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
REQUIRED_ROW_KEYS = frozenset(
    (
        "name",
        "supported",
        "sources",
        "dependencies",
        "required_features",
        "required_artifacts",
        "m1",
        "m2",
        "timeout",
    )
)


class ConfigError(ValueError):
    """The declarative matrix is malformed or is not a DAG."""


def _string_list(value: Any, where: str, *, allow_empty: bool = True) -> list[str]:
    if not isinstance(value, list) or any(
        not isinstance(item, str) or not item for item in value
    ):
        raise ConfigError(f"{where} must be a list of non-empty strings")
    if not allow_empty and not value:
        raise ConfigError(f"{where} must not be empty")
    if len(value) != len(set(value)):
        raise ConfigError(f"{where} contains duplicates")
    return value


def _relative_path(value: str, where: str) -> None:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ConfigError(f"{where} must be a repository-relative path")


def validate_config(config: Any) -> list[str]:
    """Validate the schema and return a deterministic topological order."""
    if not isinstance(config, dict):
        raise ConfigError("matrix root must be a mapping")
    if config.get("version") != 1:
        raise ConfigError("matrix version must be 1")
    raw_rows = config.get("subsystems")
    if not isinstance(raw_rows, list) or not raw_rows:
        raise ConfigError("subsystems must be a non-empty list")

    names: list[str] = []
    for index, row in enumerate(raw_rows):
        where = f"subsystems[{index}]"
        if not isinstance(row, dict):
            raise ConfigError(f"{where} must be a mapping")
        missing = REQUIRED_ROW_KEYS - row.keys()
        if missing:
            raise ConfigError(f"{where} missing keys: {', '.join(sorted(missing))}")
        extra = row.keys() - REQUIRED_ROW_KEYS - {
            "block_reason", "m2_kernels", "resume"
        }
        if extra:
            raise ConfigError(f"{where} has unknown keys: {', '.join(sorted(extra))}")

        name = row["name"]
        if not isinstance(name, str) or not NAME_RE.fullmatch(name):
            raise ConfigError(f"{where}.name must match {NAME_RE.pattern}")
        if name in names:
            raise ConfigError(f"duplicate subsystem name: {name}")
        names.append(name)

        if type(row["supported"]) is not bool:
            raise ConfigError(f"{name}.supported must be boolean")
        reason = row.get("block_reason")
        if not row["supported"] and (not isinstance(reason, str) or not reason.strip()):
            raise ConfigError(f"{name}.block_reason is required when unsupported")
        if reason is not None and (not isinstance(reason, str) or not reason.strip()):
            raise ConfigError(f"{name}.block_reason must be a non-empty string")

        sources = row["sources"]
        if not isinstance(sources, dict) or set(sources) != set(SOURCE_GROUPS):
            raise ConfigError(
                f"{name}.sources must contain exactly: {', '.join(SOURCE_GROUPS)}"
            )
        for group in SOURCE_GROUPS:
            paths = _string_list(
                sources[group], f"{name}.sources.{group}", allow_empty=False
            )
            for path in paths:
                _relative_path(path, f"{name}.sources.{group}")

        _string_list(row["dependencies"], f"{name}.dependencies")
        features = _string_list(
            row["required_features"],
            f"{name}.required_features",
            allow_empty=not row["supported"],
        )
        unknown_features = set(features) - FEATURES
        if unknown_features:
            raise ConfigError(
                f"{name}.required_features has unknown features: "
                f"{', '.join(sorted(unknown_features))}"
            )

        artifacts = row["required_artifacts"]
        if not isinstance(artifacts, dict) or set(artifacts) != set(TIERS):
            raise ConfigError(f"{name}.required_artifacts must contain m1 and m2")
        for tier in TIERS:
            paths = _string_list(artifacts[tier], f"{name}.required_artifacts.{tier}")
            for path in paths:
                _relative_path(path, f"{name}.required_artifacts.{tier}")

        for tier in TIERS:
            _string_list(row[tier], f"{name}.{tier}")
        if "m2_kernels" in row:
            _m2_kernels_list(row["m2_kernels"], name)
        if "resume" in row and type(row["resume"]) is not bool:
            raise ConfigError(f"{name}.resume must be boolean")
        timeout = row["timeout"]
        if type(timeout) is not int or timeout <= 0:
            raise ConfigError(f"{name}.timeout must be a positive integer")

    rows_by_name = {row["name"]: row for row in raw_rows}
    known = set(rows_by_name)
    for row in raw_rows:
        name = row["name"]
        unknown = set(row["dependencies"]) - known
        if unknown:
            raise ConfigError(
                f"{name} has unknown dependencies: {', '.join(sorted(unknown))}"
            )

    return topological_order(rows_by_name, names)


def topological_order(
    rows_by_name: Mapping[str, Mapping[str, Any]],
    declaration_order: Sequence[str] | None = None,
) -> list[str]:
    """Return a stable dependency-first order, rejecting cycles."""
    order = list(declaration_order or rows_by_name)
    rank = {name: index for index, name in enumerate(order)}
    indegree = {name: len(row["dependencies"]) for name, row in rows_by_name.items()}
    children: dict[str, list[str]] = {name: [] for name in rows_by_name}
    for name, row in rows_by_name.items():
        for dependency in row["dependencies"]:
            if dependency not in rows_by_name:
                raise ConfigError(f"{name} has unknown dependency: {dependency}")
            children[dependency].append(name)

    ready = sorted((name for name, degree in indegree.items() if degree == 0), key=rank.get)
    result: list[str] = []
    while ready:
        name = ready.pop(0)
        result.append(name)
        for child in sorted(children[name], key=rank.get):
            indegree[child] -= 1
            if indegree[child] == 0:
                ready.append(child)
        ready.sort(key=rank.get)

    if len(result) != len(rows_by_name):
        cycle_nodes = sorted(name for name, degree in indegree.items() if degree > 0)
        raise ConfigError(f"dependency cycle involving: {', '.join(cycle_nodes)}")
    return result


def load_config(path: Path | str = CONFIG_PATH) -> dict[str, Any]:
    try:
        with Path(path).open(encoding="utf-8") as handle:
            config = yaml.safe_load(handle)
    except (OSError, yaml.YAMLError) as exc:
        raise ConfigError(f"cannot load {path}: {exc}") from exc
    validate_config(config)
    return config


def _m2_kernels_list(value: Any, name: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ConfigError(f"{name}.m2_kernels must be a non-empty list")
    allowed = set(M2_KERNELS)
    if any(not isinstance(item, str) or item not in allowed for item in value):
        raise ConfigError(
            f"{name}.m2_kernels must be a list of {', '.join(M2_KERNELS)}"
        )
    if len(value) != len(set(value)):
        raise ConfigError(f"{name}.m2_kernels contains duplicates")
    return list(value)


def row_m2_kernels(row: Mapping[str, Any]) -> list[str]:
    """Per-row M2 kernel list. Default: raw, warp, scalar (verify_cuda.py)."""
    value = row.get("m2_kernels")
    if value is None:
        return list(M2_KERNELS)
    return list(value)


def row_wants_resume(row: Mapping[str, Any]) -> bool:
    """Per-row continuous-vs-resume BP_ digest gate. Default false."""
    return row.get("resume") is True


_RESUME_VALUE_FLAGS = frozenset(
    (
        "--snapshot",
        "--tape",
        "--features",
        "--chain-seed",
        "--expected-chain-actions",
        "--seeds",
        "--ticks",
    )
)
_RESUME_BOOL_FLAGS = frozenset(
    (
        "--chain",
        "--mobs-on",
        "--natural-spawn",
        "--natural-spawn-passive",
    )
)


def resume_argv(row: Mapping[str, Any], *, cuda: bool = False) -> list[str]:
    """Build verify_resume_parity.py argv from the row's m1 gate flags."""
    argv = [
        "uv",
        "run",
        "--no-project",
        "--with",
        "numpy",
        "python",
        "blaze/env/verify_resume_parity.py",
    ]
    if cuda:
        argv.append("--cuda")
    src = list(row["m1"])
    i = 0
    while i < len(src):
        tok = src[i]
        if tok in _RESUME_BOOL_FLAGS:
            argv.append(tok)
            i += 1
        elif tok in _RESUME_VALUE_FLAGS:
            argv.append(tok)
            if i + 1 < len(src):
                argv.append(src[i + 1])
                i += 2
            else:
                i += 1
        else:
            i += 1
    return argv


def requested_tiers(tier: str) -> tuple[str, ...]:
    if tier == "all":
        return TIERS
    if tier not in TIERS:
        raise ConfigError(f"unknown tier: {tier}")
    return (tier,)


def missing_artifacts(
    row: Mapping[str, Any], tiers: Iterable[str], root: Path | str = REPO_ROOT
) -> list[str]:
    root = Path(root)
    missing: list[str] = []
    for tier in tiers:
        for artifact in row["required_artifacts"][tier]:
            target = str(root / artifact)
            if glob.has_magic(artifact):
                present = bool(glob.glob(target))
            else:
                present = Path(target).exists()
            if not present and artifact not in missing:
                missing.append(artifact)
    return missing


def classify(
    row: Mapping[str, Any],
    tiers: Iterable[str],
    *,
    dependency_statuses: Mapping[str, str] | None = None,
    absent_artifacts: Sequence[str] = (),
    gate_returncodes: Mapping[str, int] | None = None,
    check: bool = False,
    ignore_dependencies: bool = False,
) -> tuple[str, str]:
    """Classify one row without executing it."""
    tiers = tuple(tiers)
    dependency_statuses = dependency_statuses or {}
    gate_returncodes = gate_returncodes or {}

    if not row["supported"]:
        return BLOCKED, row["block_reason"]

    if not ignore_dependencies:
        unavailable = [
            dependency
            for dependency in row["dependencies"]
            if dependency_statuses.get(dependency) != VERIFIED
        ]
        if unavailable:
            return BLOCKED, f"dependencies not VERIFIED: {', '.join(unavailable)}"

    missing_gates = [tier for tier in tiers if not row[tier]]
    if missing_gates:
        reason = row.get("block_reason")
        if isinstance(reason, str) and reason.strip():
            return BLOCKED, reason
        return BLOCKED, f"missing required gates: {', '.join(missing_gates)}"
    if absent_artifacts:
        return BLOCKED, f"missing artifacts: {', '.join(absent_artifacts)}"
    if check:
        return BLOCKED, "gate evidence not executed (--check)"

    for tier in tiers:
        if tier not in gate_returncodes:
            return BLOCKED, f"missing evidence: {tier} gate did not run"
        returncode = gate_returncodes[tier]
        if returncode == 3:
            return BLOCKED, f"{tier} gate reported missing capability or evidence (rc=3)"
        if returncode != 0:
            return FAILED, f"{tier} gate failed (rc={returncode})"
    return VERIFIED, "all required gates passed with evidence"


def dependency_closure(name: str, rows_by_name: Mapping[str, Mapping[str, Any]]) -> set[str]:
    selected: set[str] = set()

    def visit(current: str) -> None:
        if current in selected:
            return
        selected.add(current)
        for dependency in rows_by_name[current]["dependencies"]:
            visit(dependency)

    visit(name)
    return selected


def ready_frontier(config: Mapping[str, Any]) -> list[str]:
    """Unsupported rows whose direct prerequisites are all declared supported."""
    order = validate_config(config)
    rows = {row["name"]: row for row in config["subsystems"]}
    return [
        name
        for name in order
        if not rows[name]["supported"]
        and all(rows[dependency]["supported"] for dependency in rows[name]["dependencies"])
    ]

def worker_prompt(row: Mapping[str, Any]) -> dict[str, Any]:
    files = [
        path
        for group in SOURCE_GROUPS
        for path in row["sources"][group]
    ]
    files.extend([
        "magma/game/rl_mode.c",
        "blaze/core/port_parity.h",
        "blaze/env/verify_cpu.py",
        "blaze/env/verify_cuda.py",
        "blaze/env/make_snapshots.py",
        "blaze/env/port_matrix.yaml",
    ])
    name = row["name"]
    return {
        "acceptance": (
            "uv run --no-project --with pyyaml python "
            f"blaze/env/port_matrix.py --tier m1 --subsystem {name} --no-deps"
        ),
        "dependencies": list(row["dependencies"]),
        "name": name,
        "prompt": (
            f"# Target\nPort the {name} subsystem from Magma into the shared "
            "Blaze CPU/CUDA core. Touch only the listed source seams and "
            "focused tests.\n# Change\nReplace this matrix row's BLOCKED "
            "result with measured parity digests, deterministic "
            "fixtures, and argv-only M1/M2 gates. Preserve one shared "
            "implementation for Blaze CPU and CUDA. Never claim parity from "
            "missing evidence.\n# Acceptance\nThe focused port_matrix command "
            "must report VERIFIED, not BLOCKED, and existing focused tests "
            "must pass. Skip project-wide builds, formatters, and suites."
        ),
        "required_features": list(row["required_features"]),
        "sources": files,
    }


def _last_output_line(completed: subprocess.CompletedProcess[str]) -> str:
    lines = (completed.stderr + "\n" + completed.stdout).splitlines()
    return next((line.strip() for line in reversed(lines) if line.strip()), "")


def run_command(argv: Sequence[str], root: Path, timeout: int) -> tuple[int, str]:
    """Run one argv-only gate. No shell is involved."""
    argv = list(argv)
    try:
        completed = subprocess.run(
            argv,
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
            env={**os.environ, "PYTHONUNBUFFERED": "1"},
        )
    except subprocess.TimeoutExpired:
        return 124, f"timeout after {timeout}s"
    except OSError as exc:
        return 127, str(exc)
    log_dir = Path(root) / "out" / "verify"
    try:
        log_dir.mkdir(parents=True, exist_ok=True)
        kernel = "m1"
        if "--m2-kernel" in argv:
            idx = argv.index("--m2-kernel")
            if idx + 1 < len(argv):
                kernel = argv[idx + 1]
        snap = "gate"
        for item in argv:
            if item.endswith(".bsnp"):
                snap = Path(item).stem
                break
        (log_dir / f"warpm2_{snap}_{kernel}.log").write_text(
            completed.stderr + completed.stdout
        )
    except OSError:
        pass
    return completed.returncode, _last_output_line(completed)


Executor = Callable[[Sequence[str], Path, int], tuple[int, str]]


def run_matrix(
    config: Mapping[str, Any],
    *,
    root: Path | str = REPO_ROOT,
    tier: str = "all",
    subsystem: str | None = None,
    check: bool = False,
    no_deps: bool = False,
    executor: Executor = run_command,
) -> dict[str, Any]:
    order = validate_config(config)
    tiers = requested_tiers(tier)
    rows = {row["name"]: row for row in config["subsystems"]}
    if subsystem is not None and subsystem not in rows:
        raise ConfigError(f"unknown subsystem: {subsystem}")

    if subsystem is None:
        selected = set(order)
    elif no_deps:
        selected = {subsystem}
    else:
        selected = dependency_closure(subsystem, rows)

    root = Path(root)
    statuses: dict[str, str] = {}
    results: list[dict[str, Any]] = []
    for name in order:
        if name not in selected:
            continue
        row = rows[name]
        absent = missing_artifacts(row, tiers, root)
        gate_returncodes: dict[str, int] = {}
        gates: list[dict[str, Any]] = []

        pre_status, _ = classify(
            row,
            tiers,
            dependency_statuses=statuses,
            absent_artifacts=absent,
            gate_returncodes=gate_returncodes,
            check=True,
            ignore_dependencies=no_deps,
        )
        can_run = (
            not check
            and pre_status == BLOCKED
            and row["supported"]
            and not absent
            and all(row[current_tier] for current_tier in tiers)
            and (
                no_deps
                or all(statuses.get(dep) == VERIFIED for dep in row["dependencies"])
            )
        )
        if can_run:
            for current_tier in tiers:
                if current_tier == "m2":
                    last_rc = 0
                    for kernel in row_m2_kernels(row):
                        argv = list(row["m2"]) + ["--m2-kernel", kernel]
                        returncode, detail = executor(argv, root, row["timeout"])
                        last_rc = returncode
                        gate = {
                            "argv": list(argv),
                            "kernel": kernel,
                            "returncode": returncode,
                            "tier": current_tier,
                        }
                        if detail:
                            gate["detail"] = detail
                        gates.append(gate)
                        if returncode != 0:
                            break
                    gate_returncodes[current_tier] = last_rc
                else:
                    argv = row[current_tier]
                    returncode, detail = executor(argv, root, row["timeout"])
                    gate_returncodes[current_tier] = returncode
                    gate = {
                        "argv": list(argv),
                        "returncode": returncode,
                        "tier": current_tier,
                    }
                    if detail:
                        gate["detail"] = detail
                    gates.append(gate)
                    if returncode != 0:
                        break
                if (
                    row_wants_resume(row)
                    and gate_returncodes.get(current_tier, 1) == 0
                ):
                    argv = resume_argv(row, cuda=(current_tier == "m2"))
                    returncode, detail = executor(argv, root, row["timeout"])
                    gate = {
                        "argv": list(argv),
                        "returncode": returncode,
                        "tier": current_tier,
                        "resume": True,
                    }
                    if detail:
                        gate["detail"] = detail
                    gates.append(gate)
                    if returncode != 0:
                        gate_returncodes[current_tier] = returncode

        status, reason = classify(
            row,
            tiers,
            dependency_statuses=statuses,
            absent_artifacts=absent,
            gate_returncodes=gate_returncodes,
            check=check,
            ignore_dependencies=no_deps,
        )
        statuses[name] = status
        result: dict[str, Any] = {"name": name, "reason": reason, "status": status}
        if gates:
            result["gates"] = gates
        results.append(result)

    counts = Counter(result["status"] for result in results)
    return {
        "check": check,
        "results": results,
        "summary": {status: counts[status] for status in STATUSES},
        "tier": tier,
        "version": config["version"],
    }


def report_exit_code(report: Mapping[str, Any]) -> int:
    statuses = {result["status"] for result in report["results"]}
    if FAILED in statuses:
        return 1
    if BLOCKED in statuses:
        return 3
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="classify without running gates")
    parser.add_argument("--tier", choices=("m1", "m2", "all"), default="all")
    parser.add_argument("--subsystem", help="select one subsystem by name")
    parser.add_argument(
        "--ready",
        action="store_true",
        help="list unsupported frontier rows whose dependencies are supported",
    )
    parser.add_argument(
        "--prompts",
        action="store_true",
        help="emit self-contained contracts for frontier and failing rows",
    )
    parser.add_argument("--json", action="store_true", help="emit deterministic JSON")
    parser.add_argument(
        "--no-deps",
        action="store_true",
        help="with --subsystem, run only that row and explicitly assume its dependencies",
    )
    return parser


def _print_text(report: Mapping[str, Any]) -> None:
    for result in report["results"]:
        print(f"{result['status']:<8} {result['name']}: {result['reason']}")
    summary = report["summary"]
    print(
        "SUMMARY "
        + " ".join(f"{status}={summary[status]}" for status in STATUSES)
    )


def main(argv: Sequence[str] | None = None, *, config_path: Path | str = CONFIG_PATH) -> int:
    args = _parser().parse_args(argv)
    try:
        config = load_config(config_path)
        ready = ready_frontier(config)
        if args.subsystem is not None:
            names = {row["name"] for row in config["subsystems"]}
            if args.subsystem not in names:
                raise ConfigError(f"unknown subsystem: {args.subsystem}")
        if args.ready:
            if args.subsystem is not None:
                ready = [name for name in ready if name == args.subsystem]
            payload: dict[str, Any] = {
                "ready": ready,
                "version": config["version"],
            }
            if args.prompts:
                rows = {row["name"]: row for row in config["subsystems"]}
                payload["workers"] = [worker_prompt(rows[name]) for name in ready]
            if args.json:
                print(json.dumps(payload, indent=2, sort_keys=True))
            elif args.prompts:
                for worker in payload["workers"]:
                    print(f"===== {worker['name']} =====")
                    print(worker["prompt"])
                    print("Sources: " + ", ".join(worker["sources"]))
                    print("Acceptance: " + worker["acceptance"])
            else:
                for name in ready:
                    print(f"READY {name}")
            return 0

        report = run_matrix(
            config,
            tier=args.tier,
            subsystem=args.subsystem,
            check=args.check,
            no_deps=args.no_deps,
        )
        if args.prompts:
            rows = {row["name"]: row for row in config["subsystems"]}
            candidates = [
                result["name"]
                for result in report["results"]
                if rows[result["name"]]["supported"]
                and result["status"] != VERIFIED
            ]
            candidates.extend(name for name in ready if name not in candidates)
            report["workers"] = [
                worker_prompt(rows[name]) for name in candidates
            ]
    except ConfigError as exc:
        if args.json:
            print(json.dumps({"error": str(exc)}, sort_keys=True))
        else:
            print(f"CONFIG ERROR: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        _print_text(report)
        if args.prompts:
            for worker in report["workers"]:
                print(f"===== {worker['name']} =====")
                print(worker["prompt"])
                print("Sources: " + ", ".join(worker["sources"]))
                print("Acceptance: " + worker["acceptance"])
    return report_exit_code(report)


if __name__ == "__main__":
    sys.exit(main())
