#ifndef NETHERITE_BEDROCK_PORTAL_SIM_H
#define NETHERITE_BEDROCK_PORTAL_SIM_H

/* Lane B (br-portal-sim): GPU 3D portal-casting physics + obs v2 extractor + Rung 3 step API.
 * Strictly conforms to csrc/bedrock/br_contract.h.
 */

#include "br_contract.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Packed 32-bit Sparse Block Delta Entry (BRC_DELTA_CAP = 64 per world)
 *   bits [0..5]   : x (0..63)
 *   bits [6..11]  : z (0..63)
 *   bits [12..16] : y (0..31)   -> key = bits [0..16] = (y<<12)|(z<<6)|x
 *   bit  [17]     : solid (0 or 1)
 *   bits [18..25] : block_id (0..255)
 *   bits [26..29] : meta (0..15)
 *   bits [30..31] : reserved (0)
 * ============================================================================ */
static inline uint32_t brc_delta_pack(int x, int y, int z, int solid, uint8_t blk_id, uint8_t meta) {
    uint32_t key = (uint32_t)(((y & 31) << 12) | ((z & 63) << 6) | (x & 63));
    return key | (((uint32_t)(solid ? 1 : 0)) << 17) |
           (((uint32_t)blk_id & 0xFFu) << 18) |
           (((uint32_t)meta & 0xFu) << 26);
}

static inline int brc_delta_x(uint32_t e)      { return (int)(e & 63u); }
static inline int brc_delta_z(uint32_t e)      { return (int)((e >> 6) & 63u); }
static inline int brc_delta_y(uint32_t e)      { return (int)((e >> 12) & 31u); }
static inline int brc_delta_solid(uint32_t e)  { return (int)((e >> 17) & 1u); }
static inline uint8_t brc_delta_id(uint32_t e) { return (uint8_t)((e >> 18) & 0xFFu); }
static inline uint8_t brc_delta_meta(uint32_t e) { return (uint8_t)((e >> 26) & 0xFu); }

static inline int brc_blk_idx(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

/* Host-visible per-world player & portal state snapshot (upload / download / parity) */
typedef struct {
    double   x, y, z;
    double   vx, vy, vz;
    float    yaw, pitch;
    float    hp;
    uint8_t  on_ground;
    uint8_t  sneaking;
    uint8_t  jump_ticks;
    uint8_t  use_cd;
    uint8_t  mine_cd;
    uint8_t  hurt_cd;
    uint8_t  sel_slot;
    uint8_t  portal_lit;
    uint8_t  in_portal;
    uint8_t  dead;
    uint16_t ep_tick;
    uint16_t obsidian_placed;
    uint16_t portal_blocks;
    uint16_t hotbar_item[9];
    uint8_t  hotbar_cnt[9];
    uint8_t  ndelta;
    uint32_t deltas[BRC_DELTA_CAP];
} br_portal_state_t;

/* Opaque GPU portal simulator fleet handle */
typedef struct br_portal_sim br_portal_sim_t;

/* Create / destroy GPU fleet initialized from BRS1 seed entries */
br_portal_sim_t *br_portal_sim_create(int n_worlds, const brc_seed_entry_t *seeds,
                                      int num_seeds, int max_ticks);
void br_portal_sim_destroy(br_portal_sim_t *sim);

/* Reset all worlds to their assigned seed spawn + hotbar + empty delta ring */
void br_portal_sim_reset_all(br_portal_sim_t *sim);

/* Upload / download full per-world states for parity inspection */
void br_portal_sim_upload_states(br_portal_sim_t *sim, const br_portal_state_t *st, int n);
void br_portal_sim_download_states(br_portal_sim_t *sim, br_portal_state_t *st, int n);

/* Extract shared obs[128] v2 on GPU into d_obs_out (or internal buffer if d_obs_out == NULL) */
void br_portal_sim_extract_obs(br_portal_sim_t *sim, float *d_obs_out);
void br_portal_sim_download_obs(br_portal_sim_t *sim, float *h_obs, int n);

/* Primary C-callable RL step function (Lane C / Coordinator integration):
 * - h_or_d_acts: N actions (brc_s1_action_t[N]), on host if acts_on_device==0 else GPU
 * - ticks_per_step: K simulation ticks per RL step (e.g. 1 or 4)
 * - d_obs_out: optional device float[N * 128] (if NULL, writes internal sim obs buffer)
 * - d_rew_out: optional device float[N] (if NULL, writes internal sim rew buffer)
 * - d_done_out: optional device uint8_t[N] (if NULL, writes internal sim done buffer)
 * - auto_reset: if nonzero, automatically resets done worlds before extracting final obs
 */
void br_portal_sim_step(br_portal_sim_t *sim,
                        const brc_s1_action_t *h_or_d_acts, int acts_on_device,
                        int ticks_per_step, int auto_reset,
                        float *d_obs_out, float *d_rew_out, uint8_t *d_done_out);

/* Deterministic action-tape replay for Rung 3 parity:
 * h_acts_tn is host array of shape [ticks][N] (action at tick t for world i is h_acts_tn[t*N + i]).
 * Returns elapsed GPU kernel time in microseconds.
 */
double br_portal_sim_replay(br_portal_sim_t *sim, const brc_s1_action_t *h_acts_tn,
                            int ticks, int auto_reset);

/* Pure GPU throughput benchmark: runs `iters` steps of `ticks_per_step` ticks each.
 * Returns throughput in Mticks/s (million world-ticks per second).
 */
double br_portal_sim_bench(br_portal_sim_t *sim, int ticks_per_step, int iters, int extract_obs);

/* Zero-copy device buffer accessors for Lane C GPU PPO */
float   *br_portal_sim_obs_ptr(br_portal_sim_t *sim);
float   *br_portal_sim_rew_ptr(br_portal_sim_t *sim);
uint8_t *br_portal_sim_done_ptr(br_portal_sim_t *sim);
int      br_portal_sim_n_worlds(const br_portal_sim_t *sim);

/* Helper to synthesize a surface lava-pool portal seed for tests & benchmarks */
void br_portal_make_test_seed(brc_seed_entry_t *out, uint64_t rng_seed);

/* Host helper to query effective (block_id, meta) from a seed + downloaded delta ring */
uint8_t br_portal_query_block(const brc_seed_entry_t *seed, const br_portal_state_t *st,
                              int x, int y, int z, uint8_t *out_meta);

#ifdef __cplusplus
}
#endif

#endif /* NETHERITE_BEDROCK_PORTAL_SIM_H */
