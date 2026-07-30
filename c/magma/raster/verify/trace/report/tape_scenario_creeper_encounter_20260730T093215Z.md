# Tape replay: scenario_creeper_encounter_20260730T093215Z

403 ticks, seed 0, world_time 6000, start (0.50,4.00,0.50).

**FIRST DIVERGENCE: tick 109, field `x`** oracle=0.48600126150995493 magma=0.5 |d|=0.014; inputs {'f': 0.0, 's': 0.0, 'jump': 0, 'sneak': 0, 'sprint': 0, 'atk': 0, 'use': 0, 'hb': 0}. End-of-tape euclid 2.2421 blocks.

**State gate** (inventory / entities / world hash; not physics):

- inventory: checked=202 independent=201 seeded_only=False mismatches=0 available=True pass=True
- entities: checked=21 available=True
- world nearby_hash: checked=21 deltas=0 available=True

**Pixel gate: FAIL** over 202 frames.

| class | frames | px | max cluster |
|---|---|---|---|
| UNEXPLAINED | 171 | 17356709 | 168643 |
| bossbar | 147 | 1338185 | 20686 |
| hud | 154 | 4170558 | 31854 |
| particles | 3 | 1439 | 1320 |
| viewmodel | 141 | 1259051 | 18449 |

Failed frames (worst first, top 20):

- t=116: 242374 unexplained px, clusters [{'px': 61013, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 746, 'cls': 'UNEXPLAINED', 'bbox': [193, 596, 220, 644], 'soak_from': 'viewmodel'}, {'px': 106, 'cls': 'UNEXPLAINED', 'bbox': [193, 646, 200, 669], 'soak_from': 'viewmodel'}, {'px': 166269, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 613], 'soak_from': 'particles'}]
- t=118: 232770 unexplained px, clusters [{'px': 61052, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 823], 'soak_from': 'viewmodel'}, {'px': 87, 'cls': 'UNEXPLAINED', 'bbox': [363, 840, 375, 853], 'soak_from': 'viewmodel'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [378, 833, 383, 853], 'soak_from': 'viewmodel'}, {'px': 168643, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 684], 'soak_from': 'particles'}]
- t=120: 230559 unexplained px, clusters [{'px': 61779, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 848], 'soak_from': 'viewmodel'}, {'px': 168092, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 744], 'soak_from': 'particles'}, {'px': 116, 'cls': 'UNEXPLAINED', 'bbox': [45, 736, 61, 747], 'soak_from': 'particles'}, {'px': 54, 'cls': 'UNEXPLAINED', 'bbox': [54, 363, 64, 370], 'soak_from': 'particles'}]
- t=114: 220089 unexplained px, clusters [{'px': 63366, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 156234, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 714], 'soak_from': 'particles'}, {'px': 129, 'cls': 'UNEXPLAINED', 'bbox': [71, 397, 85, 412], 'soak_from': 'particles'}, {'px': 126, 'cls': 'UNEXPLAINED', 'bbox': [94, 713, 111, 729], 'soak_from': 'particles'}]
- t=122: 207866 unexplained px, clusters [{'px': 50459, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 845], 'soak_from': 'viewmodel'}, {'px': 167, 'cls': 'UNEXPLAINED', 'bbox': [273, 649, 286, 671], 'soak_from': 'viewmodel'}, {'px': 463, 'cls': 'UNEXPLAINED', 'bbox': [277, 689, 305, 724], 'soak_from': 'viewmodel'}, {'px': 69, 'cls': 'UNEXPLAINED', 'bbox': [292, 656, 301, 670], 'soak_from': 'viewmodel'}]
- t=112: 187517 unexplained px, clusters [{'px': 50940, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 136507, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 691], 'soak_from': 'particles'}, {'px': 70, 'cls': 'UNEXPLAINED', 'bbox': [205, 89, 216, 101], 'soak_from': 'particles'}]
- t=110: 171289 unexplained px, clusters [{'px': 40045, 'cls': 'UNEXPLAINED', 'bbox': [193, 445, 383, 853], 'soak_from': 'viewmodel'}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [193, 570, 198, 580], 'soak_from': 'viewmodel'}, {'px': 130503, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 108, 'cls': 'UNEXPLAINED', 'bbox': [129, 0, 149, 10]}]
- t=124: 134771 unexplained px, clusters [{'px': 118156, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1775, 'cls': 'UNEXPLAINED', 'bbox': [45, 473, 98, 544], 'soak_from': 'particles'}, {'px': 82, 'cls': 'UNEXPLAINED', 'bbox': [45, 580, 53, 602], 'soak_from': 'particles'}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [47, 385, 55, 392], 'soak_from': 'particles'}]
- t=126: 132237 unexplained px, clusters [{'px': 114808, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 79, 'cls': 'UNEXPLAINED', 'bbox': [45, 431, 54, 443], 'soak_from': 'particles'}, {'px': 1081, 'cls': 'UNEXPLAINED', 'bbox': [45, 475, 84, 523], 'soak_from': 'particles'}, {'px': 257, 'cls': 'UNEXPLAINED', 'bbox': [45, 635, 66, 660], 'soak_from': 'particles'}]
- t=128: 130386 unexplained px, clusters [{'px': 114106, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 221, 'cls': 'UNEXPLAINED', 'bbox': [45, 259, 57, 293], 'soak_from': 'particles'}, {'px': 240, 'cls': 'UNEXPLAINED', 'bbox': [45, 351, 59, 366], 'soak_from': 'particles'}, {'px': 563, 'cls': 'UNEXPLAINED', 'bbox': [45, 483, 71, 525], 'soak_from': 'particles'}]
- t=130: 126025 unexplained px, clusters [{'px': 113293, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1146, 'cls': 'UNEXPLAINED', 'bbox': [45, 316, 88, 385], 'soak_from': 'particles'}, {'px': 481, 'cls': 'UNEXPLAINED', 'bbox': [45, 628, 73, 660], 'soak_from': 'particles'}, {'px': 498, 'cls': 'UNEXPLAINED', 'bbox': [47, 750, 80, 776]}]
- t=132: 123992 unexplained px, clusters [{'px': 112317, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 555, 'cls': 'UNEXPLAINED', 'bbox': [45, 330, 60, 386], 'soak_from': 'particles'}, {'px': 68, 'cls': 'UNEXPLAINED', 'bbox': [45, 654, 48, 670], 'soak_from': 'particles'}, {'px': 316, 'cls': 'UNEXPLAINED', 'bbox': [45, 755, 71, 774]}]
- t=134: 122147 unexplained px, clusters [{'px': 112673, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 275, 'cls': 'UNEXPLAINED', 'bbox': [45, 577, 69, 596], 'soak_from': 'particles'}, {'px': 252, 'cls': 'UNEXPLAINED', 'bbox': [45, 760, 62, 779]}, {'px': 64, 'cls': 'UNEXPLAINED', 'bbox': [56, 371, 63, 378]}]
- t=136: 120597 unexplained px, clusters [{'px': 113015, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 160, 'cls': 'UNEXPLAINED', 'bbox': [45, 579, 57, 599], 'soak_from': 'particles'}, {'px': 175, 'cls': 'UNEXPLAINED', 'bbox': [50, 303, 69, 317], 'soak_from': 'particles'}, {'px': 1059, 'cls': 'UNEXPLAINED', 'bbox': [65, 432, 119, 477], 'soak_from': 'particles'}]
- t=138: 119486 unexplained px, clusters [{'px': 112581, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 1060, 'cls': 'UNEXPLAINED', 'bbox': [52, 433, 105, 478], 'soak_from': 'particles'}, {'px': 169, 'cls': 'UNEXPLAINED', 'bbox': [69, 595, 83, 617], 'soak_from': 'particles'}, {'px': 56, 'cls': 'UNEXPLAINED', 'bbox': [86, 523, 92, 530], 'soak_from': 'particles'}]
- t=140: 117556 unexplained px, clusters [{'px': 111819, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 745, 'cls': 'UNEXPLAINED', 'bbox': [45, 433, 92, 463], 'soak_from': 'particles'}, {'px': 380, 'cls': 'UNEXPLAINED', 'bbox': [57, 265, 75, 284], 'soak_from': 'particles'}, {'px': 112, 'cls': 'UNEXPLAINED', 'bbox': [57, 597, 71, 611], 'soak_from': 'particles'}]
- t=142: 117276 unexplained px, clusters [{'px': 111884, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 260, 'cls': 'UNEXPLAINED', 'bbox': [45, 264, 57, 283], 'soak_from': 'particles'}, {'px': 575, 'cls': 'UNEXPLAINED', 'bbox': [45, 434, 78, 464], 'soak_from': 'particles'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [45, 598, 59, 613], 'soak_from': 'particles'}]
- t=144: 115473 unexplained px, clusters [{'px': 111874, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 330, 'cls': 'UNEXPLAINED', 'bbox': [45, 434, 65, 464], 'soak_from': 'particles'}, {'px': 66, 'cls': 'UNEXPLAINED', 'bbox': [69, 697, 81, 705]}, {'px': 57, 'cls': 'UNEXPLAINED', 'bbox': [70, 739, 78, 751]}]
- t=146: 114906 unexplained px, clusters [{'px': 111782, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 120, 'cls': 'UNEXPLAINED', 'bbox': [45, 450, 52, 464], 'soak_from': 'particles'}, {'px': 60, 'cls': 'UNEXPLAINED', 'bbox': [61, 740, 69, 752]}, {'px': 63, 'cls': 'UNEXPLAINED', 'bbox': [63, 698, 74, 706]}]
- t=148: 114368 unexplained px, clusters [{'px': 111445, 'cls': 'UNEXPLAINED', 'bbox': [45, 0, 383, 444], 'soak_from': 'particles'}, {'px': 265, 'cls': 'UNEXPLAINED', 'bbox': [47, 351, 65, 379], 'soak_from': 'particles'}, {'px': 265, 'cls': 'UNEXPLAINED', 'bbox': [55, 831, 77, 853], 'soak_from': 'particles'}, {'px': 72, 'cls': 'UNEXPLAINED', 'bbox': [56, 698, 68, 707]}]

| tick | whole mean/ch | %diff | terrain mean/ch |
|---|---|---|---|
| 0 | 2.45 | 88.90% | 2.21 |
| 2 | 1.11 | 81.23% | 1.20 |
| 4 | 1.11 | 81.24% | 1.20 |
| 6 | 1.11 | 81.25% | 1.20 |
| 8 | 1.11 | 81.25% | 1.20 |
| 10 | 1.11 | 81.26% | 1.20 |
| 12 | 1.11 | 81.26% | 1.20 |
| 14 | 1.11 | 81.26% | 1.20 |
| 16 | 1.14 | 86.74% | 1.23 |
| 18 | 1.12 | 81.33% | 1.21 |
| 20 | 1.12 | 81.34% | 1.21 |
| 22 | 1.12 | 81.35% | 1.21 |
| 24 | 1.12 | 81.35% | 1.21 |
| 26 | 1.15 | 81.36% | 1.26 |
| 28 | 1.17 | 81.35% | 1.29 |
| 30 | 1.16 | 81.34% | 1.28 |
| 32 | 1.15 | 81.34% | 1.26 |
| 34 | 1.17 | 81.34% | 1.29 |
| 36 | 1.19 | 81.35% | 1.33 |
| 38 | 1.20 | 81.35% | 1.34 |
| 40 | 1.18 | 81.36% | 1.31 |
| 42 | 1.17 | 81.35% | 1.30 |
| 44 | 1.21 | 81.36% | 1.37 |
| 46 | 1.23 | 81.36% | 1.40 |
| 48 | 1.21 | 81.35% | 1.36 |
| 50 | 1.13 | 81.32% | 1.23 |
| 52 | 1.20 | 81.35% | 1.34 |
| 54 | 1.24 | 81.36% | 1.42 |
| 56 | 1.23 | 81.35% | 1.40 |
| 58 | 1.14 | 81.30% | 1.24 |
| 60 | 1.26 | 81.35% | 1.44 |
| 62 | 1.31 | 81.35% | 1.53 |
| 64 | 1.31 | 81.34% | 1.53 |
| 66 | 1.21 | 81.22% | 1.37 |
| 68 | 1.31 | 81.30% | 1.54 |
| 70 | 1.45 | 81.33% | 1.76 |
| 72 | 1.49 | 81.32% | 1.83 |
| 74 | 1.39 | 81.26% | 1.64 |
| 76 | 1.33 | 81.24% | 1.50 |
| 78 | 1.50 | 81.34% | 1.60 |
| 80 | 1.46 | 81.40% | 1.52 |
| 82 | 1.26 | 81.60% | 1.38 |
| 84 | 1.25 | 81.65% | 1.38 |
| 86 | 1.33 | 81.29% | 1.51 |
| 88 | 1.24 | 81.66% | 1.38 |
| 90 | 1.25 | 81.66% | 1.40 |
| 92 | 1.33 | 81.24% | 1.53 |
| 94 | 1.25 | 81.71% | 1.40 |
| 96 | 1.34 | 81.33% | 1.54 |
| 98 | 1.36 | 81.36% | 1.57 |
| 100 | 1.27 | 81.92% | 1.43 |
| 102 | 1.32 | 81.45% | 1.51 |
| 104 | 1.33 | 81.48% | 1.53 |
| 106 | 1.31 | 82.43% | 1.49 |
| 108 | 1.35 | 81.76% | 1.56 |
| 110 | 60.45 | 86.02% | 73.77 |
| 112 | 61.68 | 87.38% | 75.01 |
| 114 | 70.07 | 88.74% | 81.95 |
| 116 | 63.59 | 91.73% | 71.47 |
| 118 | 65.95 | 91.85% | 78.25 |
| 120 | 64.96 | 90.16% | 77.12 |
| 122 | 55.02 | 87.88% | 62.32 |
| 124 | 45.40 | 85.66% | 49.52 |
| 126 | 43.87 | 85.03% | 46.70 |
| 128 | 42.38 | 84.73% | 44.77 |
| 130 | 40.36 | 84.15% | 42.50 |
| 132 | 38.67 | 83.86% | 41.22 |
| 134 | 37.80 | 83.62% | 40.40 |
| 136 | 36.81 | 83.24% | 39.47 |
| 138 | 36.50 | 82.97% | 39.34 |
| 140 | 35.75 | 82.77% | 38.48 |
| 142 | 35.34 | 82.73% | 38.10 |
| 144 | 34.02 | 82.66% | 36.13 |
| 146 | 33.59 | 82.59% | 35.67 |
| 148 | 33.26 | 82.50% | 35.40 |
| 150 | 33.01 | 82.30% | 35.16 |
| 152 | 32.94 | 82.20% | 35.08 |
| 154 | 32.93 | 82.11% | 35.02 |
| 156 | 32.91 | 82.14% | 34.98 |
| 158 | 32.81 | 82.08% | 34.83 |
| 160 | 32.78 | 82.11% | 34.82 |
| 162 | 32.79 | 82.10% | 34.88 |
| 164 | 32.74 | 82.09% | 34.82 |
| 166 | 32.71 | 82.08% | 34.79 |
| 168 | 32.66 | 82.08% | 34.75 |
| 170 | 32.62 | 82.08% | 34.69 |
| 172 | 32.62 | 82.08% | 34.68 |
| 174 | 32.62 | 82.08% | 34.66 |
| 176 | 32.62 | 82.08% | 34.66 |
| 178 | 32.60 | 82.08% | 34.65 |
| 180 | 32.57 | 82.08% | 34.61 |
| 182 | 32.57 | 82.08% | 34.61 |
| 184 | 32.56 | 82.08% | 34.58 |
| 186 | 32.56 | 82.08% | 34.59 |
| 188 | 32.54 | 82.08% | 34.56 |
| 190 | 32.55 | 82.08% | 34.54 |
| 192 | 32.56 | 82.08% | 34.55 |
| 194 | 32.57 | 82.08% | 34.55 |
| 196 | 32.58 | 82.08% | 34.54 |
| 198 | 32.58 | 82.07% | 34.54 |
| 200 | 32.57 | 82.08% | 34.54 |
| 202 | 32.55 | 82.08% | 34.54 |
| 204 | 32.54 | 82.08% | 34.54 |
| 206 | 32.53 | 82.08% | 34.54 |
| 208 | 32.54 | 82.08% | 34.55 |
| 210 | 32.54 | 82.08% | 34.55 |
| 212 | 32.53 | 82.06% | 34.54 |
| 214 | 32.51 | 82.08% | 34.54 |
| 216 | 32.51 | 82.08% | 34.54 |
| 218 | 32.51 | 82.08% | 34.55 |
| 220 | 32.52 | 82.08% | 34.55 |
| 222 | 32.53 | 82.08% | 34.55 |
| 224 | 32.53 | 82.08% | 34.55 |
| 226 | 32.53 | 82.08% | 34.54 |
| 228 | 32.54 | 82.08% | 34.54 |
| 230 | 32.54 | 82.08% | 34.54 |
| 232 | 32.54 | 82.08% | 34.54 |
| 234 | 32.55 | 82.08% | 34.54 |
| 236 | 32.57 | 82.08% | 34.55 |
| 238 | 32.56 | 82.08% | 34.55 |
| 240 | 32.56 | 82.08% | 34.56 |
| 242 | 32.55 | 82.08% | 34.55 |
| 244 | 32.54 | 82.08% | 34.55 |
| 246 | 32.54 | 82.07% | 34.55 |
| 248 | 32.54 | 82.08% | 34.55 |
| 250 | 32.54 | 82.08% | 34.54 |
| 252 | 32.54 | 82.08% | 34.54 |
| 254 | 32.55 | 82.08% | 34.55 |
| 256 | 32.55 | 82.05% | 34.55 |
| 258 | 32.56 | 82.08% | 34.55 |
| 260 | 32.55 | 82.07% | 34.54 |
| 262 | 32.55 | 82.08% | 34.54 |
| 264 | 32.55 | 82.08% | 34.54 |
| 266 | 32.54 | 82.08% | 34.54 |
| 268 | 32.54 | 82.08% | 34.54 |
| 270 | 32.53 | 82.08% | 34.54 |
| 272 | 32.53 | 82.08% | 34.55 |
| 274 | 32.51 | 82.08% | 34.54 |
| 276 | 32.50 | 82.08% | 34.53 |
| 278 | 32.50 | 82.08% | 34.54 |
| 280 | 32.51 | 82.01% | 34.54 |
| 282 | 32.53 | 82.08% | 34.55 |
| 284 | 32.53 | 82.05% | 34.55 |
| 286 | 32.53 | 82.08% | 34.55 |
| 288 | 32.53 | 82.08% | 34.55 |
| 290 | 32.52 | 82.08% | 34.54 |
| 292 | 32.51 | 82.07% | 34.54 |
| 294 | 32.51 | 82.08% | 34.54 |
| 296 | 32.52 | 82.08% | 34.54 |
| 298 | 32.53 | 82.08% | 34.55 |
| 300 | 32.54 | 82.08% | 34.55 |
| 302 | 32.54 | 82.08% | 34.55 |
| 304 | 32.53 | 82.08% | 34.55 |
| 306 | 32.53 | 82.04% | 34.55 |
| 308 | 32.52 | 82.08% | 34.55 |
| 310 | 32.53 | 82.08% | 34.55 |
| 312 | 32.53 | 82.08% | 34.55 |
| 314 | 32.54 | 82.08% | 34.54 |
| 316 | 32.55 | 82.08% | 34.55 |
| 318 | 32.56 | 82.08% | 34.55 |
| 320 | 32.57 | 82.08% | 34.55 |
| 322 | 32.58 | 82.08% | 34.54 |
| 324 | 32.58 | 82.08% | 34.54 |
| 326 | 32.57 | 82.08% | 34.54 |
| 328 | 32.55 | 82.08% | 34.55 |
| 330 | 32.54 | 82.07% | 34.54 |
| 332 | 32.53 | 82.08% | 34.54 |
| 334 | 32.54 | 82.08% | 34.55 |
| 336 | 32.53 | 82.08% | 34.54 |
| 338 | 32.52 | 82.08% | 34.54 |
| 340 | 32.51 | 82.07% | 34.54 |
| 342 | 32.51 | 82.08% | 34.55 |
| 344 | 32.52 | 82.07% | 34.55 |
| 346 | 32.52 | 82.08% | 34.55 |
| 348 | 32.53 | 82.08% | 34.55 |
| 350 | 32.53 | 82.01% | 34.55 |
| 352 | 32.54 | 82.08% | 34.54 |
| 354 | 32.54 | 82.06% | 34.54 |
| 356 | 32.54 | 82.08% | 34.54 |
| 358 | 32.54 | 82.08% | 34.54 |
| 360 | 32.55 | 82.08% | 34.55 |
| 362 | 32.56 | 82.08% | 34.55 |
| 364 | 32.56 | 82.08% | 34.55 |
| 366 | 32.56 | 82.08% | 34.56 |
| 368 | 32.55 | 82.08% | 34.55 |
| 370 | 32.54 | 82.08% | 34.55 |
| 372 | 32.54 | 82.07% | 34.55 |
| 374 | 32.54 | 82.06% | 34.55 |
| 376 | 32.54 | 82.08% | 34.54 |
| 378 | 32.55 | 82.07% | 34.54 |
| 380 | 32.55 | 82.08% | 34.54 |
| 382 | 32.55 | 82.08% | 34.55 |
| 384 | 32.56 | 82.08% | 34.55 |
| 386 | 32.55 | 82.07% | 34.54 |
| 388 | 32.55 | 82.08% | 34.54 |
| 390 | 32.55 | 82.08% | 34.55 |
| 392 | 32.54 | 82.08% | 34.54 |
| 394 | 32.53 | 82.08% | 34.54 |
| 396 | 32.53 | 82.08% | 34.55 |
| 398 | 32.52 | 82.08% | 34.55 |
| 400 | 32.51 | 82.08% | 34.54 |
| 402 | 32.50 | 82.08% | 34.54 |
