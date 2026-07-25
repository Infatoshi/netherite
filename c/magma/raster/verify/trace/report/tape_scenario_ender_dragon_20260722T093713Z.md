# Tape replay: scenario_ender_dragon_20260722T093713Z

1609 ticks, seed 0, world_time 6000, start (100.00,49.00,0.00).

**FIRST DIVERGENCE: tick 187, field `z`** oracle=-0.0030718683265149593 magma=0 |d|=0.00307; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=1 mismatches=3 available=True pass=False
- entities: checked=81 available=True
- world nearby_hash: checked=81 deltas=11 available=True

**Pixel gate: FAIL** over 77 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 57 | 445907 | 45216 |
| bossbar | 8 | 46605 | 10890 |
| hud | 57 | 363184 | 13592 |
| particles | 45 | 208602 | 2805 |
| thinline | 3 | 271 | 133 |
| viewmodel | 56 | 159412 | 12770 |

Failed frames (worst first, top 20):

- t=420: 45216 unexplained px, clusters [{'px': 45216, 'cls': 'UNEXPLAINED', 'bbox': [45, 179, 268, 415]}]
- t=780: 23442 unexplained px, clusters [{'px': 13572, 'cls': 'UNEXPLAINED', 'bbox': [174, 103, 290, 218]}, {'px': 9870, 'cls': 'UNEXPLAINED', 'bbox': [314, 36, 383, 176]}]
- t=500: 23227 unexplained px, clusters [{'px': 8940, 'cls': 'UNEXPLAINED', 'bbox': [121, 95, 322, 291]}, {'px': 2662, 'cls': 'UNEXPLAINED', 'bbox': [122, 524, 192, 609]}, {'px': 1924, 'cls': 'UNEXPLAINED', 'bbox': [150, 395, 207, 472]}, {'px': 1418, 'cls': 'UNEXPLAINED', 'bbox': [151, 781, 190, 849]}]
- t=800: 20872 unexplained px, clusters [{'px': 2451, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 101, 42]}, {'px': 10901, 'cls': 'UNEXPLAINED', 'bbox': [45, 72, 173, 179]}, {'px': 5112, 'cls': 'UNEXPLAINED', 'bbox': [198, 373, 269, 443]}, {'px': 2408, 'cls': 'UNEXPLAINED', 'bbox': [341, 0, 383, 55]}]
- t=760: 17082 unexplained px, clusters [{'px': 17082, 'cls': 'UNEXPLAINED', 'bbox': [45, 358, 161, 503]}]
- t=1000: 15502 unexplained px, clusters [{'px': 1001, 'cls': 'UNEXPLAINED', 'bbox': [142, 478, 192, 530]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 128, 'cls': 'UNEXPLAINED', 'bbox': [179, 82, 193, 101]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=1020: 14777 unexplained px, clusters [{'px': 951, 'cls': 'UNEXPLAINED', 'bbox': [153, 478, 192, 530]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [168, 142, 178, 148]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 514, 'cls': 'UNEXPLAINED', 'bbox': [209, 348, 244, 379]}]
- t=980: 14088 unexplained px, clusters [{'px': 985, 'cls': 'UNEXPLAINED', 'bbox': [148, 478, 192, 530]}, {'px': 77, 'cls': 'UNEXPLAINED', 'bbox': [168, 142, 178, 148]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=680: 13729 unexplained px, clusters [{'px': 7036, 'cls': 'UNEXPLAINED', 'bbox': [66, 785, 188, 853]}, {'px': 441, 'cls': 'UNEXPLAINED', 'bbox': [176, 358, 196, 378]}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [219, 443, 251, 444]}, {'px': 660, 'cls': 'UNEXPLAINED', 'bbox': [255, 413, 279, 444]}]
- t=700: 12995 unexplained px, clusters [{'px': 5639, 'cls': 'UNEXPLAINED', 'bbox': [66, 785, 188, 853]}, {'px': 830, 'cls': 'UNEXPLAINED', 'bbox': [164, 489, 192, 543]}, {'px': 1516, 'cls': 'UNEXPLAINED', 'bbox': [173, 416, 214, 468]}, {'px': 305, 'cls': 'UNEXPLAINED', 'bbox': [173, 551, 190, 567]}]
- t=940: 12077 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}, {'px': 339, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=960: 11895 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 339, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}, {'px': 331, 'cls': 'UNEXPLAINED', 'bbox': [212, 182, 232, 215]}]
- t=380: 11387 unexplained px, clusters [{'px': 11387, 'cls': 'UNEXPLAINED', 'bbox': [45, 290, 103, 482]}]
- t=920: 10798 unexplained px, clusters [{'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}, {'px': 340, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [205, 44, 231, 46]}]
- t=480: 10606 unexplained px, clusters [{'px': 1889, 'cls': 'UNEXPLAINED', 'bbox': [140, 551, 192, 609]}, {'px': 3888, 'cls': 'UNEXPLAINED', 'bbox': [148, 219, 322, 291]}, {'px': 1418, 'cls': 'UNEXPLAINED', 'bbox': [151, 781, 190, 849]}, {'px': 467, 'cls': 'UNEXPLAINED', 'bbox': [184, 203, 214, 219]}]
- t=1320: 10382 unexplained px, clusters [{'px': 660, 'cls': 'UNEXPLAINED', 'bbox': [194, 287, 232, 325]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1330, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=1280: 9611 unexplained px, clusters [{'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [207, 300, 219, 315]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1264, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=1300: 9404 unexplained px, clusters [{'px': 260, 'cls': 'UNEXPLAINED', 'bbox': [207, 300, 219, 319]}, {'px': 140, 'cls': 'UNEXPLAINED', 'bbox': [217, 430, 245, 444]}, {'px': 1299, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}]
- t=1260: 8309 unexplained px, clusters [{'px': 1330, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 134, 'cls': 'UNEXPLAINED', 'bbox': [223, 430, 245, 444]}, {'px': 6674, 'cls': 'UNEXPLAINED', 'bbox': [277, 272, 382, 444]}, {'px': 171, 'cls': 'UNEXPLAINED', 'bbox': [284, 299, 298, 312]}]
- t=1240: 8020 unexplained px, clusters [{'px': 1131, 'cls': 'UNEXPLAINED', 'bbox': [218, 313, 273, 380]}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [221, 430, 245, 444]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [233, 391, 245, 403]}, {'px': 178, 'cls': 'UNEXPLAINED', 'bbox': [272, 299, 285, 312]}]

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
| 380 | 7.62 | 92.69% | 7.07 |
| 400 | 6.10 | 93.15% | 7.26 |
| 420 | 17.81 | 93.05% | 21.56 |
| 440 | 9.42 | 93.61% | 10.85 |
| 460 | 9.46 | 93.55% | 10.68 |
| 480 | 9.83 | 92.80% | 11.21 |
| 500 | 11.75 | 94.09% | 14.11 |
| 520 | 0.93 | 92.54% | 0.87 |
| 540 | 2.17 | 92.72% | 2.61 |
| 560 | 4.30 | 92.98% | 3.58 |
| 580 | 4.72 | 93.00% | 5.46 |
| 600 | 8.01 | 92.24% | 5.11 |
| 620 | 7.66 | 93.31% | 8.21 |
| 640 | 8.41 | 94.11% | 9.54 |
| 660 | 9.17 | 94.11% | 10.58 |
| 680 | 10.57 | 94.11% | 12.37 |
| 700 | 10.94 | 94.11% | 13.14 |
| 720 | 2.30 | 93.34% | 1.89 |
| 740 | 1.73 | 90.84% | 2.09 |
| 760 | 6.60 | 92.78% | 7.90 |
| 780 | 9.87 | 92.81% | 11.99 |
| 800 | 10.63 | 93.37% | 10.00 |
| 820 | 5.93 | 94.18% | 6.33 |
| 840 | 6.05 | 94.46% | 6.42 |
| 860 | 6.20 | 94.86% | 6.76 |
| 880 | 6.58 | 94.91% | 7.46 |
| 900 | 6.29 | 94.82% | 7.21 |
| 920 | 6.53 | 94.92% | 7.60 |
| 940 | 6.62 | 94.88% | 7.75 |
| 960 | 6.58 | 94.65% | 7.71 |
| 980 | 7.30 | 94.92% | 8.71 |
| 1000 | 7.58 | 94.75% | 9.05 |
| 1020 | 7.51 | 94.88% | 8.97 |
| 1040 | 4.52 | 88.32% | 5.43 |
| 1060 | 4.52 | 88.28% | 5.54 |
| 1080 | 4.41 | 88.22% | 5.40 |
| 1100 | 4.55 | 88.21% | 5.63 |
| 1120 | 4.40 | 88.25% | 5.37 |
| 1140 | 4.46 | 88.21% | 5.49 |
| 1160 | 4.52 | 88.22% | 5.58 |
| 1180 | 4.55 | 88.22% | 5.63 |
| 1200 | 4.61 | 88.24% | 5.73 |
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
