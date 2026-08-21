import json

import port_matrix as pm
import pytest
import yaml

EXPECTED_ROWS = [
    "mining_slice",
    "spawn_to_torch",
    "world_dynamics",
    "fluids",
    "random_ticks",
    "falling_blocks",
    "entity_spine",
    "projectiles",
    "explosions",
    "mobs",
    "chests",
    "portals_dimensions",
    "nether_route",
    "dragon_victory",
    "weather_optional",
    "boats_elytra_xp",
]


def row(name, *, dependencies=(), supported=True, m1=None, m2=None):
    result = {
        "name": name,
        "supported": supported,
        "sources": {
            "magma": ["magma/source.c"],
            "shared": ["blaze/core/source.h"],
            "blaze": ["blaze/env/source.c"],
        },
        "dependencies": list(dependencies),
        "required_features": ["player"],
        "required_artifacts": {"m1": [], "m2": []},
        "m1": ["m1-gate"] if m1 is None else m1,
        "m2": ["m2-gate"] if m2 is None else m2,
        "timeout": 10,
    }
    if not supported:
        result["block_reason"] = f"{name} is not ported"
    return result


def matrix(*rows):
    return {"version": 1, "subsystems": list(rows)}


def test_checked_in_schema_dag_and_ready_frontier():
    config = pm.load_config()

    assert pm.validate_config(config) == EXPECTED_ROWS
    assert pm.ready_frontier(config) == [
        "random_ticks",
        "falling_blocks",
        "entity_spine",
        "chests",
        "portals_dimensions",
        "weather_optional",
    ]


def test_schema_requires_every_declarative_field():
    broken = row("root")
    del broken["required_artifacts"]

    with pytest.raises(pm.ConfigError, match="missing keys: required_artifacts"):
        pm.validate_config(matrix(broken))


def test_schema_rejects_unknown_dependencies():
    config = matrix(row("root", dependencies=["not_declared"]))

    with pytest.raises(pm.ConfigError, match="unknown dependencies: not_declared"):
        pm.validate_config(config)


def test_schema_rejects_dependency_cycles():
    config = matrix(
        row("first", dependencies=["second"]),
        row("second", dependencies=["first"]),
    )

    with pytest.raises(pm.ConfigError, match="dependency cycle"):
        pm.validate_config(config)


def test_topological_order_is_stable_and_dependency_first():
    config = matrix(
        row("root"),
        row("sibling"),
        row("child", dependencies=["root"]),
    )

    assert pm.validate_config(config) == ["root", "sibling", "child"]


@pytest.mark.parametrize("tier", ["m1", "m2", "all"])
def test_supported_row_with_no_requested_gates_cannot_verify(tier):
    empty = row("empty", m1=[], m2=[])

    status, reason = pm.classify(empty, pm.requested_tiers(tier))

    assert status == pm.BLOCKED
    assert "missing required gates" in reason


def test_classifier_is_fail_closed_and_distinguishes_gate_failures():
    subject = row("subject", dependencies=["root"])
    tiers = ("m1",)

    assert pm.classify(subject, tiers)[0] == pm.BLOCKED
    assert pm.classify(
        subject,
        tiers,
        dependency_statuses={"root": pm.VERIFIED},
        absent_artifacts=["fixture.bin"],
    )[0] == pm.BLOCKED
    assert pm.classify(
        subject,
        tiers,
        dependency_statuses={"root": pm.VERIFIED},
        gate_returncodes={"m1": 3},
    )[0] == pm.BLOCKED
    assert pm.classify(
        subject,
        tiers,
        dependency_statuses={"root": pm.VERIFIED},
        gate_returncodes={"m1": 1},
    )[0] == pm.FAILED
    assert pm.classify(
        subject,
        tiers,
        dependency_statuses={"root": pm.VERIFIED},
        gate_returncodes={"m1": 0},
    )[0] == pm.VERIFIED


def test_unsupported_row_is_always_blocked():
    subject = row("future", supported=False)

    status, reason = pm.classify(
        subject,
        ("m1",),
        gate_returncodes={"m1": 0},
        ignore_dependencies=True,
    )

    assert status == pm.BLOCKED
    assert reason == "future is not ported"


def test_dependency_failure_blocks_child_without_executing_child():
    config = matrix(row("root"), row("child", dependencies=["root"]))
    calls = []

    def executor(argv, root, timeout):
        calls.append(list(argv))
        return (1 if argv == ["m1-gate"] else 0), "fixture failure"

    report = pm.run_matrix(config, tier="m1", executor=executor)

    assert [result["status"] for result in report["results"]] == [
        pm.FAILED,
        pm.BLOCKED,
    ]
    assert calls == [["m1-gate"]]


def test_check_json_is_deterministic_and_never_executes_gates(tmp_path, capsys):
    path = tmp_path / "matrix.yaml"
    path.write_text(yaml.safe_dump(matrix(row("root")), sort_keys=False))

    first_rc = pm.main(["--check", "--json"], config_path=path)
    first = capsys.readouterr().out
    second_rc = pm.main(["--check", "--json"], config_path=path)
    second = capsys.readouterr().out

    assert first_rc == second_rc == 3
    assert first == second
    payload = json.loads(first)
    assert payload["results"] == [
        {
            "name": "root",
            "reason": "gate evidence not executed (--check)",
            "status": pm.BLOCKED,
        }
    ]


def test_subsystem_selection_runs_dependencies_unless_explicitly_disabled():
    config = matrix(row("root"), row("child", dependencies=["root"]))
    calls = []

    def executor(argv, root, timeout):
        calls.append(list(argv))
        return 0, ""

    report = pm.run_matrix(
        config,
        tier="m1",
        subsystem="child",
        executor=executor,
    )
    assert [result["name"] for result in report["results"]] == ["root", "child"]
    assert all(result["status"] == pm.VERIFIED for result in report["results"])

    calls.clear()
    report = pm.run_matrix(
        config,
        tier="m1",
        subsystem="child",
        no_deps=True,
        executor=executor,
    )
    assert [result["name"] for result in report["results"]] == ["child"]
    assert report["results"][0]["status"] == pm.VERIFIED
    assert calls == [["m1-gate"]]


def test_ready_json_is_a_successful_static_query(tmp_path, capsys):
    config = matrix(
        row("root"),
        row("left", dependencies=["root"], supported=False),
        row("right", dependencies=["root"], supported=False),
        row("later", dependencies=["left"], supported=False),
    )
    path = tmp_path / "matrix.yaml"
    path.write_text(yaml.safe_dump(config, sort_keys=False))

    rc = pm.main(["--ready", "--json"], config_path=path)

    assert rc == 0
    assert json.loads(capsys.readouterr().out) == {
        "ready": ["left", "right"],
        "version": 1,
    }

    rc = pm.main(["--ready", "--prompts", "--json"], config_path=path)
    payload = json.loads(capsys.readouterr().out)
    assert rc == 0
    assert [worker["name"] for worker in payload["workers"]] == [
        "left", "right",
    ]
    assert payload["workers"][0]["sources"] == [
        "magma/source.c", "blaze/core/source.h", "blaze/env/source.c",
        "magma/game/rl_mode.c", "blaze/core/port_parity.h",
        "blaze/env/verify_cpu.py", "blaze/env/verify_cuda.py",
        "blaze/env/make_snapshots.py", "blaze/env/port_matrix.yaml",
    ]
    assert payload["workers"][0]["acceptance"].endswith(
        "--tier m1 --subsystem left --no-deps")
    assert "Never claim parity from missing evidence" in \
        payload["workers"][0]["prompt"]
