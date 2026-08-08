"""Per-seed milestone matrix for a trained chain net in the batched env.

LANES_PER lanes per t0 seed (all 13 by default, incl. held-out 11/33), one
sampled episode each, reports the deepest milestone reached per lane and the
full-chain (torch) rate. Diagnostic; the transfer gate is eval_chain_rl.py
on the real env.

Run: cd magma && CHAIN_NET=chain_net_cu.pt uv run --no-project \
       --with numpy,torch python blaze/env/eval_chain_batched.py
"""
import os
import sys

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.join(os.path.dirname(HERE), "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)

from blaze import VecBlaze, CUDA_SO                          # noqa: E402
from ppo_chain_cu import (ChainPolicy, build_frame, build_scal, obs_float,
                          stage_of_best, acts_to_rows, NPLANES, STACK,
                          REPEAT, EP_DEC, NHEAD, N_STAGES,
                          MILE_NAMES)                        # noqa: E402

OUT = os.path.join(RL, "out")
SNAPS = os.path.join(OUT, "snaps")
SEEDS = [int(s) for s in os.environ.get(
    "SEEDS", "2,3,10,11,14,16,20,27,29,32,33,44,46").split(",")]
HELD_OUT = {11, 33}
LANES_PER = int(os.environ.get("LANES_PER", 64))
DEVICE = int(os.environ.get("BLAZE_DEV", 0))
EP = int(os.environ.get("EP_DEC", EP_DEC))


def main():
    torch.manual_seed(0)
    dev = torch.device(f"cuda:{DEVICE}")
    net = ChainPolicy().to(dev)
    net_file = os.environ.get("CHAIN_NET", "chain_net_cu.pt")
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True, map_location=dev))
    net.eval()

    paths = [os.path.join(SNAPS, f"s{s}_t0.bsnp") for s in SEEDS]
    n = len(SEEDS) * LANES_PER
    env = VecBlaze(n, device=DEVICE, so_path=CUDA_SO)
    env.set_success_item(50)
    env.load_snapshots(paths)
    env.assign([i // LANES_PER for i in range(n)])
    env.reset()

    noop = torch.zeros((n, NHEAD), dtype=torch.int64, device=dev)
    noop[:, 0] = noop[:, 1] = noop[:, 2] = 1
    cam, depth, edge, scal, rew, done, pose = env.step(
        acts_to_rows(noop, dev), repeat=REPEAT)
    frame = build_frame(cam, depth, edge)
    stack = frame.repeat(1, STACK, 1, 1)
    best = env.status[:, :9].clone()
    finished = torch.zeros(n, dtype=torch.bool, device=dev)
    succ = torch.zeros(n, dtype=torch.bool, device=dev)
    ep_dec = torch.zeros(n, dtype=torch.float32, device=dev)

    for t in range(EP):
        scal_full = build_scal(scal, env.status, pose, ep_dec / EP)
        with torch.no_grad():
            logits, _ = net(obs_float(stack), scal_full)
            a = torch.stack(
                [torch.distributions.Categorical(logits=lg).sample()
                 for lg in logits], dim=1)
        cam, depth, edge, scal, rew, done, pose = env.step(
            acts_to_rows(a, dev), repeat=REPEAT)
        alive = ~finished
        best[alive] = torch.maximum(best[alive], env.status[alive, :9])
        succ |= (done == 1) & alive
        finished |= done > 0
        ep_dec += 1
        frame = build_frame(cam, depth, edge)
        stack = torch.cat([stack[:, NPLANES:], frame], dim=1)
        if finished.all():
            break

    reach = stage_of_best(best)
    reach[succ] = N_STAGES
    reach = reach.view(len(SEEDS), LANES_PER).cpu().numpy()
    print(f"net {net_file}  sampled  {LANES_PER} lanes/seed  "
          f"{EP} decisions")
    print("seed    " + "  ".join(f"{m:>7s}" for m in
                                 (*MILE_NAMES[1:], "TORCH")))
    for i, s in enumerate(SEEDS):
        row = [float((reach[i] >= m).mean()) for m in range(1, N_STAGES + 1)]
        ho = "*" if s in HELD_OUT else " "
        print(f"s{s:<4d}{ho}  " + "  ".join(f"{v:7.2f}" for v in row))
    allr = reach.reshape(-1)
    print("mean    " + "  ".join(
        f"{float((allr >= m).mean()):7.2f}" for m in range(1, N_STAGES + 1)))
    env.close()


if __name__ == "__main__":
    main()
