/* biome_props_full: FULL vanilla biome property table (Biome.registerBiomes + subclass ctors).
 * PORT TARGET: net/minecraft/world/biome/Biome.java registerBiomes() + per-subclass topBlock/fillerBlock
 * overrides + genTerrainBlocks dispatch (BiomeHills/Taiga/Swamp/Mesa/SavannaMutated).
 * Isolated from chunk_provider.h cb_* tables; mc_bpf_* accessors are the canonical lookup here.
 * Block-state ids (BPF_BS_*): sanctioned substitution, identical in golden + candidate. */
#ifndef MC_BIOME_PROPS_FULL_H
#define MC_BIOME_PROPS_FULL_H

#include "mc.h"

/* block-state id substitution (matches chunk_provider CB_* where they overlap) */
enum {
    BPF_BS_GRASS = 3,
    BPF_BS_DIRT = 4,
    BPF_BS_STONE = 1,
    BPF_BS_SAND = 7,
    BPF_BS_MYCELIUM = 15,
    BPF_BS_SNOW = 16,
    BPF_BS_STAINED_HARDENED_CLAY = 18,
    BPF_BS_RED_SAND = 21
};

/* genTerrainBlocks dispatch (low byte) + hills/taiga subtype (high byte when applicable) */
enum { BPF_GT_BASE = 0, BPF_GT_HILLS = 1, BPF_GT_TAIGA = 2, BPF_GT_SWAMP = 3, BPF_GT_MESA = 4, BPF_GT_SAVANNA_MUT = 5 };
enum { BPF_HILLS_NORMAL = 0, BPF_HILLS_EXTRA_TREES = 1, BPF_HILLS_MUTATED = 2 };
enum { BPF_TAIGA_NORMAL = 0, BPF_TAIGA_MEGA = 1, BPF_TAIGA_MEGA_SPRUCE = 2 };

#define BPF_N 62
static const int BPF_ALL_IDS[BPF_N] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    127, 129, 130, 131, 132, 133, 134, 140, 149, 151, 155, 156, 157, 158, 160, 161, 162, 163, 164, 165, 166, 167
};

MC_HD MC_NOINLINE static float mc_bpf_baseHeight(int id) {
    switch (id) {
        case 24: return -1.8f;
        case 0: case 10: return -1.0f;
        case 7: case 11: return -0.5f;
        case 6: return -0.2f;
        case 134: return -0.1f;
        case 15: case 16: case 26: return 0.0f;
        case 1: case 2: case 12: case 35: case 129: return 0.125f;
        case 5: case 14: case 30: case 32: case 149: case 151: case 155: case 157: case 160: case 161: return 0.2f;
        case 130: return 0.225f;
        case 133: case 158: return 0.3f;
        case 163: return 0.3625f;
        case 140: return 0.425f;
        case 13: case 17: case 18: case 19: case 22: case 28: case 31: case 33: case 166: case 167: return 0.45f;
        case 156: return 0.55f;
        case 20: return 0.8f;
        case 3: case 34: case 131: case 162: return 1.0f;
        case 164: return 1.05f;
        case 36: case 38: case 39: return 1.5f;
        default: return 0.1f;
    }
}

MC_HD MC_NOINLINE static float mc_bpf_heightVariation(int id) {
    switch (id) {
        case 7: case 11: return 0.0f;
        case 15: case 16: case 26: case 36: case 38: case 39: return 0.025f;
        case 1: case 2: case 12: case 35: case 129: return 0.05f;
        case 0: case 6: case 10: case 24: return 0.1f;
        case 130: return 0.25f;
        case 13: case 14: case 17: case 18: case 19: case 20: case 22: case 28: case 31: case 33: case 134: case 166: case 167: return 0.3f;
        case 132: case 133: case 149: case 151: case 155: case 157: case 158: return 0.4f;
        case 140: return 0.45000002f;
        case 3: case 34: case 131: case 156: case 162: return 0.5f;
        case 25: return 0.8f;
        case 164: return 1.2125001f;
        case 163: return 1.225f;
        default: return 0.2f;
    }
}

MC_HD MC_NOINLINE static float mc_bpf_temperature(int id) {
    switch (id) {
        case 30: case 31: case 158: return -0.5f;
        case 10: case 11: case 12: case 13: case 140: return 0.0f;
        case 26: return 0.05f;
        case 3: case 20: case 25: case 34: case 131: case 162: return 0.2f;
        case 5: case 19: case 133: case 160: case 161: return 0.25f;
        case 32: case 33: return 0.3f;
        case 27: case 28: case 155: case 156: return 0.6f;
        case 4: case 18: case 29: case 132: case 157: return 0.7f;
        case 1: case 6: case 16: case 129: case 134: return 0.8f;
        case 14: case 15: return 0.9f;
        case 21: case 22: case 23: case 149: case 151: return 0.95f;
        case 36: case 164: return 1.0f;
        case 163: return 1.1f;
        case 35: return 1.2f;
        case 2: case 8: case 17: case 37: case 38: case 39: case 130: case 165: case 166: case 167: return 2.0f;
        default: return 0.5f;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_topBlock(int id) {
    switch (id) {
        case 25: return 1;
        case 9: return 4;
        case 2: case 16: case 17: case 26: case 130: return 7;
        case 14: case 15: return 15;
        case 140: return 16;
        case 37: case 38: case 39: case 165: case 166: case 167: return 21;
        default: return 3;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_fillerBlock(int id) {
    switch (id) {
        case 25: return 1;
        case 2: case 16: case 17: case 26: case 130: return 7;
        case 37: case 38: case 39: case 165: case 166: case 167: return 18;
        default: return 4;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_genTerrainType(int id) {
    switch (id) {
        case 3: case 20: case 34: case 131: case 162: return 1;
        case 5: case 19: case 30: case 31: case 32: case 33: case 133: case 158: case 160: case 161: return 2;
        case 6: case 134: return 3;
        case 37: case 38: case 39: case 165: case 166: case 167: return 4;
        case 163: case 164: return 5;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_hillsType(int id) {
    switch (id) {
        case 20: case 34: return 1;
        case 131: case 162: return 2;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_taigaType(int id) {
    switch (id) {
        case 32: case 33: return 1;
        case 160: case 161: return 2;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int mc_bpf_genTerrainTypePacked(int id) {
    int gt = mc_bpf_genTerrainType(id);
    int sub = 0;
    if (gt == BPF_GT_HILLS) sub = mc_bpf_hillsType(id);
    else if (gt == BPF_GT_TAIGA) sub = mc_bpf_taigaType(id);
    return (sub << 8) | gt;
}

MC_HD MC_NOINLINE static u32 bpf_pack_float(float f) {
    union { float f; u32 u; } v;
    v.f = f;
    return v.u;
}

#endif /* MC_BIOME_PROPS_FULL_H */
