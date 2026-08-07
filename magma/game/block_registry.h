/* block_registry.h - explicit bridge between renderer/worldgen model keys and
 * canonical Minecraft 1.11.2 packed block states.
 *
 * Runtime gameplay uses vanilla legacy state: (block_id << 4) | meta. The PB/CB
 * integers emitted by blaze worldgen are model keys only. They overlap vanilla
 * ids (PB 61 is a double plant while vanilla 61 is a furnace), so an untagged
 * integer can never safely serve both roles.
 */
#ifndef MAGMA_GAME_BLOCK_REGISTRY_H
#define MAGMA_GAME_BLOCK_REGISTRY_H

#include <stdint.h>

enum {
    GM_MAP_UNSUPPORTED = 0,
    GM_MAP_EXACT = 1,
    GM_MAP_LOSSY = 2
};

/* Deliberately outside every current PB/CBX table. bm_block() renders it with
 * the explicit stone fallback; it can never alias a real generated model key. */
#define GM_MODEL_FALLBACK 4095
#define GM_MODEL_STONE_SLAB_BOTTOM_BASE 237
#define GM_MODEL_STONE_SLAB_TOP_BASE 245
#define GM_MODEL_TRAPDOOR 255
#define GM_MODEL_LADDER 256
#define GM_MODEL_STONEBRICK 257

static inline uint16_t gm_pack_state(int id, int meta) {
    return (uint16_t)(((id & 0x0fff) << 4) | (meta & 15));
}
static inline int gm_state_id(uint16_t state) { return (int)(state >> 4); }
static inline int gm_state_meta(uint16_t state) { return (int)(state & 15); }

/* Convert a worldgen/render model key plus its sidecar meta into canonical state.
 * Unknown keys are rejected. Lossy means the PB producer already discarded a
 * vanilla property such as fluid level, cocoa facing/age, or mushroom-cap shape. */
static inline int gm_model_key_to_state(int key, int raw_meta, uint16_t *out) {
    int id = -1, meta = 0, quality = GM_MAP_EXACT;

    if (key & 0x4000) {
        uint16_t state = (uint16_t)(key & 0x3fff);
        id = gm_state_id(state);
        if (out) *out = state;
        return id >= 0 && id <= 1023 ? GM_MAP_EXACT : GM_MAP_UNSUPPORTED;
    }
    if (key & 0x8000) {
        id = key & 0x0fff;
        if (out) *out = gm_pack_state(id, raw_meta);
        return id > 0 && id <= 4095 ? GM_MAP_EXACT : GM_MAP_UNSUPPORTED;
    }

    switch (key) {
        case 0: id = 0; break;                    /* air */
        case 1: id = 1; break;                    /* stone */
        case 2: id = 9; meta = raw_meta; quality = GM_MAP_LOSSY; break;
        case 3: id = 2; break;                    /* grass */
        case 4: id = 3; break;                    /* dirt */
        case 5: id = 7; break;                    /* bedrock */
        case 6: id = 13; break;                   /* gravel */
        case 7: id = 12; quality = GM_MAP_LOSSY; break; /* red-sand provenance lost */
        case 8: id = 24; quality = GM_MAP_LOSSY; break;
        case 9: id = 179; break;                  /* red sandstone */
        case 10: id = 79; break;                  /* ice */
        case 11: id = 11; break;                  /* still lava */
        case 12: id = 10; meta = raw_meta; quality = GM_MAP_LOSSY; break;
        case 13: id = 8; meta = raw_meta; quality = GM_MAP_LOSSY; break;
        case 14: id = 111; break;                 /* water lily */
        case 15: id = 110; break;                 /* mycelium */
        case 16: id = 78; break;                  /* snow layer */
        case 17: id = 172; break;                 /* hardened clay */
        case 18: id = 159; break;                 /* stained clay, collapsed meta */
        case 19: id = 3; meta = 2; break;         /* podzol */
        case 20: id = 3; meta = 1; break;         /* coarse dirt */

        case 21: id = 1; meta = 1; break;         /* granite */
        case 22: id = 1; meta = 3; break;         /* diorite */
        case 23: id = 1; meta = 5; break;         /* andesite */
        case 24: id = 16; break;                  /* coal ore */
        case 25: id = 15; break;                  /* iron ore */
        case 26: id = 14; break;                  /* gold ore */
        case 27: id = 73; break;                  /* redstone ore */
        case 28: id = 56; break;                  /* diamond ore */
        case 29: id = 21; break;                  /* lapis ore */
        case 30: id = 82; break;                  /* clay */
        case 31: id = 17; meta = 0; break;        /* oak log */
        case 32: id = 17; meta = 2; break;        /* birch log */
        case 33: id = 17; meta = 1; break;        /* spruce log */
        case 34: id = 18; meta = 0; break;        /* oak leaves */
        case 35: id = 18; meta = 2; break;        /* birch leaves */
        case 36: id = 18; meta = 1; break;        /* spruce leaves */
        case 37: id = 17; meta = 4; break;        /* oak log X */
        case 38: id = 17; meta = 8; break;        /* oak log Z */
        case 39: id = 31; meta = 1; break;        /* tallgrass */
        case 40: id = 31; meta = 2; break;        /* fern */
        case 41: id = 32; break;                  /* dead bush */
        case 42: id = 39; break;                  /* brown mushroom */
        case 43: id = 40; break;                  /* red mushroom */
        case 44: id = 83; break;                  /* reeds */
        case 45: id = 4; break;                   /* cobblestone */
        case 46: id = 48; break;                  /* mossy cobble */
        case 47: id = 52; break;                  /* mob spawner */
        case 48: id = 216; quality = GM_MAP_LOSSY; break; /* bone axis lost */
        case 49: id = 54;
                 meta = raw_meta >= 2 && raw_meta <= 5 ? raw_meta : 2;
                 break;                           /* chest horizontal facing */
        case 50: id = 37; break;                  /* dandelion */

        case 66: id = 175; meta = 10; break;      /* double-plant upper */
        case 75: id = 129; break;                 /* emerald ore */
        case 76: id = 97; break;                  /* monster egg */
        case 77: id = 162; meta = 1; break;       /* dark-oak log */
        case 78: id = 161; meta = 1; break;       /* dark-oak leaves */
        case 79: id = 99; quality = GM_MAP_LOSSY; break;
        case 80: id = 100; quality = GM_MAP_LOSSY; break;
        case 81: id = 81; break;                  /* cactus */
        case 82: id = 162; meta = 0; break;       /* acacia log */
        case 83: id = 161; meta = 0; break;       /* acacia leaves */
        case 84: id = 44; meta = 1; break;        /* sandstone slab */
        case 85: id = 17; meta = 3; break;        /* jungle log */
        case 86: id = 18; meta = 3; break;        /* jungle leaves */
        case 87: id = 103; break;                 /* melon */
        case 88: id = 127; quality = GM_MAP_LOSSY; break; /* cocoa age/facing lost */
        case 89: id = 49; break;                  /* obsidian */
        case 90: id = 24; meta = 2; break;        /* smooth sandstone */
        case 91: id = 24; meta = 1; break;        /* chiseled sandstone */
        case 92: id = 128; meta = 0; break;       /* sandstone stairs east */
        case 93: id = 128; meta = 1; break;       /* sandstone stairs west */
        case 94: id = 128; meta = 2; break;       /* sandstone stairs south */
        case 95: id = 128; meta = 3; break;       /* sandstone stairs north */
        case 96: id = 159; meta = 1; break;       /* orange stained clay */
        case 97: id = 159; meta = 11; break;      /* blue stained clay */
        case 98: id = 70; break;                  /* stone pressure plate */
        case 99: id = 46; break;                  /* TNT */

        /* Synthetic/model-test keys and dimension-dump keys. */
        case 200: id = 20; break;                 /* glass */
        case 201: id = 53; meta = raw_meta; break;/* oak stairs */
        case 202: id = 44; meta = raw_meta; break;/* stone slab */
        case 203: id = 85; meta = raw_meta; break;/* oak fence */
        case 210: id = 87; break;                 /* netherrack */
        case 211: id = 90; meta = raw_meta; break;/* portal */
        case 212: id = 121; break;                /* end stone */
        case 213: id = 51; meta = raw_meta; break;/* fire */
        case 214: id = 89; break;                 /* glowstone */
        case 215: id = 88; break;                 /* soul sand */
        case 216: id = 120; meta = raw_meta; break;/* end portal frame */
        case 217: id = 153; break;                /* quartz ore */
        case 218: id = 39; break;
        case 219: id = 40; break;
        case 220: id = 213; break;                /* magma */
        case 221: id = 101; break;                /* iron bars */
        case 222: id = 50; meta = raw_meta; break;/* torch */
        case 223: id = 58; break;                 /* crafting table */
        case 224: id = 5; meta = raw_meta & 7; break; /* planks (species in meta) */
        case 225: id = 1; meta = 2; break;        /* polished granite */
        case 226: id = 1; meta = 4; break;        /* polished diorite */
        case 227: id = 1; meta = 6; break;        /* polished andesite */
        case 228: id = 112; break;                /* nether brick */
        case 229: id = 165; break;                /* slime block */
        case 230: id = 30; break;                 /* cobweb */
        case 231: id = 174; break;                /* packed ice */
        case 232: id = 113; break;                /* nether brick fence */
        case 233: id = 139; meta = raw_meta; break;/* cobblestone wall */
        case 234: id = 119; break;                  /* end portal (active) */
        case 235: id = 66; meta = raw_meta; break;  /* rail */
        case 236: id = 46; break;                   /* TNT */
        case 253: id = 102; break;                  /* glass pane */
        case 254: id = 67; meta = raw_meta; break;  /* cobblestone stairs */
        case GM_MODEL_TRAPDOOR:
            id = 96; meta = raw_meta; break;        /* oak trapdoor */
        case GM_MODEL_LADDER: id = 65; meta = raw_meta; break;
        case GM_MODEL_STONEBRICK: id = 98; meta = raw_meta; break;
        case 263: id = 201; break;                  /* purpur block */
        case 264: id = 202; break;                  /* purpur pillar Y */
        case 265: id = 202; meta = 4; break;        /* purpur pillar X */
        case 266: id = 202; meta = 8; break;        /* purpur pillar Z */
        case 267: id = 203; meta = raw_meta; break; /* purpur stairs */
        case 268: id = 205; break;                  /* purpur slab bottom */
        case 269: id = 205; meta = 8; break;        /* purpur slab top */
        case 270: id = 206; break;                  /* End stone bricks */
        case 271: id = 198; meta = raw_meta; break; /* End rod */
        case 272: id = 95; meta = 2; break;         /* magenta stained glass */
        case 273: id = 199; break;                  /* chorus plant */
        case 274: id = 200; break;                  /* living chorus flower */
        case 275: id = 200; meta = 5; break;        /* dead chorus flower */
        default: break;
    }

    if (key >= GM_MODEL_STONE_SLAB_BOTTOM_BASE
            && key < GM_MODEL_STONE_SLAB_TOP_BASE) {
        id = 44;
        meta = key - GM_MODEL_STONE_SLAB_BOTTOM_BASE;
    } else if (key >= GM_MODEL_STONE_SLAB_TOP_BASE
               && key < GM_MODEL_STONE_SLAB_TOP_BASE + 8) {
        id = 44;
        meta = 8 | (key - GM_MODEL_STONE_SLAB_TOP_BASE);
    }

    if (key >= 51 && key <= 59) {
        id = 38; meta = key - 51; quality = GM_MAP_EXACT;
    } else if (key >= 60 && key <= 65) {
        id = 175; meta = key - 60; quality = GM_MAP_EXACT;
    } else if (key >= 67 && key <= 70) {
        id = 86; meta = key - 67; quality = GM_MAP_EXACT;
    } else if (key >= 71 && key <= 74) {
        static const int vine_meta[4] = {8, 2, 1, 4};
        id = 106; meta = vine_meta[key - 71]; quality = GM_MAP_EXACT;
    } else if (key >= 120 && key <= 135) {
        id = 159; meta = key - 120; quality = GM_MAP_EXACT;
    }

    if (id < 0) return GM_MAP_UNSUPPORTED;
    if (out) *out = gm_pack_state(id, meta);
    return quality;
}

/* Best available render/model key for canonical gameplay state. A fallback key
 * is explicit and non-colliding, so unsupported visuals can never corrupt the
 * gameplay meaning of wheat/farmland/furnaces/doors or future blocks. */
static inline int gm_state_to_model_key(uint16_t state) {
    int id = gm_state_id(state), meta = gm_state_meta(state);
    switch (id) {
        case 0: return 0;
        case 1:
            if ((meta & 7) == 1) return 21;
            if ((meta & 7) == 2) return 225;
            if ((meta & 7) == 3) return 22;
            if ((meta & 7) == 4) return 226;
            if ((meta & 7) == 5) return 23;
            if ((meta & 7) == 6) return 227;
            return 1;
        case 2: return 3;
        case 3: return (meta & 3) == 2 ? 19 : (meta & 3) == 1 ? 20 : 4;
        case 4: return 45;
        case 5: return 224;                       /* planks, oak sprite for all */
        case 7: return 5;
        case 8: return 13;
        case 9: return 2;
        case 10: return 12;
        case 11: return 11;
        case 12: return 7;
        case 13: return 6;
        case 14: return 26;
        case 15: return 25;
        case 16: return 24;
        case 17: {
            int species = meta & 3, axis = meta & 12;
            if (species == 0 && axis == 4) return 37;
            if (species == 0 && axis == 8) return 38;
            if (species == 1) return 33;
            if (species == 2) return 32;
            if (species == 3) return 85;
            return 31;
        }
        case 18:
            if ((meta & 3) == 1) return 36;
            if ((meta & 3) == 2) return 35;
            if ((meta & 3) == 3) return 86;
            return 34;
        case 20: return 200;
        case 30: return 230;
        case 21: return 29;
        case 24: return meta == 1 ? 91 : meta == 2 ? 90 : 8;
        case 31: return (meta & 3) == 2 ? 40 : 39;
        case 32: return 41;
        case 37: return 50;
        case 38: return 51 + (meta > 8 ? 0 : meta);
        case 39: return 42;
        case 40: return 43;
        case 44:
            return ((meta & 8) ? GM_MODEL_STONE_SLAB_TOP_BASE
                               : GM_MODEL_STONE_SLAB_BOTTOM_BASE) + (meta & 7);
        case 46: return 236;
        case 48: return 46;
        case 49: return 89;
        case 50: return 222;
        case 51: return 213;
        case 52: return 47;
        case 53: return 201;
        case 54: return 49;
        case 56: return 28;
        case 58: return 223;                      /* crafting table */
        case 65: return GM_MODEL_LADDER;
        case 66: return 235;                      /* rail */
        case 67: return 254;                      /* cobblestone stairs */
        case 70: return 98;
        case 73: return 27;
        case 78: return 16;
        case 79: return 10;
        case 81: return 81;
        case 82: return 30;
        case 83: return 44;
        case 85: return 203;
        case 86: return 67 + (meta & 3);
        case 87: return 210;
        case 88: return 215;
        case 89: return 214;
        case 90: return 211;
        case 95: return (meta & 15) == 2 ? 272 : 200;
        case 96: return GM_MODEL_TRAPDOOR;
        case 97: return 76;
        case 98: return GM_MODEL_STONEBRICK;
        case 99: return 79;
        case 100: return 80;
        case 101: return 221;
        case 102: return 253;
        case 103: return 87;
        case 106:
            if (meta & 8) return 71;
            if (meta & 2) return 72;
            if (meta & 1) return 73;
            if (meta & 4) return 74;
            return 71;
        case 110: return 15;
        case 111: return 14;
        case 112: return 228;
        case 113: return 232;
        case 119: return 234;                     /* active End portal TESR mesh */
        case 120: return 216;
        case 121: return 212;
        case 127: return 88;
        case 128: return 92 + (meta & 3);
        case 129: return 75;
        case 139: return 233;
        case 153: return 217;
        case 159: return meta == 1 ? 96 : meta == 11 ? 97 : 120 + (meta & 15);
        case 161: return (meta & 1) ? 78 : 83;
        case 162: return (meta & 1) ? 77 : 82;
        case 165: return 229;
        case 172: return 17;
        case 174: return 231;
        case 175: return (meta & 8) ? 66 : 60 + (meta & 7);
        case 179: return 9;
        case 198: return 271;
        case 199: return 273;
        case 200: return (meta & 7) == 5 ? 275 : 274;
        case 201: return 263;
        case 202: return (meta & 12) == 4 ? 265 : (meta & 12) == 8 ? 266 : 264;
        case 203: return 267;
        case 205: return (meta & 8) ? 269 : 268;
        case 206: return 270;
        case 213: return 220;
        case 216: return 48;
        default: return GM_MODEL_FALLBACK;
    }
}

#endif /* MAGMA_GAME_BLOCK_REGISTRY_H */
