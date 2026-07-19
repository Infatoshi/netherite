#ifndef CRASTER_GAME_DRAGON_LIVE_H
#define CRASTER_GAME_DRAGON_LIVE_H

#include "game/game.h"
#include "ender_dragon_death.h"

enum { GM_ENTITY_CRYSTAL=8, GM_ENTITY_DRAGON=9 };

typedef struct {
    EdeWorld state;
    int initialized;
    int player_attack_cooldown;
    int world_applied;
} GmDragonLive;

void gm_dragon_init(GmDragonLive *d, GmWorld *world, long long seed);
int gm_dragon_player_attack(GmDragonLive *d, const struct PsvPlayer *player, int ox, int oz);
/* Returns 1 once on the tick the active exit podium is applied. */
int gm_dragon_tick(GmDragonLive *d, GmWorld *world, const struct McSinTable *sin_table,
                   double player_x, double player_y, double player_z);
int gm_dragon_fill_views(const GmDragonLive *d, GmEntityView *out, int max);
int gm_dragon_damage_near(GmDragonLive *d,double x,double y,double z,double radius,float damage);

#endif
