#ifndef NETHERITE_BEDROCK_CONTRACT_H
#define NETHERITE_BEDROCK_CONTRACT_H

/* Frozen shared interface contract between all Bedrock portal-speedrun lanes:
 *   Lane A (br-harvester):      produces BRS1 portal_seeds.bin from libnetherite.a snapshots
 *   Lane B (br-portal-sim):     GPU portal-casting physics + Rung 3 CPU parity flywheel
 *   Lane C (br-policy-s1):      NVFP4 Fwd/Bwd PPO with exact net.h s1 factored action heads + BRT1 ckpt
 *   Lane D (br-distill-bridge): live CPU World -> obs[128] extractor + zero-shot replay + distill.cu rec
 */

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* World bubble dimensions */
#define BRC_WX          64
#define BRC_WY          32
#define BRC_WZ          64
#define BRC_BLOCKS      (BRC_WX * BRC_WY * BRC_WZ)      /* 131072 */
#define BRC_BIT_WORDS   (BRC_BLOCKS / 32)               /* 4096 */
#define BRC_DELTA_CAP   64                              /* up to 64 block edits for 4x5 mold + lava cast + portal */

/* Exact csrc/runtime/train/net.h + csrc/runtime/vecenv.h action dimensions */
#define BRC_CAM_BINS    41                              /* 41 mu-law bins for pitch and yaw; bin 20 = 0.0 deg */
#define BRC_KEYS        14                              /* forward back left right sprint jump sneak mine place throw drop hit swap paused */
#define BRC_HOTBAR_CATS 10                              /* 0 = keep slot, 1..9 = select hotbar slot 0..8 */
#define BRC_S1_ACT_DIM  (2 + BRC_KEYS + 1)              /* 17: [pitch_bin, yaw_bin, k0..k13, hotbar] */
#define BRC_OBS_DIM     128                             /* K=128 aligned for NVFP4 16x16 Tensor Core tiles */

/* Key bit positions matching csrc/runtime/vecenv.c:1666 */
enum brc_key_idx {
    BRC_KEY_FORWARD = 0,
    BRC_KEY_BACK    = 1,
    BRC_KEY_LEFT    = 2,
    BRC_KEY_RIGHT   = 3,
    BRC_KEY_SPRINT  = 4,
    BRC_KEY_JUMP    = 5,
    BRC_KEY_SNEAK   = 6,
    BRC_KEY_MINE    = 7,
    BRC_KEY_USE     = 8,
    BRC_KEY_THROW   = 9,
    BRC_KEY_DROP    = 10,
    BRC_KEY_HIT     = 11,
    BRC_KEY_SWAP    = 12,
    BRC_KEY_PAUSED  = 13
};

/* Minecraft 1.7.10 block IDs relevant to surface lava-pool portal speedrunning */
#define BRC_BLK_AIR        0
#define BRC_BLK_STONE      1
#define BRC_BLK_GRASS      2
#define BRC_BLK_DIRT       3
#define BRC_BLK_COBBLE     4
#define BRC_BLK_FLOW_WATER 8
#define BRC_BLK_WATER      9
#define BRC_BLK_FLOW_LAVA  10
#define BRC_BLK_LAVA       11
#define BRC_BLK_SAND       12
#define BRC_BLK_GRAVEL     13
#define BRC_BLK_IRON_ORE   15
#define BRC_BLK_LOG        17
#define BRC_BLK_OBSIDIAN   49
#define BRC_BLK_FIRE       51
#define BRC_BLK_PORTAL     90

/* Minecraft 1.7.10 hotbar item IDs */
#define BRC_ITEM_NONE         0
#define BRC_ITEM_COBBLE       4
#define BRC_ITEM_DIRT         3
#define BRC_ITEM_STONE        1
#define BRC_ITEM_IRON_PICK    257
#define BRC_ITEM_FLINT_STEEL  259
#define BRC_ITEM_BUCKET_EMPTY 325
#define BRC_ITEM_BUCKET_WATER 326
#define BRC_ITEM_BUCKET_LAVA  327

/* Exact csrc/runtime/vecenv.c:257-259 41-bin mu-law camera step in degrees */
static inline float brc_cam_deg(int b) {
    if (b < 0) b = 0;
    if (b >= BRC_CAM_BINS) b = BRC_CAM_BINS - 1;
    double x = (double)b / 40.0 * 2.0 - 1.0;
    double s = (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0);
    return (float)(s * (pow(21.0, fabs(x)) - 1.0) / 20.0 * 90.0);
}

static inline int brc_blk_is_solid(uint8_t b) {
    if (b == 0 || b == 6 || b == 8 || b == 9 || b == 10 || b == 11 ||
        b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 ||
        b == 50 || b == 51 || b == 55 || b == 59 || b == 63 || b == 65 ||
        b == 68 || b == 69 || b == 75 || b == 76 || b == 77 || b == 78 ||
        b == 83 || b == 90 || b == 106 || b == 111 || b == 141 || b == 142 || b == 175)
        return 0;
    return 1;
}

static inline float brc_blk_class_code(uint8_t b) {
    if (b == BRC_BLK_AIR) return 0.0f;
    if (b == BRC_BLK_LAVA || b == BRC_BLK_FLOW_LAVA) return 0.50f;
    if (b == BRC_BLK_WATER || b == BRC_BLK_FLOW_WATER) return 0.75f;
    if (b == BRC_BLK_OBSIDIAN) return 0.90f;
    if (b == BRC_BLK_PORTAL || b == BRC_BLK_FIRE) return 1.00f;
    return 0.25f;
}

/* ============================================================================
 * 1. Harvested Seed Pack Format ("BRS1", produced by Lane A, consumed by B/C/D)
 * ============================================================================ */
#define BRC_SEED_MAGIC 0x31535242u /* "BRS1" little-endian */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_seeds;
    uint32_t reserved;
} brc_seed_file_hdr_t;

typedef struct {
    int32_t  origin_x, origin_y, origin_z;  /* world-space integer origin of bubble (0,0,0) */
    float    spawn_x, spawn_y, spawn_z;     /* local bubble coords (0..64, 0..32, 0..64) */
    float    spawn_yaw, spawn_pitch;        /* degrees, Minecraft convention */
    float    lava_x, lava_y, lava_z;        /* local bubble coords of nearest surface lava center */
    int32_t  lava_sources;                  /* count of source lava (id=11, meta=0) in bubble */
    uint16_t hotbar_item[9];                /* Minecraft 1.7.10 item IDs in slots 0..8 */
    uint8_t  hotbar_cnt[9];                 /* stack count in slots 0..8 */
    uint8_t  sel_slot;                      /* initial selected hotbar slot (0..8) */
    uint8_t  pad[6];
    char     source_name[64];               /* snapshot directory basename */
    uint8_t  blocks[BRC_BLOCKS];            /* [y][z][x] = ((y<<12)|(z<<6)|x) */
    uint8_t  meta[BRC_BLOCKS];              /* 4-bit metadata (0 = fluid source) */
    uint32_t solid_bits[BRC_BIT_WORDS];     /* precomputed 1-bit solid collision mask */
} brc_seed_entry_t;

/* ============================================================================
 * 2. Factored s1 Action & Shared 128-Dim Privileged Obs Layout (v2)
 * ============================================================================ */
typedef struct {
    int32_t a[BRC_S1_ACT_DIM]; /* [0]=pitch_bin, [1]=yaw_bin, [2..15]=keys 0/1, [16]=hotbar 0..9 */
} brc_s1_action_t;

/* Shared 128-dim observation layout (v2, used identically on GPU and CPU World):
 *   obs[0..63]   : 4 pitch rows (dy_deg = -24, -8, +8, +24 relative to pitch) x 16 yaw cols (-60..+60 deg),
 *                  normalized ray distance hit_dist / 12.0f (1.0 = no hit within 12 blocks)
 *   obs[64..79]  : 16 block-class codes along row 1 (pitch - 8 deg) using brc_blk_class_code()
 *   obs[80]      : rel_fwd   = dot(fwd_xz, dir_to_lava)
 *   obs[81]      : rel_side  = dot(right_xz, dir_to_lava)
 *   obs[82]      : clamp(dist_to_lava / 16.0f, 0, 1)
 *   obs[83]      : clamp((lava_y - py) / 8.0f, -1, 1)
 *   obs[84]      : sin(yaw_rad)
 *   obs[85]      : cos(yaw_rad)
 *   obs[86]      : pitch_deg / 90.0f
 *   obs[87]      : clamp(vx, -1, 1)
 *   obs[88]      : clamp(vy, -1, 1)
 *   obs[89]      : clamp(vz, -1, 1)
 *   obs[90]      : on_ground (0 or 1)
 *   obs[91]      : sneaking (0 or 1)
 *   obs[92]      : hp / 20.0f
 *   obs[93]      : sel_slot / 8.0f
 *   obs[94]      : holding_water_bucket (0 or 1)
 *   obs[95]      : holding_lava_bucket  (0 or 1)
 *   obs[96]      : holding_empty_bucket (0 or 1)
 *   obs[97]      : holding_flint_steel  (0 or 1)
 *   obs[98]      : holding_build_block  (0 or 1)
 *   obs[99]      : clamp(cobble_count / 64.0f, 0, 1)
 *   obs[100]     : clamp(obsidian_placed / 10.0f, 0, 1)
 *   obs[101]     : portal_lit (0 or 1)
 *   obs[102]     : crosshair_blk_class (brc_blk_class_code of block under 4.5b crosshair, or 0)
 *   obs[103]     : ep_tick / max_ticks
 *   obs[104..127]: 0.0f pad (K=128 NVFP4 tile alignment)
 */

/* ============================================================================
 * 3. Bedrock Teacher Checkpoint Format ("BRT1", saved by Lane C, read by Lane D)
 * ============================================================================ */
#define BRC_CKPT_MAGIC 0x31545242u /* "BRT1" little-endian */

typedef struct {
    uint32_t magic;
    uint32_t version;       /* 1 */
    uint32_t obs_dim;       /* 128 */
    uint32_t hidden;        /* 1024 or 2048 */
    uint32_t cam_bins;      /* 41 */
    uint32_t n_keys;        /* 14 */
    uint32_t n_hotbar;      /* 10 */
    uint32_t reserved;
} brc_teacher_hdr_t;
/* Followed immediately by little-endian float32 tensors (row-major [out][in]):
 *   W1     [hidden * obs_dim]
 *   W2     [hidden * hidden]
 *   W_cam  [(2 * cam_bins) * hidden]   (rows 0..40 = pitch logits, rows 41..81 = yaw logits)
 *   b_cam  [2 * cam_bins]
 *   W_keys [n_keys * hidden]
 *   b_keys [n_keys]
 *   W_hot  [n_hotbar * hidden]
 *   b_hot  [n_hotbar]
 *   W_val  [1 * hidden]
 *   b_val  [1]
 */

#ifdef __cplusplus
}
#endif

#endif /* NETHERITE_BEDROCK_CONTRACT_H */
