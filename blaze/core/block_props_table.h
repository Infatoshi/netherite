/* block_props_table: FULL BlockProps table for all KEEP blocks (SPEC scope manifest).
 *
 * PORT TARGET: net/minecraft/block/Block.java registerBlocks() + per-subclass setHardness/setLight*.
 * Vanilla verify: float hardness + int light_emit/light_opacity match Java exactly; BF_* flags are
 * the sim-layer extension (identical in Golden.java).
 *
 * KEEP (161 ids): survival terrain/ores/fluids/wood/tools blocks/furnace/chest/crafting_table/torches,
 * worldgen-placed vegetation/structures/nether/end blocks used by verified populate/chunk_provider.
 *
 * CUT (omitted from table rows; vanilla id gaps preserved): redstone/automation (dispenser/piston/hopper/
 * repeater/comparator/rails/daylight sensor/redstone block/lamp/torch wire),
 * decorative (noteblock/wool/stained glass+carpet/banners/beacon/jukebox/flower_pot/skull/double_plant).
 * Stone/wood/weighted pressure plates, the wooden button, repeaters, and
 * tripwire are promoted exceptions. Repeaters are 1/8-high, opacity 0,
 * hardness 0, and non-random-ticking. Tripwire hook/wire have NULL collision,
 * opacity 0, hardness 0, and scheduled/random-tick registration.
 *
 * Flag bits (core/mc_blocks.h BF_*): SOLID=1 LIQUID=2 REPLACEABLE=4 FALLING=8 TICK_RANDOM=16.
 * mc_bpt_props(id) switch lookup; CUT ids return default {1.5,0,255,BF_SOLID}. */
#ifndef MC_BLOCK_PROPS_TABLE_H
#define MC_BLOCK_PROPS_TABLE_H

#include "mc.h"
#include "mc_blocks.h"

typedef struct {
    float hardness;
    u8    light_emit;
    u8    light_opacity;
    u16   flags;
} BptProps;

#define BPT_NKEEP 169

static const int BPT_KEEP_IDS[BPT_NKEEP] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 26, 30, 31, 32, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 67, 68, 70, 72, 73, 74, 78, 79, 80, 81, 82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 139, 140, 141, 142, 143, 145, 147, 148, 149, 150, 153, 155, 156, 161, 162, 163, 164, 165, 168, 169, 170, 172, 173, 174, 175, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205
};

MC_HD static inline BptProps mc_bpt_props(int id) {
    BptProps d = (BptProps){ 1.5f, 0, 255, BF_SOLID };
    switch (id) {
        case 0: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 1: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 2: d = (BptProps){ 0.6f, 0, 255, 17 }; break;
        case 3: d = (BptProps){ 0.5f, 0, 255, 1 }; break;
        case 4: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 5: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 6: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 7: d = (BptProps){ -1.0f, 0, 255, 1 }; break;
        case 8: d = (BptProps){ 100.0f, 0, 3, 6 }; break;
        case 9: d = (BptProps){ 100.0f, 0, 3, 6 }; break;
        case 10: d = (BptProps){ 100.0f, 15, 0, 6 }; break;
        case 11: d = (BptProps){ 100.0f, 15, 0, 6 }; break;
        case 12: d = (BptProps){ 0.5f, 0, 255, 9 }; break;
        case 13: d = (BptProps){ 0.6f, 0, 255, 9 }; break;
        case 14: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 15: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 16: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 17: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 18: d = (BptProps){ 0.2f, 0, 1, 17 }; break;
        case 19: d = (BptProps){ 0.6f, 0, 255, 1 }; break;
        case 20: d = (BptProps){ 0.3f, 0, 0, 1 }; break;
        case 21: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 22: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 24: d = (BptProps){ 0.8f, 0, 255, 1 }; break;
        case 26: d = (BptProps){ 0.2f, 0, 255, 1 }; break;
        case 30: d = (BptProps){ 4.0f, 0, 1, 1 }; break;
        case 31: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 32: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 37: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 38: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 39: d = (BptProps){ 0.0f, 1, 0, 4 }; break;
        case 40: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 41: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 42: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 43: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 44: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 45: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 46: d = (BptProps){ 0.0f, 0, 255, 1 }; break;
        case 47: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 48: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 49: d = (BptProps){ 50.0f, 0, 255, 1 }; break;
        case 50: d = (BptProps){ 0.0f, 14, 0, 4 }; break;
        case 51: d = (BptProps){ 0.0f, 15, 0, 20 }; break;
        case 52: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 53: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 54: d = (BptProps){ 2.5f, 0, 0, 1 }; break;
        case 56: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 57: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 58: d = (BptProps){ 2.5f, 0, 255, 1 }; break;
        case 59: d = (BptProps){ 0.0f, 0, 0, 20 }; break;
        case 60: d = (BptProps){ 0.6f, 0, 255, 1 }; break;
        case 61: d = (BptProps){ 3.5f, 0, 255, 1 }; break;
        case 62: d = (BptProps){ 3.5f, 13, 255, 1 }; break;
        case 63: d = (BptProps){ 1.0f, 0, 255, 1 }; break;
        case 64: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 65: d = (BptProps){ 0.4f, 0, 0, 1 }; break;
        case 67: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 68: d = (BptProps){ 1.0f, 0, 255, 1 }; break;
        case 70: d = (BptProps){ 0.5f, 0, 0, 16 }; break;
        case 72: d = (BptProps){ 0.5f, 0, 0, 16 }; break;
        case 73: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 74: d = (BptProps){ 3.0f, 9, 255, 1 }; break;
        case 78: d = (BptProps){ 0.1f, 0, 0, 4 }; break;
        case 79: d = (BptProps){ 0.5f, 0, 3, 1 }; break;
        case 80: d = (BptProps){ 0.2f, 0, 255, 1 }; break;
        case 81: d = (BptProps){ 0.4f, 0, 255, 17 }; break;
        case 82: d = (BptProps){ 0.6f, 0, 255, 1 }; break;
        case 83: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 85: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 86: d = (BptProps){ 1.0f, 0, 255, 1 }; break;
        case 87: d = (BptProps){ 0.4f, 0, 255, 1 }; break;
        case 88: d = (BptProps){ 0.5f, 0, 255, 1 }; break;
        case 89: d = (BptProps){ 0.3f, 15, 255, 1 }; break;
        case 90: d = (BptProps){ -1.0f, 11, 0, 0 }; break;
        case 91: d = (BptProps){ 1.0f, 15, 255, 1 }; break;
        case 92: d = (BptProps){ 0.5f, 0, 0, 1 }; break;
        case 93: d = (BptProps){ 0.0f, 0, 0, 0 }; break;
        case 94: d = (BptProps){ 0.0f, 0, 0, 0 }; break;
        case 95: d = (BptProps){ 0.3f, 0, 0, 1 }; break;
        case 96: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 98: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 99: d = (BptProps){ 0.2f, 0, 255, 1 }; break;
        case 100: d = (BptProps){ 0.2f, 0, 255, 1 }; break;
        case 101: d = (BptProps){ 5.0f, 0, 0, 1 }; break;
        case 102: d = (BptProps){ 0.3f, 0, 0, 1 }; break;
        case 103: d = (BptProps){ 1.0f, 0, 255, 1 }; break;
        case 104: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 105: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 106: d = (BptProps){ 0.2f, 0, 0, 4 }; break;
        case 107: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 108: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 109: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 110: d = (BptProps){ 0.6f, 0, 255, 1 }; break;
        case 111: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 112: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 113: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 114: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 115: d = (BptProps){ 0.0f, 0, 0, 20 }; break;
        case 116: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 117: d = (BptProps){ 0.5f, 1, 255, 1 }; break;
        case 118: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 119: d = (BptProps){ -1.0f, 0, 0, 0 }; break;
        case 120: d = (BptProps){ -1.0f, 1, 255, 1 }; break;
        case 121: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 122: d = (BptProps){ 3.0f, 1, 255, 1 }; break;
        case 125: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 126: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 127: d = (BptProps){ 0.2f, 0, 255, 1 }; break;
        case 128: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 129: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 130: d = (BptProps){ 22.5f, 7, 255, 1 }; break;
        case 131: d = (BptProps){ 0.0f, 0, 0, 16 }; break;
        case 132: d = (BptProps){ 0.0f, 0, 0, 16 }; break;
        case 133: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 134: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 135: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 136: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 139: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 140: d = (BptProps){ 0.0f, 0, 255, 1 }; break;
        case 141: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 142: d = (BptProps){ 0.0f, 0, 0, 4 }; break;
        case 143: d = (BptProps){ 0.5f, 0, 0, 16 }; break;
        case 145: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 146: d = (BptProps){ 2.5f, 0, 0, 1 }; break;
        case 147: d = (BptProps){ 0.5f, 0, 0, 16 }; break;
        case 148: d = (BptProps){ 0.5f, 0, 0, 16 }; break;
        case 149: d = (BptProps){ 0.0f, 0, 0, 0 }; break;
        case 150: d = (BptProps){ 0.0f, 0, 0, 0 }; break;
        case 153: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 155: d = (BptProps){ 0.8f, 0, 255, 1 }; break;
        case 156: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 161: d = (BptProps){ 0.2f, 0, 1, 17 }; break;
        case 162: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 163: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 164: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 165: d = (BptProps){ 0.0f, 0, 255, 1 }; break;
        case 168: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 169: d = (BptProps){ 0.3f, 15, 255, 1 }; break;
        case 170: d = (BptProps){ 0.5f, 0, 255, 1 }; break;
        case 172: d = (BptProps){ 1.25f, 0, 255, 1 }; break;
        case 173: d = (BptProps){ 5.0f, 0, 255, 1 }; break;
        case 174: d = (BptProps){ 0.5f, 0, 255, 1 }; break;
        case 175: d = (BptProps){ 0.0f, 0, 0, 20 }; break;
        case 179: d = (BptProps){ 0.8f, 0, 255, 1 }; break;
        case 180: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 181: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 182: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 183: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 184: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 185: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 186: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 187: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 188: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 189: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 190: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 191: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 192: d = (BptProps){ 2.0f, 0, 0, 1 }; break;
        case 193: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 194: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 195: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 196: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 197: d = (BptProps){ 3.0f, 0, 255, 1 }; break;
        case 198: d = (BptProps){ 0.0f, 14, 255, 1 }; break;
        case 199: d = (BptProps){ 0.4f, 0, 255, 1 }; break;
        case 200: d = (BptProps){ 0.4f, 0, 255, 1 }; break;
        case 201: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 202: d = (BptProps){ 1.5f, 0, 255, 1 }; break;
        case 203: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 204: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        case 205: d = (BptProps){ 2.0f, 0, 255, 1 }; break;
        default: break;
    }
    return d;
}

MC_HD static inline u32 bpt_pack_hardness(float h) {
    union { float f; u32 u; } v;
    v.f = h;
    return v.u;
}

#endif /* MC_BLOCK_PROPS_TABLE_H */
