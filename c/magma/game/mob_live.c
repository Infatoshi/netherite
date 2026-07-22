#include "player_survival.h"

#include "game/mob_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"
#include "mc_rng.h"
#include "player_vitals.h"

#include <math.h>
#include <string.h>

#define GM_MOB_REACH 2.0
#define GM_MOB_BLOCKS 256
#define GM_MOB_WANDER_INTERVAL 120  /* EntityAIWander executeChance-ish cadence */
#define GM_MOB_WANDER_RADIUS 8
#define GM_MOB_PANIC_TICKS 100
#define GM_MOB_DESPAWN_SOFT 32.0    /* EntityLiving.despawnEntity */
#define GM_MOB_DESPAWN_HARD 128.0
#define GM_MOB_DESPAWN_DELAY 600
#define GM_MOB_FIRE_TICKS 160       /* setFire(8) */

static EwStore *now_store(GmMobLive *m) { return m->current ? &m->b : &m->a; }
static EwStore *next_store(GmMobLive *m) { return m->current ? &m->a : &m->b; }
static const EwStore *const_store(const GmMobLive *m) { return m->current ? &m->b : &m->a; }
static int gm_hostile(int type){return ehs_is_hostile((u8)type)||type==GM_MOB_BLAZE;}
static int gm_passive(int type){return type>=GM_MOB_SHEEP&&type<=GM_MOB_CHICKEN;}
static int gm_living(int type){return gm_hostile(type)||gm_passive(type);}

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

static float max_health(int type) { return type == EW_TYPE_ENDERMAN ? 40.0f : 20.0f; }

/* EntityZombie.applyEntityAttributes sets ATTACK_DAMAGE=3.0. NORMAL leaves
 * difficulty-scaled mob damage unchanged in EntityPlayer.attackEntityFrom. */
static float melee_damage(int type) {
    return type == EW_TYPE_ENDERMAN ? 7.0f : type == EW_TYPE_ZOMBIE ? 3.0f : 4.0f;
}

/* Vanilla followRange: zombie 40 (EntityZombie attribute), everything else base 16. */
static double follow_range(int type) { return type == EW_TYPE_ZOMBIE ? 40.0 : 16.0; }

static void reset_slot_state_s(GmMobLive *m, EwStore *s, int slot) {
    if (slot < 0 || slot >= EW_MAX_ENTITIES) return;
    /* Fresh mobs idle a full wander interval before their first stroll
     * (EntityAIWander fires with 1/120 chance per tick in vanilla). */
    if (s) s->repath_timer[slot] = GM_MOB_WANDER_INTERVAL;
    m->creeper_fuse[slot] = 0;
    m->hurt_aggro[slot] = 0;
    m->panic_ticks[slot] = 0;
    m->fire_ticks[slot] = 0;
    m->despawn_ticks[slot] = 0;
}

static void mark_hurt(GmMobLive *m, EwStore *s, int slot) {
    m->hurt_aggro[slot] = 1;
    if (gm_passive(s->type[slot])) m->panic_ticks[slot] = GM_MOB_PANIC_TICKS;
}

/* Cheap sampled line-of-sight through gm_world_block (EntitySenses stand-in). */
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

/* Find a standable cell near y0 at (x,z): solid below, two air above; max 3 down. */
static int wander_ground_y(GmWorld *w, int x, int y0, int z) {
    for (int yy = y0 + 1; yy >= y0 - 3; --yy) {
        if (yy < 1) break;
        if (solid_id(gm_world_block(w, x, yy - 1, z)) &&
            !solid_id(gm_world_block(w, x, yy, z)) &&
            !solid_id(gm_world_block(w, x, yy + 1, z))) return yy;
    }
    return -1000;
}

/* Zombie/AbstractSkeleton onLivingUpdate: burn in daytime under open sky, not in water. */
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
        eo_set_position(o,x,y,z);
    }
}

int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z) {
    if (!m || !gm_living(type)) return -1;
    EwStore *s = now_store(m);
    int slot = ew_store_spawn(s, (u8)type, m->next_id++, x, y, z, max_health(type));
    if (slot >= 0) { reset_slot_state_s(m, s, slot); ew_store_copy(next_store(m), s); }
    return slot;
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

static void mob_drop(GmMobLive *m, EwStore *s, int i, GmLiveSim *drops) {
    int item = 0, count = 1, xp = 5;
    switch (s->type[i]) {
    case EW_TYPE_ZOMBIE: item = 367; break;
    case EW_TYPE_SKELETON: item = 352; break;
    case EW_TYPE_CREEPER: item = 289; break;
    case EW_TYPE_SPIDER: item = 287; break;
    case EW_TYPE_ENDERMAN:
        /* Vanilla drops 0..1. This stateless roll is deterministic per entity. */
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item = 368;
        xp = 5;
        break;
    case GM_MOB_BLAZE:
        if ((mc_hash64((u64)m->seed ^ (u64)s->id[i]) & 1ULL) != 0) item=369;
        xp=10;break;
    case GM_MOB_SHEEP:
        item=35;xp=1;
        gm_live_spawn_item(drops,s->x[i],s->y[i]+0.25,s->z[i],423,1,0,10);break;
    case GM_MOB_PIG: item=319;xp=1;break;
    case GM_MOB_COW:
        item=363;xp=1;
        gm_live_spawn_item(drops,s->x[i],s->y[i]+0.25,s->z[i],334,1,0,10);break;
    case GM_MOB_CHICKEN:
        item=365;xp=1;
        gm_live_spawn_item(drops,s->x[i],s->y[i]+0.25,s->z[i],288,1,0,10);break;
    default: break;
    }
    if (item) gm_live_spawn_item(drops, s->x[i], s->y[i] + 0.25, s->z[i], item, count, 0, 10);
    gm_mobs_spawn_xp(m,s->x[i],s->y[i]+0.25,s->z[i],xp);
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
        float width, height; ehs_size(s->type[i], &width, &height);
        double cx = s->x[i], cy = s->y[i] + height * 0.5, cz = s->z[i];
        double vx = cx-px, vy = cy-py, vz = cz-pz;
        double t = vx*dx + vy*dy + vz*dz;
        if (t < 0.0 || t > best_t) continue;
        double ex=vx-t*dx, ey=vy-t*dy, ez=vz-t*dz;
        double radius = width * 0.5 + 0.25;
        if (ex*ex + ez*ez <= radius*radius && fabs(ey) <= height*0.5 + 0.25) {
            best=i; best_t=t;
        }
    }
    if (best < 0) return 0;
    if (m->player_attack_cooldown > 0) return 1;
    s->health[best] -= held_damage(p);
    mark_hurt(m, s, best);
    damage_held_weapon((PsvPlayer *)p);
    m->player_attack_cooldown = 10;
    if (s->health[best] <= 0.0f) mob_drop(m, s, best, drops);
    ew_store_copy(next_store(m), s);
    return 1;
}

static void move_mob(GmWorld *w, const McSinTable *st, EwStore *s, int i, int moving, int jump) {
    EhsIntent intent;
    EbLiving liv;
    PcfBlock blocks[GM_MOB_BLOCKS];
    ehs_intent_from_ai(s->type[i], s->ai_state[i], moving, s->x[i], s->z[i],
                       s->path_tx[i], s->path_tz[i], s->path_tx[i], s->path_tz[i], &intent);
    if (!moving) intent.yaw = s->yaw[i];
    if (moving && jump) intent.isJumping = 1;
    ehs_load_living(&liv, s, i, &intent);
    float slip = 0.6f;
    if (liv.base.phys.onGround) {
        int id = gm_world_block(w, mc_floor(liv.base.phys.posX),
                                mc_floor(liv.base.phys.box.minY)-1,
                                mc_floor(liv.base.phys.posZ));
        if (id == 79 || id == 174 || id == 212) slip = 0.98f;
    }
    McAABB q = mc_aabb_addcoord(&liv.base.phys.box, liv.base.phys.motionX,
                                liv.base.phys.motionY, liv.base.phys.motionZ);
    q.minY -= liv.base.phys.stepHeight; q.maxY += liv.base.phys.stepHeight;
    int n = collect_blocks(w, &q, blocks, GM_MOB_BLOCKS);
    eb_tick_living(&liv, slip, 0, blocks, n, st);
    ehs_store_living(s, i, &liv);
}

static int alive_count(const EwStore *s) {
    int n=0;
    for (int i=1;i<EW_MAX_ENTITIES;++i) if (s->alive[i] && gm_hostile(s->type[i])) ++n;
    return n;
}
static int living_count(const EwStore *s) {
    int n=0;
    for(int i=1;i<EW_MAX_ENTITIES;++i)
        if(s->alive[i]&&gm_living(s->type[i]))++n;
    return n;
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
    for(int i=0;i<GM_XP_ORBS;++i){McOrb *o=&m->xp_orbs[i];if(o->dead||o->xpValue<=0)continue;
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

static void passive_spawn(GmMobLive *m,GmWorld *w,EwStore *s,double px,double py,double pz){
    if(m->tick==0||(m->tick%200)||living_count(s)>=7)return;
    for(int a=0;a<8;++a){u64 h=mc_hash_seed((u64)m->seed,m->tick,a,0,0,0x50415353u);
        int dx=12+mc_hash_bound(h,9),dz=mc_hash_bound(mc_hash64(h),17)-8;if(h&1ULL)dx=-dx;
        int x=mc_floor(px)+dx,z=mc_floor(pz)+dz;gm_world_ensure(w,x>>4,z>>4,0);
        int y=gm_world_surface_y(w,x,z);double vx=x+0.5-px,vy=y-py,vz=z+0.5-pz;
        if(vx*vx+vy*vy+vz*vz>24.0*24.0||gm_world_block(w,x,y-1,z)!=2||
           gm_world_block(w,x,y,z)||gm_world_block(w,x,y+1,z))continue;
        int types[4]={GM_MOB_SHEEP,GM_MOB_PIG,GM_MOB_COW,GM_MOB_CHICKEN};
        int slot=ew_store_spawn(s,(u8)types[mc_hash_bound(mc_hash64(h+1),4)],m->next_id++,x+0.5,y,z+0.5,10.0f);
        reset_slot_state_s(m,s,slot);return;
    }
}

static void natural_spawn(GmMobLive *m, GmWorld *w, EwStore *s,
                          double px, double py, double pz, int dimension, long long world_time) {
    if(dimension==-1){
        if((m->tick%200)||alive_count(s)>=7)return;
        int pcx=mc_floor(px),pcy=mc_floor(py),pcz=mc_floor(pz);
        for(int x=pcx-16;x<=pcx+16;++x)for(int y=pcy-8;y<=pcy+8;++y)
            for(int z=pcz-16;z<=pcz+16;++z)if(gm_world_block(w,x,y,z)==52){
                for(int dx=-4;dx<=4;++dx)for(int dz=-4;dz<=4;++dz){
                    int sx=x+dx,sz=z+dz,sy=y;
                    if(gm_world_block(w,sx,sy,sz)||gm_world_block(w,sx,sy+1,sz))continue;
                    reset_slot_state_s(m,s,ew_store_spawn(s,GM_MOB_BLAZE,m->next_id++,sx+0.5,sy,sz+0.5,20.0f));return;
                }
            }
        return;
    }
    if(dimension!=0)return;
    int tod = (int)(world_time % 24000LL); if (tod < 0) tod += 24000;
    if(tod<12000){passive_spawn(m,w,s,px,py,pz);return;}
    if (tod < 13000 || tod > 23000 || (m->tick % 20) || alive_count(s) >= 7) return;
    for (int a=0;a<8;++a) {
        u64 h=mc_hash_seed((u64)m->seed,m->tick,a,0,0,0x4d4f4253u);
        int dx=24+mc_hash_bound(h,9), dz=mc_hash_bound(mc_hash64(h),17)-8;
        if (h&1ULL) dx=-dx;
        int x=mc_floor(px)+dx, z=mc_floor(pz)+dz;
        gm_world_ensure(w,x>>4,z>>4,0);
        int y=gm_world_surface_y(w,x,z);
        double ddx=(x+0.5)-px, ddy=y-py, ddz=(z+0.5)-pz;
        double ds=ddx*ddx+ddy*ddy+ddz*ddz;
        if (ds<24.0*24.0 || ds>32.0*32.0 || !solid_id(gm_world_block(w,x,y-1,z)) ||
            gm_world_block(w,x,y,z)!=0 || gm_world_block(w,x,y+1,z)!=0 ||
            gm_world_block_light(w,x,y,z)>7) continue;
        int types[5]={EW_TYPE_ZOMBIE,EW_TYPE_SKELETON,EW_TYPE_CREEPER,EW_TYPE_SPIDER,EW_TYPE_ENDERMAN};
        int type=types[mc_hash_bound(mc_hash64(h+1),5)];
        int slot=ew_store_spawn(s,(u8)type,m->next_id++,x+0.5,y,z+0.5,max_health(type));
        if(slot>=0){s->cx[slot]=x>>4;s->cz[slot]=z>>4;reset_slot_state_s(m,s,slot);} return;
    }
}

void gm_mobs_tick(GmMobLive *m, GmWorld *w, const struct McSinTable *st_,
                  struct PsvPlayer *player_, struct PvStats *vitals_,
                  int ox, int oz, int dimension, long long world_time, GmLiveSim *drops) {
    if (!m || !w || !player_ || !vitals_) return;
    PsvPlayer *p=(PsvPlayer *)player_; PvStats *v=(PvStats *)vitals_;
    const McSinTable *st=(const McSinTable *)st_;
    EwStore *now=now_store(m), *nx=next_store(m); ew_store_copy(nx,now);
    if(m->player_attack_cooldown>0)--m->player_attack_cooldown;
    double px=p->ent.posX+ox, py=p->ent.posY, pz=p->ent.posZ+oz;
    natural_spawn(m,w,nx,px,py,pz,dimension,world_time);
    int tod=(int)(world_time%24000LL); if(tod<0)tod+=24000;
    int day=dimension==0&&tod<12000;
    for(int i=1;i<EW_MAX_ENTITIES;++i) if(now->alive[i]&&gm_living(now->type[i])){
        int type=now->type[i];
        int hostile=gm_hostile(type), passive=gm_passive(type);
        if(nx->attack_time[i]>0)--nx->attack_time[i];
        double dx=px-now->x[i],dy=py-now->y[i],dz=pz-now->z[i];
        double d=sqrt(dx*dx+dy*dy+dz*dz), xz=sqrt(dx*dx+dz*dz);
        /* EntityLiving.despawnEntity: hostiles only, passives persist. */
        if(hostile){
            if(d>GM_MOB_DESPAWN_HARD){nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;}
            if(d>GM_MOB_DESPAWN_SOFT){
                if(++m->despawn_ticks[i]>=GM_MOB_DESPAWN_DELAY){
                    nx->alive[i]=0;nx->type[i]=EW_TYPE_NONE;continue;
                }
            }else m->despawn_ticks[i]=0;
        }
        /* Zombie/skeleton daylight burning. */
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
        /* Target acquisition: follow range + line of sight; spiders neutral in
         * daylight, endermen only retaliate (no look-trigger in this sim). */
        int aggro=0;
        if(hostile){
            int wants=1;
            if(type==EW_TYPE_ENDERMAN)wants=m->hurt_aggro[i];
            else if(type==EW_TYPE_SPIDER)wants=!day||m->hurt_aggro[i];
            if(wants&&d<=follow_range(type)){
                float mw,mh;ehs_size((u8)type,&mw,&mh);
                aggro=los_clear(w,now->x[i],now->y[i]+mh*0.85,now->z[i],
                                px,py+PSV_EYE_HEIGHT,pz);
            }
        }
        int moving=0,jump=0,wandering=0;
        int ranged=type==EW_TYPE_SKELETON||type==GM_MOB_BLAZE;
        if(aggro&&ranged){
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
        }else if(aggro&&xz<=GM_MOB_REACH&&fabs(dy)<3.0){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_ATTACK;nx->yaw[i]=ehs_yaw_toward(dx,dz);
            if(nx->attack_time[i]<=0){
                pv_attack(v,melee_damage(type));p->health=v->health;
                nx->attack_time[i]=20;
            }
        }else if(aggro){
            nx->path_tx[i]=px;nx->path_ty[i]=py;nx->path_tz[i]=pz;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_CHASE;moving=1;
            if(type==EW_TYPE_CREEPER&&m->creeper_fuse[i]>0)--m->creeper_fuse[i];
        }else if(passive&&m->panic_ticks[i]>0){
            /* EntityAIPanic: run away from the damage source (the player here). */
            --m->panic_ticks[i];
            double ux=xz>0.01?dx/xz:1.0, uz=xz>0.01?dz/xz:0.0;
            nx->path_tx[i]=now->x[i]-ux*8.0;nx->path_ty[i]=now->y[i];
            nx->path_tz[i]=now->z[i]-uz*8.0;nx->path_len[i]=0;
            nx->ai_state[i]=EW_AI_IDLE;moving=1;
        }else{
            /* EntityAIWander: deterministic nearby ground cell every ~120 ticks. */
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
        }
        if(moving){
            double mvx=nx->path_tx[i]-now->x[i],mvz=nx->path_tz[i]-now->z[i];
            double len=sqrt(mvx*mvx+mvz*mvz);
            if(len>0.01){
                int ax=mc_floor(now->x[i]+mvx/len*0.9),az=mc_floor(now->z[i]+mvz/len*0.9);
                int fy=mc_floor(now->y[i]);
                if(solid_id(gm_world_block(w,ax,fy,az))&&
                   !solid_id(gm_world_block(w,ax,fy+1,az))&&
                   !solid_id(gm_world_block(w,ax,fy+2,az)))jump=1;
                /* Wanderers refuse drops of more than 2 blocks. */
                else if(wandering&&!solid_id(gm_world_block(w,ax,fy,az))&&
                        !solid_id(gm_world_block(w,ax,fy-1,az))&&
                        !solid_id(gm_world_block(w,ax,fy-2,az))){
                    moving=0;nx->path_len[i]=0;
                }
            }
        }
        move_mob(w,st,nx,i,moving,jump);
    }
    tick_xp_orbs(m,w,p,ox,oz);
    ++m->tick;m->current^=1;
    (void)drops;
}

int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max) {
    if(!m||!out||max<=0)return 0;
    const EwStore *s=const_store(m);int n=0;
    for(int i=1;i<EW_MAX_ENTITIES&&n<max;++i)if(s->alive[i]&&gm_living(s->type[i])){
        out[n].type=s->type[i];out[n].x=(float)s->x[i];out[n].y=(float)s->y[i];
        out[n].z=(float)s->z[i];out[n].yaw=s->yaw[i];out[n].health=s->health[i];++n;
    }
    for(int i=0;i<GM_XP_ORBS&&n<max;++i){const McOrb *o=&m->xp_orbs[i];
        if(o->dead||o->xpValue<=0)continue;
        out[n++]=(GmEntityView){GM_ENTITY_XP_ORB,(float)o->posX,(float)o->posY,
                               (float)o->posZ,0.0f,(float)o->xpValue};
    }return n;
}

int gm_mobs_alive(const GmMobLive *m){return m?alive_count(const_store(m)):0;}

int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops){
    if(!m)return 0;
    EwStore *s=now_store(m);int best=-1;double bd=radius*radius;
    for(int i=1;i<EW_MAX_ENTITIES;++i)if(s->alive[i]&&gm_living(s->type[i])){
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
