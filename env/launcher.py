"""Launch and manage MC instances for RL training."""

import os
import shutil
import signal
import subprocess
import time
from pathlib import Path

from config import NetheriteConfig
from startup_trace import ensure_trace_parent, trace_event


def _shmem_path(name: str) -> str:
    if os.uname().sysname == "Darwin":
        return f"/tmp/{name}"
    return f"/dev/shm/{name}"


class MCInstance:
    """A single managed MC client process."""

    def __init__(
        self,
        config: NetheriteConfig,
        project_dir: Path,
        *,
        game_dir: Path | None = None,
        log_path: Path | None = None,
    ):
        self.config = config
        self.project_dir = project_dir
        self.game_dir = game_dir
        self.log_path = log_path
        self.process: subprocess.Popen | None = None
        self._log_handle = None

    def start(self):
        env = os.environ.copy()
        if self.config.java_home:
            env["JAVA_HOME"] = self.config.java_home

        self._prepare_game_dir()
        cmd = self._build_launch_command()
        stdout_target = subprocess.DEVNULL

        if self.log_path is not None:
            ensure_trace_parent(self.log_path)
            self._log_handle = self.log_path.open("wb")
            stdout_target = self._log_handle

        trace_event(
            "launch.instance.spawn.begin",
            instance_id=self.config.instance_id,
            game_dir=self.game_dir,
            log_path=self.log_path,
        )

        try:
            self.process = subprocess.Popen(
                cmd,
                cwd=str(self.project_dir),
                env=env,
                start_new_session=True,
                stdout=stdout_target,
                stderr=subprocess.STDOUT,
            )
        except Exception:
            if self._log_handle is not None:
                self._log_handle.close()
                self._log_handle = None
            raise
        trace_event(
            "launch.instance.spawned",
            instance_id=self.config.instance_id,
            pid=self.process.pid,
            game_dir=self.game_dir,
        )

    def _prepare_game_dir(self):
        if self.game_dir is None:
            return

        self.game_dir.mkdir(parents=True, exist_ok=True)
        shared_mods = self.project_dir / "run" / "mods"
        instance_mods = self.game_dir / "mods"

        if shared_mods.exists() and not instance_mods.exists():
            try:
                instance_mods.symlink_to(shared_mods, target_is_directory=True)
            except OSError:
                shutil.copytree(shared_mods, instance_mods, dirs_exist_ok=True)

    def _build_launch_command(self) -> list[str]:
        gradlew = self.project_dir / "gradlew"
        cmd = [str(gradlew), "runClient"]
        cmd.extend(self.config.to_gradle_args())
        username = f"netherite_{self.config.instance_id}"
        args = [
            "--width",
            str(self.config.width),
            "--height",
            str(self.config.height),
            "--username",
            username,
        ]
        if self.game_dir is not None:
            args.extend(["--gameDir", str(self.game_dir)])
        cmd.append("--args=" + " ".join(args))
        return cmd

    def wait_for_ready(self, timeout: float = 120.0) -> bool:
        """Wait until shmem files appear and have valid magic numbers."""
        import mmap
        import struct

        iid = self.config.instance_id
        state_path = _shmem_path(f"netherite_state_{iid}")
        deadline = time.monotonic() + timeout
        saw_state_path = False

        trace_event(
            "launch.instance.ready_wait.begin",
            instance_id=iid,
            timeout=timeout,
            state_path=state_path,
        )

        while time.monotonic() < deadline:
            if not os.path.exists(state_path):
                time.sleep(1.0)
                continue
            if not saw_state_path:
                saw_state_path = True
                trace_event(
                    "launch.instance.state_shmem_seen",
                    instance_id=iid,
                    state_path=state_path,
                )
            try:
                fd = os.open(state_path, os.O_RDONLY)
                mm = mmap.mmap(fd, 64 * 1024, access=mmap.ACCESS_READ)
                os.close(fd)
                magic, tick, _, ready = struct.unpack("<IIII", mm[:16])
                mm.close()
                # ready=1 is set during init, before player spawns
                if magic == 0x4E455453 and ready == 1:
                    trace_event(
                        "launch.instance.ready_ok",
                        instance_id=iid,
                        tick=tick,
                        ready=ready,
                    )
                    return True
            except Exception:
                pass
            time.sleep(1.0)
        trace_event("launch.instance.ready_timeout", instance_id=iid, timeout=timeout)
        return False

    def _matching_process_ids(self) -> list[int]:
        identity_tokens = [f"-Dnetherite.instance_id={self.config.instance_id}"]
        if self.game_dir is not None:
            identity_tokens.append(f"--gameDir {self.game_dir}")

        try:
            output = subprocess.check_output(
                ["ps", "-axo", "pid=,command="],
                text=True,
            )
        except (OSError, subprocess.SubprocessError):
            return []

        project_token = str(self.project_dir)
        pids: list[int] = []
        for raw_line in output.splitlines():
            line = raw_line.strip()
            if not line:
                continue
            pid_text, _, command = line.partition(" ")
            if not pid_text.isdigit():
                continue
            pid = int(pid_text)
            if self.process is not None and pid == self.process.pid:
                continue
            if project_token not in command:
                continue
            if not any(token in command for token in identity_tokens):
                continue
            if (
                "net.fabricmc.devlaunchinjector.Main" not in command
                and "GradleWrapperMain runClient" not in command
            ):
                continue
            pids.append(pid)
        return pids

    def _wait_for_matching_processes_exit(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self._matching_process_ids():
                return True
            time.sleep(0.1)
        return not self._matching_process_ids()

    def _terminate_lingering_processes(self):
        lingering = self._matching_process_ids()
        if not lingering:
            return

        for sig, timeout in (
            (signal.SIGTERM, 2.0),
            (signal.SIGKILL, 1.0),
        ):
            for pid in lingering:
                try:
                    os.kill(pid, sig)
                except OSError:
                    continue
            if self._wait_for_matching_processes_exit(timeout):
                return
            lingering = self._matching_process_ids()
            if not lingering:
                return

    def stop(self):
        if self.process and self.process.poll() is None:
            try:
                pgid = os.getpgid(self.process.pid)
                os.killpg(pgid, signal.SIGTERM)
            except OSError:
                self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                try:
                    pgid = os.getpgid(self.process.pid)
                    os.killpg(pgid, signal.SIGKILL)
                except OSError:
                    self.process.kill()
                self.process.wait(timeout=5)
        self._terminate_lingering_processes()
        if self._log_handle is not None:
            self._log_handle.close()
            self._log_handle = None

    @property
    def alive(self) -> bool:
        return self.process is not None and self.process.poll() is None


class Launcher:
    """Manage multiple MC instances for parallel RL."""

    def __init__(self, project_dir: str | Path):
        self.project_dir = Path(project_dir)
        self.instances: list[MCInstance] = []

    def instance_run_dir(self, instance_id: int) -> Path:
        return self.project_dir / "run" / "instances" / str(instance_id)

    def instance_log_path(self, instance_id: int) -> Path:
        return self.project_dir / "run" / "instance_logs" / f"{instance_id}.log"

    def launch(self, configs: list[NetheriteConfig]) -> list[MCInstance]:
        """Launch N MC instances in parallel."""
        instances = []
        for cfg in configs:
            inst = MCInstance(
                cfg,
                self.project_dir,
                game_dir=self.instance_run_dir(cfg.instance_id),
                log_path=self.instance_log_path(cfg.instance_id),
            )
            inst.start()
            instances.append(inst)
        self.instances.extend(instances)
        return instances

    def launch_with_mod_cache_prewarm(
        self,
        configs: list[NetheriteConfig],
        *,
        timeout: float = 120.0,
        stagger_seconds: float = 0.0,
    ) -> list[MCInstance]:
        """Launch one instance first to validate startup before fanning out.

        Each launched instance uses its own game dir under `run/instances/<id>`,
        which gives Fabric a private `.fabric/processedMods` cache.
        """
        if not configs:
            return []

        launched: list[MCInstance] = []
        try:
            trace_event(
                "launch.prewarm.begin",
                instance_id=configs[0].instance_id,
                timeout=timeout,
            )
            first = MCInstance(
                configs[0],
                self.project_dir,
                game_dir=self.instance_run_dir(configs[0].instance_id),
                log_path=self.instance_log_path(configs[0].instance_id),
            )
            first.start()
            launched.append(first)
            if not first.wait_for_ready(timeout=timeout):
                trace_event(
                    "launch.prewarm.ready_timeout",
                    instance_id=first.config.instance_id,
                    timeout=timeout,
                )
                raise RuntimeError(
                    f"Instance {first.config.instance_id} failed to start during startup validation"
                )
            trace_event(
                "launch.prewarm.ready_ok",
                instance_id=first.config.instance_id,
            )

            for cfg in configs[1:]:
                inst = MCInstance(
                    cfg,
                    self.project_dir,
                    game_dir=self.instance_run_dir(cfg.instance_id),
                    log_path=self.instance_log_path(cfg.instance_id),
                )
                inst.start()
                launched.append(inst)
                trace_event(
                    "launch.fanout.instance.spawned",
                    instance_id=cfg.instance_id,
                )
                if stagger_seconds > 0:
                    time.sleep(stagger_seconds)

            trace_event("launch.fanout.done", count=len(launched))
            self.instances.extend(launched)
            return launched
        except Exception:
            for inst in launched:
                inst.stop()
            raise

    def wait_all_ready(self, timeout: float = 120.0) -> bool:
        """Wait for all instances to be ready."""
        deadline = time.monotonic() + timeout
        for inst in self.instances:
            remaining = deadline - time.monotonic()
            trace_event(
                "launch.all_ready.wait.begin",
                instance_id=inst.config.instance_id,
                timeout=remaining,
            )
            if remaining <= 0 or not inst.wait_for_ready(timeout=remaining):
                trace_event(
                    "launch.all_ready.wait.timeout",
                    instance_id=inst.config.instance_id,
                    timeout=max(remaining, 0.0),
                )
                return False
            trace_event(
                "launch.all_ready.wait.done",
                instance_id=inst.config.instance_id,
            )
        trace_event("launch.all_ready.done", count=len(self.instances))
        return True

    def stop_all(self):
        for inst in self.instances:
            inst.stop()
        self.instances.clear()

    def cleanup_processed_mods(self):
        """Remove Fabric processed mod caches from all known run dirs."""
        candidates = [self.project_dir / "run" / ".fabric" / "processedMods"]
        instances_root = self.project_dir / "run" / "instances"
        if instances_root.exists():
            candidates.extend(instances_root.glob("*/.fabric/processedMods"))

        for processed_mods in candidates:
            if not processed_mods.exists():
                continue
            for path in processed_mods.iterdir():
                if path.is_file() or path.is_symlink():
                    path.unlink()
                else:
                    shutil.rmtree(path)

    def cleanup_instance_run_dirs(self, instance_ids: list[int]):
        for instance_id in instance_ids:
            run_dir = self.instance_run_dir(instance_id)
            if run_dir.exists():
                shutil.rmtree(run_dir)

    def cleanup_shmem(self):
        """Remove all netherite shmem files."""
        import glob

        prefix = "/tmp" if os.uname().sysname == "Darwin" else "/dev/shm"
        for path in glob.glob(f"{prefix}/netherite_*"):
            try:
                os.remove(path)
            except OSError:
                pass
