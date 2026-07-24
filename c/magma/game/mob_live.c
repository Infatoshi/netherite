#include "player_survival.h"

#include "game/mob_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "mc_rng.h"
#include "player_vitals.h"

#include <math.h>
#include <string.h>

#define GM_MOB_REACH 2.0
#define GM_MOB_BLOCKS 256
#define GM_MOB_WANDER_INTERVAL 120
#define GM_MOB_WANDER_RADIUS 8
#define GM_MOB_PANIC_TICKS 100
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
    if (gm_passive(type)) return 10.0f;
    if (type == EW_TYPE_BOAT) return 40.0f;
    return 20.0f;
}

/* EntityZombie ATTACK_DAMAGE=3; pigman=5; wither skeleton base 4 + stone sword 4;
 * silverfish base 1; slime damages when size > 1 for size; magma is size + 2. */
static float melee_damage(int type, int size) {
    if (type == EW_TYPE_ENDERMAN) return 7.0f;
    if (type == EW_TYPE_ZOMBIE) return 3.0f;
    if (type == EW_TYPE_PIGMAN) return 5.0f;
    if (type == EW_TYPE_WITHER_SKELETON) return 8.0f;
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
    return 16.0;
}

static void reset_slot_state_s(GmMobLive *m, EwStore *s, int slot) {
    if (slot < 0 || slot >= EW_MAX_ENTITIES) return;
    if (s) s->repath_timer[slot] = GM_MOB_WANDER_INTERVAL;
    m->creeper_fuse[slot] = 0;
    m->hurt_aggro[slot] = 0;
    m->panic_ticks[slot] = 0;
    m->fire_ticks[slot] = 0;
    m->despawn_ticks[slot] = 0;
    m->anger[slot] = 0;
    m->jump_delay[slot] = 0;
    m->charge[slot] = 0;
    m->boat_damage[slot] = 0;
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
    if (gm_passive(s->type[slot])) m->panic_ticks[slot] = GM_MOB_PANIC_TICKS;
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

int gm_mobs_place_boat(GmMobLive *m, double x, double y, double z, float yaw) {
    int slot = gm_mobs_spawn(m, EW_TYPE_BOAT, x, y, z);
    if (slot < 0) return -1;
    now_store(m)->yaw[slot] = yaw;
    next_store(m)->yaw[slot] = yaw;
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
    damage_held_weapon((PsvPlayer *)p);
    m->player_attack_cooldown = 10;
    if (s->health[best] <= 0.0f) mob_drop(m, s, best, drops);
    ew_store_copy(next_store(m), s);
    return 1;
}

static void move_mob(GmWorld *w, const McSinTable *st, GmMobLive *m, EwStore *s,
                     int i, int moving, int jump) {
    EhsIntent intent;
    EbLiving liv;
    PcfBlock blocks[GM_MOB_BLOCKS];
    ehs_intent_from_ai(s->type[i], s->ai_state[i], moving, s->x[i], s->z[i],
                       s->path_tx[i], s->path_tz[i], s->path_tx[i], s->path_tz[i], &intent);
    if (!moving) intent.yaw = s->yaw[i];
    if (moving && jump) intent.isJumping = 1;
    ehs_load_living(&liv, s, i, &intent);
    /* Override size for slime/magma. */
    if (gm_is_slimey(s->type[i])) {
        float w, h; ehs_size_scaled(s->type[i], m->size[i], &w, &h);
        liv.base.width = w; liv.base.height = h;
        liv.base.phys.box = mc_aabb_make(s->x[i] - w * 0.5, s->y[i], s->z[i] - w * 0.5,
                                         s->x[i] + w * 0.5, s->y[i] + h, s->z[i] + w * 0.5);
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
        int slot = ew_store_spawn(s, (u8)types[mc_hash_bound(mc_hash64(h + 1), 4)],
                                  m->next_id++, x + 0.5, y, z + 0.5, 10.0f);
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

static void natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz, int dimension, long long world_time) {
    discover_spawners(m, w, px, py, pz, dimension);
    tick_spawners(m, w, s, px, py, pz);

    if (dimension == -1) {
        nether_natural_spawn(m, w, s, px, py, pz);
        return;
    }
    if (dimension != 0) return;

    slime_spawn(m, w, s, px, py, pz);

    int tod = (int)(world_time % 24000LL); if (tod < 0) tod += 24000;
    if (tod < 12000) { passive_spawn(m, w, s, px, py, pz); return; }
    if (tod < 13000 || tod > 23000 || (m->tick % 20) ||
        alive_count(m,s) >= GM_NATURAL_HOSTILE_CAP) return;
    for (int a = 0; a < 8; ++a) {
        u64 h = mc_hash_seed((u64)m->seed, m->tick, a, 0, 0, 0x4d4f4253u);
        int dx = 24 + mc_hash_bound(h, 9), dz = mc_hash_bound(mc_hash64(h), 17) - 8;
        if (h & 1ULL) dx = -dx;
        int x = mc_floor(px) + dx, z = mc_floor(pz) + dz;
        gm_world_ensure(w, x >> 4, z >> 4, 0);
        int y = gm_world_surface_y(w, x, z);
        double ddx = (x + 0.5) - px, ddy = y - py, ddz = (z + 0.5) - pz;
        double ds = ddx * ddx + ddy * ddy + ddz * ddz;
        if (ds < 24.0 * 24.0 || ds > 32.0 * 32.0 || !solid_id(gm_world_block(w, x, y - 1, z)) ||
            gm_world_block(w, x, y, z) != 0 || gm_world_block(w, x, y + 1, z) != 0 ||
            gm_world_block_light(w, x, y, z) > 7) continue;
        int types[5] = {EW_TYPE_ZOMBIE, EW_TYPE_SKELETON, EW_TYPE_CREEPER,
                        EW_TYPE_SPIDER, EW_TYPE_ENDERMAN};
        int type = types[mc_hash_bound(mc_hash64(h + 1), 5)];
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

static void tick_boat(GmMobLive *m, GmWorld *w, EwStore *nx, int i,
                      PsvPlayer *p, int ox, int oz, float forward, float strafe) {
    (void)strafe;
    /* Buoyancy: if water below/at, float; else gravity via living spine-lite. */
    int bx = mc_floor(nx->x[i]), by = mc_floor(nx->y[i]), bz = mc_floor(nx->z[i]);
    int feet = gm_world_block(w, bx, by, bz);
    int below = gm_world_block(w, bx, by - 1, bz);
    int in_water = (feet == 8 || feet == 9 || below == 8 || below == 9);
    if (in_water) {
        if (nx->y[i] < (double)by + 0.45) nx->vy[i] = 0.04;
        else nx->vy[i] *= 0.9;
        nx->vy[i] *= 0.95;
    } else {
        nx->vy[i] -= 0.04;
        if (nx->vy[i] < -0.5) nx->vy[i] = -0.5;
    }
    if (m->boat_ride == i && p) {
        nx->yaw[i] = p->yaw;
        double yr = p->yaw * MC_PI / 180.0;
        double speed = in_water ? 0.25 : 0.04;
        nx->vx[i] = -sin(yr) * forward * speed;
        nx->vz[i] = cos(yr) * forward * speed;
        p->ent.posX = nx->x[i] - ox;
        p->ent.posY = nx->y[i] + 0.35;
        p->ent.posZ = nx->z[i] - oz;
        p->ent.motionX = p->ent.motionY = p->ent.motionZ = 0.0;
        p->ent.onGround = 1;
    } else {
        nx->vx[i] *= 0.9; nx->vz[i] *= 0.9;
    }
    /* Simple collision: don't enter solid blocks. */
    double nx_ = nx->x[i] + nx->vx[i];
    double ny_ = nx->y[i] + nx->vy[i];
    double nz_ = nx->z[i] + nx->vz[i];
    if (!solid_id(gm_world_block(w, mc_floor(nx_), mc_floor(ny_), mc_floor(nz_)))) {
        nx->x[i] = nx_; nx->y[i] = ny_; nx->z[i] = nz_;
    } else {
        nx->vx[i] = nx->vz[i] = 0.0;
        if (nx->vy[i] < 0) { nx->vy[i] = 0; nx->on_ground[i] = 1; }
    }
}

void gm_mobs_tick(GmMobLive *m, GmWorld *w, const struct McSinTable *st_,
                  struct PsvPlayer *player_, struct PvStats *vitals_,
                  int ox, int oz, int dimension, long long world_time, GmLiveSim *drops) {
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
    natural_spawn(m,w,nx,px,py,pz,dimension,world_time);
    int tod=(int)(world_time%24000LL); if(tod<0)tod+=24000;
    int day=dimension==0&&tod<12000;
    float boat_fwd = 0.0f;
    /* While riding, WASD is not available here; boat holds position unless
     * tests set path. Runtime sets forward via player look when mounted. */
    if (m->boat_ride >= 0) boat_fwd = 1.0f;

    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(now->alive[i]&&m->entity_dimension[i]==dimension&&gm_living(now->type[i])){
        int type=now->type[i];
        if (type == EW_TYPE_BOAT) {
            tick_boat(m, w, nx, i, p, ox, oz, boat_fwd, 0.0f);
            continue;
        }
        int hostile=gm_hostile(type), passive=gm_passive(type);
        if(nx->attack_time[i]>0)--nx->attack_time[i];
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
        int moving=0,jump=0,wandering=0;
        int ranged=type==EW_TYPE_SKELETON||type==EW_TYPE_BLAZE||type==EW_TYPE_GHAST;

        /* Ghast AIFireballAttack: charge then fire large fireball. */
        if(aggro&&type==EW_TYPE_GHAST){
            nx->path_tx[i]=px;nx->path_ty[i]=py+8.0;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            nx->ai_state[i]=EW_AI_ATTACK;
            if(d>16.0){moving=1;nx->ai_state[i]=EW_AI_CHASE;}
            ++m->charge[i];
            /* Charge 20 ticks, then reset through a 40-tick cooldown. */
            if(m->charge[i]>=20 && !m->fireball_pending){
                double len=d>0.01?d:1.0;
                m->fireball_pending=1;
                m->fireball_x=now->x[i];m->fireball_y=now->y[i]+1.5;m->fireball_z=now->z[i];
                m->fireball_vx=dx/len*0.5;m->fireball_vy=dy/len*0.5;m->fireball_vz=dz/len*0.5;
                m->charge[i]=-40;
            }
        }else if(aggro&&ranged){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(nx->attack_time[i]<=0)nx->attack_time[i]=40;
        }else if(aggro&&type==EW_TYPE_CREEPER&&xz<=3.0&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(++m->creeper_fuse[i]>=30){
                nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;m->creeper_fuse[i]=0;m->explosion_pending=1;
                m->explosion_x=now->x[i];m->explosion_y=now->y[i]+0.5;m->explosion_z=now->z[i];
            }
        }else if(aggro&&gm_is_slimey(type)){
            /* Slime hop toward player. */
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;
            nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(m->jump_delay[i]>0)--m->jump_delay[i];
            if(now->on_ground[i]&&m->jump_delay[i]<=0){
                jump=1;moving=1;
                m->jump_delay[i]=10+m->size[i]*5;
                nx->ai_state[i]=EW_AI_CHASE;
            }else if(!now->on_ground[i]){
                moving=1;nx->ai_state[i]=EW_AI_CHASE;
            }else nx->ai_state[i]=EW_AI_IDLE;
            if(xz<=GM_MOB_REACH*(0.5+m->size[i]*0.25)&&fabs(dy)<(double)m->size[i]+1.0&&
               nx->attack_time[i]<=0){
                float dmg=melee_damage(type,m->size[i]);
                if(dmg>0.0f){
                    (void)gm_mobs_attack_player(m,(struct PvStats *)v,
                                                &p->inv,dmg,0);
                    p->health=v->health;
                }
                nx->attack_time[i]=20;
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
                nx->attack_time[i]=20;
            }
        }else if(aggro){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_CHASE;moving=1;
            if(type==EW_TYPE_CREEPER&&m->creeper_fuse[i]>0)--m->creeper_fuse[i];
        }else if(passive&&m->panic_ticks[i]>0){
            --m->panic_ticks[i];
            double ux=xz>0.01?dx/xz:1.0, uz=xz>0.01?dz/xz:0.0;
            nx->path_tx[i]=now->x[i]-ux*8.0;nx->path_ty[i]=now->y[i];
            nx->path_tz[i]=now->z[i]-uz*8.0;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_IDLE;moving=1;
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
                else if(moving){jump=1;m->jump_delay[i]=20+m->size[i]*10;}
            }
        }
        if(moving&&type!=EW_TYPE_GHAST){
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
        move_mob(w,st,m,nx,i,moving,jump);
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
        out[n].item_meta=m->size[i]; /* slime/magma size for render scale */
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
    if(!m||!m->fireball_pending)return 0;
    if(x)*x=m->fireball_x;if(y)*y=m->fireball_y;if(z)*z=m->fireball_z;
    if(vx)*vx=m->fireball_vx;if(vy)*vy=m->fireball_vy;if(vz)*vz=m->fireball_vz;
    m->fireball_pending=0;return 1;
}
