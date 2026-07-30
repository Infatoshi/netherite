# Tape replay: scenario_glass_pane_connections_20260730T102612Z

308 ticks, seed 0, world_time 6000, start (-7.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=16 independent=15 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=5 available=True

**Pixel gate: FAIL** over 16 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 6 | 605020 | 72276 |
| hud | 2 | 15442 | 5551 |
| particles | 13 | 25038 | 1963 |
| viewmodel | 13 | 9090 | 1346 |

Failed frames (worst first, top 20):

- t=0: 212714 unexplained px, clusters [{'px': 64814, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=20: 195937 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=40: 195937 unexplained px, clusters [{'px': 41979, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 602], 'soak_from': 'hud'}, {'px': 9565, 'cls': 'UNEXPLAINED', 'bbox': [384, 737, 479, 853], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 51.22 | 82.61% | 57.76 |
| 20 | 49.93 | 80.33% | 56.78 |
| 40 | 49.93 | 80.33% | 56.78 |
| 60 | 1.07 | 78.08% | 1.19 |
| 80 | 1.14 | 78.09% | 1.18 |
| 100 | 1.25 | 78.13% | 1.24 |
| 120 | 1.38 | 78.14% | 1.45 |
| 140 | 1.41 | 78.15% | 1.75 |
| 160 | 1.39 | 78.07% | 1.79 |
| 180 | 1.39 | 78.13% | 1.80 |
| 200 | 1.39 | 78.06% | 1.79 |
| 220 | 1.35 | 77.98% | 1.76 |
| 240 | 1.35 | 77.98% | 1.76 |
| 260 | 2.16 | 78.00% | 2.87 |
| 280 | 2.63 | 78.50% | 2.12 |
| 300 | 2.84 | 78.00% | 1.91 |
