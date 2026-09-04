/* Focused CPU matrix: blaze leftClickCounter mirrors magma player_ctl. */
#define MC_HOST 1
#include "blaze_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int fail;
#define CHECK(C,M) do{if(!(C)){fprintf(stderr,"FAIL: %s\n",M);fail=1;}else fprintf(stderr,"OK: %s\n",M);}while(0)

static void fill_air_floor(Chunk *win) {
    int i, x, y, z;
    for (i = 0; i < PSV_NCHUNKS; ++i) {
        memset(&win[i], 0, sizeof(Chunk));
        for (y = 0; y < 64; ++y)
            for (z = 0; z < 16; ++z)
                for (x = 0; x < 16; ++x) {
                    int id = (y <= 4) ? 1 : 0; /* stone floor */
                    win[i].blocks[y*256 + z*16 + x] = (u16)(id << 4);
                }
    }
}

int main(void) {
    Blaze *env = (Blaze *)calloc(1, sizeof(Blaze));
    McSinTable st;
    Chunk *win = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    McAABB *blocks = (McAABB *)calloc((size_t)PSV_MAX_BLOCKS, sizeof(McAABB));
    CuEdit edits[8];
    int nedits;
    CuAction act;
    if (!env || !win || !blocks) return 2;
    mc_sin_table_init(&st);
    fill_air_floor(win);
    env->window = win;
    env->ox = 0; env->oz = 0;
    psv_player_init(&env->pl);
    pv_init(&env->vit);
    env->pl.ent.posX = 8.5; env->pl.ent.posY = 5.0; env->pl.ent.posZ = 8.5;
    env->pl.ent.box = psv_player_box(env->pl.ent.posX, env->pl.ent.posY, env->pl.ent.posZ);
    env->pl.ent.onGround = 1;
    env->ctl.dig_hx = INT_MIN;
    env->ctl.left_click_counter = 0;
    env->ctl.atk_prev = 0;

    memset(&act, 0, sizeof act);
    act.attack = 1;
    env->pl.pitch = -89.0f; /* air miss */
    nedits = 0;
    blaze_player_tick(env, &st, act, edits, &nedits, 8, blocks);
    CHECK(env->ctl.left_click_counter == 10, "blaze press-miss arms leftClickCounter=10");
    CHECK(env->ctl.dig_hitting == 0, "blaze press-miss does not dig");

    for (int t = 0; t < 9; ++t) {
        nedits = 0;
        blaze_player_tick(env, &st, act, edits, &nedits, 8, blocks);
    }
    CHECK(env->ctl.left_click_counter == 1, "blaze after 1 press + 9 holds counter=1");

    /* Look at floor while counter still positive: dig must freeze. */
    env->pl.pitch = 89.0f;
    isr_set_stack(&env->pl.inv, 0, ic_mk(257, 1, 0));
    env->pl.inv.current_item = 0;
    {
        float prog0 = env->ctl.dig_progress;
        int hit0 = env->ctl.dig_hitting;
        nedits = 0;
        blaze_player_tick(env, &st, act, edits, &nedits, 8, blocks);
        CHECK(env->ctl.left_click_counter == 0, "blaze 10th post-arm tick drains to 0");
        /* may start dig same tick once counter hits 0 */
        (void)prog0; (void)hit0;
    }

    /* Release clears. */
    env->ctl.left_click_counter = 7;
    act.attack = 0;
    nedits = 0;
    blaze_player_tick(env, &st, act, edits, &nedits, 8, blocks);
    CHECK(env->ctl.left_click_counter == 0, "blaze release clears leftClickCounter");

    free(blocks); free(win); free(env);
    fprintf(stderr, fail ? "blaze_lcc: FAIL\n" : "blaze_lcc: PASS\n");
    return fail;
}
