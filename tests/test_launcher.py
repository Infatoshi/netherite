"""Tests for Minecraft launcher command construction."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from config import NetheriteConfig
import launcher
from launcher import Launcher, MCInstance


def test_mcinstance_launch_command_passes_vanilla_window_size_args():
    cfg = NetheriteConfig(width=160, height=90, seed=4242)
    inst = MCInstance(cfg, ROOT)

    cmd = inst._build_launch_command()

    assert cmd[0] == str(ROOT / "gradlew")
    assert cmd[1] == "runClient"
    assert "-Dnetherite.width=160" in cmd
    assert "-Dnetherite.height=90" in cmd
    assert "--args=--width 160 --height 90 --username netherite_0" in cmd


def test_mcinstance_start_uses_new_process_session(monkeypatch, tmp_path: Path):
    calls = {}

    class FakeProcess:
        pid = 123

        def poll(self):
            return None

    def fake_popen(cmd, **kwargs):
        calls["cmd"] = cmd
        calls["kwargs"] = kwargs
        return FakeProcess()

    monkeypatch.setattr(launcher.subprocess, "Popen", fake_popen)

    cfg = NetheriteConfig(java_home="/java21")
    inst = MCInstance(cfg, tmp_path, game_dir=tmp_path / "run" / "instances" / "0")

    inst.start()

    assert calls["kwargs"]["start_new_session"] is True
    assert calls["kwargs"]["stderr"] == launcher.subprocess.STDOUT
    assert (tmp_path / "run" / "instances" / "0").exists()


def test_mcinstance_launch_command_includes_game_dir_when_configured():
    cfg = NetheriteConfig(width=160, height=90, seed=4242)
    inst = MCInstance(cfg, ROOT, game_dir=ROOT / "run" / "instances" / "0")

    cmd = inst._build_launch_command()

    assert (
        "--args=--width 160 --height 90 --username netherite_0 "
        f"--gameDir {ROOT / 'run' / 'instances' / '0'}"
    ) in cmd


def test_prepare_game_dir_links_shared_mods(tmp_path: Path):
    shared_mods = tmp_path / "run" / "mods"
    shared_mods.mkdir(parents=True)
    (shared_mods / "lithium.jar").write_text("x", encoding="utf-8")

    cfg = NetheriteConfig(instance_id=7)
    game_dir = tmp_path / "run" / "instances" / "7"
    inst = MCInstance(cfg, tmp_path, game_dir=game_dir)

    inst._prepare_game_dir()

    instance_mods = game_dir / "mods"
    assert instance_mods.exists()
    assert (instance_mods / "lithium.jar").exists()


def test_stop_terminates_process_group(monkeypatch):
    signals: list[tuple[int, int]] = []

    class FakeProcess:
        pid = 456

        def __init__(self):
            self.wait_calls = 0

        def poll(self):
            return None

        def send_signal(self, _sig):
            raise AssertionError("send_signal should not be used when killpg works")

        def wait(self, timeout):
            self.wait_calls += 1
            if self.wait_calls == 1:
                raise launcher.subprocess.TimeoutExpired("cmd", timeout)
            return 0

        def kill(self):
            raise AssertionError("kill should not be used when killpg works")

    monkeypatch.setattr(launcher.os, "getpgid", lambda _pid: 999)
    monkeypatch.setattr(
        launcher.os,
        "killpg",
        lambda pgid, sig: signals.append((pgid, sig)),
    )

    inst = MCInstance(NetheriteConfig(), ROOT)
    inst.process = FakeProcess()

    inst.stop()

    assert signals == [
        (999, launcher.signal.SIGTERM),
        (999, launcher.signal.SIGKILL),
    ]


def test_stop_terminates_lingering_matching_processes(monkeypatch, tmp_path: Path):
    calls: list[tuple[int, int]] = []
    game_dir = tmp_path / "run" / "instances" / "4"

    class FakeProcess:
        pid = 456

        def poll(self):
            return None

        def wait(self, timeout):
            return 0

        def send_signal(self, _sig):
            raise AssertionError("send_signal should not be used when killpg works")

        def kill(self):
            raise AssertionError("kill should not be used when wait succeeds")

    ps_outputs = iter(
        [
            "\n".join(
                [
                    (
                        "111 "
                        f"/usr/bin/java {tmp_path} "
                        "-Dnetherite.instance_id=4 "
                        "--gameDir "
                        f"{game_dir} "
                        "net.fabricmc.devlaunchinjector.Main"
                    ),
                    (
                        "222 "
                        f"/usr/bin/java {tmp_path} "
                        "-Dnetherite.instance_id=99 "
                        "net.fabricmc.devlaunchinjector.Main"
                    ),
                ]
            ),
            "",
        ]
    )

    monkeypatch.setattr(launcher.os, "getpgid", lambda _pid: 999)
    monkeypatch.setattr(launcher.os, "killpg", lambda _pgid, _sig: None)
    monkeypatch.setattr(
        launcher.subprocess,
        "check_output",
        lambda *args, **kwargs: next(ps_outputs),
    )
    monkeypatch.setattr(
        launcher.os,
        "kill",
        lambda pid, sig: calls.append((pid, sig)),
    )

    inst = MCInstance(NetheriteConfig(instance_id=4), tmp_path, game_dir=game_dir)
    inst.process = FakeProcess()

    inst.stop()

    assert calls == [(111, launcher.signal.SIGTERM)]


def test_matching_process_ids_ignore_unrelated_projects(tmp_path: Path, monkeypatch):
    cfg = NetheriteConfig(instance_id=2)
    inst = MCInstance(cfg, tmp_path, game_dir=tmp_path / "run" / "instances" / "2")

    monkeypatch.setattr(
        launcher.subprocess,
        "check_output",
        lambda *args, **kwargs: "\n".join(
            [
                (
                    "101 "
                    f"/usr/bin/java {tmp_path} "
                    "-Dnetherite.instance_id=2 "
                    "net.fabricmc.devlaunchinjector.Main"
                ),
                (
                    "202 /usr/bin/java /other/project "
                    "-Dnetherite.instance_id=2 "
                    "net.fabricmc.devlaunchinjector.Main"
                ),
            ]
        ),
    )

    assert inst._matching_process_ids() == [101]


def test_launch_with_mod_cache_prewarm_waits_for_first_instance(monkeypatch):
    events: list[tuple[str, int]] = []
    sleeps: list[float] = []

    class FakeInstance:
        def __init__(self, config, project_dir, *, game_dir=None, log_path=None):
            self.config = config
            self.project_dir = project_dir
            self.game_dir = game_dir
            self.log_path = log_path
            self.process = None

        def start(self):
            events.append(("start", self.config.instance_id))

        def wait_for_ready(self, timeout: float = 120.0) -> bool:
            events.append(("wait", self.config.instance_id))
            return True

        def stop(self):
            events.append(("stop", self.config.instance_id))

    monkeypatch.setattr(launcher, "MCInstance", FakeInstance)
    monkeypatch.setattr(launcher.time, "sleep", lambda seconds: sleeps.append(seconds))
    launch = Launcher(ROOT)
    configs = [NetheriteConfig(instance_id=i) for i in range(3)]

    instances = launch.launch_with_mod_cache_prewarm(
        configs,
        timeout=3.0,
        stagger_seconds=0.25,
    )

    assert len(instances) == 3
    assert events == [
        ("start", 0),
        ("wait", 0),
        ("start", 1),
        ("start", 2),
    ]
    assert sleeps == [0.25, 0.25]


def test_cleanup_processed_mods_removes_cached_files(tmp_path: Path):
    processed_mods = tmp_path / "run" / ".fabric" / "processedMods"
    processed_mods.mkdir(parents=True)
    (processed_mods / "lithium.jar").write_text("x", encoding="utf-8")
    (processed_mods / "sodium.jar").write_text("y", encoding="utf-8")

    launch = Launcher(tmp_path)
    launch.cleanup_processed_mods()

    assert processed_mods.exists()
    assert list(processed_mods.iterdir()) == []


def test_cleanup_instance_run_dirs_removes_isolated_game_dirs(tmp_path: Path):
    launch = Launcher(tmp_path)
    run_dir = launch.instance_run_dir(3)
    run_dir.mkdir(parents=True)
    (run_dir / "marker.txt").write_text("x", encoding="utf-8")

    launch.cleanup_instance_run_dirs([3])

    assert not run_dir.exists()
