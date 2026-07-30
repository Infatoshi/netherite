# Tape replay: scenario_nether_elytra_20260729T104733Z

348 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 38, field `x`** oracle=-73.40620116756513 magma=-73.43181134805202 |d|=0.0256; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 6.5810 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=177 independent=176 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=9 available=True
- world nearby_hash: checked=9 deltas=5 available=True

**Pixel gate: FAIL** over 177 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 139 | 22986918 | 181587 |
| bossbar | 99 | 842950 | 38430 |
| hud | 69 | 1975629 | 51642 |
| particles | 31 | 652237 | 37772 |
| thinline | 6 | 8601 | 3078 |
| viewmodel | 54 | 483945 | 38175 |

Failed frames (worst first, top 20):

- t=176: 274755 unexplained px, clusters [{'px': 38332, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [384, 328, 398, 401], 'soak_from': 'transit'}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [385, 13, 390, 33], 'soak_from': 'transit'}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [389, 67, 397, 81], 'soak_from': 'transit'}]
- t=167: 272974 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1470, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2044, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=158: 272643 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1534, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=173: 272304 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 988, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 373], 'soak_from': 'transit'}]
- t=175: 272186 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 292, 421, 325], 'soak_from': 'transit'}]
- t=149: 272076 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=171: 271936 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 309], 'soak_from': 'transit'}]
- t=172: 271812 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 388, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 293], 'soak_from': 'transit'}]
- t=170: 271769 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=174: 271700 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=141: 271661 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1350, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=169: 271366 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=144: 271355 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1483, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=147: 271206 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1551, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=168: 271143 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1538, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=165: 270868 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50661, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=159: 270864 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1433, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51067, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=166: 270842 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50718, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=138: 270818 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1476, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 884, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=163: 270783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50620, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.55 | 87.56% | 1.60 |
| 1 | 1.36 | 86.83% | 1.53 |
| 2 | 1.17 | 84.83% | 1.27 |
| 3 | 1.17 | 84.83% | 1.27 |
| 4 | 1.17 | 84.83% | 1.27 |
| 5 | 1.17 | 84.83% | 1.27 |
| 6 | 1.17 | 84.83% | 1.27 |
| 7 | 1.17 | 84.83% | 1.27 |
| 8 | 1.17 | 84.83% | 1.28 |
| 9 | 1.17 | 84.83% | 1.28 |
| 10 | 1.17 | 84.83% | 1.28 |
| 11 | 1.18 | 84.83% | 1.28 |
| 12 | 1.18 | 84.83% | 1.28 |
| 13 | 1.18 | 84.83% | 1.28 |
| 14 | 1.18 | 84.83% | 1.28 |
| 15 | 1.18 | 84.83% | 1.28 |
| 16 | 1.18 | 84.84% | 1.28 |
| 17 | 1.19 | 85.52% | 1.29 |
| 18 | 1.19 | 86.28% | 1.30 |
| 19 | 1.18 | 87.56% | 1.29 |
| 20 | 1.20 | 89.96% | 1.29 |
| 21 | 1.21 | 91.04% | 1.29 |
| 22 | 1.20 | 91.01% | 1.29 |
| 23 | 1.21 | 90.92% | 1.30 |
| 24 | 1.20 | 90.88% | 1.29 |
| 25 | 1.19 | 90.87% | 1.29 |
| 26 | 1.19 | 90.42% | 1.30 |
| 27 | 1.20 | 90.15% | 1.30 |
| 28 | 1.19 | 89.83% | 1.30 |
| 29 | 1.20 | 89.67% | 1.30 |
| 30 | 1.19 | 89.51% | 1.30 |
| 31 | 1.20 | 89.48% | 1.31 |
| 32 | 1.19 | 89.37% | 1.30 |
| 33 | 1.19 | 89.48% | 1.31 |
| 34 | 1.19 | 89.73% | 1.31 |
| 35 | 1.19 | 90.02% | 1.30 |
| 36 | 1.20 | 90.34% | 1.30 |
| 37 | 1.20 | 90.70% | 1.30 |
| 38 | 2.97 | 93.47% | 2.62 |
| 39 | 2.93 | 93.47% | 2.52 |
| 40 | 2.89 | 93.48% | 2.47 |
| 41 | 2.79 | 93.51% | 2.43 |
| 42 | 2.69 | 93.49% | 2.35 |
| 43 | 2.77 | 93.25% | 2.46 |
| 44 | 3.08 | 93.48% | 2.78 |
| 45 | 3.56 | 93.52% | 3.25 |
| 46 | 4.17 | 93.52% | 3.83 |
| 47 | 5.62 | 93.47% | 5.35 |
| 48 | 6.34 | 93.45% | 6.21 |
| 49 | 7.05 | 93.43% | 7.00 |
| 50 | 7.71 | 93.43% | 7.75 |
| 51 | 8.11 | 93.42% | 8.10 |
| 52 | 8.73 | 93.42% | 8.76 |
| 53 | 9.32 | 93.39% | 9.38 |
| 54 | 9.89 | 93.39% | 9.98 |
| 55 | 10.33 | 93.40% | 10.42 |
| 56 | 10.79 | 93.40% | 10.99 |
| 57 | 11.21 | 93.42% | 11.40 |
| 58 | 11.68 | 96.23% | 11.86 |
| 59 | 11.90 | 96.23% | 12.13 |
| 60 | 12.38 | 96.24% | 12.52 |
| 61 | 12.58 | 96.24% | 12.55 |
| 62 | 12.50 | 96.22% | 12.40 |
| 63 | 12.88 | 96.22% | 12.78 |
| 64 | 13.71 | 96.16% | 13.57 |
| 65 | 14.64 | 96.21% | 14.58 |
| 66 | 15.73 | 96.28% | 15.90 |
| 67 | 17.02 | 96.29% | 17.57 |
| 68 | 18.01 | 96.30% | 19.23 |
| 69 | 23.55 | 96.30% | 22.98 |
| 70 | 26.63 | 96.31% | 24.62 |
| 71 | 21.74 | 96.29% | 21.55 |
| 72 | 20.11 | 96.35% | 19.31 |
| 73 | 17.71 | 96.43% | 15.20 |
| 74 | 15.86 | 96.25% | 15.48 |
| 75 | 16.73 | 96.33% | 16.12 |
| 76 | 16.69 | 96.31% | 16.26 |
| 77 | 16.61 | 96.32% | 16.22 |
| 78 | 16.32 | 96.35% | 16.08 |
| 79 | 16.20 | 96.39% | 16.24 |
| 80 | 15.96 | 96.42% | 16.25 |
| 81 | 16.34 | 96.44% | 16.76 |
| 82 | 16.58 | 96.47% | 17.16 |
| 83 | 16.59 | 96.50% | 17.23 |
| 84 | 17.63 | 95.84% | 18.61 |
| 85 | 18.07 | 96.52% | 19.10 |
| 86 | 18.86 | 96.53% | 19.86 |
| 87 | 18.87 | 96.53% | 20.12 |
| 88 | 19.21 | 96.53% | 20.98 |
| 89 | 20.04 | 96.52% | 21.82 |
| 90 | 20.43 | 96.52% | 22.31 |
| 91 | 20.95 | 96.51% | 22.85 |
| 92 | 22.67 | 96.38% | 23.92 |
| 93 | 26.01 | 64.05% | 27.55 |
| 94 | 33.29 | 96.53% | 34.40 |
| 95 | 33.44 | 96.53% | 33.24 |
| 96 | 20.03 | 96.53% | 20.72 |
| 97 | 20.04 | 96.53% | 20.66 |
| 98 | 20.14 | 96.53% | 20.95 |
| 99 | 20.79 | 96.53% | 21.68 |
| 100 | 21.43 | 96.53% | 22.66 |
| 101 | 20.92 | 96.52% | 23.17 |
| 102 | 21.06 | 96.52% | 24.40 |
| 103 | 22.65 | 96.52% | 26.51 |
| 104 | 25.42 | 96.51% | 29.97 |
| 105 | 27.39 | 95.81% | 32.74 |
| 106 | 28.69 | 96.11% | 33.70 |
| 107 | 30.16 | 96.11% | 34.90 |
| 108 | 30.91 | 96.12% | 35.63 |
| 109 | 31.49 | 96.14% | 36.27 |
| 110 | 32.74 | 96.20% | 37.29 |
| 111 | 34.71 | 96.42% | 39.29 |
| 112 | 35.25 | 96.42% | 40.35 |
| 113 | 35.87 | 96.42% | 41.53 |
| 114 | 37.05 | 96.40% | 42.73 |
| 115 | 38.81 | 96.41% | 42.62 |
| 116 | 36.02 | 96.40% | 41.86 |
| 117 | 36.32 | 96.41% | 42.35 |
| 118 | 36.98 | 96.42% | 43.41 |
| 119 | 37.83 | 96.41% | 44.04 |
| 120 | 38.22 | 96.42% | 44.68 |
| 121 | 38.98 | 96.42% | 45.71 |
| 122 | 25.89 | 95.58% | 31.43 |
| 123 | 26.75 | 95.57% | 32.66 |
| 124 | 27.59 | 94.78% | 29.39 |
| 125 | 32.17 | 95.84% | 32.41 |
| 126 | 29.31 | 95.84% | 30.22 |
| 127 | 29.29 | 95.85% | 29.45 |
| 128 | 29.13 | 96.11% | 29.65 |
| 129 | 28.54 | 95.88% | 29.14 |
| 130 | 28.43 | 95.88% | 29.40 |
| 131 | 29.06 | 96.11% | 30.24 |
| 132 | 28.71 | 95.90% | 29.93 |
| 133 | 28.87 | 95.89% | 30.15 |
| 134 | 29.46 | 96.12% | 30.74 |
| 135 | 29.68 | 96.12% | 30.92 |
| 136 | 29.21 | 95.91% | 30.43 |
| 137 | 29.33 | 95.93% | 30.62 |
| 138 | 29.91 | 96.15% | 31.30 |
| 139 | 29.51 | 95.93% | 30.82 |
| 140 | 29.51 | 95.92% | 30.88 |
| 141 | 30.08 | 96.22% | 31.60 |
| 142 | 29.43 | 95.95% | 30.89 |
| 143 | 29.41 | 95.95% | 30.86 |
| 144 | 30.00 | 96.23% | 31.51 |
| 145 | 29.39 | 95.94% | 30.85 |
| 146 | 29.37 | 95.94% | 30.84 |
| 147 | 29.81 | 96.23% | 31.30 |
| 148 | 29.21 | 95.97% | 30.68 |
| 149 | 30.03 | 96.30% | 31.55 |
| 150 | 29.08 | 95.96% | 30.62 |
| 151 | 28.98 | 95.97% | 30.51 |
| 152 | 28.95 | 95.98% | 30.49 |
| 153 | 28.77 | 95.99% | 30.28 |
| 154 | 28.79 | 95.98% | 30.31 |
| 155 | 28.79 | 95.96% | 30.32 |
| 156 | 28.77 | 95.97% | 30.31 |
| 157 | 28.51 | 95.98% | 30.06 |
| 158 | 29.54 | 96.40% | 31.26 |
| 159 | 28.61 | 95.99% | 30.25 |
| 160 | 28.34 | 95.99% | 29.96 |
| 161 | 28.35 | 95.99% | 29.94 |
| 162 | 28.39 | 95.98% | 29.96 |
| 163 | 28.43 | 95.99% | 30.00 |
| 164 | 28.18 | 96.00% | 29.77 |
| 165 | 28.22 | 96.01% | 29.83 |
| 166 | 28.26 | 96.01% | 29.87 |
| 167 | 29.24 | 96.44% | 31.08 |
| 168 | 28.23 | 96.06% | 29.95 |
| 169 | 28.14 | 96.11% | 29.86 |
| 170 | 28.10 | 96.19% | 29.79 |
| 171 | 27.96 | 96.09% | 29.67 |
| 172 | 28.02 | 96.09% | 29.72 |
| 173 | 27.99 | 96.18% | 29.70 |
| 174 | 27.92 | 96.07% | 29.65 |
| 175 | 28.01 | 96.17% | 29.76 |
| 176 | 17.69 | 91.38% | 16.04 |
