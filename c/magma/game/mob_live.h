#ifndef MAGMA_GAME_MOB_LIVE_H
#define MAGMA_GAME_MOB_LIVE_H

#include "game/game.h"
#include "game/live_sim.h"
#include "entity_hostile_spine.h"
#include "entity_xp_orb.h"

#define GM_XP_ORBS 64

typedef struct {
    EwStore a, b;
    int current;
    long long seed, tick;
    int next_id;
    int player_attack_cooldown;
    int xp_total;
    McOrb xp_orbs[GM_XP_ORBS];
    int next_orb_id;
    int creeper_fuse[EW_MAX_ENTITIES];
    unsigned char hurt_aggro[EW_MAX_ENTITIES];   /* revenge target set (enderman/spider) */
    int panic_ticks[EW_MAX_ENTITIES];            /* passive flee timer after damage */
    int fire_ticks[EW_MAX_ENTITIES];             /* daylight burn */
    int despawn_ticks[EW_MAX_ENTITIES];          /* ticks spent >32 blocks from player */
    int explosion_pending;
    double explosion_x, explosion_y, explosion_z;
} GmMobLive;

enum {
    GM_MOB_BLAZE=7,
    GM_MOB_SHEEP=10,
    GM_MOB_PIG=11,
    GM_MOB_COW=12,
    GM_MOB_CHICKEN=13,
    GM_ENTITY_XP_ORB=21
};

void gm_mobs_init(GmMobLive *m, long long seed);
/* Component/test hook. Runtime progression never calls this directly. */
int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z);
/* Returns nonzero when attack is aimed at a mob, including cooldown ticks. */
int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player,
                          int ox, int oz, GmLiveSim *drops);
void gm_mobs_tick(GmMobLive *m, GmWorld *world, const struct McSinTable *sin_table,
                  struct PsvPlayer *player, struct PvStats *vitals,
                  int ox, int oz, int dimension, long long world_time, GmLiveSim *drops);
int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max);
int gm_mobs_alive(const GmMobLive *m);
int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops);
int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z);
void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value);

#endif
