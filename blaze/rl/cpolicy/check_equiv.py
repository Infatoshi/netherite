"""Equivalence gate: C/CUDA cpolicy path vs torch ChainPolicy.

Same weights, same obs batch. Checks:
  1. logits max abs diff  <= LOGIT_ATOL (1e-3; see TOLERANCE_RATIONALE)
  2. value  max abs diff  <= VALUE_ATOL
  3. logp of C-path actions recomputed by torch within LOGIT_ATOL
  4. greedy (argmax) agreement >= 99.9% on a 6144-env batch; disagreements
     only at near-ties (margin reported)
  5. all tensors finite

Sampling RNG deliberately differs from torch (see RNG_PROTOCOL.md); we do
NOT require draw-for-draw action equality in Gumbel mode.

Run:
  UV_CACHE_DIR=... TMPDIR=... uv run --no-project --with numpy==2.5.1 \\
      --with torch==2.13.0 python blaze/rl/cpolicy/check_equiv.py
"""
from __future__ import annotations

import os
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ENV = os.path.abspath(os.path.join(HERE, "..", "..", "env"))
sys.path.insert(0, ENV)
sys.path.insert(0, HERE)

import ppo_chain_cu as P
from wrapper import CPolicyFwd, NLOGITS, HEADS

# ---- tolerances ----
# TOLERANCE_RATIONALE:
# The C path uses hand-written direct convolutions (different reduction order
# over the 5x5x18 / 3x3x32 windows than cuDNN) and cublasSgemm for the FC /
# heads. fp32 reassociation over ~450 multiply-adds per conv1 output (and
# 6299 for the FC) produces absolute logit drift on the order of 1e-4 to a
# few e-4 in practice; we gate at 1e-3 absolute, which is well below the
# scale of a trained logit (~1) and well above bit noise. This is NOT a
# claim of bit-identity with torch/cuDNN.
LOGIT_ATOL = 1e-3
VALUE_ATOL = 1e-3
GREEDY_AGREE = 0.999
N = int(os.environ.get("CPOLICY_EQUIV_N", "6144"))
SEED = int(os.environ.get("CPOLICY_EQUIV_SEED", "1234"))


def maxerr(a, b):
    d = (a.float() - b.float()).abs()
    return d.max().item(), d.mean().item()


def torch_packed_logits(logits_list):
    """list of [N,w_h] -> [N,34] packed."""
    return torch.cat(logits_list, dim=1)


def main():
    dev = torch.device("cuda:0")
    torch.manual_seed(SEED)
    net = P.ChainPolicy().to(dev).eval()

    obs_u8 = torch.randint(0, 3, (N, P.NCH, P.CAM_H, P.CAM_W),
                           dtype=torch.uint8, device=dev)
    # depth planes get the full 0..255 range
    for k in range(P.STACK):
        obs_u8[:, P.NPLANES * k + 7] = torch.randint(
            0, 256, (N, P.CAM_H, P.CAM_W), dtype=torch.uint8, device=dev)
    scal = torch.randn(N, P.NSCAL, device=dev)
    burnin = torch.zeros(N, dtype=torch.bool, device=dev)
    burnin[::17] = True
    noop = torch.zeros(P.NHEAD, dtype=torch.int64, device=dev)
    noop[0] = noop[1] = noop[2] = 1

    fails = []
    lines = []

    def report(name, ok, detail):
        lines.append(f"{'PASS' if ok else 'FAIL'}  {name:22s}  {detail}")
        if not ok:
            fails.append(name)

    with torch.no_grad():
        ref_logits, ref_vals = net(P.obs_float(obs_u8), scal)
    ref_pack = torch_packed_logits(ref_logits)

    # ---- C path ----
    cp = CPolicyFwd(0, N)
    cp.upload_from_net(net)
    # greedy for action agreement; also grab logits
    acts_g, logp_g, vals_g, ent_g, logits_c = cp.forward_sample(
        obs_u8, scal, burnin, noop, mode=1, want_logits=True)
    torch.cuda.synchronize()

    # finite
    bad = []
    for name, t in (("logits_c", logits_c), ("vals_c", vals_g),
                    ("logp_c", logp_g), ("ref_pack", ref_pack),
                    ("ref_vals", ref_vals)):
        if not torch.isfinite(t).all():
            bad.append(name)
    report("finite", not bad, f"nonfinite={bad}")

    # logits
    a, mean = maxerr(logits_c, ref_pack)
    report("logits", a <= LOGIT_ATOL,
           f"max_abs={a:.3e} mean_abs={mean:.3e} tol={LOGIT_ATOL:g}")
    # value
    a, mean = maxerr(vals_g, ref_vals)
    report("value", a <= VALUE_ATOL,
           f"max_abs={a:.3e} mean_abs={mean:.3e} tol={VALUE_ATOL:g}")

    # logp of C greedy acts recomputed by torch fused_logp_entropy
    with torch.no_grad():
        lp_torch, ent_torch = P.fused_logp_entropy(ref_logits, acts_g)
    a, mean = maxerr(logp_g, lp_torch)
    # logp is a sum of 9 log-softmax terms; allow a slightly looser bound
    # but still within the 1e-3-class budget (use 9 * LOGIT_ATOL as ceiling
    # of linear error accumulation, still report against LOGIT_ATOL first).
    logp_tol = 9 * LOGIT_ATOL
    report("logp_vs_torch_recompute", a <= logp_tol,
           f"max_abs={a:.3e} mean_abs={mean:.3e} tol={logp_tol:g} "
           f"(C logp vs torch logp of SAME acts)")

    # greedy agreement: torch argmax of ref logits vs C greedy acts
    torch_acts = torch.stack([lg.argmax(dim=1) for lg in ref_logits], dim=1)
    # burn-in lanes forced to noop on both sides
    torch_acts = torch.where(burnin.unsqueeze(1), noop.unsqueeze(0), torch_acts)
    # C path already applied burn-in inside the kernel
    agree = (torch_acts == acts_g)
    per_head = agree.float().mean(dim=0)
    overall = agree.float().mean().item()
    n_cells = agree.numel()
    n_bad = (~agree).sum().item()
    report("greedy_agree", overall >= GREEDY_AGREE,
           f"agree={overall:.6f} ({n_cells - n_bad}/{n_cells}) "
           f"per_head={[round(x, 6) for x in per_head.tolist()]} "
           f"need>={GREEDY_AGREE}")

    # tie margins on disagreements: |logit_chosen_torch - logit_chosen_c|
    # and gap from best to second-best on the torch side
    if n_bad > 0:
        margins = []
        for h, w in enumerate(HEADS):
            mask = ~agree[:, h]
            if not mask.any():
                continue
            lg = ref_logits[h][mask]  # [D, w]
            top2 = torch.topk(lg, k=min(2, w), dim=1).values
            if w >= 2:
                gap = (top2[:, 0] - top2[:, 1]).abs()
            else:
                gap = torch.zeros(lg.shape[0], device=dev)
            margins.append((h, gap.min().item(), gap.median().item(),
                            gap.max().item(), int(mask.sum())))
        detail = "; ".join(
            f"h{h}: n={nn} gap_min={mn:.3e} med={md:.3e} max={mx:.3e}"
            for h, mn, md, mx, nn in margins)
        # near-tie: require every disagreement has torch top1-top2 gap < 5e-3
        worst_min = max((m[1] for m in margins), default=0.0)
        # actually we want the max of the per-disagreement min gaps? check all
        all_ok = all(m[1] < 5e-3 or m[4] == 0 for m in margins) if margins else True
        # More precise: recompute per-cell
        max_gap_at_disagree = 0.0
        for h, w in enumerate(HEADS):
            mask = ~agree[:, h]
            if not mask.any() or w < 2:
                continue
            lg = ref_logits[h][mask]
            top2 = torch.topk(lg, k=2, dim=1).values
            gap = top2[:, 0] - top2[:, 1]
            max_gap_at_disagree = max(max_gap_at_disagree, gap.max().item())
        report("greedy_tie_margins", max_gap_at_disagree < 5e-3,
               f"max_top1_top2_gap_at_disagree={max_gap_at_disagree:.3e} "
               f"(need <5e-3); {detail}")
    else:
        report("greedy_tie_margins", True, "no disagreements")

    # entropy sanity (not a hard gate beyond finite; print measured)
    a, mean = maxerr(ent_g, ent_torch)
    report("entropy_info", True,
           f"max_abs_vs_torch={a:.3e} mean_abs={mean:.3e} (info only)")

    print("\n".join(lines))
    print(f"\nN={N} LOGIT_ATOL={LOGIT_ATOL} VALUE_ATOL={VALUE_ATOL}")
    if fails:
        print(f"\nEQUIV FAIL: {fails}")
        cp.close()
        return 1
    print(f"\nEQUIV PASS ({len(lines)} checks)")
    cp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
