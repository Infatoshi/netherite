# Tape replay: scenario_tnt_explosion_20260730T093124Z

303 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**Physics: clean.** No divergence (tol {'x': 1e-09, 'y': 1e-09, 'z': 1e-09, 'vx': 1e-09, 'vy': 1e-09, 'vz': 1e-09, 'og': 0, 'hp': 0.0001, 'food': 0, 'dim': 0}).

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=152 independent=151 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=16 available=True
- world nearby_hash: checked=16 deltas=1 available=True

**Pixel gate: FAIL** over 152 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 61 | 4584991 | 143603 |
| bossbar | 19 | 3405 | 178 |
| hud | 136 | 1230635 | 18368 |
| particles | 115 | 946077 | 23867 |
| viewmodel | 136 | 180127 | 9432 |

Skipped renderable entity rows (more than 4 fails the gate):

- `EntityTNTPrimed`: 77 rows (FAIL)

Failed frames (worst first, top 20):

- t=0: 296196 unexplained px, clusters [{'px': 65872, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=28: 295487 unexplained px, clusters [{'px': 65367, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=30: 295396 unexplained px, clusters [{'px': 65315, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [424, 508, 429, 525], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=2: 293504 unexplained px, clusters [{'px': 64091, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 853], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 330, 473, 361], 'soak_from': 'hud'}]
- t=4: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=6: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=8: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=10: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=12: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=14: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=16: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=18: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=20: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=22: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=24: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=26: 277951 unexplained px, clusters [{'px': 49423, 'cls': 'UNEXPLAINED', 'bbox': [384, 0, 479, 734], 'soak_from': 'hud'}, {'px': 4239, 'cls': 'UNEXPLAINED', 'bbox': [384, 712, 479, 817], 'soak_from': 'hud'}, {'px': 684, 'cls': 'UNEXPLAINED', 'bbox': [442, 250, 473, 281], 'soak_from': 'hud'}, {'px': 1024, 'cls': 'UNEXPLAINED', 'bbox': [442, 290, 473, 321], 'soak_from': 'hud'}]
- t=32: 13231 unexplained px, clusters [{'px': 13231, 'cls': 'UNEXPLAINED', 'bbox': [166, 367, 336, 468]}]
- t=38: 5919 unexplained px, clusters [{'px': 5919, 'cls': 'UNEXPLAINED', 'bbox': [182, 383, 283, 460]}]
- t=40: 5097 unexplained px, clusters [{'px': 5097, 'cls': 'UNEXPLAINED', 'bbox': [180, 386, 271, 458]}]
- t=42: 4422 unexplained px, clusters [{'px': 4422, 'cls': 'UNEXPLAINED', 'bbox': [177, 389, 260, 459]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 84.00 | 94.54% | 99.34 |
| 2 | 83.92 | 87.38% | 98.94 |
| 4 | 81.28 | 83.56% | 96.62 |
| 6 | 81.28 | 83.52% | 96.62 |
| 8 | 81.28 | 83.48% | 96.62 |
| 10 | 81.28 | 83.46% | 96.62 |
| 12 | 81.28 | 83.44% | 96.62 |
| 14 | 81.28 | 83.43% | 96.62 |
| 16 | 81.51 | 90.53% | 96.90 |
| 18 | 81.49 | 83.66% | 96.88 |
| 20 | 81.49 | 83.64% | 96.88 |
| 22 | 81.49 | 83.63% | 96.88 |
| 24 | 81.49 | 83.62% | 96.88 |
| 26 | 81.49 | 83.61% | 96.88 |
| 28 | 88.62 | 87.50% | 102.85 |
| 30 | 89.08 | 87.50% | 103.38 |
| 32 | 10.24 | 86.45% | 11.92 |
| 34 | 6.78 | 84.17% | 9.36 |
| 36 | 5.58 | 83.73% | 7.39 |
| 38 | 4.07 | 83.04% | 4.91 |
| 40 | 3.86 | 83.19% | 4.53 |
| 42 | 3.61 | 82.77% | 4.16 |
| 44 | 3.93 | 82.50% | 4.71 |
| 46 | 3.63 | 82.05% | 4.26 |
| 48 | 3.03 | 82.30% | 3.29 |
| 50 | 2.96 | 82.54% | 3.14 |
| 52 | 2.86 | 82.60% | 2.96 |
| 54 | 3.08 | 82.26% | 3.35 |
| 56 | 3.00 | 82.23% | 3.23 |
| 58 | 2.72 | 82.14% | 2.76 |
| 60 | 2.74 | 81.49% | 2.78 |
| 62 | 2.62 | 81.63% | 2.61 |
| 64 | 2.75 | 81.44% | 2.81 |
| 66 | 2.70 | 81.49% | 2.72 |
| 68 | 2.54 | 81.32% | 2.42 |
| 70 | 2.61 | 81.41% | 2.51 |
| 72 | 2.47 | 81.31% | 2.35 |
| 74 | 2.59 | 81.44% | 2.53 |
| 76 | 2.57 | 81.42% | 2.46 |
| 78 | 2.46 | 81.38% | 2.30 |
| 80 | 2.44 | 81.40% | 2.31 |
| 82 | 2.43 | 81.38% | 2.29 |
| 84 | 2.53 | 81.38% | 2.44 |
| 86 | 2.55 | 81.39% | 2.47 |
| 88 | 2.46 | 81.39% | 2.32 |
| 90 | 2.46 | 81.39% | 2.32 |
| 92 | 2.46 | 81.39% | 2.32 |
| 94 | 2.55 | 81.39% | 2.47 |
| 96 | 2.55 | 81.39% | 2.47 |
| 98 | 2.46 | 81.39% | 2.32 |
| 100 | 2.46 | 81.39% | 2.32 |
| 102 | 2.46 | 81.39% | 2.32 |
| 104 | 4.32 | 81.50% | 5.47 |
| 106 | 6.46 | 82.41% | 9.10 |
| 108 | 7.05 | 82.89% | 10.09 |
| 110 | 7.10 | 83.33% | 10.16 |
| 112 | 7.17 | 83.39% | 10.26 |
| 114 | 6.86 | 83.27% | 9.72 |
| 116 | 6.35 | 83.17% | 8.77 |
| 118 | 5.90 | 83.04% | 7.93 |
| 120 | 5.61 | 82.90% | 7.41 |
| 122 | 5.40 | 82.69% | 7.06 |
| 124 | 5.12 | 82.55% | 6.60 |
| 126 | 4.98 | 82.42% | 6.35 |
| 128 | 4.80 | 82.32% | 6.03 |
| 130 | 4.69 | 82.26% | 5.86 |
| 132 | 4.59 | 82.15% | 5.66 |
| 134 | 4.45 | 82.02% | 5.48 |
| 136 | 4.40 | 81.97% | 5.42 |
| 138 | 4.35 | 81.87% | 5.36 |
| 140 | 4.29 | 81.79% | 5.28 |
| 142 | 4.22 | 81.74% | 5.21 |
| 144 | 4.20 | 81.68% | 5.16 |
| 146 | 4.15 | 81.65% | 5.10 |
| 148 | 4.12 | 81.63% | 5.03 |
| 150 | 4.08 | 81.60% | 4.98 |
| 152 | 4.06 | 81.59% | 4.96 |
| 154 | 4.04 | 81.58% | 4.92 |
| 156 | 4.02 | 81.55% | 4.91 |
| 158 | 4.01 | 81.54% | 4.90 |
| 160 | 4.00 | 81.52% | 4.89 |
| 162 | 3.99 | 81.51% | 4.88 |
| 164 | 3.99 | 81.50% | 4.87 |
| 166 | 3.98 | 81.49% | 4.87 |
| 168 | 3.97 | 81.49% | 4.86 |
| 170 | 3.97 | 81.48% | 4.86 |
| 172 | 3.97 | 81.48% | 4.86 |
| 174 | 3.96 | 81.48% | 4.85 |
| 176 | 3.96 | 81.48% | 4.85 |
| 178 | 3.96 | 81.48% | 4.85 |
| 180 | 3.96 | 81.47% | 4.85 |
| 182 | 3.96 | 81.47% | 4.85 |
| 184 | 3.95 | 81.47% | 4.85 |
| 186 | 3.95 | 81.47% | 4.85 |
| 188 | 3.95 | 81.47% | 4.85 |
| 190 | 3.95 | 81.47% | 4.85 |
| 192 | 3.95 | 81.47% | 4.85 |
| 194 | 3.95 | 81.47% | 4.85 |
| 196 | 3.95 | 81.47% | 4.85 |
| 198 | 3.95 | 81.47% | 4.85 |
| 200 | 3.95 | 81.47% | 4.85 |
| 202 | 3.95 | 81.47% | 4.85 |
| 204 | 3.95 | 81.47% | 4.85 |
| 206 | 3.95 | 81.47% | 4.85 |
| 208 | 3.95 | 81.47% | 4.85 |
| 210 | 3.95 | 81.47% | 4.85 |
| 212 | 3.95 | 81.47% | 4.85 |
| 214 | 3.95 | 81.47% | 4.85 |
| 216 | 3.95 | 81.47% | 4.85 |
| 218 | 3.95 | 81.47% | 4.85 |
| 220 | 3.95 | 81.47% | 4.85 |
| 222 | 3.95 | 81.47% | 4.85 |
| 224 | 3.95 | 81.47% | 4.85 |
| 226 | 3.95 | 81.47% | 4.85 |
| 228 | 3.95 | 81.47% | 4.85 |
| 230 | 3.95 | 81.47% | 4.85 |
| 232 | 3.95 | 81.47% | 4.85 |
| 234 | 3.95 | 81.47% | 4.85 |
| 236 | 3.95 | 81.47% | 4.85 |
| 238 | 3.95 | 81.47% | 4.85 |
| 240 | 3.95 | 81.47% | 4.85 |
| 242 | 3.95 | 81.47% | 4.85 |
| 244 | 3.95 | 81.47% | 4.85 |
| 246 | 3.95 | 81.47% | 4.85 |
| 248 | 3.95 | 81.47% | 4.85 |
| 250 | 3.95 | 81.47% | 4.85 |
| 252 | 3.95 | 81.47% | 4.85 |
| 254 | 3.95 | 81.47% | 4.85 |
| 256 | 3.95 | 81.47% | 4.85 |
| 258 | 3.95 | 81.47% | 4.85 |
| 260 | 3.95 | 81.47% | 4.85 |
| 262 | 3.95 | 81.47% | 4.85 |
| 264 | 3.95 | 81.47% | 4.85 |
| 266 | 3.95 | 81.47% | 4.85 |
| 268 | 3.95 | 81.47% | 4.85 |
| 270 | 3.95 | 81.47% | 4.85 |
| 272 | 3.95 | 81.47% | 4.85 |
| 274 | 3.95 | 81.47% | 4.85 |
| 276 | 3.95 | 81.47% | 4.85 |
| 278 | 3.95 | 81.47% | 4.85 |
| 280 | 3.95 | 81.47% | 4.85 |
| 282 | 3.95 | 81.47% | 4.85 |
| 284 | 3.95 | 81.47% | 4.85 |
| 286 | 3.95 | 81.47% | 4.85 |
| 288 | 3.95 | 81.47% | 4.85 |
| 290 | 3.95 | 81.47% | 4.85 |
| 292 | 3.95 | 81.47% | 4.85 |
| 294 | 3.95 | 81.47% | 4.85 |
| 296 | 3.95 | 81.47% | 4.85 |
| 298 | 3.95 | 81.47% | 4.85 |
| 300 | 3.95 | 81.47% | 4.85 |
| 302 | 3.95 | 81.47% | 4.85 |
