"""Smoke test: connect to running MC instance, read a frame, display it."""

import sys
import time

import numpy as np

from netherite_env import NetheriteEnv


def main():
    instance_id = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    env = NetheriteEnv(instance_id=instance_id, timeout=10.0)

    print(f"Connecting to instance {instance_id}...")
    obs, info = env.reset()

    pov = obs["pov"]
    pos = obs["position"]
    hp = obs["health"]
    print(f"Frame shape: {pov.shape}, dtype: {pov.dtype}")
    print(f"Position: {pos}")
    print(f"Health: {hp}")
    print(f"Non-zero pixels: {np.count_nonzero(pov)}")

    # Step a few times with forward movement
    for i in range(20):
        action = {"forward": 1, "camera": np.array([0.0, 0.0], dtype=np.float32)}
        obs, reward, term, trunc, info = env.step(action)
        pos = obs["position"]
        print(f"Step {i}: pos={pos[0]:.2f},{pos[1]:.2f},{pos[2]:.2f}")

    env.close()
    print("Done.")


if __name__ == "__main__":
    main()
