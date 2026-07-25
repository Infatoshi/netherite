# Tape replay: scenario_smoke_zombie_20260722T081735Z

803 ticks, seed 0, world_time 18000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=2 independent=1 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=18 available=True
- world nearby_hash: checked=18 deltas=0 available=True

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
| 20 | 0.55 | 49.95% | 0.65 |
| 40 | 0.49 | 52.98% | 0.55 |
| 60 | 0.36 | 47.47% | 0.32 |
| 80 | 0.30 | 46.23% | 0.27 |
| 100 | 0.30 | 46.22% | 0.27 |
| 120 | 0.30 | 45.34% | 0.26 |
| 140 | 0.30 | 46.15% | 0.27 |
| 160 | 0.65 | 51.43% | 0.65 |
| 180 | 0.30 | 46.57% | 0.26 |
| 200 | 0.30 | 46.70% | 0.26 |
| 220 | 0.30 | 46.94% | 0.26 |
| 240 | 0.31 | 47.15% | 0.28 |
| 260 | 0.34 | 47.08% | 0.28 |
| 280 | 0.45 | 47.93% | 0.28 |
| 300 | 0.44 | 48.00% | 0.28 |
| 320 | 0.40 | 46.63% | 0.31 |
| 340 | 0.50 | 49.93% | 0.43 |
