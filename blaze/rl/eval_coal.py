"""Evaluate the trained mine-coal policy inside the full chain.

Runs the scripted prefix (chop -> craft -> pick -> dig), then hands control
to coal_net.pt with NO scripted help; on success, crafts torches. Reports
per-seed success and saves the best full episode's action stream for
make_chain_video.py (chain_actions_s<seed>_learned.json).

Run (anvil): cd magma && uv run --no-project --with numpy,torch python rl/eval_coal.py
"""
import json
import os
import sys

import numpy as np
import torch

import chain_probe as cp
from ppo_coal import (ConvPolicy, planes, act_dict, make_env, STACK,
                      EP_LEN, REPEAT, IX_COAL)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")


def run_policy(env, net, greedy=True):
    """Learned burrow phase. Returns True when coal is mined."""
    base = cp.inv(env, IX_COAL)
    stack = [planes(env.obs)[0]] * STACK
    for _ in range(EP_LEN // REPEAT):
        stack.pop(0)
        stack.append(planes(env.obs)[0])
        p = torch.from_numpy(np.concatenate(stack)[None])
        s = torch.from_numpy(planes(env.obs)[1][None])
        with torch.no_grad():
            logits, _ = net(p, s)
        if greedy:
            idx = tuple(int(lg.argmax()) for lg in logits)
        else:
            idx = tuple(int(torch.distributions.Categorical(logits=lg)
                            .sample()) for lg in logits)
        for rep in range(REPEAT):
            a = act_dict(idx)
            if rep > 0:
                a = dict(a)
                a["dyaw"] = 0.0
                a["dpitch"] = 0.0
                if rep < REPEAT - 1:    # camera only where obs are consumed
                    a["cam"] = 0
            env.send(a)
            env.recv()
            if cp.inv(env, IX_COAL) > base:
                return True
    return False


def main():
    prefixes = json.load(open(os.path.join(OUT, "coal_prefixes.json")))
    net = ConvPolicy()
    net_file = os.environ.get("COAL_NET", "coal_net.pt")
    net.load_state_dict(torch.load(os.path.join(OUT, net_file),
                                   weights_only=True))
    net.eval()

    tries = int(os.environ.get("TRIES", 3))
    greedy = bool(int(os.environ.get("GREEDY", 0)))
    torch.manual_seed(0)
    results = {}
    for seed_s, prefix in sorted(prefixes.items(), key=lambda kv: int(kv[0])):
        seed = int(seed_s)
        for attempt in range(tries):
            env = make_env(seed, prefix)
            try:
                ok = run_policy(env, net, greedy=greedy)
                torches = False
                if ok:
                    torches = cp.stage_torch(env)
                if ok or attempt == tries - 1:
                    results[seed] = (ok, torches, len(env.actions))
                    print(f"seed {seed:3d}: coal {'OK ' if ok else 'no '} "
                          f"torches {'OK' if torches else 'no'} "
                          f"({len(env.actions)} ticks, try {attempt + 1})",
                          flush=True)
                if ok and torches:
                    path = os.path.join(
                        OUT, f"chain_actions_s{seed}_learned.json")
                    if not os.path.exists(path):
                        with open(path, "w") as f:
                            json.dump(env.actions, f)
                if ok:
                    break
            finally:
                env.close()

    n_ok = sum(1 for ok, _, _ in results.values() if ok)
    print(f"\nlearned mine-coal: {n_ok}/{len(results)} seeds", flush=True)
    return 0 if n_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
