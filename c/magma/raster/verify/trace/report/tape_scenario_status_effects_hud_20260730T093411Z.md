# Tape replay: scenario_status_effects_hud_20260730T093411Z

406 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=21 independent=20 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 21 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 21 | 15939 | 103 |
| bossbar | 21 | 35664 | 864 |
| hud | 21 | 26869 | 13338 |
| particles | 21 | 1444 | 69 |
| viewmodel | 21 | 13484 | 3614 |

Failed frames (worst first, top 20):

- t=0: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=20: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=40: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=60: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=80: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=100: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=120: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=140: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=160: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=180: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=200: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=220: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=240: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=260: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=280: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=300: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=320: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=340: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=360: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]
- t=380: 759 unexplained px, clusters [{'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [298, 70, 309, 83]}, {'px': 55, 'cls': 'UNEXPLAINED', 'bbox': [327, 113, 334, 139]}, {'px': 67, 'cls': 'UNEXPLAINED', 'bbox': [337, 12, 346, 35]}, {'px': 103, 'cls': 'UNEXPLAINED', 'bbox': [344, 422, 362, 444]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 6.16 | 89.80% | 6.12 |
| 20 | 4.81 | 86.04% | 5.10 |
| 40 | 4.83 | 76.68% | 5.12 |
| 60 | 4.83 | 76.68% | 5.12 |
| 80 | 4.79 | 76.65% | 5.07 |
| 100 | 4.79 | 76.64% | 5.07 |
| 120 | 4.79 | 76.65% | 5.07 |
| 140 | 4.83 | 76.68% | 5.12 |
| 160 | 4.83 | 76.68% | 5.12 |
| 180 | 4.79 | 76.65% | 5.07 |
| 200 | 4.79 | 76.65% | 5.07 |
| 220 | 4.79 | 76.65% | 5.07 |
| 240 | 4.83 | 76.68% | 5.12 |
| 260 | 4.83 | 76.68% | 5.12 |
| 280 | 4.79 | 76.65% | 5.07 |
| 300 | 4.79 | 76.65% | 5.07 |
| 320 | 4.79 | 76.65% | 5.07 |
| 340 | 4.83 | 76.68% | 5.12 |
| 360 | 4.83 | 76.68% | 5.12 |
| 380 | 4.62 | 76.66% | 5.07 |
| 400 | 4.50 | 76.70% | 5.07 |
