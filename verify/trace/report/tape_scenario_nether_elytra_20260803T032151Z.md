# Tape replay: scenario_nether_elytra_20260803T032151Z

368 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 65, field `hp`** oracle=10.166667 magma=10.333334 |d|=0.167; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=99 independent=98 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=5 ghost_ticks=99 mismatches=0 verified=True available=True pass=True
- world hash: mode=java compared=99 anchor_skips=0 mismatches=0 deltas=3 verified=True available=True pass=True

**Pixel gate: FAIL** over 99 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 47 | 11965479 | 182275 |
| bossbar | 4 | 153720 | 38430 |
| hud | 40 | 26856 | 1597 |
| particles | 17 | 7078 | 632 |
| viewmodel | 37 | 16426 | 3120 |

Failed frames (worst first, top 20):

- t=98: 304044 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 70682, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [402, 462, 419, 479], 'soak_from': 'transit'}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [456, 453, 464, 479], 'soak_from': 'transit'}]
- t=90: 276231 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=96: 276087 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2008, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=93: 275995 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1912, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=79: 275647 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1564, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=67: 275375 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1292, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=70: 275375 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1292, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=73: 275371 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1292, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=76: 275371 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1292, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=91: 275183 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=60: 275047 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 964, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=63: 275043 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 964, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=88: 274871 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=89: 274567 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=92: 274551 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 292, 421, 325], 'soak_from': 'transit'}]
- t=95: 274443 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 294, 407, 307], 'soak_from': 'transit'}]
- t=97: 274391 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 308, 421, 341], 'soak_from': 'transit'}]
- t=94: 274321 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=59: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=65: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.56 | 87.55% | 1.60 |
| 1 | 1.37 | 86.83% | 1.53 |
| 2 | 1.18 | 84.82% | 1.28 |
| 3 | 1.18 | 84.82% | 1.28 |
| 4 | 1.18 | 84.82% | 1.28 |
| 5 | 1.18 | 84.82% | 1.28 |
| 6 | 1.18 | 84.83% | 1.28 |
| 7 | 1.18 | 84.83% | 1.28 |
| 8 | 1.18 | 84.83% | 1.28 |
| 9 | 1.18 | 84.83% | 1.28 |
| 10 | 1.17 | 85.52% | 1.26 |
| 11 | 1.17 | 86.27% | 1.26 |
| 12 | 1.18 | 87.54% | 1.26 |
| 13 | 1.20 | 89.95% | 1.26 |
| 14 | 1.21 | 91.01% | 1.26 |
| 15 | 1.20 | 90.99% | 1.26 |
| 16 | 1.21 | 90.90% | 1.27 |
| 17 | 1.20 | 90.53% | 1.26 |
| 18 | 1.20 | 90.17% | 1.26 |
| 19 | 1.20 | 89.87% | 1.25 |
| 20 | 1.17 | 89.67% | 1.25 |
| 21 | 1.17 | 89.57% | 1.24 |
| 22 | 1.17 | 89.53% | 1.25 |
| 23 | 1.16 | 89.56% | 1.24 |
| 24 | 1.16 | 89.68% | 1.25 |
| 25 | 1.15 | 89.73% | 1.24 |
| 26 | 1.15 | 89.98% | 1.24 |
| 27 | 1.15 | 90.36% | 1.24 |
| 28 | 1.16 | 90.79% | 1.23 |
| 29 | 1.16 | 91.21% | 1.23 |
| 30 | 1.16 | 91.54% | 1.22 |
| 31 | 1.15 | 91.68% | 1.22 |
| 32 | 1.14 | 92.04% | 1.24 |
| 33 | 1.14 | 92.41% | 1.25 |
| 34 | 1.13 | 92.74% | 1.27 |
| 35 | 1.13 | 92.94% | 1.29 |
| 36 | 1.15 | 93.03% | 1.30 |
| 37 | 1.14 | 93.09% | 1.30 |
| 38 | 1.15 | 93.15% | 1.30 |
| 39 | 1.15 | 93.13% | 1.30 |
| 40 | 1.16 | 93.12% | 1.30 |
| 41 | 1.16 | 93.10% | 1.30 |
| 42 | 1.18 | 93.06% | 1.31 |
| 43 | 1.78 | 95.80% | 1.94 |
| 44 | 1.95 | 95.73% | 2.05 |
| 45 | 2.05 | 95.61% | 2.11 |
| 46 | 1.70 | 92.73% | 1.89 |
| 47 | 1.61 | 94.95% | 1.78 |
| 48 | 1.73 | 94.24% | 1.94 |
| 49 | 1.63 | 93.53% | 1.77 |
| 50 | 1.38 | 95.19% | 1.51 |
| 51 | 1.90 | 83.50% | 2.09 |
| 52 | 4.26 | 92.85% | 4.64 |
| 53 | 5.90 | 93.13% | 6.36 |
| 54 | 7.01 | 92.72% | 7.55 |
| 55 | 9.60 | 94.19% | 10.28 |
| 56 | 10.00 | 93.94% | 10.73 |
| 57 | 10.79 | 94.30% | 11.55 |
| 58 | 10.76 | 93.83% | 11.53 |
| 59 | 11.15 | 93.68% | 11.94 |
| 60 | 11.71 | 94.00% | 12.53 |
| 61 | 11.44 | 93.73% | 12.25 |
| 62 | 11.65 | 93.95% | 12.46 |
| 63 | 12.26 | 94.12% | 13.08 |
| 64 | 11.76 | 93.89% | 12.55 |
| 65 | 11.81 | 94.72% | 12.61 |
| 66 | 12.04 | 94.64% | 12.86 |
| 67 | 12.63 | 94.69% | 13.49 |
| 68 | 11.97 | 94.15% | 12.77 |
| 69 | 11.90 | 93.86% | 12.70 |
| 70 | 12.79 | 94.11% | 13.67 |
| 71 | 12.37 | 93.77% | 13.20 |
| 72 | 12.16 | 93.76% | 12.97 |
| 73 | 12.81 | 94.08% | 13.69 |
| 74 | 12.16 | 93.76% | 12.98 |
| 75 | 11.94 | 93.77% | 12.74 |
| 76 | 13.05 | 94.08% | 13.94 |
| 77 | 12.64 | 93.76% | 13.47 |
| 78 | 12.64 | 93.76% | 13.47 |
| 79 | 13.65 | 94.96% | 14.60 |
| 80 | 12.89 | 94.50% | 13.75 |
| 81 | 13.11 | 94.24% | 13.98 |
| 82 | 13.10 | 93.99% | 13.97 |
| 83 | 13.09 | 93.86% | 13.96 |
| 84 | 13.09 | 93.80% | 13.95 |
| 85 | 13.32 | 93.77% | 14.20 |
| 86 | 13.32 | 93.76% | 14.19 |
| 87 | 13.32 | 93.76% | 14.19 |
| 88 | 13.77 | 94.73% | 14.63 |
| 89 | 13.90 | 94.58% | 14.80 |
| 90 | 14.67 | 94.71% | 15.65 |
| 91 | 14.05 | 94.20% | 14.92 |
| 92 | 13.89 | 93.94% | 14.78 |
| 93 | 14.86 | 94.26% | 15.91 |
| 94 | 14.05 | 93.81% | 14.97 |
| 95 | 14.07 | 93.82% | 14.98 |
| 96 | 14.86 | 94.21% | 15.86 |
| 97 | 14.07 | 93.82% | 14.99 |
| 98 | 12.53 | 91.38% | 10.20 |
