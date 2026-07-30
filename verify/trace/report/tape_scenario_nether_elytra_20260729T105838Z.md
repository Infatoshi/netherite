# Tape replay: scenario_nether_elytra_20260729T105838Z

347 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 42, field `x`** oracle=-75.59642351025522 magma=-75.69105018549888 |d|=0.0946; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 16.9126 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=105 independent=104 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 available=True
- world nearby_hash: checked=6 deltas=5 available=True

**Pixel gate: FAIL** over 105 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 63 | 13610016 | 182275 |
| bossbar | 22 | 495378 | 30852 |
| hud | 60 | 247975 | 51047 |
| particles | 14 | 73292 | 20355 |
| viewmodel | 33 | 87483 | 22110 |

Failed frames (worst first, top 20):

- t=95: 276127 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2044, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=86: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=97: 274867 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 293], 'soak_from': 'transit'}]
- t=99: 274863 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 468, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 309], 'soak_from': 'transit'}]
- t=102: 274831 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=101: 274829 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 341], 'soak_from': 'transit'}]
- t=103: 274551 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=96: 274447 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 294, 407, 307], 'soak_from': 'transit'}]
- t=98: 274395 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 308, 421, 341], 'soak_from': 'transit'}]
- t=100: 274351 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=87: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=88: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=90: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=91: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=92: 274081 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51207, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=93: 274079 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=94: 274079 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=77: 272689 unexplained px, clusters [{'px': 35270, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 795], 'soak_from': 'transit'}, {'px': 2540, 'cls': 'UNEXPLAINED', 'bbox': [0, 790, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}]
- t=82: 272632 unexplained px, clusters [{'px': 38227, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50718, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.58 | 87.55% | 1.64 |
| 1 | 1.39 | 86.83% | 1.57 |
| 2 | 1.20 | 84.82% | 1.32 |
| 3 | 1.20 | 84.82% | 1.32 |
| 4 | 1.20 | 84.82% | 1.32 |
| 5 | 1.20 | 84.83% | 1.32 |
| 6 | 1.20 | 84.83% | 1.32 |
| 7 | 1.20 | 84.83% | 1.32 |
| 8 | 1.20 | 84.83% | 1.32 |
| 9 | 1.20 | 84.83% | 1.32 |
| 10 | 1.21 | 85.52% | 1.33 |
| 11 | 1.22 | 86.27% | 1.33 |
| 12 | 1.23 | 87.55% | 1.33 |
| 13 | 1.25 | 89.94% | 1.35 |
| 14 | 1.26 | 91.01% | 1.34 |
| 15 | 1.25 | 90.99% | 1.34 |
| 16 | 1.26 | 90.90% | 1.36 |
| 17 | 1.25 | 90.52% | 1.34 |
| 18 | 1.25 | 90.16% | 1.34 |
| 19 | 1.22 | 89.87% | 1.32 |
| 20 | 1.22 | 89.67% | 1.32 |
| 21 | 1.22 | 89.57% | 1.32 |
| 22 | 1.21 | 89.52% | 1.32 |
| 23 | 1.21 | 89.56% | 1.32 |
| 24 | 1.21 | 89.55% | 1.32 |
| 25 | 1.21 | 89.73% | 1.32 |
| 26 | 1.21 | 89.98% | 1.33 |
| 27 | 1.22 | 90.36% | 1.33 |
| 28 | 1.22 | 90.79% | 1.33 |
| 29 | 1.23 | 91.22% | 1.34 |
| 30 | 1.24 | 91.55% | 1.35 |
| 31 | 1.24 | 91.69% | 1.36 |
| 32 | 1.24 | 92.05% | 1.39 |
| 33 | 1.24 | 92.43% | 1.41 |
| 34 | 1.24 | 92.70% | 1.44 |
| 35 | 1.24 | 92.95% | 1.45 |
| 36 | 1.26 | 93.05% | 1.47 |
| 37 | 1.26 | 93.11% | 1.47 |
| 38 | 1.27 | 93.18% | 1.48 |
| 39 | 1.28 | 93.16% | 1.48 |
| 40 | 1.28 | 93.16% | 1.48 |
| 41 | 1.29 | 93.11% | 1.49 |
| 42 | 5.67 | 96.45% | 5.94 |
| 43 | 6.17 | 96.36% | 6.52 |
| 44 | 6.20 | 96.37% | 6.48 |
| 45 | 6.86 | 96.18% | 7.29 |
| 46 | 7.84 | 96.28% | 8.63 |
| 47 | 10.13 | 96.42% | 11.43 |
| 48 | 13.54 | 96.31% | 15.39 |
| 49 | 22.45 | 96.32% | 26.03 |
| 50 | 27.13 | 95.77% | 31.74 |
| 51 | 20.30 | 94.13% | 25.67 |
| 52 | 18.45 | 96.19% | 19.73 |
| 53 | 18.72 | 96.20% | 20.24 |
| 54 | 19.62 | 96.19% | 21.15 |
| 55 | 20.32 | 96.21% | 21.87 |
| 56 | 21.17 | 96.22% | 22.79 |
| 57 | 21.75 | 96.15% | 23.41 |
| 58 | 22.55 | 96.18% | 24.21 |
| 59 | 23.59 | 96.08% | 25.72 |
| 60 | 24.38 | 96.15% | 26.60 |
| 61 | 25.50 | 96.40% | 27.81 |
| 62 | 26.27 | 96.15% | 27.93 |
| 63 | 25.52 | 94.52% | 27.99 |
| 64 | 26.83 | 94.59% | 29.52 |
| 65 | 27.10 | 94.25% | 29.95 |
| 66 | 28.08 | 94.23% | 31.34 |
| 67 | 29.35 | 95.41% | 33.14 |
| 68 | 28.97 | 95.13% | 32.78 |
| 69 | 29.91 | 95.21% | 33.84 |
| 70 | 29.66 | 94.71% | 33.58 |
| 71 | 29.50 | 94.49% | 33.29 |
| 72 | 30.43 | 94.73% | 34.26 |
| 73 | 30.06 | 94.36% | 33.92 |
| 74 | 30.36 | 94.28% | 34.20 |
| 75 | 31.40 | 94.49% | 35.21 |
| 76 | 31.30 | 94.23% | 34.92 |
| 77 | 32.42 | 95.39% | 36.79 |
| 78 | 31.95 | 95.00% | 36.18 |
| 79 | 31.98 | 94.85% | 35.97 |
| 80 | 32.21 | 94.57% | 36.11 |
| 81 | 32.32 | 94.49% | 36.69 |
| 82 | 32.16 | 94.18% | 36.24 |
| 83 | 8.19 | 93.44% | 9.12 |
| 84 | 9.31 | 93.68% | 10.37 |
| 85 | 9.20 | 92.62% | 10.40 |
| 86 | 11.47 | 94.40% | 12.69 |
| 87 | 12.57 | 94.01% | 13.11 |
| 88 | 13.40 | 93.65% | 12.51 |
| 89 | 12.36 | 93.32% | 12.19 |
| 90 | 12.53 | 93.37% | 12.03 |
| 91 | 12.30 | 93.31% | 11.46 |
| 92 | 11.99 | 93.28% | 11.43 |
| 93 | 11.35 | 93.27% | 11.09 |
| 94 | 11.39 | 93.27% | 11.41 |
| 95 | 12.66 | 94.58% | 12.92 |
| 96 | 11.53 | 94.08% | 11.76 |
| 97 | 11.67 | 93.90% | 11.95 |
| 98 | 11.66 | 93.67% | 12.01 |
| 99 | 11.64 | 93.58% | 12.01 |
| 100 | 11.67 | 93.40% | 12.12 |
| 101 | 11.90 | 93.44% | 12.42 |
| 102 | 11.82 | 93.42% | 12.39 |
| 103 | 11.96 | 93.37% | 12.58 |
| 104 | 11.28 | 91.38% | 8.90 |
