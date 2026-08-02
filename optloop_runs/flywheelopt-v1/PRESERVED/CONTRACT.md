# optloop CONTRACT - flywheelopt-v1

Lane: reduce the netherite RL flywheel's policy/update cost. Worktree
`/home/infatoshi/dev/nw/flywheelopt` on `wt/flywheelopt` from master
`b5cdc18`. Sibling lane for comparison: `ppo-native-bf16-d55-v3/v4`.

**M is `chunk_wall_ms` (minimize)**: median over 5 independent processes of
the synchronised wall time of ONE complete post-warm-up training chunk
(rollout + GAE + PPO update), measured between two `torch.cuda.synchronize`
calls after 2 warm-up chunks. Pinned config, identical to the ppo-native-bf16
lane so the two scoreboards are directly comparable:

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192
    MAX_TICKS=1e12 MAX_WALL=3600 PYTHONHASHSEED=0 RNG_SEED=0
    CUDA_VISIBLE_DEVICES=0, GPU0 exclusive

Reciprocal reported as `786432 / wall_s` env-ticks/s.

**C is** `bash blaze/rl/flywheel/check_correctness.sh` (so-liveness probe +
38 differential checks against the unmodified eager fp32 path, tolerances and
measured values in `blaze/rl/flywheel/tolerances.tsv`), plus a 30-chunk
training smoke compared with `blaze/rl/flywheel/compare_smoke.py` at the same
`RNG_SEED`.

The gate judges GRADIENTS, not post-optimizer parameter values. Comparing
parameters after K Adam steps was tried first and abandoned: Adam divides by
sqrt(v), so a parameter whose gradient is ~0 turns 1e-07 of fp32 noise into
an O(lr) position change, and the divergence is chaotic. Repeated
eager-vs-eager controls on identical inputs swung over 3x run to run and the
candidate's own number swung 6x, which is a gate that decides nothing. On
gradients the same comparison is stable to three digits: graph vs eager
5.72e-08 against a 5.68e-08 eager-vs-eager control, fused vs 9-Categorical
1.898e-06, reproduced across three runs.

**eps = 0.02, noise_k = 2.0, baseline_reps = 5.** Keep only if C passes AND
relative gain >= eps AND absolute gain >= MDD = noise_k * baseline stderr.

Measured baseline: **1715.62 ms**, stderr 1.58 ms, cv 0.21%, so
MDD = 3.15 ms and the binding threshold is eps_abs = **34.31 ms**.

## Isolation

Every timed run holds `/home/infatoshi/dev/nw/.tmp/gpu0.lock` exclusively for
its entire config list, so an A/B pair cannot be split by another tenant.
`bench.py` aborts before measuring if either GPU shows >512 MiB used, >5%
utilisation, or any compute app. GPU1 is never used. `ncu` is blocked on this
box (`ERR_NVGPUCTRPERM`); all evidence here is CUDA events plus `nsys`.

`blaze/env/blaze_cuda.so` was rebuilt in this worktree as a dual-arch fatbin
(`make -C magma blaze_cuda_so BLAZE_SM="sm_86 sm_120"`) and verified live
before any measurement: `cuobjdump` shows both `sm_86.cubin` and
`sm_120.cubin`, and four forward steps move the player 2.879237 blocks in xz.
A .so built for the wrong SM makes every `blaze_step` a silent no-op with
rc=0, which is why the probe checks a pose delta and not a return code.

## Reading the scoreboard

`vs_base` is the gain against the fresh baseline; it is what the keep rule is
written against. It is also, for several rows here, misleading on its own,
because forcing a CUDA-graph capture on also forces
`torch.distributions` validation off (a graph cannot contain a host sync).
Those rows therefore inherit the whole `novalidate` gain whether or not the
graph itself does anything. `vs_ref` is the increment over the configuration
each candidate actually has to beat, and it is the column the verdicts follow.

## Verdicts

| candidate | M (ms) | vs base | vs ref | verdict |
|---|---|---|---|---|
| baseline | 1715.62 | - | - | reference |
| act_cache | 1724.96 / 1733.43 | -0.5% / -1.0% | -7.9 / -11.4 ms | revert, reproduced twice |
| novalidate | 1610.20 | +6.15% | +105.4 ms | subsumed by fused_sample |
| fused_sample | 1591.24 / 1590.39 | +7.25% | +124.4 ms | **KEEP** |
| graph_rollout | 1608.64 | +6.24% | +1.6 ms over novalidate | revert |
| graph_update | 1618.62 | +5.65% | -8.4 ms over novalidate | revert |
| graph_both | 1614.20 | +5.91% | -4.0 ms over novalidate | revert |
| fused + graph_rollout | 1589.04 | +7.38% | +1.4 ms over fused | revert |
| fused + graph_update | 1596.27 | +6.96% | -5.9 ms over fused | revert |
| **accepted default** | **1585.17** | **+7.60%** | **+137.75 ms paired** | **ACCEPTED** |

Accepted config: `FUSED_SAMPLE=1` (now the default in `ppo_chain_cu.py`).
Paired against `FUSED_SAMPLE=0` back-to-back in one lock hold:
**1722.93 -> 1585.17 ms, +8.00%, 456451 -> 496118 env-ticks/s.**

## Why it wins, and why the graphs do not

`nsys` over the measured chunk, GPU busy computed as the union of kernel
intervals:

| | kernels/chunk | GPU busy | GPU idle | busy frac |
|---|---|---|---|---|
| baseline | 50,032 | 1540.1 ms | 309.9 ms | 0.832 |
| accepted | 22,736 | 1534.4 ms | 92.6 ms | 0.943 |

GPU busy time is unchanged (-5.7 ms). The entire 130 ms gain is recovered
idle. The idle was host synchronisation: `Categorical(logits=...)` validates
its arguments with a check that ends in `.all()` read back to the host, and
the trainer builds 9 of them per forward across 81 forwards per chunk
(32 rollout + 1 GAE + 48 minibatches) = 729 pipeline drains per chunk.

That also explains every graph result. CUDA Graphs attack launch overhead and
idle, and after the fused sampler there are only ~93 ms of idle left in a
1585 ms chunk (5.8%, and that figure is measured under nsys, so the real
number is lower). Capturing the rollout returned +1.35 ms; capturing the PPO
minibatch returned -5.88 ms, i.e. the staging `index_select` into the graph's
static input buffers costs slightly more than the launches it saves. Both are
implemented, tested, and left behind the `GRAPH_ROLLOUT` / `GRAPH_UPDATE`
flags, off by default.

## Phase breakdown at the pinned config (CUDA events, full accounting)

Diagnostic runs only - the extra events perturb the chunk wall, so these
numbers are never the metric M.

| phase | baseline ms | accepted ms | accepted ms/decision |
|---|---|---|---|
| ppo/mb_fwd | 535.69 | 482.55 | 15.08 |
| ppo/mb_bwd | 537.35 | 478.63 | 14.96 |
| ppo/mb_opt | 4.28 | 4.25 | 0.13 |
| env/step | 240.82 | 245.46 | 7.67 |
| rollout/policy_fwd | 191.55 | 191.51 | 5.98 |
| reward | 42.02 | 42.24 | 1.32 |
| stack_roll | 41.77 | 41.72 | 1.30 |
| rollout/obs_build | 35.86 | 35.89 | 1.12 |
| curriculum | 34.99 | 34.84 | 1.09 |
| gae | 7.57 | 7.55 | 0.24 |
| rollout/act_decode | 4.28 | 3.83 | 0.12 |
| rollout/sample | **40.28** | **1.39** | 0.04 |
| accounted | 1716.5 | 1569.9 | |
| chunk wall (instrumented) | 1731.8 | 1584.7 | 49.52 |

The PPO update stays the majority of the chunk (61%) and its own drop
(537+536 -> 483+479) is the same effect: the update built 9 Categoricals per
minibatch too.

## Files

- `blaze/rl/flywheel/bench.py` - M, preflight, lock, scoreboard, keep rule
- `blaze/rl/flywheel/check_correctness.py` / `.sh` / `tolerances.tsv` - C
- `blaze/rl/flywheel/compare_smoke.py` - 30-chunk trend gate
- `blaze/rl/flywheel/so_sanity.py` - dual-arch + live-env probe
- `blaze/rl/flywheel/RNG_PROTOCOL.md` - what the fused sampler preserves
- `optloop_runs/flywheelopt-v1/scoreboard.jsonl` - every run, with hw stamps
- `optloop_runs/flywheelopt-v1/bench.log` - raw stdout of every rep
- `optloop_runs/flywheelopt-v1/smoke_{base,fused}.txt` - 30-chunk smokes (repo .gitignore drops *.log)

## What remains untried, and why

Ranked by measured size, from the same `nsys` capture.

1. **cuDNN NCHW<->NHWC layout transposes: 158 ms/chunk (10%).**
   `nchwToNhwcKernel` + `nhwcToNchwKernel` cost 474 ms across the three
   profiled chunks. cuDNN selects NHWC tensor-core kernels for parts of the
   conv stack and then pays to transpose in and out of them on every call.
   This is pure layout tax, it is the largest single remaining item, and it
   is bigger than the whole gain this lane banked. It was NOT measured here:
   the lane brief lists channels-last among the ppo-native-bf16 lane's
   rejected hypotheses and instructs against re-running rejections. That
   rejection was established in the native C++ BF16 trainer, so whether it
   transfers to the eager fp32 torch path is an open question with a
   quantified prize. One flag, one A/B, four minutes.

2. **`implicit_convolve_sgemm` (conv1, 5x5 s2): 281 ms/chunk fwd, plus 175
   ms/chunk `wgrad` and 51 ms/chunk `dgrad`.** This is the real arithmetic
   and the GPU is 94% busy running it, so it is not an overhead problem. It
   is an fp32 problem: prior art measured that BF16 tensor cores do not
   double these shapes.

3. **Remaining host syncs: ~93 ms/chunk of idle (5.8%, nsys-inflated).**
   The per-decision curriculum block still reads back to the host
   (`cand.any()`, `.cpu().numpy()`, `ended.any()`, `burnin.any()`) and
   measures 34.8 ms/chunk. Making the capture logic fully device-side is
   plausible but the ceiling is under 2% and this lane's keep rule is 2%.

4. **Hypothesis 4 (hand-written C/CUDA forward-only rollout policy) was not
   attempted, by its own gate.** The brief authorised it only if hypotheses
   1-3 left more than 20% of inference time on the table. They left 5.8% of
   the chunk as idle, and `rollout/policy_fwd` is 191.5 ms of near-saturated
   conv math that a hand-written fp32 kernel would have to beat cuDNN at.
   Hypothesis 5 (fusing the policy into the env step loop) depended on 4.
