# Tape replay: scenario_smoke_zombie_20260801T152023Z

806 ticks, seed 0, world_time 18000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=20 independent=19 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=19 ghost_ticks=373 mismatches=0 verified=True available=True pass=True
- world hash: mode=java compared=373 anchor_skips=0 mismatches=0 deltas=0 verified=True available=True pass=True

**Pixel gate: PASS** over 19 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 1 | 105 | 105 |
| hud | 2 | 1126 | 692 |
| transit | 1 | 820 | 660 |
| viewmodel | 2 | 327 | 219 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.10 | 54.48% | 0.68 |
| 20 | 0.42 | 47.81% | 0.53 |
| 40 | 0.34 | 47.89% | 0.32 |
| 60 | 0.39 | 47.93% | 0.45 |
| 80 | 0.70 | 45.15% | 0.70 |
| 100 | 0.27 | 45.37% | 0.27 |
| 120 | 0.26 | 44.49% | 0.26 |
| 140 | 0.27 | 44.99% | 0.26 |
| 160 | 0.31 | 45.56% | 0.27 |
| 180 | 0.26 | 45.05% | 0.25 |
| 200 | 0.26 | 45.43% | 0.25 |
| 220 | 0.27 | 46.22% | 0.26 |
| 240 | 0.27 | 47.05% | 0.27 |
| 260 | 0.28 | 47.80% | 0.28 |
| 280 | 0.28 | 48.03% | 0.28 |
| 300 | 0.27 | 47.17% | 0.26 |
| 320 | 0.27 | 46.35% | 0.26 |
| 340 | 0.26 | 45.69% | 0.26 |
| 360 | 0.63 | 45.31% | 0.56 |
