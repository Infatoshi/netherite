"""Launch and manage MC instances for RL training."""

import os
import signal
import subprocess
import time
from pathlib import Path

from config import NetheriteConfig


def _shmem_path(name: str) -> str:
    if os.uname().sysname == "Darwin":
        return f"/tmp/{name}"
    return f"/dev/shm/{name}"


class MCInstance:
    """A single managed MC client process."""

    def __init__(self, config: NetheriteConfig, project_dir: Path):
        self.config = config
        self.project_dir = project_dir
        self.process: subprocess.Popen | None = None

    def start(self):
        env = os.environ.copy()
        if self.config.java_home:
            env["JAVA_HOME"] = self.config.java_home

        cmd = self._build_launch_command()

        self.process = subprocess.Popen(
            cmd,
            cwd=str(self.project_dir),
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def _build_launch_command(self) -> list[str]:
        gradlew = self.project_dir / "gradlew"
        cmd = [str(gradlew), "runClient"]
        cmd.extend(self.config.to_gradle_args())
        username = f"netherite_{self.config.instance_id}"
        cmd.append(
            f"--args=--width {self.config.width} --height {self.config.height} --username {username}"
        )
        return cmd

    def wait_for_ready(self, timeout: float = 120.0) -> bool:
        """Wait until shmem files appear and have valid magic numbers."""
        import mmap
        import struct

        iid = self.config.instance_id
        state_path = _shmem_path(f"netherite_state_{iid}")
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            if not os.path.exists(state_path):
                time.sleep(1.0)
                continue
            try:
                fd = os.open(state_path, os.O_RDONLY)
                mm = mmap.mmap(fd, 64 * 1024, access=mmap.ACCESS_READ)
                os.close(fd)
                magic, tick, _, ready = struct.unpack("<IIII", mm[:16])
                mm.close()
                # ready=1 is set during init, before player spawns
                if magic == 0x4E455453 and ready == 1:
                    return True
            except Exception:
                pass
            time.sleep(1.0)
        return False

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()

    @property
    def alive(self) -> bool:
        return self.process is not None and self.process.poll() is None


class Launcher:
    """Manage multiple MC instances for parallel RL."""

    def __init__(self, project_dir: str | Path):
        self.project_dir = Path(project_dir)
        self.instances: list[MCInstance] = []

    def launch(self, configs: list[NetheriteConfig]) -> list[MCInstance]:
        """Launch N MC instances in parallel."""
        instances = []
        for cfg in configs:
            inst = MCInstance(cfg, self.project_dir)
            inst.start()
            instances.append(inst)
        self.instances.extend(instances)
        return instances

    def wait_all_ready(self, timeout: float = 120.0) -> bool:
        """Wait for all instances to be ready."""
        deadline = time.monotonic() + timeout
        for inst in self.instances:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not inst.wait_for_ready(timeout=remaining):
                return False
        return True

    def stop_all(self):
        for inst in self.instances:
            inst.stop()
        self.instances.clear()

    def cleanup_shmem(self):
        """Remove all netherite shmem files."""
        import glob

        prefix = "/tmp" if os.uname().sysname == "Darwin" else "/dev/shm"
        for path in glob.glob(f"{prefix}/netherite_*"):
            try:
                os.remove(path)
            except OSError:
                pass
