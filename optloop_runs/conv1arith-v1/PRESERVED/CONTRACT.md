# optloop CONTRACT - conv1arith-v1

Lane: reduce conv/obs-conversion arithmetic cost in the eager fp32 PPO
trainer (`blaze/env/ppo_chain_cu.py`). Worktree
`/home/infatoshi/dev/nw/conv1arith` on `wt/conv1arith` from master
`a8378a1`. Sibling methodology lane: `flywheelopt-v1` (fused sample
accepted; CUDA graphs measured and reverted).

**M is `chunk_wall_ms` (minimize)**: median over 5 independent processes of
the synchronised wall time of ONE complete post-warm-up training chunk
(rollout + GAE + PPO update), measured between two `torch.cuda.synchronize`
calls after 2 warm-up chunks. Pinned config, identical to flywheelopt and
ppo-native-bf16:

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192
    MAX_TICKS=1e12 MAX_WALL=3600 PYTHONHASHSEED=0 RNG_SEED=0
    CUDA_VISIBLE_DEVICES=0, GPU0 exclusive

Reciprocal reported as `786432 / wall_s` env-ticks/s.

**C is** `bash blaze/rl/flywheel/check_correctness.sh` with the candidate
flag exported, at the existing tolerances in
`blaze/rl/flywheel/tolerances.tsv`. Never loosen a tolerance.

**eps = 0.02, noise_k = 2.0, baseline_reps = 5.** Keep only if C passes AND
relative gain >= eps AND absolute gain >= MDD = noise_k * baseline stderr.

Channels-last is out of scope (owned by a parallel lane).

## Isolation

Every timed run holds `/home/infatoshi/dev/nw/.tmp/gpu0.lock` exclusively
for its entire config list. `bench.py --lane conv1arith-v1` writes receipts
here. Both GPUs must be idle before measure.

## Verdicts

| candidate | baseline med (ms) | candidate med (ms) | vs base | correctness | verdict |
|---|---|---|---|---|---|
| cudnn_bench (CUDNN_BENCH=1) | 1573.07 | 1922.94 | -22.24% / -349.9 ms | PASS | **revert** (slower) |
| fold_scale (FOLD_SCALE=1) | 1594.94 | 1581.03 | +0.87% / +13.91 ms | PASS | **revert** (below eps) |
| tf32_conv (TF32_CONV=1) | 1610.16 | 1573.53 | +2.27% / +36.62 ms | FAIL sampler_grad_parity | **report-only** (numerics) |

Keepers: none. Combined keepers A/B: not run.

Accepted defaults: all three flags remain **0** (current path unchanged).

## Why each outcome

1. **CUDNN_BENCH.** Static shapes, so algo autotune should amortize over the
   2 warm-up chunks. On this Blackwell box the selected algorithms were
   strictly worse for the measured chunk (median +350 ms). Correctness
   held (deterministic within the differential gate). Flag stays off.

2. **FOLD_SCALE.** Depth-plane `/=255` folded into a `W * scale_mask` view
   of conv1 weights (bias untouched). Autograd maps grads onto the
   stored canonical weights, so Adam steps the unscaled parameters.
   Algebraically equivalent; residual is fp32 reduction order on the
   depth channels. Ceiling is tiny: only two depth planes per stacked
   frame are scaled, so the elementwise savings sits well under eps
   (+13.9 ms vs eps_abs 31.9 ms). Flag stays off.

3. **TF32_CONV.** Enables `cudnn.allow_tf32` and `matmul.allow_tf32`.
   Speed would clear the keep rule (+2.27%, +36.6 ms > eps_abs 32.2 ms)
   but `sampler_grad_parity` fails at the existing budget
   (`rel_l2=1.31e-05 > 1e-05`). Report-only; flag stays off. Never
   loosen tolerances.

## Files

- `blaze/env/ppo_chain_cu.py` - CUDNN_BENCH / FOLD_SCALE / TF32_CONV flags
- `blaze/rl/flywheel/bench.py` - `--lane` / `--lane-dir` / `LANE_DIR` only
- `optloop_runs/conv1arith-v1/scoreboard.jsonl` - every run
- `optloop_runs/conv1arith-v1/PRESERVED/` - raw logs + this contract
