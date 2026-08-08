# Native BF16 PPO — campaign report (d55-v4)

## Goal

Port/optimize the chain PPO trainer to a native C++/libtorch BF16 hot path,
match the formal BF16 correctness oracle, beat the Python FP32 contract median
`chunk_wall_ms`, then run one 1.92B-tick train + eval.

## Setup

- Host: anvil, GPU0 only (RTX PRO 6000 Blackwell)
- Pin: d55c7f0
- Worktree: `/home/infatoshi/dev/nw/ppo-native-bf16`
- M: median chunk_wall_ms @ N=6144,T=32,REPEAT=4,EPOCHS=2,MB=8192
- C: `blaze/rl/native/check_correctness.sh` (BF16 fixture)
- ε=0.02, MDD≈34.45 ms

## Result (optimization)

| | median ms | ticks/s |
|---|---:|---:|
| Python FP32 contract | 1724.16 | ~456k |
| Native BF16 baseline | 1513.78 | ~520k |
| **Accepted (hyp3 stack)** | **1349.89** | **582.6k** |

Speedup: **1.277× vs Python**, **1.121× vs native baseline**.

Accepted stack: fused categorical + branchless reward + reusable obs buffer +
direct uint8→BF16 obs conversion. NCHW kept (`NATIVE_CHANNELS_LAST=0`).

## Discarded

| hyp | median | why |
|---|---:|---|
| channels-last | — | convert overhead |
| always-on burnin | 1525 | regress |
| bf16_scope=conv_only | 1383 | slower than full BF16 |
| preconvert selected obs once/phase | 1399 | correctness OK, +49 ms vs best; earlier full preconvert OOMed |

## Quality

- 10-chunk smoke: finite, memory stable, steady median ~1354 ms
- 100-chunk proxy: reward −0.00886→0.00947, stages 1–2, alloc stable 8.65 GiB
- Sim value explosion: BF16-specific (FP32 smoke stays |V|<2). Bounded in
  `final/value_explosion_bound.md`. Policy still diverse; train finite.

## Residual bottleneck (nsys)

1. cuDNN `nchwToNhwc` on BF16 activations (~25% GPU time)
2. `k_tick_warp` env tick (~17%)
3. BF16 conv fprop (~12%)

Next opts (not in this campaign): NHWC-native weights end-to-end, or fuse
layout into custom conv; env tick kernel work is separate from trainer.

## Full train

- Recipe: MAX_TICKS=1.92e9, NATIVE_BF16=1, channels_last=0, RNG_SEED=0
- Projected wall ~54.9 min @ accepted median
- Artifacts: `final/native_1p92b.{nckpt,pt}`, curve, receipts
- Post-train: convert + sim_sanity + (optional) Java pilot

## Artifacts

- `scoreboard.jsonl`, `negatives.jsonl`, `SCOREBOARD.md`
- `hyp3/finalist.json`, `profile/native_bf16_h3.nsys-rep`
- `quality_proxy/`, `smoke/`, `final/`
- Source: `blaze/rl/native/`

## Full train result

- Completed: 2442 chunks, 1920466944 ticks in 62.43 min
- Reward: -0.00886 → 0.06817; episodes 339150; cells 53
- Convert: PASS strict FP32 roundtrip
- Sim sanity: multi-head diverse=True; values explode to [-2.32e+07, -1.6e+05] (bound documented)
- Java pilot: see `final/java_pilot/`

## Java pilot result

3 seeds (2,3,10), 5 tries, 6000 ep ticks. **0/15 successes**, reached=0, torches=0.
Policy non-noop throughout (mean 4465 steps). Transfer gap open;
sim training curriculum progressed (cells 53, reward 0.068) but Java pilot did not craft.

## Eval stage board

| stage | status |
|---|---|
| full train 1.92B | PASS (finite, reward −0.00886→0.06817, wall 62.4 min) |
| convert .nckpt→.pt | PASS (strict roundtrip) |
| sim_sanity | FAIL value gate only (|V|~1e7); multi-head diverse |
| java pilot | COMPLETE 0/15 success |

Accepted opt M: **1349.89 ms median** (1.277× Python, 1.121× native baseline).
