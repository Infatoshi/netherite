#include "player_survival.h"

#include "game/mob_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "player_vitals.h"
#include "core/config.h"
#include "path_finder.h"

#include <math.h>
#include <string.h>

#define GM_MOB_REACH 2.0
#define GM_MOB_BLOCKS 256
#define GM_MOB_WANDER_INTERVAL 120
#define GM_MOB_WANDER_RADIUS 8
#define GM_MOB_REVENGE_TICKS 101
#define GM_MOB_DESPAWN_SOFT 32.0
#define GM_MOB_DESPAWN_HARD 128.0
#define GM_MOB_DESPAWN_DELAY 600
#define GM_MOB_FIRE_TICKS 160
#define GM_NATURAL_HOSTILE_CAP 70
#define GM_NATURAL_PASSIVE_CAP 10
#define GM_PIGMAN_ANGER_BASE 400
#define GM_PIGMAN_ANGER_RANGE 400
#define GM_PIGMAN_HELP_RANGE 32.0
#define B_SWAMP 6

static EwStore *now_store(GmMobLive *m) { return m->current ? &m->b : &m->a; }
static EwStore *next_store(GmMobLive *m) { return m->current ? &m->a : &m->b; }
static const EwStore *const_store(const GmMobLive *m) { return m->current ? &m->b : &m->a; }

static int gm_hostile(int type){return ehs_is_hostile((u8)type);}
static int gm_passive(int type){
    return type==EW_TYPE_SHEEP||type==EW_TYPE_PIG||type==EW_TYPE_COW||type==EW_TYPE_CHICKEN;
}
static int gm_living(int type){
    return gm_hostile(type)||gm_passive(type)||type==EW_TYPE_BOAT;
}
static int gm_is_slimey(int type){return type==EW_TYPE_SLIME||type==EW_TYPE_MAGMA;}

static int solid_id(int id) {
    if (id == 0) return 0;
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID);
}

static int collect_blocks(GmWorld *w, const McAABB *q, PcfBlock *out, int cap) {
    int n = 0;
    int x0 = mc_floor(q->minX) - 1, x1 = mc_floor(q->maxX) + 1;
    int y0 = mc_floor(q->minY) - 1, y1 = mc_floor(q->maxY) + 1;
    int z0 = mc_floor(q->minZ) - 1, z1 = mc_floor(q->maxZ) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = gm_world_block(w, x, y, z);
                if (!solid_id(id)) continue;
                if (n == cap) return n;
                out[n].block_id = id;
                out[n].ox = x; out[n].oy = y; out[n].oz = z;
                out[n].ladder_facing = 0;
                ++n;
            }
    return n;
}

static float max_health(int type, int size) {
    if (type == EW_TYPE_ENDERMAN) return 40.0f;
    if (type == EW_TYPE_GHAST) return 10.0f;
    if (type == EW_TYPE_SILVERFISH) return 8.0f;
    if (gm_is_slimey(type)) {
        int s = size > 0 ? size : 2;
        return (float)(s * s);
    }
    if (type == EW_TYPE_SHEEP) return 8.0f;
    if (type == EW_TYPE_CHICKEN) return 4.0f;
    if (type == EW_TYPE_PIG || type == EW_TYPE_COW) return 10.0f;
    if (type == EW_TYPE_BOAT) return 40.0f;
    return 20.0f;
}

/* EntityZombie ATTACK_DAMAGE=3; pigman=5; wither skeleton base 4 + stone sword 4;
 * blaze=6 (EntityBlaze.applyEntityAttributes); silverfish base 1; slime damages
 * when size > 1 for size; magma is size + 2. */
static float melee_damage(int type, int size) {
    if (type == EW_TYPE_ENDERMAN) return 7.0f;
    if (type == EW_TYPE_ZOMBIE) return 3.0f;
    if (type == EW_TYPE_PIGMAN) return 5.0f;
    if (type == EW_TYPE_WITHER_SKELETON) return 8.0f;
    if (type == EW_TYPE_BLAZE) return 6.0f;
    if (type == EW_TYPE_SILVERFISH) return 1.0f;
    if (type == EW_TYPE_MAGMA) {
        int s = size > 0 ? size : 1;
        return (float)(s + 2);
    }
    if (type == EW_TYPE_SLIME) {
        int s = size > 0 ? size : 1;
        return s > 1 ? (float)s : 0.0f;
    }
    return 4.0f;
}

/* Apply armor absorb + durability. Returns residual damage to health. */
static float mob_apply_armor(IsrInv *inv, float amount, int bypass_armor)
{
    ITAStack slots[4];
    if (!inv || bypass_armor || amount <= 0.0f) return amount;
    for (int i = 0; i < 4; ++i) {
        ICStack s = isr_get_stack(inv, ISR_ARMOR0 + i);
        slots[i] = ita_mk(s.item, s.meta);
        slots[i].count = s.count;
    }
    ita_damage_armor_set(slots, amount);
    for (int i = 0; i < 4; ++i) {
        if (slots[i].item <= 0 || slots[i].count <= 0)
            isr_set_stack(inv, ISR_ARMOR0 + i, ic_empty());
        else
            isr_set_stack(inv, ISR_ARMOR0 + i,
                          ic_mk(slots[i].item, 1, slots[i].damage));
    }
    return ita_apply_armor_absorb(amount, slots, 0);
}

/* EntityLivingBase.attackEntityFrom hurtResistantTime/lastDamage gate. Returns
 * whether the attack was accepted, as EntityWitherSkeleton.attackEntityAsMob
 * uses that result before adding PotionEffect(WITHER, 200, 0). lastDamage is
 * the RAW pre-armor amount; armor runs in damageEntity after the gate. */
int gm_mobs_attack_player(GmMobLive *m, struct PvStats *vitals_,
                          struct IsrInv *player_inv, float amount,
                          int bypass_armor) {
    PvStats *v=(PvStats *)vitals_;
    float applied;
    if (!m || !v || amount <= 0.0f) return 0;
    if (m->player_hurt_resistant > 10) {
        if (amount <= m->player_last_damage) return 0;
        applied = amount - m->player_last_damage;
        m->player_last_damage = amount;
    } else {
        applied = amount;
        m->player_last_damage = amount;
        m->player_hurt_resistant = 20;
    }
    applied = mob_apply_armor((IsrInv *)player_inv, applied, bypass_armor);
    if (applied > 0.0f) pv_attack(v, applied);
    return 1;
}

void gm_mobs_player_hurt_tick(GmMobLive *m) {
    if (m && m->player_hurt_resistant > 0) --m->player_hurt_resistant;
}

static double follow_range(int type) {
    if (type == EW_TYPE_ZOMBIE) return 40.0;
    if (type == EW_TYPE_GHAST) return 64.0;
    if (type == EW_TYPE_BLAZE) return 48.0;
    if (type == EW_TYPE_ENDERMAN) return 64.0;
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_WITHER_SKELETON) return 16.0;
    if (type == EW_TYPE_SPIDER) return 16.0;
    if (type == EW_TYPE_CREEPER) return 16.0;
    if (type == EW_TYPE_SILVERFISH) return 8.0;
    return 16.0;
}

/* EntityLiving.attackTime / AI attack cooldowns (ticks) after a landed hit
 * or ranged release. Route-roster values from 1.11.2 Entity* classes.
 * Skeleton uses 40 so spawn_hostile_projectiles (reload edge) stays aligned
 * with the arrow emit path. Blaze uses AIFireballAttack (60/6/100), not this. */
static int attack_cooldown_ticks(int type) {
    if (type == EW_TYPE_BLAZE) return 20; /* AIFireballAttack melee branch */
    if (type == EW_TYPE_SKELETON) return 40;
    if (type == EW_TYPE_WITHER_SKELETON) return 20;
    if (type == EW_TYPE_GHAST) return 40;
    if (type == EW_TYPE_SILVERFISH) return 20;
    if (type == EW_TYPE_SPIDER) return 20;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_PIGMAN) return 20;
    if (type == EW_TYPE_ENDERMAN) return 20;
    return 20;
}

/* EntityBoat.deltaRotation + land boatGlide (per-slot; not in EwStore). */
static float s_boat_delta_rot[EW_MAX_ENTITIES];
static float s_boat_glide[EW_MAX_ENTITIES];

static void reset_slot_state_s(GmMobLive *m, EwStore *s, int slot) {
    if (slot < 0 || slot >= EW_MAX_ENTITIES) return;
    if (s) s->repath_timer[slot] = GM_MOB_WANDER_INTERVAL;
    m->creeper_fuse[slot] = 0;
    m->hurt_aggro[slot] = 0;
    m->panic_ticks[slot] = 0;
    m->passive_tasks[slot] = 0;
    m->passive_task_tick[slot] = 0;
    m->passive_watch_time[slot] = 0;
    m->passive_idle_time[slot] = 0;
    m->passive_eat_time[slot] = 0;
    m->passive_idle_x[slot] = 0.0;
    m->passive_idle_z[slot] = 0.0;
    m->passive_nav_speed[slot] = 0.0;
    m->passive_head_yaw[slot] = s ? s->yaw[slot] : 0.0f;
    m->passive_head_pitch[slot] = 0.0f;
    m->passive_render_yaw[slot] = m->passive_head_yaw[slot];
    m->passive_prev_head_yaw[slot] = m->passive_head_yaw[slot];
    m->passive_body_ticks[slot] = 0;
    m->passive_sheared[slot] = 0;
    m->ent_jr_seed[slot] = 0;
    m->living_sound_time[slot] = 0;
    m->entity_age[slot] = 0;
    m->chicken_egg[slot] = 0;
    m->det_nav_n[slot] = 0;
    m->det_nav_i[slot] = 0;
    m->fire_ticks[slot] = 0;
    m->despawn_ticks[slot] = 0;
    m->anger[slot] = 0;
    m->jump_delay[slot] = 0;
    m->charge[slot] = 0;
    m->blaze_on_fire[slot] = 0;
    m->boat_damage[slot] = 0;
    s_boat_delta_rot[slot] = 0.0f;
    s_boat_glide[slot] = 0.8f;
    if (!m->size[slot]) m->size[slot] = gm_is_slimey(s ? s->type[slot] : 0) ? 2 : 1;
}

/* Chunk.getRandomWithSeed(987234911).nextInt(10)==0 slime-chunk test. */
static int is_slime_chunk(long long world_seed, int cx, int cz) {
    i64 seed = (i64)world_seed
        + (i64)cx * (i64)cx * 4987142LL
        + (i64)cx * 5947611LL
        + (i64)cz * (i64)cz * 4392871LL
        + (i64)cz * 389711LL;
    seed ^= 987234911LL;
    u64 s = (u64)(seed ^ 0x5DEECE66DLL) & ((1ULL << 48) - 1ULL);
    for (;;) {
        s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1ULL);
        int bits = (int)(s >> 17);
        int val = bits % 10;
        if (bits - val + 9 >= 0) return val == 0;
    }
}

static void mark_hurt(GmMobLive *m, EwStore *s, int slot) {
    m->hurt_aggro[slot] = 1;
    if (gm_passive(s->type[slot])) m->panic_ticks[slot] = GM_MOB_REVENGE_TICKS;
    /* EntityPigZombie.becomeAngryAt + AIHurtByAggressor group help. */
    if (s->type[slot] == EW_TYPE_PIGMAN) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, slot, s->id[slot], 0, 0x414E4752u);
        m->anger[slot] = GM_PIGMAN_ANGER_BASE + (int)mc_hash_bound(h, GM_PIGMAN_ANGER_RANGE);
        for (int j = 1; j < EW_MAX_ENTITIES; ++j) {
            if (j == slot || !s->alive[j] || s->type[j] != EW_TYPE_PIGMAN) continue;
            if (m->entity_dimension[j] != m->entity_dimension[slot]) continue;
            double dx = s->x[j] - s->x[slot], dy = s->y[j] - s->y[slot], dz = s->z[j] - s->z[slot];
            if (dx * dx + dy * dy + dz * dz > GM_PIGMAN_HELP_RANGE * GM_PIGMAN_HELP_RANGE) continue;
            u64 h2 = mc_hash_seed((u64)m->seed, m->tick, j, s->id[j], 0, 0x414E4752u);
            m->anger[j] = GM_PIGMAN_ANGER_BASE + (int)mc_hash_bound(h2, GM_PIGMAN_ANGER_RANGE);
            m->hurt_aggro[j] = 1;
        }
    }
}

static int los_clear(GmWorld *w, double x0, double y0, double z0,
                     double x1, double y1, double z1) {
    double dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    int steps = (int)(d * 2.0);
    if (steps < 1) return 1;
    if (steps > 96) steps = 96;
    for (int s = 1; s < steps; ++s) {
        double t = (double)s / (double)steps;
        if (solid_id(gm_world_block(w, mc_floor(x0 + dx * t),
                                    mc_floor(y0 + dy * t),
                                    mc_floor(z0 + dz * t)))) return 0;
    }
    return 1;
}

static int wander_ground_y(GmWorld *w, int x, int y0, int z) {
    for (int yy = y0 + 1; yy >= y0 - 3; --yy) {
        if (yy < 1) break;
        if (solid_id(gm_world_block(w, x, yy - 1, z)) &&
            !solid_id(gm_world_block(w, x, yy, z)) &&
            !solid_id(gm_world_block(w, x, yy + 1, z))) return yy;
    }
    return -1000;
}

/* Passive AI is a direct port of the 1.11.2 EntityAITasks goal lists. Mate,
 * tempt, and follow-parent are deliberately absent from the runnable set:
 * live animals have no growingAge/inLove state and breeding items/interact are
 * a product cut, so each oracle shouldExecute is currently always false. */
enum {
    PAI_SWIM = 0,
    PAI_PANIC,
    PAI_EAT,
    PAI_WANDER,
    PAI_WATCH,
    PAI_IDLE,
    PAI_NTASKS
};

#define PAI_BIT(t) (1u << (t))
#define PAI_RNG 0x50414952u

static int pai_priority(int type, int task) {
    if (task == PAI_SWIM) return 0;
    if (task == PAI_PANIC) return 1;
    if (type == EW_TYPE_SHEEP) {
        if (task == PAI_EAT) return 5;
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_PIG) {
        if (task == PAI_WANDER) return 6;
        if (task == PAI_WATCH) return 7;
        if (task == PAI_IDLE) return 8;
    } else if (type == EW_TYPE_COW || type == EW_TYPE_CHICKEN) {
        if (task == PAI_WANDER) return 5;
        if (task == PAI_WATCH) return 6;
        if (task == PAI_IDLE) return 7;
    }
    return 99;
}

static int pai_mutex(int task) {
    if (task == PAI_SWIM) return 4;
    if (task == PAI_PANIC || task == PAI_WANDER) return 1;
    if (task == PAI_WATCH) return 2;
    if (task == PAI_IDLE) return 3;
    if (task == PAI_EAT) return 7;
    return 0;
}

static double pai_attribute_speed(int type) {
    if (type == EW_TYPE_SHEEP) return 0.23000000417232513;
    if (type == EW_TYPE_PIG || type == EW_TYPE_CHICKEN) return 0.25;
    if (type == EW_TYPE_COW) return 0.20000000298023224;
    return 0.23000000417232513;
}

static double pai_panic_multiplier(int type) {
    if (type == EW_TYPE_COW) return 2.0;
    if (type == EW_TYPE_CHICKEN) return 1.4;
    return 1.25; /* sheep, pig */
}

static void pai_size(int type, float *width, float *height) {
    *width = 0.9f;
    if (type == EW_TYPE_SHEEP) *height = 1.3f;
    else if (type == EW_TYPE_PIG) *height = 0.9f;
    else if (type == EW_TYPE_COW) *height = 1.4f;
    else { *width = 0.4f; *height = 0.7f; }
}

static double pai_eye_height(int type) {
    float width, height;
    pai_size(type, &width, &height);
    (void)width;
    if (type == EW_TYPE_SHEEP) return (double)(0.95f * height);
    if (type == EW_TYPE_COW) return 1.3;
    if (type == EW_TYPE_CHICKEN) return (double)height;
    return (double)(height * 0.85f);
}

static u64 pai_rng_start(const GmMobLive *m, const EwStore *s, int i, int task) {
    return mc_hash_seed((u64)m->seed, m->tick, i, s->id[i], task, PAI_RNG);
}

static u64 pai_rng_next(u64 *stream) {
    *stream = mc_hash64(*stream + 0x9E3779B97F4A7C15ULL);
    return *stream;
}

static int pai_rng_bound(u64 *stream, int bound) {
    return mc_hash_bound(pai_rng_next(stream), bound);
}

static float pai_rng_float(u64 *stream) {
    return mc_hash_f01(pai_rng_next(stream));
}

/* java.util.Random.nextDouble consumes 26 then 27 bits. The runtime uses the
 * mandated hash stream, but preserves that two-draw shape. */
static double pai_rng_double(u64 *stream) {
    u64 a = pai_rng_next(stream), b = pai_rng_next(stream);
    u64 hi = (u64)(mc_hash_u32(a) >> 6);
    u64 lo = (u64)(mc_hash_u32(b) >> 5);
    return (double)((hi << 27) + lo) * (1.0 / 9007199254740992.0);
}

/* Draw-site census: magma/game/entity_rand_census.tsv. Hash path (det off)
 * is unchanged: these helpers only swap the generator when det_entity_rng=1. */
static int pai_det(void) { return cr_cfg()->det_entity_rng; }
static JavaRandom *pai_jr(GmMobLive *m, int i) {
    return (JavaRandom *)&m->ent_jr_seed[i];
}
static int pai_bound(GmMobLive *m, int i, u64 *stream, int bound) {
    if (pai_det()) return jrand_int_bound(pai_jr(m, i), bound);
    return pai_rng_bound(stream, bound);
}
static float pai_float(GmMobLive *m, int i, u64 *stream) {
    if (pai_det()) return jrand_float(pai_jr(m, i));
    return pai_rng_float(stream);
}
static double pai_double(GmMobLive *m, int i, u64 *stream) {
    if (pai_det()) return jrand_double(pai_jr(m, i));
    return pai_rng_double(stream);
}

static int pai_in_material(GmWorld *w, const EwStore *s, int i, int lava) {
    float width, height;
    pai_size(s->type[i], &width, &height);
    double inset = lava ? 0.10000000149011612 : 0.001;
    int x0 = mc_floor(s->x[i] - width * 0.5 + inset);
    int x1 = mc_floor(s->x[i] + width * 0.5 - inset);
    int z0 = mc_floor(s->z[i] - width * 0.5 + inset);
    int z1 = mc_floor(s->z[i] + width * 0.5 - inset);
    int y0 = mc_floor(s->y[i] - 0.4000000059604645 + (lava ? 0.0 : 0.001));
    int y1 = mc_floor(s->y[i] + height - (lava ? 0.0 : 0.001));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = gm_world_block(w, x, y, z);
                if ((!lava && (id == 8 || id == 9)) ||
                    (lava && (id == 10 || id == 11))) return 1;
            }
    return 0;
}

/* EntityAIPanic.getRandPos: burning creatures choose the closest water block
 * in the inclusive 5 x 4 x 5 search before RandomPositionGenerator. */
static int pai_nearest_water(GmWorld *w, const EwStore *s, int i,
                             double *out_x, double *out_y, double *out_z) {
    int ox = mc_floor(s->x[i]), oy = mc_floor(s->y[i]), oz = mc_floor(s->z[i]);
    float best = 5.0f * 5.0f * 4.0f * 2.0f;
    int found = 0;
    for (int x = ox - 5; x <= ox + 5; ++x) {
        for (int y = oy - 4; y <= oy + 4; ++y) {
            for (int z = oz - 5; z <= oz + 5; ++z) {
                int id = gm_world_block(w, x, y, z);
                if (id != 8 && id != 9) continue;
                float dx = (float)(x - ox), dy = (float)(y - oy), dz = (float)(z - oz);
                float dist = dx * dx + dy * dy + dz * dz;
                if (dist < best) {
                    best = dist;
                    *out_x = x; *out_y = y; *out_z = z;
                    found = 1;
                }
            }
        }
    }
    return found;
}

static float pai_brightness(GmWorld *w, int x, int y, int z) {
    int light = gm_world_sky_light(w, x, y, z);
    int block = gm_world_block_light(w, x, y, z);
    if (block > light) light = block;
    if (light < 0) light = 0;
    if (light > 15) light = 15;
    float f1 = 1.0f - (float)light / 15.0f;
    return (1.0f - f1) / (f1 * 3.0f + 1.0f);
}

/* RandomPositionGenerator.generateRandomPos: ten triples, strict-greater best
 * weight, animal grass preference, and getLandPos water rejection. Returned
 * coordinates intentionally use the original offsets, as the Java method
 * does even after moveAboveSolid is used only for validation/scoring. */
static int pai_random_position(GmMobLive *m, GmWorld *w, const EwStore *s, int i,
                               int xz, int yrange, int land, u64 *stream,
                               double *out_x, double *out_y, double *out_z) {
    int found = 0, best_dx = 0, best_dy = 0, best_dz = 0;
    float best = -99999.0f;
    for (int k = 0; k < 10; ++k) {
        int dx = pai_bound(m, i, stream, 2 * xz + 1) - xz;
        int dy = pai_bound(m, i, stream, 2 * yrange + 1) - yrange;
        int dz = pai_bound(m, i, stream, 2 * xz + 1) - xz;
        int bx = mc_floor(s->x[i] + dx);
        int by = mc_floor(s->y[i] + dy);
        int bz = mc_floor(s->z[i] + dz);
        if (by <= 0 || !solid_id(gm_world_block(w, bx, by - 1, bz))) continue;
        int score_y = by;
        if (land && solid_id(gm_world_block(w, bx, score_y, bz))) {
            while (score_y < 256 && solid_id(gm_world_block(w, bx, score_y, bz)))
                ++score_y;
        }
        if (land) {
            int id = gm_world_block(w, bx, score_y, bz);
            if (id == 8 || id == 9) continue;
        }
        float score = gm_world_block(w, bx, score_y - 1, bz) == 2
                    ? 10.0f : pai_brightness(w, bx, score_y, bz) - 0.5f;
        if (score > best) {
            best = score;
            best_dx = dx; best_dy = dy; best_dz = dz;
            found = 1;
        }
    }
    if (!found) return 0;
    *out_x = s->x[i] + best_dx;
    *out_y = s->y[i] + best_dy;
    *out_z = s->z[i] + best_dz;
    return 1;
}

#define PAI_NAV_MAX 48

static int pai_mc_to_pb(int id) {
    if (id == 0) return PB_AIR;
    if (id == 8 || id == 9) return PB_WATER;
    if (id == 10 || id == 11) return PB_LAVA;
    if (id == 51) return PB_FIRE;
    if (id == 81) return PB_CACTUS;
    if (id == 85 || id == 113 || id == 188 || id == 189 || id == 190 ||
        id == 191 || id == 192 || id == 139 || id == 107)
        return PB_FENCE;
    if (id == 96 || id == 167) return PB_TRAPDOOR;
    if (id == 27 || id == 28 || id == 66 || id == 157) return PB_RAIL;
    if (!solid_id(id)) return PB_AIR;
    return PB_STONE;
}

static int pai_ceil_f(float v) {
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

static int pai_chunk16(int x) {
    return x >= 0 ? x / 16 : -(((-x) + 15) / 16);
}

static Pf12 pai_pf;

static void pai_fill_pf(GmWorld *w, const EwStore *s, int i, int *ox, int *oy, int *oz) {
    float width, height;
    int x, y, z;
    memset(pai_pf.blocks, 0, sizeof pai_pf.blocks);
    pai_pf.overflow = 0;
    *ox = mc_floor(s->x[i]) - 16;
    *oy = mc_floor(s->y[i]) - 8;
    *oz = mc_floor(s->z[i]) - 16;
    if (*oy < 0) *oy = 0;
    gm_world_ensure(w, pai_chunk16(*ox), pai_chunk16(*oz), 2);
    gm_world_ensure(w, pai_chunk16(*ox + 31), pai_chunk16(*oz + 31), 2);
    for (y = 0; y < PNP_DY; ++y)
        for (z = 0; z < PNP_DZ; ++z)
            for (x = 0; x < PNP_DX; ++x)
                pnp_setblock(pai_pf.blocks, x, y, z,
                             pai_mc_to_pb(gm_world_block(w, *ox + x, *oy + y, *oz + z)));
    memset(&pai_pf.ent, 0, sizeof pai_pf.ent);
    pai_size(s->type[i], &width, &height);
    pai_pf.ent.width = width;
    pai_pf.ent.height = height;
    pai_pf.ent.stepHeight = 0.6f;
    pai_pf.ent.canSwim = 0;
    pai_pf.ent.canEnterDoors = 1;
    pai_pf.ent.maxFallHeight = 3;
    pai_pf.ent.onGround = s->on_ground[i] ? 1 : 0;
    pai_pf.ent.inWater = pai_in_material(w, s, i, 0);
    pai_pf.ent.posX = s->x[i] - (double)(*ox);
    pai_pf.ent.posY = s->y[i] - (double)(*oy);
    pai_pf.ent.posZ = s->z[i] - (double)(*oz);
    pnp_ent_default_priorities(&pai_pf.ent);
}

static int pai_position_clear(int ox, int oy, int oz,
        int x, int y, int z, int sizeX, int sizeY, int sizeZ,
        double vx, double vz, double d0, double d1) {
    int ix, iy, iz;
    for (ix = x; ix < x + sizeX; ++ix)
        for (iy = y; iy < y + sizeY; ++iy)
            for (iz = z; iz < z + sizeZ; ++iz) {
                double dx = (double)ix + 0.5 - vx;
                double dz = (double)iz + 0.5 - vz;
                if (dx * d0 + dz * d1 >= 0.0) {
                    int id = pnp_getblock(pai_pf.blocks, ix - ox, iy - oy, iz - oz);
                    if (!pnp_blockdef(id).isPassable) return 0;
                }
            }
    return 1;
}

/* PathNavigateGround.isSafeToStandAt. Node-type queries are WalkNodeProcessor
 * getPathNodeType size-sweep (canBreakDoors=true, canEnterDoors=true). */
static int pai_safe_stand(int ox, int oy, int oz,
        int x, int y, int z, int sizeX, int sizeY, int sizeZ,
        double vx, double vz, double d0, double d1) {
    int i = x - sizeX / 2;
    int j = z - sizeZ / 2;
    int k, l;
    if (!pai_position_clear(ox, oy, oz, i, y, j, sizeX, sizeY, sizeZ, vx, vz, d0, d1))
        return 0;
    for (k = i; k < i + sizeX; ++k) {
        for (l = j; l < j + sizeZ; ++l) {
            double dx = (double)k + 0.5 - vx;
            double dz = (double)l + 0.5 - vz;
            if (dx * d0 + dz * d1 >= 0.0) {
                int t, t2;
                float f;
                if (!pnp_in(k - ox, (y - 1) - oy, l - oz) ||
                    !pnp_in(k - ox, y - oy, l - oz))
                    return 0;
                t = pnp_getPathNodeTypeSize(&pai_pf, k - ox, (y - 1) - oy, l - oz,
                                           sizeX, sizeY, sizeZ, 1, 1);
                if (t == PNT_WATER || t == PNT_LAVA || t == PNT_OPEN) return 0;
                t2 = pnp_getPathNodeTypeSize(&pai_pf, k - ox, y - oy, l - oz,
                                            sizeX, sizeY, sizeZ, 1, 1);
                f = pnp_getPathPriority(&pai_pf.ent, t2);
                if (f < 0.0f || f >= 8.0f) return 0;
                if (t2 == PNT_DAMAGE_FIRE || t2 == PNT_DANGER_FIRE || t2 == PNT_DAMAGE_OTHER)
                    return 0;
            }
        }
    }
    return 1;
}

/* PathNavigateGround.isDirectPathBetweenPoints. */
static int pai_direct_path(int ox, int oy, int oz,
        double x1, double y1, double z1, double x2, double y2, double z2,
        int sizeX, int sizeY, int sizeZ) {
    int i = pnp_floor_d(x1);
    int j = pnp_floor_d(z1);
    double d0 = x2 - x1;
    double d1 = z2 - z1;
    double d2 = d0 * d0 + d1 * d1;
    double d3, d4, d5, d6, d7;
    int k, l, i1, j1, k1, l1;
    (void)y2;
    if (d2 < 1.0e-8) return 0;
    d3 = 1.0 / sqrt(d2);
    d0 *= d3;
    d1 *= d3;
    sizeX += 2;
    sizeZ += 2;
    if (!pai_safe_stand(ox, oy, oz, i, (int)y1, j, sizeX, sizeY, sizeZ, x1, z1, d0, d1))
        return 0;
    sizeX -= 2;
    sizeZ -= 2;
    d4 = 1.0 / fabs(d0);
    d5 = 1.0 / fabs(d1);
    d6 = (double)i - x1;
    d7 = (double)j - z1;
    if (d0 >= 0.0) ++d6;
    if (d1 >= 0.0) ++d7;
    d6 = d6 / d0;
    d7 = d7 / d1;
    k = d0 < 0.0 ? -1 : 1;
    l = d1 < 0.0 ? -1 : 1;
    i1 = pnp_floor_d(x2);
    j1 = pnp_floor_d(z2);
    k1 = i1 - i;
    l1 = j1 - j;
    while (k1 * k > 0 || l1 * l > 0) {
        if (d6 < d7) {
            d6 += d4;
            i += k;
            k1 = i1 - i;
        } else {
            d7 += d5;
            j += l;
            l1 = j1 - j;
        }
        if (!pai_safe_stand(ox, oy, oz, i, (int)y1, j, sizeX, sizeY, sizeZ, x1, z1, d0, d1))
            return 0;
    }
    return 1;
}

static void pai_nav_apply(GmMobLive *m, EwStore *s, int i) {
    float width, height;
    int off, idx, n;
    pai_size(s->type[i], &width, &height);
    n = m->det_nav_n[i];
    idx = m->det_nav_i[i];
    if (n <= 0 || idx >= n) {
        s->path_len[i] = 0;
        return;
    }
    off = pnp_floor_d((double)(width + 1.0f));
    s->path_tx[i] = (double)m->det_nav_x[i][idx] + (double)off * 0.5;
    s->path_ty[i] = (double)m->det_nav_y[i][idx];
    s->path_tz[i] = (double)m->det_nav_z[i][idx] + (double)off * 0.5;
    s->path_len[i] = 1;
}

/* PathNavigate.pathFollow: close-advance then isDirectPathBetweenPoints skip.
 * Called after goalSelector (setPath) to match EntityLiving.updateEntityActionState. */
static void pai_nav_follow(GmMobLive *m, GmWorld *w, EwStore *s, int i) {
    float width, height, maxDist;
    int idx, n, j, same_y_end, k, l, i1, ox, oy, oz;
    double ex, ey, ez;
    if (!pai_det() || m->det_nav_n[i] == 0) return;
    pai_size(s->type[i], &width, &height);
    maxDist = width > 0.75f ? width * 0.5f : 0.75f - width * 0.5f;
    idx = m->det_nav_i[i];
    n = m->det_nav_n[i];
    ex = s->x[i];
    ey = (double)((int)(s->y[i] + 0.5));
    ez = s->z[i];
    same_y_end = n;
    for (j = idx; j < n; ++j) {
        if ((double)m->det_nav_y[i][j] != floor(ey)) {
            same_y_end = j;
            break;
        }
    }
    if (idx < n) {
        double cx = (double)m->det_nav_x[i][idx] + 0.5;
        double cy = (double)m->det_nav_y[i][idx];
        double cz = (double)m->det_nav_z[i][idx] + 0.5;
        if (fabsf((float)(s->x[i] - cx)) < maxDist &&
            fabsf((float)(s->z[i] - cz)) < maxDist &&
            fabs(s->y[i] - cy) < 1.0)
            m->det_nav_i[i] = (unsigned char)(idx + 1);
    }
    k = pai_ceil_f(width);
    l = pai_ceil_f(height);
    i1 = k;
    if (m->det_nav_i[i] < n) {
        pai_fill_pf(w, s, i, &ox, &oy, &oz);
        for (j = same_y_end - 1; j >= (int)m->det_nav_i[i]; --j) {
            int off = pnp_floor_d((double)(width + 1.0f));
            double tx = (double)m->det_nav_x[i][j] + (double)off * 0.5;
            double ty = (double)m->det_nav_y[i][j];
            double tz = (double)m->det_nav_z[i][j] + (double)off * 0.5;
            if (pai_direct_path(ox, oy, oz, ex, ey, ez, tx, ty, tz, k, l, i1)) {
                m->det_nav_i[i] = (unsigned char)j;
                break;
            }
        }
    }
    pai_nav_apply(m, s, i);
}

static int pai_find_path(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                         double tx, double ty, double tz) {
    int ox, oy, oz, n, k;
    pai_fill_pf(w, s, i, &ox, &oy, &oz);
    n = pf12_findPath(&pai_pf,
                      (double)mc_floor(tx) + 0.5 - (double)ox,
                      (double)((int)ty) + 0.5 - (double)oy,
                      (double)mc_floor(tz) + 0.5 - (double)oz,
                      16.0f);
    if (n <= 0) {
        m->det_nav_n[i] = 0;
        return 0;
    }
    if (n > PAI_NAV_MAX) n = PAI_NAV_MAX;
    m->det_nav_n[i] = (unsigned char)n;
    m->det_nav_i[i] = 0;
    for (k = 0; k < n; ++k) {
        m->det_nav_x[i][k] = (short)(pai_pf.resultPts[k * 3 + 0] + ox);
        m->det_nav_y[i][k] = (short)(pai_pf.resultPts[k * 3 + 1] + oy);
        m->det_nav_z[i][k] = (short)(pai_pf.resultPts[k * 3 + 2] + oz);
    }
    /* setPath does not pathFollow. onUpdateNavigation later this tick does. */
    s->path_len[i] = 1;
    return 1;
}

static void pai_set_path(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                         double x, double y, double z, double speed) {
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    s->path_tx[i] = x; s->path_ty[i] = y; s->path_tz[i] = z;
    /* Knob-off keeps the old 3-high standability gate. Det runs PathFinder
     * (pathfinding12 WalkNodeProcessor) and MOVE_TO follows PathPoints. */
    if (pai_det()) {
        if (!pai_find_path(m, w, s, i, x, y, z))
            s->path_len[i] = 0;
    } else {
        s->path_len[i] = by > 0 && solid_id(gm_world_block(w, bx, by - 1, bz)) &&
                          !solid_id(gm_world_block(w, bx, by, bz)) &&
                          !solid_id(gm_world_block(w, bx, by + 1, bz));
    }
    m->passive_nav_speed[i] = speed;
}

static int pai_path_done(GmWorld *w, const EwStore *s, int i) {
    float width, height;
    pai_size(s->type[i], &width, &height);
    (void)height;
    float waypoint = width > 0.75f ? width * 0.5f : 0.75f - width * 0.5f;
    if (fabs(s->x[i] - s->path_tx[i]) < waypoint &&
        fabs(s->z[i] - s->path_tz[i]) < waypoint &&
        fabs(s->y[i] - s->path_ty[i]) < 1.0) return 1;
    /* PathNavigate.noPath is the Path object. The RPG cell being solid is
     * not a stop; A* walks to a neighbour. Det uses distance only. */
    if (pai_det()) return 0;
    int bx = mc_floor(s->path_tx[i]), by = mc_floor(s->path_ty[i]);
    int bz = mc_floor(s->path_tz[i]);
    return !solid_id(gm_world_block(w, bx, by - 1, bz)) ||
           solid_id(gm_world_block(w, bx, by, bz));
}

static float pai_wrap_degrees(float v) {
    v = fmodf(v, 360.0f);
    if (v >= 180.0f) v -= 360.0f;
    if (v < -180.0f) v += 360.0f;
    return v;
}

static float pai_update_rotation(float current, float target, float max_delta) {
    float d = pai_wrap_degrees(target - current);
    if (d > max_delta) d = max_delta;
    if (d < -max_delta) d = -max_delta;
    return current + d;
}

/* EntityMoveHelper.limitAngle: wrap the result into [0, 360]. */
static float pai_limit_angle(float current, float target, float max_delta) {
    float f1 = pai_update_rotation(current, target, max_delta);
    if (f1 < 0.0f) f1 += 360.0f;
    else if (f1 > 360.0f) f1 -= 360.0f;
    return f1;
}

/* EntityLookHelper / EntityMoveHelper fold `180D / Math.PI` to a double
 * constant in bytecode, but remainder hyaw on the 1.11.2 oracle matches
 * multiplying the LUT result by (float)(180.0 / (float)Math.PI). */
static float pai_deg(double rad) {
    return (float)(rad * (float)(180.0 / (float)MC_PI));
}

static float pai_atan2_yaw(double dz, double dx) {
    return pai_deg(mc_atan2(dz, dx)) - 90.0f;
}

static int pai_can_use(const GmMobLive *m, int type, int i, int task) {
    int pri = pai_priority(type, task), mutex = pai_mutex(task);
    for (int other = 0; other < PAI_NTASKS; ++other) {
        if (other == task || !(m->passive_tasks[i] & PAI_BIT(other))) continue;
        if (pri >= pai_priority(type, other) && (mutex & pai_mutex(other))) return 0;
        /* All vanilla tasks in these four lists are interruptible. */
    }
    return 1;
}

static int pai_continue(const GmMobLive *m, GmWorld *w, const EwStore *s, int i,
                        int task, double px, double py, double pz) {
    if (task == PAI_SWIM) return pai_in_material(w, s, i, 0) || pai_in_material(w, s, i, 1);
    if (task == PAI_PANIC || task == PAI_WANDER) return s->path_len[i] != 0;
    if (task == PAI_EAT) return m->passive_eat_time[i] > 0;
    if (task == PAI_WATCH) {
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        return dx * dx + dy * dy + dz * dz <= 36.0 && m->passive_watch_time[i] > 0;
    }
    if (task == PAI_IDLE) return m->passive_idle_time[i] >= 0;
    return 0;
}

static void pai_reset(GmMobLive *m, EwStore *s, int i, int task) {
    m->passive_tasks[i] &= ~PAI_BIT(task);
    if (task == PAI_EAT) m->passive_eat_time[i] = 0;
    if (task == PAI_WATCH) m->passive_watch_time[i] = 0;
    if (pai_det() && (task == PAI_PANIC || task == PAI_WANDER)) {
        m->det_nav_n[i] = 0;
        s->path_len[i] = 0;
    }
}

static int pai_try_start(GmMobLive *m, GmWorld *w, EwStore *s, int i, int task,
                         double px, double py, double pz) {
    u64 stream = pai_rng_start(m, s, i, task);
    double x, y, z;
    if (task == PAI_SWIM) {
        if (!pai_in_material(w, s, i, 0) && !pai_in_material(w, s, i, 1)) return 0;
    } else if (task == PAI_PANIC) {
        int burning = m->fire_ticks[i] > 0;
        if (m->panic_ticks[i] <= 0 && !burning) return 0;
        int found = burning && pai_nearest_water(w, s, i, &x, &y, &z);
        if (!found)
            found = pai_random_position(m, w, s, i, 5, 4, 0, &stream, &x, &y, &z);
        if (!found) return 0;
        pai_set_path(m, w, s, i, x, y, z, pai_panic_multiplier(s->type[i]));
    } else if (task == PAI_EAT) {
        if (pai_bound(m, i, &stream, 1000) != 0) return 0;
        int bx = mc_floor(s->x[i]), by = mc_floor(s->y[i]), bz = mc_floor(s->z[i]);
        int tall_grass = gm_world_block(w, bx, by, bz) == 31 &&
                         gm_world_meta(w, bx, by, bz) == 1;
        if (!tall_grass && gm_world_block(w, bx, by - 1, bz) != 2) return 0;
        m->passive_eat_time[i] = 40;
        s->path_len[i] = 0;
    } else if (task == PAI_WANDER) {
        if (pai_det() && m->entity_age[i] >= 100) return 0;
        if (pai_bound(m, i, &stream, 120) != 0) return 0;
        int ok;
        if (pai_in_material(w, s, i, 0)) {
            ok = pai_random_position(m, w, s, i, 15, 7, 1, &stream, &x, &y, &z);
            if (!ok) ok = pai_random_position(m, w, s, i, 10, 7, 0, &stream, &x, &y, &z);
        } else {
            int land = pai_float(m, i, &stream) >= 0.001f;
            ok = pai_random_position(m, w, s, i, 10, 7, land, &stream, &x, &y, &z);
        }
        if (!ok) return 0;
        pai_set_path(m, w, s, i, x, y, z, 1.0);
    } else if (task == PAI_WATCH) {
        if (pai_float(m, i, &stream) >= 0.02f) return 0;
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        if (dx * dx + dy * dy + dz * dz > 36.0) return 0;
        m->passive_watch_time[i] = 40 + pai_bound(m, i, &stream, 40);
    } else if (task == PAI_IDLE) {
        if (pai_float(m, i, &stream) >= 0.02f) return 0;
        double angle = 2.0 * MC_PI * pai_double(m, i, &stream);
        m->passive_idle_x[i] = cos(angle);
        m->passive_idle_z[i] = sin(angle);
        m->passive_idle_time[i] = 20 + pai_bound(m, i, &stream, 20);
    } else return 0;
    m->passive_tasks[i] |= PAI_BIT(task);
    return 1;
}

static void pai_look_update(GmMobLive *m, const EwStore *s, int i, int looking,
                            double look_x, double look_y, double look_z) {
    float pitch = 0.0f;
    float head = m->passive_head_yaw[i];
    float body = pai_det() ? m->passive_render_yaw[i] : s->yaw[i];
    if (looking) {
        double dx = look_x - s->x[i];
        double dy = look_y - (s->y[i] + pai_eye_height(s->type[i]));
        double dz = look_z - s->z[i];
        double horiz;
        float target_yaw, target_pitch;
        if (pai_det()) {
            /* EntityLookHelper: MathHelper.sqrt + MathHelper.atan2 LUT. */
            horiz = (double)(float)sqrt(dx * dx + dz * dz);
            target_yaw = pai_atan2_yaw(dz, dx);
            target_pitch = -pai_deg(mc_atan2(dy, horiz));
        } else {
            horiz = sqrt(dx * dx + dz * dz);
            target_yaw = (float)(atan2(dz, dx) * (180.0 / MC_PI)) - 90.0f;
            target_pitch = (float)(-(atan2(dy, horiz) * (180.0 / MC_PI)));
        }
        pitch = pai_update_rotation(0.0f, target_pitch, 40.0f);
        head = pai_update_rotation(head, target_yaw, 10.0f);
    } else {
        head = pai_update_rotation(head, body, 10.0f);
    }
    if (s->path_len[i]) {
        float rel = pai_wrap_degrees(head - body);
        if (rel < -75.0f) head = body - 75.0f;
        if (rel > 75.0f) head = body + 75.0f;
    }
    m->passive_head_yaw[i] = head;
    m->passive_head_pitch[i] = pitch;
}

/* EntityLiving.updateDistance -> EntityBodyHelper.updateRenderAngles. */
static float pai_angle_bound(float a1, float a2, float maxd) {
    float f = pai_wrap_degrees(a1 - a2);
    if (f < -maxd) f = -maxd;
    if (f >= maxd) f = maxd;
    return a1 - f;
}

static void pai_body_update(GmMobLive *m, const EwStore *s, const EwStore *prev, int i) {
    double d0 = s->x[i] - prev->x[i];
    double d1 = s->z[i] - prev->z[i];
    if (d0 * d0 + d1 * d1 > 2.500000277905201e-7) {
        m->passive_render_yaw[i] = s->yaw[i];
        m->passive_head_yaw[i] = pai_angle_bound(m->passive_render_yaw[i],
                                                 m->passive_head_yaw[i], 75.0f);
        m->passive_prev_head_yaw[i] = m->passive_head_yaw[i];
        m->passive_body_ticks[i] = 0;
    } else {
        float f = 75.0f;
        float head = m->passive_head_yaw[i];
        if (fabsf(head - m->passive_prev_head_yaw[i]) > 15.0f) {
            m->passive_body_ticks[i] = 0;
            m->passive_prev_head_yaw[i] = head;
        } else {
            ++m->passive_body_ticks[i];
            if (m->passive_body_ticks[i] > 10)
                f = fmaxf(1.0f - (float)(m->passive_body_ticks[i] - 10) / 10.0f, 0.0f) * 75.0f;
        }
        m->passive_render_yaw[i] = pai_angle_bound(head, m->passive_render_yaw[i], f);
    }
}

static void pai_apply_current_look(GmMobLive *m, const EwStore *s, int i,
                                   double px, double py, double pz) {
    if (m->passive_tasks[i] & PAI_BIT(PAI_WATCH)) {
        pai_look_update(m, s, i, 1, px, py + PSV_EYE_HEIGHT, pz);
    } else if (m->passive_tasks[i] & PAI_BIT(PAI_IDLE)) {
        pai_look_update(m, s, i, 1,
                        s->x[i] + m->passive_idle_x[i],
                        s->y[i] + pai_eye_height(s->type[i]),
                        s->z[i] + m->passive_idle_z[i]);
    } else {
        pai_look_update(m, s, i, 0, 0.0, 0.0, 0.0);
    }
}

/* Returns movement/jump intents after an EntityAITasks.onUpdateTasks pass. */
static void pai_tick(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                     double px, double py, double pz, int mob_griefing,
                     int *moving, int *jump, int *wandering, int *swim_jump,
                     double *nav_speed) {
    int type = s->type[i];
    int setup = (m->passive_task_tick[i]++ % 3) == 0;
    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (pai_priority(type, task) >= 99) continue;
        int using_task = (m->passive_tasks[i] & PAI_BIT(task)) != 0;
        if (setup) {
            if (using_task) {
                if (!pai_can_use(m, type, i, task) ||
                    !pai_continue(m, w, s, i, task, px, py, pz))
                    pai_reset(m, s, i, task);
            } else if (pai_can_use(m, type, i, task)) {
                (void)pai_try_start(m, w, s, i, task, px, py, pz);
            }
        } else if (using_task && !pai_continue(m, w, s, i, task, px, py, pz)) {
            pai_reset(m, s, i, task);
        }
    }

    for (int task = 0; task < PAI_NTASKS; ++task) {
        if (!(m->passive_tasks[i] & PAI_BIT(task))) continue;
        if (task == PAI_SWIM) {
            u64 stream = pai_rng_start(m, s, i, PAI_SWIM + 16);
            if (pai_float(m, i, &stream) < 0.8f) *swim_jump = 1;
        } else if (task == PAI_EAT) {
            if (m->passive_eat_time[i] > 0) --m->passive_eat_time[i];
            if (m->passive_eat_time[i] == 4) {
                int bx = mc_floor(s->x[i]), by = mc_floor(s->y[i]), bz = mc_floor(s->z[i]);
                if (gm_world_block(w, bx, by, bz) == 31 &&
                    gm_world_meta(w, bx, by, bz) == 1) {
                    if (mob_griefing) gm_world_set_block(w, bx, by, bz, 0);
                    m->passive_sheared[i] = 0; /* EntitySheep.eatGrassBonus */
                } else if (gm_world_block(w, bx, by - 1, bz) == 2) {
                    if (mob_griefing) gm_world_set_block(w, bx, by - 1, bz, 3);
                    m->passive_sheared[i] = 0;
                }
            }
        } else if (task == PAI_WATCH) {
            --m->passive_watch_time[i];
        } else if (task == PAI_IDLE) {
            --m->passive_idle_time[i];
        }
    }

    /* EntityLiving.updateEntityActionState: goalSelector then navigator. */
    if (pai_det() && m->det_nav_n[i])
        pai_nav_follow(m, w, s, i);
    else if (s->path_len[i] && pai_path_done(w, s, i)) s->path_len[i] = 0;

    *moving = s->path_len[i] != 0;
    *wandering = *moving && (m->passive_tasks[i] & PAI_BIT(PAI_WANDER));
    *jump = 0;
    *nav_speed = *moving ? m->passive_nav_speed[i] : 0.0;
    s->ai_state[i] = EW_AI_IDLE;
    if (m->panic_ticks[i] > 0) --m->panic_ticks[i];
}

static int sky_exposed(GmWorld *w, double x, double y, double z) {
    int bx = mc_floor(x), bz = mc_floor(z);
    int by = mc_floor(y + 1.8);
    int feet = gm_world_block(w, bx, mc_floor(y), bz);
    if (feet && (mc_bpt_props(feet).flags & BF_LIQUID)) return 0;
    for (int yy = by; yy < 256; ++yy)
        if (solid_id(gm_world_block(w, bx, yy, bz))) return 0;
    return 1;
}

void gm_mobs_init(GmMobLive *m, long long seed) {
    memset(m, 0, sizeof *m);
    ew_store_clear(&m->a); ew_store_clear(&m->b);
    m->seed = seed; m->next_id = 1; m->next_orb_id=1000;
    m->active_dimension = 0;
    m->boat_ride = -1;
}

static int xp_split(int value){
    static const int split[]={2477,1237,617,307,149,73,37,17,7,3,1};
    for(unsigned i=0;i<sizeof split/sizeof split[0];++i)if(value>=split[i])return split[i];
    return 1;
}

void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value){
    if(!m||value<=0)return;
    while(value>0){
        int slot=-1;for(int i=0;i<GM_XP_ORBS;++i)if(m->xp_orbs[i].dead||m->xp_orbs[i].xpValue<=0){slot=i;break;}
        if(slot<0)return;
        int amount=xp_split(value);value-=amount;
        McOrb *o=&m->xp_orbs[slot];memset(o,0,sizeof *o);
        o->xpValue=amount;o->eid=m->next_orb_id++;o->motionY=0.2;
        u64 h=mc_hash64((u64)m->seed^(u64)o->eid);
        double angle=(double)(h&0xffffu)*(2.0*MC_PI/65536.0);
        double speed=(double)((h>>16)&0xffffu)*(0.2/65535.0);
        o->motionX=-sin(angle)*speed;o->motionZ=cos(angle)*speed;
        m->orb_dimension[slot]=(signed char)m->active_dimension;
        eo_set_position(o,x,y,z);
    }
}

int gm_mobs_spawn_sized(GmMobLive *m, int type, double x, double y, double z, int size) {
    if (!m || !gm_living(type)) return -1;
    EwStore *s = now_store(m);
    float hp = max_health(type, size);
    int slot = ew_store_spawn(s, (u8)type, m->next_id++, x, y, z, hp);
    if (slot >= 0) {
        m->entity_dimension[slot]=(signed char)m->active_dimension;
        m->size[slot] = (unsigned char)(size > 0 ? size : (gm_is_slimey(type) ? 2 : 1));
        s->health[slot] = max_health(type, m->size[slot]);
        reset_slot_state_s(m, s, slot);
        ew_store_copy(next_store(m), s);
    }
    return slot;
}

int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z) {
    int sz = 1;
    if (type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) sz = 2;
    return gm_mobs_spawn_sized(m, type, x, y, z, sz);
}

int gm_mobs_det_place(GmMobLive *m, int eid, int type,
                      double x, double y, double z, float yaw, float pitch, float head_yaw,
                      unsigned long long seed48, int living_sound, int entity_age, int task_tick,
                      unsigned tasks, int watch, int idle, double idle_x, double idle_z,
                      int eat, int egg, int on_ground, float render_yaw, float prev_head_yaw,
                      int body_ticks) {
    int slot;
    EwStore *s;
    if (!m || !gm_passive(type)) return -1;
    slot = gm_mobs_spawn(m, type, x, y, z);
    if (slot < 0) return -1;
    s = now_store(m);
    s->id[slot] = eid;
    s->x[slot] = x; s->y[slot] = y; s->z[slot] = z;
    s->yaw[slot] = yaw;
    s->on_ground[slot] = on_ground ? 1 : 0;
    s->path_len[slot] = 0;
    m->ent_jr_seed[slot] = seed48 & MC_JR_MASK;
    m->living_sound_time[slot] = living_sound;
    m->entity_age[slot] = entity_age;
    m->passive_task_tick[slot] = task_tick;
    m->passive_tasks[slot] = tasks;
    m->passive_watch_time[slot] = watch;
    m->passive_idle_time[slot] = idle;
    m->passive_idle_x[slot] = idle_x;
    m->passive_idle_z[slot] = idle_z;
    m->passive_eat_time[slot] = eat;
    m->chicken_egg[slot] = egg;
    m->passive_head_yaw[slot] = head_yaw;
    m->passive_head_pitch[slot] = pitch;
    /* EntityBodyHelper.prevRenderYawHead + rotationTickCounter. Old tapes
     * without those fields pass head_yaw / 0 and keep the 15-degree phase
     * error on the first long idle. */
    m->passive_render_yaw[slot] = render_yaw;
    m->passive_prev_head_yaw[slot] = prev_head_yaw;
    m->passive_body_ticks[slot] = body_ticks;
    if (eid >= m->next_id) m->next_id = eid + 1;
    ew_store_copy(next_store(m), s);
    return slot;
}

int gm_mobs_place_boat(GmMobLive *m, double x, double y, double z, float yaw) {
    int slot = gm_mobs_spawn(m, EW_TYPE_BOAT, x, y, z);
    if (slot < 0) return -1;
    now_store(m)->yaw[slot] = yaw;
    next_store(m)->yaw[slot] = yaw;
    if (slot >= 0 && slot < EW_MAX_ENTITIES) {
        s_boat_delta_rot[slot] = 0.0f;
        s_boat_glide[slot] = 0.8f;
    }
    return slot;
}

int gm_mobs_boat_riding(const GmMobLive *m) {
    return m && m->boat_ride >= 0;
}

int gm_mobs_boat_mount(GmMobLive *m, struct PsvPlayer *player_, int ox, int oz) {
    if (!m || !player_) return 0;
    PsvPlayer *p = (PsvPlayer *)player_;
    if (m->boat_ride >= 0) return 1;
    EwStore *s = now_store(m);
    double px = p->ent.posX + ox, py = p->ent.posY, pz = p->ent.posZ + oz;
    int best = -1; double bd = 2.5 * 2.5;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (!s->alive[i] || s->type[i] != EW_TYPE_BOAT) continue;
        if (m->entity_dimension[i] != m->active_dimension) continue;
        double dx = s->x[i] - px, dy = s->y[i] - py, dz = s->z[i] - pz;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return 0;
    m->boat_ride = best;
    p->ent.posX = s->x[best] - ox;
    p->ent.posY = s->y[best] + 0.3;
    p->ent.posZ = s->z[best] - oz;
    p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
    return 1;
}

void gm_mobs_boat_dismount(GmMobLive *m, struct PsvPlayer *player_, int ox, int oz) {
    if (!m || m->boat_ride < 0) return;
    PsvPlayer *p = (PsvPlayer *)player_;
    EwStore *s = now_store(m);
    int i = m->boat_ride;
    if (p && s->alive[i] && s->type[i] == EW_TYPE_BOAT) {
        double yaw = s->yaw[i] * MC_PI / 180.0;
        p->ent.posX = s->x[i] - ox - sin(yaw) * 1.0;
        p->ent.posY = s->y[i] + 0.5;
        p->ent.posZ = s->z[i] - oz + cos(yaw) * 1.0;
    }
    m->boat_ride = -1;
}

static float held_damage(const PsvPlayer *p) {
    int id = isr_get_stack(&p->inv, p->inv.current_item).item;
    if (id == 268) return mc_combat_weapon_raw(1);
    if (id == 272) return mc_combat_weapon_raw(2);
    if (id == 267) return mc_combat_weapon_raw(3);
    if (id == 276) return mc_combat_weapon_raw(4);
    /* EntityPlayer.applyEntityAttributes ATTACK_DAMAGE=1.0. SharedMonster
     * default 2.0 is the knob-off live-sim fist (test_mob_live). */
    if (pai_det()) return 1.0f;
    return mc_combat_weapon_raw(0);
}

static void damage_held_weapon(PsvPlayer *p){
    int slot=p->inv.current_item;ICStack held=isr_get_stack(&p->inv,slot);
    ITAStack tool=ita_mk(held.item,held.meta);ita_hit_entity(&tool);
    int max=ita_stack_max_damage(&tool);if(max<=0)return;
    if(tool.damage>max)(void)isr_decr_stack_size(&p->inv,slot,1);
    else{held.meta=tool.damage;isr_set_stack(&p->inv,slot,held);}
}

static void slime_split(GmMobLive *m, EwStore *s, int i) {
    int sz = m->size[i];
    if (sz <= 1) return;
    int child = sz / 2;
    for (int k = 0; k < 2; ++k) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, i, k, s->id[i], 0x53504C54u);
        double ox = ((double)mc_hash_bound(h, 1000) / 1000.0 - 0.5) * (double)sz * 0.5;
        double oz = ((double)mc_hash_bound(mc_hash64(h), 1000) / 1000.0 - 0.5) * (double)sz * 0.5;
        int slot = ew_store_spawn(s, s->type[i], m->next_id++,
                                  s->x[i] + ox, s->y[i] + 0.5, s->z[i] + oz,
                                  max_health(s->type[i], child));
        if (slot < 0) break;
        m->entity_dimension[slot]=m->entity_dimension[i];
        m->size[slot] = (unsigned char)child;
        reset_slot_state_s(m, s, slot);
    }
}

static void mob_drop(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    int item = 0, count = 1, xp = 5;
    int type = s->type[i];
    switch (type) {
    case EW_TYPE_ZOMBIE: item = 367; break;
    case EW_TYPE_SKELETON: item = 352; break;
    case EW_TYPE_WITHER_SKELETON:
        item = 352;
        /* wither skeleton skull rare drop skipped; coal possible */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x57495448ULL) & 3ULL) == 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 263, 1, 0, 10);
        break;
    case EW_TYPE_CREEPER: item = 289; break;
    case EW_TYPE_SPIDER: item = 287; break;
    case EW_TYPE_ENDERMAN:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 368;
        break;
    case EW_TYPE_BLAZE:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 369;
        xp = 10; break;
    case EW_TYPE_PIGMAN:
        item = 367; /* rotten flesh */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x474F4C44ULL) & 3ULL) == 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 266, 1, 0, 10);
        break;
    case EW_TYPE_GHAST:
        item = 289; /* gunpowder */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i] ^ 0x54454152ULL) & 1ULL) != 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 370, 1, 0, 10);
        break;
    case EW_TYPE_MAGMA:
        if (m->size[i] > 1 &&
            (mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 378;
        xp = m->size[i];
        slime_split(m, s, i);
        break;
    case EW_TYPE_SLIME:
        if (m->size[i] == 1) item = 341; /* slime ball */
        xp = m->size[i];
        slime_split(m, s, i);
        break;
    case EW_TYPE_SILVERFISH:
        item = 0; xp = 5; break;
    case EW_TYPE_SHEEP:
        item = 35; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 423, 1, 0, 10); break;
    case EW_TYPE_PIG: item = 319; xp = 1; break;
    case EW_TYPE_COW:
        item = 363; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 334, 1, 0, 10); break;
    case EW_TYPE_CHICKEN:
        item = 365; xp = 1;
        gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], 288, 1, 0, 10); break;
    case EW_TYPE_BOAT:
        item = 333; xp = 0; break;
    default: break;
    }
    if (item) gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], item, count, 0, 10);
    if (xp > 0) gm_mobs_spawn_xp(m, s->x[i], s->y[i] + 0.25, s->z[i], xp);
    if (m->boat_ride == i) m->boat_ride = -1;
    s->alive[i] = 0;
    s->type[i] = EW_TYPE_NONE;
}

int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player_,
                          int ox, int oz, GmLiveSim *drops) {
    if (!m || !player_) return 0;
    const PsvPlayer *p = (const PsvPlayer *)player_;
    EwStore *s = now_store(m);
    double px = p->ent.posX + ox, py = p->ent.posY + PSV_EYE_HEIGHT, pz = p->ent.posZ + oz;
    double yr = p->yaw * MC_PI / 180.0, pr = p->pitch * MC_PI / 180.0;
    double dx = -sin(yr) * cos(pr), dy = -sin(pr), dz = cos(yr) * cos(pr);
    int best = -1; double best_t = 3.0;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) if (s->alive[i] && gm_living(s->type[i])) {
        if (m->entity_dimension[i] != m->active_dimension) continue;
        float width, height;
        ehs_size_scaled(s->type[i], m->size[i], &width, &height);
        double cx = s->x[i], cy = s->y[i] + height * 0.5, cz = s->z[i];
        double vx = cx-px, vy = cy-py, vz = cz-pz;
        double t = vx*dx + vy*dy + vz*dz;
        if (t < 0.0 || t > best_t) continue;
        double ex=vx-t*dx, ey=vy-t*dy, ez=vz-t*dz;
        double radius = width * 0.5 + 0.25;
        double vtol = height * 0.5 + 0.25;
        if (s->type[i] == EW_TYPE_BOAT) vtol = 2.0; /* low hull, aim from standing */
        if (ex*ex + ez*ez <= radius*radius && fabs(ey) <= vtol) {
            best=i; best_t=t;
        }
    }
    if (best < 0) return 0;
    if (m->player_attack_cooldown > 0) return 1;
    if (s->type[best] == EW_TYPE_BOAT) {
        m->boat_damage[best] += 10;
        if (m->boat_damage[best] >= 40) mob_drop(m, s, best, drops);
        m->player_attack_cooldown = 10;
        ew_store_copy(next_store(m), s);
        return 1;
    }
    s->health[best] -= held_damage(p);
    mark_hurt(m, s, best);
    /* EntityLivingBase.attackEntityFrom flag1: setBeenAttacked, knockBack,
     * then playHurtSound->getSoundPitch (2 nextFloat). Knob-off keeps the
     * old no-velocity path so test_mob_live stays byte-identical. */
    if (pai_det() && gm_passive(s->type[best])) {
        double xRatio = (p->ent.posX + ox) - s->x[best];
        double zRatio = (p->ent.posZ + oz) - s->z[best];
        JavaRandom *jr = pai_jr(m, best);
        (void)jrand_double(jr); /* EntityLivingBase.setBeenAttacked */
        if (jrand_double(jr) >= 0.0 &&
            xRatio * xRatio + zRatio * zRatio >= 1.0e-4) {
            float f = (float)sqrt(xRatio * xRatio + zRatio * zRatio);
            /* attackEntityFrom passes 0.4F; (double)0.4F is 0.4000000059604645. */
            const double strength = (double)0.4f;
            s->vx[best] *= 0.5;
            s->vz[best] *= 0.5;
            s->vx[best] -= xRatio / (double)f * strength;
            s->vz[best] -= zRatio / (double)f * strength;
            if (s->on_ground[best]) {
                s->vy[best] *= 0.5;
                s->vy[best] += strength;
                if (s->vy[best] > 0.4000000059604645)
                    s->vy[best] = 0.4000000059604645;
            }
        }
        (void)jrand_float(jr);
        (void)jrand_float(jr);
    }
    damage_held_weapon((PsvPlayer *)p);
    m->player_attack_cooldown = 10;
    if (s->health[best] <= 0.0f) mob_drop(m, s, best, drops);
    ew_store_copy(next_store(m), s);
    return 1;
}

static void move_mob(GmWorld *w, const McSinTable *st, GmMobLive *m, EwStore *s,
                     int i, int moving, int jump, int swim_jump, double nav_speed) {
    EhsIntent intent;
    EbLiving liv;
    PcfBlock blocks[GM_MOB_BLOCKS];
    ehs_intent_from_ai(s->type[i], s->ai_state[i], moving, s->x[i], s->z[i],
                       s->path_tx[i], s->path_tz[i], s->path_tx[i], s->path_tz[i], &intent);
    if (!moving) intent.yaw = s->yaw[i];
    if (gm_passive(s->type[i]) && moving) {
        if (pai_det()) {
            double ddx = s->path_tx[i] - s->x[i];
            double ddz = s->path_tz[i] - s->z[i];
            intent.yaw = pai_limit_angle(s->yaw[i], pai_atan2_yaw(ddz, ddx), 90.0f);
        } else {
            intent.yaw = pai_update_rotation(s->yaw[i], intent.yaw, 90.0f);
        }
    }
    if (moving && jump) intent.isJumping = 1;
    ehs_load_living(&liv, s, i, &intent);
    /* Override sizes not represented exactly by the shared hostile spine. */
    if (gm_is_slimey(s->type[i]) || gm_passive(s->type[i])) {
        float w, h;
        if (gm_passive(s->type[i])) pai_size(s->type[i], &w, &h);
        else ehs_size_scaled(s->type[i], m->size[i], &w, &h);
        liv.base.width = w; liv.base.height = h;
        liv.base.phys.box = mc_aabb_make(s->x[i] - w * 0.5, s->y[i], s->z[i] - w * 0.5,
                                         s->x[i] + w * 0.5, s->y[i] + h, s->z[i] + w * 0.5);
    }
    if (gm_passive(s->type[i])) {
        /* EntityMoveHelper MOVE_TO:
         *   setAIMoveSpeed((float)(navigatorSpeed * MOVEMENT_SPEED attr));
         * EntityLiving.setAIMoveSpeed writes that same value to moveForward.
         * EntityLivingBase then damps moveForward by 0.98 before travel. */
        float ai_speed = (float)(nav_speed * pai_attribute_speed(s->type[i]));
        liv.landMovementFactor = ai_speed;
        liv.moveForward = moving ? ai_speed : 0.0f;
        liv.moveStrafing = 0.0f;
        if (moving) {
            double dx = s->path_tx[i] - s->x[i];
            double dy = s->path_ty[i] - s->y[i];
            double dz = s->path_tz[i] - s->z[i];
            if (dy > liv.base.phys.stepHeight && dx * dx + dz * dz < fmax(1.0, liv.base.width))
                liv.isJumping = 1;
        }
    }
    /* Ghast: no gravity; fly toward path. */
    if (s->type[i] == EW_TYPE_GHAST) {
        double dx = s->path_tx[i] - s->x[i];
        double dy = s->path_ty[i] - s->y[i];
        double dz = s->path_tz[i] - s->z[i];
        double len = sqrt(dx * dx + dy * dy + dz * dz);
        if (moving && len > 0.01) {
            double sp = 0.1;
            s->vx[i] = dx / len * sp;
            s->vy[i] = dy / len * sp;
            s->vz[i] = dz / len * sp;
        } else {
            s->vx[i] *= 0.9; s->vy[i] *= 0.9; s->vz[i] *= 0.9;
        }
        s->x[i] += s->vx[i]; s->y[i] += s->vy[i]; s->z[i] += s->vz[i];
        s->yaw[i] = intent.yaw;
        s->on_ground[i] = 0;
        return;
    }
    /* EntityBlaze.onLivingUpdate: slow fall (motionY *= 0.6 when falling).
     * Full heightOffset hover from updateAITasks is not ported (needs its own
     * randomized heightOffset timer); fall damping alone is the small half. */
    if (s->type[i] == EW_TYPE_BLAZE && !liv.base.phys.onGround &&
        liv.base.phys.motionY < 0.0)
        liv.base.phys.motionY *= 0.6;

    /* EntityAISwimming only requests a jump; the actual water/lava travel is
     * EntityLivingBase's fluid branch. Keep it here so a passive does not run
     * the land gravity branch while submerged. */
    if (gm_passive(s->type[i]) &&
        (pai_in_material(w, s, i, 0) || pai_in_material(w, s, i, 1))) {
        int in_water = pai_in_material(w, s, i, 0);
        eb_on_entity_update(&liv.base);
        if (fabs(liv.base.phys.motionX) < 0.003) liv.base.phys.motionX = 0.0;
        if (fabs(liv.base.phys.motionY) < 0.003) liv.base.phys.motionY = 0.0;
        if (fabs(liv.base.phys.motionZ) < 0.003) liv.base.phys.motionZ = 0.0;
        if (swim_jump) liv.base.phys.motionY += 0.03999999910593033;
        liv.moveStrafing *= 0.98f;
        liv.moveForward *= 0.98f;
        eb_move_relative(&liv.base, liv.moveStrafing, liv.moveForward, 0.02f, st);
        McAABB fq = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                     liv.base.phys.motionY, liv.base.phys.motionZ);
        fq.minY -= liv.base.phys.stepHeight; fq.maxY += liv.base.phys.stepHeight;
        int fn = collect_blocks(w, &fq, blocks, GM_MOB_BLOCKS);
        eb_move(&liv.base, liv.base.phys.motionX, liv.base.phys.motionY,
                liv.base.phys.motionZ, blocks, fn);
        double drag = in_water ? 0.800000011920929 : 0.5;
        liv.base.phys.motionX *= drag;
        liv.base.phys.motionY *= drag;
        liv.base.phys.motionZ *= drag;
        if (!liv.base.hasNoGravity) liv.base.phys.motionY -= 0.02;
        ehs_store_living(s, i, &liv);
        return;
    }
    float slip = 0.6f;
    if (liv.base.phys.onGround) {
        int id = gm_world_block(w, mc_floor(liv.base.phys.posX),
                                mc_floor(liv.base.phys.box.minY)-1,
                                mc_floor(liv.base.phys.posZ));
        if (id == 79 || id == 174 || id == 212) slip = 0.98f;
        if (id == 8 || id == 9) slip = 0.8f; /* water friction for boat handled below */
    }
    /* Angry pigman speed boost +0.05 (AttributeModifier). */
    if (s->type[i] == EW_TYPE_PIGMAN && m->anger[i] > 0)
        liv.landMovementFactor += 0.05f;
    McAABB q = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                liv.base.phys.motionY, liv.base.phys.motionZ);
    q.minY -= liv.base.phys.stepHeight; q.maxY += liv.base.phys.stepHeight;
    int n = collect_blocks(w, &q, blocks, GM_MOB_BLOCKS);
    eb_tick_living(&liv, slip, 0, blocks, n, st);
    ehs_store_living(s, i, &liv);
}

static int alive_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for (int i=1;i<EW_MAX_ENTITIES;++i)
        if (s->alive[i] && m->entity_dimension[i]==m->active_dimension &&
            gm_hostile(s->type[i])) ++n;
    return n;
}
static int living_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&
           gm_living(s->type[i]))++n;
    return n;
}
static int passive_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&
           gm_passive(s->type[i]))++n;
    return n;
}

int gm_mobs_living_count(const GmMobLive *m){
    return m?living_count(m,const_store(m)):0;
}

static int collect_orb_blocks(GmWorld *w,const McAABB *q,McAABB *out,int cap){
    int n=0,x0=mc_floor(q->minX)-1,x1=mc_floor(q->maxX)+1;
    int y0=mc_floor(q->minY)-1,y1=mc_floor(q->maxY)+1;
    int z0=mc_floor(q->minZ)-1,z1=mc_floor(q->maxZ)+1;
    if(y0<0)y0=0;
    if(y1>255)y1=255;
    for(int x=x0;x<=x1;++x)for(int y=y0;y<=y1;++y)for(int z=z0;z<=z1;++z){
        if(!solid_id(gm_world_block(w,x,y,z)))continue;
        if(n==cap)return n;
        out[n++]=mc_aabb_make(x,y,z,x+1,y+1,z+1);
    }return n;
}

static void tick_xp_orbs(GmMobLive *m,GmWorld *w,PsvPlayer *p,int ox,int oz){
    McAABB player=p->ent.box;player.minX+=ox;player.maxX+=ox;player.minZ+=oz;player.maxZ+=oz;
    for(int i=0;i<GM_XP_ORBS;++i){McOrb *o=&m->xp_orbs[i];
        if(o->dead||o->xpValue<=0||m->orb_dimension[i]!=m->active_dimension)continue;
        McAABB q=mc_aabb_addcoord(&o->box,o->motionX,o->motionY,o->motionZ),blocks[64];
        int nb=collect_orb_blocks(w,&q,blocks,64);
        int ux=mc_floor(o->posX),uy=mc_floor(o->box.minY)-1,uz=mc_floor(o->posZ);
        if(uy<0)uy=0;
        u16 under=mc_state(gm_world_block(w,ux,uy,uz),gm_world_meta(w,ux,uy,uz));
        eo_tick(o,p->ent.posX+ox,p->ent.posY,p->ent.posZ+oz,PSV_EYE_HEIGHT,0,
                blocks,nb,under,0);
        if(!o->dead&&o->delayBeforeCanPickup<=0&&mc_aabb_intersects(&o->box,&player)){
            m->xp_total+=o->xpValue;o->dead=1;
        }
    }
}

int gm_mobs_register_spawner(GmMobLive *m, int x, int y, int z, int entity_type) {
    if (!m || !entity_type) return -1;
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        if (m->spawners[i].active && m->spawners[i].x == x &&
            m->spawners[i].y == y && m->spawners[i].z == z &&
            m->spawners[i].dimension == m->active_dimension) {
            m->spawners[i].entity_type = entity_type;
            return i;
        }
    }
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        if (m->spawners[i].active) continue;
        m->spawners[i].active = 1;
        m->spawners[i].dimension = m->active_dimension;
        m->spawners[i].x = x; m->spawners[i].y = y; m->spawners[i].z = z;
        m->spawners[i].entity_type = entity_type;
        m->spawners[i].delay = 20;
        m->spawners[i].min_delay = 200;
        m->spawners[i].max_delay = 800;
        m->spawners[i].spawn_count = 4;
        m->spawners[i].max_nearby = 6;
        m->spawners[i].spawn_range = 4;
        m->spawners[i].activate_range = 16;
        return i;
    }
    return -1;
}

static int count_type_near(const GmMobLive *m,const EwStore *s, int type,
                           double x, double y, double z, double r) {
    int n = 0; double r2 = r * r;
    for (int i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (!s->alive[i] || s->type[i] != type) continue;
        if (m->entity_dimension[i] != m->active_dimension) continue;
        double dx = s->x[i] - x, dy = s->y[i] - y, dz = s->z[i] - z;
        if (dx * dx + dy * dy + dz * dz <= r2) ++n;
    }
    return n;
}

/* Discover block-52 spawners and stamp entity id from dimension/structure. */
static void discover_spawners(GmMobLive *m, GmWorld *w, double px, double py, double pz,
                              int dimension) {
    int pcx = mc_floor(px), pcy = mc_floor(py), pcz = mc_floor(pz);
    for (int x = pcx - 24; x <= pcx + 24; ++x)
        for (int y = pcy - 12; y <= pcy + 12; ++y)
            for (int z = pcz - 24; z <= pcz + 24; ++z) {
                if (y < 0 || y > 255) continue;
                if (gm_world_block(w, x, y, z) != 52) continue;
                int known = 0;
                for (int i = 0; i < GM_SPAWNERS; ++i)
                    if (m->spawners[i].active && m->spawners[i].x == x &&
                        m->spawners[i].y == y && m->spawners[i].z == z &&
                        m->spawners[i].dimension == dimension) {
                        known = 1; break;
                    }
                if (known) continue;
                int et = 0;
                if (dimension == -1) {
                    /* Fortress spawners are blaze; detect nearby nether brick. */
                    int bricks = 0;
                    for (int dx = -4; dx <= 4 && !bricks; ++dx)
                        for (int dy = -2; dy <= 2 && !bricks; ++dy)
                            for (int dz = -4; dz <= 4; ++dz)
                                if (gm_world_block(w, x + dx, y + dy, z + dz) == 112) {
                                    bricks = 1; break;
                                }
                    et = bricks ? EW_TYPE_BLAZE : 0;
                } else if (dimension == 0) {
                    /* Stronghold portal-room silverfish spawner. */
                    int frames = 0;
                    for (int dx = -8; dx <= 8; ++dx)
                        for (int dy = -4; dy <= 4; ++dy)
                            for (int dz = -8; dz <= 8; ++dz)
                                if (gm_world_block(w, x + dx, y + dy, z + dz) == 120) ++frames;
                    et = frames >= 4 ? EW_TYPE_SILVERFISH : EW_TYPE_ZOMBIE;
                }
                if (et) gm_mobs_register_spawner(m, x, y, z, et);
            }
}

static void tick_spawners(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz) {
    for (int i = 0; i < GM_SPAWNERS; ++i) {
        GmSpawnerTE *sp = &m->spawners[i];
        if (!sp->active || sp->dimension != m->active_dimension) continue;
        if (gm_world_block(w, sp->x, sp->y, sp->z) != 52) {
            sp->active = 0; continue;
        }
        double dx = px - (sp->x + 0.5), dy = py - (sp->y + 0.5), dz = pz - (sp->z + 0.5);
        double r = (double)sp->activate_range;
        if (dx * dx + dy * dy + dz * dz >= r * r) continue; /* strict < range */
        if (sp->delay > 0) { --sp->delay; continue; }
        int nearby = count_type_near(m,s, sp->entity_type, sp->x + 0.5, sp->y + 0.5,
                                     sp->z + 0.5, 16.0);
        if (nearby >= sp->max_nearby) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x52455345u);
            int span = sp->max_delay - sp->min_delay;
            sp->delay = sp->min_delay + (span > 0 ? (int)mc_hash_bound(h, span) : 0);
            continue;
        }
        int spawned = 0;
        for (int k = 0; k < sp->spawn_count && living_count(m,s) < GM_MOB_CAPACITY; ++k) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x53504157u + k);
            double r1 = (double)mc_hash_f01(h);
            double r2 = (double)mc_hash_f01(mc_hash64(h));
            double sx = sp->x + (r1 - r2) * sp->spawn_range + 0.5;
            int yoff = (int)mc_hash_bound(mc_hash64(h + 2), 3) - 1;
            double sy = sp->y + yoff;
            double r3 = (double)mc_hash_f01(mc_hash64(h + 3));
            double r4 = (double)mc_hash_f01(mc_hash64(h + 4));
            double sz = sp->z + (r3 - r4) * sp->spawn_range + 0.5;
            int bx = mc_floor(sx), by = mc_floor(sy), bz = mc_floor(sz);
            if (solid_id(gm_world_block(w, bx, by, bz)) ||
                solid_id(gm_world_block(w, bx, by + 1, bz))) continue;
            int sz_slime = 2;
            if (sp->entity_type == EW_TYPE_MAGMA || sp->entity_type == EW_TYPE_SLIME)
                sz_slime = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 5), 3)];
            int slot = ew_store_spawn(s, (u8)sp->entity_type, m->next_id++,
                                      sx, sy, sz,
                                      max_health(sp->entity_type, sz_slime));
            if (slot < 0) break;
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz_slime;
            reset_slot_state_s(m, s, slot);
            spawned = 1;
        }
        if (spawned || nearby >= sp->max_nearby) {
            u64 h = mc_hash_seed((u64)m->seed, m->tick, sp->x, sp->y, sp->z, 0x52455345u);
            int span = sp->max_delay - sp->min_delay;
            sp->delay = sp->min_delay + (span > 0 ? (int)mc_hash_bound(h, span) : 0);
        } else {
            sp->delay = 20; /* retry soon if no space/block */
        }
    }
}

static int in_fortress_bricks(GmWorld *w, int x, int y, int z) {
    for (int dx = -8; dx <= 8; ++dx)
        for (int dy = -4; dy <= 4; ++dy)
            for (int dz = -8; dz <= 8; ++dz)
                if (gm_world_block(w, x + dx, y + dy, z + dz) == 112) return 1;
    return 0;
}

static void passive_spawn(GmMobLive *m, GmWorld *w, EwStore *s, double px, double py, double pz) {
    if (m->tick == 0 || (m->tick % 200) ||
        passive_count(m,s) >= GM_NATURAL_PASSIVE_CAP) return;
    for (int a = 0; a < 8; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x50415353u);
        int dx = 12 + mc_hash_bound(h, 9), dz = mc_hash_bound(mc_hash64(h), 17) - 8;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = gm_world_surface_y(w, x, z);
        double vx = x + 0.5 - px, vy = y - py, vz = z + 0.5 - pz;
        if (vx * vx + vy * vy + vz * vz > 24.0 * 24.0 || gm_world_block(w, x, y - 1, z) != 2 ||
            gm_world_block(w, x, y, z) || gm_world_block(w, x, y + 1, z)) continue;
        int types[4] = {EW_TYPE_SHEEP, EW_TYPE_PIG, EW_TYPE_COW, EW_TYPE_CHICKEN};
        int type = types[mc_hash_bound(mc_hash64(h + 1), 4)];
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, 1));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            reset_slot_state_s(m, s, slot);
        }
        return;
    }
}

static void nether_natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                                 double px, double py, double pz) {
    if ((m->tick % 40) || alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    for (int a = 0; a < 6; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x4E455448u);
        int dx = 16 + mc_hash_bound(h, 17), dz = mc_hash_bound(mc_hash64(h), 33) - 16;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = mc_floor(py) + (int)mc_hash_bound(mc_hash64(h + 2), 9) - 4;
        if (y < 5) y = 5;
        if (y > 120) y = 120;
        if (solid_id(gm_world_block(w, x, y, z)) || solid_id(gm_world_block(w, x, y + 1, z)))
            continue;
        if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
        double ddx = x + 0.5 - px, ddy = y - py, ddz = z + 0.5 - pz;
        double ds = ddx * ddx + ddy * ddy + ddz * ddz;
        if (ds < 16.0 * 16.0 || ds > 48.0 * 48.0) continue;

        int type;
        int fortress = in_fortress_bricks(w, x, y, z);
        if (fortress) {
            /* MapGenNetherBridge spawn list weights: blaze 10, pigman 5,
             * wither skeleton 8, skeleton 2, magma 3. Total 28. */
            int roll = (int)mc_hash_bound(mc_hash64(h + 3), 28);
            if (roll < 10) type = EW_TYPE_BLAZE;
            else if (roll < 15) type = EW_TYPE_PIGMAN;
            else if (roll < 23) type = EW_TYPE_WITHER_SKELETON;
            else if (roll < 25) type = EW_TYPE_SKELETON;
            else type = EW_TYPE_MAGMA;
        } else {
            /* BiomeHell: ghast 50, pigman 100, magma 2, enderman 1. Total 153. */
            int roll = (int)mc_hash_bound(mc_hash64(h + 3), 153);
            if (roll < 50) type = EW_TYPE_GHAST;
            else if (roll < 150) type = EW_TYPE_PIGMAN;
            else if (roll < 152) type = EW_TYPE_MAGMA;
            else type = EW_TYPE_ENDERMAN;
        }
        int sz = 2;
        if (type == EW_TYPE_MAGMA)
            sz = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 4), 3)];
        if (type == EW_TYPE_GHAST) y += 4; /* fly above terrain */
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, sz));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz;
            reset_slot_state_s(m, s, slot);
        }
        return;
    }
}

static void slime_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                        double px, double py, double pz) {
    if ((m->tick % 80) || alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    for (int a = 0; a < 4; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x534C494Du);
        int dx = 16 + mc_hash_bound(h, 17), dz = mc_hash_bound(mc_hash64(h), 33) - 16;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        int cx = x >> 4, cz = z >> 4;
        gm_world_ensure(w, cx, cz, 0);
        int biome = gm_world_biome(w, x, z);
        int y;
        int ok = 0;
        if (biome == B_SWAMP) {
            /* Swamp slime: y 50..70, light-gated. */
            y = 50 + (int)mc_hash_bound(mc_hash64(h + 1), 21);
            if (y < 50 || y > 70) continue;
            if (gm_world_block_light(w, x, y, z) > 7) continue;
            if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
            if (solid_id(gm_world_block(w, x, y, z))) continue;
            ok = 1;
        } else if (is_slime_chunk(m->seed, cx, cz)) {
            /* Slime chunk: y < 40, 1/10 of spawn attempts already in vanilla
             * via chunk RNG; we already gated by is_slime_chunk. */
            y = 5 + (int)mc_hash_bound(mc_hash64(h + 1), 35);
            if (y >= 40) continue;
            if (!solid_id(gm_world_block(w, x, y - 1, z))) continue;
            if (solid_id(gm_world_block(w, x, y, z)) || solid_id(gm_world_block(w, x, y + 1, z)))
                continue;
            ok = 1;
        }
        if (!ok) continue;
        int sz = (int[]){1, 2, 4}[mc_hash_bound(mc_hash64(h + 2), 3)];
        int slot = ew_store_spawn(s, EW_TYPE_SLIME, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(EW_TYPE_SLIME, sz));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            m->size[slot] = (unsigned char)sz;
            reset_slot_state_s(m, s, slot);
        }
        return;
    }
}

/* Route-roster weighted pick — NOT Java-exact WorldEntitySpawner pack loop.
 * Approximate biome monster weights for supported types only (zombie 95,
 * skeleton/creeper/spider 100, enderman 10). Full pack enumeration, chunk
 * RNG order, and type-specific EntityAITasks remain open (OPEN_DIVERGENCES). */
static int overworld_hostile_weighted(JavaRandom *r) {
    int roll = jrand_int_bound(r, 405);
    if (roll < 95) return EW_TYPE_ZOMBIE;
    if (roll < 195) return EW_TYPE_SKELETON;
    if (roll < 295) return EW_TYPE_CREEPER;
    if (roll < 395) return EW_TYPE_SPIDER;
    return EW_TYPE_ENDERMAN;
}

static void natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz, int dimension, long long world_time) {
    discover_spawners(m, w, px, py, pz, dimension);
    tick_spawners(m, w, s, px, py, pz);

    if (dimension == -1) {
        nether_natural_spawn(m, w, s, px, py, pz);
        return;
    }
    if (dimension != 0) return;

    /* Simplified natural spawn (not Java WorldEntitySpawner call-order parity):
     * slime pocket, then day creature / night monster attempt. */
    slime_spawn(m, w, s, px, py, pz);

    int tod = (int)(world_time % 24000LL); if (tod < 0) tod += 24000;
    if (tod < 12000) { passive_spawn(m, w, s, px, py, pz); return; }
    if (tod < 13000 || tod > 23000 || (m->tick % 20) ||
        alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    JavaRandom rng;
    jrand_set(&rng, (i64)m->seed ^ ((i64)m->tick * 6364136223846793005LL) ^ 0x4d4f4253LL);
    for (int a = 0; a < 8; ++a) {
        int dx = 24 + jrand_int_bound(&rng, 9);
        int dz = jrand_int_bound(&rng, 17) - 8;
        if (jrand_int_bound(&rng, 2) == 0) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = gm_world_surface_y(w, x, z);
        double ddx = (x + 0.5) - px, ddy = y - py, ddz = (z + 0.5) - pz;
        double ds = ddx * ddx + ddy * ddy + ddz * ddz;
        if (ds < 24.0 * 24.0 || ds > 32.0 * 32.0 || !solid_id(gm_world_block(w, x, y - 1, z)) ||
            gm_world_block(w, x, y, z) != 0 || gm_world_block(w, x, y + 1, z) != 0 ||
            gm_world_block_light(w, x, y, z) > 7) continue;
        int type = overworld_hostile_weighted(&rng);
        int slot = ew_store_spawn(s, (u8)type, m->next_id++, x + 0.5, y, z + 0.5,
                                  max_health(type, 1));
        if (slot >= 0) {
            m->entity_dimension[slot]=(signed char)m->active_dimension;
            s->cx[slot] = x >> 4; s->cz[slot] = z >> 4;
            reset_slot_state_s(m, s, slot);
        }
        return;
    }
}

/* Status: 0 IN_WATER, 1 ON_LAND, 2 IN_AIR (subset of EntityBoat.Status). */
static int boat_status(GmWorld *w, double x, double y, double z) {
    /* Feet sample: water in body cell => IN_WATER; solid below empty feet => ON_LAND. */
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    int feet = gm_world_block(w, bx, by, bz);
    int below = gm_world_block(w, bx, by - 1, bz);
    int head = gm_world_block(w, bx, mc_floor(y + 0.5625), bz);
    if (feet == 8 || feet == 9 || head == 8 || head == 9) return 0; /* IN_WATER */
    if (solid_id(below) && feet == 0) return 1; /* ON_LAND */
    return 2; /* IN_AIR */
}

static void tick_boat(GmMobLive *m, GmWorld *w, EwStore *nx, int i,
                      PsvPlayer *p, int ox, int oz, float forward, float strafe) {
    /* Boat half-extents for AABB collision (1.375 wide, 0.5625 tall). */
    const double half_w = 1.375 * 0.5;
    const double height = 0.5625;
    int status = boat_status(w, nx->x[i], nx->y[i], nx->z[i]);
    float momentum = 0.05f;
    double d1 = -0.03999999910593033; /* gravity */
    double d2 = 0.0;                  /* buoyancy factor */

    if (status == 0) { /* IN_WATER */
        momentum = 0.9f;
        /* Approximate waterLevel - minY: partial submersion buoyancy. */
        {
            int by = mc_floor(nx->y[i]);
            double water_level = (double)by + 1.0;
            d2 = (water_level - nx->y[i]) / height;
            if (d2 < 0.0) d2 = 0.0;
            if (d2 > 1.0) d2 = 1.0;
        }
    } else if (status == 1) { /* ON_LAND */
        if (s_boat_glide[i] <= 0.0f) s_boat_glide[i] = 0.8f; /* default land glide */
        momentum = s_boat_glide[i];
        if (m->boat_ride == i)
            s_boat_glide[i] *= 0.5f; /* player-controlled land glide halves each tick */
    } else { /* IN_AIR */
        momentum = 0.9f;
    }

    /* updateMotion: apply momentum then gravity/buoyancy. */
    nx->vx[i] *= (double)momentum;
    nx->vz[i] *= (double)momentum;
    s_boat_delta_rot[i] *= momentum;
    nx->vy[i] += d1;
    if (d2 > 0.0) {
        nx->vy[i] += d2 * 0.06153846016296973;
        nx->vy[i] *= 0.75;
    }

    /* controlBoat when ridden: no auto-thrust without forward/back input. */
    if (m->boat_ride == i && p) {
        int left = strafe < -0.01f, right = strafe > 0.01f;
        int fwd = forward > 0.01f, back = forward < -0.01f;
        float f = 0.0f;
        if (left) s_boat_delta_rot[i] += -1.0f;
        if (right) s_boat_delta_rot[i] += 1.0f;
        if (left != right && !fwd && !back) f += 0.005f;
        nx->yaw[i] += s_boat_delta_rot[i];
        if (fwd) f += 0.04f;
        if (back) f -= 0.005f;
        {
            double yr = (double)nx->yaw[i] * 0.017453292;
            nx->vx[i] += -sin(yr) * (double)f;
            nx->vz[i] += cos(yr) * (double)f;
        }
        p->yaw = nx->yaw[i];
        p->ent.posX = nx->x[i] - ox;
        p->ent.posY = nx->y[i] + 0.35;
        p->ent.posZ = nx->z[i] - oz;
        p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
        p->ent.onGround = (status == 1);
    }

    /* AABB-style collide: sample corners of the 1.375 x 0.5625 box on XZ and Y. */
    {
        double try_x = nx->x[i] + nx->vx[i];
        double try_z = nx->z[i] + nx->vz[i];
        double try_y = nx->y[i] + nx->vy[i];
        int blocked_xz = 0;
        int mid_y = mc_floor(nx->y[i] + height * 0.5);
        double corners[4][2] = {
            { try_x - half_w, try_z - half_w },
            { try_x + half_w, try_z - half_w },
            { try_x - half_w, try_z + half_w },
            { try_x + half_w, try_z + half_w }
        };
        for (int c = 0; c < 4; ++c) {
            if (solid_id(gm_world_block(w, mc_floor(corners[c][0]), mid_y,
                                        mc_floor(corners[c][1])))) {
                blocked_xz = 1; break;
            }
        }
        if (!blocked_xz) {
            nx->x[i] = try_x;
            nx->z[i] = try_z;
        } else {
            nx->vx[i] = nx->vz[i] = 0.0;
        }
        {
            int foot = mc_floor(try_y);
            int head = mc_floor(try_y + height);
            int bx = mc_floor(nx->x[i]), bz = mc_floor(nx->z[i]);
            if (!solid_id(gm_world_block(w, bx, foot, bz)) &&
                !solid_id(gm_world_block(w, bx, head, bz))) {
                nx->y[i] = try_y;
                nx->on_ground[i] = 0;
            } else if (nx->vy[i] < 0) {
                nx->vy[i] = 0;
                nx->on_ground[i] = 1;
                nx->y[i] = (double)foot + 1.0;
            } else {
                nx->vy[i] = 0;
            }
        }
    }
}

void gm_mobs_tick(GmMobLive *m, GmWorld *w, const struct McSinTable *st_,
                  struct PsvPlayer *player_, struct PvStats *vitals_,
                  int ox, int oz, int dimension, long long world_time, GmLiveSim *drops,
                  float boat_forward, float boat_strafe, int mob_griefing) {
    if (!m || !w || !player_ || !vitals_) return;
    m->active_dimension=dimension;
    PsvPlayer *p=(PsvPlayer *)player_; PvStats *v=(PvStats *)vitals_;
    const McSinTable *st=(const McSinTable *)st_;
    EwStore *now=now_store(m), *nx=next_store(m); ew_store_copy(nx,now);
    if (m->player_wither_ticks > 0) {
        /* DamageSource.WITHER is unblockable. */
        if (m->player_wither_ticks % 40 == 0)
            (void)gm_mobs_attack_player(m, (struct PvStats *)v,
                                        &p->inv, 1.0f, 1);
        --m->player_wither_ticks;
        p->health = v->health;
    }
    if(m->player_attack_cooldown>0)--m->player_attack_cooldown;
    double px=p->ent.posX+ox, py=p->ent.posY, pz=p->ent.posZ+oz;
    if (!pai_det())
        natural_spawn(m,w,nx,px,py,pz,dimension,world_time);
    int tod=(int)(world_time%24000LL); if(tod<0)tod+=24000;
    int day=dimension==0&&tod<12000;
    float boat_fwd = m->boat_ride >= 0 ? boat_forward : 0.0f;
    float boat_str = m->boat_ride >= 0 ? boat_strafe : 0.0f;

    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(now->alive[i]&&m->entity_dimension[i]==dimension&&gm_living(now->type[i])){
        int type=now->type[i];
        if (type == EW_TYPE_BOAT) {
            tick_boat(m, w, nx, i, p, ox, oz, boat_fwd, boat_str);
            continue;
        }
        int hostile=gm_hostile(type), passive=gm_passive(type);
        /* AIFireballAttack owns its attackTime countdown only while its task
         * executes. Other mobs keep the EntityLiving-style global cooldown. */
        if(type!=EW_TYPE_BLAZE&&nx->attack_time[i]>0)--nx->attack_time[i];
        if (type == EW_TYPE_PIGMAN && m->anger[i] > 0) --m->anger[i];
        double dx=px-now->x[i],dy=py-now->y[i],dz=pz-now->z[i];
        double d=sqrt(dx*dx+dy*dy+dz*dz), xz=sqrt(dx*dx+dz*dz);
        if(hostile){
            if(d>GM_MOB_DESPAWN_HARD){nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;}
            if(d>GM_MOB_DESPAWN_SOFT){
                if(++m->despawn_ticks[i]>=GM_MOB_DESPAWN_DELAY){
                    nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;
                }
            }else m->despawn_ticks[i]=0;
        }
        if(day&&(type==EW_TYPE_ZOMBIE||type==EW_TYPE_SKELETON)&&
           m->fire_ticks[i]<=0&&sky_exposed(w,now->x[i],now->y[i],now->z[i]))
            m->fire_ticks[i]=GM_MOB_FIRE_TICKS;
        /* Entity.setOnFireFromLava calls setFire(15). The live damage model is
         * coarser, but preserving the 300-tick burning state is what makes
         * EntityAIPanic's burning trigger and water search reachable. */
        if(passive&&pai_in_material(w,now,i,1)&&m->fire_ticks[i]<300)
            m->fire_ticks[i]=300;
        if(m->fire_ticks[i]>0){
            --m->fire_ticks[i];
            if(m->fire_ticks[i]%20==0){
                nx->health[i]-=1.0f;
                if(nx->health[i]<=0.0f){mob_drop(m,nx,i,drops);continue;}
            }
        }
        int aggro=0;
        if(hostile){
            int wants=1;
            if(type==EW_TYPE_ENDERMAN)wants=m->hurt_aggro[i];
            else if(type==EW_TYPE_SPIDER)wants=!day||m->hurt_aggro[i];
            else if(type==EW_TYPE_PIGMAN)wants=m->anger[i]>0;
            else if(type==EW_TYPE_SLIME)wants=m->size[i]>1;
            if(wants&&d<=follow_range(type)){
                float mw,mh;ehs_size_scaled((u8)type,m->size[i],&mw,&mh);
                if(type==EW_TYPE_GHAST)
                    aggro=1; /* ghast doesn't need tight LOS for targeting */
                else
                    aggro=los_clear(w,now->x[i],now->y[i]+mh*0.85,now->z[i],
                                    px,py+PSV_EYE_HEIGHT,pz);
            }
        }
        int moving=0,jump=0,wandering=0,swim_jump=0;
        double nav_speed=1.0;
        /* AIFireballAttack.resetTask: clear ON_FIRE when no target. */
        if(!aggro&&type==EW_TYPE_BLAZE){
            m->blaze_on_fire[i]=0;
            m->charge[i]=0;
        }

        if(passive && pai_det()){
            /* EntityLiving.onEntityUpdate living-sound, then ++entityAge + despawn.
             * EntityAnimal.getTalkInterval=120; canDespawn=false (no death). */
            {
                int lst = m->living_sound_time[i];
                int sound_draw = jrand_int_bound(pai_jr(m, i), 1000);
                m->living_sound_time[i] = lst + 1;
                if (sound_draw < lst) {
                    m->living_sound_time[i] = -120;
                    (void)jrand_float(pai_jr(m, i));
                    (void)jrand_float(pai_jr(m, i));
                }
            }
            {
                double d3 = dx * dx + dy * dy + dz * dz;
                m->entity_age[i]++;
                if (m->entity_age[i] > 600) {
                    (void)jrand_int_bound(pai_jr(m, i), 800);
                    if (d3 < 1024.0) m->entity_age[i] = 0;
                } else if (d3 < 1024.0) {
                    m->entity_age[i] = 0;
                }
            }
        }
        if(passive){
            pai_tick(m,w,nx,i,px,py,pz,mob_griefing,
                     &moving,&jump,&wandering,&swim_jump,&nav_speed);
        /* Ghast AIFireballAttack: charge then fire large fireball. */
        }else if(aggro&&type==EW_TYPE_GHAST){
            nx->path_tx[i]=px;nx->path_ty[i]=py+8.0;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            nx->ai_state[i]=EW_AI_ATTACK;
            if(d>16.0){moving=1;nx->ai_state[i]=EW_AI_CHASE;}
            ++m->charge[i];
            /* Charge 20 ticks, then reset through a 40-tick cooldown. */
            if(m->charge[i]>=20 && !m->fireball_pending){
                double len=d>0.01?d:1.0;
                m->fireball_pending=5; /* EntityLargeFireball */
                m->fireball_x=now->x[i];m->fireball_y=now->y[i]+1.5;m->fireball_z=now->z[i];
                m->fireball_vx=dx/len*0.5;m->fireball_vy=dy/len*0.5;m->fireball_vz=dz/len*0.5;
                m->charge[i]=-40;
            }
        }else if(aggro&&type==EW_TYPE_BLAZE){
            /* EntityBlaze.AIFireballAttack.updateTask (EntityBlaze.java:252-317).
             * attack_time = AI attackTime; charge[] = attackStep; blaze_on_fire = ON_FIRE. */
            const double blaze_h = 1.8;
            double d0 = d * d; /* getDistanceSqToEntity */
            double attack_radius = follow_range(EW_TYPE_BLAZE); /* 48 */
            --nx->attack_time[i];
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            if(d0 < 4.0){
                /* Melee fallback under dist 2. */
                nx->ai_state[i]=EW_AI_ATTACK;moving=1;
                if(nx->attack_time[i]<=0){
                    (void)gm_mobs_attack_player(m,(struct PvStats *)v,&p->inv,
                                                melee_damage(type,m->size[i]),0);
                    p->health=v->health;
                    nx->attack_time[i]=20;
                }
            }else if(d0 < attack_radius * attack_radius){
                nx->ai_state[i]=EW_AI_ATTACK;
                if(nx->attack_time[i]<=0){
                    int step = m->charge[i] + 1;
                    m->charge[i] = step;
                    if(step == 1){
                        nx->attack_time[i]=60;
                        m->blaze_on_fire[i]=1;
                    }else if(step <= 4){
                        nx->attack_time[i]=6;
                    }else{
                        nx->attack_time[i]=100;
                        m->charge[i]=0;
                        m->blaze_on_fire[i]=0;
                        step = 0;
                    }
                    if(step > 1){
                        /* Aim + spread from AIFireballAttack + EntityFireball ctor. */
                        double d1 = px - now->x[i];
                        double d2 = (py + 0.9) - (now->y[i] + blaze_h * 0.5);
                        double d3 = pz - now->z[i];
                        float f = (float)(sqrt(sqrt(d0)) * 0.5);
                        u64 h = mc_hash_seed((u64)m->seed, m->tick, i,
                                             now->id[i], step, 0x424C415Au);
                        /* Box-Muller pair for AIFireballAttack gaussians on d1/d3. */
                        float u1 = mc_hash_f01(h); if(u1 < 1e-7f) u1 = 1e-7f;
                        float u2 = mc_hash_f01(mc_hash64(h));
                        double g1 = sqrt(-2.0 * (double)logf(u1)) *
                                    cos(2.0 * 3.141592653589793 * (double)u2);
                        u1 = mc_hash_f01(mc_hash64(h + 1)); if(u1 < 1e-7f) u1 = 1e-7f;
                        u2 = mc_hash_f01(mc_hash64(h + 2));
                        double g3 = sqrt(-2.0 * (double)logf(u1)) *
                                    cos(2.0 * 3.141592653589793 * (double)u2);
                        d1 += g1 * (double)f;
                        d3 += g3 * (double)f;
                        /* EntityFireball(shooter,...): + nextGaussian()*0.4 on each axis. */
                        for(int k=0;k<3;++k){
                            u1 = mc_hash_f01(mc_hash64(h + 3 + k)); if(u1 < 1e-7f) u1 = 1e-7f;
                            u2 = mc_hash_f01(mc_hash64(h + 10 + k));
                            double gk = sqrt(-2.0 * (double)logf(u1)) *
                                        cos(2.0 * 3.141592653589793 * (double)u2);
                            if(k==0) d1 += gk * 0.4;
                            else if(k==1) d2 += gk * 0.4;
                            else d3 += gk * 0.4;
                        }
                        double len = sqrt(d1*d1 + d2*d2 + d3*d3);
                        if(len < 1e-6) len = 1.0;
                        /* Magma projectile path uses constant velocity (not accel);
                         * 0.6 matches prior blaze small-fireball live speed. */
                        const double speed = 0.6;
                        m->fireball_pending = 3; /* EntitySmallFireball */
                        m->fireball_x = now->x[i];
                        m->fireball_y = now->y[i] + blaze_h * 0.5 + 0.5;
                        m->fireball_z = now->z[i];
                        m->fireball_vx = d1 / len * speed;
                        m->fireball_vy = d2 / len * speed;
                        m->fireball_vz = d3 / len * speed;
                    }
                }
            }else{
                /* Beyond attack radius: close distance. */
                nx->ai_state[i]=EW_AI_CHASE;moving=1;
            }
        }else if(aggro&&type==EW_TYPE_SKELETON){
            /* EntityAIAttackRangedBow: hold range, strafe away inside 6. */
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(xz<6.0&&xz>0.01){
                double ux=dx/xz, uz=dz/xz;
                nx->path_tx[i]=now->x[i]-ux*4.0;nx->path_ty[i]=now->y[i];
                nx->path_tz[i]=now->z[i]-uz*4.0;nx->path_len[i]=0;
                nx->ai_state[i]=EW_AI_CHASE;moving=1;
            }else if(xz>14.0){
                nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
                nx->path_len[i]=0;nx->ai_state[i]=EW_AI_CHASE;moving=1;
            }else{
                nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
                nx->ai_state[i]=EW_AI_ATTACK;
            }
            if(nx->attack_time[i]<=0)
                nx->attack_time[i]=attack_cooldown_ticks(type);
        }else if(aggro&&type==EW_TYPE_CREEPER&&xz<=3.0&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(++m->creeper_fuse[i]>=30){
                nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;m->creeper_fuse[i]=0;m->explosion_pending=1;
                m->explosion_x=now->x[i];m->explosion_y=now->y[i]+0.5;m->explosion_z=now->z[i];
            }
        }else if(aggro&&gm_is_slimey(type)){
            /* Slime hop toward player. Squish edges use wasOnGround after
             * move_mob (EntitySlime.onUpdate order); AI only triggers jumps. */
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(m->jump_delay[i]>0)--m->jump_delay[i];
            if(now->on_ground[i]&&m->jump_delay[i]<=0){
                jump=1;moving=1;
                m->jump_delay[i]=10+m->size[i]*5;
                nx->ai_state[i]=EW_AI_CHASE;
            }else if(!now->on_ground[i]){
                moving=1;nx->ai_state[i]=EW_AI_CHASE;
            }else {
                nx->ai_state[i]=EW_AI_IDLE;
            }
            if(xz<=GM_MOB_REACH*(0.5+m->size[i]*0.25)&&fabs(dy)<(double)m->size[i]+1.0&&
               nx->attack_time[i]<=0){
                float dmg=melee_damage(type,m->size[i]);
                if(dmg>0.0f){
                    (void)gm_mobs_attack_player(m,(struct PvStats *)v,
                                                &p->inv,dmg,0);
                    p->health=v->health;
                }
                nx->attack_time[i]=attack_cooldown_ticks(type);
            }
        }else if(aggro&&xz<=GM_MOB_REACH&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(nx->attack_time[i]<=0){
                int hit=gm_mobs_attack_player(m,(struct PvStats *)v,
                                              &p->inv,
                                              melee_damage(type,m->size[i]),0);
                p->health=v->health;
                if(hit&&type==EW_TYPE_WITHER_SKELETON)
                    m->player_wither_ticks=200;
                nx->attack_time[i]=attack_cooldown_ticks(type);
            }
            /* Spider leap (EntityAILeapAtTarget): periodic, not every tick. */
            if(type==EW_TYPE_SPIDER&&now->on_ground[i]&&xz>1.5&&xz<8.0&&
               (m->tick % 20)==0)jump=1;
        }else if(aggro){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_CHASE;moving=1;
            if(type==EW_TYPE_CREEPER&&m->creeper_fuse[i]>0)--m->creeper_fuse[i];
            if(type==EW_TYPE_SPIDER&&now->on_ground[i]&&xz>2.0&&xz<10.0&&
               (m->tick % 20)==0)jump=1;
            /* Silverfish short follow; hop on a 10-tick cadence. */
            if(type==EW_TYPE_SILVERFISH&&now->on_ground[i]&&(m->tick % 10)==0)jump=1;
        }else{
            nx->ai_state[i]=EW_AI_IDLE;wandering=1;
            if(nx->repath_timer[i]>0)--nx->repath_timer[i];
            int has=nx->path_len[i]==1;
            if(has){
                double wx2=nx->path_tx[i]-now->x[i],wz2=nx->path_tz[i]-now->z[i];
                if(wx2*wx2+wz2*wz2<1.0){nx->path_len[i]=0;has=0;}
            }
            if(!has&&nx->repath_timer[i]<=0){
                nx->repath_timer[i]=GM_MOB_WANDER_INTERVAL;
                u64 h=mc_hash_seed((u64)m->seed,m->tick,i,0,0,0x57414e44u);
                int ddx=mc_hash_bound(h,2*GM_MOB_WANDER_RADIUS+1)-GM_MOB_WANDER_RADIUS;
                int ddz=mc_hash_bound(mc_hash64(h),2*GM_MOB_WANDER_RADIUS+1)-GM_MOB_WANDER_RADIUS;
                if(ddx||ddz){
                    int txc=mc_floor(now->x[i])+ddx,tzc=mc_floor(now->z[i])+ddz;
                    int ty=wander_ground_y(w,txc,mc_floor(now->y[i]),tzc);
                    if(ty>-999){
                        nx->path_tx[i]=txc+0.5;nx->path_ty[i]=ty;nx->path_tz[i]=tzc+0.5;
                        nx->path_len[i]=1;has=1;
                    }
                }
            }
            if(has)moving=1;
            /* Neutral pigman / small slime idle hop. */
            if(gm_is_slimey(type)&&now->on_ground[i]){
                if(m->jump_delay[i]>0)--m->jump_delay[i];
                else if(moving){
                    jump=1;m->jump_delay[i]=20+m->size[i]*10;
                }
            }
        }
        /* EntitySlime.onUpdate: lerp factor first (before physics). */
        if(gm_is_slimey(type)){
            m->squish_factor[i] += (m->squish_amount[i] - m->squish_factor[i]) * 0.5f;
        }
        /* Hostile-era step-up / hole-abort. EntityMoveHelper owns jump on the
         * det passive path; PathNavigateGround does not sample 0.9 ahead. */
        if(moving&&type!=EW_TYPE_GHAST && !(pai_det() && passive)){
            double mvx=nx->path_tx[i]-now->x[i],mvz=nx->path_tz[i]-now->z[i];
            double len=sqrt(mvx*mvx+mvz*mvz);
            if(len>0.01){
                int ax=mc_floor(now->x[i]+mvx/len*0.9),az=mc_floor(now->z[i]+mvz/len*0.9);
                int fy=mc_floor(now->y[i]);
                if(solid_id(gm_world_block(w,ax,fy,az))&&
                   !solid_id(gm_world_block(w,ax,fy+1,az))&&
                   !solid_id(gm_world_block(w,ax,fy+2,az)))jump=1;
                else if(wandering&&!solid_id(gm_world_block(w,ax,fy,az))&&
                        !solid_id(gm_world_block(w,ax,fy-1,az))&&
                        !solid_id(gm_world_block(w,ax,fy-2,az))){
                    moving=0;nx->path_len[i]=0;
                }
            }
        }
        /* EntityLivingBase.onLivingUpdate: updateEntityActionState (lookHelper)
         * then moveEntityWithHeading. Det matches that; knob-off keeps look
         * after travel so test_mob_live stays byte-identical. */
        if(passive && pai_det()) pai_apply_current_look(m,nx,i,px,py,pz);
        move_mob(w,st,m,nx,i,moving,jump,swim_jump,nav_speed);
        if(passive && !pai_det()) pai_apply_current_look(m,nx,i,px,py,pz);
        /* EntityLiving.updateDistance -> bodyHelper, once from onUpdate headTurn. */
        if(passive && pai_det()) pai_body_update(m, nx, now, i);
        if(passive && pai_det() && type==EW_TYPE_CHICKEN){
            if(--m->chicken_egg[i] <= 0){
                (void)jrand_float(pai_jr(m, i));
                (void)jrand_float(pai_jr(m, i));
                m->chicken_egg[i] = jrand_int_bound(pai_jr(m, i), 6000) + 6000;
            }
        }
        /* After super.onUpdate/move: wasOnGround edges then alterSquishAmount. */
        if(gm_is_slimey(type)){
            int on = nx->on_ground[i] ? 1 : 0;
            if(on && !m->was_on_ground[i]) m->squish_amount[i] = -0.5f;
            else if(!on && m->was_on_ground[i]) m->squish_amount[i] = 1.0f;
            m->was_on_ground[i] = (unsigned char)on;
            float damp = (type == EW_TYPE_MAGMA) ? 0.9f : 0.6f;
            m->squish_amount[i] *= damp; /* alterSquishAmount */
        }
    }
    tick_xp_orbs(m,w,p,ox,oz);
    ++m->tick;m->current^=1;
    (void)drops;
}

int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max) {
    if(!m||!out||max<=0)return 0;
    const EwStore *s=const_store(m);int n=0;
    for(int i=1;i<EW_MAX_ENTITIES&&n<max;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&gm_living(s->type[i])){
        out[n]=(GmEntityView){0};
        out[n].type=s->type[i];
        /* Live pigman uses type 15; render as zombie silhouette + pigman skin.
         * Keep type 15 so callers can identify pigmen; entity_render maps it. */
        if(s->type[i]==EW_TYPE_PIGMAN)
            out[n].skin = 18; /* CR_MOB_PIGMAN+1 (atlas index 17) */
        out[n].x=(float)s->x[i];out[n].y=(float)s->y[i];
        out[n].z=(float)s->z[i];out[n].yaw=s->yaw[i];
        out[n].health=s->health[i];
        out[n].ent_id=s->id[i];
        if(gm_passive(s->type[i])){
            out[n].head_yaw=m->passive_head_yaw[i];
            out[n].pitch=m->passive_head_pitch[i];
        }
        if(s->type[i]==EW_TYPE_SHEEP){
            int timer=m->passive_eat_time[i];
            out[n].sheared=m->passive_sheared[i];
            out[n].fleece_color=0;
            if(timer<=0)out[n].graze_y=0.0f;
            else if(timer>=4&&timer<=36)out[n].graze_y=1.0f;
            else if(timer<4)out[n].graze_y=(float)timer/4.0f;
            else out[n].graze_y=-(float)(timer-40)/4.0f;
            if(timer>4&&timer<=36){
                float f=(float)(timer-4)/32.0f;
                out[n].graze_x=(float)(MC_PI/5.0)+
                    (float)(MC_PI*7.0/100.0)*sinf(f*28.7f);
            }else if(timer>0)out[n].graze_x=(float)(MC_PI/5.0);
            else out[n].graze_x=out[n].pitch*(float)(MC_PI/180.0);
        }
        out[n].item_meta=m->size[i]; /* slime/magma size for render scale */
        out[n].squish=m->squish_factor[i]; /* EntitySlime.squishFactor */
        out[n].creeper_fuse=m->creeper_fuse[i];
        /* Recorder flags bit 0 is EntityLivingBase.isBurning(). Live state
         * tracks only generic fire ticks and EntityBlaze's charged override. */
        if(m->fire_ticks[i]>0 ||
           (s->type[i]==EW_TYPE_BLAZE && m->blaze_on_fire[i]))
            out[n].flags |= 1;
        ++n;
    }
    for(int i=0;i<GM_XP_ORBS&&n<max;++i){const McOrb *o=&m->xp_orbs[i];
        if(o->dead||o->xpValue<=0||m->orb_dimension[i]!=m->active_dimension)continue;
        GmEntityView v; memset(&v,0,sizeof v);
        v.type=GM_ENTITY_XP_ORB;
        v.x=(float)o->posX;v.y=(float)o->posY;v.z=(float)o->posZ;
        v.health=(float)o->xpValue;   /* legacy field */
        v.item_id=o->xpValue;         /* getTextureByXP */
        v.item_meta=o->xpColor;       /* RenderXPOrb colour phase */
        v.age=o->xpOrbAge;
        out[n++]=v;
    }return n;
}

int gm_mobs_alive(const GmMobLive *m){return m?alive_count(m,const_store(m)):0;}

int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops){
    if(!m)return 0;
    EwStore *s=now_store(m);int best=-1;double bd=radius*radius;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&gm_living(s->type[i])&&
                                         s->type[i]!=EW_TYPE_BOAT){
        double dx=s->x[i]-x,dy=(s->y[i]+0.9)-y,dz=s->z[i]-z,d=dx*dx+dy*dy+dz*dz;
        if(d<=bd){bd=d;best=i;}
    }
    if(best<0)return 0;
    s->health[best]-=damage;mark_hurt(m,s,best);
    if(s->health[best]<=0)mob_drop(m,s,best,drops);
    ew_store_copy(next_store(m),s);return 1;
}

int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z){
    if(!m||!m->explosion_pending)return 0;
    if(x)*x=m->explosion_x;
    if(y)*y=m->explosion_y;
    if(z)*z=m->explosion_z;
    m->explosion_pending=0;return 1;
}

int gm_mobs_take_fireball(GmMobLive *m,double *x,double *y,double *z,
                          double *vx,double *vy,double *vz){
    /* Returns pending kind: 0=none, 3=small fireball, 5=large fireball. */
    if(!m||!m->fireball_pending)return 0;
    int kind=m->fireball_pending;
    if(x)*x=m->fireball_x;if(y)*y=m->fireball_y;if(z)*z=m->fireball_z;
    if(vx)*vx=m->fireball_vx;if(vy)*vy=m->fireball_vy;if(vz)*vz=m->fireball_vz;
    m->fireball_pending=0;return kind;
}
