"""Per-(seed,stage) success probe for a trained net in the batched env.

Assigns LANES_PER lanes to every (seed, stage) snapshot, runs one episode
per lane (sampled policy, EP_LEN ticks) and prints the success matrix.
Diagnostic only - the transfer gate stays eval_coal.py on the real env.

Run: cd magma && COAL_NET=coal_net_cu.pt uv run --no-project \
       --with numpy,torch python blaze/env/eval_batched.py
"""
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.join(os.path.dirname(HERE), "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)

from blaze import VecBlaze, CUDA_SO                     # noqa: E402
from ppo_coal import ConvPolicy, STACK, REPEAT, EP_LEN  # noqa: E402
from ppo_coal_cu import (TRAIN_SEEDS, STAGES, NOOP,     # noqa: E402
                         build_frame, obs_float)

OUT = os.path.join(RL, "out")
LANES_PER = int(os.environ.get("LANES_PER", 64))
EP_TICKS = int(os.environ.get("EP_TICKS", EP_LEN))
DEVICE = int(os.environ.get("BLAZE_DEV", 1))
GREEDY = bool(int(os.environ.get("GREEDY", 0)))


def main():
    torch.manual_seed(0)
    dev = torch.device(f"cuda:{DEVICE}")
    net_file = os.environ.get("COAL_NET", "coal_net_cu.pt")
    net = ConvPolicy().to(dev)
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location=dev))
    net.eval()

    combos = [(s, d) for s in TRAIN_SEEDS for d in STAGES]
    paths = [os.path.join(OUT, "snaps", f"s{s}_d{d}.bsnp")
             for s, d in combos]
    n = len(combos) * LANES_PER
    env = VecBlaze(n, device=DEVICE, so_path=CUDA_SO)
    env.load_snapshots(paths)
    env.assign([i // LANES_PER for i in range(n)])
    env.reset()

    noop = torch.tensor(NOOP, dtype=torch.int32, device=dev)
    acts = noop.repeat(n, 1)
    cam, depth, edge, scal, rew, done, pose = env.step(acts, repeat=REPEAT)
    frame = build_frame(cam, depth, edge)
    stack = frame.repeat(1, STACK, 1, 1)
    finished = torch.zeros(n, dtype=torch.bool, device=dev)
    succ = torch.zeros(n, dtype=torch.bool, device=dev)

    for t in range(EP_TICKS // REPEAT):
        with torch.no_grad():
            logits, _ = net(obs_float(stack), scal.clone())
            if GREEDY:
                a = torch.stack([lg.argmax(dim=1) for lg in logits], dim=1)
            else:
                a = torch.stack(
                    [torch.distributions.Categorical(logits=lg).sample()
                     for lg in logits], dim=1)
        cam, depth, edge, scal, rew, done, pose = env.step(
            a.to(torch.int32), repeat=REPEAT)
        term = done > 0
        succ |= (done == 1) & ~finished
        finished |= term
        frame = build_frame(cam, depth, edge)
        stack[:, :-5] = stack[:, 5:].clone()
        stack[:, -5:] = frame
        if finished.all():
            break

    succ = succ.view(len(combos), LANES_PER).float().mean(dim=1).cpu()
    print(f"net {net_file}  {'greedy' if GREEDY else 'sampled'}  "
          f"{LANES_PER} lanes per combo")
    print("seed   " + "  ".join(f"d{d}" for d in STAGES))
    per_stage = {d: [] for d in STAGES}
    for k, (s, d) in enumerate(combos):
        per_stage[d].append(float(succ[k]))
    for s in TRAIN_SEEDS:
        row = [float(succ[combos.index((s, d))]) for d in STAGES]
        print(f"s{s:<5d} " + "  ".join(f"{v:.2f}" for v in row))
    print("mean   " + "  ".join(
        f"{np.mean(per_stage[d]):.2f}" for d in STAGES))
    env.close()


if __name__ == "__main__":
    main()
