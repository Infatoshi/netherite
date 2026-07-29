# Temporal divergence: fixed 200-tick trajectory

Both games start pinned at the same pose (44.5 68.0 176.5 0.0 5.0, seed 0, frozen noon) and replay the SAME inputs: forward x60, right turn 90 deg (6x15), forward x60, jump at t=130/140/150, stand. Oracle obs recorded per tick; magma --state-out per tick. Frames diffed every 20 ticks WITHOUT pose forcing (each side renders its own simulated pose), so pixel divergence includes physics drift.

![position divergence](trajectory_divergence.png)

![pixel divergence](trajectory_pixel_divergence.png)

| tick | whole mean/ch | whole %diff | terrain mean/ch | terrain %diff |
|---|---|---|---|---|
| 0 | 15.05 | 84.07% | 14.60 | 81.28% |
| 20 | 20.58 | 89.16% | 20.66 | 86.40% |
| 40 | 20.21 | 88.11% | 21.12 | 86.06% |
| 60 | 41.05 | 91.75% | 42.23 | 89.83% |
| 80 | 34.08 | 95.63% | 29.47 | 95.54% |
| 100 | 34.06 | 95.63% | 29.45 | 95.54% |
| 120 | 34.04 | 95.63% | 29.42 | 95.54% |
| 140 | 58.76 | 95.48% | 56.96 | 94.53% |
| 160 | 34.72 | 96.77% | 30.69 | 97.55% |
| 180 | 34.11 | 95.63% | 29.48 | 95.54% |
| 199 | 34.11 | 95.63% | 29.48 | 95.54% |

## tick 0

![t0](traj_t0000_sbs.png)

## tick 100

![t100](traj_t0100_sbs.png)

## tick 199

![t199](traj_t0199_sbs.png)
