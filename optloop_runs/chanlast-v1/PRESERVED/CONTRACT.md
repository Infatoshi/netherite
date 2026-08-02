# optloop CONTRACT - chanlast-v1

Lane: channels-last A/B for the eager fp32 PPO trainer
(`blaze/env/ppo_chain_cu.py`). Worktree `/home/infatoshi/dev/nw/chanlast`
on `wt/chanlast` from master `a8378a1`.

**Hypothesis.** flywheelopt-v1 nsys showed ~158 ms/chunk (~10%) spent in
cuDNN `nchwToNhwc` / `nhwcToNchw` layout transposes. Putting the policy net
and conv-path activations in `torch.channels_last` should eliminate that
tax on the eager fp32 path. The native C++ BF16 trainer rejected
channels-last; that verdict may not transfer here.

**M is `chunk_wall_ms` (minimize)**: median over 5 independent processes of
one complete post-warm-up training chunk (rollout + GAE + PPO update),
after 2 warm-up chunks, between two `torch.cuda.synchronize` calls.
Pinned config (unchanged):

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192
    MAX_TICKS=1e12 MAX_WALL=3600 PYTHONHASHSEED=0 RNG_SEED=0
    CUDA_VISIBLE_DEVICES=0, GPU0 exclusive
    FUSED_SAMPLE=1 (current accepted default)

Reciprocal: `786432 / wall_s` env-ticks/s.

**C is** `CHANNELS_LAST=1 bash blaze/rl/flywheel/check_correctness.sh`
against the existing `tolerances.tsv` (never loosened). 38 checks.

**eps = 0.02, noise_k = 2.0, baseline_reps = 5.** Keep only if C passes
AND relative gain >= eps AND absolute gain >= MDD = noise_k * baseline stderr.

## Implementation

`CHANNELS_LAST` env flag, default **0**:

- policy net: `net.to(memory_format=torch.channels_last)` (also in
  `ChainPolicy.__init__` so check_correctness picks it up)
- conv activations: `obs_float` allocates / returns channels_last float
  buffers; graph static `f_obs` buffers pre-allocated channels_last
- `CHANNELS_LAST=0` is the previous NCHW path (no format conversion)

`bench.py --lane` / `$OPTLOOP_LANE` routes receipts; default remains
`flywheelopt-v1`.

## Correctness

```
CHANNELS_LAST=1 bash blaze/rl/flywheel/check_correctness.sh
-> CORRECTNESS PASS (38 checks)
```

Raw log: `PRESERVED/check_correctness_channels_last1.log`.
All gates inside existing tolerances (graph_fwd max_abs=0, fused_logp
1.907e-06, fused_dist 2.71 sigma, graph_grad_parity 5.4e-08, etc.).

## Benchmark (one lock hold, --reps 5)

| config | median ms | env-ticks/s | samples (ms) |
|---|---|---|---|
| baseline (CHANNELS_LAST=0) | **1586.91** | 495576 | 1583.72, 1582.63, 1633.14, 1590.16, 1586.91 |
| chanlast (CHANNELS_LAST=1) | **1869.31** | 420707 | 1869.07, 1866.71, 1869.31, 1872.88, 1871.26 |

- baseline stderr = 9.55 ms, cv = 1.34%
- MDD = 2.0 * 9.55 = **19.10 ms**
- eps_abs = 0.02 * 1586.91 = **31.74 ms** (binding threshold if gain were positive)
- gain_abs = 1586.91 - 1869.31 = **-282.41 ms** (regression)
- gain_rel = **-0.1780** (-17.8%)

keep rule: correctness=pass AND rel>=0.02 AND abs>=MDD
-> **keep=False** (rel negative; candidate is slower)

## Verdict

**REVERT / leave default 0.** A receipted null.

Channels-last on eager fp32 torch is a clear regression (~282 ms/chunk,
~18% slower), not a win. Correctness is fine; the cost is pure
throughput. The native BF16 rejection transfers to this path as well,
with a larger measured loss than the 158 ms layout tax that motivated
the trial (likely: NHWC kernels are not free for these shapes in fp32,
or the contiguous reformat tax dominates the saved cudnn transpose).

Accepted default remains: `CHANNELS_LAST=0` (flag present for re-probe).

## Files

- `blaze/env/ppo_chain_cu.py` - CHANNELS_LAST flag (default 0)
- `blaze/rl/flywheel/bench.py` - `--lane` / `$OPTLOOP_LANE`
- `optloop_runs/chanlast-v1/scoreboard.jsonl` - both configs
- `optloop_runs/chanlast-v1/PRESERVED/` - raw bench + correctness logs
