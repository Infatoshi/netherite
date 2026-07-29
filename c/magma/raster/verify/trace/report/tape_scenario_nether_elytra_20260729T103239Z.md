# Tape replay: scenario_nether_elytra_20260729T103239Z

328 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 37, field `x`** oracle=-74.55900475457513 magma=-74.62922575009159 |d|=0.0702; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 10.4376 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=142 independent=141 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=8 available=True
- world nearby_hash: checked=8 deltas=7 available=True

**Pixel gate: FAIL** over 142 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 103 | 11592206 | 180950 |
| bossbar | 54 | 80504 | 10169 |
| hud | 66 | 1129871 | 33582 |
| particles | 49 | 626740 | 35266 |
| thinline | 9 | 11048 | 8642 |
| viewmodel | 75 | 917837 | 36258 |

Failed frames (worst first, top 20):

- t=109: 296173 unexplained px, clusters [{'px': 281, 'cls': 'UNEXPLAINED', 'bbox': [0, 2, 15, 62], 'soak_from': 'transit'}, {'px': 7094, 'cls': 'UNEXPLAINED', 'bbox': [0, 68, 44, 300], 'soak_from': 'transit'}, {'px': 14282, 'cls': 'UNEXPLAINED', 'bbox': [0, 302, 44, 730], 'soak_from': 'transit'}, {'px': 2030, 'cls': 'UNEXPLAINED', 'bbox': [0, 730, 44, 815], 'soak_from': 'transit'}]
- t=108: 283851 unexplained px, clusters [{'px': 4567, 'cls': 'UNEXPLAINED', 'bbox': [0, 6, 44, 111], 'soak_from': 'transit'}, {'px': 4229, 'cls': 'UNEXPLAINED', 'bbox': [0, 205, 44, 300], 'soak_from': 'transit'}, {'px': 4068, 'cls': 'UNEXPLAINED', 'bbox': [0, 393, 44, 483], 'soak_from': 'transit'}, {'px': 356, 'cls': 'UNEXPLAINED', 'bbox': [0, 572, 11, 650], 'soak_from': 'transit'}]
- t=110: 280829 unexplained px, clusters [{'px': 2582, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 99], 'soak_from': 'transit'}, {'px': 2576, 'cls': 'UNEXPLAINED', 'bbox': [0, 138, 40, 287], 'soak_from': 'transit'}, {'px': 1257, 'cls': 'UNEXPLAINED', 'bbox': [0, 356, 34, 400], 'soak_from': 'transit'}, {'px': 5734, 'cls': 'UNEXPLAINED', 'bbox': [0, 456, 44, 670], 'soak_from': 'transit'}]
- t=111: 279387 unexplained px, clusters [{'px': 704, 'cls': 'UNEXPLAINED', 'bbox': [0, 1, 24, 61], 'soak_from': 'transit'}, {'px': 1080, 'cls': 'UNEXPLAINED', 'bbox': [0, 108, 30, 205], 'soak_from': 'transit'}, {'px': 7103, 'cls': 'UNEXPLAINED', 'bbox': [0, 218, 44, 528], 'soak_from': 'transit'}, {'px': 2649, 'cls': 'UNEXPLAINED', 'bbox': [0, 503, 44, 638], 'soak_from': 'transit'}]
- t=141: 276206 unexplained px, clusters [{'px': 38427, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 946, 'cls': 'UNEXPLAINED', 'bbox': [384, 308, 398, 404], 'soak_from': 'transit'}, {'px': 1291, 'cls': 'UNEXPLAINED', 'bbox': [384, 807, 423, 853], 'soak_from': 'transit'}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [385, 573, 392, 586], 'soak_from': 'transit'}]
- t=112: 274395 unexplained px, clusters [{'px': 2169, 'cls': 'UNEXPLAINED', 'bbox': [0, 22, 38, 153], 'soak_from': 'transit'}, {'px': 3565, 'cls': 'UNEXPLAINED', 'bbox': [0, 200, 44, 365], 'soak_from': 'transit'}, {'px': 354, 'cls': 'UNEXPLAINED', 'bbox': [0, 405, 17, 426], 'soak_from': 'transit'}, {'px': 1639, 'cls': 'UNEXPLAINED', 'bbox': [0, 443, 44, 523], 'soak_from': 'transit'}]
- t=139: 273355 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1972, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=136: 272559 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2140, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=140: 272395 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=133: 272233 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1972, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=138: 271796 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 664, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 325], 'soak_from': 'transit'}]
- t=126: 271752 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=129: 271595 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=123: 271584 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=120: 271510 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=137: 271465 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 544, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 309], 'soak_from': 'transit'}]
- t=134: 271419 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1408, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 389], 'soak_from': 'transit'}]
- t=117: 271379 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1372, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=135: 271104 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 277], 'soak_from': 'transit'}]
- t=132: 270545 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1595, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]

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
| 37 | 3.54 | 93.59% | 3.28 |
| 38 | 3.12 | 93.51% | 2.88 |
| 39 | 3.27 | 93.24% | 3.05 |
| 40 | 3.52 | 93.41% | 3.38 |
| 41 | 3.85 | 93.44% | 3.76 |
| 42 | 4.04 | 93.56% | 3.92 |
| 43 | 4.30 | 93.57% | 4.15 |
| 44 | 4.55 | 93.57% | 4.41 |
| 45 | 4.79 | 93.58% | 4.67 |
| 46 | 5.13 | 96.41% | 4.96 |
| 47 | 5.67 | 96.30% | 5.56 |
| 48 | 5.90 | 96.30% | 5.87 |
| 49 | 6.20 | 96.31% | 6.20 |
| 50 | 6.38 | 96.33% | 6.49 |
| 51 | 6.52 | 96.30% | 6.63 |
| 52 | 6.77 | 96.34% | 6.83 |
| 53 | 7.03 | 96.36% | 7.13 |
| 54 | 7.37 | 96.36% | 7.46 |
| 55 | 7.69 | 96.46% | 7.90 |
| 56 | 8.02 | 96.45% | 8.34 |
| 57 | 8.48 | 96.45% | 8.79 |
| 58 | 9.19 | 96.50% | 9.49 |
| 59 | 8.33 | 94.98% | 9.06 |
| 60 | 9.40 | 95.83% | 9.22 |
| 61 | 9.09 | 94.24% | 9.37 |
| 62 | 8.77 | 95.83% | 7.61 |
| 63 | 7.05 | 94.22% | 7.38 |
| 64 | 7.41 | 95.82% | 7.79 |
| 65 | 7.41 | 94.23% | 7.82 |
| 66 | 7.65 | 95.83% | 8.06 |
| 67 | 7.68 | 95.01% | 8.06 |
| 68 | 8.05 | 96.52% | 8.39 |
| 69 | 8.22 | 96.53% | 8.56 |
| 70 | 8.22 | 96.53% | 8.63 |
| 71 | 8.52 | 96.53% | 8.93 |
| 72 | 8.60 | 96.53% | 9.03 |
| 73 | 8.47 | 96.53% | 8.96 |
| 74 | 8.68 | 96.53% | 9.20 |
| 75 | 8.63 | 96.51% | 9.18 |
| 76 | 8.69 | 96.52% | 9.42 |
| 77 | 9.04 | 96.52% | 9.72 |
| 78 | 9.22 | 96.50% | 9.87 |
| 79 | 9.09 | 96.48% | 9.77 |
| 80 | 9.15 | 96.43% | 9.90 |
| 81 | 9.22 | 96.38% | 10.08 |
| 82 | 9.62 | 96.49% | 10.61 |
| 83 | 10.15 | 96.52% | 11.23 |
| 84 | 11.29 | 96.49% | 12.56 |
| 85 | 14.30 | 96.53% | 15.96 |
| 86 | 15.51 | 94.25% | 16.13 |
| 87 | 17.66 | 94.25% | 16.49 |
| 88 | 10.89 | 96.52% | 12.34 |
| 89 | 10.89 | 96.52% | 12.50 |
| 90 | 10.74 | 94.25% | 12.69 |
| 91 | 11.62 | 96.51% | 13.61 |
| 92 | 12.10 | 96.50% | 13.92 |
| 93 | 12.45 | 96.47% | 14.49 |
| 94 | 12.91 | 96.46% | 15.39 |
| 95 | 13.49 | 96.45% | 16.49 |
| 96 | 15.11 | 96.43% | 18.47 |
| 97 | 16.31 | 96.41% | 20.44 |
| 98 | 17.92 | 96.46% | 22.88 |
| 99 | 20.32 | 96.47% | 25.63 |
| 100 | 22.51 | 96.46% | 27.54 |
| 101 | 21.96 | 96.49% | 21.88 |
| 102 | 18.40 | 96.47% | 20.19 |
| 103 | 25.13 | 96.53% | 23.59 |
| 104 | 25.95 | 96.53% | 25.74 |
| 105 | 25.90 | 96.53% | 28.06 |
| 106 | 26.85 | 96.53% | 25.64 |
| 107 | 27.21 | 96.53% | 25.92 |
| 108 | 38.70 | 96.53% | 40.62 |
| 109 | 42.92 | 96.53% | 41.35 |
| 110 | 40.57 | 96.53% | 40.91 |
| 111 | 41.07 | 96.79% | 41.37 |
| 112 | 39.78 | 96.52% | 39.99 |
| 113 | 23.17 | 96.05% | 25.54 |
| 114 | 23.76 | 96.43% | 26.16 |
| 115 | 24.75 | 95.82% | 25.94 |
| 116 | 32.28 | 95.94% | 33.30 |
| 117 | 31.94 | 96.23% | 33.40 |
| 118 | 31.08 | 95.88% | 31.58 |
| 119 | 30.35 | 95.87% | 31.09 |
| 120 | 30.93 | 96.25% | 31.60 |
| 121 | 30.01 | 95.95% | 30.50 |
| 122 | 29.69 | 96.07% | 30.39 |
| 123 | 30.31 | 96.46% | 31.26 |
| 124 | 29.47 | 96.06% | 30.45 |
| 125 | 29.42 | 96.04% | 30.46 |
| 126 | 30.05 | 96.41% | 31.25 |
| 127 | 29.25 | 96.06% | 30.41 |
| 128 | 29.26 | 96.07% | 30.45 |
| 129 | 29.97 | 96.42% | 31.30 |
| 130 | 29.07 | 96.04% | 30.37 |
| 131 | 29.44 | 96.08% | 30.80 |
| 132 | 29.23 | 96.09% | 30.60 |
| 133 | 29.79 | 96.51% | 31.28 |
| 134 | 29.16 | 96.31% | 30.56 |
| 135 | 28.99 | 96.21% | 30.42 |
| 136 | 29.67 | 96.48% | 31.24 |
| 137 | 29.19 | 96.19% | 30.77 |
| 138 | 29.31 | 96.18% | 31.01 |
| 139 | 30.33 | 96.48% | 32.31 |
| 140 | 30.09 | 96.27% | 32.07 |
| 141 | 18.87 | 91.38% | 17.28 |
