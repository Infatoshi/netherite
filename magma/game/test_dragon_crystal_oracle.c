#include "ender_dragon.h"

#include <stdio.h>

static void run_case(int healing, int player_source)
{
    EdArena arena;
    ed_init(&arena, 0);
    arena.dragon.health = 100.0F;
    arena.dragon.phase = ED_PHASE_CIRCLE;
    arena.dragon.phase_ticks = 37;
    arena.dragon.heal_crystal_idx = healing ? 0 : 1;
    arena.crystals[0].x = player_source ? 0.0 : 1000.0;
    arena.crystals[0].y = player_source ? 100.0 : 1000.0;
    arena.crystals[0].z = player_source ? 0.0 : 1000.0;
    arena.player.x = 0.0;
    arena.player.y = 80.0;
    arena.player.z = 0.0;
    if (!ed_mark_crystal_destroyed(&arena, 0)) {
        puts("error");
        return;
    }
    ed_on_crystal_destroyed(&arena, 0, player_source, 1);
    printf("%.1f %d\n", arena.dragon.health,
           arena.dragon.phase == ED_PHASE_STRAFE);
}

int main(void)
{
    run_case(1, 1);
    run_case(0, 1);
    run_case(1, 0);
    return 0;
}
