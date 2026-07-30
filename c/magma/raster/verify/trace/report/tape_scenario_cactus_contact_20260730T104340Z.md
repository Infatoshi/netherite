# Tape replay: scenario_cactus_contact_20260730T104340Z

245 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**FIRST DIVERGENCE: tick 47, field `z`** oracle=6.716169705746736 magma=6.699999988079071 |d|=0.0162; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 0.0625 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=25 independent=24 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=13 available=True
- world nearby_hash: checked=13 deltas=5 available=True

**Pixel gate: FAIL** over 25 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 21 | 923777 | 209786 |
| bossbar | 5 | 121478 | 37027 |
| hud | 22 | 279780 | 34630 |
| particles | 20 | 86389 | 17831 |
| thinline | 1 | 296 | 296 |
| viewmodel | 22 | 127518 | 14420 |

Failed frames (worst first, top 20):

- t=50: 281657 unexplained px, clusters [{'px': 72401, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 209256, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=60: 279791 unexplained px, clusters [{'px': 70005, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 209786, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=80: 164792 unexplained px, clusters [{'px': 164792, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 563], 'soak_from': 'particles'}]
- t=70: 102529 unexplained px, clusters [{'px': 51479, 'cls': 'UNEXPLAINED', 'bbox': [193, 481, 383, 853], 'soak_from': 'viewmodel'}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [282, 445, 289, 476], 'soak_from': 'viewmodel'}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [326, 446, 330, 459], 'soak_from': 'viewmodel'}, {'px': 105, 'cls': 'UNEXPLAINED', 'bbox': [369, 459, 375, 497], 'soak_from': 'viewmodel'}]
- t=40: 63849 unexplained px, clusters [{'px': 41722, 'cls': 'UNEXPLAINED', 'bbox': [45, 335, 383, 513], 'soak_from': 'particles'}, {'px': 22127, 'cls': 'UNEXPLAINED', 'bbox': [146, 0, 383, 112], 'soak_from': 'particles'}]
- t=120: 2372 unexplained px, clusters [{'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [299, 16, 305, 51]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [309, 31, 318, 52]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [310, 0, 319, 23]}, {'px': 85, 'cls': 'UNEXPLAINED', 'bbox': [316, 373, 334, 396]}]
- t=110: 2196 unexplained px, clusters [{'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [300, 428, 311, 444]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [315, 55, 319, 75]}, {'px': 105, 'cls': 'UNEXPLAINED', 'bbox': [317, 355, 334, 381]}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [320, 428, 334, 444]}]
- t=130: 2145 unexplained px, clusters [{'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [310, 13, 316, 33]}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [321, 27, 329, 53]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [332, 155, 338, 177]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [335, 309, 344, 325]}]
- t=140: 1954 unexplained px, clusters [{'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [303, 88, 311, 105]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [323, 333, 330, 349]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [323, 388, 333, 413]}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [326, 112, 330, 131]}]
- t=170: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=180: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=190: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=200: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=210: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=220: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=230: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=240: 1939 unexplained px, clusters [{'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [297, 118, 306, 138]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [318, 128, 324, 151]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [324, 371, 334, 387]}, {'px': 81, 'cls': 'UNEXPLAINED', 'bbox': [328, 423, 338, 441]}]
- t=100: 1914 unexplained px, clusters [{'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [299, 418, 311, 439]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [305, 50, 313, 81]}, {'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [316, 416, 334, 441]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [327, 143, 336, 156]}]
- t=160: 1873 unexplained px, clusters [{'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [316, 64, 322, 90]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [316, 120, 325, 133]}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [332, 99, 337, 130]}, {'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [332, 150, 341, 177]}]
- t=150: 1852 unexplained px, clusters [{'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [297, 47, 307, 75]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [315, 253, 322, 276]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [325, 175, 329, 194]}, {'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [330, 343, 340, 366]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 3.38 | 88.16% | 3.88 |
| 10 | 2.00 | 77.19% | 2.86 |
| 20 | 2.19 | 77.50% | 3.18 |
| 30 | 4.21 | 78.61% | 6.62 |
| 40 | 9.26 | 81.89% | 9.96 |
| 50 | 36.16 | 93.47% | 37.22 |
| 60 | 35.86 | 93.06% | 36.78 |
| 70 | 13.70 | 81.71% | 15.31 |
| 80 | 27.47 | 88.44% | 31.26 |
| 90 | 8.83 | 78.02% | 6.94 |
| 100 | 5.86 | 78.08% | 6.60 |
| 110 | 5.68 | 78.19% | 6.42 |
| 120 | 5.52 | 78.20% | 6.25 |
| 130 | 5.54 | 78.10% | 6.38 |
| 140 | 5.50 | 78.18% | 6.26 |
| 150 | 5.59 | 78.17% | 6.35 |
| 160 | 6.10 | 78.57% | 6.53 |
| 170 | 6.18 | 78.72% | 6.79 |
| 180 | 6.18 | 78.72% | 6.79 |
| 190 | 6.18 | 78.72% | 6.79 |
| 200 | 6.18 | 78.72% | 6.79 |
| 210 | 6.18 | 78.72% | 6.79 |
| 220 | 6.18 | 78.72% | 6.79 |
| 230 | 6.18 | 78.72% | 6.79 |
| 240 | 6.18 | 78.72% | 6.79 |
