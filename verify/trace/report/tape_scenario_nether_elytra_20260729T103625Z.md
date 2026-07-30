# Tape replay: scenario_nether_elytra_20260729T103625Z

354 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 8.1916 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=128 independent=127 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=7 available=True
- world nearby_hash: checked=7 deltas=6 available=True

**Pixel gate: FAIL** over 128 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 85 | 14656377 | 182044 |
| bossbar | 45 | 389561 | 38404 |
| hud | 50 | 1118422 | 42829 |
| particles | 8 | 93313 | 34802 |
| transit | 3 | 35455 | 7951 |
| viewmodel | 30 | 233675 | 34590 |

Failed frames (worst first, top 20):

- t=116: 275176 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=113: 274660 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=110: 274367 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=117: 273656 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51013, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=106: 273562 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=115: 273308 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50826, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=114: 273108 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50697, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=103: 273041 unexplained px, clusters [{'px': 38427, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=112: 272794 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50592, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=100: 272739 unexplained px, clusters [{'px': 38404, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=111: 272654 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50501, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=94: 272496 unexplained px, clusters [{'px': 38395, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=109: 272496 unexplained px, clusters [{'px': 38421, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50434, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=108: 272447 unexplained px, clusters [{'px': 38406, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50505, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=97: 272320 unexplained px, clusters [{'px': 38405, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=107: 272230 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50299, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=105: 271950 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50163, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=91: 271949 unexplained px, clusters [{'px': 38377, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=104: 271687 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50070, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=102: 271457 unexplained px, clusters [{'px': 38422, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 49951, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

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
| 11 | 1.19 | 86.29% | 1.28 |
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
| 41 | 4.15 | 93.63% | 4.15 |
| 42 | 5.15 | 93.63% | 5.22 |
| 43 | 6.07 | 96.44% | 6.33 |
| 44 | 6.89 | 96.43% | 7.32 |
| 45 | 7.99 | 96.37% | 8.55 |
| 46 | 9.03 | 96.36% | 9.64 |
| 47 | 9.75 | 96.31% | 10.41 |
| 48 | 10.48 | 96.24% | 11.25 |
| 49 | 11.14 | 96.20% | 12.06 |
| 50 | 11.75 | 96.30% | 12.76 |
| 51 | 12.62 | 96.25% | 13.74 |
| 52 | 12.96 | 96.20% | 14.40 |
| 53 | 13.92 | 96.19% | 15.35 |
| 54 | 14.28 | 95.97% | 15.84 |
| 55 | 14.84 | 96.01% | 16.58 |
| 56 | 15.92 | 95.94% | 17.77 |
| 57 | 16.35 | 96.06% | 18.53 |
| 58 | 17.08 | 96.16% | 19.43 |
| 59 | 17.96 | 96.23% | 20.31 |
| 60 | 19.27 | 96.32% | 22.02 |
| 61 | 21.58 | 94.10% | 22.27 |
| 62 | 20.41 | 96.41% | 23.43 |
| 63 | 18.25 | 96.49% | 20.42 |
| 64 | 18.22 | 96.50% | 21.12 |
| 65 | 18.28 | 96.51% | 21.27 |
| 66 | 18.61 | 96.50% | 21.38 |
| 67 | 19.50 | 96.52% | 22.25 |
| 68 | 20.78 | 96.48% | 23.32 |
| 69 | 20.42 | 59.67% | 23.12 |
| 70 | 20.68 | 59.36% | 23.60 |
| 71 | 20.94 | 96.37% | 24.23 |
| 72 | 21.05 | 96.37% | 24.46 |
| 73 | 21.88 | 96.41% | 25.40 |
| 74 | 21.92 | 96.40% | 25.30 |
| 75 | 21.84 | 96.43% | 25.76 |
| 76 | 22.58 | 96.42% | 26.39 |
| 77 | 14.69 | 94.96% | 18.26 |
| 78 | 14.91 | 95.14% | 18.63 |
| 79 | 24.62 | 95.99% | 25.86 |
| 80 | 30.68 | 95.94% | 30.92 |
| 81 | 29.06 | 95.92% | 29.54 |
| 82 | 27.91 | 95.60% | 28.45 |
| 83 | 26.31 | 95.66% | 27.02 |
| 84 | 25.08 | 95.90% | 25.60 |
| 85 | 27.32 | 94.69% | 29.45 |
| 86 | 28.66 | 94.43% | 30.35 |
| 87 | 29.39 | 96.25% | 31.15 |
| 88 | 29.15 | 96.01% | 30.97 |
| 89 | 29.62 | 96.05% | 31.54 |
| 90 | 29.46 | 96.02% | 31.37 |
| 91 | 30.08 | 96.31% | 32.03 |
| 92 | 29.58 | 96.02% | 31.51 |
| 93 | 29.75 | 96.02% | 31.68 |
| 94 | 30.55 | 96.32% | 32.56 |
| 95 | 29.94 | 96.04% | 31.97 |
| 96 | 30.11 | 96.05% | 32.16 |
| 97 | 30.66 | 94.70% | 32.81 |
| 98 | 30.58 | 95.10% | 32.79 |
| 99 | 30.46 | 95.02% | 32.68 |
| 100 | 31.14 | 95.17% | 33.43 |
| 101 | 30.45 | 94.67% | 32.67 |
| 102 | 30.42 | 94.53% | 32.69 |
| 103 | 31.20 | 94.82% | 33.59 |
| 104 | 30.74 | 94.46% | 33.12 |
| 105 | 30.78 | 94.45% | 33.26 |
| 106 | 31.63 | 94.78% | 34.26 |
| 107 | 31.02 | 94.43% | 33.63 |
| 108 | 31.54 | 95.09% | 34.36 |
| 109 | 31.46 | 95.06% | 34.20 |
| 110 | 32.26 | 95.25% | 35.12 |
| 111 | 31.37 | 94.50% | 34.15 |
| 112 | 31.45 | 94.38% | 34.28 |
| 113 | 32.40 | 94.68% | 35.40 |
| 114 | 31.78 | 94.33% | 34.93 |
| 115 | 31.85 | 94.32% | 35.06 |
| 116 | 32.59 | 94.69% | 36.32 |
| 117 | 31.88 | 94.30% | 35.51 |
| 118 | 3.40 | 69.33% | 4.18 |
| 119 | 3.76 | 94.10% | 4.62 |
| 120 | 4.02 | 80.66% | 4.95 |
| 121 | 7.09 | 80.22% | 7.12 |
| 122 | 7.78 | 83.34% | 7.83 |
| 123 | 7.93 | 84.53% | 6.90 |
| 124 | 7.63 | 86.51% | 7.09 |
| 125 | 7.97 | 92.76% | 7.21 |
| 126 | 7.86 | 93.05% | 6.69 |
| 127 | 22.35 | 91.38% | 22.77 |
