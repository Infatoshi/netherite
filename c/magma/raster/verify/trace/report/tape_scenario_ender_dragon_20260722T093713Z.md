# Tape replay: scenario_ender_dragon_20260722T093713Z

1609 ticks, seed 0, world_time 6000, start (100.00,49.00,0.00).

**FIRST DIVERGENCE: tick 187, field `z`** oracle=-0.0030718683265149593 magma=0 |d|=0.00307; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=3 independent=2 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=81 available=True
- world nearby_hash: checked=81 deltas=11 available=True

**Pixel gate: FAIL** over 77 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 47 | 292941 | 9066 |
| bossbar | 1 | 826 | 826 |
| hud | 55 | 346068 | 13592 |
| particles | 48 | 216019 | 2621 |
| thinline | 3 | 271 | 133 |
| viewmodel | 55 | 145036 | 12770 |

Failed frames (worst first, top 20):

- t=500: 17603 unexplained px, clusters [{'px': 8940, 'cls': 'UNEXPLAINED', 'bbox': [121, 95, 322, 291]}, {'px': 2662, 'cls': 'UNEXPLAINED', 'bbox': [122, 524, 192, 609]}, {'px': 1418, 'cls': 'UNEXPLAINED', 'bbox': [151, 781, 190, 849]}, {'px': 421, 'cls': 'UNEXPLAINED', 'bbox': [275, 145, 312, 156]}]
- t=1020: 14656 unexplained px, clusters [{'px': 951, 'cls': 'UNEXPLAINED', 'bbox': [153, 478, 192, 530]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [168, 142, 178, 148]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 514, 'cls': 'UNEXPLAINED', 'bbox': [209, 348, 244, 379]}]
- t=1000: 13471 unexplained px, clusters [{'px': 1001, 'cls': 'UNEXPLAINED', 'bbox': [142, 478, 192, 530]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 128, 'cls': 'UNEXPLAINED', 'bbox': [179, 82, 193, 101]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=980: 13137 unexplained px, clusters [{'px': 985, 'cls': 'UNEXPLAINED', 'bbox': [148, 478, 192, 530]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [168, 142, 178, 148]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=940: 11803 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}, {'px': 339, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=960: 11477 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 339, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}, {'px': 331, 'cls': 'UNEXPLAINED', 'bbox': [212, 182, 232, 215]}]
- t=680: 10999 unexplained px, clusters [{'px': 7036, 'cls': 'UNEXPLAINED', 'bbox': [66, 785, 188, 853]}, {'px': 360, 'cls': 'UNEXPLAINED', 'bbox': [259, 413, 276, 432]}, {'px': 216, 'cls': 'UNEXPLAINED', 'bbox': [311, 378, 322, 395]}, {'px': 3387, 'cls': 'UNEXPLAINED', 'bbox': [323, 271, 383, 343]}]
- t=920: 10739 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}, {'px': 340, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=1320: 10382 unexplained px, clusters [{'px': 660, 'cls': 'UNEXPLAINED', 'bbox': [194, 287, 232, 325]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1330, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=700: 10208 unexplained px, clusters [{'px': 5639, 'cls': 'UNEXPLAINED', 'bbox': [66, 785, 188, 853]}, {'px': 305, 'cls': 'UNEXPLAINED', 'bbox': [173, 551, 190, 567]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [237, 362, 241, 378]}, {'px': 576, 'cls': 'UNEXPLAINED', 'bbox': [259, 413, 276, 444]}]
- t=1280: 9611 unexplained px, clusters [{'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [207, 300, 219, 315]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1264, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=1300: 9404 unexplained px, clusters [{'px': 260, 'cls': 'UNEXPLAINED', 'bbox': [207, 300, 219, 319]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1299, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=480: 8658 unexplained px, clusters [{'px': 1889, 'cls': 'UNEXPLAINED', 'bbox': [140, 551, 192, 609]}, {'px': 3888, 'cls': 'UNEXPLAINED', 'bbox': [148, 219, 322, 291]}, {'px': 1418, 'cls': 'UNEXPLAINED', 'bbox': [151, 781, 190, 849]}, {'px': 467, 'cls': 'UNEXPLAINED', 'bbox': [184, 203, 214, 219]}]
- t=1260: 8309 unexplained px, clusters [{'px': 1330, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 134, 'cls': 'UNEXPLAINED', 'bbox': [223, 430, 245, 444]}, {'px': 6674, 'cls': 'UNEXPLAINED', 'bbox': [277, 272, 382, 444]}, {'px': 171, 'cls': 'UNEXPLAINED', 'bbox': [284, 299, 298, 312]}]
- t=1240: 8020 unexplained px, clusters [{'px': 1131, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [221, 430, 245, 444]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}, {'px': 178, 'cls': 'UNEXPLAINED', 'bbox': [272, 299, 285, 312]}]
- t=1220: 7689 unexplained px, clusters [{'px': 1042, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 271, 366]}, {'px': 124, 'cls': 'UNEXPLAINED', 'bbox': [221, 430, 245, 444]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}, {'px': 178, 'cls': 'UNEXPLAINED', 'bbox': [272, 299, 285, 312]}]
- t=1200: 7030 unexplained px, clusters [{'px': 6888, 'cls': 'UNEXPLAINED', 'bbox': [218, 272, 377, 444]}, {'px': 142, 'cls': 'UNEXPLAINED', 'bbox': [221, 423, 255, 444]}]
- t=1180: 7005 unexplained px, clusters [{'px': 632, 'cls': 'UNEXPLAINED', 'bbox': [218, 339, 274, 399]}, {'px': 147, 'cls': 'UNEXPLAINED', 'bbox': [233, 423, 255, 442]}, {'px': 6226, 'cls': 'UNEXPLAINED', 'bbox': [293, 272, 382, 444]}]
- t=1160: 6801 unexplained px, clusters [{'px': 685, 'cls': 'UNEXPLAINED', 'bbox': [218, 339, 271, 376]}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [233, 430, 245, 442]}, {'px': 86, 'cls': 'UNEXPLAINED', 'bbox': [272, 378, 283, 391]}, {'px': 5954, 'cls': 'UNEXPLAINED', 'bbox': [293, 272, 383, 444]}]
- t=1140: 6574 unexplained px, clusters [{'px': 600, 'cls': 'UNEXPLAINED', 'bbox': [220, 352, 283, 390]}, {'px': 5974, 'cls': 'UNEXPLAINED', 'bbox': [293, 268, 382, 444]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 80 | 0.95 | 92.09% | 1.00 |
| 100 | 0.95 | 92.09% | 1.00 |
| 120 | 0.95 | 92.09% | 1.00 |
| 140 | 0.95 | 92.09% | 1.00 |
| 160 | 0.95 | 92.09% | 1.00 |
| 180 | 0.95 | 92.09% | 1.00 |
| 200 | 1.03 | 93.20% | 1.08 |
| 220 | 1.07 | 93.17% | 1.11 |
| 240 | 1.14 | 93.59% | 1.14 |
| 260 | 1.12 | 94.15% | 1.16 |
| 280 | 1.01 | 94.22% | 0.94 |
| 300 | 0.91 | 94.15% | 0.96 |
| 320 | 0.91 | 93.77% | 0.96 |
| 340 | 0.91 | 93.33% | 0.96 |
| 360 | 1.60 | 92.08% | 2.04 |
| 380 | 3.06 | 92.69% | 4.26 |
| 400 | 5.21 | 93.15% | 7.01 |
| 420 | 5.99 | 93.05% | 7.46 |
| 440 | 9.42 | 93.61% | 10.85 |
| 460 | 9.22 | 93.55% | 10.26 |
| 480 | 9.31 | 92.74% | 10.32 |
| 500 | 10.90 | 94.09% | 12.67 |
| 520 | 0.93 | 92.54% | 0.87 |
| 540 | 2.00 | 92.72% | 2.61 |
| 560 | 3.12 | 92.98% | 3.58 |
| 580 | 4.72 | 93.00% | 5.46 |
| 600 | 4.64 | 92.24% | 5.11 |
| 620 | 7.52 | 93.31% | 7.98 |
| 640 | 8.11 | 94.11% | 9.02 |
| 660 | 8.60 | 94.11% | 9.61 |
| 680 | 9.63 | 94.11% | 10.77 |
| 700 | 9.73 | 94.11% | 11.08 |
| 720 | 2.30 | 93.34% | 1.89 |
| 740 | 1.73 | 90.84% | 2.09 |
| 760 | 2.56 | 92.78% | 3.37 |
| 780 | 3.09 | 92.76% | 3.87 |
| 800 | 4.50 | 92.82% | 5.22 |
| 820 | 5.93 | 94.18% | 6.33 |
| 840 | 5.87 | 94.46% | 6.12 |
| 860 | 5.85 | 94.86% | 6.15 |
| 880 | 6.07 | 94.91% | 6.61 |
| 900 | 5.86 | 94.82% | 6.48 |
| 920 | 6.51 | 94.92% | 7.57 |
| 940 | 6.54 | 94.88% | 7.61 |
| 960 | 6.49 | 94.65% | 7.57 |
| 980 | 7.11 | 94.92% | 8.40 |
| 1000 | 7.34 | 94.75% | 8.65 |
| 1020 | 7.48 | 94.88% | 8.93 |
| 1040 | 4.47 | 88.32% | 5.34 |
| 1060 | 4.44 | 88.28% | 5.40 |
| 1080 | 4.33 | 88.22% | 5.25 |
| 1100 | 4.40 | 88.20% | 5.38 |
| 1120 | 4.39 | 88.25% | 5.36 |
| 1140 | 4.43 | 88.21% | 5.42 |
| 1160 | 4.46 | 88.22% | 5.49 |
| 1180 | 4.49 | 88.22% | 5.54 |
| 1200 | 4.54 | 88.23% | 5.61 |
| 1220 | 4.53 | 88.20% | 5.59 |
| 1240 | 4.60 | 88.26% | 5.72 |
| 1260 | 4.62 | 88.24% | 5.75 |
| 1280 | 4.71 | 88.24% | 5.91 |
| 1300 | 4.65 | 88.21% | 5.80 |
| 1320 | 4.75 | 88.19% | 5.97 |
| 1340 | 5.94 | 88.71% | 5.58 |
| 1360 | 3.64 | 87.43% | 4.02 |
| 1380 | 3.59 | 87.36% | 4.00 |
| 1400 | 3.59 | 87.37% | 4.00 |
| 1420 | 3.59 | 87.37% | 4.01 |
| 1440 | 3.61 | 87.37% | 4.04 |
| 1460 | 3.60 | 87.36% | 4.03 |
| 1480 | 3.62 | 87.36% | 4.05 |
| 1500 | 3.63 | 87.37% | 4.07 |
| 1520 | 3.63 | 87.36% | 4.07 |
| 1540 | 3.65 | 87.37% | 4.10 |
| 1560 | 3.66 | 87.36% | 4.12 |
| 1580 | 3.66 | 87.37% | 4.12 |
| 1600 | 3.69 | 87.37% | 4.18 |
