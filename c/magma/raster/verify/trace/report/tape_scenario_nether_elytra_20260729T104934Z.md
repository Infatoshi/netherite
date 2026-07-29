# Tape replay: scenario_nether_elytra_20260729T104934Z

349 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 30, field `x`** oracle=-73.43346949435292 magma=-73.45301737804938 |d|=0.0195; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 7.3126 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=179 independent=178 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=9 available=True
- world nearby_hash: checked=9 deltas=4 available=True

**Pixel gate: FAIL** over 179 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 148 | 24136523 | 181655 |
| bossbar | 109 | 934246 | 38430 |
| hud | 113 | 2709218 | 53080 |
| particles | 32 | 639223 | 36427 |
| thinline | 4 | 6310 | 1768 |
| viewmodel | 45 | 416625 | 38901 |

Failed frames (worst first, top 20):

- t=178: 289281 unexplained px, clusters [{'px': 38051, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 722, 'cls': 'UNEXPLAINED', 'bbox': [384, 328, 396, 402], 'soak_from': 'transit'}, {'px': 384, 'cls': 'UNEXPLAINED', 'bbox': [384, 790, 406, 853], 'soak_from': 'transit'}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [386, 8, 391, 25], 'soak_from': 'transit'}]
- t=176: 275389 unexplained px, clusters [{'px': 38407, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2188, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=173: 275324 unexplained px, clusters [{'px': 38413, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2272, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=170: 275193 unexplained px, clusters [{'px': 38409, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2216, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=159: 274628 unexplained px, clusters [{'px': 38411, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1596, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=177: 274002 unexplained px, clusters [{'px': 38412, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=172: 273870 unexplained px, clusters [{'px': 38418, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 472, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 293], 'soak_from': 'transit'}]
- t=175: 273777 unexplained px, clusters [{'px': 38415, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 116, 'cls': 'UNEXPLAINED', 'bbox': [402, 294, 407, 323], 'soak_from': 'transit'}]
- t=171: 273503 unexplained px, clusters [{'px': 38411, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 309], 'soak_from': 'transit'}]
- t=174: 273455 unexplained px, clusters [{'px': 38417, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=169: 273337 unexplained px, clusters [{'px': 38407, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1583, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=168: 273245 unexplained px, clusters [{'px': 38409, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1539, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=139: 273123 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=151: 272864 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1542, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=160: 272853 unexplained px, clusters [{'px': 38413, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1488, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51202, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=150: 272852 unexplained px, clusters [{'px': 38423, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=167: 272806 unexplained px, clusters [{'px': 38410, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50960, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=166: 272793 unexplained px, clusters [{'px': 38413, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50952, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=161: 272771 unexplained px, clusters [{'px': 38407, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1596, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50981, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=165: 272764 unexplained px, clusters [{'px': 38413, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50982, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.53 | 87.01% | 1.58 |
| 1 | 1.34 | 86.29% | 1.52 |
| 2 | 1.15 | 84.29% | 1.26 |
| 3 | 1.15 | 84.29% | 1.26 |
| 4 | 1.15 | 84.29% | 1.26 |
| 5 | 1.15 | 84.29% | 1.26 |
| 6 | 1.15 | 84.29% | 1.26 |
| 7 | 1.15 | 84.29% | 1.26 |
| 8 | 1.15 | 84.29% | 1.26 |
| 9 | 1.17 | 84.96% | 1.29 |
| 10 | 1.20 | 85.70% | 1.29 |
| 11 | 1.22 | 86.95% | 1.29 |
| 12 | 1.24 | 89.34% | 1.29 |
| 13 | 1.24 | 90.40% | 1.28 |
| 14 | 1.24 | 90.34% | 1.29 |
| 15 | 1.24 | 90.22% | 1.29 |
| 16 | 1.21 | 90.12% | 1.28 |
| 17 | 1.20 | 90.09% | 1.27 |
| 18 | 1.20 | 89.99% | 1.27 |
| 19 | 1.17 | 89.53% | 1.25 |
| 20 | 1.16 | 89.19% | 1.23 |
| 21 | 1.16 | 88.88% | 1.21 |
| 22 | 1.15 | 88.59% | 1.20 |
| 23 | 1.15 | 88.32% | 1.18 |
| 24 | 1.15 | 88.32% | 1.18 |
| 25 | 1.14 | 88.32% | 1.18 |
| 26 | 1.14 | 88.47% | 1.19 |
| 27 | 1.13 | 88.60% | 1.20 |
| 28 | 1.13 | 88.86% | 1.21 |
| 29 | 1.14 | 89.18% | 1.23 |
| 30 | 3.00 | 93.45% | 2.67 |
| 31 | 2.98 | 93.46% | 2.61 |
| 32 | 2.92 | 93.45% | 2.55 |
| 33 | 2.86 | 93.46% | 2.47 |
| 34 | 2.74 | 93.46% | 2.41 |
| 35 | 2.76 | 93.21% | 2.45 |
| 36 | 3.05 | 93.45% | 2.72 |
| 37 | 3.41 | 93.50% | 3.11 |
| 38 | 3.87 | 93.49% | 3.56 |
| 39 | 5.09 | 93.46% | 4.66 |
| 40 | 5.63 | 93.42% | 5.24 |
| 41 | 6.18 | 93.41% | 5.84 |
| 42 | 6.76 | 93.41% | 6.48 |
| 43 | 7.22 | 93.39% | 7.01 |
| 44 | 7.65 | 93.37% | 7.47 |
| 45 | 8.10 | 93.37% | 7.93 |
| 46 | 8.26 | 93.39% | 7.96 |
| 47 | 8.68 | 93.37% | 8.36 |
| 48 | 8.99 | 93.39% | 8.66 |
| 49 | 9.24 | 93.38% | 8.91 |
| 50 | 9.45 | 93.39% | 9.10 |
| 51 | 9.65 | 96.23% | 9.30 |
| 52 | 9.70 | 96.22% | 9.30 |
| 53 | 10.30 | 96.20% | 9.84 |
| 54 | 10.78 | 96.20% | 10.42 |
| 55 | 11.41 | 96.11% | 11.18 |
| 56 | 12.01 | 96.10% | 12.01 |
| 57 | 12.85 | 96.13% | 13.15 |
| 58 | 13.88 | 96.31% | 14.54 |
| 59 | 15.43 | 96.26% | 16.47 |
| 60 | 16.70 | 96.31% | 18.32 |
| 61 | 20.23 | 96.29% | 22.80 |
| 62 | 26.21 | 96.29% | 26.29 |
| 63 | 25.14 | 96.29% | 22.42 |
| 64 | 19.19 | 96.15% | 20.53 |
| 65 | 20.65 | 96.43% | 19.61 |
| 66 | 19.84 | 94.14% | 17.70 |
| 67 | 17.42 | 96.45% | 13.86 |
| 68 | 15.81 | 96.32% | 15.24 |
| 69 | 15.65 | 96.33% | 15.03 |
| 70 | 16.31 | 96.30% | 15.43 |
| 71 | 16.22 | 96.30% | 15.45 |
| 72 | 15.99 | 96.35% | 15.46 |
| 73 | 15.98 | 96.39% | 15.66 |
| 74 | 15.98 | 96.38% | 15.80 |
| 75 | 16.40 | 96.42% | 16.26 |
| 76 | 16.77 | 96.44% | 16.74 |
| 77 | 16.78 | 96.48% | 16.84 |
| 78 | 16.71 | 96.50% | 16.89 |
| 79 | 16.72 | 96.51% | 16.97 |
| 80 | 17.18 | 96.51% | 17.24 |
| 81 | 17.53 | 96.52% | 17.63 |
| 82 | 17.77 | 96.53% | 18.11 |
| 83 | 18.38 | 96.52% | 18.90 |
| 84 | 19.03 | 96.52% | 19.97 |
| 85 | 20.44 | 96.52% | 21.98 |
| 86 | 23.11 | 96.51% | 25.11 |
| 87 | 30.09 | 96.51% | 32.48 |
| 88 | 33.66 | 96.04% | 34.02 |
| 89 | 20.12 | 95.81% | 21.06 |
| 90 | 20.09 | 96.46% | 21.84 |
| 91 | 19.79 | 96.46% | 22.31 |
| 92 | 19.86 | 96.47% | 22.73 |
| 93 | 20.16 | 96.20% | 23.34 |
| 94 | 21.35 | 96.17% | 24.68 |
| 95 | 22.96 | 96.08% | 26.29 |
| 96 | 22.95 | 94.81% | 26.40 |
| 97 | 23.54 | 67.80% | 27.69 |
| 98 | 24.82 | 93.65% | 28.66 |
| 99 | 25.58 | 96.21% | 29.42 |
| 100 | 24.75 | 96.38% | 28.95 |
| 101 | 25.19 | 96.37% | 29.34 |
| 102 | 26.32 | 96.34% | 30.32 |
| 103 | 27.07 | 96.21% | 31.15 |
| 104 | 27.73 | 96.22% | 31.84 |
| 105 | 28.45 | 95.94% | 32.64 |
| 106 | 29.72 | 82.13% | 34.00 |
| 107 | 31.04 | 95.91% | 35.98 |
| 108 | 32.44 | 96.07% | 36.44 |
| 109 | 32.23 | 96.34% | 35.60 |
| 110 | 31.33 | 96.33% | 36.38 |
| 111 | 32.02 | 96.34% | 37.22 |
| 112 | 32.53 | 96.38% | 38.00 |
| 113 | 34.07 | 96.41% | 39.37 |
| 114 | 34.70 | 96.43% | 40.05 |
| 115 | 35.46 | 96.44% | 40.94 |
| 116 | 36.13 | 96.44% | 41.34 |
| 117 | 37.01 | 96.46% | 41.70 |
| 118 | 37.59 | 96.46% | 42.34 |
| 119 | 38.25 | 96.46% | 42.93 |
| 120 | 38.69 | 96.46% | 42.86 |
| 121 | 38.42 | 96.47% | 42.84 |
| 122 | 37.74 | 96.47% | 43.74 |
| 123 | 25.58 | 95.50% | 30.52 |
| 124 | 25.84 | 95.46% | 31.15 |
| 125 | 25.53 | 95.01% | 26.81 |
| 126 | 28.51 | 95.87% | 30.65 |
| 127 | 32.04 | 95.87% | 32.52 |
| 128 | 31.12 | 95.88% | 31.23 |
| 129 | 30.48 | 95.88% | 31.11 |
| 130 | 30.70 | 95.89% | 30.95 |
| 131 | 28.81 | 95.89% | 29.93 |
| 132 | 28.45 | 95.90% | 29.77 |
| 133 | 29.17 | 96.16% | 30.46 |
| 134 | 28.81 | 95.92% | 30.04 |
| 135 | 28.83 | 95.92% | 30.17 |
| 136 | 29.41 | 96.18% | 30.83 |
| 137 | 29.04 | 95.94% | 30.52 |
| 138 | 29.14 | 95.94% | 30.58 |
| 139 | 29.96 | 96.23% | 31.54 |
| 140 | 29.34 | 95.95% | 30.87 |
| 141 | 30.00 | 96.22% | 31.61 |
| 142 | 29.37 | 95.96% | 30.92 |
| 143 | 29.32 | 95.97% | 30.88 |
| 144 | 29.93 | 96.24% | 31.54 |
| 145 | 29.33 | 95.96% | 30.87 |
| 146 | 29.21 | 95.95% | 30.78 |
| 147 | 29.81 | 96.24% | 31.42 |
| 148 | 29.21 | 95.96% | 30.79 |
| 149 | 29.25 | 95.97% | 30.80 |
| 150 | 29.28 | 95.96% | 30.94 |
| 151 | 29.23 | 95.96% | 30.88 |
| 152 | 29.19 | 95.98% | 30.83 |
| 153 | 28.85 | 95.99% | 30.47 |
| 154 | 28.85 | 95.99% | 30.45 |
| 155 | 28.85 | 95.99% | 30.45 |
| 156 | 28.86 | 95.98% | 30.46 |
| 157 | 28.72 | 95.97% | 30.28 |
| 158 | 28.72 | 95.95% | 30.29 |
| 159 | 29.70 | 96.39% | 31.54 |
| 160 | 28.71 | 96.00% | 30.47 |
| 161 | 28.46 | 95.99% | 30.17 |
| 162 | 28.50 | 95.96% | 30.15 |
| 163 | 28.54 | 95.99% | 30.16 |
| 164 | 28.36 | 96.02% | 29.98 |
| 165 | 28.31 | 96.03% | 29.94 |
| 166 | 28.36 | 96.03% | 30.00 |
| 167 | 28.20 | 96.03% | 29.84 |
| 168 | 28.54 | 96.05% | 30.31 |
| 169 | 28.41 | 96.10% | 30.17 |
| 170 | 29.00 | 96.52% | 30.81 |
| 171 | 28.16 | 96.11% | 29.84 |
| 172 | 28.17 | 96.17% | 29.84 |
| 173 | 28.82 | 96.53% | 30.52 |
| 174 | 28.04 | 96.10% | 29.70 |
| 175 | 28.02 | 96.13% | 29.73 |
| 176 | 28.80 | 96.54% | 30.62 |
| 177 | 27.88 | 96.20% | 29.50 |
| 178 | 23.19 | 91.38% | 22.22 |
