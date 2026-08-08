"""blaze_cuda.so liveness probe.

A .so built for the wrong SM makes every blaze_step a SILENT no-op: rc=0,
frozen outputs, and every downstream benchmark measures an env that never
ticked. This probe fails loudly instead.

Checks, in order:
  1. the .so is a fatbin containing every arch in --arch (cuobjdump)
  2. blaze_create / load_snapshots / reset succeed
  3. one step with forward=1 moves the player: max |pose delta| > 0
  4. the camera plane is non-degenerate (not all zero)

Run:
  UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp \
    uv run --no-project --with numpy --with torch \
    python blaze/rl/flywheel/so_sanity.py
"""
import os
import subprocess
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ENV = os.path.abspath(os.path.join(HERE, "..", "..", "env"))
RL = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, ENV)

from blaze import CUDA_SO, VecBlaze

SNAPS = os.path.join(RL, "out", "snaps")
WANT_ARCH = os.environ.get("BLAZE_WANT_ARCH", "sm_86,sm_120").split(",")


def main():
    fails = []

    out = subprocess.run(["cuobjdump", "-lelf", CUDA_SO],
                         capture_output=True, text=True,
                         check=False).stdout
    for a in WANT_ARCH:
        if f".{a}." not in out and f"{a}.cubin" not in out:
            fails.append(f"arch {a} missing from {CUDA_SO}")
    print(f"so={CUDA_SO} arches_wanted={WANT_ARCH}")

    seeds = [2, 3, 10]
    paths = [os.path.join(SNAPS, f"s{s}_t0.bsnp") for s in seeds]
    for p in paths:
        if not os.path.exists(p):
            fails.append(f"snapshot missing: {p}")
    if fails:
        print("SO_SANITY FAIL\n  " + "\n  ".join(fails))
        return 1

    n = 64
    dev = int(os.environ.get("BLAZE_DEV", "0"))
    env = VecBlaze(n, device=dev, so_path=CUDA_SO)
    env.load_snapshots(paths)
    env.assign([i % len(paths) for i in range(n)])
    env.reset()

    rows = torch.zeros((n, 13), dtype=torch.float64,
                       device=f"cuda:{dev}")
    rows[:, 0] = 1.0                      # forward
    # one step first: blaze_step is what FILLS pose, so a pre-step snapshot
    # of the zeroed buffer would "move" for free.
    env.step(rows, repeat=4)
    pose0 = env.pose.clone()
    for _ in range(4):
        env.step(rows, repeat=4)
    dxz = (env.pose[:, [0, 2]] - pose0[:, [0, 2]]).abs().max().item()
    cam_nz = int((env.cam != 0).sum().item())
    print(f"pose_xz_delta_max={dxz:.6f}  cam_nonzero={cam_nz}")
    if not dxz > 0.0:
        fails.append(f"player xz frozen after 4 forward steps (delta={dxz})")
    if cam_nz == 0:
        fails.append("camera plane is all zero")
    env.close()

    if fails:
        print("SO_SANITY FAIL\n  " + "\n  ".join(fails))
        return 1
    print("SO_SANITY PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
