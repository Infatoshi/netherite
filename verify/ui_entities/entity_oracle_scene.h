/* Capture-world blocks for ui_entities candidate frames.
 *
 * Sources of truth (not the golden PNG):
 *   capture_ui_entities_driver.py place_pad / place_dragon_platform
 *   Recorder.java setblocks (numeric id+meta, flag 3)
 *   FlatGeneratorInfo.getDefaultFlatGenerator (plains, no decoration)
 *
 * fancyGraphics=false is pinned by capture_ui_entities.sh and recorded in
 * Recorder.java options. It does not change grass: BlockGrass.getBlockLayer
 * is always CUTOUT_MIPPED; overlay lives in models/block/grass.json.
 */
#ifndef VERIFY_UI_ENTITIES_ENTITY_ORACLE_SCENE_H
#define VERIFY_UI_ENTITIES_ENTITY_ORACLE_SCENE_H

#include "game/game.h"

/* driver.py:22-23 */
#define UI_ENT_CX 8
#define UI_ENT_CZ 8
#define UI_ENT_PLAT_Y 4

/* driver.py:87-111 place_pad
 * x = range(CX-6, CX+7) = [2,14]
 * z = range(CZ-2, CZ+11) = [6,18]
 * stone id=1 meta=0 at y=PLAT_Y
 * air id=0 meta=0 at y=PLAT_Y+1 .. PLAT_Y+7
 * then stone (CX+2, PLAT_Y+1, CZ+3)=(10,5,11)
 *      grass (CX+3, PLAT_Y+1, CZ+3)=(11,5,11) id=2 meta=0
 *
 * Recorder.java:5502-5536: Block.getBlockById(id).getStateFromMeta(meta),
 * WorldServer.setBlockState(..., 3).
 */
static void ui_entities_place_pad(GmWorld *w)
{
    int x, y, z;
    int x0 = UI_ENT_CX - 6;
    int x1 = UI_ENT_CX + 6;
    int z0 = UI_ENT_CZ - 2;
    int z1 = UI_ENT_CZ + 10;
    if (!w) return;
    for (x = x0; x <= x1; ++x) {
        for (z = z0; z <= z1; ++z) {
            gm_world_set_block_meta(w, x, UI_ENT_PLAT_Y, z, 1, 0);
            for (y = UI_ENT_PLAT_Y + 1; y <= UI_ENT_PLAT_Y + 7; ++y)
                gm_world_set_block_meta(w, x, y, z, 0, 0);
        }
    }
    gm_world_set_block_meta(w, UI_ENT_CX + 2, UI_ENT_PLAT_Y + 1,
                            UI_ENT_CZ + 3, 1, 0);
    gm_world_set_block_meta(w, UI_ENT_CX + 3, UI_ENT_PLAT_Y + 1,
                            UI_ENT_CZ + 3, 2, 0);
}

#endif
