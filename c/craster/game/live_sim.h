/* game/live_sim.h - minimal live entity store + plant plot for the playable seam.
 *
 * Ticked each sim frame from app/game_main.c so the world has measurable side
 * effects beyond weather/worldTime: falling item entities and wheat growth.
 */
#ifndef CRASTER_GAME_LIVE_SIM_H
#define CRASTER_GAME_LIVE_SIM_H

#include "game/game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_LIVE_MAX 16

typedef struct {
    int    active;
    int    type;     /* 0 = ground item (EntityItem-like), 1 = hostile marker */
    double x, y, z;
    double mx, my, mz;
    int    on_ground;
    int    age;
    int    item, count, meta;
    int    pickup_delay;
    int    lifespan;
} GmLiveEnt;

typedef struct {
    GmLiveEnt ents[GM_LIVE_MAX];
    int       n_active;
    /* wheat plot (world block coords) advanced with plant_growth-style rolls */
    int       plant_wx, plant_wy, plant_wz;
    int       plant_age;     /* 0..7 wheat meta */
    int       plant_active;
    unsigned  plant_rng;     /* simple LCG for growth rolls */
    int       ticks;
} GmLiveSim;

void gm_live_init(GmLiveSim *s, long long seed, int surface_y);
int  gm_live_spawn_item(GmLiveSim *s, double x, double y, double z,
                        int item, int count, int meta, int pickup_delay);
/* One tick: gravity/friction for live ents (world collision via gm_world_*), wheat growth. */
void gm_live_tick(GmLiveSim *s, GmWorld *w);
/* Same world tick plus vanilla-style pickup into the supplied local-frame player. */
void gm_live_tick_player(GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl,
                         int player_ox, int player_oz);
/* Fill GmEntityView list for rendering; returns count. */
int  gm_live_fill_views(const GmLiveSim *s, GmEntityView *out, int max);
/* Debug counters for harness / logs. */
int  gm_live_entity_moved(const GmLiveSim *s); /* 1 if any ent pos changed last tick */
int  gm_live_plant_age(const GmLiveSim *s);

#ifdef __cplusplus
}
#endif
#endif
