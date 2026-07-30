# Tape replay: scenario_nether_elytra_20260729T104003Z

336 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 9.5131 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=127 independent=126 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=7 available=True
- world nearby_hash: checked=7 deltas=6 available=True

**Pixel gate: FAIL** over 127 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 85 | 15303985 | 181951 |
| bossbar | 47 | 352471 | 38430 |
| hud | 53 | 1120099 | 42972 |
| particles | 8 | 92314 | 34028 |
| transit | 2 | 21370 | 4969 |
| viewmodel | 29 | 190069 | 36111 |

Failed frames (worst first, top 20):

- t=119: 275540 unexplained px, clusters [{'px': 38343, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2108, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=116: 274837 unexplained px, clusters [{'px': 38348, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=121: 274361 unexplained px, clusters [{'px': 38332, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=113: 274279 unexplained px, clusters [{'px': 38342, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=118: 274223 unexplained px, clusters [{'px': 38332, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 268, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=120: 273938 unexplained px, clusters [{'px': 38336, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=110: 273780 unexplained px, clusters [{'px': 38335, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=117: 273619 unexplained px, clusters [{'px': 38333, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 324, 421, 341], 'soak_from': 'transit'}]
- t=107: 273225 unexplained px, clusters [{'px': 38348, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=115: 272920 unexplained px, clusters [{'px': 38336, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50797, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=114: 272561 unexplained px, clusters [{'px': 38337, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50690, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=104: 272543 unexplained px, clusters [{'px': 38347, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=112: 272398 unexplained px, clusters [{'px': 38335, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50592, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=101: 272300 unexplained px, clusters [{'px': 38372, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=99: 272277 unexplained px, clusters [{'px': 38396, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=111: 272254 unexplained px, clusters [{'px': 22326, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 498], 'soak_from': 'transit'}, {'px': 15998, 'cls': 'UNEXPLAINED', 'bbox': [0, 496, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}]
- t=108: 271960 unexplained px, clusters [{'px': 38333, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50434, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=109: 271901 unexplained px, clusters [{'px': 38330, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50370, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=96: 271715 unexplained px, clusters [{'px': 38402, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=93: 271696 unexplained px, clusters [{'px': 38404, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]

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
| 10 | 1.18 | 85.53% | 1.28 |
| 11 | 1.19 | 86.28% | 1.28 |
| 12 | 1.20 | 87.58% | 1.28 |
| 13 | 1.22 | 90.02% | 1.28 |
| 14 | 1.23 | 91.11% | 1.28 |
| 15 | 1.22 | 91.09% | 1.28 |
| 16 | 1.23 | 91.03% | 1.30 |
| 17 | 1.21 | 90.66% | 1.28 |
| 18 | 1.22 | 90.34% | 1.28 |
| 19 | 1.22 | 90.08% | 1.28 |
| 20 | 1.22 | 89.88% | 1.29 |
| 21 | 1.21 | 89.80% | 1.29 |
| 22 | 1.21 | 89.75% | 1.29 |
| 23 | 1.18 | 89.83% | 1.28 |
| 24 | 1.18 | 89.95% | 1.29 |
| 25 | 1.18 | 90.02% | 1.29 |
| 26 | 1.18 | 90.28% | 1.29 |
| 27 | 1.18 | 90.63% | 1.28 |
| 28 | 1.18 | 91.06% | 1.27 |
| 29 | 1.18 | 91.46% | 1.27 |
| 30 | 1.18 | 91.76% | 1.26 |
| 31 | 1.18 | 91.89% | 1.25 |
| 32 | 1.17 | 92.24% | 1.27 |
| 33 | 1.16 | 92.53% | 1.28 |
| 34 | 1.16 | 92.80% | 1.30 |
| 35 | 1.16 | 92.99% | 1.30 |
| 36 | 1.17 | 93.09% | 1.31 |
| 37 | 1.17 | 93.15% | 1.32 |
| 38 | 1.19 | 93.22% | 1.34 |
| 39 | 4.04 | 93.61% | 3.95 |
| 40 | 4.14 | 93.63% | 4.09 |
| 41 | 4.16 | 93.63% | 4.15 |
| 42 | 5.04 | 93.63% | 5.05 |
| 43 | 5.92 | 96.44% | 6.11 |
| 44 | 6.79 | 96.44% | 7.10 |
| 45 | 7.85 | 96.36% | 8.35 |
| 46 | 8.82 | 96.36% | 9.34 |
| 47 | 9.58 | 96.31% | 10.16 |
| 48 | 10.35 | 96.24% | 11.08 |
| 49 | 11.03 | 96.17% | 11.92 |
| 50 | 11.75 | 96.31% | 12.71 |
| 51 | 12.62 | 96.25% | 13.77 |
| 52 | 12.97 | 96.21% | 14.43 |
| 53 | 13.85 | 96.21% | 15.25 |
| 54 | 14.26 | 95.98% | 15.86 |
| 55 | 14.85 | 95.99% | 16.60 |
| 56 | 15.76 | 95.99% | 17.57 |
| 57 | 16.34 | 96.06% | 18.51 |
| 58 | 17.04 | 96.19% | 19.41 |
| 59 | 18.08 | 96.21% | 20.47 |
| 60 | 19.14 | 96.29% | 21.80 |
| 61 | 21.53 | 94.11% | 22.16 |
| 62 | 20.28 | 96.41% | 23.24 |
| 63 | 18.15 | 96.49% | 20.32 |
| 64 | 18.04 | 96.50% | 20.85 |
| 65 | 17.90 | 96.51% | 20.79 |
| 66 | 18.43 | 96.50% | 21.15 |
| 67 | 19.24 | 96.51% | 21.96 |
| 68 | 20.67 | 96.47% | 23.19 |
| 69 | 20.31 | 59.67% | 22.93 |
| 70 | 20.63 | 59.40% | 23.50 |
| 71 | 20.97 | 96.39% | 24.23 |
| 72 | 21.22 | 96.38% | 24.71 |
| 73 | 21.58 | 96.40% | 25.01 |
| 74 | 21.89 | 96.40% | 25.28 |
| 75 | 21.78 | 96.43% | 25.66 |
| 76 | 22.50 | 96.42% | 26.33 |
| 77 | 14.73 | 95.01% | 18.28 |
| 78 | 14.82 | 95.01% | 18.48 |
| 79 | 24.24 | 95.80% | 25.29 |
| 80 | 31.38 | 95.86% | 31.97 |
| 81 | 30.34 | 96.11% | 30.83 |
| 82 | 29.40 | 95.74% | 29.09 |
| 83 | 27.28 | 95.54% | 27.17 |
| 84 | 23.80 | 96.00% | 25.98 |
| 85 | 27.86 | 94.27% | 29.97 |
| 86 | 29.40 | 94.16% | 30.56 |
| 87 | 30.22 | 96.27% | 31.54 |
| 88 | 29.90 | 96.04% | 31.38 |
| 89 | 29.74 | 96.06% | 31.22 |
| 90 | 30.26 | 96.28% | 31.81 |
| 91 | 29.71 | 96.03% | 31.31 |
| 92 | 29.80 | 96.04% | 31.46 |
| 93 | 30.52 | 96.29% | 32.28 |
| 94 | 29.92 | 96.01% | 31.69 |
| 95 | 30.07 | 96.02% | 31.87 |
| 96 | 30.66 | 96.27% | 32.54 |
| 97 | 30.22 | 96.00% | 32.10 |
| 98 | 30.35 | 94.36% | 32.32 |
| 99 | 31.34 | 95.41% | 33.52 |
| 100 | 30.55 | 95.01% | 32.64 |
| 101 | 31.26 | 95.23% | 33.42 |
| 102 | 30.43 | 94.73% | 32.56 |
| 103 | 30.54 | 94.59% | 32.69 |
| 104 | 31.41 | 94.79% | 33.65 |
| 105 | 30.86 | 94.38% | 33.09 |
| 106 | 30.95 | 94.34% | 33.30 |
| 107 | 31.73 | 94.73% | 34.22 |
| 108 | 31.46 | 95.01% | 34.13 |
| 109 | 31.18 | 95.04% | 33.85 |
| 110 | 32.03 | 95.17% | 34.83 |
| 111 | 31.23 | 94.50% | 33.98 |
| 112 | 31.36 | 94.44% | 34.16 |
| 113 | 32.22 | 94.77% | 35.14 |
| 114 | 31.52 | 94.30% | 34.31 |
| 115 | 31.88 | 94.23% | 34.84 |
| 116 | 32.73 | 94.62% | 35.84 |
| 117 | 32.93 | 95.05% | 35.88 |
| 118 | 32.85 | 95.06% | 35.88 |
| 119 | 33.63 | 95.22% | 36.97 |
| 120 | 32.86 | 94.68% | 36.20 |
| 121 | 32.44 | 94.68% | 35.76 |
| 122 | 5.31 | 94.41% | 6.22 |
| 123 | 5.88 | 94.01% | 6.94 |
| 124 | 2.84 | 84.48% | 3.11 |
| 125 | 10.18 | 90.37% | 10.16 |
| 126 | 11.08 | 91.38% | 8.11 |
