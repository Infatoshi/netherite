# Tape replay: scenario_stone_stairs_profile_20260730T102657Z

369 ticks, seed 0, world_time 6000, start (0.50,4.00,-1.50).

**FIRST DIVERGENCE: tick 74, field `y`** oracle=4.5 magma=4 |d|=0.5; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 16.0160 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=19 independent=18 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=19 available=True
- world nearby_hash: checked=19 deltas=7 available=True

**Pixel gate: FAIL** over 19 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 19 | 491551 | 115335 |
| bossbar | 3 | 28512 | 9504 |
| hud | 16 | 192141 | 32665 |
| particles | 12 | 15959 | 1377 |
| thinline | 1 | 199 | 199 |
| viewmodel | 17 | 103847 | 28785 |

Failed frames (worst first, top 20):

- t=100: 173335 unexplained px, clusters [{'px': 57737, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [355, 606, 362, 621], 'soak_from': 'viewmodel'}, {'px': 115335, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 595], 'soak_from': 'particles'}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [375, 200, 383, 218], 'soak_from': 'particles'}]
- t=120: 158376 unexplained px, clusters [{'px': 49989, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 220, 'cls': 'UNEXPLAINED', 'bbox': [338, 556, 353, 576], 'soak_from': 'viewmodel'}, {'px': 98, 'cls': 'UNEXPLAINED', 'bbox': [360, 699, 362, 734], 'soak_from': 'viewmodel'}, {'px': 108069, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 595], 'soak_from': 'particles'}]
- t=80: 86208 unexplained px, clusters [{'px': 81080, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 595], 'soak_from': 'particles'}, {'px': 101, 'cls': 'UNEXPLAINED', 'bbox': [214, 388, 217, 444], 'soak_from': 'particles'}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [255, 251, 261, 271]}, {'px': 187, 'cls': 'UNEXPLAINED', 'bbox': [268, 246, 282, 271]}]
- t=140: 41550 unexplained px, clusters [{'px': 3792, 'cls': 'UNEXPLAINED', 'bbox': [198, 0, 215, 444]}, {'px': 37758, 'cls': 'UNEXPLAINED', 'bbox': [226, 86, 383, 444]}]
- t=160: 11355 unexplained px, clusters [{'px': 6583, 'cls': 'UNEXPLAINED', 'bbox': [200, 0, 225, 444]}, {'px': 2573, 'cls': 'UNEXPLAINED', 'bbox': [337, 263, 383, 331]}, {'px': 2199, 'cls': 'UNEXPLAINED', 'bbox': [337, 354, 383, 415]}]
- t=180: 4475 unexplained px, clusters [{'px': 4475, 'cls': 'UNEXPLAINED', 'bbox': [201, 0, 219, 444]}]
- t=200: 3851 unexplained px, clusters [{'px': 3851, 'cls': 'UNEXPLAINED', 'bbox': [199, 0, 217, 444]}]
- t=260: 971 unexplained px, clusters [{'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [320, 262, 326, 278]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [326, 72, 336, 86]}, {'px': 142, 'cls': 'UNEXPLAINED', 'bbox': [337, 0, 346, 29]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [342, 49, 349, 70]}]
- t=240: 875 unexplained px, clusters [{'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [320, 383, 324, 404]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [330, 347, 338, 359]}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [336, 360, 343, 383]}, {'px': 74, 'cls': 'UNEXPLAINED', 'bbox': [347, 133, 352, 156]}]
- t=320: 875 unexplained px, clusters [{'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [308, 49, 312, 84]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [322, 404, 328, 425]}, {'px': 93, 'cls': 'UNEXPLAINED', 'bbox': [337, 63, 349, 96]}, {'px': 61, 'cls': 'UNEXPLAINED', 'bbox': [344, 12, 350, 33]}]
- t=220: 865 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [316, 4, 322, 24]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [320, 174, 326, 196]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [322, 406, 326, 427]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [327, 94, 333, 109]}]
- t=360: 814 unexplained px, clusters [{'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [311, 174, 318, 190]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [332, 359, 338, 376]}, {'px': 95, 'cls': 'UNEXPLAINED', 'bbox': [336, 98, 349, 117]}, {'px': 91, 'cls': 'UNEXPLAINED', 'bbox': [337, 399, 349, 422]}]
- t=300: 756 unexplained px, clusters [{'px': 73, 'cls': 'UNEXPLAINED', 'bbox': [321, 99, 328, 122]}, {'px': 87, 'cls': 'UNEXPLAINED', 'bbox': [339, 103, 349, 125]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [342, 36, 349, 53]}, {'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [344, 246, 355, 258]}]
- t=340: 715 unexplained px, clusters [{'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [318, 319, 328, 332]}, {'px': 115, 'cls': 'UNEXPLAINED', 'bbox': [321, 100, 329, 139]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [342, 348, 349, 365]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [347, 388, 352, 405]}]
- t=280: 684 unexplained px, clusters [{'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [320, 418, 328, 438]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [321, 135, 324, 158]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [325, 74, 336, 86]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [332, 104, 338, 122]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 2.78 | 80.69% | 2.91 |
| 20 | 1.45 | 78.41% | 1.93 |
| 40 | 1.45 | 78.41% | 1.93 |
| 60 | 2.29 | 79.54% | 3.35 |
| 80 | 27.98 | 85.40% | 34.40 |
| 100 | 33.33 | 85.85% | 41.09 |
| 120 | 30.70 | 86.22% | 37.69 |
| 140 | 14.27 | 78.42% | 17.17 |
| 160 | 10.60 | 77.83% | 12.02 |
| 180 | 8.47 | 77.52% | 9.31 |
| 200 | 7.27 | 77.87% | 8.52 |
| 220 | 5.68 | 78.00% | 6.58 |
| 240 | 5.70 | 78.02% | 6.59 |
| 260 | 5.70 | 78.04% | 6.60 |
| 280 | 5.65 | 77.98% | 6.54 |
| 300 | 5.66 | 77.97% | 6.57 |
| 320 | 5.69 | 77.99% | 6.61 |
| 340 | 5.69 | 78.06% | 6.61 |
| 360 | 5.65 | 78.05% | 6.51 |
