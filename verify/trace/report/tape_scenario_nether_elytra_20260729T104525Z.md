# Tape replay: scenario_nether_elytra_20260729T104525Z

353 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=78 independent=77 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=4 available=True
- world nearby_hash: checked=4 deltas=3 available=True

**Pixel gate: FAIL** over 78 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 25 | 6587506 | 182275 |
| hud | 1 | 183 | 66 |
| transit | 8 | 2231 | 310 |
| viewmodel | 16 | 4835 | 161 |

Failed frames (worst first, top 20):

- t=72: 276231 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=67: 276219 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2136, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=75: 276151 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2072, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=69: 275991 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1908, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=64: 275727 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=71: 275179 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 293], 'soak_from': 'transit'}]
- t=76: 275179 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=74: 274943 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 504, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 325], 'soak_from': 'transit'}]
- t=70: 274871 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=73: 274865 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 424, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 293], 'soak_from': 'transit'}]
- t=57: 274831 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=60: 274827 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 748, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=68: 274323 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 116, 'cls': 'UNEXPLAINED', 'bbox': [402, 310, 407, 339], 'soak_from': 'transit'}]
- t=55: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=56: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=62: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=63: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=65: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=66: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=58: 274081 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51207, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

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
| 17 | 1.21 | 91.00% | 1.28 |
| 18 | 1.21 | 90.68% | 1.28 |
| 19 | 1.21 | 90.28% | 1.29 |
| 20 | 1.22 | 90.08% | 1.29 |
| 21 | 1.21 | 89.86% | 1.28 |
| 22 | 1.21 | 89.76% | 1.29 |
| 23 | 1.18 | 89.78% | 1.28 |
| 24 | 1.18 | 89.80% | 1.29 |
| 25 | 1.18 | 89.92% | 1.29 |
| 26 | 1.18 | 90.15% | 1.29 |
| 27 | 1.18 | 90.42% | 1.29 |
| 28 | 1.19 | 90.67% | 1.29 |
| 29 | 1.19 | 91.01% | 1.27 |
| 30 | 1.18 | 91.39% | 1.27 |
| 31 | 1.18 | 91.63% | 1.26 |
| 32 | 1.18 | 91.78% | 1.26 |
| 33 | 1.17 | 92.14% | 1.27 |
| 34 | 1.17 | 92.48% | 1.28 |
| 35 | 1.16 | 92.76% | 1.30 |
| 36 | 1.16 | 92.97% | 1.30 |
| 37 | 1.18 | 93.06% | 1.34 |
| 38 | 1.17 | 93.15% | 1.32 |
| 39 | 1.18 | 93.23% | 1.33 |
| 40 | 1.21 | 93.23% | 1.35 |
| 41 | 1.19 | 93.19% | 1.33 |
| 42 | 1.20 | 93.22% | 1.34 |
| 43 | 1.23 | 93.20% | 1.35 |
| 44 | 1.80 | 96.00% | 1.96 |
| 45 | 1.98 | 95.93% | 2.09 |
| 46 | 2.12 | 95.89% | 2.21 |
| 47 | 1.72 | 92.89% | 1.91 |
| 48 | 1.69 | 95.14% | 1.86 |
| 49 | 1.67 | 94.45% | 1.82 |
| 50 | 1.63 | 93.69% | 1.78 |
| 51 | 1.41 | 95.19% | 1.51 |
| 52 | 3.09 | 92.96% | 3.42 |
| 53 | 6.61 | 93.85% | 7.10 |
| 54 | 9.02 | 93.79% | 9.71 |
| 55 | 9.10 | 93.34% | 9.76 |
| 56 | 9.57 | 93.47% | 10.25 |
| 57 | 10.22 | 93.43% | 10.93 |
| 58 | 10.02 | 93.60% | 10.74 |
| 59 | 10.52 | 93.60% | 11.28 |
| 60 | 11.37 | 93.77% | 12.16 |
| 61 | 10.95 | 93.72% | 11.73 |
| 62 | 11.47 | 94.55% | 12.29 |
| 63 | 11.68 | 94.69% | 12.49 |
| 64 | 12.76 | 94.76% | 13.65 |
| 65 | 11.99 | 94.12% | 12.79 |
| 66 | 12.22 | 94.00% | 13.05 |
| 67 | 13.58 | 95.19% | 14.48 |
| 68 | 13.00 | 94.68% | 13.85 |
| 69 | 13.80 | 94.85% | 14.76 |
| 70 | 13.35 | 94.30% | 14.19 |
| 71 | 13.36 | 94.23% | 14.19 |
| 72 | 14.20 | 94.44% | 15.16 |
| 73 | 13.50 | 94.07% | 14.37 |
| 74 | 13.51 | 94.08% | 14.38 |
| 75 | 14.44 | 94.39% | 15.41 |
| 76 | 13.53 | 93.97% | 14.37 |
| 77 | 12.24 | 91.38% | 9.95 |
