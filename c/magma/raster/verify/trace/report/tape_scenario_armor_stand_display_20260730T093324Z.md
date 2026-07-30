# Tape replay: scenario_armor_stand_display_20260730T093324Z

407 ticks, seed 0, world_time 6000, start (-6.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=21 independent=20 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 21 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 21 | 707782 | 75438 |
| hud | 18 | 173841 | 7898 |
| particles | 10 | 10879 | 4804 |
| viewmodel | 18 | 167622 | 14201 |

Failed frames (worst first, top 20):

- t=0: 218333 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=20: 201630 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 201630 unexplained px, clusters [{'px': 42053, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 607], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=80: 12544 unexplained px, clusters [{'px': 6489, 'cls': 'UNEXPLAINED', 'bbox': [186, 0, 297, 124]}, {'px': 5668, 'cls': 'UNEXPLAINED', 'bbox': [186, 143, 295, 289]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [355, 75, 359, 96]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [355, 370, 360, 386]}]
- t=60: 12368 unexplained px, clusters [{'px': 11782, 'cls': 'UNEXPLAINED', 'bbox': [186, 0, 298, 306]}, {'px': 396, 'cls': 'UNEXPLAINED', 'bbox': [245, 0, 294, 30]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [311, 1, 317, 24]}, {'px': 76, 'cls': 'UNEXPLAINED', 'bbox': [365, 129, 371, 155]}]
- t=100: 12139 unexplained px, clusters [{'px': 11636, 'cls': 'UNEXPLAINED', 'bbox': [186, 0, 299, 287]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [323, 168, 331, 187]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [323, 327, 328, 348]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [325, 417, 330, 444]}]
- t=120: 11476 unexplained px, clusters [{'px': 6073, 'cls': 'UNEXPLAINED', 'bbox': [186, 0, 295, 242]}, {'px': 4890, 'cls': 'UNEXPLAINED', 'bbox': [186, 278, 298, 348]}, {'px': 240, 'cls': 'UNEXPLAINED', 'bbox': [186, 446, 192, 480]}, {'px': 62, 'cls': 'UNEXPLAINED', 'bbox': [215, 244, 224, 279]}]
- t=140: 11154 unexplained px, clusters [{'px': 5509, 'cls': 'UNEXPLAINED', 'bbox': [186, 146, 294, 311]}, {'px': 4515, 'cls': 'UNEXPLAINED', 'bbox': [186, 346, 298, 408]}, {'px': 275, 'cls': 'UNEXPLAINED', 'bbox': [186, 508, 192, 547]}, {'px': 301, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 220, 144]}]
- t=160: 8670 unexplained px, clusters [{'px': 5247, 'cls': 'UNEXPLAINED', 'bbox': [186, 128, 295, 333]}, {'px': 2493, 'cls': 'UNEXPLAINED', 'bbox': [186, 403, 297, 458]}, {'px': 313, 'cls': 'UNEXPLAINED', 'bbox': [186, 569, 192, 615]}, {'px': 285, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 222, 126]}]
- t=180: 6031 unexplained px, clusters [{'px': 4639, 'cls': 'UNEXPLAINED', 'bbox': [186, 311, 298, 392]}, {'px': 268, 'cls': 'UNEXPLAINED', 'bbox': [186, 488, 192, 526]}, {'px': 349, 'cls': 'UNEXPLAINED', 'bbox': [186, 631, 192, 682]}, {'px': 247, 'cls': 'UNEXPLAINED', 'bbox': [215, 148, 220, 277]}]
- t=200: 5424 unexplained px, clusters [{'px': 4081, 'cls': 'UNEXPLAINED', 'bbox': [186, 378, 297, 444]}, {'px': 300, 'cls': 'UNEXPLAINED', 'bbox': [186, 550, 192, 593]}, {'px': 384, 'cls': 'UNEXPLAINED', 'bbox': [186, 693, 192, 750]}, {'px': 252, 'cls': 'UNEXPLAINED', 'bbox': [215, 143, 220, 282]}]
- t=220: 2332 unexplained px, clusters [{'px': 250, 'cls': 'UNEXPLAINED', 'bbox': [186, 469, 192, 504]}, {'px': 336, 'cls': 'UNEXPLAINED', 'bbox': [186, 612, 192, 661]}, {'px': 423, 'cls': 'UNEXPLAINED', 'bbox': [186, 754, 192, 817]}, {'px': 537, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 222, 283]}]
- t=240: 1731 unexplained px, clusters [{'px': 293, 'cls': 'UNEXPLAINED', 'bbox': [186, 530, 192, 572]}, {'px': 375, 'cls': 'UNEXPLAINED', 'bbox': [186, 673, 192, 728]}, {'px': 239, 'cls': 'UNEXPLAINED', 'bbox': [186, 816, 192, 853]}, {'px': 594, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 223, 302]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 65.08 | 82.68% | 72.87 |
| 20 | 64.25 | 80.64% | 72.44 |
| 40 | 64.25 | 80.62% | 72.44 |
| 60 | 8.07 | 81.16% | 9.45 |
| 80 | 8.75 | 81.57% | 9.27 |
| 100 | 8.58 | 81.46% | 10.33 |
| 120 | 8.49 | 81.38% | 10.28 |
| 140 | 8.41 | 81.35% | 10.20 |
| 160 | 8.40 | 81.36% | 10.11 |
| 180 | 8.44 | 81.24% | 10.23 |
| 200 | 8.47 | 81.35% | 10.29 |
| 220 | 8.61 | 81.45% | 9.71 |
| 240 | 8.45 | 81.32% | 9.42 |
| 260 | 4.11 | 79.99% | 4.14 |
| 280 | 4.11 | 79.99% | 4.14 |
| 300 | 4.11 | 79.99% | 4.14 |
| 320 | 4.11 | 79.99% | 4.14 |
| 340 | 3.80 | 79.80% | 4.35 |
| 360 | 3.80 | 79.80% | 4.35 |
| 380 | 3.80 | 79.80% | 4.35 |
| 400 | 3.80 | 79.80% | 4.35 |
