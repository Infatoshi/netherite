# Tape replay: scenario_trapdoor_toggle_20260730T104916Z

325 ticks, seed 0, world_time 6000, start (0.50,4.00,1.50).

**FIRST DIVERGENCE: tick 82, field `z`** oracle=4.910011038874917 magma=4.699999988079071 |d|=0.21; inputs {'f': 1.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 10.7822 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=17 independent=16 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=17 available=True
- world nearby_hash: checked=17 deltas=9 available=True

**Pixel gate: FAIL** over 14 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 14 | 2438297 | 211231 |
| bossbar | 14 | 350652 | 38430 |
| hud | 13 | 408562 | 34705 |
| particles | 4 | 42883 | 2079 |
| thinline | 4 | 2897 | 837 |
| viewmodel | 6 | 109862 | 14503 |

Failed frames (worst first, top 20):

- t=200: 285131 unexplained px, clusters [{'px': 73900, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 211231, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853]}]
- t=280: 283858 unexplained px, clusters [{'px': 74456, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 209402, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=300: 283858 unexplained px, clusters [{'px': 74456, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 209402, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=320: 283858 unexplained px, clusters [{'px': 74456, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 209402, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}]
- t=260: 280603 unexplained px, clusters [{'px': 74340, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 205010, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 853], 'soak_from': 'particles'}, {'px': 146, 'cls': 'UNEXPLAINED', 'bbox': [232, 20, 237, 67], 'soak_from': 'particles'}, {'px': 86, 'cls': 'UNEXPLAINED', 'bbox': [237, 0, 240, 34], 'soak_from': 'particles'}]
- t=220: 274426 unexplained px, clusters [{'px': 59856, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [217, 739, 219, 770], 'soak_from': 'viewmodel'}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [217, 807, 219, 839], 'soak_from': 'viewmodel'}, {'px': 1101, 'cls': 'UNEXPLAINED', 'bbox': [229, 707, 262, 836], 'soak_from': 'viewmodel'}]
- t=180: 251120 unexplained px, clusters [{'px': 30274, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 768], 'soak_from': 'viewmodel'}, {'px': 1729, 'cls': 'UNEXPLAINED', 'bbox': [193, 801, 235, 853], 'soak_from': 'viewmodel'}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [217, 616, 219, 646], 'soak_from': 'viewmodel'}, {'px': 1876, 'cls': 'UNEXPLAINED', 'bbox': [221, 632, 270, 734], 'soak_from': 'viewmodel'}]
- t=240: 246800 unexplained px, clusters [{'px': 63054, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 104, 'cls': 'UNEXPLAINED', 'bbox': [265, 445, 272, 472], 'soak_from': 'viewmodel'}, {'px': 147, 'cls': 'UNEXPLAINED', 'bbox': [275, 445, 278, 487], 'soak_from': 'viewmodel'}, {'px': 276, 'cls': 'UNEXPLAINED', 'bbox': [283, 445, 292, 490], 'soak_from': 'viewmodel'}]
- t=100: 69358 unexplained px, clusters [{'px': 280, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 51, 40], 'soak_from': 'particles'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [45, 154, 51, 174], 'soak_from': 'particles'}, {'px': 20906, 'cls': 'UNEXPLAINED', 'bbox': [45, 44, 314, 444], 'soak_from': 'particles'}, {'px': 13141, 'cls': 'UNEXPLAINED', 'bbox': [45, 619, 168, 853], 'soak_from': 'particles'}]
- t=120: 69358 unexplained px, clusters [{'px': 280, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 51, 40], 'soak_from': 'particles'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [45, 154, 51, 174], 'soak_from': 'particles'}, {'px': 20906, 'cls': 'UNEXPLAINED', 'bbox': [45, 44, 314, 444], 'soak_from': 'particles'}, {'px': 13141, 'cls': 'UNEXPLAINED', 'bbox': [45, 619, 168, 853], 'soak_from': 'particles'}]
- t=140: 69358 unexplained px, clusters [{'px': 280, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 51, 40], 'soak_from': 'particles'}, {'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [45, 154, 51, 174], 'soak_from': 'particles'}, {'px': 20906, 'cls': 'UNEXPLAINED', 'bbox': [45, 44, 314, 444], 'soak_from': 'particles'}, {'px': 13141, 'cls': 'UNEXPLAINED', 'bbox': [45, 619, 168, 853], 'soak_from': 'particles'}]
- t=80: 20589 unexplained px, clusters [{'px': 20394, 'cls': 'UNEXPLAINED', 'bbox': [241, 210, 383, 444]}, {'px': 195, 'cls': 'UNEXPLAINED', 'bbox': [349, 229, 358, 258]}]
- t=60: 13171 unexplained px, clusters [{'px': 4250, 'cls': 'UNEXPLAINED', 'bbox': [153, 77, 189, 235]}, {'px': 5604, 'cls': 'UNEXPLAINED', 'bbox': [153, 320, 256, 467]}, {'px': 3267, 'cls': 'UNEXPLAINED', 'bbox': [153, 615, 192, 754]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [158, 727, 162, 749]}]
- t=160: 6809 unexplained px, clusters [{'px': 136, 'cls': 'UNEXPLAINED', 'bbox': [45, 224, 51, 243]}, {'px': 466, 'cls': 'UNEXPLAINED', 'bbox': [52, 491, 72, 514]}, {'px': 221, 'cls': 'UNEXPLAINED', 'bbox': [94, 215, 113, 229]}, {'px': 610, 'cls': 'UNEXPLAINED', 'bbox': [135, 487, 150, 525]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 60 | 5.78 | 92.00% | 7.60 |
| 80 | 15.16 | 93.75% | 20.18 |
| 100 | 18.01 | 93.12% | 20.22 |
| 120 | 18.01 | 93.12% | 20.22 |
| 140 | 18.01 | 93.12% | 20.22 |
| 160 | 12.87 | 93.04% | 13.27 |
| 180 | 42.51 | 93.84% | 36.36 |
| 200 | 43.80 | 94.18% | 36.64 |
| 220 | 43.79 | 93.50% | 36.52 |
| 240 | 40.13 | 93.10% | 33.01 |
| 260 | 46.33 | 94.26% | 37.82 |
| 280 | 48.46 | 94.26% | 39.62 |
| 300 | 48.46 | 94.26% | 39.62 |
| 320 | 48.46 | 94.26% | 39.62 |
