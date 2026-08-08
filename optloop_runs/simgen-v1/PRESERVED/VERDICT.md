# VERDICT: simgen-v1 native_1p92b sim vs Java pilot

## Question

Does the 1.92B-tick native BF16 checkpoint that went 0/15 in the real Java client also fail in the BLAZE sim on the same seeds?

- Sim FAIL + Java FAIL => generalization / checkpoint quality
- Sim SUCCESS + Java FAIL => sim-to-Java transfer gap

## Setup

- checkpoint: `blaze/rl/out/native_1p92b.pt`
- sha256: `ecd7aa73709fa9485364fda768559a7cbc45e43352ed703f5c2ead8a373266f0`
- strict load: `ok` (renames: {})
- environment: blaze CUDA (`/home/infatoshi/dev/nw/simgen/blaze/env/blaze_cuda.so`)
- ep_ticks=6000 tries=5 sampling=categorical rng=torch.manual_seed(seed*100+attempt)
- pilot seeds evaluated: [2, 3, 10]
- control (TRAIN_SEEDS) seeds evaluated: [2, 3]

## Per-seed 2x2 (sim x java)

| seed | role | sim reached (best) | sim torches>0 | sim success | java reached | java success | cell |
|------|------|--------------------|---------------|-------------| --------------|--------------|------|
| 2 | pilot+control | 0 | 0/5 | 0/5 | 0 | 0/5 | SIM_FAIL / JAVA_FAIL => generalization / quality |
| 3 | pilot+control | 0 | 0/5 | 0/5 | 0 | 0/5 | SIM_FAIL / JAVA_FAIL => generalization / quality |
| 10 | pilot | 0 | 0/5 | 0/5 | 0 | 0/5 | SIM_FAIL / JAVA_FAIL => generalization / quality |

## Aggregate

- sim successes (torch): 0/15
- sim attempts with torches>0: 0
- sim attempts with reached>0: 0
- sim non_noop_steps mean/min/max: 6000.0 / 6000 / 6000
- java pilot: successes=0, torches_gt0=0, reached_gt0=0, non_noop mean=4464.533333333334

## Control-seed comparison

TRAIN_SEEDS default = [2, 3, 10, 14, 16, 20, 27, 29, 32, 44, 46]. First two with snapshots used as controls: [2, 3].
Note: pilot seeds 2/3/10 are themselves members of TRAIN_SEEDS, so a sim failure on them is not pure OOD-seed generalization; it is in-distribution checkpoint quality / incomplete skill.
- control seed 2: best_reached=0, torches_gt0=0/5, successes=0/5, non_noop_mean=6000.0
- control seed 3: best_reached=0, torches_gt0=0/5, successes=0/5, non_noop_mean=6000.0

## Verdict

**GENERALIZATION / CHECKPOINT QUALITY**: checkpoint fails in BLAZE sim on the same pilot seeds (0 torches, 0 waypoints) just as it failed in Java. The discriminating failure is not sim-to-real transfer; the policy does not complete the chain in the training environment either.

overall_label: `generalization_quality`

