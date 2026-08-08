# cgraph-v1 optimization contract

Lane: replace the ChainPolicy training chunk hot loop with direct C/C++ CUDA,
cuDNN, and cuBLAS calls, with mandatory CUDA graph capture for rollout policy
steps and PPO minibatch updates. Worktree
`/home/infatoshi/dev/nw/cgraph`, branch `wt/cgraph`, base `d2afdae`.

**M is `chunk_wall_ms` (minimize)**: median over 5 independent processes of
one complete post-warm-up training chunk, measured by
`blaze/rl/flywheel/bench.py --lane cgraph-v1` in one GPU0 lock hold for the
Python baseline and C graph candidate. Pinned configuration:

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192
    MAX_TICKS=1e12 MAX_WALL=3600 PYTHONHASHSEED=0 RNG_SEED=0
    CUDA_VISIBLE_DEVICES=0

**C is**, in order:

1. `blaze/rl/cgraph/check_equiv.py`: matched initial FP32 weights and inputs;
   logits/value/logp absolute error at most `1e-3`, greedy agreement at least
   `0.999`, then gradient-direction parity for one PPO update using the
   eager-vs-eager numerical floor methodology from
   `blaze/rl/flywheel/check_correctness.py`.
2. Unmodified `bash blaze/rl/flywheel/check_correctness.sh` passes.
3. A 30-chunk Python-versus-cgraph smoke passes the existing trend criteria,
   with the independent C graph RNG protocol documented.
4. CUDA graph capture is proven by profiler launch counts and graph launches,
   not inferred from source structure.

**Keep rule:** relative gain at least `0.02`, absolute gain at least
`MDD = 2 * baseline_stderr`, and all correctness gates green.

## Ceiling gate, stated before implementation

The preserved accepted Python baseline measured about `1585 ms/chunk` without
profiling. Its Nsight Systems capture measured `1534.4 ms` of GPU-busy time in
a profiler-inflated `1627 ms` window, leaving `92.6 ms` profiler-observed idle
as an upper bound. A perfect removal of that upper bound is only about `5.8%`.
The unprofiled wall-minus-profiled-busy comparison is about `50.8 ms`, or
`3.2%`, but mixes runs and is not itself a valid ceiling measurement.

Before building the full candidate, run one isolated, NVTX-segmented profile
of the pinned accepted Python path and compute kernel-union busy and idle time
separately for rollout, GAE, and PPO update. The graph-reachable ceiling is the
idle time inside rollout plus PPO update, because cuDNN/cuBLAS arithmetic is
retained and environment stepping remains outside the graphs. If that measured
ceiling is clearly below the `2%` keep threshold, stop and preserve an abort
receipt. Otherwise proceed with the full implementation and gates.

### Measured ceiling, 2026-08-02

The isolated Nsight Systems run measured a `1602.047723 ms` benchmark chunk;
the projected NVTX phase windows cover `1601.992562 ms`. GPU busy below is the
union of kernels and memory operations in each projected phase window:

| phase | projected window ms | GPU busy ms | idle ms | GPU operations |
|---|---:|---:|---:|---:|
| rollout | 626.349663 | 551.797022 | 74.552641 | 12,800 |
| GAE | 7.590919 | 4.017920 | 3.572999 | 4 |
| PPO update | 968.051980 | 890.137076 | 77.914904 | 11,448 |

Rollout plus update idle is `152.467545 ms`, or `9.517%` of the profiled phase
window, so the ceiling gate says **PROCEED**. This remains deliberately labeled
an upper bound: the rollout phase includes `env.step`, reward, stack building,
and curriculum work between policy launches, while the mandatory rollout graph
captures only observation conversion, policy forward, sampling, logp, and
value. The implementation must earn the gain in the final unprofiled A/B.

Raw trace: `PRESERVED/python_ceiling.nsys-rep`. Machine-readable calculation:
`PRESERVED/ceiling_profile.json`.

## Constraints

- FP32 NCHW, TF32 disabled. No channels-last and no cuDNN benchmark mode.
- cuDNN for convolution forward/backward and cuBLAS for GEMMs. Custom kernels
  only for small unsupported elementwise/reduction/update operations.
- One captured steady-state rollout graph and one captured steady-state PPO
  minibatch graph. Parameters and Adam state remain at stable device addresses.
- Dual `sm_86` and `sm_120` code generation.
- Timed work runs only through `bench.py` under
  `/home/infatoshi/dev/nw/.tmp/gpu0.lock`.

## Final disposition

All equivalence, regression, smoke, and graph-capture gates passed. The final
locked A/B measured `1579.544158 ms` for Python and `1693.880 ms` for cgraph,
a `114.335842 ms` regression (`-7.238534%` gain). The MDD was `2.988760 ms`.
The keep rule therefore rejects the candidate. Full evidence and rationale are
in `PRESERVED/RECEIPT.md` and `optloop_runs/cgraph-v1/scoreboard.jsonl`.
