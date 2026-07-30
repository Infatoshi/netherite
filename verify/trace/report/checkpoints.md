# Checkpoint pixel parity: REAL MC 1.11.2 vs magma

Same seed-0 world, same pose (feet + MC yaw/pitch), same 854x480 FOV-70 camera. Oracle = qrl in-process frame grab (llvmpipe SW GL); magma = headless `--script` render, frame after 2 ticks. Diff = abs per-channel over RGB; `%diff` = fraction of pixels with any channel delta > 0. Regions: whole frame; terrain crop (rows 14-86%, cols 9-91%: excludes sky top + HUD strip); HUD strip (bottom 10%).

| checkpoint | pose (feet x,y,z yaw/pitch, time) | whole mean/ch | whole %diff | terrain mean/ch | terrain %diff | hud mean/ch | hud %diff |
|---|---|---|---|---|---|---|---|
| spawn_plains | (44.5, 68.0, 176.5) 0/5 t=6000 | 8.57 | 55.81% | 7.39 | 50.12% | 23.46 | 85.48% |
| water_bank | (-28.5, 64.0, 316.5) -41/20 t=6000 | 5.47 | 66.41% | 2.72 | 59.81% | 25.47 | 77.72% |
| forest_canopy | (-102.8, 73.0, 83.0) 45/-10 t=6000 | 21.49 | 89.91% | 20.98 | 87.85% | 31.72 | 98.84% |
| beach_sand | (74.5, 67.0, 74.5) -86/10 t=6000 | 9.88 | 58.15% | 3.40 | 49.05% | 49.80 | 75.54% |
| mountain_vista | (89.8, 111.0, 266.9) 153/10 t=6000 | 11.68 | 93.26% | 7.32 | 92.18% | 49.52 | 88.17% |
| underground_cave | (-99.5, 39.0, 66.5) -90/0 t=6000 | 6.22 | 30.99% | 2.90 | 32.68% | 34.65 | 62.30% |
| snow_slope | (68.5, 106.0, 180.5) -160/10 t=6000 | 7.16 | 94.63% | 4.14 | 94.32% | 30.01 | 87.50% |
| night_spawn | (44.5, 68.0, 176.5) 0/5 t=18000 | 6.29 | 51.63% | 3.48 | 46.38% | 31.46 | 84.51% |
| **mean over 8** | | 9.60 | 67.60% | 6.54 | 64.05% | 34.51 | 82.51% |

## spawn_plains

Seed-0 world spawn column (extreme-hills edge, grass). Feet on ground y=68 (surface 67).

Oracle readback: null

![side-by-side](ck_spawn_plains_sbs.png)

![heatmap](ck_spawn_plains_heat.png)

## water_bank

Grass bank of the large lake SW of spawn, looking down across open water.

Oracle readback: null

![side-by-side](ck_water_bank_sbs.png)

![heatmap](ck_water_bank_heat.png)

## forest_canopy

Inside the roofed-forest canopy NW of spawn; dark-oak leaves 2-5 blocks overhead, looking slightly up.

Oracle readback: null

![side-by-side](ck_forest_canopy_sbs.png)

![heatmap](ck_forest_canopy_heat.png)

## beach_sand

Sand beach column with water to the west; tests sand/water/biome tint.

Oracle readback: null

![side-by-side](ck_beach_sand_sbs.png)

![heatmap](ck_beach_sand_heat.png)

## mountain_vista

Top of the extreme-hills peak SE of spawn (highest ground in the surveyed box, y=110), looking back toward spawn.

Oracle readback: null

![side-by-side](ck_mountain_vista_sbs.png)

![heatmap](ck_mountain_vista_heat.png)

## underground_cave

Inside a natural cave pocket (air at y=39-41 under 30+ blocks of stone). Tests darkness/light falloff parity; expect a near-black frame on both sides.

Oracle readback: null

![side-by-side](ck_underground_cave_sbs.png)

![heatmap](ck_underground_cave_heat.png)

## snow_slope

Snow-layer-covered high slope near spawn; tests snow layers + high-altitude fog.

Oracle readback: null

![side-by-side](ck_snow_slope_sbs.png)

![heatmap](ck_snow_slope_heat.png)

## night_spawn

Same pose as spawn_plains at midnight (time 18000, frozen). Tests sky/lightmap night parity.

Oracle readback: null

![side-by-side](ck_night_spawn_sbs.png)

![heatmap](ck_night_spawn_heat.png)

