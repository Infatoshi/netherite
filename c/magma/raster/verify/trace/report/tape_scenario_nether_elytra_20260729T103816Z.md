# Tape replay: scenario_nether_elytra_20260729T103816Z

345 ticks, seed 0, world_time 6000, start (-69.50,58.00,-84.50).

**FIRST DIVERGENCE: tick 38, field `x`** oracle=-74.7644614879449 magma=-74.83976121916331 |d|=0.0753; inputs {'f': 1.0, 's': 0.0, 'jump': 1, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 6.2952 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=108 independent=107 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=6 available=True
- world nearby_hash: checked=6 deltas=5 available=True

**Pixel gate: FAIL** over 108 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 68 | 3269245 | 211387 |
| bossbar | 58 | 247426 | 38430 |
| hud | 70 | 1308509 | 35281 |
| particles | 43 | 665010 | 35643 |
| thinline | 12 | 6099 | 1709 |
| viewmodel | 70 | 991881 | 37936 |

Failed frames (worst first, top 20):

- t=107: 371441 unexplained px, clusters [{'px': 81936, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 78118, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 211387, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=106: 233484 unexplained px, clusters [{'px': 51209, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 182275, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=103: 220142 unexplained px, clusters [{'px': 72028, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 86, 'cls': 'UNEXPLAINED', 'bbox': [193, 510, 207, 523], 'soak_from': 'viewmodel'}, {'px': 144975, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}, {'px': 129, 'cls': 'UNEXPLAINED', 'bbox': [45, 54, 51, 85]}]
- t=101: 216176 unexplained px, clusters [{'px': 67675, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [381, 447, 383, 469], 'soak_from': 'viewmodel'}, {'px': 59110, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 192, 853], 'soak_from': 'particles'}, {'px': 435, 'cls': 'UNEXPLAINED', 'bbox': [99, 827, 115, 853], 'soak_from': 'particles'}]
- t=102: 209795 unexplained px, clusters [{'px': 71436, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 2114, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 86, 102], 'soak_from': 'particles'}, {'px': 581, 'cls': 'UNEXPLAINED', 'bbox': [45, 65, 56, 137], 'soak_from': 'particles'}, {'px': 1781, 'cls': 'UNEXPLAINED', 'bbox': [45, 171, 96, 273], 'soak_from': 'particles'}]
- t=100: 203883 unexplained px, clusters [{'px': 231, 'cls': 'UNEXPLAINED', 'bbox': [193, 456, 203, 514], 'soak_from': 'viewmodel'}, {'px': 58054, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [193, 628, 197, 643], 'soak_from': 'viewmodel'}, {'px': 318, 'cls': 'UNEXPLAINED', 'bbox': [193, 655, 202, 747], 'soak_from': 'viewmodel'}]
- t=104: 175205 unexplained px, clusters [{'px': 47940, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 119593, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [45, 48, 48, 82], 'soak_from': 'particles'}, {'px': 363, 'cls': 'UNEXPLAINED', 'bbox': [45, 101, 56, 166], 'soak_from': 'particles'}]
- t=105: 172590 unexplained px, clusters [{'px': 49854, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 118850, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}, {'px': 228, 'cls': 'UNEXPLAINED', 'bbox': [45, 150, 58, 178]}, {'px': 386, 'cls': 'UNEXPLAINED', 'bbox': [46, 789, 73, 838], 'soak_from': 'particles'}]
- t=99: 164022 unexplained px, clusters [{'px': 51203, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [193, 603, 205, 616], 'soak_from': 'viewmodel'}, {'px': 925, 'cls': 'UNEXPLAINED', 'bbox': [193, 645, 212, 724], 'soak_from': 'viewmodel'}, {'px': 456, 'cls': 'UNEXPLAINED', 'bbox': [193, 728, 212, 770], 'soak_from': 'viewmodel'}]
- t=98: 136878 unexplained px, clusters [{'px': 150, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 204, 459], 'soak_from': 'viewmodel'}, {'px': 50672, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [304, 838, 313, 853], 'soak_from': 'viewmodel'}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [316, 778, 321, 798], 'soak_from': 'viewmodel'}]
- t=97: 119069 unexplained px, clusters [{'px': 49006, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 80, 'cls': 'UNEXPLAINED', 'bbox': [193, 610, 199, 629], 'soak_from': 'viewmodel'}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [292, 484, 298, 501], 'soak_from': 'viewmodel'}, {'px': 129, 'cls': 'UNEXPLAINED', 'bbox': [317, 829, 326, 853], 'soak_from': 'viewmodel'}]
- t=96: 107705 unexplained px, clusters [{'px': 45986, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 1782, 'cls': 'UNEXPLAINED', 'bbox': [320, 445, 383, 527], 'soak_from': 'viewmodel'}, {'px': 147, 'cls': 'UNEXPLAINED', 'bbox': [338, 500, 349, 525], 'soak_from': 'viewmodel'}, {'px': 172, 'cls': 'UNEXPLAINED', 'bbox': [375, 739, 383, 776], 'soak_from': 'viewmodel'}]
- t=85: 107481 unexplained px, clusters [{'px': 8198, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 535], 'soak_from': 'viewmodel'}, {'px': 12447, 'cls': 'UNEXPLAINED', 'bbox': [193, 530, 383, 654], 'soak_from': 'viewmodel'}, {'px': 891, 'cls': 'UNEXPLAINED', 'bbox': [193, 662, 235, 720], 'soak_from': 'viewmodel'}, {'px': 7846, 'cls': 'UNEXPLAINED', 'bbox': [193, 722, 280, 853], 'soak_from': 'viewmodel'}]
- t=95: 102445 unexplained px, clusters [{'px': 47054, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [195, 636, 203, 649], 'soak_from': 'viewmodel'}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [204, 598, 213, 609], 'soak_from': 'viewmodel'}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [218, 630, 229, 638], 'soak_from': 'viewmodel'}]
- t=94: 97312 unexplained px, clusters [{'px': 46311, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 128, 'cls': 'UNEXPLAINED', 'bbox': [290, 591, 301, 626], 'soak_from': 'viewmodel'}, {'px': 130, 'cls': 'UNEXPLAINED', 'bbox': [297, 516, 309, 558], 'soak_from': 'viewmodel'}, {'px': 901, 'cls': 'UNEXPLAINED', 'bbox': [45, 502, 192, 514]}]
- t=93: 90366 unexplained px, clusters [{'px': 40772, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 59, 'cls': 'UNEXPLAINED', 'bbox': [324, 445, 332, 459], 'soak_from': 'viewmodel'}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [333, 536, 342, 559], 'soak_from': 'viewmodel'}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [336, 830, 344, 853], 'soak_from': 'viewmodel'}]
- t=92: 87574 unexplained px, clusters [{'px': 39176, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 323, 'cls': 'UNEXPLAINED', 'bbox': [352, 557, 379, 589], 'soak_from': 'viewmodel'}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [358, 447, 366, 470], 'soak_from': 'viewmodel'}, {'px': 326, 'cls': 'UNEXPLAINED', 'bbox': [358, 579, 383, 609], 'soak_from': 'viewmodel'}]
- t=86: 63216 unexplained px, clusters [{'px': 232, 'cls': 'UNEXPLAINED', 'bbox': [45, 492, 192, 497]}, {'px': 235, 'cls': 'UNEXPLAINED', 'bbox': [45, 505, 192, 510], 'soak_from': 'particles'}, {'px': 13206, 'cls': 'UNEXPLAINED', 'bbox': [45, 562, 192, 664]}, {'px': 418, 'cls': 'UNEXPLAINED', 'bbox': [45, 669, 69, 693], 'soak_from': 'particles'}]
- t=84: 51821 unexplained px, clusters [{'px': 1334, 'cls': 'UNEXPLAINED', 'bbox': [45, 474, 192, 492]}, {'px': 2481, 'cls': 'UNEXPLAINED', 'bbox': [45, 489, 192, 517], 'soak_from': 'particles'}, {'px': 96, 'cls': 'UNEXPLAINED', 'bbox': [45, 518, 53, 528], 'soak_from': 'particles'}, {'px': 444, 'cls': 'UNEXPLAINED', 'bbox': [45, 539, 56, 579]}]
- t=91: 45754 unexplained px, clusters [{'px': 491, 'cls': 'UNEXPLAINED', 'bbox': [45, 488, 192, 497]}, {'px': 412, 'cls': 'UNEXPLAINED', 'bbox': [45, 505, 192, 512], 'soak_from': 'particles'}, {'px': 43909, 'cls': 'UNEXPLAINED', 'bbox': [210, 0, 383, 444], 'soak_from': 'particles'}, {'px': 99, 'cls': 'UNEXPLAINED', 'bbox': [210, 407, 226, 422], 'soak_from': 'particles'}]

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
| 12 | 1.20 | 87.57% | 1.28 |
| 13 | 1.22 | 90.01% | 1.28 |
| 14 | 1.22 | 91.11% | 1.28 |
| 15 | 1.22 | 91.09% | 1.28 |
| 16 | 1.23 | 91.02% | 1.30 |
| 17 | 1.21 | 90.66% | 1.28 |
| 18 | 1.21 | 90.34% | 1.28 |
| 19 | 1.22 | 90.08% | 1.28 |
| 20 | 1.21 | 89.88% | 1.29 |
| 21 | 1.21 | 89.80% | 1.29 |
| 22 | 1.21 | 89.74% | 1.29 |
| 23 | 1.21 | 89.82% | 1.29 |
| 24 | 1.18 | 89.95% | 1.29 |
| 25 | 1.18 | 90.15% | 1.29 |
| 26 | 1.18 | 90.28% | 1.29 |
| 27 | 1.18 | 90.63% | 1.28 |
| 28 | 1.18 | 91.06% | 1.27 |
| 29 | 1.18 | 91.46% | 1.27 |
| 30 | 1.18 | 91.76% | 1.26 |
| 31 | 1.18 | 91.89% | 1.25 |
| 32 | 1.17 | 92.24% | 1.27 |
| 33 | 1.16 | 92.53% | 1.28 |
| 34 | 1.16 | 92.80% | 1.30 |
| 35 | 1.15 | 93.02% | 1.30 |
| 36 | 1.17 | 93.09% | 1.31 |
| 37 | 1.17 | 93.15% | 1.32 |
| 38 | 3.76 | 93.60% | 3.59 |
| 39 | 3.38 | 93.46% | 3.18 |
| 40 | 3.61 | 93.31% | 3.44 |
| 41 | 3.90 | 93.46% | 3.82 |
| 42 | 4.23 | 93.47% | 4.16 |
| 43 | 4.50 | 93.58% | 4.42 |
| 44 | 4.86 | 96.39% | 4.77 |
| 45 | 5.16 | 96.41% | 5.09 |
| 46 | 5.84 | 96.31% | 5.83 |
| 47 | 5.88 | 96.28% | 5.89 |
| 48 | 6.23 | 96.27% | 6.29 |
| 49 | 6.56 | 96.22% | 6.63 |
| 50 | 6.78 | 96.25% | 6.94 |
| 51 | 6.95 | 96.25% | 7.20 |
| 52 | 7.39 | 96.44% | 7.57 |
| 53 | 7.61 | 96.45% | 7.82 |
| 54 | 7.85 | 96.45% | 8.05 |
| 55 | 8.20 | 96.46% | 8.51 |
| 56 | 8.48 | 96.46% | 8.92 |
| 57 | 9.12 | 96.49% | 9.55 |
| 58 | 8.64 | 94.33% | 9.39 |
| 59 | 8.93 | 94.99% | 9.72 |
| 60 | 10.18 | 95.83% | 11.00 |
| 61 | 10.03 | 94.24% | 10.94 |
| 62 | 9.95 | 95.84% | 8.91 |
| 63 | 7.87 | 94.23% | 8.42 |
| 64 | 8.29 | 95.83% | 8.87 |
| 65 | 8.40 | 94.24% | 9.02 |
| 66 | 8.40 | 95.84% | 9.03 |
| 67 | 8.46 | 95.02% | 9.07 |
| 68 | 8.79 | 96.53% | 9.38 |
| 69 | 9.24 | 96.53% | 9.79 |
| 70 | 9.35 | 96.53% | 9.94 |
| 71 | 9.20 | 96.53% | 9.89 |
| 72 | 9.19 | 96.53% | 9.86 |
| 73 | 9.42 | 96.53% | 10.16 |
| 74 | 9.50 | 96.52% | 10.27 |
| 75 | 10.05 | 96.52% | 10.81 |
| 76 | 9.95 | 96.52% | 10.75 |
| 77 | 9.79 | 96.51% | 10.66 |
| 78 | 10.04 | 96.52% | 11.01 |
| 79 | 10.30 | 96.52% | 11.27 |
| 80 | 10.49 | 96.50% | 11.44 |
| 81 | 11.15 | 96.52% | 12.04 |
| 82 | 11.23 | 96.46% | 12.27 |
| 83 | 11.44 | 96.48% | 12.73 |
| 84 | 12.01 | 94.16% | 13.75 |
| 85 | 14.76 | 95.82% | 13.70 |
| 86 | 14.33 | 96.48% | 16.34 |
| 87 | 10.87 | 96.47% | 12.63 |
| 88 | 11.35 | 95.80% | 13.23 |
| 89 | 12.35 | 96.47% | 14.16 |
| 90 | 12.66 | 96.45% | 14.64 |
| 91 | 13.02 | 96.45% | 15.23 |
| 92 | 13.43 | 96.43% | 15.87 |
| 93 | 13.84 | 96.38% | 16.56 |
| 94 | 15.58 | 96.41% | 18.45 |
| 95 | 16.41 | 96.36% | 19.91 |
| 96 | 17.26 | 96.39% | 21.64 |
| 97 | 19.54 | 96.41% | 25.09 |
| 98 | 22.79 | 96.45% | 29.29 |
| 99 | 27.33 | 96.45% | 34.98 |
| 100 | 33.87 | 96.45% | 42.65 |
| 101 | 41.95 | 96.37% | 49.85 |
| 102 | 47.66 | 96.53% | 52.98 |
| 103 | 47.63 | 96.66% | 52.04 |
| 104 | 31.72 | 94.61% | 36.66 |
| 105 | 32.62 | 94.65% | 38.10 |
| 106 | 26.10 | 95.23% | 27.18 |
| 107 | 42.25 | 100.00% | 43.77 |
