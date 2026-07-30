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
| hud | 13 | 113045 | 7692 |
| particles | 13 | 25323 | 2139 |
| viewmodel | 13 | 9875 | 1789 |

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 60 | 2.78 | 78.08% | 2.19 |
| 80 | 2.86 | 78.12% | 2.17 |
| 100 | 2.95 | 78.12% | 2.22 |
| 120 | 3.08 | 78.09% | 2.45 |
| 140 | 3.12 | 78.12% | 2.73 |
| 160 | 3.09 | 78.03% | 2.79 |
| 180 | 3.09 | 78.13% | 2.79 |
| 200 | 3.11 | 78.05% | 2.79 |
| 220 | 3.06 | 78.01% | 2.76 |
| 240 | 3.06 | 78.01% | 2.76 |
| 260 | 3.66 | 77.97% | 3.58 |
| 280 | 5.67 | 78.53% | 4.30 |
| 300 | 4.84 | 77.99% | 3.00 |
