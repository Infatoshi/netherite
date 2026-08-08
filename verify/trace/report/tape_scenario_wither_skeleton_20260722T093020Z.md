# Tape replay: scenario_wither_skeleton_20260722T093020Z

1202 ticks, seed 0, world_time 18000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 independent=0 seeded_only=True mismatches=0 available=True pass=True
- entities: checked=31 ghost_ticks=610 mismatches=0 verified=True available=True pass=True
- world hash: mode=c_only compared=0 anchor_skips=0 mismatches=0 deltas=8 verified=False available=True pass=True

**Pixel gate: PASS** over 31 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| hud | 5 | 4947 | 2124 |
| transit | 2 | 1368 | 184 |
| viewmodel | 2 | 2235 | 2136 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 0.88 | 42.77% | 0.56 |
| 20 | 0.25 | 34.47% | 0.25 |
| 40 | 0.25 | 34.40% | 0.26 |
| 60 | 0.27 | 36.27% | 0.29 |
| 80 | 0.27 | 42.50% | 0.27 |
| 100 | 0.28 | 45.89% | 0.28 |
| 120 | 0.27 | 44.52% | 0.27 |
| 140 | 0.28 | 44.60% | 0.28 |
| 160 | 0.39 | 45.71% | 0.32 |
| 180 | 0.26 | 44.67% | 0.25 |
| 200 | 0.26 | 45.73% | 0.26 |
| 220 | 0.25 | 41.70% | 0.24 |
| 240 | 0.22 | 35.31% | 0.21 |
| 260 | 0.30 | 37.85% | 0.34 |
| 280 | 0.27 | 36.89% | 0.30 |
| 300 | 0.30 | 44.70% | 0.33 |
| 320 | 0.54 | 49.50% | 0.54 |
| 340 | 0.35 | 43.84% | 0.41 |
| 360 | 0.41 | 46.75% | 0.42 |
| 380 | 1.00 | 49.71% | 1.38 |
| 400 | 0.27 | 44.41% | 0.26 |
| 420 | 0.69 | 44.35% | 0.68 |
| 440 | 0.26 | 44.12% | 0.25 |
| 460 | 2.08 | 62.10% | 3.24 |
| 480 | 0.37 | 47.19% | 0.35 |
| 500 | 1.04 | 47.29% | 1.09 |
| 520 | 1.08 | 49.79% | 1.20 |
| 540 | 1.17 | 55.39% | 1.30 |
| 560 | 1.51 | 59.68% | 2.03 |
| 580 | 1.23 | 50.08% | 1.26 |
| 600 | 0.94 | 66.41% | 1.14 |
