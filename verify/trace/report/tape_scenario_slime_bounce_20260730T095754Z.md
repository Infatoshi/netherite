# Tape replay: scenario_slime_bounce_20260730T095754Z

406 ticks, seed 0, world_time 6000, start (0.50,8.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=41 independent=40 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=6 available=True

**Pixel gate: FAIL** over 41 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| hud | 21 | 505652 | 29505 |
| particles | 27 | 369109 | 25321 |
| viewmodel | 26 | 311740 | 21338 |

Failed frames (worst first, top 20):

- t=30: 0 unexplained px, clusters []
- t=60: 0 unexplained px, clusters []
- t=70: 0 unexplained px, clusters []
- t=80: 0 unexplained px, clusters []
- t=90: 0 unexplained px, clusters []
- t=100: 0 unexplained px, clusters []
- t=110: 0 unexplained px, clusters []
- t=120: 0 unexplained px, clusters []
- t=130: 0 unexplained px, clusters []
- t=140: 0 unexplained px, clusters []
- t=150: 0 unexplained px, clusters []
- t=160: 0 unexplained px, clusters []
- t=170: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 3.98 | 94.34% | 2.63 |
| 10 | 0.82 | 75.28% | 0.89 |
| 20 | 0.84 | 86.79% | 0.91 |
| 30 | 5.52 | 77.42% | 5.90 |
| 40 | 1.18 | 74.45% | 0.76 |
| 50 | 4.53 | 77.05% | 4.62 |
| 60 | 5.67 | 77.66% | 6.02 |
| 70 | 5.60 | 77.66% | 5.94 |
| 80 | 5.60 | 77.66% | 5.94 |
| 90 | 5.60 | 77.66% | 5.94 |
| 100 | 5.60 | 77.66% | 5.94 |
| 110 | 5.60 | 77.66% | 5.94 |
| 120 | 5.60 | 77.66% | 5.94 |
| 130 | 5.60 | 77.66% | 5.94 |
| 140 | 5.41 | 79.07% | 5.93 |
| 150 | 5.39 | 79.06% | 5.84 |
| 160 | 5.33 | 79.08% | 5.67 |
| 170 | 5.23 | 79.07% | 5.35 |
| 180 | 4.94 | 79.08% | 4.89 |
| 190 | 4.40 | 79.07% | 4.15 |
| 200 | 3.72 | 79.05% | 3.24 |
| 210 | 2.84 | 79.05% | 2.01 |
| 220 | 1.68 | 79.02% | 0.95 |
| 230 | 0.80 | 77.92% | 0.76 |
| 240 | 0.79 | 78.01% | 0.88 |
| 250 | 0.81 | 78.12% | 0.91 |
| 260 | 0.81 | 77.92% | 0.91 |
| 270 | 0.89 | 78.05% | 1.01 |
| 280 | 0.75 | 77.96% | 0.80 |
| 290 | 0.77 | 78.05% | 0.82 |
| 300 | 0.74 | 76.69% | 0.77 |
| 310 | 0.73 | 76.64% | 0.75 |
| 320 | 0.73 | 76.64% | 0.75 |
| 330 | 0.73 | 76.64% | 0.75 |
| 340 | 0.73 | 76.64% | 0.75 |
| 350 | 0.73 | 76.64% | 0.75 |
| 360 | 0.73 | 76.64% | 0.75 |
| 370 | 0.73 | 76.64% | 0.75 |
| 380 | 0.73 | 76.64% | 0.75 |
| 390 | 0.73 | 76.64% | 0.75 |
| 400 | 0.73 | 76.64% | 0.75 |
