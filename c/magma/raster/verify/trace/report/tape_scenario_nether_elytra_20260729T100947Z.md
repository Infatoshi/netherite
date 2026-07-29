# Tape replay: scenario_nether_elytra_20260729T100947Z

348 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 35, field `x`** oracle=-72.68682632171482 magma=-72.68686495670053 |d|=3.86e-05; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0008 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=71 independent=70 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=4 available=True
- world nearby_hash: checked=4 deltas=3 available=True

**Pixel gate: FAIL** over 71 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 7 | 1772388 | 211387 |
| bossbar | 7 | 268156 | 38430 |
| hud | 9 | 15591 | 1597 |
| particles | 1 | 122 | 122 |
| viewmodel | 23 | 5663 | 161 |

Failed frames (worst first, top 20):

- t=70: 371490 unexplained px, clusters [{'px': 81984, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 78119, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 211387, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=64: 233484 unexplained px, clusters [{'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=65: 233484 unexplained px, clusters [{'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=66: 233484 unexplained px, clusters [{'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=67: 233484 unexplained px, clusters [{'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=68: 233482 unexplained px, clusters [{'px': 51207, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=69: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=62: 0 unexplained px, clusters []
- t=63: 0 unexplained px, clusters []

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
| 17 | 1.21 | 91.00% | 1.28 |
| 18 | 1.21 | 90.66% | 1.28 |
| 19 | 1.21 | 90.34% | 1.28 |
| 20 | 1.22 | 90.07% | 1.28 |
| 21 | 1.22 | 89.95% | 1.28 |
| 22 | 1.22 | 89.78% | 1.28 |
| 23 | 1.19 | 89.82% | 1.28 |
| 24 | 1.19 | 89.89% | 1.28 |
| 25 | 1.19 | 90.06% | 1.28 |
| 26 | 1.19 | 90.29% | 1.28 |
| 27 | 1.18 | 90.50% | 1.28 |
| 28 | 1.20 | 91.26% | 1.27 |
| 29 | 1.20 | 91.33% | 1.27 |
| 30 | 1.20 | 91.37% | 1.27 |
| 31 | 1.19 | 91.31% | 1.27 |
| 32 | 1.19 | 91.34% | 1.27 |
| 33 | 1.19 | 91.32% | 1.27 |
| 34 | 1.19 | 91.38% | 1.27 |
| 35 | 1.30 | 91.36% | 1.35 |
| 36 | 1.28 | 91.47% | 1.32 |
| 37 | 1.27 | 91.62% | 1.31 |
| 38 | 1.26 | 91.76% | 1.30 |
| 39 | 1.26 | 91.86% | 1.29 |
| 40 | 1.25 | 92.02% | 1.30 |
| 41 | 1.25 | 92.37% | 1.31 |
| 42 | 1.24 | 92.60% | 1.33 |
| 43 | 1.23 | 92.84% | 1.33 |
| 44 | 1.22 | 93.01% | 1.33 |
| 45 | 1.23 | 93.08% | 1.33 |
| 46 | 1.23 | 93.15% | 1.34 |
| 47 | 1.24 | 93.21% | 1.35 |
| 48 | 1.24 | 93.23% | 1.35 |
| 49 | 1.24 | 93.23% | 1.35 |
| 50 | 1.24 | 93.20% | 1.35 |
| 51 | 1.26 | 93.21% | 1.36 |
| 52 | 1.30 | 93.46% | 1.41 |
| 53 | 1.81 | 96.01% | 1.95 |
| 54 | 1.99 | 95.92% | 2.07 |
| 55 | 2.09 | 95.89% | 2.14 |
| 56 | 1.72 | 92.94% | 1.87 |
| 57 | 1.67 | 95.24% | 1.81 |
| 58 | 1.67 | 96.04% | 1.80 |
| 59 | 1.66 | 95.34% | 1.77 |
| 60 | 1.43 | 95.28% | 1.53 |
| 61 | 1.78 | 83.14% | 1.95 |
| 62 | 4.73 | 93.73% | 5.13 |
| 63 | 6.57 | 93.85% | 7.08 |
| 64 | 8.29 | 93.82% | 8.91 |
| 65 | 8.83 | 93.22% | 9.47 |
| 66 | 9.32 | 93.30% | 10.00 |
| 67 | 10.55 | 93.87% | 11.30 |
| 68 | 10.52 | 93.61% | 11.27 |
| 69 | 11.14 | 93.59% | 11.93 |
| 70 | 29.56 | 100.00% | 29.10 |
