# Tape replay: scenario_nether_elytra_20260729T103004Z

357 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=79 independent=78 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=4 available=True
- world nearby_hash: checked=4 deltas=3 available=True

**Pixel gate: FAIL** over 79 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 24 | 6583361 | 182275 |
| hud | 1 | 183 | 66 |
| transit | 1 | 530 | 381 |
| viewmodel | 18 | 4942 | 161 |

Failed frames (worst first, top 20):

- t=77: 276295 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2216, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=71: 276235 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2152, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=74: 276231 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=65: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=68: 275727 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=72: 275027 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=73: 275023 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 472, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 293], 'soak_from': 'transit'}]
- t=76: 274963 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=58: 274831 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=61: 274827 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=70: 274727 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=75: 274725 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=69: 274323 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 326, 407, 339], 'soak_from': 'transit'}]
- t=56: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=57: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=63: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=64: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=66: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=67: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=59: 274081 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51207, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.48 | 87.03% | 1.50 |
| 1 | 1.29 | 86.30% | 1.44 |
| 2 | 1.10 | 84.30% | 1.18 |
| 3 | 1.10 | 84.30% | 1.18 |
| 4 | 1.10 | 84.30% | 1.18 |
| 5 | 1.10 | 84.30% | 1.18 |
| 6 | 1.10 | 84.30% | 1.18 |
| 7 | 1.10 | 84.30% | 1.18 |
| 8 | 1.10 | 84.30% | 1.18 |
| 9 | 1.10 | 84.30% | 1.18 |
| 10 | 1.11 | 84.97% | 1.19 |
| 11 | 1.12 | 85.71% | 1.20 |
| 12 | 1.13 | 86.97% | 1.20 |
| 13 | 1.14 | 89.39% | 1.20 |
| 14 | 1.15 | 90.48% | 1.19 |
| 15 | 1.14 | 90.43% | 1.20 |
| 16 | 1.15 | 90.32% | 1.21 |
| 17 | 1.13 | 90.24% | 1.19 |
| 18 | 1.12 | 90.22% | 1.19 |
| 19 | 1.12 | 89.72% | 1.19 |
| 20 | 1.13 | 89.45% | 1.18 |
| 21 | 1.12 | 89.12% | 1.17 |
| 22 | 1.12 | 88.89% | 1.17 |
| 23 | 1.09 | 88.73% | 1.16 |
| 24 | 1.09 | 88.66% | 1.17 |
| 25 | 1.08 | 88.68% | 1.17 |
| 26 | 1.08 | 88.81% | 1.17 |
| 27 | 1.07 | 89.00% | 1.18 |
| 28 | 1.07 | 89.29% | 1.18 |
| 29 | 1.07 | 89.53% | 1.19 |
| 30 | 1.07 | 89.86% | 1.20 |
| 31 | 1.08 | 90.36% | 1.22 |
| 32 | 1.09 | 90.89% | 1.23 |
| 33 | 1.10 | 91.24% | 1.26 |
| 34 | 1.11 | 91.95% | 1.27 |
| 35 | 1.13 | 92.30% | 1.29 |
| 36 | 1.15 | 92.73% | 1.29 |
| 37 | 1.16 | 92.97% | 1.31 |
| 38 | 1.17 | 93.10% | 1.32 |
| 39 | 1.19 | 93.17% | 1.33 |
| 40 | 1.19 | 93.22% | 1.33 |
| 41 | 1.20 | 93.23% | 1.35 |
| 42 | 1.21 | 93.20% | 1.36 |
| 43 | 1.22 | 93.22% | 1.35 |
| 44 | 1.22 | 93.18% | 1.34 |
| 45 | 1.76 | 96.00% | 1.90 |
| 46 | 1.97 | 95.94% | 2.06 |
| 47 | 2.11 | 95.90% | 2.16 |
| 48 | 1.74 | 92.91% | 1.92 |
| 49 | 1.68 | 95.18% | 1.84 |
| 50 | 1.71 | 94.45% | 1.89 |
| 51 | 1.58 | 93.66% | 1.70 |
| 52 | 1.36 | 95.22% | 1.44 |
| 53 | 3.07 | 92.96% | 3.41 |
| 54 | 6.60 | 93.85% | 7.10 |
| 55 | 9.00 | 93.77% | 9.67 |
| 56 | 9.09 | 93.34% | 9.76 |
| 57 | 9.57 | 93.47% | 10.25 |
| 58 | 10.22 | 93.43% | 10.93 |
| 59 | 10.02 | 93.60% | 10.74 |
| 60 | 10.52 | 93.60% | 11.28 |
| 61 | 11.37 | 93.77% | 12.16 |
| 62 | 10.95 | 93.72% | 11.73 |
| 63 | 11.25 | 94.54% | 12.05 |
| 64 | 11.46 | 94.68% | 12.26 |
| 65 | 12.33 | 94.78% | 13.20 |
| 66 | 11.54 | 94.12% | 12.32 |
| 67 | 11.78 | 94.00% | 12.57 |
| 68 | 13.02 | 94.34% | 13.94 |
| 69 | 12.26 | 94.76% | 13.06 |
| 70 | 12.33 | 94.78% | 13.12 |
| 71 | 13.33 | 94.87% | 14.21 |
| 72 | 13.11 | 94.33% | 13.93 |
| 73 | 13.11 | 94.20% | 13.93 |
| 74 | 14.01 | 94.43% | 14.92 |
| 75 | 13.46 | 94.05% | 14.34 |
| 76 | 13.44 | 93.92% | 14.30 |
| 77 | 14.17 | 94.24% | 15.12 |
| 78 | 13.17 | 91.38% | 10.19 |
