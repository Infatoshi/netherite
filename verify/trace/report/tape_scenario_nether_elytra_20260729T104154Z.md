# Tape replay: scenario_nether_elytra_20260729T104154Z

345 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 10.5059 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=127 independent=126 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=7 available=True
- world nearby_hash: checked=7 deltas=6 available=True

**Pixel gate: FAIL** over 127 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 85 | 14544220 | 181894 |
| bossbar | 47 | 353546 | 38404 |
| hud | 49 | 1120043 | 42868 |
| particles | 8 | 93313 | 34802 |
| transit | 2 | 34796 | 6555 |
| viewmodel | 32 | 290463 | 36526 |

Failed frames (worst first, top 20):

- t=117: 275679 unexplained px, clusters [{'px': 38327, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2216, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=115: 274854 unexplained px, clusters [{'px': 38324, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=112: 274349 unexplained px, clusters [{'px': 38330, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=109: 273971 unexplained px, clusters [{'px': 38327, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=106: 273347 unexplained px, clusters [{'px': 38326, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=116: 273303 unexplained px, clusters [{'px': 38315, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50998, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=114: 272939 unexplained px, clusters [{'px': 38335, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50786, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=113: 272866 unexplained px, clusters [{'px': 38325, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50884, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=103: 272792 unexplained px, clusters [{'px': 38327, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=98: 272777 unexplained px, clusters [{'px': 38308, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=100: 272633 unexplained px, clusters [{'px': 38312, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=111: 272461 unexplained px, clusters [{'px': 38332, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50616, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=110: 272325 unexplained px, clusters [{'px': 38325, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50524, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=95: 272310 unexplained px, clusters [{'px': 38322, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=92: 272286 unexplained px, clusters [{'px': 38318, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=107: 272129 unexplained px, clusters [{'px': 38322, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50555, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=108: 272107 unexplained px, clusters [{'px': 38327, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50509, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=105: 271838 unexplained px, clusters [{'px': 38330, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50339, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 271658 unexplained px, clusters [{'px': 38318, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=104: 271639 unexplained px, clusters [{'px': 38329, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50218, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

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
| 42 | 5.16 | 93.63% | 5.23 |
| 43 | 6.07 | 96.44% | 6.33 |
| 44 | 6.89 | 96.43% | 7.32 |
| 45 | 7.99 | 96.37% | 8.55 |
| 46 | 9.03 | 96.36% | 9.63 |
| 47 | 9.74 | 96.31% | 10.40 |
| 48 | 10.47 | 96.24% | 11.24 |
| 49 | 11.04 | 96.18% | 11.93 |
| 50 | 11.80 | 96.30% | 12.83 |
| 51 | 12.62 | 96.25% | 13.75 |
| 52 | 12.98 | 96.20% | 14.42 |
| 53 | 13.91 | 96.19% | 15.34 |
| 54 | 14.29 | 95.97% | 15.88 |
| 55 | 14.83 | 96.00% | 16.57 |
| 56 | 15.88 | 95.98% | 17.73 |
| 57 | 16.35 | 96.05% | 18.51 |
| 58 | 17.06 | 96.17% | 19.40 |
| 59 | 18.00 | 96.22% | 20.35 |
| 60 | 19.24 | 96.32% | 21.97 |
| 61 | 21.58 | 94.10% | 22.27 |
| 62 | 20.41 | 96.41% | 23.43 |
| 63 | 18.20 | 96.49% | 20.36 |
| 64 | 18.18 | 96.50% | 21.04 |
| 65 | 18.20 | 96.51% | 21.17 |
| 66 | 18.56 | 96.51% | 21.33 |
| 67 | 19.42 | 96.52% | 22.13 |
| 68 | 20.72 | 96.47% | 23.23 |
| 69 | 20.43 | 59.70% | 23.09 |
| 70 | 20.76 | 59.42% | 23.70 |
| 71 | 20.89 | 96.38% | 24.12 |
| 72 | 21.22 | 96.38% | 24.66 |
| 73 | 21.75 | 96.40% | 25.18 |
| 74 | 22.02 | 96.40% | 25.42 |
| 75 | 21.71 | 96.42% | 25.55 |
| 76 | 22.59 | 96.42% | 26.36 |
| 77 | 14.53 | 94.74% | 18.03 |
| 78 | 14.72 | 95.22% | 18.40 |
| 79 | 24.69 | 96.01% | 26.04 |
| 80 | 30.87 | 95.91% | 31.14 |
| 81 | 29.14 | 95.94% | 29.60 |
| 82 | 27.84 | 96.01% | 28.40 |
| 83 | 26.12 | 95.72% | 26.74 |
| 84 | 25.38 | 96.14% | 25.95 |
| 85 | 26.97 | 94.18% | 29.01 |
| 86 | 28.52 | 94.29% | 30.11 |
| 87 | 29.70 | 96.26% | 31.52 |
| 88 | 29.00 | 96.02% | 30.82 |
| 89 | 29.73 | 96.33% | 31.61 |
| 90 | 29.40 | 96.04% | 31.30 |
| 91 | 29.52 | 96.01% | 31.45 |
| 92 | 30.24 | 96.27% | 32.27 |
| 93 | 29.60 | 95.99% | 31.57 |
| 94 | 29.81 | 96.01% | 31.81 |
| 95 | 30.40 | 96.29% | 32.48 |
| 96 | 29.95 | 95.99% | 32.01 |
| 97 | 29.87 | 94.37% | 32.00 |
| 98 | 31.11 | 95.42% | 33.43 |
| 99 | 30.33 | 94.99% | 32.55 |
| 100 | 30.89 | 95.23% | 33.17 |
| 101 | 30.20 | 94.72% | 32.45 |
| 102 | 30.30 | 94.60% | 32.58 |
| 103 | 31.14 | 94.81% | 33.49 |
| 104 | 30.61 | 94.40% | 32.94 |
| 105 | 30.70 | 94.35% | 33.13 |
| 106 | 31.48 | 94.73% | 34.07 |
| 107 | 31.35 | 95.07% | 34.22 |
| 108 | 31.04 | 94.99% | 33.90 |
| 109 | 31.90 | 95.12% | 34.85 |
| 110 | 31.12 | 94.46% | 34.01 |
| 111 | 31.28 | 94.45% | 34.19 |
| 112 | 32.13 | 94.77% | 35.16 |
| 113 | 31.44 | 94.29% | 34.36 |
| 114 | 31.74 | 94.20% | 34.80 |
| 115 | 32.50 | 94.60% | 35.71 |
| 116 | 31.91 | 94.28% | 35.23 |
| 117 | 33.34 | 95.62% | 36.70 |
| 118 | 3.36 | 93.78% | 4.07 |
| 119 | 4.89 | 94.26% | 5.79 |
| 120 | 6.12 | 89.11% | 7.54 |
| 121 | 5.26 | 83.12% | 6.54 |
| 122 | 9.17 | 84.48% | 9.02 |
| 123 | 9.52 | 86.61% | 8.49 |
| 124 | 8.96 | 90.03% | 7.38 |
| 125 | 8.06 | 92.81% | 7.03 |
| 126 | 9.77 | 91.38% | 6.79 |
