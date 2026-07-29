# Tape replay: scenario_nether_elytra_20260729T105510Z

326 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 39, field `x`** oracle=-74.97102712093216 magma=-75.05130486623412 |d|=0.0803; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 4.9806 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=100 independent=99 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=5 available=True
- world nearby_hash: checked=5 deltas=4 available=True

**Pixel gate: FAIL** over 100 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 61 | 11444938 | 181990 |
| bossbar | 21 | 34805 | 4413 |
| hud | 59 | 534879 | 42829 |
| particles | 9 | 111121 | 36113 |
| viewmodel | 36 | 218817 | 34253 |

Failed frames (worst first, top 20):

- t=99: 327617 unexplained px, clusters [{'px': 21143, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 474], 'soak_from': 'transit'}, {'px': 16054, 'cls': 'UNEXPLAINED', 'bbox': [0, 490, 44, 853], 'soak_from': 'transit'}, {'px': 65156, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'transit'}, {'px': 86, 'cls': 'UNEXPLAINED', 'bbox': [413, 302, 421, 315], 'soak_from': 'transit'}]
- t=73: 317482 unexplained px, clusters [{'px': 4187, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 120], 'soak_from': 'transit'}, {'px': 13620, 'cls': 'UNEXPLAINED', 'bbox': [0, 200, 44, 527], 'soak_from': 'transit'}, {'px': 4285, 'cls': 'UNEXPLAINED', 'bbox': [0, 567, 44, 674], 'soak_from': 'transit'}, {'px': 4025, 'cls': 'UNEXPLAINED', 'bbox': [0, 758, 44, 853], 'soak_from': 'transit'}]
- t=75: 305716 unexplained px, clusters [{'px': 1860, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 23, 116], 'soak_from': 'transit'}, {'px': 121, 'cls': 'UNEXPLAINED', 'bbox': [0, 143, 5, 173], 'soak_from': 'transit'}, {'px': 993, 'cls': 'UNEXPLAINED', 'bbox': [0, 175, 25, 254], 'soak_from': 'transit'}, {'px': 6178, 'cls': 'UNEXPLAINED', 'bbox': [0, 244, 44, 449], 'soak_from': 'transit'}]
- t=74: 304890 unexplained px, clusters [{'px': 829, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 34, 90], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [0, 28, 4, 54], 'soak_from': 'transit'}, {'px': 1927, 'cls': 'UNEXPLAINED', 'bbox': [0, 174, 41, 272], 'soak_from': 'transit'}, {'px': 6304, 'cls': 'UNEXPLAINED', 'bbox': [0, 273, 44, 506], 'soak_from': 'transit'}]
- t=76: 303816 unexplained px, clusters [{'px': 122, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 8, 22], 'soak_from': 'transit'}, {'px': 3378, 'cls': 'UNEXPLAINED', 'bbox': [0, 29, 44, 229], 'soak_from': 'transit'}, {'px': 557, 'cls': 'UNEXPLAINED', 'bbox': [0, 305, 13, 353], 'soak_from': 'transit'}, {'px': 3708, 'cls': 'UNEXPLAINED', 'bbox': [0, 313, 44, 491], 'soak_from': 'transit'}]
- t=72: 302115 unexplained px, clusters [{'px': 11040, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 245], 'soak_from': 'transit'}, {'px': 1983, 'cls': 'UNEXPLAINED', 'bbox': [0, 427, 44, 471], 'soak_from': 'transit'}, {'px': 9872, 'cls': 'UNEXPLAINED', 'bbox': [0, 480, 44, 700], 'soak_from': 'transit'}, {'px': 2771, 'cls': 'UNEXPLAINED', 'bbox': [0, 790, 44, 853], 'soak_from': 'transit'}]
- t=71: 285742 unexplained px, clusters [{'px': 16345, 'cls': 'UNEXPLAINED', 'bbox': [0, 62, 44, 426], 'soak_from': 'transit'}, {'px': 945, 'cls': 'UNEXPLAINED', 'bbox': [0, 437, 44, 457], 'soak_from': 'transit'}, {'px': 15086, 'cls': 'UNEXPLAINED', 'bbox': [0, 517, 44, 853], 'soak_from': 'transit'}, {'px': 84, 'cls': 'UNEXPLAINED', 'bbox': [384, 7, 389, 36], 'soak_from': 'transit'}]
- t=70: 283826 unexplained px, clusters [{'px': 19215, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 426], 'soak_from': 'transit'}, {'px': 900, 'cls': 'UNEXPLAINED', 'bbox': [0, 436, 44, 455], 'soak_from': 'transit'}, {'px': 11036, 'cls': 'UNEXPLAINED', 'bbox': [0, 607, 44, 853], 'soak_from': 'transit'}, {'px': 7889, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 139], 'soak_from': 'transit'}]
- t=94: 275462 unexplained px, clusters [{'px': 38418, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2024, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=90: 274532 unexplained px, clusters [{'px': 38401, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=95: 274282 unexplained px, clusters [{'px': 38422, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 276, 421, 293], 'soak_from': 'transit'}]
- t=97: 274193 unexplained px, clusters [{'px': 38412, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 2084, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 405], 'soak_from': 'transit'}]
- t=96: 273829 unexplained px, clusters [{'px': 38427, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 312, 'cls': 'UNEXPLAINED', 'bbox': [402, 308, 421, 341], 'soak_from': 'transit'}]
- t=93: 273459 unexplained px, clusters [{'px': 38415, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [402, 278, 407, 291], 'soak_from': 'transit'}]
- t=92: 273428 unexplained px, clusters [{'px': 38415, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 156, 'cls': 'UNEXPLAINED', 'bbox': [402, 260, 421, 277], 'soak_from': 'transit'}]
- t=87: 273020 unexplained px, clusters [{'px': 38425, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 1644, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 419, 405], 'soak_from': 'transit'}]
- t=91: 272947 unexplained px, clusters [{'px': 38412, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50812, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=89: 272572 unexplained px, clusters [{'px': 38400, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50741, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]
- t=98: 272260 unexplained px, clusters [{'px': 38420, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [402, 244, 421, 261], 'soak_from': 'transit'}]
- t=88: 271957 unexplained px, clusters [{'px': 38416, 'cls': 'UNEXPLAINED', 'bbox': [0, 0, 44, 853], 'soak_from': 'transit'}, {'px': 572, 'cls': 'UNEXPLAINED', 'bbox': [384, 334, 394, 400], 'soak_from': 'transit'}, {'px': 1597, 'cls': 'UNEXPLAINED', 'bbox': [384, 808, 419, 853], 'soak_from': 'transit'}, {'px': 50571, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'transit'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 1.57 | 87.56% | 1.63 |
| 1 | 1.38 | 86.84% | 1.57 |
| 2 | 1.19 | 84.83% | 1.31 |
| 3 | 1.19 | 84.83% | 1.31 |
| 4 | 1.19 | 84.83% | 1.31 |
| 5 | 1.19 | 84.84% | 1.31 |
| 6 | 1.19 | 84.84% | 1.31 |
| 7 | 1.19 | 84.84% | 1.31 |
| 8 | 1.19 | 84.84% | 1.31 |
| 9 | 1.19 | 84.84% | 1.31 |
| 10 | 1.21 | 85.53% | 1.32 |
| 11 | 1.21 | 86.28% | 1.33 |
| 12 | 1.22 | 87.56% | 1.33 |
| 13 | 1.25 | 89.96% | 1.33 |
| 14 | 1.25 | 91.03% | 1.33 |
| 15 | 1.25 | 91.01% | 1.33 |
| 16 | 1.25 | 90.92% | 1.34 |
| 17 | 1.24 | 90.54% | 1.32 |
| 18 | 1.24 | 90.18% | 1.32 |
| 19 | 1.22 | 89.90% | 1.31 |
| 20 | 1.21 | 89.69% | 1.31 |
| 21 | 1.21 | 89.60% | 1.30 |
| 22 | 1.21 | 89.55% | 1.31 |
| 23 | 1.20 | 89.59% | 1.31 |
| 24 | 1.20 | 89.71% | 1.32 |
| 25 | 1.20 | 89.77% | 1.31 |
| 26 | 1.20 | 90.01% | 1.31 |
| 27 | 1.20 | 90.40% | 1.31 |
| 28 | 1.21 | 90.82% | 1.31 |
| 29 | 1.21 | 91.24% | 1.31 |
| 30 | 1.22 | 91.57% | 1.32 |
| 31 | 1.22 | 91.71% | 1.32 |
| 32 | 1.22 | 92.07% | 1.35 |
| 33 | 1.22 | 92.45% | 1.37 |
| 34 | 1.22 | 92.78% | 1.40 |
| 35 | 1.21 | 92.98% | 1.40 |
| 36 | 1.23 | 93.07% | 1.42 |
| 37 | 1.23 | 93.12% | 1.42 |
| 38 | 1.24 | 93.19% | 1.43 |
| 39 | 4.32 | 93.60% | 4.42 |
| 40 | 4.40 | 93.62% | 4.53 |
| 41 | 4.35 | 93.63% | 4.48 |
| 42 | 5.61 | 93.62% | 6.01 |
| 43 | 6.58 | 96.43% | 7.19 |
| 44 | 7.47 | 96.42% | 8.30 |
| 45 | 8.65 | 96.37% | 9.68 |
| 46 | 9.76 | 96.36% | 10.88 |
| 47 | 10.56 | 96.31% | 11.78 |
| 48 | 11.38 | 96.24% | 12.77 |
| 49 | 12.10 | 96.20% | 13.69 |
| 50 | 12.84 | 96.30% | 14.60 |
| 51 | 13.87 | 96.25% | 15.86 |
| 52 | 14.33 | 96.21% | 16.72 |
| 53 | 15.45 | 96.20% | 17.94 |
| 54 | 16.06 | 95.97% | 18.85 |
| 55 | 16.81 | 96.01% | 19.92 |
| 56 | 18.15 | 95.99% | 21.56 |
| 57 | 18.90 | 96.07% | 22.86 |
| 58 | 20.07 | 96.17% | 24.50 |
| 59 | 21.58 | 96.23% | 26.44 |
| 60 | 23.92 | 96.32% | 29.91 |
| 61 | 27.35 | 94.10% | 31.51 |
| 62 | 27.36 | 96.41% | 33.34 |
| 63 | 26.59 | 96.49% | 30.41 |
| 64 | 27.98 | 96.50% | 31.66 |
| 65 | 28.61 | 96.51% | 32.63 |
| 66 | 29.82 | 96.51% | 33.56 |
| 67 | 30.36 | 96.53% | 33.51 |
| 68 | 32.05 | 96.51% | 34.15 |
| 69 | 30.63 | 79.25% | 31.09 |
| 70 | 33.95 | 96.53% | 36.54 |
| 71 | 32.85 | 96.51% | 34.80 |
| 72 | 41.30 | 96.53% | 39.96 |
| 73 | 47.80 | 96.53% | 50.30 |
| 74 | 48.57 | 96.53% | 52.00 |
| 75 | 50.03 | 96.86% | 53.42 |
| 76 | 49.72 | 96.52% | 53.26 |
| 77 | 28.99 | 96.22% | 33.28 |
| 78 | 29.88 | 96.49% | 33.90 |
| 79 | 24.64 | 96.06% | 26.92 |
| 80 | 29.87 | 95.84% | 31.47 |
| 81 | 28.92 | 96.18% | 30.75 |
| 82 | 28.41 | 95.78% | 29.16 |
| 83 | 27.92 | 95.80% | 29.09 |
| 84 | 28.69 | 96.20% | 29.93 |
| 85 | 28.04 | 95.85% | 29.18 |
| 86 | 27.92 | 95.85% | 29.34 |
| 87 | 28.83 | 96.26% | 30.51 |
| 88 | 27.97 | 95.87% | 29.65 |
| 89 | 28.07 | 95.91% | 29.79 |
| 90 | 28.98 | 96.33% | 30.81 |
| 91 | 28.28 | 95.92% | 29.97 |
| 92 | 28.60 | 95.85% | 30.11 |
| 93 | 28.66 | 95.85% | 30.20 |
| 94 | 29.67 | 96.38% | 31.43 |
| 95 | 28.35 | 96.13% | 30.28 |
| 96 | 28.23 | 95.90% | 30.14 |
| 97 | 28.87 | 96.30% | 31.11 |
| 98 | 28.05 | 95.86% | 30.22 |
| 99 | 22.69 | 91.38% | 20.70 |
