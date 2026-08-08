# optloop CONTRACT - cpolicy-v1

Lane: C/CUDA rollout-side policy forward (exploratory). Worktree
`/home/infatoshi/dev/nw/cpolicy` on `wt/cpolicy` from master `a8378a1`.

**M is `chunk_wall_ms` (minimize)**: median over 5 independent processes of
one post-warm-up training chunk (rollout + GAE + PPO update), via
`blaze/rl/flywheel/bench.py --lane cpolicy-v1`. Pinned:

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192
    CUDA_VISIBLE_DEVICES=0, GPU0 exclusive via flock

**C is** the cpolicy equivalence gate plus the flywheel correctness suite:

  * `blaze/rl/cpolicy/check_equiv.py` (logits/value <= 1e-3 abs, greedy
    >= 99.9%, logp vs torch recompute of same acts)
  * `bash blaze/rl/flywheel/check_correctness.sh` with `CPOLICY=0` (prove
    default path untouched)
  * 30-chunk smoke via `compare_smoke.py` (CPOLICY=0 vs 1)

**eps = 0.02, noise_k = 2.0, baseline_reps = 5.** Keep only if C passes AND
relative gain >= eps AND absolute gain >= MDD = noise_k * baseline stderr.

## Ceiling math (stated before measurement)

From flywheelopt-v1 phase breakdown at the accepted default (~1585 ms/chunk):

  * `rollout/policy_fwd` + `rollout/sample` ~ 191 + 1.4 = ~192 ms/chunk
  * Even a 2x faster rollout forward saves ~90 ms = ~5.7% of end-to-end
  * Keep threshold is 2%, so the prize is small but not below the floor a
    priori. A null (working kernel, slower or <2%) is an acceptable outcome.

The PPO update dominates (~60%) and is OUT of scope.

## Isolation

Timed runs hold `/home/infatoshi/dev/nw/.tmp/gpu0.lock`. Dual-arch fatbin
`blaze/rl/cpolicy/cpolicy_fwd.so` built with
`make -C magma cpolicy_so BLAZE_SM="sm_86 sm_120"`.

## RNG

`CPOLICY=1` sampling is independent of torch Philox; see
`blaze/rl/cpolicy/RNG_PROTOCOL.md`. Smoke is a trend test, not
chunk-for-chunk equality.

## Verdict (measured 2026-08-02)

| cfg | M (ms) | vs base | verdict |
|---|---|---|---|
| baseline | 1606.18 | - | reference |
| CPOLICY=1 | 1834.25 | **-14.2% / -228 ms** | **REVERT** |

Equivalence PASS, correctness (CPOLICY=0) PASS, smoke PASS. Keep rule fails
on gain. Default stays `CPOLICY=0`. Full numbers in `RECEIPT.md`.
