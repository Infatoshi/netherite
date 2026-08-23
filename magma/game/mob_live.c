#include "player_survival.h"

#include "game/mob_live.h"
#include "entity_spine.h"
#include "explosion_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "player_vitals.h"
#include "core/config.h"
#include "path_finder.h"
#include "../../blaze/env/blaze_snapshot.h"

#define ML_W GmWorld
#define ML_BLOCK(w, x, y, z) gm_world_block((w), (x), (y), (z))
#define ML_SKY(w, x, y, z) gm_world_sky_light((w), (x), (y), (z))
#define ML_BLK(w, x, y, z) gm_world_block_light((w), (x), (y), (z))
#define ML_SET_BLOCK(w, x, y, z, id) gm_world_set_block((w), (x), (y), (z), (id))
#include "hostile_live.h"

typedef struct {
    GmWorld *w;
    GmMobLive *m;
    EwStore *s;
} GmHsWorld;

static int gm_hs_place(GmHsWorld *h, int type, double x, double y, double z,
                       float yaw, unsigned long long seed48, int have_g,
                       double g, int extra);
static int gm_hs_count(const GmHsWorld *h);
static int gm_ps_count(const GmHsWorld *h);
static int gm_hs_hit(const GmHsWorld *h, double x0, double y0, double z0,
                     double x1, double y1, double z1);
static int gm_hs_in_clip(const GmHsWorld *h, int x, int y, int z);
static int gm_hs_block(const GmHsWorld *h, int x, int y, int z);
static int gm_hs_sky(const GmHsWorld *h, int x, int y, int z);
static int gm_hs_blk(const GmHsWorld *h, int x, int y, int z);

#define HS_W GmHsWorld
#define HS_BLOCK(h, x, y, z) gm_hs_block((h), (x), (y), (z))
#define HS_SKY(h, x, y, z) gm_hs_sky((h), (x), (y), (z))
#define HS_BLK(h, x, y, z) gm_hs_blk((h), (x), (y), (z))
#define HS_HOSTILE_COUNT(h) gm_hs_count(h)
#define HS_MOB_HIT(h, x0, y0, z0, x1, y1, z1) gm_hs_hit((h), (x0), (y0), (z0), (x1), (y1), (z1))
#define HS_PLACE(h, type, x, y, z, yaw, seed48, have_g, g, extra) \
    gm_hs_place((h), (type), (x), (y), (z), (yaw), (seed48), (have_g), (g), (extra))
#define HS_CREATURE_COUNT(h) gm_ps_count(h)
#include "hostile_spawn.h"
#define ML_SKY(w, x, y, z) gm_world_sky_light((w), (x), (y), (z))
#define ML_BLK(w, x, y, z) gm_world_block_light((w), (x), (y), (z))
#include "passive_live.h"
#include "xp_live.h"
#define BL_W GmWorld
#define BL_BLOCK(w, x, y, z) gm_world_block((w), (x), (y), (z))
#include "boat_live.h"

#include <math.h>
#include <string.h>

typedef char gm_mobs_snap_cap_is_ew_max
    [(BLAZE_SNAP_MAX_MOBS == EW_MAX_ENTITIES) ? 1 : -1];
typedef char gm_mobs_path_cap_is_48
    [(BLAZE_SNAP_PATH_CAP == 48) ? 1 : -1];

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
static int hai_ok(int type){
    return type==EW_TYPE_ZOMBIE||type==EW_TYPE_SKELETON||type==EW_TYPE_CREEPER
        ||type==EW_TYPE_SPIDER||type==EW_TYPE_SLIME||type==EW_TYPE_ENDERMAN
        ||type==EW_TYPE_WITCH;
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

/* PathNavigate.canEntityStandOnPos: IBlockState.isFullBlock() of pos.down.
 * Block.fullBlock is isOpaqueCube (light_opacity 255 for KEEP cubes). Stairs,
 * slabs, fences, walls, and gates are not full blocks even when BF_SOLID. */
static int pai_is_full_block(int id) {
    BptProps p;
    if (id == 0) return 0;
    switch (id) {
        case 44: case 43: case 125: case 126: case 181: case 182: /* slabs */
        case 53: case 67: case 108: case 109: case 114: case 128:
        case 134: case 135: case 136: case 156: case 163: case 164:
        case 180: case 203: /* stairs */
        case 85: case 113: case 188: case 189: case 190: case 191: case 192:
        case 139: case 107: /* fence / wall / gate */
        case 65: case 96: case 167: /* ladder / trapdoor */
            return 0;
        default:
            break;
    }
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID) && p.light_opacity >= 255;
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
    if (type == EW_TYPE_WITCH) return 26.0f;
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
    int gate;
    if (!m || !v || amount <= 0.0f) return 0;
    gate = ml_hurt_gate(&m->player_hurt_resistant, &m->player_last_damage,
                        amount, &applied);
    if (!gate) return 0;
    applied = mob_apply_armor((IsrInv *)player_inv, applied, bypass_armor);
    if (applied > 0.0f) pv_attack(v, applied);
    return gate;
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
    m->det_target_tick[slot] = 0;
    m->det_target_tasks[slot] = 0;
    m->det_has_target[slot] = 0;
    m->det_melee_delay[slot] = 0;
    m->det_melee_tx[slot] = 0.0;
    m->det_melee_ty[slot] = 0.0;
    m->det_melee_tz[slot] = 0.0;
    m->det_see_time[slot] = 0;
    m->det_strafe_time[slot] = -1;
    m->det_bow_attack_time[slot] = -1;
    m->det_strafe_cw[slot] = 0;
    m->det_strafe_back[slot] = 0;
    m->det_cstate[slot] = -1;
    m->det_raise_arm[slot] = 0;
    m->det_skel_melee[slot] = 0;
    m->det_follow[slot] = 0.0f;
    m->det_box_on[slot] = 0;
    m->fire_ticks[slot] = 0;
    m->despawn_ticks[slot] = 0;
    m->anger[slot] = 0;
    m->jump_delay[slot] = 0;
    m->charge[slot] = 0;
    m->blaze_on_fire[slot] = 0;
    m->boat_damage[slot] = 0;
    m->det_persist[slot] = 0;
    m->ent_jr_have_gauss[slot] = 0;
    m->ent_jr_gauss[slot] = 0.0;
    m->blaze_hot[slot] = 0;
    m->blaze_hof[slot] = 0.5f;
    m->hurt_time[slot] = 0;
    m->death_time[slot] = 0;
    m->screaming[slot] = 0;
    m->carried[slot] = 0;
    m->carried_meta[slot] = 0;
    m->target_change_time[slot] = 0;
    m->ticks_existed[slot] = 0;
    m->find_aggro[slot] = 0;
    m->teleport_time[slot] = 0;
    m->witch_attack_timer[slot] = 0;
    m->witch_drink[slot] = 0;
    m->effect_id[slot] = 0;
    m->effect_duration[slot] = 0;
    m->effect_amplifier[slot] = 0;
    m->boat_delta_rot[slot] = 0.0f;
    m->boat_glide[slot] = 0.8f;
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
    if (gm_passive(s->type[slot])) {
        m->panic_ticks[slot] = GM_MOB_REVENGE_TICKS;
        s->path_len[slot] = 0;
        s->ai_state[slot] = EW_AI_IDLE;
    }
    m->hurt_aggro[slot] = 1;
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
    return ml_los_clear(w, x0, y0, z0, x1, y1, z1);
}

static void ml_load_slot(MlMob *o, const GmMobLive *m, const EwStore *s, int i) {
    memset(o, 0, sizeof *o);
    o->snap.slot = i;
    o->snap.id = s->id[i];
    o->snap.type = (int)s->type[i];
    o->snap.alive = (int)s->alive[i];
    o->snap.x = s->x[i];
    o->snap.y = s->y[i];
    o->snap.z = s->z[i];
    o->snap.yaw = s->yaw[i];
    o->snap.mx = s->vx[i];
    o->snap.my = s->vy[i];
    o->snap.mz = s->vz[i];
    o->snap.on_ground = (int)s->on_ground[i];
    o->snap.health = s->health[i];
    o->snap.hurt_time = m->hurt_time[i];
    o->snap.death_time = m->death_time[i];
    o->snap.screaming = m->screaming[i];
    o->snap.carried = m->carried[i];
    o->snap.carried_meta = m->carried_meta[i];
    o->snap.target_change_time = m->target_change_time[i];
    o->snap.ticks_existed = m->ticks_existed[i];
    o->snap.find_aggro = m->find_aggro[i];
    o->snap.teleport_time = m->teleport_time[i];
    o->snap.witch_attack_timer = m->witch_attack_timer[i];
    o->snap.witch_drink = m->witch_drink[i];
    o->snap.effect_id = m->effect_id[i];
    o->snap.effect_duration = m->effect_duration[i];
    o->snap.effect_amplifier = m->effect_amplifier[i];
    o->snap.attack_time = s->attack_time[i];
    o->snap.swell = m->creeper_fuse[i];
    o->snap.target_idx = m->det_has_target[i] ? 1 : 0;
    o->snap.task_bits = s->ai_state[i];
    o->snap.wander_x = s->path_tx[i];
    o->snap.wander_z = s->path_tz[i];
    o->snap.panic = (int)s->path_len[i];
    o->snap.box_on = m->det_box_on[i];
    o->snap.melee_delay = m->det_melee_delay[i];
    o->snap.see_time = m->det_see_time[i];
    o->snap.stime = m->det_strafe_time[i];
    o->snap.anger = m->anger[i];
    o->repath_timer = s->repath_timer[i];
    o->despawn_ticks = m->entity_age[i];
    o->fire_ticks = m->fire_ticks[i];
    o->size = (int)m->size[i];
    o->snap.persist = (int)m->det_persist[i];
    o->snap.seed48 = m->ent_jr_seed[i];
    o->snap.have_gauss = m->ent_jr_have_gauss[i];
    o->snap.gauss = m->ent_jr_gauss[i];
    if (gm_is_slimey(s->type[i])) {
        if (o->size < 1) o->size = 2;
        o->snap.swell = o->size;
        o->snap.melee_delay = m->jump_delay[i];
        o->snap.see_time = m->was_on_ground[i];
    }
}

static void ml_save_slot(GmMobLive *m, EwStore *s, int i, const MlMob *o) {
    const RlSnapMob *p = &o->snap;
    s->id[i] = p->id;
    s->type[i] = (u8)p->type;
    s->alive[i] = (u8)(p->alive ? 1 : 0);
    s->x[i] = p->x;
    s->y[i] = p->y;
    s->z[i] = p->z;
    s->yaw[i] = p->yaw;
    s->vx[i] = p->mx;
    s->vy[i] = p->my;
    s->vz[i] = p->mz;
    s->on_ground[i] = (u8)(p->on_ground ? 1 : 0);
    s->health[i] = p->health;
    m->hurt_time[i] = p->hurt_time;
    m->death_time[i] = p->death_time;
    m->screaming[i] = p->screaming;
    m->carried[i] = p->carried;
    m->carried_meta[i] = p->carried_meta;
    m->target_change_time[i] = p->target_change_time;
    m->ticks_existed[i] = p->ticks_existed;
    m->find_aggro[i] = p->find_aggro;
    m->teleport_time[i] = p->teleport_time;
    m->witch_attack_timer[i] = p->witch_attack_timer;
    m->witch_drink[i] = p->witch_drink;
    m->effect_id[i] = p->effect_id;
    m->effect_duration[i] = p->effect_duration;
    m->effect_amplifier[i] = p->effect_amplifier;
    s->attack_time[i] = p->attack_time;
    s->ai_state[i] = p->task_bits;
    s->path_tx[i] = p->wander_x;
    s->path_ty[i] = p->y;
    s->path_tz[i] = p->wander_z;
    s->path_len[i] = (u32)(p->panic ? p->panic : 0);
    s->repath_timer[i] = o->repath_timer;
    m->creeper_fuse[i] = p->swell;
    m->det_has_target[i] = (unsigned char)(p->target_idx ? 1 : 0);
    m->passive_tasks[i] = p->task_bits;
    m->passive_idle_x[i] = p->wander_x;
    m->passive_idle_z[i] = p->wander_z;
    m->det_melee_delay[i] = p->melee_delay;
    m->det_see_time[i] = p->see_time;
    m->det_strafe_time[i] = p->stime;
    m->anger[i] = p->anger;
    m->despawn_ticks[i] = o->despawn_ticks;
    m->entity_age[i] = o->despawn_ticks;
    m->ent_jr_seed[i] = o->snap.seed48;
    m->ent_jr_have_gauss[i] = o->snap.have_gauss;
    m->ent_jr_gauss[i] = o->snap.gauss;
    m->fire_ticks[i] = o->fire_ticks;
    if (gm_is_slimey(p->type)) {
        m->size[i] = (unsigned char)(p->swell > 0 ? p->swell : 1);
        m->jump_delay[i] = p->melee_delay;
        m->was_on_ground[i] = (unsigned char)(p->see_time ? 1 : 0);
    }
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
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_ENDERMAN) {
        if (task == PAI_WANDER) return 7;
        if (task == PAI_WATCH || task == PAI_IDLE) return 8;
        return 99;
    }
    if (type == EW_TYPE_PIGMAN) {
        if (task == PAI_SWIM) return 0;
        if (task == PAI_WANDER) return 7;
        if (task == PAI_WATCH || task == PAI_IDLE) return 8;
        return 99;
    }
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
    if (type == EW_TYPE_ZOMBIE) return 0.23000000417232513;
    if (type == EW_TYPE_SKELETON || type == EW_TYPE_CREEPER) return 0.25;
    if (type == EW_TYPE_ENDERMAN) return 0.30000001192092896;
    if (type == EW_TYPE_WITCH) return 0.25;
    return 0.23000000417232513;
}

/* SharedMonsterAttributes.FOLLOW_RANGE setBaseValue. EntityLiving default
 * 16.0; EntityZombie/PigZombie 35.0; EntityBlaze 48.0. Creeper/skeleton keep 16. */
static float pai_follow_range(int type) {
    if (type == EW_TYPE_ENDERMAN) return 64.0f;
    if (type == EW_TYPE_BLAZE) return 48.0f;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_PIGMAN) return 35.0f;
    return 16.0f;
}

static double pai_panic_multiplier(int type) {
    if (type == EW_TYPE_COW) return 2.0;
    if (type == EW_TYPE_CHICKEN) return 1.4;
    return 1.25; /* sheep, pig */
}

static void pai_size(int type, float *width, float *height) {
    if (type == EW_TYPE_BLAZE) { *width = 0.6f; *height = 1.8f; return; }
    if (type == EW_TYPE_PIGMAN) { *width = 0.6f; *height = 1.95f; return; }
    if (type == EW_TYPE_ENDERMAN) { *width = 0.6f; *height = 2.9f; return; }
    if (type == EW_TYPE_WITCH) { *width = 0.6f; *height = 1.95f; return; }
    *width = 0.9f;
    if (type == EW_TYPE_SHEEP) *height = 1.3f;
    else if (type == EW_TYPE_PIG) *height = 0.9f;
    else if (type == EW_TYPE_COW) *height = 1.4f;
    else if (type == EW_TYPE_ZOMBIE) { *width = 0.6f; *height = 1.95f; }
    else if (type == EW_TYPE_SKELETON) { *width = 0.6f; *height = 1.99f; }
    else if (type == EW_TYPE_CREEPER) { *width = 0.6f; *height = 1.7f; }
    else { *width = 0.4f; *height = 0.7f; }
}

static double pai_eye_height(int type) {
    float width, height;
    pai_size(type, &width, &height);
    (void)width;
    if (type == EW_TYPE_SHEEP) return (double)(0.95f * height);
    if (type == EW_TYPE_COW) return 1.3;
    if (type == EW_TYPE_CHICKEN) return (double)height;
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON)
        return (double)1.74f; /* AbstractSkeleton/EntityZombie getEyeHeight ldc 1.74F */
    if (type == EW_TYPE_ENDERMAN) return (double)2.55f; /* EntityEnderman.getEyeHeight */
    if (type == EW_TYPE_CREEPER) return (double)(1.7f * 0.85f);
    return (double)(height * 0.85f);
}

/* EntityLookHelper.setLookPositionWithEntity: posY + getEyeHeight()F f2d. */
static double pai_player_eye_y(double py) {
    return py + (double)(float)PSV_EYE_HEIGHT;
}

static double pai_watch_range_sq(int type) {
    /* EntityAIWatchClosest maxDistance: animals 6, blaze/pigman/enderman 8. */
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN)
        return 64.0;
    return 36.0;
}

static float pai_avoid_water_p(int type) {
    /* EntityAIWanderAvoidWater probability. Blaze and enderman pass 0.0F. */
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_ENDERMAN) return 0.0f;
    return 0.001f;
}

static int pai_talk_interval(int type) {
    /* EntityLiving.getTalkInterval=80; EntityAnimal overrides 120. */
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN)
        return 80;
    return 120;
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

/* java.util.Random.nextGaussian polar method + one-value cache. */
static double pai_gaussian(GmMobLive *m, int i) {
    double v1, v2, s, mul;
    if (m->ent_jr_have_gauss[i]) {
        m->ent_jr_have_gauss[i] = 0;
        return m->ent_jr_gauss[i];
    }
    do {
        v1 = 2.0 * jrand_double(pai_jr(m, i)) - 1.0;
        v2 = 2.0 * jrand_double(pai_jr(m, i)) - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    mul = sqrt(-2.0 * log(s) / s);
    m->ent_jr_gauss[i] = v2 * mul;
    m->ent_jr_have_gauss[i] = 1;
    return v1 * mul;
}

static int pai_det_ai(int type) {
    return type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN ||
           type == EW_TYPE_ENDERMAN ||
           type == EW_TYPE_SHEEP || type == EW_TYPE_PIG ||
           type == EW_TYPE_COW || type == EW_TYPE_CHICKEN;
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

/* World.getLightBrightness = provider.lightBrightnessTable[getLightFromNeighbors].
 * Overworld: (1-f1)/(f1*3+1). Nether WorldProviderHell: that * 0.9F + 0.1F. */
static float pai_light_brightness(GmWorld *w, int dim, int x, int y, int z) {
    float t = pai_brightness(w, x, y, z);
    if (dim == -1) t = t * 0.9f + 0.1f;
    return t;
}

/* EntityAnimal: grass-down 10 else getLightBrightness-0.5F.
 * EntityMob (blaze/pigman/zombie/skel/creeper): 0.5F - getLightBrightness.
 * EntityCreature default is 0.0F. */
static float pai_block_path_weight(GmMobLive *m, GmWorld *w, int type,
                                   int x, int y, int z) {
    float br = pai_light_brightness(w, m->active_dimension, x, y, z);
    if (type == EW_TYPE_SHEEP || type == EW_TYPE_PIG ||
        type == EW_TYPE_COW || type == EW_TYPE_CHICKEN) {
        if (gm_world_block(w, x, y - 1, z) == 2) return 10.0f;
        return br - 0.5f;
    }
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN ||
        type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON ||
        type == EW_TYPE_CREEPER)
        return 0.5f - br;
    return 0.0f;
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
        /* PathNavigate.canEntityStandOnPos: IBlockState.isFullBlock of pos.down. */
        if (by <= 0 || !pai_is_full_block(gm_world_block(w, bx, by - 1, bz))) continue;
        int score_y = by;
        if (land && solid_id(gm_world_block(w, bx, score_y, bz))) {
            while (score_y < 256 && solid_id(gm_world_block(w, bx, score_y, bz)))
                ++score_y;
        }
        if (land) {
            int id = gm_world_block(w, bx, score_y, bz);
            if (id == 8 || id == 9) continue;
        }
        float score = pai_block_path_weight(m, w, s->type[i], bx, score_y, bz);
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

/* PathNavigateGround.getPathToPos: Material.AIR walks down then up one;
 * solid material walks up to the first non-solid. */
static int pai_mat_air(int id) { return id == 0; }
static int pai_mat_solid(int id) {
    BptProps p;
    if (id == 0 || id == 8 || id == 9 || id == 10 || id == 11 || id == 51)
        return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) != 0;
}

static void pai_path_to_pos(GmWorld *w, int *x, int *y, int *z) {
    int id = gm_world_block(w, *x, *y, *z);
    if (pai_mat_air(id)) {
        int by = *y - 1;
        while (by > 0 && pai_mat_air(gm_world_block(w, *x, by, *z))) --by;
        if (by > 0) {
            *y = by + 1;
            return;
        }
        while (*y < 256 && pai_mat_air(gm_world_block(w, *x, *y, *z))) ++*y;
        return;
    }
    if (!pai_mat_solid(id)) return;
    /* PathNavigateGround.getPathToPos: for (blockpos1 = pos.up();
     * blockpos1.getY() < world.getHeight() &&
     * world.getBlockState(blockpos1).getMaterial().isSolid();
     * blockpos1 = blockpos1.up()); return super.getPathToPos(blockpos1).
     * Starts at the RPG BlockPos, not the entity column. Material.isSolid,
     * not isFullBlock / isPassable. Stops at the first non-solid: a 1-block
     * air pocket (nether T182511 dest col y=96) is dest. */
    while (*y < 256 && pai_mat_solid(gm_world_block(w, *x, *y, *z))) ++*y;
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
    pai_pf.ent.canBreakDoors = 0;
    pai_pf.ent.maxFallHeight = 3;
    pai_pf.ent.onGround = s->on_ground[i] ? 1 : 0;
    pai_pf.ent.inWater = pai_in_material(w, s, i, 0);
    pai_pf.ent.posX = s->x[i] - (double)(*ox);
    pai_pf.ent.posY = s->y[i] - (double)(*oy);
    pai_pf.ent.posZ = s->z[i] - (double)(*oz);
    pnp_ent_default_priorities(&pai_pf.ent);
    /* EntityBlaze.<init> setPathPriority: WATER -1, LAVA 8, DANGER/DAMAGE_FIRE 0. */
    if (s->type[i] == EW_TYPE_BLAZE) {
        pai_pf.ent.pathPriority[PNT_WATER] = -1.0f;
        pai_pf.ent.pathPriority[PNT_LAVA] = 8.0f;
        pai_pf.ent.pathPriority[PNT_DANGER_FIRE] = 0.0f;
        pai_pf.ent.pathPriority[PNT_DAMAGE_FIRE] = 0.0f;
    }
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
 * getPathNodeType size-sweep with the navigator flags (canEnterDoors true,
 * canBreakDoors false unless zombie break-door). */
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
                                           sizeX, sizeY, sizeZ,
                                           pai_pf.ent.canBreakDoors,
                                           pai_pf.ent.canEnterDoors);
                if (t == PNT_WATER || t == PNT_LAVA || t == PNT_OPEN) return 0;
                t2 = pnp_getPathNodeTypeSize(&pai_pf, k - ox, y - oy, l - oz,
                                            sizeX, sizeY, sizeZ,
                                            pai_pf.ent.canBreakDoors,
                                            pai_pf.ent.canEnterDoors);
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

/* PathNavigateGround.canNavigate: onGround || (canSwim && inLiquid) || riding.
 * Sheep PathNavigateGround never setCanSwim; riding is unused. */
static int pai_can_navigate(const EwStore *s, int i) {
    return s->on_ground[i] != 0;
}

/* PathNavigate.pathFollow: close-advance then isDirectPathBetweenPoints skip. */
static void pai_nav_follow(GmMobLive *m, GmWorld *w, EwStore *s, int i) {
    float width, height, maxDist;
    int idx, n, j, same_y_end, k, l, i1, ox, oy, oz;
    double ex, ey, ez;
    pai_size(s->type[i], &width, &height);
    maxDist = width > 0.75f ? width / 2.0f : 0.75f - width / 2.0f;
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
}

/* PathNavigate.checkForStuck: 100-tick / 2.25D window. currentTimeMillis
 * timeout is wall-clock and CUT. */
static void pai_nav_stuck(GmMobLive *m, EwStore *s, int i) {
    double ex = s->x[i];
    double ey = (double)((int)(s->y[i] + 0.5));
    double ez = s->z[i];
    if (m->det_nav_ticks[i] - m->det_nav_stuck_at[i] > 100) {
        double dx = ex - m->det_nav_stuck_x[i];
        double dy = ey - m->det_nav_stuck_y[i];
        double dz = ez - m->det_nav_stuck_z[i];
        if (dx * dx + dy * dy + dz * dz < 2.25) {
            m->det_nav_n[i] = 0;
            s->path_len[i] = 0;
        }
        m->det_nav_stuck_at[i] = m->det_nav_ticks[i];
        m->det_nav_stuck_x[i] = ex;
        m->det_nav_stuck_y[i] = ey;
        m->det_nav_stuck_z[i] = ez;
    }
}

/* PathNavigate.onUpdateNavigation: pathFollow iff canNavigate, else airborne
 * same-cell Y-above increment. setMoveTo (pai_nav_apply) always. totalTicks
 * increments only while a Path exists (caller gates on det_nav_n). */
static void pai_nav_update(GmMobLive *m, GmWorld *w, EwStore *s, int i) {
    int idx, n;
    if (!pai_det() || m->det_nav_n[i] == 0) return;
    ++m->det_nav_ticks[i];
    if (pai_can_navigate(s, i)) {
        pai_nav_follow(m, w, s, i);
        pai_nav_stuck(m, s, i);
    } else {
        float width, height;
        int off;
        double vx, vy, vz, ey;
        pai_size(s->type[i], &width, &height);
        idx = m->det_nav_i[i];
        n = m->det_nav_n[i];
        if (idx < n) {
            off = pnp_floor_d((double)(width + 1.0f));
            vx = (double)m->det_nav_x[i][idx] + (double)off * 0.5;
            vy = (double)m->det_nav_y[i][idx];
            vz = (double)m->det_nav_z[i][idx] + (double)off * 0.5;
            ey = (double)((int)(s->y[i] + 0.5));
            if (ey > vy && mc_floor(s->x[i]) == mc_floor(vx) &&
                mc_floor(s->z[i]) == mc_floor(vz))
                m->det_nav_i[i] = (unsigned char)(idx + 1);
        }
    }
    pai_nav_apply(m, s, i);
}

static int pai_find_path(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                         double tx, double ty, double tz) {
    int ox, oy, oz, n, k;
    int bx = mc_floor(tx), by = mc_floor(ty), bz = mc_floor(tz);
    /* PathNavigate.getPathToPos: if (!canNavigate()) return null.
     * Ground override still ends at super, so airborne tryMoveToXYZ is a no-op. */
    if (!pai_can_navigate(s, i)) {
        m->det_nav_n[i] = 0;
        s->path_len[i] = 0;
        return 0;
    }
    pai_fill_pf(w, s, i, &ox, &oy, &oz);
    pai_path_to_pos(w, &bx, &by, &bz);
    /* PathFinder.findPath: dest PathPoint is openPoint(floor) even if it sits
     * outside the 32x24x32 window (Java ChunkCache is FOLLOW_RANGE+8,
     * PathNavigate.java:121-126). Do not clip dest into the window: that made
     * A* treat a neighbour as closer and over-walk. Closest==start already
     * returns n=0 (PathFinder.java:113-116). */
    n = pf12_findPath(&pai_pf,
                      (double)((float)bx + 0.5f) - (double)ox,
                      (double)((float)by + 0.5f) - (double)oy,
                      (double)((float)bz + 0.5f) - (double)oz,
                      m->det_follow[i] > 0.5f ? m->det_follow[i]
                                              : pai_follow_range(s->type[i]));
    /* Magma's 32x24x32 window is smaller than Java ChunkCache (FOLLOW_RANGE+8),
     * so a dest snapped onto a solid column (PathNavigateGround.getPathToPos
     * walk-up, .java:76-83) can sit outside the grid. A* then treats a same-y
     * neighbour as closer via manhattan to that far dest. On a flat floor
     * PathFinder.java:113-116 does NOT null that case (a neighbour is closer).
     * Reject only a neighbour-only (md<=1) path when dest is out of window:
     * T182511 t=31 n=2 md=1 matches Java null; t=595 n=4 dest y=95 Java walks
     * so a blanket dest-out-of-window null regresses. t=745 n=10 dest y=96
     * same-Y +Z is the residual (Java closest==start). */
    if (n > 0 && !pnp_in(bx - ox, by - oy, bz - oz)) {
        int sx = pai_pf.resultPts[0], sy = pai_pf.resultPts[1], sz = pai_pf.resultPts[2];
        int ex = pai_pf.resultPts[(n - 1) * 3 + 0];
        int ey = pai_pf.resultPts[(n - 1) * 3 + 1];
        int ez = pai_pf.resultPts[(n - 1) * 3 + 2];
        int md = (ex > sx ? ex - sx : sx - ex)
               + (ey > sy ? ey - sy : sy - ey)
               + (ez > sz ? ez - sz : sz - ez);
        if (md <= 1) n = 0;
    }
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

/* EntityLookHelper.onUpdateLook / EntityMoveHelper bytecode is
 * atan2; ldc2_w 57.29577951308232d; dmul; (pitch: dneg); d2f.
 * The 1.11.2 oracle remainder matches LUT * (float)(180.0/(float)PI)
 * for MOVE_TO yaw, watch pitch, and post-hit pitch. dmul is 1 ULP off. */
static float pai_deg(double rad) {
    return (float)(rad * (float)(180.0 / (float)MC_PI));
}

static float pai_atan2_yaw(double dz, double dx) {
    return pai_deg(mc_atan2(dz, dx)) - 90.0f;
}

static float pai_look_pitch(double dy, double horiz) {
    return -pai_deg(mc_atan2(dy, horiz));
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
        return dx * dx + dy * dy + dz * dz <= pai_watch_range_sq(s->type[i]) &&
               m->passive_watch_time[i] > 0;
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
            int land = pai_float(m, i, &stream) >= pai_avoid_water_p(s->type[i]);
            ok = pai_random_position(m, w, s, i, 10, 7, land, &stream, &x, &y, &z);
        }
        if (!ok) return 0;
        pai_set_path(m, w, s, i, x, y, z, 1.0);
    } else if (task == PAI_WATCH) {
        if (pai_float(m, i, &stream) >= 0.02f) return 0;
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        if (dx * dx + dy * dy + dz * dz > pai_watch_range_sq(s->type[i])) return 0;
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
            target_pitch = pai_look_pitch(dy, horiz);
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
        /* EntityAIWatchClosest.updateTask / LookHelper.setLookPositionWithEntity:
         * posY + (double)getEyeHeight()F. PSV_EYE_HEIGHT is the 1.62 double
         * literal; the 1.11.2 player method returns 1.62F then f2d. */
        pai_look_update(m, s, i, 1, px, py + (double)(float)PSV_EYE_HEIGHT, pz);
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
    /* targetSelector then goalSelector. Blaze heightOffset is the later
     * updateAITasks ("mob tick") slot, after navigator. */
    if (setup && pai_det() && type == EW_TYPE_BLAZE) {
        u64 stream = pai_rng_start(m, s, i, 64);
        (void)pai_bound(m, i, &stream, 10);
    }
    /* EntityEnderman.targetTasks: AIFindPlayer overrides NAT.shouldExecute
     * (no nextInt(10); stare predicate, 0 Entity.rand). HurtBy 0. Endermite
     * NAT chance 10: nextInt(10) on setup. Mutex 1; FindPlayer not using. */
    if (setup && pai_det() && type == EW_TYPE_ENDERMAN) {
        u64 stream = pai_rng_start(m, s, i, 64);
        (void)pai_bound(m, i, &stream, 10);
    }
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
        pai_nav_update(m, w, s, i);
    else if (s->path_len[i] && pai_path_done(w, s, i)) s->path_len[i] = 0;

    /* EntityBlaze.updateAITasks: after goals+nav. Super is empty. */
    if (pai_det() && type == EW_TYPE_BLAZE) {
        --m->blaze_hot[i];
        if (m->blaze_hot[i] <= 0) {
            m->blaze_hot[i] = 100;
            m->blaze_hof[i] = 0.5f + (float)pai_gaussian(m, i) * 3.0f;
        }
    }

    *moving = s->path_len[i] != 0;
    *wandering = *moving && (m->passive_tasks[i] & PAI_BIT(PAI_WANDER));
    *jump = 0;
    *nav_speed = *moving ? m->passive_nav_speed[i] : 0.0;
    s->ai_state[i] = EW_AI_IDLE;
    if (m->panic_ticks[i] > 0) --m->panic_ticks[i];
}

static int sky_exposed(GmWorld *w, double x, double y, double z);

/* Hostile det path (zombie/skeleton/creeper). Knob-off hostiles stay on the
 * generic aggro chain. Goal bits reuse swim/wander/watch/idle; extra bits
 * are melee=64 swell=128 bow=256. Target scheduler is a second EntityAITasks. */
enum {
    HSWIM = 0, HMELEE, HSWELL, HBOW, HREST, HFLEE, HAVOID, HHOME, HVILL,
    HWAND, HWATCH, HIDLE, HN
};
#define HT_HURT 1u
#define HT_PLAYER 2u
#define HT_VILLAGER 4u
#define HT_GOLEM 8u

static unsigned hai_bit(int task) {
    static const unsigned b[HN] = {
        1u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u, 8192u, 8u, 16u, 32u
    };
    return (task >= 0 && task < HN) ? b[task] : 0u;
}
static int hai_pri(int type, int task) {
    if (task == HSWIM) return type == EW_TYPE_ZOMBIE ? 0 : 1;
    if (task == HMELEE) return type == EW_TYPE_ZOMBIE ? 2 : 4;
    if (task == HSWELL) return 2;
    if (task == HBOW || task == HREST) return type == EW_TYPE_SKELETON && task == HREST ? 2 : 4;
    if (task == HFLEE || task == HAVOID) return 3;
    if (task == HHOME) return 5;
    if (task == HVILL) return 6;
    if (task == HWAND) return type == EW_TYPE_ZOMBIE ? 7 : 5;
    if (task == HWATCH || task == HIDLE) return type == EW_TYPE_ZOMBIE ? 8 : 6;
    return 99;
}
static int hai_mutex(int task) {
    if (task == HSWIM) return 4;
    if (task == HMELEE || task == HBOW || task == HIDLE) return 3;
    if (task == HWATCH) return 2;
    if (task == HREST) return 0;
    return 1;
}
static double hai_follow(int type) {
    return type == EW_TYPE_ZOMBIE ? 35.0 : 16.0;
}
static const int *hai_goals(int type, int skel_melee) {
    static const int z[] = { HSWIM, HMELEE, HHOME, HVILL, HWAND, HWATCH, HIDLE, -1 };
    /* AbstractSkeleton.initEntityAI adds swim/restrict/flee/avoid/wander/watch/idle
     * with no combat task. Ctor then setCombatTask() LinkedHashSet-appends melee
     * (empty hand) or bow (onInitialSpawn). EntityAITasks iterates insertion
     * order, so wander/watch/idle shouldExecute run before combat starts and
     * still consume nextInt(120)+nextFloat+nextFloat on that setup tick. */
    static const int s_bow[] = { HSWIM, HREST, HFLEE, HAVOID, HWAND, HWATCH, HIDLE, HBOW, -1 };
    static const int s_melee[] = { HSWIM, HREST, HFLEE, HAVOID, HWAND, HWATCH, HIDLE, HMELEE, -1 };
    static const int c[] = { HSWIM, HSWELL, HAVOID, HMELEE, HWAND, HWATCH, HIDLE, -1 };
    if (type == EW_TYPE_SKELETON) return skel_melee ? s_melee : s_bow;
    if (type == EW_TYPE_CREEPER) return c;
    return z;
}

static int hai_can_use(const GmMobLive *m, int type, int i, int task) {
    unsigned mutex = (unsigned)hai_mutex(task);
    int pri = hai_pri(type, task), other;
    for (other = 0; other < HN; ++other) {
        if (other == task || !(m->passive_tasks[i] & hai_bit(other))) continue;
        if (pri >= hai_pri(type, other) && (mutex & (unsigned)hai_mutex(other))) return 0;
    }
    return 1;
}

static int hai_player_range(const EwStore *s, int i, double px, double py, double pz) {
    double fr = hai_follow(s->type[i]);
    double dx = px - s->x[i], dz = pz - s->z[i];
    double dy = pai_player_eye_y(py) - (s->y[i] + pai_eye_height(s->type[i]));
    if (dx * dx + dz * dz > fr * fr) return 0;
    if (fabs(dy) > fr) return 0;
    return 1;
}

static void hai_clear_nav(GmMobLive *m, EwStore *s, int i) {
    m->det_nav_n[i] = 0;
    s->path_len[i] = 0;
}

static int hai_continue(GmMobLive *m, GmWorld *w, EwStore *s, int i, int task,
                        double px, double py, double pz) {
    if (task == HSWIM) return pai_in_material(w, s, i, 0) || pai_in_material(w, s, i, 1);
    if (task == HMELEE) return m->det_has_target[i] && s->path_len[i] != 0;
    if (task == HSWELL) return 1;
    if (task == HBOW) return m->det_has_target[i] != 0;
    if (task == HREST) return 0; /* night tapes; daytime-only */
    if (task == HFLEE || task == HAVOID || task == HHOME || task == HVILL || task == HWAND)
        return s->path_len[i] != 0;
    if (task == HWATCH) {
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        return dx * dx + dy * dy + dz * dz <= 64.0 && m->passive_watch_time[i] > 0;
    }
    if (task == HIDLE) return m->passive_idle_time[i] >= 0;
    return 0;
}

static void hai_reset(GmMobLive *m, EwStore *s, int i, int task) {
    m->passive_tasks[i] &= ~hai_bit(task);
    if (task == HWATCH) m->passive_watch_time[i] = 0;
    if (task == HMELEE || task == HWAND || task == HBOW)
        hai_clear_nav(m, s, i);
    if (task == HMELEE) m->det_raise_arm[i] = 0;
    if (task == HBOW) {
        m->det_see_time[i] = 0;
        m->det_bow_attack_time[i] = -1;
        m->det_strafe_time[i] = -1;
    }
}

static int hai_try_start(GmMobLive *m, GmWorld *w, EwStore *s, int i, int task,
                         double px, double py, double pz, int day) {
    int type = s->type[i];
    (void)day;
    if (task == HSWIM)
        return pai_try_start(m, w, s, i, PAI_SWIM, px, py, pz);
    if (task == HMELEE) {
        if (!m->det_has_target[i]) return 0;
        if (!pai_find_path(m, w, s, i, px, py, pz)) {
            float width, height;
            double reach, ddx, ddz, ddy;
            pai_size(type, &width, &height);
            (void)height;
            reach = (double)(width * 2.0f * width * 2.0f + 0.6f);
            ddx = px - s->x[i]; ddz = pz - s->z[i];
            ddy = py - s->y[i];
            if (ddx * ddx + ddz * ddz + ddy * ddy > reach) return 0;
        }
        /* AbstractSkeleton$1 melee speed 1.2; EntityAIZombieAttack 1.0. */
        m->passive_nav_speed[i] = type == EW_TYPE_SKELETON ? 1.2 : 1.0;
        m->det_melee_delay[i] = 0;
        m->det_melee_tx[i] = m->det_melee_ty[i] = m->det_melee_tz[i] = 0.0;
        m->det_raise_arm[i] = 0;
        m->passive_tasks[i] |= hai_bit(HMELEE);
        return 1;
    }
    if (task == HSWELL) {
        double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
        if (m->det_cstate[i] <= 0 && !(m->det_has_target[i] && dx * dx + dy * dy + dz * dz < 9.0))
            return 0;
        hai_clear_nav(m, s, i);
        m->passive_tasks[i] |= hai_bit(HSWELL);
        return 1;
    }
    if (task == HBOW) {
        if (!m->det_has_target[i] || type != EW_TYPE_SKELETON) return 0;
        m->passive_nav_speed[i] = 1.0;
        m->passive_tasks[i] |= hai_bit(HBOW);
        return 1;
    }
    if (task == HREST || task == HFLEE || task == HAVOID || task == HHOME)
        return 0;
    if (task == HVILL) return 0;
    if (task == HWAND) {
        int ok = pai_try_start(m, w, s, i, PAI_WANDER, px, py, pz);
        if (ok && type == EW_TYPE_CREEPER) m->passive_nav_speed[i] = 0.8;
        return ok;
    }
    if (task == HWATCH) {
        u64 stream = pai_rng_start(m, s, i, PAI_WATCH);
        if (pai_float(m, i, &stream) >= 0.02f) return 0;
        {
            double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
            if (dx * dx + dy * dy + dz * dz > 64.0) return 0;
        }
        m->passive_watch_time[i] = 40 + pai_bound(m, i, &stream, 40);
        m->passive_tasks[i] |= hai_bit(HWATCH);
        return 1;
    }
    if (task == HIDLE)
        return pai_try_start(m, w, s, i, PAI_IDLE, px, py, pz);
    return 0;
}

static void hai_melee_update(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                             double px, double py, double pz) {
    int see, fire_repath;
    double d0, moved;
    --m->det_melee_delay[i];
    see = los_clear(w, s->x[i], s->y[i] + pai_eye_height(s->type[i]), s->z[i],
                    px, pai_player_eye_y(py), pz);
    d0 = (px - s->x[i]) * (px - s->x[i]) + (py - s->y[i]) * (py - s->y[i])
       + (pz - s->z[i]) * (pz - s->z[i]);
    moved = (px - m->det_melee_tx[i]) * (px - m->det_melee_tx[i])
          + (py - m->det_melee_ty[i]) * (py - m->det_melee_ty[i])
          + (pz - m->det_melee_tz[i]) * (pz - m->det_melee_tz[i]);
    fire_repath = 0;
    if (see && m->det_melee_delay[i] <= 0) {
        if (m->det_melee_tx[i] == 0.0 && m->det_melee_ty[i] == 0.0 && m->det_melee_tz[i] == 0.0)
            fire_repath = 1;
        else if (moved >= 1.0)
            fire_repath = 1;
        else if (jrand_float(pai_jr(m, i)) < 0.05f)
            fire_repath = 1;
    }
    if (fire_repath) {
        m->det_melee_tx[i] = px;
        m->det_melee_ty[i] = py;
        m->det_melee_tz[i] = pz;
        m->det_melee_delay[i] = 4 + jrand_int_bound(pai_jr(m, i), 7);
        if (d0 > 1024.0) m->det_melee_delay[i] += 10;
        else if (d0 > 256.0) m->det_melee_delay[i] += 5;
        if (!pai_find_path(m, w, s, i, px, py, pz))
            m->det_melee_delay[i] += 15;
        m->passive_nav_speed[i] = s->type[i] == EW_TYPE_SKELETON ? 1.2 : 1.0;
    }
    ++m->det_raise_arm[i];
}

static void hai_bow_update(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                           double px, double py, double pz) {
    int see;
    double d0;
    see = los_clear(w, s->x[i], s->y[i] + pai_eye_height(s->type[i]), s->z[i],
                    px, pai_player_eye_y(py), pz);
    d0 = (px - s->x[i]) * (px - s->x[i])
       + (py - s->y[i]) * (py - s->y[i])
       + (pz - s->z[i]) * (pz - s->z[i]);
    if (see) {
        if (m->det_see_time[i] < 0) m->det_see_time[i] = 0;
        ++m->det_see_time[i];
    } else {
        if (m->det_see_time[i] > 0) m->det_see_time[i] = 0;
        --m->det_see_time[i];
    }
    if (d0 <= 225.0 && m->det_see_time[i] >= 20) {
        hai_clear_nav(m, s, i);
        ++m->det_strafe_time[i];
    } else {
        (void)pai_find_path(m, w, s, i, px, py, pz);
        m->passive_nav_speed[i] = 1.0;
        m->det_strafe_time[i] = -1;
    }
    if (m->det_strafe_time[i] >= 20) {
        if ((double)jrand_float(pai_jr(m, i)) < 0.3)
            m->det_strafe_cw[i] = (unsigned char)!m->det_strafe_cw[i];
        if ((double)jrand_float(pai_jr(m, i)) < 0.3)
            m->det_strafe_back[i] = (unsigned char)!m->det_strafe_back[i];
        m->det_strafe_time[i] = 0;
    }
    if (m->det_strafe_time[i] > -1) {
        if (d0 > 225.0 * 0.75) m->det_strafe_back[i] = 0;
        else if (d0 < 225.0 * 0.25) m->det_strafe_back[i] = 1;
    }
    if (m->det_raise_arm[i] > 0) {
        if (!see && m->det_see_time[i] < -60) m->det_raise_arm[i] = 0;
        else if (see) {
            ++m->det_raise_arm[i];
            if (m->det_raise_arm[i] >= 20) {
                (void)jrand_float(pai_jr(m, i)); /* shoot playSound pitch */
                m->det_raise_arm[i] = 0;
                m->det_bow_attack_time[i] = 40;
            }
        }
    } else if (--m->det_bow_attack_time[i] <= 0 && m->det_see_time[i] >= -60) {
        m->det_raise_arm[i] = 1;
    }
}

static void hai_swell_update(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                             double px, double py, double pz) {
    double dx = px - s->x[i], dy = py - s->y[i], dz = pz - s->z[i];
    double d2 = dx * dx + dy * dy + dz * dz;
    int see = los_clear(w, s->x[i], s->y[i] + pai_eye_height(s->type[i]), s->z[i],
                        px, pai_player_eye_y(py), pz);
    if (!m->det_has_target[i] || d2 > 49.0 || !see) m->det_cstate[i] = -1;
    else m->det_cstate[i] = 1;
}

static void hai_target_tick(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                            double px, double py, double pz) {
    int type = s->type[i];
    int setup = (m->det_target_tick[i]++ % 3) == 0;
    unsigned using = m->det_target_tasks[i];
    int order[4];
    int n = 0, k;
    if (type == EW_TYPE_CREEPER) { order[n++] = 2; order[n++] = 1; }
    else {
        order[n++] = 1;
        order[n++] = 2;
        /* EntityZombie.initEntityAI: villager then iron golem, both pri 3.
         * AbstractSkeleton.initEntityAI: nearestGolem pri 3 (no villager).
         * Each NAT draws nextInt(10) on setup when mutex-free; empty AABB
         * still consumes the draw. Dropping golem desyncs summoned skeletons. */
        if (type == EW_TYPE_ZOMBIE) order[n++] = 4;
        if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON) order[n++] = 8;
    }
    for (k = 0; k < n; ++k) {
        unsigned bit = (unsigned)order[k];
        int is_using = (using & bit) != 0;
        int mutex_ok = 1;
        unsigned o;
        for (o = 1; o <= 8; o <<= 1) {
            if (o == bit) continue;
            if ((using & o) && (/* mutex 1 */ 1)) { mutex_ok = 0; break; }
        }
        if (!setup) {
            if (is_using) {
                int keep = 0;
                if (bit == HT_PLAYER)
                    keep = m->det_has_target[i] && hai_player_range(s, i, px, py, pz);
                else
                    keep = 0;
                if (!keep) {
                    using &= ~bit;
                    if (bit == HT_PLAYER) m->det_has_target[i] = 0;
                }
            }
            continue;
        }
        if (is_using) {
            int keep = (bit == HT_PLAYER)
                && m->det_has_target[i] && hai_player_range(s, i, px, py, pz);
            if (!keep) {
                using &= ~bit;
                if (bit == HT_PLAYER) m->det_has_target[i] = 0;
            }
        } else if (mutex_ok) {
            if (bit == HT_HURT) {
                /* no revenge in these tapes */
            } else {
                /* EntityAINearestAttackableTarget: nextInt(10) then search. */
                if (jrand_int_bound(pai_jr(m, i), 10) == 0) {
                    if (bit == HT_PLAYER && hai_player_range(s, i, px, py, pz)
                        && los_clear(w, s->x[i], s->y[i] + pai_eye_height(type), s->z[i],
                                     px, pai_player_eye_y(py), pz)) {
                        using |= HT_PLAYER;
                        m->det_has_target[i] = 1;
                    }
                    /* villager/golem AABB empty in these tapes */
                }
            }
        }
    }
    m->det_target_tasks[i] = using;
}

static void hai_look(GmMobLive *m, const EwStore *s, int i,
                     double px, double py, double pz) {
    unsigned t = m->passive_tasks[i];
    float yaw_d = 10.0f, pitch_d = 40.0f;
    int looking = 0;
    double lx = 0, ly = 0, lz = 0;
    /* EntityAICreeperSwell.updateTask only setCreeperState; no lookHelper. */
    if (t & (hai_bit(HMELEE) | hai_bit(HBOW))) {
        looking = 1;
        yaw_d = 30.0f; pitch_d = 30.0f;
        lx = px; ly = pai_player_eye_y(py); lz = pz;
    } else if (t & hai_bit(HWATCH)) {
        looking = 1;
        lx = px; ly = pai_player_eye_y(py); lz = pz;
    } else if (t & hai_bit(HIDLE)) {
        looking = 1;
        lx = s->x[i] + m->passive_idle_x[i];
        ly = s->y[i] + pai_eye_height(s->type[i]);
        lz = s->z[i] + m->passive_idle_z[i];
    }
    {
        float pitch = 0.0f;
        float head = m->passive_head_yaw[i];
        float body = m->passive_render_yaw[i];
        if (looking) {
            double dx = lx - s->x[i];
            double dy = ly - (s->y[i] + pai_eye_height(s->type[i]));
            double dz = lz - s->z[i];
            double horiz = (double)(float)sqrt(dx * dx + dz * dz);
            float target_yaw = pai_atan2_yaw(dz, dx);
            float target_pitch = -pai_deg(mc_atan2(dy, horiz));
            pitch = pai_update_rotation(0.0f, target_pitch, pitch_d);
            head = pai_update_rotation(head, target_yaw, yaw_d);
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
}

static void hai_living(GmMobLive *m, GmWorld *w, EwStore *s, int i, int day) {
    int type = s->type[i];
    int lst = m->living_sound_time[i];
    int sound_draw = jrand_int_bound(pai_jr(m, i), 1000);
    m->living_sound_time[i] = lst + 1;
    if (sound_draw < lst) {
        m->living_sound_time[i] = -80;
        if (type != EW_TYPE_CREEPER) {
            (void)jrand_float(pai_jr(m, i));
            (void)jrand_float(pai_jr(m, i));
        }
    }
    if (day && (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON)) {
        float f = pai_brightness(w, mc_floor(s->x[i]), mc_floor(s->y[i]), mc_floor(s->z[i]));
        if (f > 0.5f) {
            float nf = jrand_float(pai_jr(m, i));
            if (nf * 30.0f < (f - 0.4f) * 2.0f &&
                sky_exposed(w, s->x[i], s->y[i], s->z[i]))
                m->fire_ticks[i] = 160;
        }
    }
    /* PersistenceRequired: despawnEntity zeros age. Wander getAge stays 0. */
    m->entity_age[i] = 0;
}

static void hai_tick(GmMobLive *m, GmWorld *w, EwStore *s, int i,
                     double px, double py, double pz, int day,
                     int *moving, int *jump, int *wandering, int *swim_jump,
                     double *nav_speed) {
    int type = s->type[i];
    const int *goals = hai_goals(type, m->det_skel_melee[i]);
    int setup, g;
    if (type == EW_TYPE_CREEPER) {
        m->creeper_fuse[i] += (int)m->det_cstate[i];
        if (m->creeper_fuse[i] < 0) m->creeper_fuse[i] = 0;
        if (m->creeper_fuse[i] >= 30) {
            s->alive[i] = 0;
            s->type[i] = EW_TYPE_NONE;
            m->creeper_fuse[i] = 0;
            m->explosion_pending = 1;
            m->explosion_x = s->x[i];
            m->explosion_y = s->y[i] + 0.5;
            m->explosion_z = s->z[i];
            m->explosion_size = EXL_RADIUS;
            *moving = 0; *jump = 0; *wandering = 0; *swim_jump = 0; *nav_speed = 0.0;
            return;
        }
    }
    setup = (m->passive_task_tick[i]++ % 3) == 0;
    hai_target_tick(m, w, s, i, px, py, pz);
    for (g = 0; goals[g] >= 0; ++g) {
        int task = goals[g];
        int using_task = (m->passive_tasks[i] & hai_bit(task)) != 0;
        if (setup) {
            if (using_task) {
                if (!hai_can_use(m, type, i, task) ||
                    !hai_continue(m, w, s, i, task, px, py, pz))
                    hai_reset(m, s, i, task);
            } else if (hai_can_use(m, type, i, task)) {
                (void)hai_try_start(m, w, s, i, task, px, py, pz, day);
            }
        } else if (using_task && !hai_continue(m, w, s, i, task, px, py, pz)) {
            hai_reset(m, s, i, task);
        }
    }
    *swim_jump = 0;
    if (m->passive_tasks[i] & hai_bit(HSWIM)) {
        u64 stream = pai_rng_start(m, s, i, PAI_SWIM + 16);
        if (pai_float(m, i, &stream) < 0.8f) *swim_jump = 1;
    }
    if (m->passive_tasks[i] & hai_bit(HWATCH)) --m->passive_watch_time[i];
    if (m->passive_tasks[i] & hai_bit(HIDLE)) --m->passive_idle_time[i];
    if (m->passive_tasks[i] & hai_bit(HMELEE))
        hai_melee_update(m, w, s, i, px, py, pz);
    if (m->passive_tasks[i] & hai_bit(HBOW))
        hai_bow_update(m, w, s, i, px, py, pz);
    if (m->passive_tasks[i] & hai_bit(HSWELL))
        hai_swell_update(m, w, s, i, px, py, pz);
    /* PathNavigate.onUpdateNavigation: pathFollow iff canNavigate, then setMoveTo. */
    if (pai_det() && m->det_nav_n[i])
        pai_nav_update(m, w, s, i);
    else if (s->path_len[i] && pai_path_done(w, s, i)) s->path_len[i] = 0;
    *moving = s->path_len[i] != 0;
    if ((m->passive_tasks[i] & hai_bit(HBOW)) && m->det_strafe_time[i] > -1)
        *moving = 1;
    *wandering = *moving && (m->passive_tasks[i] & hai_bit(HWAND));
    *jump = 0;
    *nav_speed = *moving ? m->passive_nav_speed[i] : 0.0;
    if (m->passive_tasks[i] & hai_bit(HMELEE)) s->ai_state[i] = EW_AI_CHASE;
    else if (m->passive_tasks[i] & hai_bit(HBOW)) s->ai_state[i] = EW_AI_ATTACK;
    else if (m->passive_tasks[i] & hai_bit(HSWELL)) s->ai_state[i] = EW_AI_ATTACK;
    else s->ai_state[i] = EW_AI_IDLE;
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
    JavaRandom r;
    memset(m, 0, sizeof *m);
    ew_store_clear(&m->a); ew_store_clear(&m->b);
    m->seed = seed; m->next_id = 1; m->next_orb_id=1000;
    m->active_dimension = 0;
    m->boat_ride = -1;
    jrand_set(&r, seed);
    m->spawn_world_seed48 = r.seed;
    jrand_set(&r, seed ^ (long long)0x4D415448);
    m->spawn_math_seed48 = r.seed;
    jrand_set(&r, seed ^ (long long)0x5348464C);
    m->spawn_shuffle_seed48 = r.seed;
}

void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value){
    if(!m||value<=0)return;
    while (value > 0) {
        int amount = xl_xp_split(value);
        int eid;
        u64 h;
        double angle, speed, mx, mz;
        value -= amount;
        eid = m->next_orb_id;
        h = mc_hash64((u64)m->seed ^ (u64)eid);
        angle = (double)(h & 0xffffu) * (2.0 * MC_PI / 65536.0);
        speed = (double)((h >> 16) & 0xffffu) * (0.2 / 65535.0);
        mx = -sin(angle) * speed;
        mz = cos(angle) * speed;
        if (!xl_spawn(m->xp_orbs, GM_XP_ORBS, &m->next_orb_id, m->orb_dimension,
                      (signed char)m->active_dimension, x, y, z, amount,
                      mx, 0.2, mz))
            return;
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

int gm_mobs_spawn_tnt_primed(GmMobLive *m, double x, double y, double z,
                             int fuse) {
    EwStore *s;
    int slot;
    if (!m) return -1;
    s = now_store(m);
    slot = ew_store_spawn(s, (u8)EW_TYPE_TNT_PRIMED, m->next_id++, x, y, z, 0.0f);
    if (slot < 0) return -1;
    m->entity_dimension[slot] = (signed char)m->active_dimension;
    reset_slot_state_s(m, s, slot);
    m->creeper_fuse[slot] = fuse;
    s->vy[slot] = EXL_TNT_SPAWN_MY;
    s->on_ground[slot] = 0;
    m->det_box_on[slot] = 1;
    m->det_box[slot].minX = x - 0.49;
    m->det_box[slot].minY = y;
    m->det_box[slot].minZ = z - 0.49;
    m->det_box[slot].maxX = x + 0.49;
    m->det_box[slot].maxY = y + (double)EXL_TNT_HEIGHT;
    m->det_box[slot].maxZ = z + 0.49;
    ew_store_copy(next_store(m), s);
    return slot;
}

/* java.util.Random.nextGaussian (Box-Muller). Spare unused: applied from
 * seed48_init, not the live cursor. */
static double pai_jrand_gaussian(JavaRandom *r) {
    double v1, v2, s, m;
    do {
        v1 = 2.0 * jrand_double(r) - 1.0;
        v2 = 2.0 * jrand_double(r) - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    m = sqrt(-2.0 * log(s) / s);
    return v1 * m;
}

int gm_mobs_det_place(GmMobLive *m, int eid, int type,
                      double x, double y, double z, float yaw, float pitch, float head_yaw,
                      unsigned long long seed48, int living_sound, int entity_age, int task_tick,
                      unsigned tasks, int watch, int idle, double idle_x, double idle_z,
                      int eat, int egg, int on_ground, float render_yaw, float prev_head_yaw,
                      int body_ticks, unsigned long long seed48_init) {
    int slot;
    EwStore *s;
    if (!m || !(pai_det_ai(type) || hai_ok(type))) return -1;
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
    /* FOLLOW_RANGE: species setBaseValue. EntityLiving.onInitialSpawn then
     * applyModifier("Random spawn bonus", nextGaussian()*0.05, op=1).
     * CommandSummon with NBT (istore 15 / ifne skip) does not call it, and
     * that draw is not on the live cursor (mixin reseeds at ctor RETURN;
     * living ctors use Math.random). Det tapes: worldgen passives got the
     * bonus; /summon NBT hostiles and persist sheep did not. Apply from
     * seed48_init without touching the live cursor. */
    m->det_follow[slot] = pai_follow_range(type);
    if (seed48_init && (type == EW_TYPE_SHEEP || type == EW_TYPE_PIG ||
                        type == EW_TYPE_COW || type == EW_TYPE_CHICKEN)) {
        JavaRandom jr;
        double g;
        jr.seed = seed48_init & MC_JR_MASK;
        g = pai_jrand_gaussian(&jr);
        m->det_follow[slot] = (float)((double)m->det_follow[slot] * (1.0 + g * 0.05));
    }
    /* AbstractSkeleton.<init> calls setCombatTask with an empty hand -> melee.
     * onInitialSpawn (skipped here) would equip a bow and switch. Tape bow
     * bit 256 is the only override. Anonymous AbstractSkeleton$1 is melee
     * even when the recorder writes tasks=0 (getSimpleName empty). */
    m->det_skel_melee[slot] = (type == EW_TYPE_SKELETON && (tasks & 256u) == 0) ? 1 : 0;
    /* Entity.setPosition: float f = width/2.0F then f2d. Persist so later
     * ticks do not rebuild AABB from pos ± width/2 (1 ULP vs Entity.move). */
    {
        float bw, bh, hf;
        pai_size(type, &bw, &bh);
        hf = bw / 2.0f;
        m->det_box[slot] = mc_aabb_make(x - (double)hf, y, z - (double)hf,
                                        x + (double)hf, y + (double)bh, z + (double)hf);
        m->det_box_on[slot] = 1;
    }
    if (eid >= m->next_id) m->next_id = eid + 1;
    if (type == EW_TYPE_BLAZE || type == EW_TYPE_PIGMAN || type == EW_TYPE_ENDERMAN)
        m->det_persist[slot] = 1;
    ew_store_copy(next_store(m), s);
    return slot;
}

void gm_mobs_det_hydrate_hostile(GmMobLive *m, int slot,
                                int ttt, unsigned ttasks, int tgt, int fuse, int mdelay,
                                int see, int stime, int atime, int scw, int sback, int cstate) {
    if (!m || slot < 0 || slot >= EW_MAX_ENTITIES) return;
    m->det_target_tick[slot] = ttt;
    m->det_target_tasks[slot] = ttasks;
    m->det_has_target[slot] = tgt ? 1 : 0;
    m->creeper_fuse[slot] = fuse;
    m->det_melee_delay[slot] = mdelay;
    m->det_see_time[slot] = see;
    m->det_strafe_time[slot] = stime;
    m->det_bow_attack_time[slot] = atime;
    m->det_strafe_cw[slot] = scw ? 1 : 0;
    m->det_strafe_back[slot] = sback ? 1 : 0;
    m->det_cstate[slot] = (signed char)cstate;
    if (now_store(m)->type[slot] == EW_TYPE_SKELETON && (stime >= 0 || atime >= 0))
        m->det_skel_melee[slot] = 0;
}

void gm_mobs_det_rng_extra(GmMobLive *m, int slot, int have_gauss, double gauss,
                           int height_off_time, float height_off, int persist, int anger) {
    if (!m || slot < 0 || slot >= EW_MAX_ENTITIES) return;
    m->ent_jr_have_gauss[slot] = have_gauss ? 1 : 0;
    m->ent_jr_gauss[slot] = gauss;
    m->blaze_hot[slot] = height_off_time;
    m->blaze_hof[slot] = height_off;
    if (persist) {
        m->det_persist[slot] = 1;
        /* /summon {PersistenceRequired} skips onInitialSpawn. Undo the
         * worldgen-passive FOLLOW_RANGE bonus if det_place applied one. */
        if (now_store(m)->type[slot] == EW_TYPE_SHEEP ||
            now_store(m)->type[slot] == EW_TYPE_PIG ||
            now_store(m)->type[slot] == EW_TYPE_COW ||
            now_store(m)->type[slot] == EW_TYPE_CHICKEN)
            m->det_follow[slot] = pai_follow_range(now_store(m)->type[slot]);
    }
    if (anger > 0) m->anger[slot] = anger;
}

int gm_mobs_place_boat(GmMobLive *m, double x, double y, double z, float yaw) {
    int slot = gm_mobs_spawn(m, EW_TYPE_BOAT, x, y, z);
    if (slot < 0) return -1;
    now_store(m)->yaw[slot] = yaw;
    next_store(m)->yaw[slot] = yaw;
    if (slot >= 0 && slot < EW_MAX_ENTITIES) {
        m->boat_delta_rot[slot] = 0.0f;
        m->boat_glide[slot] = 0.8f;
    }
    return slot;
}

int gm_mobs_boat_riding(const GmMobLive *m) {
    return m && m->boat_ride >= 0;
}

int gm_mobs_boat_status(const GmMobLive *m, struct GmWorld *w, int slot) {
    const EwStore *s;
    if (!m || !w || slot < 1 || slot >= EW_MAX_ENTITIES) return BL_STATUS_IN_AIR;
    s = const_store(m);
    if (!s->alive[slot] || s->type[slot] != EW_TYPE_BOAT)
        return BL_STATUS_IN_AIR;
    return bl_world_status(w, s->x[slot], s->y[slot], s->z[slot]);
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

/* EntitySlime.setDead EntitySlime.java:217-247. Table cap is the shared
 * magma/blaze cap: skip remaining inserts, still consume yaw draws. */
static void slime_split(GmMobLive *m, EwStore *s, int i, JavaRandom *er) {
    int sz = m->size[i];
    int child, n, k;
    if (sz <= 1 || !er) return;
    child = sz / 2;
    n = ml_slime_split_n(er);
    for (k = 0; k < n; ++k) {
        float ox, oz, yaw;
        int slot;
        ml_slime_split_off(k, sz, &ox, &oz);
        yaw = jrand_float(er) * 360.0f;
        slot = ew_store_spawn(s, s->type[i], m->next_id++,
                              s->x[i] + (double)ox, s->y[i] + 0.5, s->z[i] + (double)oz,
                              max_health(s->type[i], child));
        if (slot < 0) continue;
        m->entity_dimension[slot]=m->entity_dimension[i];
        m->size[slot] = (unsigned char)child;
        m->creeper_fuse[slot] = child;
        s->yaw[slot] = yaw;
        reset_slot_state_s(m, s, slot);
        m->size[slot] = (unsigned char)child;
        m->creeper_fuse[slot] = child;
    }
}

/* EntityLivingBase.onDeath EntityLivingBase.java:1224-1271 dropLoot.
 * XP / EntitySlime.setDead split wait for deathTime==20. */
static void mob_on_death(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    int item = 0, count = 1;
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
    case EW_TYPE_SPIDER: {
        JavaRandom er;
        MlDrop pd[2];
        int nd, di;
        er.seed = m->ent_jr_seed[i];
        nd = ml_spider_drop(&er, 1, pd, 2);
        m->ent_jr_seed[i] = er.seed;
        for (di = 0; di < nd; ++di)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i],
                               pd[di].item, pd[di].count, pd[di].meta, 10);
        item = 0;
        break;
    }
    case EW_TYPE_ENDERMAN: {
        JavaRandom er;
        MlDrop pd[1];
        int nd;
        er.seed = m->ent_jr_seed[i];
        nd = ml_enderman_drop(&er, pd, 1);
        m->ent_jr_seed[i] = er.seed;
        if (nd > 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i],
                               pd[0].item, pd[0].count, pd[0].meta, 10);
        item = 0;
        break;
    }
    case EW_TYPE_WITCH: {
        JavaRandom er;
        MlDrop pd[7];
        int nd, di;
        er.seed = m->ent_jr_seed[i];
        nd = ml_witch_drop(&er, pd, 7);
        m->ent_jr_seed[i] = er.seed;
        for (di = 0; di < nd; ++di)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i],
                               pd[di].item, pd[di].count, pd[di].meta, 10);
        item = 0;
        break;
    }
    case EW_TYPE_BLAZE:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 369;
        break;
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
        break;
    case EW_TYPE_SLIME: {
        JavaRandom er;
        MlDrop pd[1];
        int nd, sz;
        er.seed = m->ent_jr_seed[i];
        sz = m->size[i] > 0 ? (int)m->size[i] : 1;
        nd = ml_slime_drop(&er, sz, pd, 1);
        m->ent_jr_seed[i] = er.seed;
        if (nd > 0)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i],
                               pd[0].item, pd[0].count, pd[0].meta, 10);
        item = 0;
        break;
    }
    case EW_TYPE_SILVERFISH:
        item = 0; break;
    case EW_TYPE_SHEEP:
    case EW_TYPE_PIG:
    case EW_TYPE_COW:
    case EW_TYPE_CHICKEN: {
        JavaRandom er;
        PlDrop pd[4];
        int nd, di;
        er.seed = m->ent_jr_seed[i];
        nd = pl_drop_few(type, pl_sheep_sheared(m->creeper_fuse[i]),
                         pl_sheep_color(m->creeper_fuse[i]), &er, pd, 4);
        m->ent_jr_seed[i] = er.seed;
        for (di = 0; di < nd; ++di)
            gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i],
                               pd[di].item, pd[di].count, pd[di].meta, 10);
        item = 0;
        break;
    }
    case EW_TYPE_BOAT:
        item = 333; break;
    default: break;
    }
    if (item) gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], item, count, 0, 10);
}

/* EntityLivingBase.onDeathUpdate deathTime==20 then setDead.
 * EntitySlime.setDead split EntitySlime.java:217-247. */
static void mob_finish_dead(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    int xp = 5;
    int type = s->type[i];
    (void)drops;
    switch (type) {
    case EW_TYPE_MAGMA:
        xp = m->size[i];
        {
            JavaRandom er;
            er.seed = m->ent_jr_seed[i];
            slime_split(m, s, i, &er);
            m->ent_jr_seed[i] = er.seed;
        }
        break;
    case EW_TYPE_SLIME: {
        JavaRandom er;
        int sz = m->size[i] > 0 ? (int)m->size[i] : 1;
        er.seed = m->ent_jr_seed[i];
        xp = sz;
        if (sz > 1) slime_split(m, s, i, &er);
        m->ent_jr_seed[i] = er.seed;
        break;
    }
    case EW_TYPE_SHEEP:
    case EW_TYPE_PIG:
    case EW_TYPE_COW:
    case EW_TYPE_CHICKEN: {
        JavaRandom er;
        er.seed = m->ent_jr_seed[i];
        xp = pl_xp_points(&er);
        m->ent_jr_seed[i] = er.seed;
        break;
    }
    case EW_TYPE_BOAT:
        xp = 0; break;
    case EW_TYPE_BLAZE:
        xp = 10; break;
    default:
        xp = 5; break;
    }
    if (xp > 0) gm_mobs_spawn_xp(m, s->x[i], s->y[i] + 0.25, s->z[i], xp);
    if (m->boat_ride == i) m->boat_ride = -1;
    s->alive[i] = 0;
    s->type[i] = EW_TYPE_NONE;
}

static void mob_drop(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    mob_on_death(m, s, i, drops);
    mob_finish_dead(m, s, i, drops);
}

/* EntityLivingBase.onDeath then onDeathUpdate ++deathTime; setDead at 20. */
static int tick_corpse(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    if (!s || !s->alive[i] || s->health[i] > 0.0f) return 0;
    if (s->type[i] == EW_TYPE_BOAT) return 0;
    if (m->death_time[i] == 0)
        mob_on_death(m, s, i, drops);
    ++m->death_time[i];
    if (m->death_time[i] >= 20)
        mob_finish_dead(m, s, i, drops);
    return 1;
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
    } else if (!pai_det()) {
        /* Generic path: 1.11.2 setBeenAttacked is velocityChanged=true
         * with no rand (Entity.java:1666-1668). knockBack consumes
         * entity.rand.nextDouble vs KR=0 (EntityLivingBase.java:1298). */
        PsvPlayer *pl = (PsvPlayer *)p;
        double xRatio = (pl->ent.posX + ox) - s->x[best];
        double zRatio = (pl->ent.posZ + oz) - s->z[best];
        JavaRandom jr;
        int kb_i = 0;
        jr.seed = m->ent_jr_seed[best];
        if (xRatio * xRatio + zRatio * zRatio >= 1.0e-4) {
            (void)jrand_double(&jr); /* KR default 0, EntityLivingBase.java:1298 */
            ml_knockback(&s->vx[best], &s->vy[best], &s->vz[best],
                         s->on_ground[best], 0.4f, xRatio, zRatio);
        }
        /* EntityPlayer.attackTargetEntityWithCurrentItem
         * (EntityPlayer.java:1366-1432): knockback enchant + sprint. */
        if (pl->sprinting) ++kb_i;
        if (kb_i > 0) {
            (void)jrand_double(&jr);
            ml_knockback_yaw(&s->vx[best], &s->vy[best], &s->vz[best],
                             s->on_ground[best], (float)kb_i * 0.5f,
                             pl->yaw);
            pl->ent.motionX *= 0.6;
            pl->ent.motionZ *= 0.6;
            pl->sprinting = 0;
        }
        m->ent_jr_seed[best] = jr.seed;
    }
    damage_held_weapon((PsvPlayer *)p);
    m->player_attack_cooldown = 10;
    if (s->type[best] == EW_TYPE_ENDERMAN) {
        /* EntityAIHurtByTarget: setAttackTarget -> screaming + targetChangeTime. */
        m->screaming[best] = 1;
        m->target_change_time[best] = m->ticks_existed[best];
        m->det_has_target[best] = 1;
    }
    if (s->health[best] <= 0.0f) s->health[best] = 0.0f;
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
    if ((gm_passive(s->type[i]) ||
         (pai_det() && (pai_det_ai(s->type[i]) || hai_ok(s->type[i])))) && moving) {
        if (pai_det()) {
            double ddx = s->path_tx[i] - s->x[i];
            double ddy = s->path_ty[i] - s->y[i];
            double ddz = s->path_tz[i] - s->z[i];
            /* EntityMoveHelper MOVE_TO: d3 = dx^2+dy^2+dz^2; ldc2_w 2.500000277905201E-7. */
            if (ddx * ddx + ddy * ddy + ddz * ddz >= 2.500000277905201e-7) {
                intent.yaw = pai_limit_angle(s->yaw[i], pai_atan2_yaw(ddz, ddx), 90.0f);
            } else {
                /* setMoveForward(0); return; yaw unchanged. */
                moving = 0;
                intent.yaw = s->yaw[i];
            }
        } else {
            intent.yaw = pai_update_rotation(s->yaw[i], intent.yaw, 90.0f);
        }
    }
    if (moving && jump) intent.isJumping = 1;
    ehs_load_living(&liv, s, i, &intent);
    /* Override sizes not represented exactly by the shared hostile spine. */
    if (gm_is_slimey(s->type[i]) || gm_passive(s->type[i]) ||
        (pai_det() && (hai_ok(s->type[i]) || pai_det_ai(s->type[i])))) {
        float w, h;
        if (gm_passive(s->type[i]) || hai_ok(s->type[i]) ||
            (pai_det() && pai_det_ai(s->type[i])))
            pai_size(s->type[i], &w, &h);
        else ehs_size_scaled(s->type[i], m->size[i], &w, &h);
        liv.base.width = w; liv.base.height = h;
        liv.base.phys.box = mc_aabb_make(s->x[i] - w * 0.5, s->y[i], s->z[i] - w * 0.5,
                                         s->x[i] + w * 0.5, s->y[i] + h, s->z[i] + w * 0.5);
    }
    /* Entity.move keeps the swept AABB. Rebuild from pos is 1 ULP vs Java. */
    if (pai_det() && m->det_box_on[i])
        liv.base.phys.box = m->det_box[i];
    if (gm_passive(s->type[i]) ||
        (pai_det() && (pai_det_ai(s->type[i]) || hai_ok(s->type[i])))) {
        /* EntityMoveHelper MOVE_TO:
         *   setAIMoveSpeed((float)(navigatorSpeed * MOVEMENT_SPEED attr));
         * EntityLiving.setAIMoveSpeed writes that same value to moveForward.
         * EntityLivingBase then damps moveForward by 0.98 before travel. */
        float ai_speed = (float)(nav_speed * pai_attribute_speed(s->type[i]));
        liv.landMovementFactor = ai_speed;
        liv.moveForward = moving ? ai_speed : 0.0f;
        liv.moveStrafing = 0.0f;
        if (pai_det() && hai_ok(s->type[i]) &&
            (m->passive_tasks[i] & 256u) && m->det_strafe_time[i] > -1) {
            liv.moveForward = (m->det_strafe_back[i] ? -0.5f : 0.5f) * ai_speed;
            liv.moveStrafing = (m->det_strafe_cw[i] ? 0.5f : -0.5f) * ai_speed;
        }
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
     * heightOffset gaussian is consumed in pai_tick. Hover motionY lift needs
     * an attack target; ambient det tapes stay on fall damping. */
    if (s->type[i] == EW_TYPE_BLAZE && !liv.base.phys.onGround &&
        liv.base.phys.motionY < 0.0)
        liv.base.phys.motionY *= 0.6;

    /* EntityAISwimming only requests a jump; the actual water/lava travel is
     * EntityLivingBase's fluid branch. Keep it here so a passive does not run
     * the land gravity branch while submerged. */
    if ((gm_passive(s->type[i]) || (pai_det() && hai_ok(s->type[i]))) &&
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
        if (pai_det()) {
            m->det_box[i] = liv.base.phys.box;
            m->det_box_on[i] = 1;
        }
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
    if (pai_det()) {
        m->det_box[i] = liv.base.phys.box;
        m->det_box_on[i] = 1;
    }
}

static int pai_det_living(int type) {
    return pai_det_ai(type) || hai_ok(type);
}

static void pai_living_aabb(const GmMobLive *m, const EwStore *s, int i, McAABB *out) {
    if (m->det_box_on[i]) {
        *out = m->det_box[i];
        return;
    }
    float w, h;
    pai_size(s->type[i], &w, &h);
    *out = mc_aabb_make(s->x[i] - (double)w * 0.5, s->y[i], s->z[i] - (double)w * 0.5,
                        s->x[i] + (double)w * 0.5, s->y[i] + (double)h,
                        s->z[i] + (double)w * 0.5);
}

/* Entity.applyEntityCollision: d0 = other.x - this.x, this += -d0, other += d0
 * after absMax / MathHelper.sqrt / 0.05 scale. entityCollisionReduction=0. */
static void pai_apply_collision_vel(double ax, double az, double bx, double bz,
                                    double *avx, double *avz, double *bvx, double *bvz) {
    double d0 = bx - ax;
    double d1 = bz - az;
    double ad0 = d0 < 0.0 ? -d0 : d0;
    double ad1 = d1 < 0.0 ? -d1 : d1;
    double d2 = ad0 > ad1 ? ad0 : ad1;
    double d3;
    if (d2 < 0.009999999776482582) return;
    d2 = (double)(float)sqrt(d2);
    d0 /= d2;
    d1 /= d2;
    d3 = 1.0 / d2;
    if (d3 > 1.0) d3 = 1.0;
    d0 *= d3;
    d1 *= d3;
    d0 *= 0.05000000074505806;
    d1 *= 0.05000000074505806;
    if (avx) {
        *avx -= d0;
        *avz -= d1;
    }
    if (bvx) {
        *bvx += d0;
        *bvz += d1;
    }
}

/* EntityLivingBase.collideWithNearbyEntities after travel. */
static void pai_collide_nearby(GmMobLive *m, EwStore *s, int i,
                               const McAABB *player_bb, double px, double pz) {
    McAABB self;
    int j;
    if (!s->alive[i] || !pai_det_living(s->type[i])) return;
    pai_living_aabb(m, s, i, &self);
    for (j = 1; j < EW_MAX_ENTITIES; ++j) {
        McAABB other;
        if (j == i || !s->alive[j] || !pai_det_living(s->type[j])) continue;
        if (m->entity_dimension[j] != m->entity_dimension[i]) continue;
        pai_living_aabb(m, s, j, &other);
        if (!mc_aabb_intersects(&self, &other)) continue;
        pai_apply_collision_vel(s->x[i], s->z[i], s->x[j], s->z[j],
                                &s->vx[i], &s->vz[i], &s->vx[j], &s->vz[j]);
    }
    if (player_bb && mc_aabb_intersects(&self, player_bb))
        pai_apply_collision_vel(s->x[i], s->z[i], px, pz,
                                &s->vx[i], &s->vz[i], NULL, NULL);
}

/* Player collideWithNearbyEntities (tickPlayers, before entity list). */
static void pai_player_collide_mobs(GmMobLive *m, EwStore *s,
                                    const McAABB *player_bb, double px, double pz) {
    int i;
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        McAABB mob;
        if (!s->alive[i] || !pai_det_living(s->type[i])) continue;
        pai_living_aabb(m, s, i, &mob);
        if (!mc_aabb_intersects(player_bb, &mob)) continue;
        pai_apply_collision_vel(px, pz, s->x[i], s->z[i],
                                NULL, NULL, &s->vx[i], &s->vz[i]);
    }
}

static int alive_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for (int i=1;i<EW_MAX_ENTITIES;++i)
        if (s->alive[i] && s->health[i] > 0.0f &&
            m->entity_dimension[i]==m->active_dimension &&
            gm_hostile(s->type[i])) ++n;
    return n;
}
static int living_count(const GmMobLive *m,const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&s->health[i]>0.0f&&
           m->entity_dimension[i]==m->active_dimension&&
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
    XlPlayer xp;
    xp.xpCooldown=p->xpCooldown; xp.experience=p->experience;
    xp.experienceLevel=p->experienceLevel; xp.experienceTotal=p->experienceTotal;
    xl_player_tick(&xp);
    for(int i=0;i<GM_XP_ORBS;++i){McOrb *o=&m->xp_orbs[i];
        if(o->dead||o->xpValue<=0||m->orb_dimension[i]!=m->active_dimension)continue;
        McAABB q=mc_aabb_addcoord(&o->box,o->motionX,o->motionY,o->motionZ),blocks[64];
        int nb=collect_orb_blocks(w,&q,blocks,64);
        int ux=mc_floor(o->posX),uy=mc_floor(o->box.minY)-1,uz=mc_floor(o->posZ);
        if(uy<0)uy=0;
        u16 under=mc_state(gm_world_block(w,ux,uy,uz),gm_world_meta(w,ux,uy,uz));
        eo_tick(o,p->ent.posX+ox,p->ent.posY,p->ent.posZ+oz,
                (float)psv_player_eye_height(p),0, blocks,nb,under,0);
        if(xl_try_pickup(o,&xp,&player)) m->xp_pickups++;
    }
    p->xpCooldown=xp.xpCooldown; p->experience=xp.experience;
    p->experienceLevel=xp.experienceLevel; p->experienceTotal=xp.experienceTotal;
    m->xp_total=p->experienceTotal;
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

static int gm_hs_in_clip(const GmHsWorld *h, int x, int y, int z) {
    const GmMobLive *m;
    if (!h || !h->m || !h->m->spawn_clip) return 1;
    m = h->m;
    if (x < m->spawn_rx0 || x >= m->spawn_rx0 + m->spawn_rnx) return 0;
    if (y < m->spawn_ry0 || y >= m->spawn_ry0 + m->spawn_rny) return 0;
    if (z < m->spawn_rz0 || z >= m->spawn_rz0 + m->spawn_rnz) return 0;
    return 1;
}
static int gm_hs_block(const GmHsWorld *h, int x, int y, int z) {
    if (!h || !h->w) return 0;
    if (!gm_hs_in_clip(h, x, y, z)) return 0;
    return gm_world_block(h->w, x, y, z);
}
static int gm_hs_sky(const GmHsWorld *h, int x, int y, int z) {
    if (!h || !h->w) return 15;
    if (!gm_hs_in_clip(h, x, y, z)) return 15;
    return gm_world_sky_light(h->w, x, y, z);
}
static int gm_hs_blk(const GmHsWorld *h, int x, int y, int z) {
    if (!h || !h->w) return 0;
    if (!gm_hs_in_clip(h, x, y, z)) return 0;
    return gm_world_block_light(h->w, x, y, z);
}

static int gm_hs_count(const GmHsWorld *h) {
    const EwStore *s;
    int i, n = 0;
    if (!h || !h->s) return 0;
    s = h->s;
    for (i = 1; i < EW_MAX_ENTITIES; ++i)
        if (s->alive[i] && ehs_is_hostile(s->type[i])) ++n;
    return n;
}

static int gm_ps_count(const GmHsWorld *h) {
    const EwStore *s;
    int i, n = 0;
    if (!h || !h->s) return 0;
    s = h->s;
    for (i = 1; i < EW_MAX_ENTITIES; ++i)
        if (s->alive[i] && ehs_is_passive(s->type[i])) ++n;
    return n;
}

static int gm_hs_hit(const GmHsWorld *h, double x0, double y0, double z0,
                     double x1, double y1, double z1) {
    const EwStore *s;
    int i;
    if (!h || !h->s) return 0;
    s = h->s;
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        float w, ht;
        double mx0, my0, mz0, mx1, my1, mz1;
        if (!s->alive[i] || !gm_living(s->type[i])) continue;
        ehs_size(s->type[i], &w, &ht);
        mx0 = s->x[i] - (double)w * 0.5;
        my0 = s->y[i];
        mz0 = s->z[i] - (double)w * 0.5;
        mx1 = s->x[i] + (double)w * 0.5;
        my1 = s->y[i] + (double)ht;
        mz1 = s->z[i] + (double)w * 0.5;
        if (x0 < mx1 && x1 > mx0 && y0 < my1 && y1 > my0 && z0 < mz1 && z1 > mz0)
            return 1;
    }
    return 0;
}

static int gm_hs_place(GmHsWorld *h, int type, double x, double y, double z,
                       float yaw, unsigned long long seed48, int have_g,
                       double g, int extra) {
    EwStore *s;
    GmMobLive *m;
    int slot;
    if (!h || !h->m || !h->s) return 0;
    m = h->m;
    s = h->s;
    slot = ew_store_spawn(s, (u8)type, m->next_id++, x, y, z,
                          ehs_max_health_of((u8)type, extra));
    if (slot < 0) return 0;
    m->entity_dimension[slot] = (signed char)m->active_dimension;
    reset_slot_state_s(m, s, slot);
    if (type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) {
        int sz = extra > 0 ? extra : 2;
        m->size[slot] = (unsigned char)sz;
        m->creeper_fuse[slot] = sz;
        s->health[slot] = ehs_max_health_of((u8)type, sz);
    }
    s->yaw[slot] = yaw;
    s->cx[slot] = mc_floor(x) >> 4;
    s->cz[slot] = mc_floor(z) >> 4;
    m->ent_jr_seed[slot] = seed48;
    m->ent_jr_have_gauss[slot] = (unsigned char)(have_g ? 1 : 0);
    m->ent_jr_gauss[slot] = g;
    m->entity_age[slot] = 0;
    m->det_persist[slot] = 0;
    m->despawn_ticks[slot] = 0;
    m->det_strafe_time[slot] = 0;
    m->det_bow_attack_time[slot] = 0;
    m->det_target_tasks[slot] = 0;
    m->det_see_time[slot] = 0;
    m->det_melee_delay[slot] = 0;
    m->hurt_time[slot] = 0;
    m->death_time[slot] = 0;
    {
        float bw, bh, hf;
        ehs_size((u8)type, &bw, &bh);
        hf = bw / 2.0f;
        m->det_box[slot] = mc_aabb_make(x - (double)hf, y, z - (double)hf,
                                        x + (double)hf, y + (double)bh, z + (double)hf);
        m->det_box_on[slot] = 1;
    }
    if (type == EW_TYPE_SHEEP)
        m->creeper_fuse[slot] = extra; /* packed fleece + sheared */
    if (type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) {
        float bw, bh, hf;
        int sz = extra > 0 ? extra : 2;
        ehs_size_scaled((u8)type, sz, &bw, &bh);
        hf = bw / 2.0f;
        m->det_box[slot] = mc_aabb_make(x - (double)hf, y, z - (double)hf,
                                        x + (double)hf, y + (double)bh, z + (double)hf);
        m->det_box_on[slot] = 1;
    }
    (void)extra;
    return 1;
}

static void gm_hs_run(GmMobLive *m, GmWorld *w, EwStore *s,
                      double px, double py, double pz, long long world_time) {
    GmHsWorld hw;
    HsState st;
    if (!m || !w || !s) return;
    /* PlayerChunkMap sent chunks in Java (WorldEntitySpawner.java:67-71).
     * Interactive play generates the radius-8 disk. Snapshot lockstep clips
     * to the region AABB and must not grow the world. */
    if (!m->spawn_clip)
        gm_world_ensure(w, mc_floor(px) >> 4, mc_floor(pz) >> 4, 8);
    hw.w = w;
    hw.m = m;
    hw.s = s;
    memset(&st, 0, sizeof st);
    st.world_rand.seed = m->spawn_world_seed48;
    st.math_rand.seed = m->spawn_math_seed48;
    st.shuffle_rand.seed = m->spawn_shuffle_seed48;
    st.seed = m->seed;
    st.world_time = world_time;
    st.difficulty = 2;
    st.thundering = 0;
    st.spawn_x = 0.0;
    st.spawn_y = 64.0;
    st.spawn_z = 0.0;
    if (m->natural_spawn)
        hs_find_chunks_for_spawning(&hw, &st, px, py, pz);
    /* WorldServer.java:206 spawnOnSetTickRate = totalTime % 400L == 0. */
    if (m->natural_spawn_passive && (world_time % 400LL) == 0LL)
        hs_find_chunks_for_creatures(&hw, &st, px, py, pz);
    m->spawn_world_seed48 = st.world_rand.seed;
    m->spawn_math_seed48 = st.math_rand.seed;
    m->spawn_shuffle_seed48 = st.shuffle_rand.seed;
}

static void natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz, int dimension, long long world_time) {
    discover_spawners(m, w, px, py, pz, dimension);
    tick_spawners(m, w, s, px, py, pz);

    if (!m->natural_spawn && !m->natural_spawn_passive) return;

    if (dimension == -1) {
        if (m->natural_spawn)
            nether_natural_spawn(m, w, s, px, py, pz);
        return;
    }
    if (dimension != 0) return;

    /* WorldServer.tick mobSpawner then updateEntities. Magma calls this at
     * the start of gm_mobs_tick (entity phase). World.rand is the isolated
     * spawn stream in spawn_*_seed48, not ww.rand. */
    gm_hs_run(m, w, s, px, py, pz, world_time);
}

/* Status: 0 IN_WATER, 1 ON_LAND, 2 IN_AIR (subset of EntityBoat.Status). */
static void tick_boat(GmMobLive *m, GmWorld *w, EwStore *nx, int i,
                      PsvPlayer *p, int ox, int oz, float forward, float strafe) {
    BlBoat b;
    int status, ridden = (m->boat_ride == i) && p;
    b.x = nx->x[i]; b.y = nx->y[i]; b.z = nx->z[i];
    b.vx = nx->vx[i]; b.vy = nx->vy[i]; b.vz = nx->vz[i];
    b.yaw = nx->yaw[i];
    b.on_ground = nx->on_ground[i];
    b.delta_rot = m->boat_delta_rot[i];
    b.glide = m->boat_glide[i];
    status = bl_tick_world(&b, w, ridden, forward, strafe);
    nx->x[i] = b.x; nx->y[i] = b.y; nx->z[i] = b.z;
    nx->vx[i] = b.vx; nx->vy[i] = b.vy; nx->vz[i] = b.vz;
    nx->yaw[i] = b.yaw;
    nx->on_ground[i] = (u8)(b.on_ground ? 1 : 0);
    m->boat_delta_rot[i] = b.delta_rot;
    m->boat_glide[i] = b.glide;
    if (ridden) {
        p->yaw = nx->yaw[i];
        p->ent.posX = nx->x[i] - ox;
        p->ent.posY = nx->y[i] + BL_RIDE_Y;
        p->ent.posZ = nx->z[i] - oz;
        p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
        p->ent.onGround = (status == BL_STATUS_ON_LAND);
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
    EwStore *now=now_store(m), *nx=next_store(m);
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
    /* Tape pl is client EntityPlayerSP after ServerTick END. lookHelper
     * samples server EntityPlayerMP (EntityLookHelper.java:31-42, WatchClosest
     * .java:98). t=29 pitch sign-flips if lookHelper uses this tick's pl;
     * previous pl matches t=34..41. t=42 tape pitch is look of pl t=40
     * (no lag 0/1/2 fits t=29..49; recorder-gap, not interpolation). Path dest
     * uses the same clock (EntityAIAttackMelee.java:114 look then tryMoveTo). */
    double lx = m->look_have ? m->look_px : px;
    double ly = m->look_have ? m->look_py : py;
    double lz = m->look_have ? m->look_pz : pz;
    McAABB player_bb = p->ent.box;
    player_bb.minX += (double)ox;
    player_bb.maxX += (double)ox;
    player_bb.minZ += (double)oz;
    player_bb.maxZ += (double)oz;
    if (!pai_det())
        natural_spawn(m,w,now,px,py,pz,dimension,world_time);
    ew_store_copy(nx,now);
    if (pai_det())
        pai_player_collide_mobs(m, nx, &player_bb, px, pz);
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
        if (tick_corpse(m, nx, i, drops)) continue;
        int hostile=gm_hostile(type), passive=gm_passive(type);
        /* AIFireballAttack owns its attackTime countdown only while its task
         * executes. Other mobs keep the EntityLiving-style global cooldown. */
        if(type!=EW_TYPE_BLAZE&&nx->attack_time[i]>0)--nx->attack_time[i];
        if (type == EW_TYPE_PIGMAN && m->anger[i] > 0) --m->anger[i];
        double dx=px-now->x[i],dy=py-now->y[i],dz=pz-now->z[i];
        double d=sqrt(dx*dx+dy*dy+dz*dz), xz=sqrt(dx*dx+dz*dz);
        if(hostile && !(hai_ok(type) && !pai_det())){
            /* PersistenceRequired hostiles on the det path skip the 32/128
             * despawn clock (Java despawnEntity zeros age and never nextInt).
             * Generic hai_ok path uses ml_hostile_pre (EntityLiving.despawnEntity). */
            if(!(pai_det() && (hai_ok(type) || m->det_persist[i]))){
                if(d>GM_MOB_DESPAWN_HARD){nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;}
                if(d>GM_MOB_DESPAWN_SOFT){
                    if(++m->despawn_ticks[i]>=GM_MOB_DESPAWN_DELAY){
                        nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;
                    }
                }else m->despawn_ticks[i]=0;
            }
        }
        if(!(pai_det() && hai_ok(type)) && !(hai_ok(type) && !pai_det()) &&
           day&&(type==EW_TYPE_ZOMBIE||type==EW_TYPE_SKELETON)&&
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
                if(nx->health[i]<=0.0f){
                    nx->health[i]=0.0f;
                    (void)tick_corpse(m, nx, i, drops);
                    continue;
                }
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

        if(hai_ok(type) && pai_det()){
            hai_living(m,w,now,i,day);
        }
        if(pai_det() && pai_det_ai(type)){
            /* EntityLiving.onEntityUpdate living-sound, then ++entityAge + despawn.
             * Animals talk 120; blaze/pigman/enderman talk 80. PersistenceRequired:
             * age=0, no nextInt(800). */
            {
                int lst = m->living_sound_time[i];
                int sound_draw = jrand_int_bound(pai_jr(m, i), 1000);
                m->living_sound_time[i] = lst + 1;
                if (sound_draw < lst) {
                    m->living_sound_time[i] = -pai_talk_interval(type);
                    (void)jrand_float(pai_jr(m, i));
                    (void)jrand_float(pai_jr(m, i));
                }
            }
            {
                double d3 = dx * dx + dy * dy + dz * dz;
                m->entity_age[i]++;
                if (m->det_persist[i]) {
                    m->entity_age[i] = 0;
                } else if (m->entity_age[i] > 600) {
                    (void)jrand_int_bound(pai_jr(m, i), 800);
                    if (d3 < 1024.0) m->entity_age[i] = 0;
                } else if (d3 < 1024.0) {
                    m->entity_age[i] = 0;
                }
            }
        }
        if(pai_det() && pai_det_ai(type) && !aggro){
            pai_tick(m,w,nx,i,px,py,pz,mob_griefing,
                     &moving,&jump,&wandering,&swim_jump,&nav_speed);
        }else if(passive && !pai_det()){
            MlMob mm;
            PlAiOut po;
            ml_load_slot(&mm, m, nx, i);
            mm.snap.panic = m->panic_ticks[i];
            mm.snap.swell = m->creeper_fuse[i];
            mm.snap.see_time = m->det_see_time[i];
            pl_passive_pre(&mm);
            pl_passive_ai(&mm, w, &po);
            ml_save_slot(m, nx, i, &mm);
            m->panic_ticks[i] = mm.snap.panic;
            m->creeper_fuse[i] = mm.snap.swell;
            m->det_see_time[i] = mm.snap.see_time;
            m->entity_age[i] = mm.despawn_ticks;
            if (!nx->alive[i]) continue;
            if (tick_corpse(m, nx, i, drops)) continue;
            pl_move_passive(&mm, w, st, po.moving, po.jump, po.speed_mul);
            ml_save_slot(m, nx, i, &mm);
            m->panic_ticks[i] = mm.snap.panic;
            m->creeper_fuse[i] = mm.snap.swell;
            m->det_see_time[i] = mm.snap.see_time;
            m->entity_age[i] = mm.despawn_ticks;
            continue;
        }else if(hai_ok(type) && !pai_det()){
            MlMob mm;
            MlAiOut o;
            int pre;
            ml_load_slot(&mm, m, nx, i);
            pre = ml_hostile_pre(&mm, w, px, py, pz, day);
            if (pre <= 0) {
                ml_save_slot(m, nx, i, &mm);
                continue;
            }
            ml_save_slot(m, nx, i, &mm);
            if (tick_corpse(m, nx, i, drops)) continue;
            {
                MlEndCtx ectx;
                memset(&ectx, 0, sizeof ectx);
                ectx.yaw = p->yaw;
                ectx.pitch = p->pitch;
                ectx.helmet = isr_get_stack(&p->inv, ISR_ARMOR_HEAD).item;
                ectx.griefing = mob_griefing;
                ectx.world_time = world_time;
                ectx.player_health = v ? v->health : 20.0f;
                ectx.pmx = p->ent.motionX;
                ectx.pmz = p->ent.motionZ;
                ml_hostile_ai(&mm, w, px, py, pz, day, m->seed, m->tick,
                              &ectx, &o);
            }
            ml_save_slot(m, nx, i, &mm);
            if (mm.exploded) {
                m->explosion_pending = 1;
                m->explosion_x = mm.snap.x;
                m->explosion_y = mm.snap.y + 0.5;
                m->explosion_z = mm.snap.z;
                m->explosion_size = EXL_RADIUS;
                continue;
            }
            if (!nx->alive[i]) continue;
            moving = o.moving;
            jump = o.jump;
            wandering = o.wandering;
            if (o.hit_player) {
                int acc = gm_mobs_attack_player(m, (struct PvStats *)v, &p->inv,
                                                o.hit_dmg, 0);
                p->health = v->health;
                /* EntityLivingBase.attackEntityFrom flag1 knockBack 0.4F
                 * (EntityLivingBase.java:1056-1067). Math.random jitter CUT
                 * when xz >= 1e-4. Player has no JavaRandom. */
                if (acc == 1) {
                    double d1 = nx->x[i] - px;
                    double d0 = nx->z[i] - pz;
                    if (d1 * d1 + d0 * d0 >= 1.0e-4)
                        ml_knockback(&p->ent.motionX, &p->ent.motionY,
                                     &p->ent.motionZ, p->ent.onGround, 0.4f,
                                     d1, d0);
                }
            }
            if (type == EW_TYPE_SPIDER || type == EW_TYPE_SLIME
                || type == EW_TYPE_ENDERMAN || type == EW_TYPE_WITCH) {
                ml_move_hostile(&mm, w, st, o.moving, o.jump);
                ml_save_slot(m, nx, i, &mm);
                m->panic_ticks[i] = mm.snap.panic;
                continue;
            }
        }else if(pai_det() && hai_ok(type)){
            /* EntityAIAttackMelee.updateTask: lookHelper then tryMoveToEntityLiving
             * read the same target.pos. Both use look_px (previous tape pl). */
            hai_tick(m,w,nx,i,lx,ly,lz,day,
                     &moving,&jump,&wandering,&swim_jump,&nav_speed);
            if(!nx->alive[i]) continue;
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
                m->explosion_size=EXL_RADIUS;
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
        if(moving&&type!=EW_TYPE_GHAST && !(pai_det() && (pai_det_ai(type) || hai_ok(type)))){
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
        if(pai_det() && pai_det_ai(type)) pai_apply_current_look(m,nx,i,lx,ly,lz);
        if(hai_ok(type) && pai_det()) hai_look(m,nx,i,lx,ly,lz);
        move_mob(w,st,m,nx,i,moving,jump,swim_jump,nav_speed);
        if(pai_det() && pai_det_living(type))
            pai_collide_nearby(m, nx, i, &player_bb, px, pz);
        if(passive && !pai_det()) pai_apply_current_look(m,nx,i,px,py,pz);
        /* EntityLiving.updateDistance -> bodyHelper, once from onUpdate headTurn. */
        if(pai_det() && (pai_det_ai(type) || hai_ok(type))) pai_body_update(m, nx, now, i);
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
    m->look_px = px;
    m->look_py = py;
    m->look_pz = pz;
    m->look_have = 1;
    ++m->tick;m->current^=1;
    (void)drops;
}

void gm_mobs_tick_orbs(GmMobLive *m, struct GmWorld *w, struct PsvPlayer *p,
                       int ox, int oz) {
    if (!m || !w || !p) return;
    tick_xp_orbs(m, w, (PsvPlayer *)p, ox, oz);
}

void gm_mobs_tick_boats(GmMobLive *m, struct GmWorld *w, struct PsvPlayer *p,
                        int ox, int oz, float forward, float strafe) {
    EwStore *now, *nx;
    int i;
    float boat_fwd, boat_str;
    PsvPlayer *pl = (PsvPlayer *)p;
    if (!m || !w || !p) return;
    now = now_store(m);
    nx = next_store(m);
    ew_store_copy(nx, now);
    boat_fwd = m->boat_ride >= 0 ? forward : 0.0f;
    boat_str = m->boat_ride >= 0 ? strafe : 0.0f;
    for (i = 1; i < EW_MAX_ENTITIES; ++i)
        if (now->alive[i] && m->entity_dimension[i] == m->active_dimension &&
            now->type[i] == EW_TYPE_BOAT)
            tick_boat(m, w, nx, i, pl, ox, oz, boat_fwd, boat_str);
    ew_store_copy(now, nx);
}

void gm_mobs_tick_spine(GmMobLive *m, GmWorld *w, const struct McSinTable *st_) {
    const McSinTable *st = (const McSinTable *)st_;
    EwStore *now, *nx;
    int i;
    if (!m || !w || !st) return;
    now = now_store(m);
    nx = next_store(m);
    ew_store_copy(nx, now);
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        EbLiving liv;
        PcfBlock blocks[ESS_MOB_BLOCKS];
        McAABB q;
        int n;
        float slip;
        int under;
        if (!now->alive[i] || !ess_is_spine_type((int)now->type[i])) continue;
        ess_load_pose(&liv, (int)now->type[i],
                      now->x[i], now->y[i], now->z[i],
                      now->vx[i], now->vy[i], now->vz[i],
                      now->on_ground[i], now->yaw[i],
                      m->det_box_on[i],
                      m->det_box[i].minX, m->det_box[i].minY, m->det_box[i].minZ,
                      m->det_box[i].maxX, m->det_box[i].maxY, m->det_box[i].maxZ);
        ess_query_box(&liv, &q);
        n = collect_blocks(w, &q, blocks, ESS_MOB_BLOCKS);
        under = gm_world_block(w, mc_floor(liv.base.phys.posX),
                               mc_floor(liv.base.phys.box.minY) - 1,
                               mc_floor(liv.base.phys.posZ));
        slip = ess_slip_on_ground(&liv, under);
        ess_tick_living(&liv, slip, blocks, n, st);
        ess_chicken_glide(&liv, (int)now->type[i]);
        ehs_store_living(nx, i, &liv);
        m->det_box[i] = liv.base.phys.box;
        m->det_box_on[i] = 1;
    }
    ++m->tick;
    m->current ^= 1;
}

void gm_mobs_tick_creeper_fuse(GmMobLive *m) {
    EwStore *s;
    int i;
    if (!m) return;
    s = now_store(m);
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        int ignited;
        if (!s->alive[i] || s->type[i] != EW_TYPE_CREEPER) continue;
        ignited = m->det_has_target[i] ? 1 : 0;
        if (!exl_fuse_tick(&m->creeper_fuse[i], ignited)) continue;
        s->alive[i] = 0;
        s->type[i] = EW_TYPE_NONE;
        m->creeper_fuse[i] = 0;
        m->explosion_pending = 1;
        m->explosion_x = s->x[i];
        m->explosion_y = s->y[i] + EXL_Y_OFF;
        m->explosion_z = s->z[i];
        m->explosion_size = EXL_RADIUS;
        break;
    }
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
        if(gm_passive(s->type[i]) || (/* det hydrate views */ hai_ok(s->type[i]) && pai_det())){
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

int gm_mobs_damage_near(GmMobLive *m, GmWorld *w,
                        double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops){
    if(!m)return 0;
    EwStore *s=now_store(m);int best=-1;double bd=radius*radius;
    (void)drops;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&m->entity_dimension[i]==m->active_dimension&&gm_living(s->type[i])&&
                                         s->type[i]!=EW_TYPE_BOAT){
        double dx=s->x[i]-x,dy=(s->y[i]+0.9)-y,dz=s->z[i]-z,d=dx*dx+dy*dy+dz*dz;
        if(d<=bd){bd=d;best=i;}
    }
    if(best<0)return 0;
    if (s->type[best] == EW_TYPE_ENDERMAN && w) {
        MlMob mm;
        ml_load_slot(&mm, m, s, best);
        (void)ml_enderman_arrow_hit(&mm, w);
        ml_save_slot(m, s, best, &mm);
        ew_store_copy(next_store(m), s);
        return 1;
    }
    s->health[best]-=damage;mark_hurt(m,s,best);
    if(s->health[best]<=0.0f) s->health[best]=0.0f;
    ew_store_copy(next_store(m),s);return 1;
}

int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z){
    float sz;
    return gm_mobs_take_explosion_size(m,x,y,z,&sz);
}

int gm_mobs_take_explosion_size(GmMobLive *m,double *x,double *y,double *z,
                                float *size){
    if(!m||!m->explosion_pending)return 0;
    if(x)*x=m->explosion_x;
    if(y)*y=m->explosion_y;
    if(z)*z=m->explosion_z;
    if(size)*size=m->explosion_size>0.0f?m->explosion_size:EXL_RADIUS;
    m->explosion_pending=0;return 1;
}

void gm_mobs_tick_tnt(GmMobLive *m, GmWorld *w) {
    EwStore *s;
    int i;
    if (!m || !w) return;
    s = now_store(m);
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        int wx, wy, wz, id, solid;
        double floor_y;
        if (!s->alive[i] || s->type[i] != EW_TYPE_TNT_PRIMED) continue;
        wx = mc_floor(s->x[i]);
        wy = mc_floor(s->y[i] - 0.01);
        wz = mc_floor(s->z[i]);
        id = gm_world_block(w, wx, wy, wz);
        solid = id > 0 && id != BLK_WEB
            && (mc_bpt_props(id).flags & BF_SOLID)
            && !(mc_bpt_props(id).flags & BF_LIQUID);
        floor_y = (double)wy + 1.0;
        {
            int og = (int)s->on_ground[i];
            if (!exl_tnt_on_update(&s->x[i], &s->y[i], &s->z[i],
                                   &s->vx[i], &s->vy[i], &s->vz[i],
                                   &og, &m->creeper_fuse[i],
                                   solid, floor_y)) {
                s->on_ground[i] = (u8)(og ? 1 : 0);
                continue;
            }
            s->on_ground[i] = (u8)(og ? 1 : 0);
        }
        m->explosion_pending = 1;
        m->explosion_x = s->x[i];
        m->explosion_y = s->y[i] + EXL_TNT_Y_OFF;
        m->explosion_z = s->z[i];
        m->explosion_size = EXL_TNT_SIZE;
        s->alive[i] = 0;
        s->type[i] = EW_TYPE_NONE;
        m->creeper_fuse[i] = 0;
        break;
    }
    ew_store_copy(next_store(m), s);
}

void gm_mobs_explosion_knockback(GmMobLive *m, GmLiveSim *drops,
                                 const u16 *grid, int ox, int oy, int oz,
                                 double ex, double ey, double ez, float size) {
    EwStore *s;
    int i;
    if (!m || !grid) return;
    s = now_store(m);
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        float width, height, eye, dens;
        double minx, miny, minz, maxx, maxy, maxz;
        ExBlast blast;
        if (!s->alive[i] || m->entity_dimension[i] != m->active_dimension)
            continue;
        if (s->type[i] == EW_TYPE_TNT_PRIMED) {
            float dens;
            double minx, miny, minz, maxx, maxy, maxz;
            ExBlast blast;
            minx = s->x[i] - 0.49; miny = s->y[i]; minz = s->z[i] - 0.49;
            maxx = s->x[i] + 0.49; maxy = s->y[i] + (double)EXL_TNT_HEIGHT;
            maxz = s->z[i] + 0.49;
            dens = ex_block_density(grid, ox, oy, oz, ex, ey, ez,
                                    minx, miny, minz, maxx, maxy, maxz);
            ex_entity_blast(s->x[i], s->y[i], s->z[i], 0.0f, ex, ey, ez, size,
                            dens, 0, &blast);
            if (blast.hit) {
                s->vx[i] += blast.addx;
                s->vy[i] += blast.addy;
                s->vz[i] += blast.addz;
            }
            continue;
        }
        if (!gm_living(s->type[i]) || s->type[i] == EW_TYPE_BOAT) continue;
        ehs_size((u8)s->type[i], &width, &height);
        if (m->det_box_on[i]) {
            minx = m->det_box[i].minX;
            miny = m->det_box[i].minY;
            minz = m->det_box[i].minZ;
            maxx = m->det_box[i].maxX;
            maxy = m->det_box[i].maxY;
            maxz = m->det_box[i].maxZ;
            height = (float)(maxy - miny);
        } else {
            minx = s->x[i] - (double)(width * 0.5f);
            miny = s->y[i];
            minz = s->z[i] - (double)(width * 0.5f);
            maxx = s->x[i] + (double)(width * 0.5f);
            maxy = s->y[i] + (double)height;
            maxz = s->z[i] + (double)(width * 0.5f);
        }
        eye = exl_eye_height(s->type[i], height);
        dens = ex_block_density(grid, ox, oy, oz, ex, ey, ez,
                                minx, miny, minz, maxx, maxy, maxz);
        ex_entity_blast(s->x[i], s->y[i], s->z[i], eye, ex, ey, ez, size,
                        dens, 0, &blast);
        if (!blast.hit) continue;
        s->vx[i] += blast.addx;
        s->vy[i] += blast.addy;
        s->vz[i] += blast.addz;
        s->health[i] -= blast.damage;
        mark_hurt(m, s, i);
        if (s->health[i] <= 0.0f) s->health[i] = 0.0f;
    }
    ew_store_copy(next_store(m), s);
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

unsigned gm_mobs_export_snap(const GmMobLive *m, struct RlSnapMob *out,
                             unsigned cap) {
    const EwStore *s;
    unsigned n = 0;
    int i, p;
    if (!m || !out || cap == 0) return 0;
    s = const_store(m);
    for (i = 1; i < EW_MAX_ENTITIES && n < cap; ++i) {
        RlSnapMob *o;
        if (s->type[i] == EW_TYPE_NONE && !s->alive[i]) continue;
        o = &out[n++];
        memset(o, 0, sizeof *o);
        o->slot = i;
        o->id = s->id[i];
        o->type = (int)s->type[i];
        o->alive = (int)s->alive[i];
        o->persist = (int)m->det_persist[i];
        o->x = s->x[i];
        o->y = s->y[i];
        o->z = s->z[i];
        o->yaw = s->yaw[i];
        o->pitch = m->passive_head_pitch[i];
        o->yaw_body = m->passive_render_yaw[i];
        o->mx = s->vx[i];
        o->my = s->vy[i];
        o->mz = s->vz[i];
        o->on_ground = (int)s->on_ground[i];
        o->health = s->health[i];
        o->hurt_time = m->hurt_time[i];
        o->death_time = m->death_time[i];
        o->screaming = m->screaming[i];
        o->carried = m->carried[i];
        o->carried_meta = m->carried_meta[i];
        o->target_change_time = m->target_change_time[i];
        o->ticks_existed = m->ticks_existed[i];
        o->find_aggro = m->find_aggro[i];
        o->teleport_time = m->teleport_time[i];
        o->witch_attack_timer = m->witch_attack_timer[i];
        o->witch_drink = m->witch_drink[i];
        o->effect_id = m->effect_id[i];
        o->effect_duration = m->effect_duration[i];
        o->effect_amplifier = m->effect_amplifier[i];
        o->task_bits = m->passive_tasks[i];
        o->target_tasks = m->det_target_tasks[i];
        o->wander_x = m->passive_idle_x[i];
        o->wander_z = m->passive_idle_z[i];
        o->panic = m->panic_ticks[i];
        o->target_idx = m->det_has_target[i] ? 1 : 0;
        o->see_time = m->det_see_time[i];
        o->stime = m->det_strafe_time[i];
        o->melee_delay = m->det_melee_delay[i];
        o->bow_attack_time = m->det_bow_attack_time[i];
        o->attack_time = s->attack_time[i];
        o->swell = m->creeper_fuse[i];
        o->anger = m->anger[i];
        if (gm_is_slimey(o->type)) {
            int sz = m->size[i] > 0 ? (int)m->size[i] : 2;
            o->swell = sz;
            o->melee_delay = m->jump_delay[i];
            o->see_time = m->was_on_ground[i];
        }
        for (p = 0; p < BLAZE_SNAP_PATH_CAP; ++p) {
            o->path_x[p] = m->det_nav_x[i][p];
            o->path_y[p] = m->det_nav_y[i][p];
            o->path_z[p] = m->det_nav_z[i][p];
        }
        o->path_n = m->det_nav_n[i];
        o->path_i = m->det_nav_i[i];
        o->nav_ticks = m->det_nav_ticks[i];
        o->nav_stuck_at = m->det_nav_stuck_at[i];
        o->nav_stuck_x = m->det_nav_stuck_x[i];
        o->nav_stuck_y = m->det_nav_stuck_y[i];
        o->nav_stuck_z = m->det_nav_stuck_z[i];
        o->box_on = m->det_box_on[i];
        o->box_minx = m->det_box[i].minX;
        o->box_miny = m->det_box[i].minY;
        o->box_minz = m->det_box[i].minZ;
        o->box_maxx = m->det_box[i].maxX;
        o->box_maxy = m->det_box[i].maxY;
        o->box_maxz = m->det_box[i].maxZ;
        o->seed48 = m->ent_jr_seed[i];
        o->have_gauss = m->ent_jr_have_gauss[i];
        o->gauss = m->ent_jr_gauss[i];
    }
    return n;
}

void gm_mobs_import_snap(GmMobLive *m, const struct RlSnapMob *in, unsigned n) {
    EwStore *s;
    long long seed;
    unsigned k;
    int p;
    if (!m) return;
    seed = m->seed;
    gm_mobs_init(m, seed);
    if (!in || n == 0) return;
    s = now_store(m);
    for (k = 0; k < n; ++k) {
        const RlSnapMob *o = &in[k];
        int i = o->slot;
        if (i < 1 || i >= EW_MAX_ENTITIES) continue;
        s->type[i] = (u8)o->type;
        s->alive[i] = (u8)(o->alive ? 1 : 0);
        s->id[i] = o->id;
        s->x[i] = o->x;
        s->y[i] = o->y;
        s->z[i] = o->z;
        s->vx[i] = o->mx;
        s->vy[i] = o->my;
        s->vz[i] = o->mz;
        s->yaw[i] = o->yaw;
        s->health[i] = o->health;
        s->on_ground[i] = (u8)(o->on_ground ? 1 : 0);
        s->attack_time[i] = o->attack_time;
        m->det_persist[i] = (unsigned char)(o->persist ? 1 : 0);
        m->passive_head_pitch[i] = o->pitch;
        m->passive_render_yaw[i] = o->yaw_body;
        m->hurt_time[i] = o->hurt_time;
        m->death_time[i] = o->death_time;
        m->screaming[i] = o->screaming;
        m->carried[i] = o->carried;
        m->carried_meta[i] = o->carried_meta;
        m->target_change_time[i] = o->target_change_time;
        m->ticks_existed[i] = o->ticks_existed;
        m->find_aggro[i] = o->find_aggro;
        m->teleport_time[i] = o->teleport_time;
        m->witch_attack_timer[i] = o->witch_attack_timer;
        m->witch_drink[i] = o->witch_drink;
        m->effect_id[i] = o->effect_id;
        m->effect_duration[i] = o->effect_duration;
        m->effect_amplifier[i] = o->effect_amplifier;
        m->passive_tasks[i] = o->task_bits;
        m->det_target_tasks[i] = o->target_tasks;
        m->passive_idle_x[i] = o->wander_x;
        m->passive_idle_z[i] = o->wander_z;
        m->panic_ticks[i] = o->panic;
        m->det_has_target[i] = (unsigned char)(o->target_idx ? 1 : 0);
        m->det_see_time[i] = o->see_time;
        m->det_strafe_time[i] = o->stime;
        m->det_melee_delay[i] = o->melee_delay;
        m->det_bow_attack_time[i] = o->bow_attack_time;
        m->creeper_fuse[i] = o->swell;
        m->anger[i] = o->anger;
        if (gm_is_slimey(o->type)) {
            m->size[i] = (unsigned char)(o->swell > 0 ? o->swell : 1);
            m->jump_delay[i] = o->melee_delay;
            m->was_on_ground[i] = (unsigned char)(o->see_time ? 1 : 0);
        }
        for (p = 0; p < BLAZE_SNAP_PATH_CAP; ++p) {
            m->det_nav_x[i][p] = o->path_x[p];
            m->det_nav_y[i][p] = o->path_y[p];
            m->det_nav_z[i][p] = o->path_z[p];
        }
        m->det_nav_n[i] = o->path_n;
        m->det_nav_i[i] = o->path_i;
        m->det_nav_ticks[i] = o->nav_ticks;
        m->det_nav_stuck_at[i] = o->nav_stuck_at;
        m->det_nav_stuck_x[i] = o->nav_stuck_x;
        m->det_nav_stuck_y[i] = o->nav_stuck_y;
        m->det_nav_stuck_z[i] = o->nav_stuck_z;
        m->det_box_on[i] = o->box_on;
        m->det_box[i].minX = o->box_minx;
        m->det_box[i].minY = o->box_miny;
        m->det_box[i].minZ = o->box_minz;
        m->det_box[i].maxX = o->box_maxx;
        m->det_box[i].maxY = o->box_maxy;
        m->det_box[i].maxZ = o->box_maxz;
        m->ent_jr_seed[i] = o->seed48;
        m->ent_jr_have_gauss[i] = (unsigned char)(o->have_gauss ? 1 : 0);
        m->ent_jr_gauss[i] = o->gauss;
        m->entity_dimension[i] = (signed char)m->active_dimension;
        if (i + 1 > s->count) s->count = i + 1;
        if (o->id >= m->next_id) m->next_id = o->id + 1;
    }
    ew_store_copy(next_store(m), s);
}

unsigned gm_mobs_export_orbs(const GmMobLive *m, struct RlSnapOrb *out,
                             unsigned cap) {
    unsigned n = 0;
    int i;
    if (!m || !out || cap == 0) return 0;
    for (i = 0; i < GM_XP_ORBS && n < cap; ++i) {
        const McOrb *o = &m->xp_orbs[i];
        RlSnapOrb *d;
        if (o->dead || o->xpValue <= 0) continue;
        d = &out[n++];
        memset(d, 0, sizeof *d);
        d->x = o->posX; d->y = o->posY; d->z = o->posZ;
        d->mx = o->motionX; d->my = o->motionY; d->mz = o->motionZ;
        d->on_ground = o->onGround;
        d->xpOrbAge = o->xpOrbAge;
        d->delayBeforeCanPickup = o->delayBeforeCanPickup;
        d->xpValue = o->xpValue;
        d->eid = o->eid;
        d->xpColor = o->xpColor;
        d->xpTargetColor = o->xpTargetColor;
        d->has_closest = o->has_closest;
        d->dead = o->dead;
    }
    return n;
}

void gm_mobs_import_orbs(GmMobLive *m, const struct RlSnapOrb *in, unsigned n) {
    unsigned k;
    int i;
    if (!m) return;
    for (i = 0; i < GM_XP_ORBS; ++i) {
        memset(&m->xp_orbs[i], 0, sizeof m->xp_orbs[i]);
        m->xp_orbs[i].dead = 1;
        m->orb_dimension[i] = (signed char)m->active_dimension;
    }
    if (!in || n == 0) return;
    for (k = 0; k < n && k < (unsigned)GM_XP_ORBS; ++k) {
        const RlSnapOrb *d = &in[k];
        McOrb *o = &m->xp_orbs[k];
        memset(o, 0, sizeof *o);
        o->xpValue = d->xpValue;
        o->eid = d->eid;
        o->delayBeforeCanPickup = d->delayBeforeCanPickup;
        o->xpOrbAge = d->xpOrbAge;
        o->xpColor = d->xpColor;
        o->xpTargetColor = d->xpTargetColor;
        o->has_closest = d->has_closest;
        o->dead = d->dead || d->xpValue <= 0;
        o->motionX = d->mx; o->motionY = d->my; o->motionZ = d->mz;
        o->onGround = d->on_ground;
        eo_set_position(o, d->x, d->y, d->z);
        o->motionX = d->mx; o->motionY = d->my; o->motionZ = d->mz;
        o->onGround = d->on_ground;
        m->orb_dimension[k] = (signed char)m->active_dimension;
        if (d->eid >= m->next_orb_id) m->next_orb_id = d->eid + 1;
    }
}
