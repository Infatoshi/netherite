# Tape replay: scenario_smoke_zombie_20260722T081735Z

803 ticks, seed 0, world_time 18000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=2 independent=1 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=18 ghost_ticks=358 mismatches=0 verified=True available=True pass=True
- world hash: mode=c_only compared=0 anchor_skips=0 mismatches=0 deltas=0 verified=False available=True pass=True

**Pixel gate: PASS** over 18 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 1 | 208 | 208 |
| hud | 2 | 226 | 78 |
| transit | 2 | 129 | 65 |
| viewmodel | 3 | 494 | 239 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.15 | 61.51% | 0.74 |
| 20 | 0.52 | 48.90% | 0.65 |
| 40 | 0.46 | 51.93% | 0.54 |
| 60 | 0.33 | 46.42% | 0.31 |
| 80 | 0.27 | 45.17% | 0.26 |
| 100 | 0.27 | 45.16% | 0.27 |
| 120 | 0.26 | 44.29% | 0.26 |
| 140 | 0.27 | 45.10% | 0.26 |
| 160 | 0.61 | 50.38% | 0.64 |
| 180 | 0.26 | 45.52% | 0.25 |
| 200 | 0.26 | 45.65% | 0.26 |
| 220 | 0.26 | 45.89% | 0.26 |
| 240 | 0.28 | 46.10% | 0.27 |
| 260 | 0.30 | 46.03% | 0.27 |
| 280 | 0.42 | 46.87% | 0.28 |
| 300 | 0.41 | 46.94% | 0.27 |
| 320 | 0.36 | 45.58% | 0.30 |
| 340 | 0.47 | 48.88% | 0.42 |
