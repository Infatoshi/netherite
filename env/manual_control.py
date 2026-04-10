"""Interactive matplotlib viewer for manually controlling the Minecraft client."""

from __future__ import annotations

import shutil
import struct
import time
from pathlib import Path

import numpy as np

from demo import build_demo_config
from launcher import Launcher, MCInstance
from netherite_env import NetheriteEnv

ROOT = Path(__file__).resolve().parents[1]


class ManualController:
    """Translate held keyboard state into Netherite action dictionaries."""

    def __init__(self, *, camera_speed: float = 4.0):
        self.camera_speed = camera_speed
        self.running = True
        self._pressed: set[str] = set()

    def on_key_press(self, key: str | None):
        normalized = self._normalize_key(key)
        if normalized is None:
            return
        if normalized == "q":
            self.running = False
            return
        self._pressed.add(normalized)

    def on_key_release(self, key: str | None):
        normalized = self._normalize_key(key)
        if normalized is None:
            return
        self._pressed.discard(normalized)

    def build_action(self) -> dict[str, object]:
        yaw = 0.0
        pitch = 0.0
        if self._pressed_any("left", "j"):
            yaw -= self.camera_speed
        if self._pressed_any("right", "l"):
            yaw += self.camera_speed
        if self._pressed_any("up", "i"):
            pitch -= self.camera_speed
        if self._pressed_any("down", "k"):
            pitch += self.camera_speed

        return {
            "forward": int("w" in self._pressed),
            "back": int("s" in self._pressed),
            "left": int("a" in self._pressed),
            "right": int("d" in self._pressed),
            "jump": int("space" in self._pressed),
            "sneak": int(self._pressed_any("x", "shift")),
            "sprint": int(self._pressed_any("c", "ctrl", "control")),
            "attack": int("f" in self._pressed),
            "use": int("e" in self._pressed),
            "camera": np.array([yaw, pitch], dtype=np.float32),
        }

    def controls_text(self) -> str:
        return (
            "WASD move | Space jump | C/Ctrl sprint | X/Shift sneak | "
            "E use | F attack | Arrows/IJKL look | Q quit"
        )

    @staticmethod
    def _normalize_key(key: str | None) -> str | None:
        if key is None:
            return None
        if key == " ":
            return "space"
        return key.lower()

    def _pressed_any(self, *keys: str) -> bool:
        return any(key in self._pressed for key in keys)


def _format_status(
    *,
    seed: int,
    step: int,
    elapsed: float,
    display_frames: int,
    position: np.ndarray,
    pitch: float,
    controls_text: str,
) -> str:
    sim_sps = step / elapsed if elapsed > 0 else 0.0
    display_fps = display_frames / elapsed if elapsed > 0 else 0.0
    return (
        f"seed {seed} | step {step} | sim {sim_sps:.0f} steps/s | "
        f"display {display_fps:.1f} fps | "
        f"pos ({position[0]:.0f}, {position[1]:.0f}, {position[2]:.0f}) | "
        f"pitch {pitch:.0f} | {controls_text}"
    )


def _current_pitch(env: NetheriteEnv) -> float:
    raw = env._state_reader.read_bytes(16, 56)
    return struct.unpack_from("<f", raw, 28)[0]


def main():
    import matplotlib

    matplotlib.use("MacOSX")
    import matplotlib.pyplot as plt

    java_home = "/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
    project_dir = ROOT
    seed = int(time.time())

    Launcher(project_dir).cleanup_shmem()
    world_dir = project_dir / "run" / "saves" / "netherite_0"
    if world_dir.exists():
        shutil.rmtree(world_dir)

    cfg = build_demo_config()
    cfg.instance_id = 0
    cfg.seed = seed
    cfg.headless = True
    cfg.uncapped = True
    cfg.max_fps = 32767
    cfg.java_home = java_home

    inst = MCInstance(cfg, project_dir)
    inst.start()
    print(f"Launching fresh world with seed={seed}, pid={inst.process.pid}")
    if not inst.wait_for_ready(timeout=120.0):
        raise SystemExit("Minecraft client did not become ready")

    env = NetheriteEnv(config=cfg, timeout=10.0)
    obs, _ = env.reset()

    controller = ManualController()
    print("Connected.")
    print(controller.controls_text())

    fig, ax = plt.subplots(1, 1, figsize=(12, 6.75))
    status = fig.suptitle("Connecting viewer...")
    img = ax.imshow(obs["pov"], interpolation="nearest")
    ax.axis("off")
    fig.tight_layout(rect=(0.0, 0.0, 1.0, 0.96))
    plt.ion()
    plt.show()

    def on_key_press(event):
        controller.on_key_press(event.key)

    def on_key_release(event):
        controller.on_key_release(event.key)

    fig.canvas.mpl_connect("key_press_event", on_key_press)
    fig.canvas.mpl_connect("key_release_event", on_key_release)

    step = 0
    display_frames = 0
    t0 = time.perf_counter()
    last_display = t0
    display_interval = 1.0 / 30.0

    try:
        while controller.running:
            obs, _, _, _, _ = env.step(controller.build_action())
            step += 1
            now = time.perf_counter()
            if now - last_display >= display_interval:
                img.set_data(obs["pov"])
                pos = obs["position"]
                elapsed = now - t0
                display_frames += 1
                status.set_text(
                    _format_status(
                        seed=seed,
                        step=step,
                        elapsed=elapsed,
                        display_frames=display_frames,
                        position=pos,
                        pitch=_current_pitch(env),
                        controls_text=controller.controls_text(),
                    )
                )
                fig.canvas.draw()
                fig.canvas.flush_events()
                plt.pause(0.001)
                last_display = now
            if not plt.fignum_exists(fig.number):
                break
    finally:
        env.close()
        inst.stop()
        plt.close("all")
        elapsed = time.perf_counter() - t0
        if elapsed > 0:
            print(f"Done. {step} steps in {elapsed:.1f}s = {step / elapsed:.1f} steps/s")


if __name__ == "__main__":
    main()
