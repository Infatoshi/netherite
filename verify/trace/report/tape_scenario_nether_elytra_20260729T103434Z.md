# Tape replay: scenario_nether_elytra_20260729T103434Z

350 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 11.4053 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=136 independent=135 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=7 available=True
- world nearby_hash: checked=7 deltas=6 available=True

**Pixel gate: FAIL** over 136 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 77 | 10593124 | 181873 |
| bossbar | 56 | 390495 | 38429 |
| hud | 58 | 1158897 | 23053 |
| particles | 13 | 170578 | 35073 |
| thinline | 3 | 2886 | 2383 |
| transit | 18 | 149442 | 20751 |
| viewmodel | 38 | 424882 | 30685 |

Failed frames (worst first, top 20):

- t=111: 274185 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=108: 273861 unexplained px, clusters [{'px': 38303, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=106: 273842 unexplained px, clusters [{'px': 38176, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=113: 272952 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50680, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=96: 272911 unexplained px, clusters [{'px': 24209, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 539], 'soak_from': 'transit'}, {'px': 13834, 'cls': 'UNEXPLAINED', 'bbox': [0, 545, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}]
- t=112: 272844 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50576, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=110: 272628 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50246, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=109: 272508 unexplained px, clusters [{'px': 38403, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50063, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=107: 272445 unexplained px, clusters [{'px': 38166, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50384, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=103: 272329 unexplained px, clusters [{'px': 38139, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=100: 272123 unexplained px, clusters [{'px': 38180, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=97: 272088 unexplained px, clusters [{'px': 24739, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 555], 'soak_from': 'transit'}, {'px': 13311, 'cls': 'UNEXPLAINED', 'bbox': [0, 553, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}]
- t=99: 271842 unexplained px, clusters [{'px': 38082, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50508, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=98: 271836 unexplained px, clusters [{'px': 28063, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 633], 'soak_from': 'transit'}, {'px': 9889, 'cls': 'UNEXPLAINED', 'bbox': [0, 626, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}]
- t=105: 271803 unexplained px, clusters [{'px': 38102, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49990, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=104: 271495 unexplained px, clusters [{'px': 38175, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49949, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=102: 271055 unexplained px, clusters [{'px': 38197, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49812, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=101: 270933 unexplained px, clusters [{'px': 38208, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49839, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=95: 231210 unexplained px, clusters [{'px': 50391, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 180819, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=94: 230857 unexplained px, clusters [{'px': 50046, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 180811, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]

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
| 41 | 4.28 | 93.57% | 4.21 |
| 42 | 4.77 | 93.59% | 4.79 |
| 43 | 5.42 | 96.43% | 5.57 |
| 44 | 6.04 | 96.44% | 6.29 |
| 45 | 6.94 | 96.37% | 7.31 |
| 46 | 7.52 | 96.36% | 7.91 |
| 47 | 7.83 | 96.32% | 8.15 |
| 48 | 8.41 | 96.26% | 8.84 |
| 49 | 8.92 | 96.19% | 9.41 |
| 50 | 9.30 | 96.36% | 9.89 |
| 51 | 9.79 | 96.31% | 10.51 |
| 52 | 10.01 | 96.26% | 10.92 |
| 53 | 10.50 | 96.22% | 11.35 |
| 54 | 11.00 | 96.33% | 12.00 |
| 55 | 11.58 | 96.38% | 12.61 |
| 56 | 12.10 | 96.41% | 13.28 |
| 57 | 12.42 | 96.38% | 13.89 |
| 58 | 12.85 | 96.43% | 14.41 |
| 59 | 12.91 | 94.81% | 14.59 |
| 60 | 14.49 | 95.70% | 16.56 |
| 61 | 15.65 | 96.47% | 16.33 |
| 62 | 14.93 | 96.44% | 17.42 |
| 63 | 12.58 | 94.21% | 14.36 |
| 64 | 12.67 | 95.82% | 14.42 |
| 65 | 13.00 | 94.24% | 14.81 |
| 66 | 13.59 | 96.53% | 15.26 |
| 67 | 14.23 | 96.53% | 15.75 |
| 68 | 14.77 | 96.53% | 16.23 |
| 69 | 14.55 | 96.52% | 16.24 |
| 70 | 15.27 | 96.52% | 16.97 |
| 71 | 15.01 | 96.52% | 16.90 |
| 72 | 15.33 | 96.52% | 17.34 |
| 73 | 15.55 | 96.49% | 17.71 |
| 74 | 15.81 | 96.51% | 18.03 |
| 75 | 15.76 | 96.51% | 18.14 |
| 76 | 16.70 | 96.44% | 18.95 |
| 77 | 16.95 | 96.29% | 19.33 |
| 78 | 17.11 | 56.91% | 19.63 |
| 79 | 17.24 | 57.09% | 19.86 |
| 80 | 17.60 | 57.49% | 20.46 |
| 81 | 17.67 | 58.33% | 20.78 |
| 82 | 18.50 | 96.49% | 21.53 |
| 83 | 19.16 | 96.47% | 22.37 |
| 84 | 21.05 | 96.47% | 24.01 |
| 85 | 21.40 | 96.48% | 23.79 |
| 86 | 18.11 | 92.86% | 19.50 |
| 87 | 12.28 | 91.73% | 15.81 |
| 88 | 23.82 | 95.72% | 25.12 |
| 89 | 32.08 | 96.22% | 34.78 |
| 90 | 33.76 | 96.05% | 34.34 |
| 91 | 32.13 | 96.06% | 33.58 |
| 92 | 31.62 | 96.05% | 33.21 |
| 93 | 32.05 | 96.23% | 33.55 |
| 94 | 31.37 | 94.80% | 32.96 |
| 95 | 31.45 | 94.62% | 33.14 |
| 96 | 32.12 | 94.78% | 34.07 |
| 97 | 31.63 | 94.44% | 33.74 |
| 98 | 31.99 | 95.07% | 34.59 |
| 99 | 31.77 | 94.93% | 34.46 |
| 100 | 32.12 | 95.22% | 34.79 |
| 101 | 31.46 | 94.82% | 34.06 |
| 102 | 31.52 | 94.48% | 34.21 |
| 103 | 32.36 | 94.66% | 35.20 |
| 104 | 31.99 | 94.38% | 34.79 |
| 105 | 32.03 | 94.37% | 34.93 |
| 106 | 33.20 | 95.45% | 36.46 |
| 107 | 32.51 | 95.06% | 35.62 |
| 108 | 33.14 | 95.10% | 36.32 |
| 109 | 32.73 | 94.40% | 35.66 |
| 110 | 32.83 | 94.39% | 35.78 |
| 111 | 34.09 | 94.76% | 37.39 |
| 112 | 33.63 | 94.32% | 37.19 |
| 113 | 33.92 | 94.17% | 37.67 |
| 114 | 3.98 | 94.03% | 4.83 |
| 115 | 3.30 | 81.88% | 4.34 |
| 116 | 5.29 | 83.29% | 6.19 |
| 117 | 7.34 | 84.62% | 6.10 |
| 118 | 6.89 | 87.06% | 6.30 |
| 119 | 6.11 | 90.15% | 5.43 |
| 120 | 6.21 | 92.96% | 5.18 |
| 121 | 6.65 | 93.31% | 5.52 |
| 122 | 5.20 | 93.02% | 4.37 |
| 123 | 5.11 | 92.94% | 4.54 |
| 124 | 5.75 | 93.44% | 5.53 |
| 125 | 4.90 | 93.01% | 4.72 |
| 126 | 6.25 | 94.31% | 6.19 |
| 127 | 5.26 | 93.86% | 5.17 |
| 128 | 5.42 | 93.65% | 5.35 |
| 129 | 5.45 | 93.34% | 5.44 |
| 130 | 5.62 | 93.24% | 5.67 |
| 131 | 5.41 | 93.09% | 5.56 |
| 132 | 5.58 | 93.07% | 5.79 |
| 133 | 5.76 | 93.06% | 6.01 |
| 134 | 5.78 | 93.11% | 6.05 |
| 135 | 8.62 | 91.38% | 6.32 |
