# Tape replay: scenario_glass_pane_connections_20260730T110115Z

310 ticks, seed 0, world_time 6000, start (-7.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=5 available=True

**Pixel gate: PASS** over 13 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 4 | 548 | 174 |
| hud | 2 | 18055 | 7692 |
| particles | 13 | 25323 | 2139 |
| viewmodel | 13 | 9875 | 1789 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 60 | 1.07 | 78.08% | 1.19 |
| 80 | 1.13 | 78.12% | 1.17 |
| 100 | 1.24 | 78.12% | 1.22 |
| 120 | 1.37 | 78.09% | 1.45 |
| 140 | 1.40 | 78.12% | 1.73 |
| 160 | 1.39 | 78.03% | 1.79 |
| 180 | 1.39 | 78.13% | 1.79 |
| 200 | 1.38 | 78.05% | 1.79 |
| 220 | 1.34 | 77.99% | 1.75 |
| 240 | 1.34 | 77.99% | 1.75 |
| 260 | 1.94 | 77.95% | 2.57 |
| 280 | 3.62 | 78.53% | 3.17 |
| 300 | 2.82 | 77.98% | 1.87 |
