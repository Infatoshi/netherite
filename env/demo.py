"""Live demo: MC agent walks forward + jumps, matplotlib viewer. Press Q to quit."""

import sys
import time

import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, "env")
from netherite_env import NetheriteEnv
from config import NetheriteConfig


def main():
    cfg = NetheriteConfig(width=1708, height=960)
    env = NetheriteEnv(config=cfg, timeout=5.0)

    print("Connecting to MC instance...")
    obs, _ = env.reset()
    print(f"Connected. pos={obs['position']}, hp={obs['health'][0]:.0f}")

    fig, ax = plt.subplots(1, 1, figsize=(10, 5.6))
    img = ax.imshow(obs["pov"])
    ax.axis("off")
    ax.set_title("Netherite -- press Q to quit")
    plt.tight_layout()
    plt.ion()
    plt.show()

    running = True

    def on_key(event):
        nonlocal running
        if event.key == "q":
            running = False

    fig.canvas.mpl_connect("key_press_event", on_key)

    step = 0
    t0 = time.perf_counter()

    while running:
        action = {
            "forward": 1,
            "jump": 1,
            "sprint": 1,
            "camera": np.array([0.5, 0.0], dtype=np.float32),
        }
        obs, _, _, _, _ = env.step(action)
        step += 1

        # Update display every 3 steps to keep matplotlib responsive
        if step % 3 == 0:
            img.set_data(obs["pov"])
            elapsed = time.perf_counter() - t0
            pos = obs["position"]
            ax.set_title(
                f"step {step} | {step/elapsed:.1f} sps | "
                f"pos ({pos[0]:.0f}, {pos[1]:.0f}, {pos[2]:.0f}) | "
                f"hp {obs['health'][0]:.0f} | Q to quit"
            )
            fig.canvas.draw_idle()
            fig.canvas.flush_events()

        if not plt.fignum_exists(fig.number):
            break

    env.close()
    plt.close("all")
    elapsed = time.perf_counter() - t0
    print(f"Done. {step} steps in {elapsed:.1f}s = {step/elapsed:.1f} steps/sec")


if __name__ == "__main__":
    main()
