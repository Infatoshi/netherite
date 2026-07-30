# Tape replay: scenario_minecart_states_20260730T092748Z

406 ticks, seed 0, world_time 6000, start (-7.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=21 independent=20 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 21 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 14 | 154387 | 28418 |
| hud | 21 | 212757 | 16944 |
| particles | 14 | 122726 | 26163 |
| viewmodel | 19 | 354858 | 28703 |

Failed frames (worst first, top 20):

- t=100: 31003 unexplained px, clusters [{'px': 195, 'cls': 'UNEXPLAINED', 'bbox': [215, 147, 220, 244]}, {'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [215, 246, 217, 333]}, {'px': 290, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 221, 145]}, {'px': 23697, 'cls': 'UNEXPLAINED', 'bbox': [229, 0, 333, 334]}]
- t=160: 29542 unexplained px, clusters [{'px': 569, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 220, 315]}, {'px': 21662, 'cls': 'UNEXPLAINED', 'bbox': [228, 20, 333, 329]}, {'px': 6486, 'cls': 'UNEXPLAINED', 'bbox': [230, 323, 335, 428]}, {'px': 125, 'cls': 'UNEXPLAINED', 'bbox': [258, 181, 267, 204]}]
- t=120: 29219 unexplained px, clusters [{'px': 504, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 223, 263]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [215, 265, 218, 303]}, {'px': 28418, 'cls': 'UNEXPLAINED', 'bbox': [229, 0, 333, 395]}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [269, 161, 275, 174]}]
- t=60: 22459 unexplained px, clusters [{'px': 595, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 223, 292]}, {'px': 21617, 'cls': 'UNEXPLAINED', 'bbox': [229, 0, 333, 309]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [269, 171, 275, 180]}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [358, 0, 367, 14]}]
- t=180: 19806 unexplained px, clusters [{'px': 304, 'cls': 'UNEXPLAINED', 'bbox': [215, 129, 221, 297]}, {'px': 273, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 224, 127]}, {'px': 18949, 'cls': 'UNEXPLAINED', 'bbox': [231, 114, 335, 387]}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [342, 115, 350, 139]}]
- t=220: 7491 unexplained px, clusters [{'px': 275, 'cls': 'UNEXPLAINED', 'bbox': [215, 146, 221, 308]}, {'px': 50, 'cls': 'UNEXPLAINED', 'bbox': [215, 322, 220, 357]}, {'px': 293, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 222, 139]}, {'px': 6809, 'cls': 'UNEXPLAINED', 'bbox': [231, 315, 333, 426]}]
- t=200: 5864 unexplained px, clusters [{'px': 247, 'cls': 'UNEXPLAINED', 'bbox': [215, 149, 220, 285]}, {'px': 53, 'cls': 'UNEXPLAINED', 'bbox': [215, 305, 219, 343]}, {'px': 294, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 223, 147]}, {'px': 5028, 'cls': 'UNEXPLAINED', 'bbox': [236, 371, 333, 444]}]
- t=140: 5598 unexplained px, clusters [{'px': 265, 'cls': 'UNEXPLAINED', 'bbox': [215, 147, 219, 296]}, {'px': 286, 'cls': 'UNEXPLAINED', 'bbox': [216, 0, 222, 145]}, {'px': 4534, 'cls': 'UNEXPLAINED', 'bbox': [232, 377, 334, 444]}, {'px': 125, 'cls': 'UNEXPLAINED', 'bbox': [269, 85, 280, 104]}]
- t=240: 1178 unexplained px, clusters [{'px': 585, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 222, 319]}, {'px': 52, 'cls': 'UNEXPLAINED', 'bbox': [306, 28, 313, 49]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [333, 16, 341, 38]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [344, 52, 355, 72]}]
- t=80: 1059 unexplained px, clusters [{'px': 531, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 222, 268]}, {'px': 65, 'cls': 'UNEXPLAINED', 'bbox': [258, 0, 270, 11]}, {'px': 126, 'cls': 'UNEXPLAINED', 'bbox': [269, 93, 280, 110]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [276, 0, 281, 13]}]
- t=260: 718 unexplained px, clusters [{'px': 549, 'cls': 'UNEXPLAINED', 'bbox': [215, 0, 222, 278]}, {'px': 51, 'cls': 'UNEXPLAINED', 'bbox': [305, 49, 311, 69]}, {'px': 58, 'cls': 'UNEXPLAINED', 'bbox': [316, 128, 326, 145]}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [344, 23, 351, 47]}]
- t=0: 150 unexplained px, clusters [{'px': 61, 'cls': 'UNEXPLAINED', 'bbox': [263, 0, 275, 8]}, {'px': 89, 'cls': 'UNEXPLAINED', 'bbox': [271, 89, 283, 100]}]
- t=20: 150 unexplained px, clusters [{'px': 61, 'cls': 'UNEXPLAINED', 'bbox': [263, 0, 275, 8]}, {'px': 89, 'cls': 'UNEXPLAINED', 'bbox': [271, 89, 283, 100]}]
- t=40: 150 unexplained px, clusters [{'px': 61, 'cls': 'UNEXPLAINED', 'bbox': [263, 0, 275, 8]}, {'px': 89, 'cls': 'UNEXPLAINED', 'bbox': [271, 89, 283, 100]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 5.11 | 81.58% | 4.77 |
| 20 | 3.81 | 79.30% | 3.80 |
| 40 | 3.81 | 79.30% | 3.80 |
| 60 | 7.95 | 80.38% | 8.81 |
| 80 | 8.29 | 80.37% | 9.27 |
| 100 | 8.52 | 80.51% | 9.77 |
| 120 | 8.85 | 80.43% | 10.21 |
| 140 | 9.25 | 80.50% | 10.80 |
| 160 | 9.56 | 80.55% | 11.92 |
| 180 | 9.59 | 80.55% | 11.96 |
| 200 | 9.32 | 80.41% | 11.13 |
| 220 | 8.92 | 80.42% | 10.60 |
| 240 | 8.65 | 80.41% | 9.84 |
| 260 | 8.41 | 80.37% | 9.46 |
| 280 | 4.58 | 79.08% | 5.06 |
| 300 | 4.58 | 79.08% | 5.06 |
| 320 | 4.58 | 79.08% | 5.06 |
| 340 | 4.58 | 79.08% | 5.06 |
| 360 | 4.58 | 79.08% | 5.06 |
| 380 | 4.58 | 79.08% | 5.06 |
| 400 | 4.58 | 79.08% | 5.06 |
