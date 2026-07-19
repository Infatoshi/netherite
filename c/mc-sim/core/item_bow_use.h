/* item_bow_use: ItemBow charge + EntityArrow spawn into projectile_motion flight.
 *
 * Subset: ItemBow.onItemRightClick, getMaxItemUseDuration, onPlayerStoppedUsing,
 * getArrowVelocity, EntityArrow(EntityLivingBase), setAim/setThrowableHeading (inaccuracy=0),
 * arrow stack shrink. Per scenario: hold ticks + yaw/pitch -> spawn motion, then pm_arrow_tick.
 *
 * Harness: 6 scenarios (charge levels 1/3/10/15/20/25, varied aim angles/scenes). Pre-spawn dump:
 * charge, arrow_vel/shoot_vel float bits, spawned, critical, arrow_count, motion xyz. Flight:
 * pos xyz per tick + inGround (zeros when charge too weak). Seed picks shooter micro-motion only.
 *
 * READ-ONLY: items_core.h (ICStack pattern), projectile_motion.h (McArrow, pm_arrow_tick, scenes).
 * CUT: Forge onArrowLoose/Nock, infinity/creative, enchants, sounds/stats, RNG inaccuracy,
 * tipped/spectral arrows, bow durability. CPU==CUDA. */
#ifndef MC_ITEM_BOW_USE_H
#define MC_ITEM_BOW_USE_H

#include "mc.h"
#include "mc_math.h"
#include "items_core.h"
#include "projectile_motion.h"

#define IBU_MAX_USE         72000
#define IBU_EYE_HEIGHT      1.62f
#define IBU_EYE_OFFSET      0.10000000149011612
#define IBU_MIN_VEL         0.1
#define IBU_SHOOT_SCALE     3.0f

#define IBU_NUM_SCENARIOS   6
#define IBU_SPAWN_FIELDS    11
#define IBU_FLIGHT_TICKS    24

enum {
    IBU_EVT_NONE  = 0,
    IBU_EVT_START = 1,
    IBU_EVT_STOP  = 2,
    IBU_EVT_DENY  = 3
};

enum {
    IBU_BOW   = 261,
    IBU_ARROW = 262
};

typedef struct {
    i32 bow_item;
    i32 arrow_count;
    i32 use_count;
    int active;
    float yaw;
    float pitch;
    double posX, posY, posZ;
    double motionX, motionY, motionZ;
    int on_ground;
} IbuPlayer;

typedef struct {
    int hold_ticks;
    float yaw;
    float pitch;
    int scene;
    int flight_ticks;
    double px, py, pz;
} IbuScenario;

MC_HD static inline float ibu_get_arrow_velocity(int charge) {
    float f = (float)charge / 20.0f;
    f = (f * f + f * 2.0f) / 3.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

MC_HD static inline int ibu_has_ammo(const IbuPlayer *p) {
    return p->arrow_count > 0;
}

MC_HD static inline int ibu_on_item_right_click(IbuPlayer *p, i32 *event) {
    if (p->active) return 0;
    if (!ibu_has_ammo(p)) {
        if (event) *event = IBU_EVT_DENY;
        return 0;
    }
    p->active = 1;
    p->use_count = IBU_MAX_USE;
    if (event) *event = IBU_EVT_START;
    return 1;
}

MC_HD static inline void ibu_hold_ticks(IbuPlayer *p, int ticks) {
    if (!p->active) return;
    p->use_count -= ticks;
    if (p->use_count < 0) p->use_count = 0;
}

MC_HD static inline u64 ibu_fbits(float f) {
    union { float f; u32 u; } u;
    u.f = f;
    return (u64)u.u;
}

MC_HD static inline u64 ibu_dbits(double d) {
    union { double d; u64 u; } u;
    u.d = d;
    return u.u;
}

/* EntityArrow.setAim + setThrowableHeading (inaccuracy=0, CUT enchants/critical side paths). */
MC_HD static inline void ibu_set_aim(McArrow *a, const McSinTable *st,
                                     float pitch, float yaw, float velocity,
                                     const IbuPlayer *shooter) {
    float f = -mc_sin(st, yaw * 0.017453292f) * mc_cos(st, pitch * 0.017453292f);
    float f1 = -mc_sin(st, pitch * 0.017453292f);
    float f2 = mc_cos(st, yaw * 0.017453292f) * mc_cos(st, pitch * 0.017453292f);
    double x = (double)f;
    double y = (double)f1;
    double z = (double)f2;
    float len = pm_sqrt(x * x + y * y + z * z);
    if (len > 0.0f) {
        x /= (double)len;
        y /= (double)len;
        z /= (double)len;
    }
    x *= (double)velocity;
    y *= (double)velocity;
    z *= (double)velocity;
    a->motionX = x;
    a->motionY = y;
    a->motionZ = z;
    a->motionX += shooter->motionX;
    a->motionZ += shooter->motionZ;
    if (!shooter->on_ground)
        a->motionY += shooter->motionY;
}

MC_HD static inline void ibu_spawn_arrow(McArrow *a, const IbuPlayer *shooter) {
    a->posX = shooter->posX;
    a->posY = shooter->posY + (double)IBU_EYE_HEIGHT - IBU_EYE_OFFSET;
    a->posZ = shooter->posZ;
    a->inGround = 0;
    a->ticksInAir = 0;
}

/* ItemBow.onPlayerStoppedUsing core (player-only subset). Returns spawned flag. */
MC_HD static inline int ibu_on_player_stopped_using(IbuPlayer *p, const McSinTable *st,
                                                    McArrow *a, int *critical) {
    int charge = IBU_MAX_USE - p->use_count;
    float f = ibu_get_arrow_velocity(charge);
    p->active = 0;
    p->use_count = 0;
    *critical = (f == 1.0f) ? 1 : 0;
    if ((double)f < IBU_MIN_VEL) return 0;
    if (!ibu_has_ammo(p)) return 0;
    ibu_spawn_arrow(a, p);
    ibu_set_aim(a, st, p->pitch, p->yaw, f * IBU_SHOOT_SCALE, p);
    p->arrow_count--;
    if (p->arrow_count < 0) p->arrow_count = 0;
    return 1;
}

MC_HD static inline void ibu_init_player(IbuPlayer *p, i64 seed) {
    p->bow_item = IBU_BOW;
    p->arrow_count = 16 + (i32)(seed % 8);
    p->use_count = 0;
    p->active = 0;
    p->yaw = 0.0f;
    p->pitch = 0.0f;
    p->posX = 8.0;
    p->posY = 5.0;
    p->posZ = 8.0;
    p->motionX = (double)((seed & 3) - 1) * 0.01;
    p->motionY = 0.0;
    p->motionZ = (double)(((seed >> 2) & 3) - 1) * 0.01;
    p->on_ground = 1;
}

MC_HD static inline void ibu_scenario(int idx, IbuScenario *s) {
    switch (idx) {
        case 0: /* min charge that fires (~3 ticks) */
            s->hold_ticks = 3;
            s->yaw = 90.0f;
            s->pitch = 0.0f;
            s->scene = 0;
            s->flight_ticks = IBU_FLIGHT_TICKS;
            s->px = 12.0; s->py = 5.0; s->pz = 8.0;
            break;
        case 1: /* partial charge, angled onto floor */
            s->hold_ticks = 10;
            s->yaw = 0.0f;
            s->pitch = -20.0f;
            s->scene = 1;
            s->flight_ticks = IBU_FLIGHT_TICKS;
            s->px = 8.0; s->py = 15.0; s->pz = 8.0;
            break;
        case 2: /* full charge flat shot */
            s->hold_ticks = 20;
            s->yaw = 90.0f;
            s->pitch = -5.0f;
            s->scene = 0;
            s->flight_ticks = IBU_FLIGHT_TICKS;
            s->px = 12.0; s->py = 5.0; s->pz = 4.0;
            break;
        case 3: /* over-charge diagonal into corner */
            s->hold_ticks = 25;
            s->yaw = 45.0f;
            s->pitch = -15.0f;
            s->scene = 2;
            s->flight_ticks = PM_MAX_TICKS;
            s->px = 2.0; s->py = 8.0; s->pz = 2.0;
            break;
        case 4: /* too weak - no spawn */
            s->hold_ticks = 1;
            s->yaw = 0.0f;
            s->pitch = 0.0f;
            s->scene = 0;
            s->flight_ticks = IBU_FLIGHT_TICKS;
            s->px = 12.0; s->py = 5.0; s->pz = 8.0;
            break;
        default: /* full charge open air */
            s->hold_ticks = 20;
            s->yaw = 180.0f;
            s->pitch = 10.0f;
            s->scene = 3;
            s->flight_ticks = 30;
            s->px = 8.0; s->py = 20.0; s->pz = 8.0;
            break;
    }
}

MC_HD static inline void ibu_setup_scene(int scene, u16 *grid) {
    switch (scene) {
        case 0: pm_scene_wall(grid); break;
        case 1: pm_scene_flat(grid); break;
        case 2: pm_scene_corner(grid); break;
        default: pm_scene_open(grid); break;
    }
}

MC_HD static inline int ibu_scenario_out_lines(int idx) {
    IbuScenario s;
    ibu_scenario(idx, &s);
    return IBU_SPAWN_FIELDS + s.flight_ticks * 3 + 1;
}

#define IBU_OUT (84 + 84 + 84 + 156 + 84 + 102)

MC_HD static inline void ibu_emit_u64(u64 v, u64 *out, int *k) {
    out[(*k)++] = v;
}

MC_HD static inline void ibu_run_scenario(int idx, i64 seed, const McSinTable *st, u64 *out, int *k) {
    IbuScenario sc;
    IbuPlayer player;
    McArrow arrow;
    u16 grid[PM_VOL];
    i32 event = IBU_EVT_NONE;
    int critical = 0;
    int spawned = 0;
    int charge;
    float arrow_vel;
    float shoot_vel;

    ibu_scenario(idx, &sc);
    ibu_init_player(&player, seed + (i64)idx * 17);
    player.yaw = sc.yaw;
    player.pitch = sc.pitch;
    player.posX = sc.px;
    player.posY = sc.py;
    player.posZ = sc.pz;

    ibu_on_item_right_click(&player, &event);
    ibu_hold_ticks(&player, sc.hold_ticks);
    charge = sc.hold_ticks;
    arrow_vel = ibu_get_arrow_velocity(charge);
    shoot_vel = arrow_vel * IBU_SHOOT_SCALE;
    spawned = ibu_on_player_stopped_using(&player, st, &arrow, &critical);
    event = IBU_EVT_STOP;

    ibu_setup_scene(sc.scene, grid);

    ibu_emit_u64((u64)(u32)charge, out, k);
    ibu_emit_u64(ibu_fbits(arrow_vel), out, k);
    ibu_emit_u64(ibu_fbits(shoot_vel), out, k);
    ibu_emit_u64((u64)(u32)spawned, out, k);
    ibu_emit_u64((u64)(u32)critical, out, k);
    ibu_emit_u64((u64)(u32)player.arrow_count, out, k);
    ibu_emit_u64(spawned ? ibu_dbits(arrow.motionX) : 0, out, k);
    ibu_emit_u64(spawned ? ibu_dbits(arrow.motionY) : 0, out, k);
    ibu_emit_u64(spawned ? ibu_dbits(arrow.motionZ) : 0, out, k);
    ibu_emit_u64((u64)(u32)event, out, k);

    {
        int t;
        for (t = 0; t < sc.flight_ticks; ++t) {
            if (spawned)
                pm_arrow_tick(&arrow, grid);
            ibu_emit_u64(spawned ? ibu_dbits(arrow.posX) : 0, out, k);
            ibu_emit_u64(spawned ? ibu_dbits(arrow.posY) : 0, out, k);
            ibu_emit_u64(spawned ? ibu_dbits(arrow.posZ) : 0, out, k);
        }
        ibu_emit_u64(spawned ? (u64)(u32)arrow.inGround : 0, out, k);
    }
}

MC_HD static inline void ibu_run(i64 seed, const McSinTable *st, u64 *out) {
    int k = 0;
    int i;
    for (i = 0; i < IBU_NUM_SCENARIOS; ++i)
        ibu_run_scenario(i, seed, st, out, &k);
}

#endif /* MC_ITEM_BOW_USE_H */
