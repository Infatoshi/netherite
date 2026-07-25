# Tape replay: scenario_smoke_zombie_20260722T081735Z

803 ticks, seed 0, world_time 18000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 mismatches=1 available=True pass=False
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
| 0 | 1.26 | 68.91% | 0.84 |
| 20 | 0.64 | 55.33% | 0.72 |
| 40 | 0.58 | 57.94% | 0.60 |
| 60 | 0.45 | 52.96% | 0.38 |
| 80 | 0.40 | 51.58% | 0.33 |
| 100 | 0.39 | 51.57% | 0.33 |
| 120 | 0.39 | 50.70% | 0.33 |
| 140 | 0.39 | 51.50% | 0.33 |
| 160 | 0.75 | 58.37% | 0.72 |
| 180 | 0.39 | 51.92% | 0.32 |
| 200 | 0.39 | 52.05% | 0.33 |
| 220 | 0.39 | 52.30% | 0.33 |
| 240 | 0.41 | 52.49% | 0.34 |
| 260 | 0.43 | 52.43% | 0.34 |
| 280 | 0.54 | 53.28% | 0.35 |
| 300 | 0.53 | 53.35% | 0.34 |
| 320 | 0.49 | 51.98% | 0.37 |
| 340 | 0.60 | 55.27% | 0.49 |
