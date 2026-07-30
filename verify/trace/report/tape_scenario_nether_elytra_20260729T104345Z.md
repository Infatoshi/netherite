# Tape replay: scenario_nether_elytra_20260729T104345Z

365 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 103, field `x`** oracle=-80.23753369532902 magma=-80.23756611466783 |d|=3.24e-05; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0011 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=111 independent=110 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 available=True
- world nearby_hash: checked=6 deltas=5 available=True

**Pixel gate: FAIL** over 111 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 56 | 14322944 | 182275 |
| bossbar | 14 | 538020 | 38430 |
| hud | 16 | 34647 | 1597 |
| viewmodel | 16 | 7568 | 3120 |

Failed frames (worst first, top 20):

- t=110: 327939 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 71216, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [402, 462, 419, 479], 'soak_from': 'transit'}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [456, 453, 464, 479], 'soak_from': 'transit'}]
- t=100: 276291 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2208, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=91: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=83: 275511 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=86: 275511 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=89: 275507 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1428, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=71: 275239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=73: 275239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=76: 275239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=79: 275235 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=103: 274867 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 293], 'soak_from': 'transit'}]
- t=109: 274863 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=101: 274835 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=104: 274707 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 468, 'cls': 'UNEXPLAINED', 'bbox': [402, 292, 421, 341], 'soak_from': 'transit'}]
- t=106: 274561 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 176, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 323], 'soak_from': 'transit'}]
- t=107: 274559 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 236, 'cls': 'UNEXPLAINED', 'bbox': [402, 310, 407, 371], 'soak_from': 'transit'}]
- t=102: 274555 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=105: 274471 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=108: 274391 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 292, 421, 309], 'soak_from': 'transit'}]
- t=72: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

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
| 9 | 1.18 | 85.53% | 1.27 |
| 10 | 1.19 | 86.28% | 1.28 |
| 11 | 1.20 | 87.57% | 1.28 |
| 12 | 1.22 | 90.01% | 1.28 |
| 13 | 1.22 | 91.11% | 1.28 |
| 14 | 1.22 | 91.09% | 1.28 |
| 15 | 1.23 | 91.02% | 1.30 |
| 16 | 1.21 | 91.00% | 1.28 |
| 17 | 1.21 | 91.00% | 1.28 |
| 18 | 1.21 | 90.96% | 1.28 |
| 19 | 1.20 | 90.53% | 1.28 |
| 20 | 1.21 | 90.28% | 1.29 |
| 21 | 1.21 | 90.01% | 1.29 |
| 22 | 1.20 | 89.80% | 1.29 |
| 23 | 1.18 | 89.70% | 1.29 |
| 24 | 1.18 | 89.69% | 1.30 |
| 25 | 1.18 | 89.73% | 1.30 |
| 26 | 1.18 | 89.87% | 1.29 |
| 27 | 1.18 | 89.93% | 1.30 |
| 28 | 1.18 | 90.23% | 1.29 |
| 29 | 1.18 | 90.60% | 1.28 |
| 30 | 1.19 | 90.96% | 1.28 |
| 31 | 1.18 | 91.31% | 1.27 |
| 32 | 1.19 | 91.56% | 1.26 |
| 33 | 1.18 | 91.78% | 1.26 |
| 34 | 1.17 | 92.26% | 1.27 |
| 35 | 1.16 | 92.51% | 1.28 |
| 36 | 1.15 | 92.81% | 1.30 |
| 37 | 1.17 | 92.99% | 1.31 |
| 38 | 1.18 | 93.10% | 1.32 |
| 39 | 1.21 | 93.16% | 1.33 |
| 40 | 1.19 | 93.20% | 1.34 |
| 41 | 1.19 | 93.24% | 1.33 |
| 42 | 1.20 | 93.20% | 1.35 |
| 43 | 1.21 | 93.20% | 1.34 |
| 44 | 1.23 | 93.18% | 1.35 |
| 45 | 1.77 | 95.98% | 1.90 |
| 46 | 1.95 | 95.92% | 2.03 |
| 47 | 2.09 | 95.89% | 2.12 |
| 48 | 1.70 | 92.92% | 1.86 |
| 49 | 1.64 | 95.20% | 1.79 |
| 50 | 1.65 | 94.48% | 1.78 |
| 51 | 1.59 | 93.62% | 1.70 |
| 52 | 1.38 | 95.43% | 1.48 |
| 53 | 1.93 | 82.92% | 2.08 |
| 54 | 4.26 | 92.85% | 4.64 |
| 55 | 5.90 | 93.13% | 6.36 |
| 56 | 7.01 | 92.72% | 7.55 |
| 57 | 9.60 | 94.19% | 10.28 |
| 58 | 10.00 | 93.94% | 10.73 |
| 59 | 10.79 | 94.30% | 11.55 |
| 60 | 10.76 | 93.83% | 11.53 |
| 61 | 11.15 | 93.68% | 11.94 |
| 62 | 11.71 | 94.00% | 12.53 |
| 63 | 11.44 | 93.73% | 12.25 |
| 64 | 11.65 | 93.95% | 12.46 |
| 65 | 12.26 | 94.12% | 13.08 |
| 66 | 11.75 | 93.89% | 12.55 |
| 67 | 11.77 | 93.90% | 12.56 |
| 68 | 12.53 | 94.14% | 13.37 |
| 69 | 11.96 | 93.90% | 12.75 |
| 70 | 11.96 | 93.92% | 12.76 |
| 71 | 12.83 | 95.03% | 13.70 |
| 72 | 12.38 | 94.50% | 13.21 |
| 73 | 12.99 | 94.52% | 13.87 |
| 74 | 12.17 | 94.00% | 12.98 |
| 75 | 12.39 | 93.86% | 13.22 |
| 76 | 12.99 | 94.08% | 13.87 |
| 77 | 12.39 | 93.77% | 13.22 |
| 78 | 12.85 | 93.76% | 13.70 |
| 79 | 13.68 | 94.04% | 14.60 |
| 80 | 13.08 | 93.76% | 13.95 |
| 81 | 13.34 | 94.58% | 14.23 |
| 82 | 13.33 | 94.50% | 14.22 |
| 83 | 14.26 | 94.58% | 15.23 |
| 84 | 13.55 | 93.99% | 14.44 |
| 85 | 13.54 | 93.86% | 14.43 |
| 86 | 14.24 | 94.15% | 15.21 |
| 87 | 13.76 | 93.77% | 14.67 |
| 88 | 13.76 | 93.76% | 14.67 |
| 89 | 14.47 | 94.11% | 15.45 |
| 90 | 13.98 | 93.76% | 14.91 |
| 91 | 15.07 | 95.00% | 16.12 |
| 92 | 14.26 | 94.50% | 15.21 |
| 93 | 14.46 | 94.24% | 15.42 |
| 94 | 14.46 | 93.99% | 15.42 |
| 95 | 14.67 | 93.86% | 15.64 |
| 96 | 14.67 | 93.80% | 15.64 |
| 97 | 14.67 | 93.77% | 15.64 |
| 98 | 14.67 | 93.76% | 15.64 |
| 99 | 14.88 | 93.76% | 15.87 |
| 100 | 15.82 | 95.07% | 16.88 |
| 101 | 15.06 | 94.63% | 16.03 |
| 102 | 15.27 | 94.32% | 16.25 |
| 103 | 15.60 | 94.41% | 16.60 |
| 104 | 15.56 | 94.20% | 16.56 |
| 105 | 15.51 | 94.06% | 16.52 |
| 106 | 15.73 | 94.03% | 16.75 |
| 107 | 15.73 | 94.02% | 16.75 |
| 108 | 15.71 | 93.99% | 16.73 |
| 109 | 15.83 | 94.07% | 16.83 |
| 110 | 13.05 | 91.38% | 10.70 |
