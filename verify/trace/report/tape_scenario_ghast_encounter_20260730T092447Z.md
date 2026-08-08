# Tape replay: scenario_ghast_encounter_20260730T092447Z

620 ticks, seed 0, world_time 6000, start (0.50,101.00,-0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=31 independent=30 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=31 ghost_ticks=620 mismatches=0 verified=True available=True pass=True
- world hash: mode=c_only compared=0 anchor_skips=0 mismatches=0 deltas=10 verified=False available=True pass=True

**Pixel gate: PASS** over 31 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 8 | 4345 | 870 |
| hud | 3 | 10069 | 4774 |
| particles | 2 | 27845 | 17923 |
| viewmodel | 3 | 4346 | 3101 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 0.81 | 69.22% | 0.93 |
| 20 | 0.39 | 69.29% | 0.50 |
| 40 | 0.34 | 69.66% | 0.43 |
| 60 | 0.35 | 69.67% | 0.45 |
| 80 | 0.38 | 69.61% | 0.49 |
| 100 | 0.39 | 69.33% | 0.51 |
| 120 | 0.49 | 68.28% | 0.69 |
| 140 | 0.51 | 68.83% | 0.66 |
| 160 | 0.37 | 68.33% | 0.49 |
| 180 | 0.40 | 66.31% | 0.53 |
| 200 | 0.38 | 65.52% | 0.50 |
| 220 | 0.35 | 66.06% | 0.44 |
| 240 | 0.41 | 66.06% | 0.55 |
| 260 | 0.47 | 69.33% | 0.65 |
| 280 | 0.47 | 66.84% | 0.65 |
| 300 | 0.55 | 66.80% | 0.79 |
| 320 | 0.67 | 66.47% | 0.93 |
| 340 | 1.12 | 65.74% | 1.77 |
| 360 | 0.80 | 65.31% | 1.22 |
| 380 | 4.64 | 69.17% | 6.65 |
| 400 | 0.39 | 68.84% | 0.52 |
| 420 | 0.39 | 68.76% | 0.52 |
| 440 | 0.37 | 65.57% | 0.48 |
| 460 | 0.34 | 65.78% | 0.44 |
| 480 | 0.44 | 65.78% | 0.60 |
| 500 | 0.44 | 64.36% | 0.57 |
| 520 | 0.49 | 65.76% | 0.69 |
| 540 | 0.63 | 65.76% | 0.93 |
| 560 | 6.40 | 72.61% | 9.15 |
| 580 | 0.61 | 65.48% | 0.89 |
| 600 | 0.61 | 65.45% | 0.88 |
