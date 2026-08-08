# Tape replay: scenario_nether_elytra_20260803T033526Z

351 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=110 independent=109 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 ghost_ticks=110 mismatches=0 verified=True available=True pass=True
- world hash: mode=java compared=110 anchor_skips=0 mismatches=0 deltas=5 verified=True available=True pass=True

**Pixel gate: FAIL** over 110 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 56 | 14543427 | 182275 |
| bossbar | 15 | 576450 | 38430 |
| hud | 52 | 56204 | 1597 |
| particles | 17 | 7097 | 632 |
| viewmodel | 36 | 14077 | 757 |

Failed frames (worst first, top 20):

- t=109: 332259 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 70604, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [384, 333, 384, 400], 'soak_from': 'transit'}, {'px': 252, 'cls': 'UNEXPLAINED', 'bbox': [395, 812, 400, 853], 'soak_from': 'transit'}]
- t=101: 276203 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2120, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=104: 276203 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2120, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=107: 276123 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2044, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=92: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=95: 275783 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=98: 275779 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1700, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=71: 275239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=74: 275239 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=77: 275235 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1156, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=99: 275023 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 940, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 341], 'soak_from': 'transit'}]
- t=102: 274867 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 293], 'soak_from': 'transit'}]
- t=108: 274863 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 277], 'soak_from': 'transit'}]
- t=100: 274835 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 148, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=103: 274707 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 468, 'cls': 'UNEXPLAINED', 'bbox': [402, 292, 421, 341], 'soak_from': 'transit'}]
- t=105: 274561 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 176, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 323], 'soak_from': 'transit'}]
- t=106: 274559 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 236, 'cls': 'UNEXPLAINED', 'bbox': [402, 310, 407, 371], 'soak_from': 'transit'}]
- t=70: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=72: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=73: 274083 unexplained px, clusters [{'px': 38430, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.53 | 87.56% | 1.56 |
| 1 | 1.34 | 86.83% | 1.50 |
| 2 | 1.15 | 84.83% | 1.24 |
| 3 | 1.15 | 84.83% | 1.24 |
| 4 | 1.15 | 84.83% | 1.24 |
| 5 | 1.15 | 84.83% | 1.24 |
| 6 | 1.15 | 84.83% | 1.24 |
| 7 | 1.15 | 84.83% | 1.24 |
| 8 | 1.15 | 84.83% | 1.24 |
| 9 | 1.15 | 84.83% | 1.24 |
| 10 | 1.17 | 85.52% | 1.25 |
| 11 | 1.17 | 86.27% | 1.26 |
| 12 | 1.18 | 87.55% | 1.25 |
| 13 | 1.20 | 89.95% | 1.26 |
| 14 | 1.20 | 91.02% | 1.25 |
| 15 | 1.20 | 90.99% | 1.25 |
| 16 | 1.20 | 90.90% | 1.26 |
| 17 | 1.19 | 90.52% | 1.25 |
| 18 | 1.19 | 90.15% | 1.25 |
| 19 | 1.17 | 89.87% | 1.24 |
| 20 | 1.16 | 89.66% | 1.24 |
| 21 | 1.16 | 89.56% | 1.23 |
| 22 | 1.16 | 89.52% | 1.23 |
| 23 | 1.16 | 89.55% | 1.23 |
| 24 | 1.15 | 89.67% | 1.24 |
| 25 | 1.15 | 89.72% | 1.23 |
| 26 | 1.15 | 89.96% | 1.23 |
| 27 | 1.15 | 90.35% | 1.22 |
| 28 | 1.15 | 90.77% | 1.22 |
| 29 | 1.15 | 91.20% | 1.22 |
| 30 | 1.15 | 91.53% | 1.21 |
| 31 | 1.15 | 91.67% | 1.21 |
| 32 | 1.14 | 92.03% | 1.23 |
| 33 | 1.13 | 92.41% | 1.25 |
| 34 | 1.13 | 92.74% | 1.27 |
| 35 | 1.13 | 92.94% | 1.28 |
| 36 | 1.14 | 93.03% | 1.29 |
| 37 | 1.14 | 93.09% | 1.29 |
| 38 | 1.14 | 93.15% | 1.30 |
| 39 | 1.15 | 93.13% | 1.30 |
| 40 | 1.15 | 93.12% | 1.30 |
| 41 | 1.16 | 93.10% | 1.29 |
| 42 | 1.17 | 93.06% | 1.30 |
| 43 | 1.75 | 95.81% | 1.88 |
| 44 | 1.91 | 95.73% | 2.00 |
| 45 | 2.03 | 95.60% | 2.09 |
| 46 | 1.71 | 92.76% | 1.89 |
| 47 | 1.60 | 94.95% | 1.74 |
| 48 | 1.61 | 94.23% | 1.73 |
| 49 | 1.53 | 93.59% | 1.64 |
| 50 | 1.36 | 95.21% | 1.47 |
| 51 | 1.86 | 83.53% | 2.01 |
| 52 | 4.06 | 92.85% | 4.40 |
| 53 | 5.61 | 92.73% | 6.05 |
| 54 | 6.56 | 92.72% | 7.07 |
| 55 | 8.18 | 93.82% | 8.77 |
| 56 | 9.34 | 93.95% | 10.02 |
| 57 | 10.57 | 94.30% | 11.31 |
| 58 | 10.75 | 93.83% | 11.53 |
| 59 | 11.37 | 93.68% | 12.17 |
| 60 | 12.14 | 94.00% | 12.99 |
| 61 | 11.88 | 93.73% | 12.72 |
| 62 | 12.09 | 93.95% | 12.93 |
| 63 | 13.22 | 94.28% | 14.09 |
| 64 | 12.71 | 94.04% | 13.57 |
| 65 | 12.66 | 93.90% | 13.51 |
| 66 | 13.43 | 94.14% | 14.33 |
| 67 | 13.13 | 93.90% | 14.01 |
| 68 | 13.13 | 93.92% | 14.01 |
| 69 | 13.80 | 95.03% | 14.73 |
| 70 | 13.13 | 94.66% | 14.01 |
| 71 | 13.96 | 94.68% | 14.90 |
| 72 | 13.35 | 94.15% | 14.25 |
| 73 | 13.35 | 94.02% | 14.25 |
| 74 | 13.88 | 94.08% | 14.82 |
| 75 | 13.06 | 93.77% | 13.93 |
| 76 | 13.51 | 93.77% | 14.41 |
| 77 | 14.35 | 94.05% | 15.31 |
| 78 | 13.75 | 93.76% | 14.66 |
| 79 | 13.97 | 93.76% | 14.90 |
| 80 | 14.01 | 94.58% | 14.94 |
| 81 | 14.23 | 94.50% | 15.17 |
| 82 | 14.22 | 94.24% | 15.16 |
| 83 | 14.21 | 93.99% | 15.15 |
| 84 | 14.21 | 93.86% | 15.14 |
| 85 | 14.43 | 93.80% | 15.38 |
| 86 | 14.43 | 93.77% | 15.38 |
| 87 | 14.43 | 93.76% | 15.38 |
| 88 | 14.65 | 93.76% | 15.61 |
| 89 | 14.89 | 93.76% | 15.88 |
| 90 | 14.93 | 94.58% | 15.92 |
| 91 | 14.93 | 94.50% | 15.91 |
| 92 | 15.72 | 94.65% | 16.81 |
| 93 | 15.20 | 94.15% | 16.21 |
| 94 | 15.19 | 94.02% | 16.20 |
| 95 | 15.99 | 94.38% | 17.10 |
| 96 | 15.19 | 93.93% | 16.19 |
| 97 | 15.41 | 93.92% | 16.43 |
| 98 | 16.22 | 94.34% | 17.34 |
| 99 | 15.66 | 94.93% | 16.65 |
| 100 | 15.83 | 94.80% | 16.84 |
| 101 | 16.81 | 94.89% | 17.96 |
| 102 | 16.05 | 94.30% | 17.07 |
| 103 | 16.01 | 94.14% | 17.04 |
| 104 | 16.79 | 94.58% | 17.94 |
| 105 | 16.19 | 94.08% | 17.23 |
| 106 | 16.43 | 94.04% | 17.51 |
| 107 | 17.29 | 94.42% | 18.48 |
| 108 | 16.78 | 94.08% | 17.85 |
| 109 | 13.56 | 91.38% | 11.18 |
