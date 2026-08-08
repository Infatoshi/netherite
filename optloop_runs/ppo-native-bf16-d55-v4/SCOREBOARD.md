# Native BF16 PPO optloop scoreboard (d55-v4)

Pin: `d55c7f01c1139299be3f7fa0b98ef11b82c3b473`  
Worktree: `/home/infatoshi/dev/nw/ppo-native-bf16`  
GPU: GPU0 RTX PRO 6000 Blackwell exclusive via overnight-compute  
Metric M: **median** `chunk_wall_ms` (N=6144,T=32,REPEAT=4,EPOCHS=2,MB=8192)  
Correctness C: `bash blaze/rl/native/check_correctness.sh` (native BF16 oracle)  
ε=0.02, MDD≈34.45 ms abs

## Baselines

| Recipe | median chunk_ms | env ticks/s | notes |
|---|---:|---:|---|
| Python FP32 (contract) | 1724.16 | ~456k | 5-rep median from v3 |
| Native BF16 NCHW baseline | 1513.78 | ~520k | pre-hyp finalist baseline |
| **Accepted finalist (hyp3)** | **1349.89** | **582.6k** | +10.8% vs native, +21.7% vs Python |

## Hypotheses

| hyp | change | median_ms | Δ vs best | keep | reason |
|---|---|---:|---:|---|---|
| hyp1 | fused categorical + branchless reward + obs buffer | (see hyp1/) | hygiene | keep hygiene | <ε alone, retained for correctness |
| hyp2 | always-on burnin | slower | neg | no | regressed |
| hyp3 | uint8→BF16 direct obs | **1349.89** | +10.8% native | **yes** | clears ε and MDD |
| hyp4 | bf16_scope=conv_only | 1383.06 | -33.2 ms | no | below MDD / slower |
| hyp5 | preconvert selected obs once/PPO phase | 1399.23 | -49.3 ms | no | OOM risk + slower than per-mb convert |

## Negatives (do not re-try without new evidence)

- channels-last layout
- always-on burnin
- bf16_scope=conv_only
- full / selected-obs preconvert across epochs

## Remaining bottleneck (nsys hyp3)

Top GPU: `nchwToNhwcKernel` ~24.6% (cuDNN still wants NHWC internally for BF16 convs).  
Next: `k_tick_warp` ~16.6%, BF16 fprop ~12.2%.  
NVTX: ppo ~20%, rollout ~14% of chunk; gae negligible.

## Full train (1.92B ticks) — COMPLETE

| field | value |
|---|---|
| ticks / chunks | 1920466944 / 2442 |
| wall | 3745.8s (62.43 min) |
| observed env ticks/s | 512699 |
| steady chunk median ms (curve Δ) | 1560.0 |
| reward first→last | -0.008860 → 0.068173 |
| episodes / cells | 339150 / 53 |
| all finite | True |
| peak alloc GiB | 10.2901 |
| convert roundtrip | PASS (`ecd7aa73709f…`) |
| sim multi-head diverse | True |
| sim value range | [-2.32e+07, -1.6e+05] |
| values_within_sane_range | False (known BF16 debt) |
| checkpoint | `final/native_1p92b.{nckpt,pt}` |

Projected wall at bench median was ~54.9 min; observed **62.4 min** (telemetry + curriculum overhead; steady Δ-median ~1560 ms vs bench 1349.89).

## Speedup summary

- vs Python FP32 contract median: **1.277×** (1724.16 / 1349.89)
- vs native BF16 baseline: **1.121×** (1513.78 / 1349.89)
- Observed full-train throughput: **512.7k** ticks/s over 1.92B

## Speedup summary

- vs Python FP32 contract median: **1.277×** (1724.16 / 1349.89)
- vs native BF16 baseline: **1.121×** (1513.78 / 1349.89)
- Projected 1.92B wall: ~54.9 min @ 582.6k ticks/s

## Java pilot (3 seeds x 5 tries x 6000 ticks)

| seed | successes | max reached | torches |
|---:|---:|---:|---:|
| 2 | 0/5 | 0 | 0 |
| 3 | 0/5 | 0 | 0 |
| 10 | 0/5 | 0 | 0 |

- Policy is active (non-noop mean 4465 steps/attempt).
- No torch craft / stage progress in Java on this pilot.
- Tool merge rejected `tracked_clean=false` in fragments (paper-t3 dirty flag during MC run); fragments merged offline into `final/java_pilot/java.json`.
- Sim value explosion bound: [-2.32e+07, -1.6e+05].
