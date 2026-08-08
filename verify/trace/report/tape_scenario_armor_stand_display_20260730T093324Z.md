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
| UNEXPLAINED | 11 | 630512 | 77525 |
| particles | 9 | 836 | 129 |
| viewmodel | 18 | 6034 | 202 |

Failed frames (worst first, top 20):

- t=0: 220420 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=20: 203643 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 203643 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 63.94 | 83.19% | 71.95 |
| 20 | 63.11 | 81.15% | 71.53 |
| 40 | 63.11 | 81.12% | 71.53 |
| 60 | 1.18 | 80.04% | 1.41 |
| 80 | 1.33 | 80.62% | 1.37 |
| 100 | 1.29 | 80.60% | 1.61 |
| 120 | 1.29 | 80.27% | 1.59 |
| 140 | 1.29 | 80.80% | 1.61 |
| 160 | 1.24 | 80.62% | 1.54 |
| 180 | 1.27 | 80.18% | 1.56 |
| 200 | 1.26 | 80.42% | 1.59 |
| 220 | 1.29 | 80.37% | 1.45 |
| 240 | 1.26 | 80.05% | 1.37 |
| 260 | 1.29 | 79.95% | 1.32 |
| 280 | 1.29 | 79.95% | 1.32 |
| 300 | 1.29 | 79.95% | 1.32 |
| 320 | 1.29 | 79.95% | 1.32 |
| 340 | 1.18 | 79.77% | 1.34 |
| 360 | 1.18 | 79.77% | 1.34 |
| 380 | 1.18 | 79.77% | 1.34 |
| 400 | 1.18 | 79.77% | 1.34 |
