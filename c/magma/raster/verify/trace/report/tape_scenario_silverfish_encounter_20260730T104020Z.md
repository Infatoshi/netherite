# Tape replay: scenario_silverfish_encounter_20260730T104020Z

367 ticks, seed 0, world_time 6000, start (0.50,4.00,1.50).

**FIRST DIVERGENCE: tick 49, field `hp`** oracle=19.2 magma=19.6000004 |d|=0.4; inputs {'f': 0.0, 's': 0.3, 'jump': 0, 'sneak': 1, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0000 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=37 independent=36 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=19 available=True
- world nearby_hash: checked=19 deltas=4 available=True

**Pixel gate: FAIL** over 37 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 30 | 221736 | 11176 |
| bossbar | 1 | 127 | 127 |
| hud | 37 | 328684 | 16534 |
| particles | 32 | 96402 | 8168 |
| thinline | 8 | 71938 | 14375 |
| viewmodel | 29 | 199696 | 6659 |

Skipped renderable entity rows (more than 4 fails the gate):

- `EntitySilverfish`: 1101 rows (FAIL)

Failed frames (worst first, top 20):

- t=50: 24580 unexplained px, clusters [{'px': 11176, 'cls': 'UNEXPLAINED', 'bbox': [103, 0, 231, 640]}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [126, 342, 145, 352]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [141, 437, 151, 450]}, {'px': 59, 'cls': 'UNEXPLAINED', 'bbox': [193, 219, 201, 237]}]
- t=60: 16222 unexplained px, clusters [{'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [184, 395, 190, 428]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [185, 283, 189, 309]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [190, 408, 197, 442]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [191, 237, 197, 276]}]
- t=30: 12584 unexplained px, clusters [{'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [175, 144, 186, 188]}, {'px': 114, 'cls': 'UNEXPLAINED', 'bbox': [183, 88, 197, 140]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [185, 28, 190, 48]}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [186, 147, 194, 174]}]
- t=40: 12487 unexplained px, clusters [{'px': 92, 'cls': 'UNEXPLAINED', 'bbox': [201, 186, 207, 228]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [201, 226, 207, 273]}, {'px': 101, 'cls': 'UNEXPLAINED', 'bbox': [201, 372, 207, 419]}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [204, 280, 207, 318]}]
- t=20: 11860 unexplained px, clusters [{'px': 105, 'cls': 'UNEXPLAINED', 'bbox': [209, 363, 217, 408]}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [213, 154, 217, 198]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [213, 206, 217, 250]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [213, 259, 217, 302]}]
- t=10: 10843 unexplained px, clusters [{'px': 105, 'cls': 'UNEXPLAINED', 'bbox': [209, 363, 217, 408]}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [213, 154, 217, 198]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [213, 206, 217, 250]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [213, 259, 217, 302]}]
- t=180: 10801 unexplained px, clusters [{'px': 7470, 'cls': 'UNEXPLAINED', 'bbox': [116, 248, 192, 853]}, {'px': 88, 'cls': 'UNEXPLAINED', 'bbox': [175, 735, 184, 759]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [175, 773, 184, 791]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [175, 807, 182, 825]}]
- t=130: 10673 unexplained px, clusters [{'px': 8374, 'cls': 'UNEXPLAINED', 'bbox': [115, 0, 192, 853]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [184, 744, 192, 763]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [244, 420, 250, 442]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [260, 62, 270, 86]}]
- t=140: 10143 unexplained px, clusters [{'px': 7691, 'cls': 'UNEXPLAINED', 'bbox': [115, 254, 192, 853]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [184, 731, 192, 749]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [266, 17, 272, 54]}, {'px': 59, 'cls': 'UNEXPLAINED', 'bbox': [275, 22, 279, 44]}]
- t=0: 9564 unexplained px, clusters [{'px': 765, 'cls': 'UNEXPLAINED', 'bbox': [204, 350, 239, 444]}, {'px': 838, 'cls': 'UNEXPLAINED', 'bbox': [209, 290, 244, 354]}, {'px': 75, 'cls': 'UNEXPLAINED', 'bbox': [213, 154, 217, 198]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [213, 206, 217, 250]}]
- t=120: 8857 unexplained px, clusters [{'px': 7008, 'cls': 'UNEXPLAINED', 'bbox': [134, 344, 192, 770]}, {'px': 71, 'cls': 'UNEXPLAINED', 'bbox': [280, 412, 292, 424]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [310, 67, 320, 81]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [310, 85, 328, 104]}]
- t=190: 7376 unexplained px, clusters [{'px': 6158, 'cls': 'UNEXPLAINED', 'bbox': [137, 574, 192, 853]}, {'px': 125, 'cls': 'UNEXPLAINED', 'bbox': [312, 413, 332, 432]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [314, 19, 322, 37]}, {'px': 126, 'cls': 'UNEXPLAINED', 'bbox': [319, 276, 339, 297]}]
- t=270: 7060 unexplained px, clusters [{'px': 3780, 'cls': 'UNEXPLAINED', 'bbox': [120, 589, 174, 853]}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [254, 20, 262, 45]}, {'px': 110, 'cls': 'UNEXPLAINED', 'bbox': [266, 79, 276, 114]}, {'px': 104, 'cls': 'UNEXPLAINED', 'bbox': [268, 159, 278, 190]}]
- t=150: 7027 unexplained px, clusters [{'px': 5970, 'cls': 'UNEXPLAINED', 'bbox': [141, 511, 192, 782]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [301, 132, 307, 146]}, {'px': 59, 'cls': 'UNEXPLAINED', 'bbox': [302, 406, 315, 415]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [317, 274, 324, 286]}]
- t=200: 6924 unexplained px, clusters [{'px': 5688, 'cls': 'UNEXPLAINED', 'bbox': [137, 593, 192, 853]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [258, 0, 267, 27]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [277, 57, 285, 79]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [296, 409, 309, 426]}]
- t=230: 6729 unexplained px, clusters [{'px': 4652, 'cls': 'UNEXPLAINED', 'bbox': [124, 621, 191, 853]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [286, 210, 295, 225]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [301, 376, 309, 387]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [310, 10, 317, 33]}]
- t=260: 5688 unexplained px, clusters [{'px': 4719, 'cls': 'UNEXPLAINED', 'bbox': [125, 596, 184, 853]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [267, 52, 277, 84]}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [300, 344, 308, 370]}, {'px': 158, 'cls': 'UNEXPLAINED', 'bbox': [312, 239, 335, 258]}]
- t=80: 4945 unexplained px, clusters [{'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [185, 392, 191, 427]}, {'px': 124, 'cls': 'UNEXPLAINED', 'bbox': [203, 395, 216, 440]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [211, 282, 216, 314]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [213, 340, 216, 371]}]
- t=70: 4807 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [211, 420, 217, 444]}, {'px': 244, 'cls': 'UNEXPLAINED', 'bbox': [222, 369, 243, 444]}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [233, 273, 235, 318]}, {'px': 83, 'cls': 'UNEXPLAINED', 'bbox': [233, 322, 235, 366]}]
- t=240: 4787 unexplained px, clusters [{'px': 3168, 'cls': 'UNEXPLAINED', 'bbox': [156, 621, 192, 853]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [217, 33, 223, 55]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [276, 181, 288, 195]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [290, 85, 297, 102]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 9.96 | 81.75% | 11.38 |
| 10 | 8.70 | 79.59% | 10.54 |
| 20 | 8.79 | 79.52% | 10.69 |
| 30 | 8.67 | 80.84% | 10.54 |
| 40 | 8.93 | 79.33% | 11.02 |
| 50 | 12.04 | 79.82% | 15.23 |
| 60 | 12.06 | 79.03% | 15.13 |
| 70 | 10.50 | 78.08% | 13.90 |
| 80 | 10.18 | 78.79% | 13.06 |
| 90 | 9.66 | 79.63% | 12.54 |
| 100 | 8.94 | 78.70% | 10.92 |
| 110 | 8.57 | 78.63% | 10.90 |
| 120 | 8.49 | 77.67% | 10.78 |
| 130 | 8.30 | 78.78% | 10.47 |
| 140 | 8.31 | 78.77% | 10.36 |
| 150 | 8.27 | 77.90% | 10.30 |
| 160 | 7.89 | 78.66% | 9.64 |
| 170 | 8.37 | 80.51% | 10.00 |
| 180 | 7.91 | 78.61% | 9.63 |
| 190 | 7.84 | 77.54% | 9.42 |
| 200 | 7.73 | 77.54% | 9.21 |
| 210 | 7.69 | 78.47% | 9.22 |
| 220 | 7.62 | 78.23% | 9.07 |
| 230 | 7.71 | 78.10% | 8.96 |
| 240 | 7.65 | 78.68% | 9.04 |
| 250 | 8.23 | 78.43% | 10.11 |
| 260 | 8.02 | 78.03% | 9.86 |
| 270 | 8.27 | 78.43% | 9.64 |
| 280 | 8.10 | 78.34% | 9.71 |
| 290 | 1.96 | 78.09% | 1.45 |
| 300 | 1.29 | 77.49% | 1.31 |
| 310 | 1.60 | 78.16% | 1.28 |
| 320 | 1.88 | 77.94% | 1.74 |
| 330 | 1.44 | 77.59% | 1.38 |
| 340 | 2.21 | 78.33% | 2.07 |
| 350 | 1.41 | 77.70% | 1.18 |
| 360 | 2.82 | 79.92% | 2.39 |
