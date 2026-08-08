# Tape replay: scenario_nether_elytra_20260729T105133Z

344 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 5.0891 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=102 independent=101 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 available=True
- world nearby_hash: checked=6 deltas=5 available=True

**Pixel gate: FAIL** over 102 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 63 | 11744977 | 182275 |
| bossbar | 23 | 55481 | 12108 |
| hud | 61 | 611378 | 42829 |
| particles | 13 | 112337 | 34073 |
| viewmodel | 36 | 214417 | 33740 |

Failed frames (worst first, top 20):

- t=73: 318635 unexplained px, clusters [{'px': 4193, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 119], 'soak_from': 'transit'}, {'px': 12731, 'cls': 'UNEXPLAINED', 'bbox': [0, 200, 44, 525], 'soak_from': 'transit'}, {'px': 215, 'cls': 'UNEXPLAINED', 'bbox': [0, 494, 11, 518], 'soak_from': 'transit'}, {'px': 4285, 'cls': 'UNEXPLAINED', 'bbox': [0, 567, 44, 674], 'soak_from': 'transit'}]
- t=75: 304532 unexplained px, clusters [{'px': 1905, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 24, 116], 'soak_from': 'transit'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [0, 142, 6, 173], 'soak_from': 'transit'}, {'px': 1011, 'cls': 'UNEXPLAINED', 'bbox': [0, 176, 25, 255], 'soak_from': 'transit'}, {'px': 6204, 'cls': 'UNEXPLAINED', 'bbox': [0, 245, 44, 449], 'soak_from': 'transit'}]
- t=74: 303798 unexplained px, clusters [{'px': 835, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 35, 91], 'soak_from': 'transit'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [0, 25, 4, 53], 'soak_from': 'transit'}, {'px': 1946, 'cls': 'UNEXPLAINED', 'bbox': [0, 173, 41, 272], 'soak_from': 'transit'}, {'px': 5397, 'cls': 'UNEXPLAINED', 'bbox': [0, 272, 44, 505], 'soak_from': 'transit'}]
- t=76: 303328 unexplained px, clusters [{'px': 127, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 9, 23], 'soak_from': 'transit'}, {'px': 3362, 'cls': 'UNEXPLAINED', 'bbox': [0, 30, 44, 228], 'soak_from': 'transit'}, {'px': 573, 'cls': 'UNEXPLAINED', 'bbox': [0, 301, 14, 353], 'soak_from': 'transit'}, {'px': 2359, 'cls': 'UNEXPLAINED', 'bbox': [0, 313, 44, 457], 'soak_from': 'transit'}]
- t=72: 289162 unexplained px, clusters [{'px': 11040, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 245], 'soak_from': 'transit'}, {'px': 1983, 'cls': 'UNEXPLAINED', 'bbox': [0, 427, 44, 471], 'soak_from': 'transit'}, {'px': 9872, 'cls': 'UNEXPLAINED', 'bbox': [0, 480, 44, 700], 'soak_from': 'transit'}, {'px': 2771, 'cls': 'UNEXPLAINED', 'bbox': [0, 790, 44, 853], 'soak_from': 'transit'}]
- t=94: 276107 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2024, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=91: 275727 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=100: 275361 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2184, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=88: 275292 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=97: 275258 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2084, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=95: 274867 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 293], 'soak_from': 'transit'}]
- t=96: 274395 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 308, 421, 341], 'soak_from': 'transit'}]
- t=93: 274320 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=92: 274239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 277], 'soak_from': 'transit'}]
- t=90: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 273976 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51175, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=99: 273960 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 760, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 341], 'soak_from': 'transit'}]
- t=85: 273747 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=98: 273401 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=87: 273337 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50927, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.58 | 87.55% | 1.64 |
| 1 | 1.39 | 86.82% | 1.57 |
| 2 | 1.20 | 84.82% | 1.32 |
| 3 | 1.20 | 84.82% | 1.32 |
| 4 | 1.20 | 84.82% | 1.32 |
| 5 | 1.20 | 84.82% | 1.32 |
| 6 | 1.20 | 84.82% | 1.32 |
| 7 | 1.20 | 84.82% | 1.32 |
| 8 | 1.20 | 84.82% | 1.32 |
| 9 | 1.20 | 84.82% | 1.32 |
| 10 | 1.21 | 85.51% | 1.33 |
| 11 | 1.22 | 86.26% | 1.34 |
| 12 | 1.23 | 87.54% | 1.34 |
| 13 | 1.25 | 89.95% | 1.35 |
| 14 | 1.26 | 91.02% | 1.34 |
| 15 | 1.26 | 90.99% | 1.35 |
| 16 | 1.26 | 90.90% | 1.36 |
| 17 | 1.25 | 90.53% | 1.34 |
| 18 | 1.25 | 90.17% | 1.34 |
| 19 | 1.22 | 89.87% | 1.32 |
| 20 | 1.22 | 89.67% | 1.32 |
| 21 | 1.22 | 89.57% | 1.32 |
| 22 | 1.21 | 89.53% | 1.32 |
| 23 | 1.21 | 89.57% | 1.32 |
| 24 | 1.21 | 89.69% | 1.33 |
| 25 | 1.21 | 89.74% | 1.32 |
| 26 | 1.21 | 89.98% | 1.33 |
| 27 | 1.21 | 90.37% | 1.33 |
| 28 | 1.22 | 90.79% | 1.33 |
| 29 | 1.23 | 91.22% | 1.34 |
| 30 | 1.23 | 91.55% | 1.35 |
| 31 | 1.24 | 91.69% | 1.36 |
| 32 | 1.24 | 92.04% | 1.39 |
| 33 | 1.24 | 92.43% | 1.41 |
| 34 | 1.24 | 92.77% | 1.43 |
| 35 | 1.24 | 92.96% | 1.45 |
| 36 | 1.26 | 93.06% | 1.47 |
| 37 | 1.26 | 93.11% | 1.47 |
| 38 | 1.27 | 93.18% | 1.48 |
| 39 | 4.27 | 93.60% | 4.33 |
| 40 | 4.36 | 93.62% | 4.46 |
| 41 | 4.33 | 93.62% | 4.45 |
| 42 | 5.55 | 93.62% | 5.91 |
| 43 | 6.51 | 96.43% | 7.07 |
| 44 | 7.38 | 96.42% | 8.15 |
| 45 | 8.56 | 96.37% | 9.51 |
| 46 | 9.66 | 96.36% | 10.71 |
| 47 | 10.46 | 96.31% | 11.61 |
| 48 | 11.28 | 96.24% | 12.61 |
| 49 | 12.01 | 96.20% | 13.54 |
| 50 | 12.77 | 96.30% | 14.48 |
| 51 | 13.79 | 96.25% | 15.74 |
| 52 | 14.27 | 96.21% | 16.61 |
| 53 | 15.39 | 96.19% | 17.84 |
| 54 | 16.00 | 95.97% | 18.76 |
| 55 | 16.77 | 96.01% | 19.84 |
| 56 | 18.12 | 95.99% | 21.51 |
| 57 | 18.87 | 96.07% | 22.80 |
| 58 | 20.05 | 96.17% | 24.46 |
| 59 | 21.57 | 96.23% | 26.42 |
| 60 | 23.93 | 96.32% | 29.92 |
| 61 | 27.37 | 94.10% | 31.55 |
| 62 | 27.38 | 96.41% | 33.38 |
| 63 | 26.63 | 96.49% | 30.48 |
| 64 | 28.01 | 96.50% | 31.70 |
| 65 | 28.65 | 96.51% | 32.70 |
| 66 | 29.83 | 96.51% | 33.56 |
| 67 | 30.29 | 96.53% | 33.39 |
| 68 | 32.09 | 96.51% | 34.20 |
| 69 | 30.35 | 79.25% | 30.55 |
| 70 | 32.59 | 96.53% | 34.21 |
| 71 | 30.49 | 96.53% | 31.02 |
| 72 | 40.84 | 96.53% | 39.04 |
| 73 | 51.66 | 96.53% | 56.63 |
| 74 | 52.33 | 96.53% | 58.12 |
| 75 | 53.70 | 96.86% | 59.47 |
| 76 | 53.39 | 96.52% | 59.44 |
| 77 | 31.20 | 96.28% | 37.05 |
| 78 | 32.17 | 96.54% | 37.75 |
| 79 | 25.55 | 96.08% | 28.31 |
| 80 | 30.09 | 95.89% | 31.79 |
| 81 | 29.24 | 96.22% | 31.06 |
| 82 | 28.85 | 95.89% | 30.03 |
| 83 | 28.61 | 95.83% | 29.66 |
| 84 | 28.70 | 95.83% | 29.91 |
| 85 | 29.41 | 96.27% | 30.72 |
| 86 | 28.59 | 95.87% | 29.97 |
| 87 | 28.69 | 95.89% | 30.33 |
| 88 | 29.38 | 96.27% | 31.26 |
| 89 | 28.70 | 95.91% | 30.57 |
| 90 | 28.83 | 95.91% | 30.75 |
| 91 | 29.80 | 96.31% | 31.83 |
| 92 | 29.32 | 95.83% | 31.04 |
| 93 | 29.33 | 95.88% | 31.08 |
| 94 | 30.20 | 96.38% | 32.17 |
| 95 | 29.55 | 96.05% | 31.43 |
| 96 | 29.38 | 95.91% | 31.28 |
| 97 | 29.48 | 96.35% | 31.88 |
| 98 | 28.66 | 95.93% | 31.01 |
| 99 | 28.90 | 96.04% | 31.29 |
| 100 | 29.65 | 96.41% | 32.15 |
| 101 | 25.06 | 91.29% | 24.68 |
