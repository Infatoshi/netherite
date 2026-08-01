# Tape replay: scenario_silverfish_encounter_20260801T175112Z

367 ticks, seed 0, world_time 6000, start (0.50,4.00,1.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=37 independent=36 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=19 ghost_ticks=367 mismatches=0 verified=True available=True pass=True
- world hash: mode=java compared=367 anchor_skips=0 mismatches=0 deltas=6 verified=True available=True pass=True

**Pixel gate: FAIL** over 37 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 10 | 15704 | 5799 |
| hud | 18 | 74015 | 13600 |
| particles | 14 | 14821 | 12448 |
| viewmodel | 14 | 24151 | 7086 |

Failed frames (worst first, top 20):

- t=260: 7706 unexplained px, clusters [{'px': 5799, 'cls': 'UNEXPLAINED', 'bbox': [289, 83, 372, 202]}, {'px': 1794, 'cls': 'UNEXPLAINED', 'bbox': [350, 0, 383, 141]}, {'px': 113, 'cls': 'UNEXPLAINED', 'bbox': [352, 92, 362, 106]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 2.50 | 80.15% | 2.50 |
| 10 | 1.25 | 77.73% | 1.67 |
| 20 | 1.48 | 77.88% | 2.06 |
| 30 | 1.88 | 78.24% | 2.40 |
| 40 | 1.35 | 77.14% | 1.55 |
| 50 | 1.17 | 78.82% | 1.26 |
| 60 | 1.30 | 77.51% | 1.72 |
| 70 | 1.34 | 77.34% | 1.22 |
| 80 | 0.96 | 78.65% | 1.18 |
| 90 | 1.05 | 78.73% | 1.29 |
| 100 | 1.12 | 78.59% | 1.44 |
| 110 | 1.24 | 77.61% | 1.15 |
| 120 | 0.99 | 78.26% | 1.20 |
| 130 | 0.97 | 78.20% | 1.19 |
| 140 | 1.07 | 77.57% | 1.19 |
| 150 | 1.06 | 78.28% | 1.24 |
| 160 | 1.02 | 78.43% | 1.18 |
| 170 | 1.02 | 77.51% | 1.11 |
| 180 | 1.39 | 78.58% | 1.48 |
| 190 | 1.07 | 78.14% | 0.97 |
| 200 | 1.43 | 77.55% | 1.24 |
| 210 | 0.90 | 80.05% | 0.92 |
| 220 | 1.63 | 78.18% | 2.10 |
| 230 | 1.20 | 77.98% | 1.19 |
| 240 | 1.93 | 78.69% | 2.76 |
| 250 | 1.80 | 78.66% | 1.30 |
| 260 | 2.48 | 78.40% | 2.05 |
| 270 | 1.58 | 78.44% | 1.09 |
| 280 | 0.86 | 77.68% | 1.01 |
| 290 | 0.84 | 78.08% | 1.01 |
| 300 | 0.84 | 77.91% | 0.97 |
| 310 | 0.78 | 77.92% | 0.90 |
| 320 | 0.78 | 78.04% | 0.88 |
| 330 | 0.81 | 77.76% | 0.92 |
| 340 | 0.84 | 78.38% | 0.99 |
| 350 | 0.80 | 77.89% | 0.88 |
| 360 | 0.79 | 78.51% | 0.88 |
