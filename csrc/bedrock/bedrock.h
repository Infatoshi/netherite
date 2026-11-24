#ifndef NETHERITE_BEDROCK_H
#define NETHERITE_BEDROCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BR_WX         64
#define BR_WY         32
#define BR_WZ         64
#define BR_BLOCKS     (BR_WX * BR_WY * BR_WZ)
#define BR_BIT_WORDS  (BR_BLOCKS / 32)
#define BR_DELTA_CAP  32
#define BR_NUM_MOBS   8
#define BR_OBS_RAYS   64
#define BR_OBS_DIM    128
#define BR_NUM_ACTS   16

/* Minecraft 1.7.10 Block IDs */
#define BR_BLK_AIR      0
#define BR_BLK_STONE    1
#define BR_BLK_GRASS    2
#define BR_BLK_DIRT     3
#define BR_BLK_LAVA     11
#define BR_BLK_IRON_ORE 15
#define BR_BLK_LOG      17
#define BR_BLK_OBSIDIAN 49
#define BR_BLK_PORTAL   90

/* Precision backends */
enum br_prec {
    BR_PREC_LOCAL_FP32 = 0,
    BR_PREC_FIXED_Q16  = 1
};

/* Backward pass precision routes */
enum br_bwd_prec {
    BR_BWD_FP16  = 0,
    BR_BWD_FP8   = 1,
    BR_BWD_NVFP4 = 2
};

/* Overworld-to-Nether-Portal curriculum environments */
enum br_env_id {
    BR_ENV_WOOD_PUNCH    = 0, /* Punch 1 oak log (block 17) */
    BR_ENV_WOOD_LOGS3    = 1, /* Harvest 3 oak logs + wooden pickaxe */
    BR_ENV_STONE_MINE    = 2, /* Mine 3 stone (block 1) + stone pickaxe */
    BR_ENV_IRON_MINE     = 3, /* Mine 3 iron ore (block 15) + iron bucket */
    BR_ENV_NETHER_PORTAL = 4, /* Cast 4 obsidian frame anchors on lava pool (11->49) + ignite portal (90) */
    BR_ENV_COUNT         = 5
};

enum br_act_bits {
    BR_ACT_FORWARD = 1 << 0,
    BR_ACT_BACK    = 1 << 1,
    BR_ACT_LEFT    = 1 << 2,
    BR_ACT_RIGHT   = 1 << 3,
    BR_ACT_JUMP    = 1 << 4,
    BR_ACT_SPRINT  = 1 << 5,
    BR_ACT_MINE    = 1 << 6,
    BR_ACT_USE     = 1 << 7
};

typedef struct {
    int16_t x, y, z;
    int16_t hp_state;
} br_mob_t;

typedef struct {
    double  x, y, z;
    double  vx, vy, vz;
    float   yaw, pitch;
    uint8_t on_ground;
    uint8_t jump_ticks;
    uint8_t dig_ticks;
    uint8_t won;
    int16_t dig_x, dig_y, dig_z;
    uint32_t logs_mined;
} br_player_state_t;

typedef struct {
    int      rung;
    int      prec;
    int      worlds;
    int      ticks;
    double   p50_pos_err;
    double   p90_pos_err;
    double   max_pos_err;
    double   p50_vel_err;
    double   ground_match_pct;
    double   win_match_pct;
    int      cpu_wins;
    int      gpu_wins;
    double   gpu_msteps_s;
    double   cpu_msteps_s;
} br_fidelity_t;

typedef struct {
    int      env_id;            /* enum br_env_id (0..4) */
    int      n_worlds;          /* pipeline.worlds (default 16384) */
    int      chunk_q;           /* pipeline.queue_chunk_q (default 8192) */
    int      cow_seeds;         /* world.cow_seeds (default 64) */
    int      persisting_l2_mb;  /* world.persisting_l2_mb (default 32) */
    int      episode_max_ticks; /* pipeline.episode_max_ticks (default 400) */
    int      target_count;      /* policy.bedrock.target_count (0 = env default) */
    int      break_ticks;       /* policy.bedrock.break_ticks  (0 = env default) */
    int      hidden;            /* net.hidden (1024, 2048, 4096) */
    int      rollout_t;         /* ppo.rollout_t (default 16) */
    int      ticks_per_step;    /* client.ticks_per_step (K = 4) */
    int      ppo_epochs;        /* ppo.epochs (default 3) */
    float    lr;                /* ppo.lr (default 2.5e-4) */
    float    ent_coef;          /* ppo.ent (default 0.01) */
    float    clip_eps;          /* ppo.clip (default 0.2) */
    float    gamma;             /* ppo.gamma (default 0.99) */
    float    gae_lam;           /* ppo.lam (default 0.95) */
    float    grad_loss_scale;   /* ppo.grad_loss_scale (default 16.0) */
    int      use_nvfp4;         /* net.forward_precision: 1 = nvfp4, 0 = fp16 */
    int      bwd_prec;          /* net.backward_precision: fp16 | fp8 | nvfp4 */
    uint64_t seed;              /* run.seed */
} br_train_cfg_t;

typedef struct {
    int    iter;
    double mean_step_rew;
    double win_rate_pct;
    double total_wins;
    double policy_loss;
    double value_loss;
    double entropy;
    double rollout_ms;
    double gae_ms;
    double learner_ms;
    double total_ms;
    double msteps_s;
    double mpol_s;
} br_train_stats_t;

typedef struct br_fleet br_fleet_t;
typedef struct br_trainer br_trainer_t;

static inline int br_blk_idx(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

static inline const char *br_env_name(int env_id) {
    static const char *names[BR_ENV_COUNT] = {
        "wood-punch", "wood-logs3", "stone-mine", "iron-mine", "nether-portal"
    };
    return (env_id >= 0 && env_id < BR_ENV_COUNT) ? names[env_id] : "?";
}

static inline const char *br_bwd_name(int bwd_prec) {
    static const char *names[3] = {"fp16", "fp8", "nvfp4"};
    return (bwd_prec >= 0 && bwd_prec < 3) ? names[bwd_prec] : "?";
}

void br_gen_seeds(uint8_t *out_blocks, uint32_t *out_bits, int num_seeds, uint64_t seed);

br_fleet_t *br_fleet_create(int n_worlds, int num_seeds, int prec,
                            const uint8_t *seed_blocks, const uint32_t *seed_bits);
void br_fleet_destroy(br_fleet_t *f);
void br_fleet_upload_players(br_fleet_t *f, const br_player_state_t *st, int n);
void br_fleet_download_players(br_fleet_t *f, br_player_state_t *st, int n);
double br_fleet_run_parity(br_fleet_t *f, const uint8_t *h_acts, const float *h_yaws, int ticks);
double br_fleet_bench_sim(br_fleet_t *f, int K, int iters);
double br_fleet_bench_rl(br_fleet_t *f, int K, int iters);

int br_run_fidelity_gate(int rung, int prec, int n_worlds, int ticks, br_fidelity_t *out_rep);

int br_check_nvfp4_gemm(double *out_cos_sim, double *out_rmse);
void br_bench_square_and_queue(void);

br_trainer_t *br_trainer_create(const br_train_cfg_t *cfg);
void br_trainer_step(br_trainer_t *tr, int iter, br_train_stats_t *out_stats);
int  br_trainer_dump_trace(br_trainer_t *tr, const char *path, int max_steps);
void br_trainer_destroy(br_trainer_t *tr);

#ifdef __cplusplus
}
#endif

#endif /* NETHERITE_BEDROCK_H */
