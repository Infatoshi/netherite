# cpolicy-v1 RECEIPT

## Decision: REVERT (default stays CPOLICY=0)

Working C/CUDA kernel, equivalence green, smoke not degraded, but end-to-end
M is **slower** by 14.2%. Keep rule fails on both rel and abs gain.

## Ceiling (pre-measurement, restated)

Rollout policy forward is ~180-192 ms of a ~1585-1606 ms chunk. A perfect
2x on that slice is ~90-96 ms = ~5.7% e2e. Keep threshold is 2%, so the
lane was not a priori below floor, but the prize is small and the
arithmetic is near-saturated cuDNN conv math that a hand-written fp32
direct kernel is unlikely to beat.

## Equivalence gate (`check_equiv.py`, N=6144)

| check | result |
|---|---|
| logits max abs | **7.036e-05** (tol 1e-3) |
| value max abs | **6.126e-05** (tol 1e-3) |
| logp vs torch recompute of same acts | **1.402e-04** (tol 9e-3) |
| greedy agreement | **0.999946** (55293/55296) |
| max top1-top2 gap at disagree | **1.803e-05** (need <5e-3) |

PASS. Sampling RNG is independent of torch Philox (see
`blaze/rl/cpolicy/RNG_PROTOCOL.md`); greedy-only is the action-equality
claim.

## Correctness (CPOLICY=0)

`bash blaze/rl/flywheel/check_correctness.sh` -> **CORRECTNESS PASS (38 checks)**.
Default path byte-for-byte untouched in behaviour.

## Smoke (30 chunks, compare_smoke.py)

| | base CPOLICY=0 | cand CPOLICY=1 |
|---|---|---|
| reward last-third mean | 0.01545 | 0.01914 (drop negative = better) |
| final entropy | 9.05 | 8.83 (>= 0.5 * base) |
| mean approx-KL | 0.0077 | 0.0087 (<= 4x base) |
| pnorm travel | 4.60 | 4.64 |

**SMOKE PASS.** Training is not degraded. Note the candidate wall is
slower (~0.436M vs 0.502M env-ticks/s over the smoke), consistent with M.

## Scoreboard (bench.py --lane cpolicy-v1 --reps 5, one lock hold)

| cfg | median chunk_wall_ms | stderr | env-ticks/s |
|---|---|---|---|
| baseline (default) | **1606.18** | 0.85 | 489,634 |
| CPOLICY=1 | **1834.25** | 0.60 | 428,752 |

- gain_rel = **-0.1420** (need >= +0.02)
- gain_abs = **-228.07 ms** (need >= MDD = 1.69 ms)
- correctness_ok = pass
- **keep = False**

## Why slower

Hand-written direct convolutions (uint8->float fused into conv1, ReLU fused)
plus cublasSgemm for FC/heads do not beat cuDNN's `implicit_convolve_sgemm`
on these shapes (conv1 5x5 s2 18->32 on 36x64 is the bulk of
`rollout/policy_fwd`). Launch fusion and avoided uint8 materialization do
not offset the arithmetic regression. Net: rollout got worse; update
unchanged; e2e +228 ms.

## Default

`CPOLICY` default remains **0**. The path is behind the flag for further
exploration (e.g. cuDNN-backed C forward, BF16, or layout work) without
affecting production runs.

## Files

- `blaze/rl/cpolicy/` (new): `cpolicy_fwd.cu/.h`, `wrapper.py`,
  `check_equiv.py`, `RNG_PROTOCOL.md`
- `blaze/env/ppo_chain_cu.py`: CPOLICY wiring
- `magma/Makefile`: `cpolicy_so` target (dual-arch via BLAZE_SM)
- `blaze/rl/flywheel/bench.py`: `--lane` parameterization only
- `optloop_runs/cpolicy-v1/` : scoreboard + PRESERVED logs

Build: `make -C magma cpolicy_so BLAZE_SM="sm_86 sm_120"`
(`.so` is gitignored under `blaze/**/*.so`).
