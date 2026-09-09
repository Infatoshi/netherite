/* Source blaze/env/blaze_core.h SHA256 925f5018c71b36e128d61c12fefcd5a2b6c84272295364d3eda3838198b56e21.
 * Exact reference span: blaze_runtime_player_phase, lines5724-5881 at c078993. */
/* Measurement-only exact extraction from blaze_runtime_player_phase.
 * Copied statements retain source order; no gameplay rule changes.
 * Paired pure_player.cu retains all emitted edits for the existing world phase. */
#include "../../blaze/env/blaze_cuda_int.h"

extern "C" __global__ void measure_after_player(Blaze *envs, int n,
                                                  const int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 2)) return;
    Blaze *env = envs + i;
    {
        PsvPlayer *pl = &env->pl;
        if (pl->hz_fire > 0.0f)
            (void)cu_hurt_player_src(env, pl->hz_fire,
                                     PSV_HURT_BYPASS | PSV_HURT_FIRE,
                                     pl->ent.posX, pl->ent.posZ);
        if (pl->hz_lava > 0.0f)
            (void)cu_hurt_player_src(env, pl->hz_lava, PSV_HURT_FIRE,
                                     pl->ent.posX, pl->ent.posZ);
        if (pl->hz_void > 0.0f)
            (void)cu_hurt_player_src(env, pl->hz_void,
                                     PSV_HURT_BYPASS | PSV_HURT_VOID |
                                         PSV_HURT_ABSOLUTE,
                                     pl->ent.posX, pl->ent.posZ);
        if (pl->hz_wall > 0.0f)
            (void)cu_hurt_player(env, pl->hz_wall, 1);
        if (pl->hz_drown > 0.0f)
            (void)cu_hurt_player(env, pl->hz_drown, 1);
        if (pl->hz_cactus > 0.0f)
            (void)cu_hurt_player(env, pl->hz_cactus, 0);
        if (pl->hz_magma > 0.0f)
            (void)cu_hurt_player_src(env, pl->hz_magma, PSV_HURT_FIRE,
                                     pl->ent.posX, pl->ent.posZ);
        env->pl.health = env->vit.health;
        psv_env_clear_hits(pl);
    }
    /* Minecraft.runTick pins this GUI sentinel after key processing. */
    if (env->container >= 1 && env->container <= 3)
        env->ctl.left_click_counter = 10000;
}
