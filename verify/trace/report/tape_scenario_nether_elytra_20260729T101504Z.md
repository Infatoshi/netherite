# Tape replay: scenario_nether_elytra_20260729T101504Z

334 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 42, field `x`** oracle=-75.59638487526954 magma=-75.59642351025522 |d|=3.86e-05; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0013 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=59 independent=58 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=3 available=True
- world nearby_hash: checked=3 deltas=2 available=True

**Pixel gate: FAIL** over 59 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 9 | 1091668 | 211387 |
| bossbar | 4 | 153720 | 38430 |
| hud | 5 | 6822 | 1597 |
| viewmodel | 28 | 10103 | 3120 |

Failed frames (worst first, top 20):

- t=58: 371490 unexplained px, clusters [{'px': 81984, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 78119, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 211387, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=55: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=56: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=57: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=54: 19124 unexplained px, clusters [{'px': 19124, 'cls': 'UNEXPLAINED', 'bbox': [115, 329, 231, 524]}]
- t=52: 0 unexplained px, clusters []
- t=53: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.55 | 87.57% | 1.59 |
| 1 | 1.36 | 86.84% | 1.52 |
| 2 | 1.17 | 84.84% | 1.27 |
| 3 | 1.17 | 84.84% | 1.27 |
| 4 | 1.17 | 84.84% | 1.27 |
| 5 | 1.17 | 84.84% | 1.27 |
| 6 | 1.17 | 84.84% | 1.27 |
| 7 | 1.17 | 84.84% | 1.27 |
| 8 | 1.17 | 84.84% | 1.27 |
| 9 | 1.17 | 84.84% | 1.27 |
| 10 | 1.18 | 85.53% | 1.27 |
| 11 | 1.19 | 86.28% | 1.28 |
| 12 | 1.20 | 87.58% | 1.28 |
| 13 | 1.22 | 90.02% | 1.28 |
| 14 | 1.22 | 91.11% | 1.28 |
| 15 | 1.22 | 91.09% | 1.28 |
| 16 | 1.23 | 91.03% | 1.30 |
| 17 | 1.21 | 90.66% | 1.28 |
| 18 | 1.21 | 90.34% | 1.28 |
| 19 | 1.22 | 90.08% | 1.28 |
| 20 | 1.21 | 89.88% | 1.28 |
| 21 | 1.21 | 89.80% | 1.29 |
| 22 | 1.21 | 89.74% | 1.29 |
| 23 | 1.18 | 89.82% | 1.28 |
| 24 | 1.18 | 89.95% | 1.29 |
| 25 | 1.18 | 90.15% | 1.29 |
| 26 | 1.18 | 90.40% | 1.29 |
| 27 | 1.18 | 90.76% | 1.28 |
| 28 | 1.18 | 91.14% | 1.27 |
| 29 | 1.18 | 91.51% | 1.26 |
| 30 | 1.19 | 91.76% | 1.26 |
| 31 | 1.18 | 91.93% | 1.26 |
| 32 | 1.17 | 92.31% | 1.27 |
| 33 | 1.16 | 92.57% | 1.28 |
| 34 | 1.15 | 92.84% | 1.29 |
| 35 | 1.17 | 93.10% | 1.31 |
| 36 | 1.18 | 93.14% | 1.31 |
| 37 | 1.18 | 93.22% | 1.32 |
| 38 | 1.21 | 93.24% | 1.35 |
| 39 | 1.19 | 93.22% | 1.33 |
| 40 | 1.21 | 93.21% | 1.33 |
| 41 | 1.25 | 93.20% | 1.38 |
| 42 | 1.31 | 93.38% | 1.43 |
| 43 | 1.84 | 95.97% | 1.97 |
| 44 | 2.02 | 95.89% | 2.11 |
| 45 | 2.13 | 95.81% | 2.18 |
| 46 | 1.76 | 92.88% | 1.93 |
| 47 | 1.73 | 95.18% | 1.88 |
| 48 | 1.73 | 94.39% | 1.87 |
| 49 | 1.71 | 93.68% | 1.84 |
| 50 | 1.51 | 95.42% | 1.64 |
| 51 | 1.97 | 82.43% | 2.15 |
| 52 | 4.27 | 92.85% | 4.64 |
| 53 | 5.90 | 93.13% | 6.36 |
| 54 | 7.01 | 92.72% | 7.55 |
| 55 | 7.71 | 92.99% | 8.28 |
| 56 | 8.21 | 93.21% | 8.81 |
| 57 | 8.61 | 93.21% | 9.24 |
| 58 | 29.87 | 100.00% | 29.46 |
