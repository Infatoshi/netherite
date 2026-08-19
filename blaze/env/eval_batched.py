"""Per-(seed,stage) success probe for a trained net in the batched env.

Assigns LANES_PER lanes to every (seed, stage) snapshot, runs one episode
per lane (sampled policy, EP_LEN ticks) and prints the success matrix.
Diagnostic only - the transfer gate stays eval_coal.py on the real env.

Run: cd magma && uv run --no-project --with numpy,torch \
       python blaze/env/eval_batched.py --coal-net coal_net_cu.pt
"""
import argparse
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


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", type=int, default=1,
                    help="CUDA device (was BLAZE_DEV, default 1)")
    ap.add_argument("--lanes-per", type=int, default=64)
    ap.add_argument("--ep-ticks", type=int, default=EP_LEN)
    ap.add_argument("--greedy", action="store_true")
    ap.add_argument("--coal-net", default="coal_net_cu.pt")
    args = ap.parse_args(argv)
    torch.manual_seed(0)
    dev = torch.device(f"cuda:{args.device}")
    net_file = args.coal_net
    net = ConvPolicy().to(dev)
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location=dev))
    net.eval()

    combos = [(s, d) for s in TRAIN_SEEDS for d in STAGES]
    paths = [os.path.join(OUT, "snaps", f"s{s}_d{d}.bsnp")
             for s, d in combos]
    n = len(combos) * args.lanes_per
    env = VecBlaze(n, device=args.device, so_path=CUDA_SO)
    env.load_snapshots(paths)
    env.assign([i // args.lanes_per for i in range(n)])
    env.reset()

    noop = torch.tensor(NOOP, dtype=torch.int32, device=dev)
    acts = noop.repeat(n, 1)
    cam, depth, edge, scal, rew, done, pose = env.step(acts, repeat=REPEAT)
    frame = build_frame(cam, depth, edge)
    stack = frame.repeat(1, STACK, 1, 1)
    finished = torch.zeros(n, dtype=torch.bool, device=dev)
    succ = torch.zeros(n, dtype=torch.bool, device=dev)

    for t in range(args.ep_ticks // REPEAT):
        with torch.no_grad():
            logits, _ = net(obs_float(stack), scal.clone())
            if args.greedy:
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

    succ = succ.view(len(combos), args.lanes_per).float().mean(dim=1).cpu()
    print(f"net {net_file}  {'greedy' if args.greedy else 'sampled'}  "
          f"{args.lanes_per} lanes per combo")
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
    main(sys.argv[1:])
