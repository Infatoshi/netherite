# Tape replay: scenario_nether_elytra_20260729T102459Z

338 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05077979201795 |d|=0.0798; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 9.8654 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=127 independent=126 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=7 available=True
- world nearby_hash: checked=7 deltas=6 available=True

**Pixel gate: FAIL** over 127 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 82 | 14241593 | 181940 |
| bossbar | 43 | 386923 | 38430 |
| hud | 49 | 1038516 | 24817 |
| particles | 8 | 97992 | 32897 |
| thinline | 1 | 3238 | 3238 |
| transit | 5 | 111178 | 13771 |
| viewmodel | 29 | 211098 | 37730 |

Failed frames (worst first, top 20):

- t=114: 275252 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=111: 274822 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=108: 274458 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=115: 273744 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=105: 273660 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=113: 273389 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51066, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=112: 273223 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50977, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=102: 273014 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=110: 272999 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50704, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=109: 272875 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50656, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=99: 272818 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=107: 272706 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50578, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=106: 272546 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50499, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=104: 272028 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50203, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=92: 271979 unexplained px, clusters [{'px': 38429, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=95: 271882 unexplained px, clusters [{'px': 38429, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=103: 271800 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50124, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 271786 unexplained px, clusters [{'px': 38426, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=101: 271545 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49998, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=100: 271356 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49901, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

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
| 20 | 1.21 | 89.88% | 1.29 |
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
| 35 | 1.16 | 93.05% | 1.30 |
| 36 | 1.17 | 93.10% | 1.31 |
| 37 | 1.18 | 93.17% | 1.32 |
| 38 | 1.18 | 93.22% | 1.32 |
| 39 | 4.23 | 93.61% | 4.19 |
| 40 | 4.33 | 93.62% | 4.33 |
| 41 | 4.36 | 93.63% | 4.42 |
| 42 | 5.38 | 93.62% | 5.58 |
| 43 | 6.38 | 96.45% | 6.80 |
| 44 | 7.14 | 96.43% | 7.64 |
| 45 | 8.29 | 96.36% | 8.98 |
| 46 | 9.27 | 96.36% | 10.05 |
| 47 | 9.96 | 96.32% | 10.76 |
| 48 | 10.77 | 96.26% | 11.67 |
| 49 | 11.47 | 96.22% | 12.48 |
| 50 | 12.22 | 96.33% | 13.42 |
| 51 | 12.95 | 96.28% | 14.36 |
| 52 | 13.49 | 96.24% | 15.06 |
| 53 | 14.44 | 96.24% | 16.10 |
| 54 | 14.91 | 96.02% | 16.67 |
| 55 | 15.43 | 96.07% | 17.39 |
| 56 | 16.38 | 95.99% | 18.41 |
| 57 | 17.08 | 96.10% | 19.46 |
| 58 | 17.59 | 96.20% | 20.04 |
| 59 | 18.54 | 69.79% | 21.16 |
| 60 | 19.67 | 96.34% | 22.61 |
| 61 | 21.90 | 94.15% | 22.80 |
| 62 | 20.85 | 96.43% | 23.88 |
| 63 | 18.93 | 96.51% | 21.38 |
| 64 | 18.98 | 96.52% | 22.05 |
| 65 | 18.87 | 96.52% | 22.14 |
| 66 | 19.36 | 96.51% | 22.54 |
| 67 | 20.83 | 96.52% | 23.55 |
| 68 | 21.39 | 96.45% | 24.57 |
| 69 | 21.63 | 64.10% | 25.07 |
| 70 | 21.96 | 63.99% | 25.52 |
| 71 | 22.17 | 96.40% | 26.09 |
| 72 | 22.62 | 96.39% | 26.74 |
| 73 | 23.26 | 96.44% | 27.42 |
| 74 | 22.81 | 96.41% | 27.22 |
| 75 | 23.26 | 96.41% | 28.07 |
| 76 | 16.46 | 94.64% | 20.63 |
| 77 | 17.39 | 94.63% | 21.59 |
| 78 | 30.65 | 96.08% | 32.37 |
| 79 | 30.05 | 96.01% | 30.45 |
| 80 | 29.41 | 96.20% | 30.24 |
| 81 | 28.35 | 95.92% | 28.91 |
| 82 | 27.38 | 95.74% | 28.19 |
| 83 | 26.63 | 96.09% | 27.38 |
| 84 | 25.53 | 94.30% | 28.56 |
| 85 | 29.86 | 94.21% | 31.65 |
| 86 | 30.57 | 96.23% | 32.47 |
| 87 | 30.29 | 96.06% | 32.28 |
| 88 | 30.13 | 96.00% | 32.13 |
| 89 | 30.71 | 96.32% | 32.74 |
| 90 | 30.15 | 96.02% | 32.15 |
| 91 | 30.32 | 95.99% | 32.37 |
| 92 | 30.97 | 96.28% | 33.17 |
| 93 | 30.52 | 96.01% | 32.71 |
| 94 | 30.58 | 96.05% | 32.83 |
| 95 | 31.35 | 94.45% | 33.75 |
| 96 | 31.01 | 94.16% | 33.42 |
| 97 | 31.52 | 95.12% | 34.06 |
| 98 | 31.58 | 95.05% | 34.15 |
| 99 | 32.22 | 95.20% | 34.82 |
| 100 | 31.44 | 94.63% | 34.00 |
| 101 | 31.57 | 94.43% | 34.21 |
| 102 | 32.29 | 94.59% | 35.11 |
| 103 | 31.71 | 94.17% | 34.51 |
| 104 | 31.72 | 94.16% | 34.67 |
| 105 | 32.70 | 94.66% | 35.82 |
| 106 | 32.66 | 95.09% | 35.84 |
| 107 | 32.77 | 94.97% | 35.94 |
| 108 | 33.41 | 95.10% | 36.72 |
| 109 | 32.50 | 94.54% | 35.87 |
| 110 | 32.56 | 94.43% | 35.97 |
| 111 | 33.53 | 94.74% | 37.06 |
| 112 | 32.92 | 94.23% | 36.40 |
| 113 | 32.81 | 94.21% | 36.43 |
| 114 | 34.04 | 94.69% | 37.63 |
| 115 | 33.75 | 94.30% | 37.84 |
| 116 | 4.62 | 94.34% | 5.71 |
| 117 | 5.52 | 89.99% | 6.97 |
| 118 | 3.52 | 80.43% | 4.19 |
| 119 | 8.69 | 83.37% | 7.91 |
| 120 | 8.23 | 84.58% | 6.84 |
| 121 | 8.18 | 86.88% | 6.76 |
| 122 | 6.75 | 92.93% | 5.76 |
| 123 | 6.31 | 93.07% | 5.57 |
| 124 | 6.74 | 93.39% | 6.43 |
| 125 | 5.78 | 93.04% | 5.31 |
| 126 | 8.70 | 91.38% | 6.13 |
