# Tape replay: scenario_ender_dragon_20260722T093713Z

1609 ticks, seed 0, world_time 6000, start (100.00,49.00,0.00).

**FIRST DIVERGENCE: tick 72, field `x`** oracle=99.16770917015579 magma=99.30000001192093 |d|=0.132; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 1.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=3 independent=2 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=81 available=True
- world nearby_hash: checked=81 deltas=10 available=True

**Pixel gate: FAIL** over 77 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 53 | 337542 | 9740 |
| bossbar | 12 | 2034 | 238 |
| hud | 57 | 285618 | 5365 |
| particles | 57 | 190194 | 2350 |
| thinline | 1 | 133 | 133 |
| viewmodel | 56 | 145870 | 11192 |

Failed frames (worst first, top 20):

- t=1020: 16318 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 459, 'cls': 'UNEXPLAINED', 'bbox': [81, 151, 103, 182]}, {'px': 251, 'cls': 'UNEXPLAINED', 'bbox': [91, 201, 113, 216]}, {'px': 358, 'cls': 'UNEXPLAINED', 'bbox': [93, 381, 115, 412]}]
- t=1000: 16209 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 890, 'cls': 'UNEXPLAINED', 'bbox': [81, 151, 113, 216]}, {'px': 358, 'cls': 'UNEXPLAINED', 'bbox': [93, 381, 115, 412]}, {'px': 588, 'cls': 'UNEXPLAINED', 'bbox': [115, 249, 146, 282]}]
- t=980: 14545 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 421, 'cls': 'UNEXPLAINED', 'bbox': [91, 184, 113, 216]}, {'px': 498, 'cls': 'UNEXPLAINED', 'bbox': [115, 249, 146, 281]}, {'px': 1170, 'cls': 'UNEXPLAINED', 'bbox': [148, 478, 192, 530]}]
- t=960: 11789 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 588, 'cls': 'UNEXPLAINED', 'bbox': [115, 249, 146, 282]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 339, 'cls': 'UNEXPLAINED', 'bbox': [180, 503, 192, 530]}]
- t=940: 11633 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 182, 'cls': 'UNEXPLAINED', 'bbox': [139, 249, 146, 281]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}]
- t=1320: 11543 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=920: 11228 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [51, 536, 53, 571]}, {'px': 588, 'cls': 'UNEXPLAINED', 'bbox': [115, 249, 146, 282]}, {'px': 329, 'cls': 'UNEXPLAINED', 'bbox': [173, 274, 221, 288]}, {'px': 208, 'cls': 'UNEXPLAINED', 'bbox': [180, 478, 192, 498]}]
- t=1300: 10658 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=1280: 10330 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=1240: 9322 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=1260: 9245 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=1220: 8893 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=1200: 8780 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=880: 8654 unexplained px, clusters [{'px': 289, 'cls': 'UNEXPLAINED', 'bbox': [174, 483, 192, 502]}, {'px': 211, 'cls': 'UNEXPLAINED', 'bbox': [174, 505, 192, 517]}, {'px': 528, 'cls': 'UNEXPLAINED', 'bbox': [209, 341, 237, 376]}, {'px': 363, 'cls': 'UNEXPLAINED', 'bbox': [245, 402, 263, 444]}]
- t=1160: 8582 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=900: 8511 unexplained px, clusters [{'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [47, 538, 48, 574]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [93, 519, 109, 524]}, {'px': 225, 'cls': 'UNEXPLAINED', 'bbox': [179, 479, 192, 499]}, {'px': 112, 'cls': 'UNEXPLAINED', 'bbox': [179, 504, 192, 511]}]
- t=680: 8207 unexplained px, clusters [{'px': 1331, 'cls': 'UNEXPLAINED', 'bbox': [150, 567, 192, 614]}, {'px': 667, 'cls': 'UNEXPLAINED', 'bbox': [151, 789, 175, 849]}, {'px': 188, 'cls': 'UNEXPLAINED', 'bbox': [172, 547, 192, 563]}, {'px': 84, 'cls': 'UNEXPLAINED', 'bbox': [181, 534, 192, 542]}]
- t=1180: 8177 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]
- t=700: 8130 unexplained px, clusters [{'px': 1331, 'cls': 'UNEXPLAINED', 'bbox': [150, 567, 192, 614]}, {'px': 102, 'cls': 'UNEXPLAINED', 'bbox': [151, 789, 156, 805]}, {'px': 188, 'cls': 'UNEXPLAINED', 'bbox': [172, 547, 192, 563]}, {'px': 84, 'cls': 'UNEXPLAINED', 'bbox': [181, 534, 192, 542]}]
- t=1140: 7718 unexplained px, clusters [{'px': 271, 'cls': 'UNEXPLAINED', 'bbox': [54, 539, 69, 558]}, {'px': 248, 'cls': 'UNEXPLAINED', 'bbox': [70, 481, 105, 495]}, {'px': 270, 'cls': 'UNEXPLAINED', 'bbox': [84, 509, 105, 530]}, {'px': 100, 'cls': 'UNEXPLAINED', 'bbox': [125, 492, 141, 502]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 80 | 2.42 | 92.09% | 2.46 |
| 100 | 2.42 | 92.09% | 2.46 |
| 120 | 2.42 | 92.09% | 2.46 |
| 140 | 2.42 | 92.09% | 2.46 |
| 160 | 2.42 | 92.09% | 2.46 |
| 180 | 2.42 | 92.09% | 2.46 |
| 200 | 2.23 | 93.06% | 2.29 |
| 220 | 2.09 | 92.93% | 1.99 |
| 240 | 1.85 | 93.27% | 1.64 |
| 260 | 1.50 | 94.13% | 1.23 |
| 280 | 1.03 | 94.22% | 0.94 |
| 300 | 0.91 | 94.15% | 0.96 |
| 320 | 0.91 | 93.77% | 0.96 |
| 340 | 0.91 | 93.33% | 0.96 |
| 360 | 1.61 | 92.08% | 2.05 |
| 380 | 3.40 | 92.69% | 4.51 |
| 400 | 5.22 | 93.15% | 7.06 |
| 420 | 6.01 | 93.05% | 7.11 |
| 440 | 5.40 | 92.40% | 6.66 |
| 460 | 5.39 | 91.41% | 6.79 |
| 480 | 5.96 | 91.70% | 7.30 |
| 500 | 7.19 | 92.93% | 9.18 |
| 520 | 5.26 | 92.96% | 4.86 |
| 540 | 6.01 | 93.31% | 6.49 |
| 560 | 6.30 | 92.84% | 7.03 |
| 580 | 6.56 | 93.96% | 7.42 |
| 600 | 7.10 | 93.96% | 8.37 |
| 620 | 7.35 | 91.97% | 8.75 |
| 640 | 7.59 | 93.96% | 9.13 |
| 660 | 8.05 | 93.96% | 9.51 |
| 680 | 8.67 | 93.96% | 10.31 |
| 700 | 8.59 | 93.73% | 10.32 |
| 720 | 3.68 | 94.43% | 3.90 |
| 740 | 4.91 | 92.50% | 5.84 |
| 760 | 5.00 | 93.51% | 6.08 |
| 780 | 4.74 | 92.85% | 5.73 |
| 800 | 4.94 | 93.10% | 6.23 |
| 820 | 5.29 | 93.65% | 6.87 |
| 840 | 5.02 | 94.03% | 6.42 |
| 860 | 5.00 | 94.35% | 6.32 |
| 880 | 5.22 | 94.42% | 6.65 |
| 900 | 4.94 | 94.43% | 6.25 |
| 920 | 5.28 | 94.46% | 6.83 |
| 940 | 5.35 | 94.14% | 6.93 |
| 960 | 5.36 | 94.00% | 6.95 |
| 980 | 5.96 | 94.55% | 7.86 |
| 1000 | 6.27 | 94.43% | 8.22 |
| 1020 | 6.30 | 94.37% | 8.30 |
| 1040 | 5.63 | 94.17% | 6.64 |
| 1060 | 5.73 | 94.25% | 6.70 |
| 1080 | 5.84 | 94.32% | 6.85 |
| 1100 | 5.84 | 94.28% | 6.86 |
| 1120 | 5.83 | 94.28% | 6.83 |
| 1140 | 5.89 | 94.30% | 6.94 |
| 1160 | 5.90 | 94.30% | 6.95 |
| 1180 | 5.89 | 94.30% | 6.93 |
| 1200 | 5.91 | 94.24% | 6.96 |
| 1220 | 5.97 | 94.31% | 7.07 |
| 1240 | 5.96 | 94.28% | 7.05 |
| 1260 | 6.01 | 94.29% | 7.14 |
| 1280 | 6.07 | 94.28% | 7.24 |
| 1300 | 6.11 | 94.31% | 7.31 |
| 1320 | 6.19 | 94.30% | 7.44 |
| 1340 | 7.11 | 94.62% | 6.70 |
| 1360 | 5.04 | 93.47% | 5.55 |
| 1380 | 5.07 | 93.49% | 5.54 |
| 1400 | 5.06 | 93.50% | 5.54 |
| 1420 | 5.07 | 93.50% | 5.55 |
| 1440 | 5.09 | 93.50% | 5.57 |
| 1460 | 5.08 | 93.49% | 5.56 |
| 1480 | 5.10 | 93.49% | 5.59 |
| 1500 | 5.11 | 93.50% | 5.61 |
| 1520 | 5.11 | 93.49% | 5.61 |
| 1540 | 5.12 | 93.50% | 5.64 |
| 1560 | 5.13 | 93.49% | 5.66 |
| 1580 | 5.14 | 93.50% | 5.66 |
| 1600 | 5.17 | 93.50% | 5.72 |
