# Tape replay: scenario_nether_elytra_20260729T105654Z

351 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 42, field `x`** oracle=-75.59642351025522 magma=-75.69105018549888 |d|=0.0946; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 18.3347 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=105 independent=104 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 available=True
- world nearby_hash: checked=6 deltas=5 available=True

**Pixel gate: FAIL** over 105 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 62 | 13562104 | 182275 |
| bossbar | 22 | 495442 | 30910 |
| hud | 60 | 250308 | 51047 |
| particles | 15 | 97374 | 23690 |
| transit | 1 | 23432 | 18524 |
| viewmodel | 33 | 88836 | 22383 |

Failed frames (worst first, top 20):

- t=97: 276291 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2208, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=100: 276227 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2144, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=103: 276187 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2108, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=88: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=91: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=94: 275779 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=98: 275027 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=95: 274863 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 468, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 309], 'soak_from': 'transit'}]
- t=101: 274561 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 176, 'cls': 'UNEXPLAINED', 'bbox': [402, 294, 407, 339], 'soak_from': 'transit'}]
- t=99: 274555 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=102: 274467 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=96: 274351 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=78: 274217 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=86: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=87: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=90: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=92: 274081 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51207, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=85: 274079 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=93: 274079 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51205, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.52 | 87.02% | 1.56 |
| 1 | 1.33 | 86.29% | 1.50 |
| 2 | 1.14 | 84.29% | 1.24 |
| 3 | 1.14 | 84.29% | 1.24 |
| 4 | 1.14 | 84.29% | 1.24 |
| 5 | 1.14 | 84.29% | 1.24 |
| 6 | 1.14 | 84.29% | 1.24 |
| 7 | 1.14 | 84.29% | 1.24 |
| 8 | 1.14 | 84.29% | 1.24 |
| 9 | 1.14 | 84.30% | 1.24 |
| 10 | 1.15 | 84.96% | 1.26 |
| 11 | 1.16 | 85.70% | 1.26 |
| 12 | 1.17 | 86.95% | 1.26 |
| 13 | 1.18 | 89.34% | 1.27 |
| 14 | 1.19 | 90.40% | 1.27 |
| 15 | 1.18 | 90.34% | 1.27 |
| 16 | 1.19 | 90.21% | 1.28 |
| 17 | 1.17 | 89.78% | 1.25 |
| 18 | 1.17 | 89.37% | 1.24 |
| 19 | 1.15 | 89.07% | 1.22 |
| 20 | 1.14 | 88.83% | 1.21 |
| 21 | 1.14 | 88.70% | 1.20 |
| 22 | 1.13 | 88.62% | 1.20 |
| 23 | 1.13 | 88.61% | 1.20 |
| 24 | 1.12 | 88.59% | 1.20 |
| 25 | 1.12 | 88.71% | 1.20 |
| 26 | 1.11 | 88.97% | 1.22 |
| 27 | 1.11 | 89.34% | 1.23 |
| 28 | 1.12 | 89.77% | 1.26 |
| 29 | 1.13 | 90.24% | 1.28 |
| 30 | 1.14 | 90.80% | 1.32 |
| 31 | 1.16 | 91.17% | 1.36 |
| 32 | 1.18 | 91.75% | 1.39 |
| 33 | 1.20 | 92.23% | 1.42 |
| 34 | 1.23 | 92.63% | 1.44 |
| 35 | 1.25 | 92.96% | 1.46 |
| 36 | 1.26 | 93.06% | 1.47 |
| 37 | 1.26 | 93.11% | 1.48 |
| 38 | 1.27 | 93.18% | 1.48 |
| 39 | 1.28 | 93.16% | 1.49 |
| 40 | 1.29 | 93.15% | 1.49 |
| 41 | 1.29 | 93.11% | 1.49 |
| 42 | 5.66 | 96.45% | 5.93 |
| 43 | 6.18 | 96.36% | 6.53 |
| 44 | 6.20 | 96.36% | 6.47 |
| 45 | 6.88 | 96.18% | 7.31 |
| 46 | 7.86 | 96.28% | 8.67 |
| 47 | 10.16 | 96.42% | 11.47 |
| 48 | 13.57 | 96.31% | 15.45 |
| 49 | 22.49 | 96.32% | 26.10 |
| 50 | 27.17 | 95.77% | 31.81 |
| 51 | 20.34 | 94.13% | 25.75 |
| 52 | 18.45 | 96.19% | 19.73 |
| 53 | 18.72 | 96.20% | 20.24 |
| 54 | 19.62 | 96.19% | 21.15 |
| 55 | 20.32 | 96.21% | 21.87 |
| 56 | 21.17 | 96.22% | 22.79 |
| 57 | 21.75 | 96.15% | 23.41 |
| 58 | 22.55 | 96.18% | 24.21 |
| 59 | 23.60 | 96.08% | 25.73 |
| 60 | 24.44 | 96.15% | 26.68 |
| 61 | 25.49 | 96.40% | 27.91 |
| 62 | 26.00 | 96.17% | 27.66 |
| 63 | 25.56 | 94.48% | 28.05 |
| 64 | 26.90 | 94.56% | 29.62 |
| 65 | 27.18 | 94.23% | 30.08 |
| 66 | 28.22 | 94.27% | 31.52 |
| 67 | 30.48 | 94.56% | 33.96 |
| 68 | 30.55 | 95.19% | 34.52 |
| 69 | 30.48 | 95.07% | 34.25 |
| 70 | 31.33 | 95.04% | 35.00 |
| 71 | 30.76 | 94.52% | 34.18 |
| 72 | 31.00 | 94.36% | 34.47 |
| 73 | 31.69 | 94.66% | 35.38 |
| 74 | 31.29 | 94.38% | 35.04 |
| 75 | 31.57 | 94.45% | 35.30 |
| 76 | 32.71 | 94.66% | 36.37 |
| 77 | 32.14 | 94.31% | 35.92 |
| 78 | 33.27 | 95.55% | 36.86 |
| 79 | 32.59 | 94.95% | 36.24 |
| 80 | 32.93 | 94.77% | 36.86 |
| 81 | 33.75 | 94.44% | 37.73 |
| 82 | 9.27 | 93.78% | 10.49 |
| 83 | 10.71 | 93.07% | 12.24 |
| 84 | 7.95 | 92.63% | 8.83 |
| 85 | 14.85 | 92.90% | 14.02 |
| 86 | 13.13 | 94.09% | 12.57 |
| 87 | 12.53 | 94.01% | 11.75 |
| 88 | 12.51 | 94.06% | 12.01 |
| 89 | 11.35 | 93.32% | 11.12 |
| 90 | 11.06 | 93.37% | 10.99 |
| 91 | 11.87 | 93.73% | 11.79 |
| 92 | 11.20 | 93.28% | 11.11 |
| 93 | 11.13 | 93.27% | 11.08 |
| 94 | 12.19 | 93.69% | 12.31 |
| 95 | 11.70 | 94.25% | 11.87 |
| 96 | 11.36 | 94.06% | 11.60 |
| 97 | 12.42 | 94.24% | 12.90 |
| 98 | 11.60 | 93.79% | 12.06 |
| 99 | 11.64 | 93.52% | 12.16 |
| 100 | 12.39 | 93.83% | 13.02 |
| 101 | 11.74 | 93.39% | 12.32 |
| 102 | 11.92 | 93.35% | 12.55 |
| 103 | 12.72 | 93.76% | 13.47 |
| 104 | 11.53 | 91.38% | 9.18 |
