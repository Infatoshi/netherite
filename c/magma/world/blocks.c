/* magma world block table + procedural atlas. See blocks.h. */
#include "world/blocks.h"
#include "chunk_provider.h"   /* CB_* enum */

int block_is_solid(int cb) { return cb != CB_AIR; }

int block_is_opaque(int cb) {
    switch (cb) {
        case CB_AIR:
        case CB_WATER:
        case CB_FLOWING_WATER:
        case CB_ICE:
        case CB_WATER_LILY:
            return 0;   /* transparent / non-occluding */
        default:
            return block_is_solid(cb);
    }
}

/* Atlas tile slots (0..15 in an 8x2 grid). Distinct, recognizable per major block. */
enum {
    TILE_GRASS = 0, TILE_DIRT, TILE_STONE, TILE_SAND, TILE_WATER, TILE_BEDROCK,
    TILE_GRAVEL, TILE_SANDSTONE, TILE_SNOW, TILE_ICE, TILE_MYCELIUM, TILE_LAVA,
    TILE_CLAY, TILE_PODZOL, TILE_RED_SAND, TILE_UNKNOWN
};

int block_tile(int cb) {
    if (cb_is_stained_clay(cb)) cb = CB_STAINED_HARDENED_CLAY;
    switch (cb) {
        case CB_GRASS:                 return TILE_GRASS;
        case CB_DIRT:                  return TILE_DIRT;
        case CB_COARSE_DIRT:           return TILE_DIRT;
        case CB_STONE:                 return TILE_STONE;
        case CB_SAND:                  return TILE_SAND;
        case CB_WATER:
        case CB_FLOWING_WATER:         return TILE_WATER;
        case CB_BEDROCK:               return TILE_BEDROCK;
        case CB_GRAVEL:                return TILE_GRAVEL;
        case CB_SANDSTONE:             return TILE_SANDSTONE;
        case CB_RED_SANDSTONE:         return TILE_SANDSTONE;
        case CB_SNOW_LAYER:            return TILE_SNOW;
        case CB_ICE:                   return TILE_ICE;
        case CB_MYCELIUM:              return TILE_MYCELIUM;
        case CB_LAVA:
        case CB_FLOWING_LAVA:          return TILE_LAVA;
        case CB_HARDENED_CLAY:
        case CB_STAINED_HARDENED_CLAY: return TILE_CLAY;
        case CB_PODZOL:                return TILE_PODZOL;
        default:                       return TILE_UNKNOWN;
    }
}

CrRgba block_color(int cb) {
    CrRgba c;
    c.a = 255;
    switch (block_tile(cb)) {
        case TILE_GRASS:     c.r = 106; c.g = 154; c.b = 72;  break; /* green */
        case TILE_DIRT:      c.r = 134; c.g = 96;  c.b = 67;  break; /* brown */
        case TILE_STONE:     c.r = 128; c.g = 128; c.b = 128; break; /* grey  */
        case TILE_SAND:      c.r = 219; c.g = 207; c.b = 153; break; /* tan   */
        case TILE_WATER:     c.r = 55;  c.g = 90;  c.b = 210; break; /* blue  */
        case TILE_BEDROCK:   c.r = 40;  c.g = 40;  c.b = 44;  break; /* near-black */
        case TILE_GRAVEL:    c.r = 130; c.g = 124; c.b = 120; break; /* grey-brown */
        case TILE_SANDSTONE: c.r = 216; c.g = 202; c.b = 140; break; /* light tan  */
        case TILE_SNOW:      c.r = 240; c.g = 244; c.b = 250; break; /* white */
        case TILE_ICE:       c.r = 145; c.g = 183; c.b = 240; break; /* light blue */
        case TILE_MYCELIUM:  c.r = 122; c.g = 108; c.b = 122; break; /* purple-grey */
        case TILE_LAVA:      c.r = 214; c.g = 90;  c.b = 24;  break; /* orange */
        case TILE_CLAY:      c.r = 150; c.g = 92;  c.b = 66;  break; /* red-brown */
        case TILE_PODZOL:    c.r = 90;  c.g = 62;  c.b = 32;  break; /* dark brown */
        case TILE_RED_SAND:  c.r = 190; c.g = 100; c.b = 50;  break;
        default:             c.r = 200; c.g = 40;  c.b = 200; break; /* magenta = unknown */
    }
    return c;
}

/* Cheap deterministic per-texel hash noise (no libm, no RNG state). */
static u32 tex_hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static u8 clamp_u8(int v) { return (u8)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

/* Paint one 16x16 tile at grid (col,row) with base colour + a little noise so the
 * surface reads as a Minecraft-ish texture rather than a flat swatch. */
static void paint_tile(CrRgba *dst, int col, int row, CrRgba base, int noise_amp) {
    int ox = col * BLK_TILE, oy = row * BLK_TILE;
    for (int ty = 0; ty < BLK_TILE; ++ty) {
        for (int tx = 0; tx < BLK_TILE; ++tx) {
            u32 h = tex_hash((u32)((oy + ty) * BLK_ATLAS_W + (ox + tx)) * 2654435761u);
            int n = (int)(h & 0xff) - 128;        /* -128..127 */
            n = n * noise_amp / 128;              /* scale to +/- noise_amp */
            CrRgba px;
            px.r = clamp_u8((int)base.r + n);
            px.g = clamp_u8((int)base.g + n);
            px.b = clamp_u8((int)base.b + n);
            px.a = base.a;
            dst[(oy + ty) * BLK_ATLAS_W + (ox + tx)] = px;
        }
    }
}

void block_build_atlas(CrRgba *dst) {
    /* clear */
    for (int i = 0; i < BLK_ATLAS_W * BLK_ATLAS_H; ++i) {
        dst[i].r = dst[i].g = dst[i].b = 0; dst[i].a = 255;
    }
    /* one representative block id per tile slot, painted into its grid cell */
    static const struct { int cb; int amp; } slots[] = {
        { CB_GRASS, 22 }, { CB_DIRT, 26 }, { CB_STONE, 20 }, { CB_SAND, 16 },
        { CB_WATER, 18 }, { CB_BEDROCK, 26 }, { CB_GRAVEL, 28 }, { CB_SANDSTONE, 14 },
        { CB_SNOW_LAYER, 8 }, { CB_ICE, 16 }, { CB_MYCELIUM, 20 }, { CB_LAVA, 30 },
        { CB_HARDENED_CLAY, 22 }, { CB_PODZOL, 20 }, { CB_RED_SANDSTONE, 18 }, { -1, 24 }
    };
    for (int t = 0; t < BLK_ATLAS_COLS * BLK_ATLAS_ROWS; ++t) {
        int col = t % BLK_ATLAS_COLS, row = t / BLK_ATLAS_COLS;
        CrRgba base;
        if (slots[t].cb < 0) { base.r = 200; base.g = 40; base.b = 200; base.a = 255; }
        else base = block_color(slots[t].cb);
        paint_tile(dst, col, row, base, slots[t].amp);
    }
}
