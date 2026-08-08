# Tape replay: scenario_nether_elytra_20260729T102757Z

345 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 59, field `x`** oracle=-78.14935559006061 magma=-78.14938800939942 |d|=3.24e-05; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0011 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=60 independent=59 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=3 available=True
- world nearby_hash: checked=3 deltas=2 available=True

**Pixel gate: FAIL** over 60 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 7 | 1324641 | 211387 |
| bossbar | 5 | 192150 | 38430 |
| hud | 5 | 8859 | 1597 |
| viewmodel | 19 | 8062 | 3120 |

Failed frames (worst first, top 20):

- t=59: 371490 unexplained px, clusters [{'px': 81984, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 78119, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 211387, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=55: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=56: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=57: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=58: 233480 unexplained px, clusters [{'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=54: 19124 unexplained px, clusters [{'px': 19124, 'cls': 'UNEXPLAINED', 'bbox': [115, 329, 231, 524]}]
- t=52: 0 unexplained px, clusters []
- t=53: 0 unexplained px, clusters []

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.48 | 87.03% | 1.50 |
| 1 | 1.29 | 86.30% | 1.44 |
| 2 | 1.10 | 84.30% | 1.18 |
| 3 | 1.10 | 84.30% | 1.18 |
| 4 | 1.10 | 84.30% | 1.18 |
| 5 | 1.10 | 84.30% | 1.18 |
| 6 | 1.10 | 84.30% | 1.18 |
| 7 | 1.10 | 84.30% | 1.18 |
| 8 | 1.10 | 84.30% | 1.18 |
| 9 | 1.10 | 84.30% | 1.18 |
| 10 | 1.11 | 84.97% | 1.19 |
| 11 | 1.12 | 85.71% | 1.20 |
| 12 | 1.13 | 86.97% | 1.20 |
| 13 | 1.14 | 89.39% | 1.20 |
| 14 | 1.15 | 90.48% | 1.19 |
| 15 | 1.14 | 90.43% | 1.20 |
| 16 | 1.15 | 90.32% | 1.21 |
| 17 | 1.13 | 89.90% | 1.18 |
| 18 | 1.13 | 89.53% | 1.18 |
| 19 | 1.14 | 89.26% | 1.17 |
| 20 | 1.13 | 89.02% | 1.17 |
| 21 | 1.13 | 88.92% | 1.17 |
| 22 | 1.12 | 88.81% | 1.16 |
| 23 | 1.09 | 88.86% | 1.16 |
| 24 | 1.09 | 88.93% | 1.17 |
| 25 | 1.09 | 89.12% | 1.18 |
| 26 | 1.08 | 89.35% | 1.18 |
| 27 | 1.08 | 89.72% | 1.19 |
| 28 | 1.08 | 90.09% | 1.20 |
| 29 | 1.08 | 90.56% | 1.21 |
| 30 | 1.09 | 91.05% | 1.24 |
| 31 | 1.11 | 91.43% | 1.26 |
| 32 | 1.11 | 92.05% | 1.27 |
| 33 | 1.13 | 92.38% | 1.28 |
| 34 | 1.14 | 92.84% | 1.29 |
| 35 | 1.18 | 93.10% | 1.30 |
| 36 | 1.17 | 93.14% | 1.31 |
| 37 | 1.18 | 93.22% | 1.32 |
| 38 | 1.19 | 93.25% | 1.32 |
| 39 | 1.19 | 93.23% | 1.33 |
| 40 | 1.21 | 93.21% | 1.35 |
| 41 | 1.21 | 93.20% | 1.32 |
| 42 | 1.22 | 93.19% | 1.34 |
| 43 | 1.78 | 96.00% | 1.93 |
| 44 | 1.95 | 95.92% | 2.04 |
| 45 | 2.04 | 95.84% | 2.10 |
| 46 | 1.69 | 92.90% | 1.82 |
| 47 | 1.64 | 95.19% | 1.77 |
| 48 | 1.66 | 94.41% | 1.78 |
| 49 | 1.61 | 93.69% | 1.73 |
| 50 | 1.39 | 95.50% | 1.49 |
| 51 | 1.86 | 83.19% | 2.00 |
| 52 | 4.26 | 92.85% | 4.64 |
| 53 | 5.90 | 93.13% | 6.36 |
| 54 | 7.01 | 92.72% | 7.55 |
| 55 | 7.71 | 92.99% | 8.28 |
| 56 | 8.21 | 93.21% | 8.81 |
| 57 | 8.61 | 93.21% | 9.24 |
| 58 | 9.11 | 93.46% | 9.78 |
| 59 | 29.77 | 100.00% | 29.34 |
