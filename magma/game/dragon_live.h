#ifndef MAGMA_GAME_DRAGON_LIVE_H
#define MAGMA_GAME_DRAGON_LIVE_H

#include "game/game.h"
#include "ender_dragon_death.h"

/* 31 is the render-only EntityEnderCrystal id returned by
 * gm_entity_type_for_name. Keep live and tape views on the same model path. */
enum { GM_ENTITY_CRYSTAL=31, GM_ENTITY_DRAGON=9 };

typedef struct {
    EdeWorld state;
    int initialized;
    int player_attack_cooldown;
    int world_applied;
} GmDragonLive;

typedef struct {
    int index;
    double x, y, z;
} GmDragonCrystalHit;

void gm_dragon_init(GmDragonLive *d, GmWorld *world, long long seed);
/* Returns 1 for a dragon-part hit and 2 for a crystal hit. A crystal hit is
 * marked dead, but its explosion and fight-manager notification remain for
 * the caller so their synchronous Java ordering is preserved. */
int gm_dragon_player_attack(GmDragonLive *d, const struct PsvPlayer *player,
                            int ox, int oz, GmDragonCrystalHit *crystal_hit);
/* Returns 1 once on the tick the active exit podium is applied. */
int gm_dragon_tick(GmDragonLive *d, GmWorld *world, const struct McSinTable *sin_table,
                   double player_x, double player_y, double player_z);
int gm_dragon_fill_views(const GmDragonLive *d, GmEntityView *out, int max);
int gm_dragon_damage_near(GmDragonLive *d,double x,double y,double z,
                          double radius,float damage,
                          GmDragonCrystalHit *crystal_hit);
void gm_dragon_crystal_destroyed(GmDragonLive *d, int index,
                                 int source_is_player,
                                 int player_can_be_targeted);
/* ProjectileHelper candidates for the represented dragon's eight multipart
 * boxes and ten crystals. Positive target IDs are dragon parts; negative IDs
 * are -(crystal index + 1). */
int gm_dragon_projectile_intercept(
    const GmDragonLive *d, double sx, double sy, double sz,
    double ex, double ey, double ez, int *target, double *distance_sq);
/* Dragon parts consume without blaze damage. Crystal hits return 2 and their
 * explosion center; ordinary part hits return 1. */
int gm_dragon_small_fireball_hit(
    GmDragonLive *d, int target, GmDragonCrystalHit *crystal_hit);

#endif
