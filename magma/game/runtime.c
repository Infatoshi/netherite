#include "game/runtime.h"
#include "game/randtick.h"
#include "game/sel_box.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crafting_recipes_full.h"
#include "game/portal_live.h"
#include "game/structures_live.h"
#include "game/world_spawn.h"
#include "explosion.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"

/* EntityLivingBase.applyArmorCalculations + InventoryPlayer.damageArmor. */
static float runtime_armor_damage(GmRuntime *r, float amount, int unblockable)
{
    ITAStack slots[4];
    if (!r || amount <= 0.0f) return amount;
    if (unblockable) return amount;
    for (int i = 0; i < 4; ++i) {
        ICStack s = isr_get_stack(&r->player.inv, ISR_ARMOR0 + i);
        slots[i] = ita_mk(s.item, s.meta);
        slots[i].count = s.count;
    }
    ita_damage_armor_set(slots, amount);
    for (int i = 0; i < 4; ++i) {
        if (slots[i].item <= 0 || slots[i].count <= 0)
            isr_set_stack(&r->player.inv, ISR_ARMOR0 + i, ic_empty());
        else {
            ICStack s = ic_mk(slots[i].item, 1, slots[i].damage);
            isr_set_stack(&r->player.inv, ISR_ARMOR0 + i, s);
        }
    }
    {
        ICStack chest = isr_get_stack(&r->player.inv, ISR_ARMOR_CHEST);
        if (chest.item == ISR_ELYTRA_ITEM)
            r->player.elytra_equipped = isr_elytra_usable(&chest);
    }
    return ita_apply_armor_absorb(amount, slots, 0);
}

static int isr_slot_ok(int slot)
{
    return (slot >= 0 && slot < ISR_MAIN_SLOTS) ||
           isr_is_armor_index(slot) ||
           slot == ISR_OFFHAND_SLOT;
}

static void sync_elytra_from_chest(GmRuntime *r)
{
    ICStack chest;
    if (!r) return;
    chest = isr_get_stack(&r->player.inv, ISR_ARMOR_CHEST);
    if (chest.item == ISR_ELYTRA_ITEM)
        r->player.elytra_equipped = isr_elytra_usable(&chest);
    else if (!isr_is_empty(&chest))
        r->player.elytra_equipped = 0;
    /* empty chest: leave set_elytra test-hook flag alone */
}

#define GM_RUNTIME_MAX_EDITS 8

static int floordiv16(int a) {
    return a >= 0 ? a >> 4 : -(((-a) + 15) >> 4);
}

/* WorldProvider.canCoordinateBeSpawn: mushroom biomes always ok, else
 * getGroundAboveSeaLevel == GRASS. */
static int runtime_can_spawn(void *ctx, int x, int z) {
    GmWorld *w = (GmWorld *)ctx;
    int y, biome;
    if (!w) return 0;
    gm_world_ensure(w, floordiv16(x), floordiv16(z), 0);
    biome = gm_world_biome(w, x, z);
    if (biome == 14 || biome == 15) return 1;
    y = 63;
    while (y < 255 && gm_world_block(w, x, y + 1, z) != 0) y++;
    return gm_world_block(w, x, y, z) == 2;
}

static int vanilla_blocks_movement(int id) {
    switch (id) {
    case 0: case 6: case 8: case 9: case 10: case 11:
    case 30: case 31: case 32: case 37: case 38: case 39: case 40:
    case 50: case 51: case 59: case 78: case 83: case 104: case 105:
    case 106: case 111: case 141: case 142: case 171: case 175:
        return 0;
    default:
        return 1;
    }
}

/* World.getTopSolidOrLiquidBlock: air cell above the topmost blocksMovement
 * non-leaf (isFoliage is inert for the vanilla 1.11.2 plant set). */
static int runtime_top_solid(GmWorld *w, int x, int z) {
    int y;
    for (y = 255; y >= 1; --y) {
        int id = gm_world_block(w, x, y - 1, z);
        if (vanilla_blocks_movement(id) && id != 18 && id != 161)
            return y;
    }
    return 64;
}

static void set_error(char *err, int cap, const char *msg) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", msg);
}

static void recenter(GmRuntime *r) {
    double wx = r->player.ent.posX + r->ox;
    double wz = r->player.ent.posZ + r->oz;
    int nccx = floordiv16((int)floor(wx));
    int nccz = floordiv16((int)floor(wz));
    if (nccx == r->ccx && nccz == r->ccz) return;
    double dx = (double)((nccx - r->ccx) * 16);
    double dz = (double)((nccz - r->ccz) * 16);
    r->ccx = nccx; r->ccz = nccz;
    r->ox = nccx * 16; r->oz = nccz * 16;
    r->player.ent.posX -= dx; r->player.ent.posZ -= dz;
    r->player.ent.box.minX -= dx; r->player.ent.box.maxX -= dx;
    r->player.ent.box.minZ -= dz; r->player.ent.box.maxZ -= dz;
    gm_player_ctl_recenter((int)dx, (int)dz);
}

static void runtime_explode(GmRuntime *r,double ex,double ey,double ez,float size){
    u16 grid[EX_VOL];u8 hit[EX_VOL];int ox=(int)floor(ex)-8,oy=(int)floor(ey)-8,oz=(int)floor(ez)-8;
    for(int x=0;x<EX_DIM;++x)for(int y=0;y<EX_DIM;++y)for(int z=0;z<EX_DIM;++z)
        grid[ex_idx(x,y,z)]=mc_state(gm_world_block(r->world,ox+x,oy+y,oz+z),
                                     gm_world_meta(r->world,ox+x,oy+y,oz+z));
    ex_do_explosion_blocks(grid,ex-ox,ey-oy,ez-oz,size,hit);
    for(int x=0;x<EX_DIM;++x)for(int y=0;y<EX_DIM;++y)for(int z=0;z<EX_DIM;++z)
        if(hit[ex_idx(x,y,z)]){
            gm_world_set_block(r->world,ox+x,oy+y,oz+z,0);
            gm_live_block_changed(&r->entities, r->world, ox+x, oy+y, oz+z);
            gm_fluid_mark(&r->fluids,r->world,r->dimension,ox+x,oy+y,oz+z);
        }
    GmPlayerView v;gm_runtime_view(r,&v);
    float damage=ex_entity_damage(v.x,v.y+v.eye_height,v.z,ex,ey,ez,size,1.0f);
    /* ExplosionDamage is not unblockable; armor absorb + durability apply. */
    damage = runtime_armor_damage(r, damage, 0);
    pv_attack(&r->vitals,damage);r->player.health=r->vitals.health;
    if(r->dimension==1&&r->dragon.initialized){
        EdDragon *d=&r->dragon.state.arena.dragon;
        float dd=ex_entity_damage(d->x,d->y+2,d->z,ex,ey,ez,size,1.0f);
        d->health-=dd;if(d->health<0)d->health=0;
        for(int i=0;i<ED_NUM_CRYSTALS;++i)if(r->dragon.state.arena.crystals[i].alive){
            EdCrystal *c=&r->dragon.state.arena.crystals[i];double dx=c->x-ex,dy=c->y-ey,dz=c->z-ez;
            if(dx*dx+dy*dy+dz*dz<=size*size*4.0)r->dragon.state.arena.crystals[i].alive=0;
        }
    }
}

/* Vanilla BlockBush.checkAndDropBlock on neighborChanged: after a block edit,
 * plants above the edit that lost their support break (dropping their item).
 * Mushrooms are exempt (vanilla lets them stay on any solid block). Walking up
 * cascades reed columns and double-plant tops. */
static int plant_support_ok(GmRuntime *r, int id, int meta, int below) {
    switch (id) {
    case 6: case 31: case 37: case 38: return below == 2 || below == 3 || below == 60;
    case 32:  return below == 3 || below == 12 || below == 159 || below == 172;
    case 83:  return below == 83 || below == 2 || below == 3 || below == 12;
    case 175: return meta >= 8 ? below == 175 : (below == 2 || below == 3);
    case 111: return below == 8 || below == 9;
    case 81:  return below == 12 || below == 81;
    default:  return 1;
    }
    (void)r;
}

/* item dropped by a broken plant (0 = nothing; vanilla grass seeds are a 1/8
 * roll we skip rather than diverge on RNG) */
static int plant_drop_item(int id, int meta) {
    switch (id) {
    case 6: case 37: case 38: return id;
    case 83:  return 338;                                  /* reeds item */
    case 81:  return 81;
    case 111: return 111;
    case 175: return meta >= 8 ? 0 : 0;                    /* tops drop nothing */
    default:  return 0;
    }
}

static void break_unsupported_plants(GmRuntime *r, int wx, int wy, int wz) {
    for (int y = wy + 1; y < 255; ++y) {
        int id = gm_world_block(r->world, wx, y, wz);
        int meta = gm_world_meta(r->world, wx, y, wz);
        int below = gm_world_block(r->world, wx, y - 1, wz);
        if (plant_support_ok(r, id, meta, below)) break;
        gm_world_set_block(r->world, wx, y, wz, 0);
        int drop = plant_drop_item(id, meta);
        if (drop > 0)
            gm_live_spawn_item(&r->entities, wx + 0.5, y + 0.5, wz + 0.5,
                               drop, 1, 0, 10);
    }
}

static int ray_axis(double start, double dir, double lo, double hi,
                    double *t0, double *t1) {
    if (fabs(dir) < 1.0e-12)
        return start >= lo && start <= hi;
    double a = (lo - start) / dir;
    double b = (hi - start) / dir;
    if (a > b) { double tmp = a; a = b; b = tmp; }
    if (a > *t0) *t0 = a;
    if (b < *t1) *t1 = b;
    return *t0 <= *t1;
}

/* Minecraft.getMouseOver resolves collidable entities before clickMouse.
 * Entity intercepts only when its AABB hit is strictly closer than the block
 * hit (EntityRenderer.getMouseOver d1 < d0). A falling AABB further along the
 * same ray must not steal a held creative attack from a closer re-landed
 * cell; that split the 151855Z digest across t46/t47. */
static int attack_hits_falling_block(const GmRuntime *r) {
    /* Entity.getCollisionBorderSize returns 0.0F (Entity.java:2366-2368).
     * expandXyz(0.1) steals the 151855Z ray one tick early (entity y~4.78)
     * and leaves blockHitDelay=1 on the re-land. 0.0F with clickBlock's
     * folded delay=4 extra-breaks the floor at t25; with delay=5 it does not. */
    const double border = 0.0;
    /* EntityRenderer.getMouseOver uses Entity.getVectorForRotation, whose
     * MathHelper float trig is shared with the block ray. A libm double ray
     * flips the grazing decision that controls blockHitDelay here. */
    float f = mc_cos(&r->sin_table,
                     -r->player.yaw * 0.017453292f - 3.1415927f);
    float f1 = mc_sin(&r->sin_table,
                      -r->player.yaw * 0.017453292f - 3.1415927f);
    float f2 = -mc_cos(&r->sin_table, -r->player.pitch * 0.017453292f);
    float f3 = mc_sin(&r->sin_table, -r->player.pitch * 0.017453292f);
    double dx = (double)(f1 * f2);
    double dy = (double)f3;
    double dz = (double)(f * f2);
    double sx = r->player.ent.posX + (double)r->ox;
    double sy = r->player.ent.posY + PSV_EYE_HEIGHT;
    double sz = r->player.ent.posZ + (double)r->oz;
    double t_block = PSV_REACH;
    if (r->window) {
        int hx, hy, hz, ax, ay, az;
        if (gm_raycast_sel_reach(r->window, &r->sin_table, &r->player,
                                 PSV_REACH, &hx, &hy, &hz,
                                 &ax, &ay, &az) >= 0) {
            float b[6];
            double t0 = 0.0, t1 = PSV_REACH;
            double ex = r->player.ent.posX;
            double ey = r->player.ent.posY + PSV_EYE_HEIGHT;
            double ez = r->player.ent.posZ;
            gm_sel_box_at(r->window, hx, hy, hz, b);
            if (ray_axis(ex, dx, hx + (double)b[0], hx + (double)b[3],
                         &t0, &t1) &&
                ray_axis(ey, dy, hy + (double)b[1], hy + (double)b[4],
                         &t0, &t1) &&
                ray_axis(ez, dz, hz + (double)b[2], hz + (double)b[5],
                         &t0, &t1) &&
                t0 >= 0.0 && t0 <= PSV_REACH)
                t_block = t0;
        }
    }
    double t_ent = t_block;
    int hit = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *e = &r->entities.ents[i];
        if (!e->active || e->type != 2) continue;
        /* The client spawn pose observed by getMouseOver is one constructor
         * half-height offset above the integrated-server simulation pose.
         * This is exactly (1 - EntityFallingBlock.height) / 2 for height .98;
         * using the server y flips a grazing ray and advances blockHitDelay. */
        double client_y = e->y + (double)((1.0f - 0.98f) / 2.0f);
        double t0 = 0.0, t1 = PSV_REACH;
        if (ray_axis(sx, dx, e->x - 0.49 - border,
                     e->x + 0.49 + border, &t0, &t1) &&
            ray_axis(sy, dy, client_y - border,
                     client_y + 0.98 + border, &t0, &t1) &&
            ray_axis(sz, dz, e->z - 0.49 - border,
                     e->z + 0.49 + border, &t0, &t1) &&
            t0 >= 0.0 && t0 < t_ent) {
            t_ent = t0;
            hit = 1;
        }
    }
    return hit;
}

static int take_arrow(PsvPlayer *p) {
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        if (isr_get_stack(&p->inv, i).item != 262) continue;
        (void)isr_decr_stack_size(&p->inv, i, 1);
        return 1;
    }
    return 0;
}

static void runtime_close_container(GmRuntime *r);
static void runtime_break_chest_te(GmRuntime *r, int wx, int wy, int wz);

static void spawn_bow_arrow(GmRuntime *r, int draw) {
    float f = (float)draw / 20.0f;
    f = (f * f + f * 2.0f) / 3.0f;
    if (f < 0.1f || !take_arrow(&r->player)) return;
    if (f > 1.0f) f = 1.0f;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (p->active) continue;
        double yr = r->player.yaw * MC_PI / 180.0;
        double pr = r->player.pitch * MC_PI / 180.0;
        double dx = -sin(yr) * cos(pr), dy = -sin(pr), dz = cos(yr) * cos(pr);
        p->active = 1; p->type = 1; p->age = 0;
        p->x = r->player.ent.posX + r->ox + dx * 0.2;
        p->y = r->player.ent.posY + PSV_EYE_HEIGHT + dy * 0.2;
        p->z = r->player.ent.posZ + r->oz + dz * 0.2;
        p->vx = dx * (double)(f * 3.0f);
        p->vy = dy * (double)(f * 3.0f);
        p->vz = dz * (double)(f * 3.0f);
        return;
    }
}

static void spawn_hostile_projectiles(GmRuntime *r) {
    const EwStore *s=r->mobs.current?&r->mobs.b:&r->mobs.a;
    GmPlayerView v;gm_runtime_view(r,&v);
    for(int j=1;j<EW_MAX_ENTITIES;++j){
        if (!s->alive[j]) continue;
        /* Skeleton: fire on the tick attack_time is reloaded (40). Blaze small
         * fireballs come from gm_mobs_take_fireball (AIFireballAttack pending). */
        if (s->type[j] != EW_TYPE_SKELETON || s->attack_time[j] != 40) continue;
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){
            GmRuntimeProjectile *p=&r->projectiles[i];if(p->active)continue;
            double sx=s->x[j],sy=s->y[j]+1.5,sz=s->z[j];
            double dx=v.x-sx,dy=(v.y+v.eye_height)-sy,dz=v.z-sz;
            double len=sqrt(dx*dx+dy*dy+dz*dz);if(len<0.001)break;
            p->active=1;p->type=2;p->age=0;
            p->x=sx;p->y=sy;p->z=sz;p->vx=dx/len*1.6;p->vy=dy/len*1.6;p->vz=dz/len*1.6;
            break;
        }
    }
    /* Blaze small (type 3) or ghast large (type 5) fireball pending. */
    {
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){
            GmRuntimeProjectile *p=&r->projectiles[i];if(p->active)continue;
            double x,y,z,vx,vy,vz;
            int kind=gm_mobs_take_fireball(&r->mobs,&x,&y,&z,&vx,&vy,&vz);
            if(kind){
                p->active=1;p->type=kind;p->age=0;
                p->x=x;p->y=y;p->z=z;p->vx=vx;p->vy=vy;p->vz=vz;
            }
            break;
        }
    }
}

static int throw_eye_of_ender(GmRuntime *r) {
    if(r->dimension!=0)return 0;
    ICStack held=isr_get_stack(&r->player.inv,r->player.inv.current_item);
    if(held.item!=381||held.count<=0)return 0;
    int sx,sz;if(!gm_stronghold_locate(r->seed,0,&sx,&sz))return 0;
    GmPlayerView v;gm_runtime_view(r,&v);
    double dx=(sx+0.5)-v.x,dz=(sz+0.5)-v.z,len=sqrt(dx*dx+dz*dz);
    if(len<0.001)return 0;
    for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){GmRuntimeProjectile *p=&r->projectiles[i];
        if(p->active)continue;
        p->active=1;p->type=4;p->age=0;p->x=v.x;p->y=v.y+v.eye_height;p->z=v.z;
        p->vx=dx/len*0.5;p->vy=0.25;p->vz=dz/len*0.5;
        (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);return 1;
    }
    return 0;
}

static void tick_projectiles(GmRuntime *r) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (!p->active) continue;
        if(p->type==4){
            p->x+=p->vx;p->y+=p->vy;p->z+=p->vz;++p->age;
            p->vx*=0.95;p->vz*=0.95;p->vy=(p->age<20)?p->vy*0.9:-0.03;
            if(p->age>=80){
                if((mc_hash64((u64)r->seed^(u64)r->tick^(u64)i)&4ULL)!=0)
                    gm_live_spawn_item(&r->entities,p->x,p->y,p->z,381,1,0,10);
                p->active=0;
            }
            continue;
        }
        double speed = sqrt(p->vx*p->vx + p->vy*p->vy + p->vz*p->vz);
        int steps = (int)ceil(speed / 0.25);
        if (steps < 1) steps = 1;
        float damage = (float)(speed * 2.0);
        for (int s = 0; s < steps && p->active; ++s) {
            p->x += p->vx / steps; p->y += p->vy / steps; p->z += p->vz / steps;
            int block=gm_world_block(r->world,(int)floor(p->x),(int)floor(p->y),(int)floor(p->z));
            if(p->type==1){
                if(block||gm_dragon_damage_near(&r->dragon,p->x,p->y,p->z,0.75,damage)||
                   gm_mobs_damage_near(&r->mobs,p->x,p->y,p->z,0.75,damage,&r->entities))p->active=0;
            }else{
                GmPlayerView v;gm_runtime_view(r,&v);
                double dx=p->x-v.x,dy=p->y-(v.y+0.9),dz=p->z-v.z;
                if(dx*dx+dy*dy+dz*dz<=0.75*0.75){
                    float dmg=p->type==5?6.0f:(p->type==3?5.0f:4.0f);
                    int hit=gm_mobs_attack_player(&r->mobs,
                        (struct PvStats *)&r->vitals, &r->player.inv,
                        dmg, 0);
                    r->player.health=r->vitals.health;
                    if(hit&&p->type==3)r->player_fire_ticks=5*20;
                    if(p->type==5)runtime_explode(r,p->x,p->y,p->z,1.0f);
                    p->active=0;
                }else if(block){
                    if(p->type==3||p->type==5)runtime_explode(r,p->x,p->y,p->z,1.0f);
                    p->active=0;
                }
            }
        }
        ++p->age;
        if (!p->active || p->age > 1200) { p->active = 0; continue; }
        if(p->type!=3&&p->type!=5)p->vy -= 0.05;
        p->vx *= 0.99; p->vy *= 0.99; p->vz *= 0.99;
    }
}

/* ======================= client sound seam =======================
 * A pure OUTPUT of the tick. Nothing below is ever read back by a simulation
 * decision and nothing here touches a seeded stream, so a build with audio
 * compiled out runs the identical simulation - that invariant is what the
 * audio=0 vs =1 digest control in docs/GATES.md proves. Sound identity,
 * volume and pitch ARE derived from simulation state, so they are deterministic
 * and comparable against the Java oracle. */

static void runtime_sound_event_append_delayed(
        GmRuntime *r, int sound, int category, int eid, int relative,
        double x, double y, double z, float volume, float pitch,
        int delay_ticks) {
    int index;
    if (!r || sound <= 0 || sound >= GM_SOUND_COUNT) return;
    if (r->sound_event_count < GM_RUNTIME_SOUND_EVENTS) {
        index = (r->sound_event_head + r->sound_event_count)
            % GM_RUNTIME_SOUND_EVENTS;
        ++r->sound_event_count;
    } else {
        /* Full ring: overwrite the oldest and account for it. A consumer sees
         * the gap as a jump in `seq` and reports it rather than going quiet. */
        index = r->sound_event_head;
        r->sound_event_head = (r->sound_event_head + 1)
            % GM_RUNTIME_SOUND_EVENTS;
        ++r->sound_event_dropped;
    }
    r->sound_events[index] = (GmRuntimeSoundEvent){
        r->sound_event_next_seq++, sound, category, eid, r->dimension,
        relative, delay_ticks, x, y, z, volume, pitch
    };
}

static void runtime_sound_event_append(
        GmRuntime *r, int sound, int category, int eid, int relative,
        double x, double y, double z, float volume, float pitch) {
    runtime_sound_event_append_delayed(
        r, sound, category, eid, relative,
        x, y, z, volume, pitch, 0);
}

/* Block.SoundType families in 1.11.2 registration order. The GM_SOUND_BLOCK_*
 * enum keeps break/place/hit in this same order, so a family index doubles as
 * an offset from the first sound of an action. */
enum {
    RUNTIME_BLOCK_SOUND_WOOD,
    RUNTIME_BLOCK_SOUND_GRAVEL,
    RUNTIME_BLOCK_SOUND_GRASS,
    RUNTIME_BLOCK_SOUND_STONE,
    RUNTIME_BLOCK_SOUND_METAL,
    RUNTIME_BLOCK_SOUND_GLASS,
    RUNTIME_BLOCK_SOUND_CLOTH,
    RUNTIME_BLOCK_SOUND_SAND,
    RUNTIME_BLOCK_SOUND_SNOW,
    RUNTIME_BLOCK_SOUND_LADDER,
    RUNTIME_BLOCK_SOUND_ANVIL,
    RUNTIME_BLOCK_SOUND_SLIME
};

/* Block.getSoundType for every registered 1.11.2 block id. STONE is the
 * registration default, so only the blocks that override it are listed.
 * Metadata is deliberately ignored: no vanilla 1.11.2 block returns a
 * state-dependent SoundType, and qrl.BlockBreakSoundGolden asserts that over
 * every valid state before the gate compares a single row per id. */
static int runtime_block_sound_family(int state_id) {
    int id, family = RUNTIME_BLOCK_SOUND_STONE;
    if (state_id < 0) return -1;
    id = state_id & 4095;
    if (id < 1 || (id > 234 && id != 255)) return -1;
    switch (id) {
    case 5: case 17: case 25: case 26: case 47: case 50: case 53:
    case 54: case 58: case 63: case 64: case 68: case 69: case 72:
    case 75: case 76: case 85: case 86: case 91: case 93: case 94:
    case 96: case 99: case 100: case 103: case 104: case 105: case 107:
    case 125: case 126: case 127: case 134: case 135: case 136:
    case 143: case 146: case 147: case 148: case 149: case 150:
    case 151: case 162: case 163: case 164: case 176: case 177:
    case 178: case 183: case 184: case 185: case 186: case 187:
    case 188: case 189: case 190: case 191: case 192: case 193:
    case 194: case 195: case 196: case 197: case 198: case 199:
    case 200: case 214:
        family = RUNTIME_BLOCK_SOUND_WOOD; break;
    case 3: case 13: case 60: case 82:
        family = RUNTIME_BLOCK_SOUND_GRAVEL; break;
    case 2: case 6: case 18: case 19: case 31: case 32: case 37:
    case 38: case 39: case 40: case 46: case 59: case 83: case 106:
    case 110: case 111: case 141: case 142: case 161: case 170:
    case 175: case 207: case 208:
        family = RUNTIME_BLOCK_SOUND_GRASS; break;
    case 27: case 28: case 41: case 42: case 52: case 57: case 66:
    case 71: case 101: case 133: case 152: case 154: case 157:
    case 167:
        family = RUNTIME_BLOCK_SOUND_METAL; break;
    case 20: case 79: case 89: case 90: case 95: case 102: case 120:
    case 123: case 124: case 160: case 169: case 174: case 212:
        family = RUNTIME_BLOCK_SOUND_GLASS; break;
    case 35: case 51: case 81: case 92: case 171:
        family = RUNTIME_BLOCK_SOUND_CLOTH; break;
    case 12: case 88:
        family = RUNTIME_BLOCK_SOUND_SAND; break;
    case 78: case 80:
        family = RUNTIME_BLOCK_SOUND_SNOW; break;
    case 65:
        family = RUNTIME_BLOCK_SOUND_LADDER; break;
    case 145:
        family = RUNTIME_BLOCK_SOUND_ANVIL; break;
    case 165:
        family = RUNTIME_BLOCK_SOUND_SLIME; break;
    default: break;
    }
    return family;
}

/* SoundType.volume is 1.0 for every family except ANVIL (0.3); SoundType.pitch
 * is 1.0 except METAL (1.5). The per-action arithmetic is vanilla's:
 * break/place use (volume+1)/2 and pitch*0.8, hit uses (volume+1)/8 and
 * pitch*0.5. Kept as float expressions so the result is bit-comparable with
 * Java's Float.floatToRawIntBits. */
static int runtime_block_sound(
        int state_id, int first_sound,
        float volume_divisor, float pitch_multiplier,
        int *sound, float *volume, float *pitch) {
    int family = runtime_block_sound_family(state_id);
    float type_volume, type_pitch;
    if (family < 0) return 0;
    if (sound) *sound = first_sound + family;
    type_volume = family == RUNTIME_BLOCK_SOUND_ANVIL ? 0.3F : 1.0F;
    type_pitch = family == RUNTIME_BLOCK_SOUND_METAL ? 1.5F : 1.0F;
    if (volume) *volume = (type_volume + 1.0F) / volume_divisor;
    if (pitch) *pitch = type_pitch * pitch_multiplier;
    return 1;
}

int gm_runtime_block_break_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_BREAK, 2.0F, 0.8F,
        sound, volume, pitch);
}

int gm_runtime_block_place_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_PLACE, 2.0F, 0.8F,
        sound, volume, pitch);
}

int gm_runtime_block_hit_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_HIT, 8.0F, 0.5F,
        sound, volume, pitch);
}

/* World.playEvent(2001) from PlayerControllerMP.onPlayerDestroyBlock: the
 * BROKEN state's SoundType at the block centre. */
static int runtime_block_break_audio_append(
        GmRuntime *r, int x, int y, int z, int state_id) {
    int sound;
    float volume, pitch;
    if (!r || !gm_runtime_block_break_sound(
                  state_id, &sound, &volume, &pitch))
        return 0;
    runtime_sound_event_append(
        r, sound, GM_SOUND_CATEGORY_BLOCKS, 0, 0,
        (double)x + 0.5, (double)y + 0.5, (double)z + 0.5,
        volume, pitch);
    return 1;
}

/* ItemBlock.onItemUse after a successful placement. Not a world event in
 * vanilla - the placing world calls playSound directly - so this must never
 * fabricate one. */
static int runtime_block_place_audio_append(
        GmRuntime *r, int x, int y, int z, int state_id) {
    int sound;
    float volume, pitch;
    if (!r || !gm_runtime_block_place_sound(
                  state_id, &sound, &volume, &pitch))
        return 0;
    runtime_sound_event_append(
        r, sound, GM_SOUND_CATEGORY_BLOCKS, 0, 0,
        (double)x + 0.5, (double)y + 0.5, (double)z + 0.5,
        volume, pitch);
    return 1;
}

/* PlayerControllerMP.onPlayerDamageBlock every fourth tick of a held dig. */
static int runtime_block_hit_audio_append(
        GmRuntime *r, int x, int y, int z, int state_id) {
    int sound;
    float volume, pitch;
    if (!r || !gm_runtime_block_hit_sound(
                  state_id, &sound, &volume, &pitch))
        return 0;
    runtime_sound_event_append(
        r, sound, GM_SOUND_CATEGORY_BLOCKS, 0, 0,
        (double)x + 0.5, (double)y + 0.5, (double)z + 0.5,
        volume, pitch);
    return 1;
}

int gm_runtime_block_break_audio_fixture(
        GmRuntime *r, int x, int y, int z, int state_id) {
    return runtime_block_break_audio_append(r, x, y, z, state_id);
}

int gm_runtime_block_place_audio_fixture(
        GmRuntime *r, int x, int y, int z, int state_id) {
    return runtime_block_place_audio_append(r, x, y, z, state_id);
}

int gm_runtime_sound_event_count(const GmRuntime *r) {
    return r ? r->sound_event_count : 0;
}

int gm_runtime_sound_event_get(
        const GmRuntime *r, int index, GmRuntimeSoundEvent *out) {
    int slot;
    if (!r || !out || index < 0 || index >= r->sound_event_count)
        return 0;
    slot = (r->sound_event_head + index) % GM_RUNTIME_SOUND_EVENTS;
    *out = r->sound_events[slot];
    return 1;
}

void gm_runtime_sound_events_clear(GmRuntime *r) {
    if (!r) return;
    r->sound_event_head = 0;
    r->sound_event_count = 0;
}

int gm_runtime_init(GmRuntime *r, const GmConfig *cfg, char *err, int err_cap) {
    if (!r || !cfg) { set_error(err, err_cap, "invalid runtime arguments"); return 0; }
    memset(r, 0, sizeof *r);
    r->tape_armor_points = -1;   /* no recorded armor total until a tape sets it */
    r->world = gm_world_create_type(cfg->seed, (int)cfg->world);
    if (!r->world) { set_error(err, err_cap, "gm_world_create failed"); return 0; }
    r->window = (Chunk *)calloc(PSV_NCHUNKS, sizeof(Chunk));
    if (!r->window) {
        gm_world_destroy(r->world); r->world = NULL;
        set_error(err, err_cap, "physics window allocation failed"); return 0;
    }
    mc_sin_table_init(&r->sin_table);
    r->worlds[1]=r->world;r->dimension=0;r->seed=cfg->seed;
    r->ccx = r->ccz = 0; r->ox = r->oz = 0;
    gm_world_ensure(r->world, 0, 0, 2);
    psv_player_init(&r->player);
    isr_init(&r->player.inv);
    r->player.inv.current_item = 0;
    gm_player_cursor_set(ic_empty());
    if (cfg->world == GM_WORLD_SUPERFLAT) {
        int surface = gm_world_surface_y(r->world, 8, 8);
        r->player.ent.posX = 8.5;
        r->player.ent.posY = (double)surface + 1.0;
        r->player.ent.posZ = 8.5;
        r->player.ent.box = psv_player_box(8.5, r->player.ent.posY, 8.5);
    } else {
        int sx, sy, sz, feet_y, nccx, nccz;
        double px, pz;
        if (!gm_create_spawn_position(cfg->seed, runtime_can_spawn, r->world,
                                      &sx, &sy, &sz)) {
            sx = 8; sy = 64; sz = 8;
        }
        nccx = floordiv16(sx);
        nccz = floordiv16(sz);
        r->ccx = nccx;
        r->ccz = nccz;
        r->ox = nccx * 16;
        r->oz = nccz * 16;
        gm_world_ensure(r->world, nccx, nccz, 2);
        feet_y = runtime_top_solid(r->world, sx, sz);
        (void)sy;
        px = (double)sx + 0.5;
        pz = (double)sz + 0.5;
        r->player.ent.posX = px - (double)r->ox;
        r->player.ent.posY = (double)feet_y;
        r->player.ent.posZ = pz - (double)r->oz;
        r->player.ent.box = psv_player_box(r->player.ent.posX, r->player.ent.posY,
                                           r->player.ent.posZ);
    }
    r->player.ent.onGround = 0;
    r->player.yaw = 180.0f; r->player.pitch = 0.0f;
    pv_init(&r->vitals);
    r->gamerules = mc_gamerules_default();
    r->gamerules.doDaylightCycle = cfg->daylight;
    gm_world_clock_init(&r->clock, cfg->seed);
    memset(&r->entities, 0, sizeof r->entities);
    gm_mobs_init(&r->mobs, cfg->seed);
    gm_fluid_init(&r->fluids);
    r->weather_enabled = cfg->weather;
    r->clock.freeze_daylight = !r->gamerules.doDaylightCycle;
    r->mobs_enabled = cfg->mobs;
    /* Live random ticks on by default; script.c clears this for tape replay. */
    r->randtick_enabled = 1;
    r->randtick_radius = cfg->view_distance > 0 ? cfg->view_distance : 2;
    r->active_furnace = -1;
    r->active_chest = -1;
    r->tape_boat_ride_id = -1;
    r->tape_boat_mount_pending = -1;
    r->chests_cap = GM_RUNTIME_CHESTS_INITIAL;
    r->chests = (GmRuntimeChest *)calloc((size_t)r->chests_cap, sizeof(GmRuntimeChest));
    if (!r->chests) {
        free(r->window);
        for (int i = 0; i < 3; ++i) if (r->worlds[i]) gm_world_destroy(r->worlds[i]);
        set_error(err, err_cap, "chest TE storage allocation failed");
        return 0;
    }
    for (int i = 0; i < 9; ++i) r->craft_grid[i] = ic_empty();
    return 1;
}

void gm_runtime_destroy(GmRuntime *r) {
    if (!r) return;
    free(r->window);
    free(r->chests);
    r->chests = NULL;
    r->chests_cap = 0;
    for(int i=0;i<3;++i)if(r->worlds[i])gm_world_destroy(r->worlds[i]);
    memset(r, 0, sizeof *r);
}

void gm_runtime_respawn(GmRuntime *r) {
    if (!r) return;
    r->dead = 0;
    r->death_screen_ticks = 0;
    r->quit_to_title = 0;
    r->vitals.health = 20.0f;
    r->player.health = 20.0f;
    r->player_fire_ticks = 0;
    r->mobs.player_hurt_resistant = 0;
    r->mobs.player_last_damage = 0.0f;
}

void gm_runtime_tick(GmRuntime *r, GmAction action) {
    if (!r || !r->world || r->won) return;
    r->te_x = r->player.ent.posX + (double)r->ox;
    r->te_y = r->player.ent.posY;
    r->te_z = r->player.ent.posZ + (double)r->oz;
    r->te_valid = 1;
    /* GuiGameOver is open: advance enableButtonsTimer and handle button clicks.
     * World/player physics stay frozen (vanilla doesGuiPauseGame=false but the
     * player entity is already dead; magma freezes the survival transition). */
    if (r->dead) {
        if (r->death_screen_ticks < 1000000)
            ++r->death_screen_ticks;
        /* enableButtonsTimer == 20 unlocks buttons (GuiGameOver.updateScreen). */
        if (action.death_click && r->death_screen_ticks >= 20) {
            if (action.death_button == 0) {
                gm_runtime_respawn(r);
            } else if (action.death_button == 1) {
                /* Title Screen: product has no main menu - end the episode. */
                r->quit_to_title = 1;
            }
        }
        return;
    }
    recenter(r);
    /* The recorded client sees the server's mount/dismount relationship on
     * the tick after right-click/sneak. ent_view has already supplied this
     * tick's authoritative boat pose before gm_runtime_tick is called. */
    if (r->tape_boat_dismount_pending) {
        r->tape_boat_ride_id = -1;
        r->tape_boat_mount_pending = -1;
        r->tape_boat_dismount_pending = 0;
    } else if (r->tape_boat_mount_pending >= 0 &&
               r->tape_boat.valid &&
               r->tape_boat.ent_id == r->tape_boat_mount_pending) {
        r->tape_boat_ride_id = r->tape_boat_mount_pending;
        r->tape_boat_mount_pending = -1;
        r->tape_boat_mount_message_ticks = 60;
    }
    ICStack held_now=isr_get_stack(&r->player.inv,r->player.inv.current_item);
    if(held_now.item==261&&action.use){r->bow_drawing=1;++r->bow_ticks;}
    else if(r->bow_drawing){spawn_bow_arrow(r,r->bow_ticks);r->bow_drawing=0;r->bow_ticks=0;}
    if(action.do_place&&held_now.item==381&&throw_eye_of_ender(r)){
        action.do_place=0;action.use=0;
    }
    /* ItemBoat: place on water/adjacent block (oak boat item id 333). */
    if(action.do_place&&held_now.item==333){
        int hx,hy,hz,ax,ay,az;
        if(gm_raycast_sel_reach(r->window,&r->sin_table,&r->player,PSV_REACH,
                                &hx,&hy,&hz,&ax,&ay,&az)>=0){
            double bx=ax+r->ox+0.5,by=ay+0.1,bz=az+r->oz+0.5;
            if(gm_mobs_place_boat(&r->mobs,bx,by,bz,r->player.yaw)>=0){
                (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);
                action.do_place=0;action.use=0;
            }
        }
    }
    /* Mount nearby boat on use when not already placing a block. A tape boat
     * is the client entity actually clicked; live play keeps the mob store
     * path. Defer the tape relationship by one tick for the server response. */
    if (action.use && !action.do_place && r->tape_boat.valid &&
        r->tape_boat_ride_id < 0 && r->tape_boat_mount_pending < 0) {
        double dx = r->tape_boat.x -
                    (r->player.ent.posX + (double)r->ox);
        double dy = r->tape_boat.y - r->player.ent.posY;
        double dz = r->tape_boat.z -
                    (r->player.ent.posZ + (double)r->oz);
        if (dx * dx + dy * dy + dz * dz < PSV_REACH * PSV_REACH)
            r->tape_boat_mount_pending = r->tape_boat.ent_id;
    } else if(action.use&&!action.do_place&&gm_mobs_boat_mount(&r->mobs,
               (struct PsvPlayer *)&r->player,r->ox,r->oz)){
        action.use=0;
    }
    /* Sneak dismounts boat. */
    if (action.sneak && r->tape_boat_ride_id >= 0)
        r->tape_boat_dismount_pending = 1;
    if(action.sneak&&gm_mobs_boat_riding(&r->mobs))
        gm_mobs_boat_dismount(&r->mobs,(struct PsvPlayer *)&r->player,r->ox,r->oz);
    /* Mounted: WASD drives the boat (EntityBoat.controlBoat); player walk is
     * suppressed so the hull is the only motion source. */
    float boat_fwd = 0.0f, boat_str = 0.0f;
    if (gm_mobs_boat_riding(&r->mobs)) {
        boat_fwd = action.forward;
        boat_str = action.strafe;
        action.forward = 0.0f;
        action.strafe = 0.0f;
    }
    if (r->container) {
        GmPlayerView cv; gm_runtime_view(r,&cv);
        double dx=(r->container_wx+0.5)-cv.x;
        double dy=(r->container_wy+0.5)-(cv.y+cv.eye_height);
        double dz=(r->container_wz+0.5)-cv.z;
        int id=gm_world_block(r->world,r->container_wx,r->container_wy,r->container_wz);
        int valid=r->container==1?id==58
                 :r->container==2?(id==61||id==62)
                 :r->container==3?id==54
                 :0;
        if (!valid || dx*dx+dy*dy+dz*dz>36.0) {
            if (r->container == 3 && r->active_chest >= 0)
                chest_live_close(&r->chests[r->active_chest].state);
            gm_container_close(r);
            r->container=0;r->active_furnace=-1;r->active_chest=-1;
        }
    }
    if (action.inv_click) {
        (void)gm_container_click(r, action.inv_slot, action.inv_button, action.inv_type);
        action.inv_click = 0;
    }
    /* Apply integrated-server landing packets before the client snapshots its
     * physics/raycast window for this tick. */
    gm_live_pre_player_tick(&r->entities, r->world);
    /* refill the physics window only when its contents can have changed:
     * recenter, dimension/world switch, or any block mutation since the last
     * fill. The unconditional refill dominated tape replay (94% of a
     * physics-only run in find_chunk/light_state). */
    {
        long long g = gm_world_block_gen(r->world);
        if (r->win_world != r->world || r->win_ccx != r->ccx ||
            r->win_ccz != r->ccz || r->win_gen != g) {
            gm_world_fill_window(r->world, r->ccx, r->ccz, (struct Chunk *)r->window);
            r->win_world = r->world; r->win_ccx = r->ccx; r->win_ccz = r->ccz;
            r->win_gen = g;
        }
    }
    if(action.do_place){
        int hx,hy,hz,ax,ay,az;
        if(gm_raycast_sel_reach(r->window,&r->sin_table,&r->player,PSV_REACH,
                                &hx,&hy,&hz,&ax,&ay,&az)>=0){
            int wx=hx+r->ox,wz=hz+r->oz,id=gm_world_block(r->world,wx,hy,wz);
            if((id==58||id==61||id==62||id==54||id==120||id==26)&&gm_runtime_use_block(r,wx,hy,wz)){
                action.do_place=0;action.use=0;
            }
        }
    }
    /* EntityLivingBase.onUpdate ages hurtResistantTime every tick even when
     * --mobs off suppresses natural mob AI during a tape replay. */
    gm_mobs_player_hurt_tick(&r->mobs);
    /* Entity.onEntityUpdate: burning is fire>0, damage lands while the
     * pre-decrement counter is divisible by 20, then the counter ages. */
    if(r->player_fire_ticks>0){
        /* DamageSource.ON_FIRE bypasses armor. */
        if(r->player_fire_ticks%20==0)
            (void)gm_mobs_attack_player(&r->mobs,
                (struct PvStats *)&r->vitals, &r->player.inv, 1.0f, 1);
        --r->player_fire_ticks;
        r->player.health=r->vitals.health;
    }
    /* Minecraft.runTick order: leftClickCounter-- then processKeyBinds
     * (clickMouse on press, sendClickBlockToController on hold). Entity
     * selection/damage is clickMouse's ENTITY branch and must share the
     * post-decrement gate. Keep the physical attack bit for gm_player_tick so
     * dig sees a held key (and only release clears the counter); mark
     * attack_entity so the dig machine takes the non-BLOCK reset path. */
    {
        int can_click = gm_player_left_click_allows(action.attack);
        if (can_click && r->dimension == 1 &&
            gm_dragon_player_attack(&r->dragon,
                (const struct PsvPlayer *)&r->player, r->ox, r->oz))
            action.attack_entity = 1;
        if (can_click && !action.attack_entity &&
            gm_mobs_player_attack(&r->mobs,
                (const struct PsvPlayer *)&r->player, r->ox, r->oz,
                &r->entities))
            action.attack_entity = 1;
        if (can_click && !action.attack_entity && attack_hits_falling_block(r))
            action.attack_entity = 1;
    }
    GmBlockEdit edits[GM_RUNTIME_MAX_EDITS];
    int n = 0;
    /* player_view lands before gm_runtime_tick for a tape row. Carry its
     * GameType into the ordinary PlayerControllerMP dig calculation; action
     * t is still consumed during row t, matching the recorder's post-tick
     * semantics. This is mode propagation, not a block-specific shortcut. */
    action.creative = r->tape_creative;
    gm_player_tick_gr((struct Chunk *)r->window,
                      (const struct McSinTable *)&r->sin_table,
                      (struct PsvPlayer *)&r->player,
                      (struct PvStats *)&r->vitals, &r->gamerules, action,
                      r->ox, 0, r->oz, edits, &n, GM_RUNTIME_MAX_EDITS);
    /* Minecraft pins this sentinel every runTick while a block GUI is open,
     * after attack-release handling would otherwise clear it. */
    if (r->container >= 1 && r->container <= 3)
        gm_player_set_gui_blocked(1);
    /* PlayerControllerMP.onPlayerDamageBlock hit sound, drained per tick. The
     * counter lives in player_ctl because that is where the dig cadence lives;
     * it is render/audio-only state and the RL snapshot excludes it. */
    {
        int hx, hy, hz, hstate;
        if (gm_player_take_dig_sound(&hx, &hy, &hz, &hstate))
            runtime_block_hit_audio_append(r, hx, hy, hz, hstate);
    }
    /* Ghost pushers (tape replay): EntityLivingBase.collideWithNearbyEntities
     * runs right after travel, queries the UNGROWN player bb (strict
     * intersects), and each hit applies Entity.applyEntityCollision - a
     * post-friction 0.05-scaled X/Z velocity nudge away from the pusher;
     * position changes only on the NEXT tick. Verbatim vanilla math. Found at
     * tape 20260712T055346Z t1471: oracle motion gained exactly
     * (-0.014510, -0.040214) from sheep 4504 at (56.4834, 124.2278) while
     * positions still matched (mobs=off replays had no pusher at all). */
    {
        const McAABB *pb = (const McAABB *)&r->player.ent.box;
        for (int i = 0; i < r->nghosts; ++i) {
            double gx = r->ghosts[i].x - (double)r->ox;
            double gy = r->ghosts[i].y;
            double gz = r->ghosts[i].z - (double)r->oz;
            double hw = r->ghosts[i].w * 0.5;
            if (!(pb->minX < gx + hw && pb->maxX > gx - hw &&
                  pb->minY < gy + r->ghosts[i].h && pb->maxY > gy &&
                  pb->minZ < gz + hw && pb->maxZ > gz - hw))
                continue;
            double d0 = r->player.ent.posX - gx;
            double d1 = r->player.ent.posZ - gz;
            double d2 = fabs(d0) > fabs(d1) ? fabs(d0) : fabs(d1);
            if (d2 >= 0.009999999776482582) {
                d2 = (double)(float)sqrt(d2);  /* MathHelper.sqrt is float */
                d0 /= d2; d1 /= d2;
                double d3 = 1.0 / d2;
                if (d3 > 1.0) d3 = 1.0;
                d0 *= d3; d1 *= d3;
                d0 *= 0.05000000074505806; d1 *= 0.05000000074505806;
                r->player.ent.motionX += d0;
                r->player.ent.motionZ += d1;
            }
        }
        r->nghosts = 0;
    }
    for (int i = 0; i < n; ++i) {
        int old_id = gm_world_block(r->world, edits[i].wx, edits[i].wy, edits[i].wz);
        /* World event 2001 carries the state that was BROKEN, so it has to be
         * sampled before the cell is overwritten below. */
        if (edits[i].break_effect)
            runtime_block_break_audio_append(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                old_id | ((gm_world_meta(r->world, edits[i].wx, edits[i].wy,
                                         edits[i].wz) & 255) << 12));
        if (edits[i].place_effect)
            runtime_block_place_audio_append(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].id | ((edits[i].meta & 255) << 12));
        if (old_id == 54 && edits[i].id != 54)
            runtime_break_chest_te(r, edits[i].wx, edits[i].wy, edits[i].wz);
        gm_world_set_block_meta(r->world, edits[i].wx, edits[i].wy, edits[i].wz,
                                edits[i].id, edits[i].meta);
        gm_live_block_changed(&r->entities, r->world,
                              edits[i].wx, edits[i].wy, edits[i].wz);
        gm_fluid_mark(&r->fluids, r->world, r->dimension,
                      edits[i].wx, edits[i].wy, edits[i].wz);
        break_unsupported_plants(r, edits[i].wx, edits[i].wy, edits[i].wz);
        if(edits[i].id==8&&r->dimension==-1){
            gm_world_set_block(r->world,edits[i].wx,edits[i].wy,edits[i].wz,0);
        }else if(edits[i].id==8||edits[i].id==10){
            static const int dx[6]={1,-1,0,0,0,0},dy[6]={0,0,1,-1,0,0},dz[6]={0,0,0,0,1,-1};
            for(int q=0;q<6;++q){int x=edits[i].wx+dx[q],y=edits[i].wy+dy[q],z=edits[i].wz+dz[q];
                int id=gm_world_block(r->world,x,y,z);
                if(edits[i].id==8&&(id==10||id==11))
                    gm_world_set_block(r->world,x,y,z,gm_world_meta(r->world,x,y,z)==0?49:4);
                else if(edits[i].id==10&&(id==8||id==9))
                    gm_world_set_block(r->world,edits[i].wx,edits[i].wy,edits[i].wz,49);
            }
        }
        if(edits[i].id==51)gm_portal_ignite(r->world,edits[i].wx,edits[i].wy,edits[i].wz);
        if (edits[i].drop_id > 0)
            gm_live_spawn_item(&r->entities, edits[i].wx + 0.5, edits[i].wy + 0.5,
                               edits[i].wz + 0.5, edits[i].drop_id,
                               edits[i].drop_count, edits[i].drop_meta, 10);
    }
    if (r->weather_enabled) gm_world_tick(&r->clock);
    else gm_world_tick_clear(&r->clock);
    gm_fluid_tick(&r->fluids, r->world, r->dimension, r->tick);
    /* Random block ticks: LIVE/WINDOW only (r->randtick_enabled). Replay keeps
     * this off; do not approximate Java's unseedable world RNG on tapes. */
    if (r->randtick_enabled)
        gm_randtick_pass(r->world, r->seed, r->tick, r->ccx, r->ccz,
                         r->randtick_radius, &r->gamerules);
    if (r->mobs_enabled) {
        gm_mobs_tick(&r->mobs,r->world,(const struct McSinTable *)&r->sin_table,
                     (struct PsvPlayer *)&r->player,(struct PvStats *)&r->vitals,
                     r->ox,r->oz,r->dimension,r->clock.world_time,&r->entities,
                     boat_fwd, boat_str, r->gamerules.mobGriefing);
        {double x,y,z;if(gm_mobs_take_explosion(&r->mobs,&x,&y,&z))runtime_explode(r,x,y,z,3.0f);}
        spawn_hostile_projectiles(r);
    }
    if(r->dimension==1){
        GmPlayerView dv;gm_runtime_view(r,&dv);
        if(gm_dragon_tick(&r->dragon,r->world,(const struct McSinTable *)&r->sin_table,
                          dv.x,dv.y,dv.z))
            gm_mobs_spawn_xp(&r->mobs,0.5,65.5,0.5,12000);
    }
    tick_projectiles(r);
    gm_live_tick_player(&r->entities, r->world,
                        (struct PsvPlayer *)&r->player, r->ox, r->oz);
    /* Entity.updateRidden runs the player's ordinary living update (which
     * retains the recorded motion fields), then the boat updates the passenger
     * position. The entity stream is authoritative for the client boat pose,
     * avoiding a second, packet-incomplete boat simulation in tape replay. */
    if (r->tape_boat_ride_id >= 0 && r->tape_boat.valid &&
        r->tape_boat.ent_id == r->tape_boat_ride_id) {
        int left = action.strafe < -0.01f;
        int right = action.strafe > 0.01f;
        int paddle_forward = action.forward > 0.01f;
        int paddle[2] = { (right && !left) || paddle_forward,
                          (left && !right) || paddle_forward };
        for (int p = 0; p < 2; ++p)
            r->tape_boat_paddle[p] =
                paddle[p] ? r->tape_boat_paddle[p] + 0.01f : 0.0f;
        for (int i = 0; i < r->nghost_views; ++i)
            if (r->ghost_views[i].type == EW_TYPE_BOAT &&
                r->ghost_views[i].ent_id == r->tape_boat_ride_id) {
                r->ghost_views[i].boat_paddle[0] = r->tape_boat_paddle[0];
                r->ghost_views[i].boat_paddle[1] = r->tape_boat_paddle[1];
            }
        r->player.ent.posX = r->tape_boat.x - (double)r->ox;
        r->player.ent.posY =
            r->tape_boat.y - 0.44999998807907104;
        r->player.ent.posZ = r->tape_boat.z - (double)r->oz;
        /* Entity.updateRidden zeroes passenger velocity, then
         * EntityLivingBase.onLivingUpdate performs one unobstructed air
         * travel step before EntityBoat.updatePassenger replaces position.
         * Preserve that independently observable motion state. */
        float forward = action.forward * 0.98f;
        float strafe = -action.strafe * 0.98f;
        float move = strafe * strafe + forward * forward;
        if (move >= 1.0e-4f) {
            move = sqrtf(move);
            if (move < 1.0f) move = 1.0f;
            move = 0.02f / move;
            strafe *= move;
            forward *= move;
        } else {
            strafe = forward = 0.0f;
        }
        /* Boat.updatePassenger applies deltaRotation after the passenger's
         * living update, so the row's final player yaw is one boat-yaw step
         * newer than the yaw which produced these motion fields. */
        float motion_yaw = r->tape_boat_prev_yaw_valid
            ? (float)r->tape_boat_prev_yaw : r->player.yaw;
        float yaw = motion_yaw * 0.017453292f;
        float sy = mc_sin(&r->sin_table, yaw);
        float cy = mc_cos(&r->sin_table, yaw);
        r->player.ent.motionX =
            (double)(strafe * cy - forward * sy) * 0.9100000262260437;
        r->player.ent.motionY =
            -0.08 * 0.9800000190734863;
        r->player.ent.motionZ =
            (double)(forward * cy + strafe * sy) * 0.9100000262260437;
        r->player.ent.onGround = 0;
        r->player.ent.box = psv_player_box(r->player.ent.posX,
                                           r->player.ent.posY,
                                           r->player.ent.posZ);
        r->te_x = r->tape_boat.x;
        r->te_y = r->player.ent.posY;
        r->te_z = r->tape_boat.z;
    }
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) if (r->furnaces[i].active) {
        GmRuntimeFurnace *f = &r->furnaces[i];
        int was_lit = f->state.burn_time > 0;
        furnace_live_tick(&f->state);
        int lit = f->state.burn_time > 0;
        if (lit != was_lit) {
            int id = gm_world_block(r->world,f->wx,f->wy,f->wz);
            if (id == 61 || id == 62)
                gm_world_set_block_meta(r->world,f->wx,f->wy,f->wz,lit?62:61,
                                        gm_world_meta(r->world,f->wx,f->wy,f->wz));
        }
    }
    for (int i = 0; i < r->chests_cap; ++i)
        if (r->chests && r->chests[i].active) chest_live_tick(&r->chests[i].state);
    if (r->vitals.health <= 0.0f && !r->dead) {
        r->dead = 1;
        r->deaths++;
        r->death_screen_ticks = 0;
        r->quit_to_title = 0;
    }
    if(r->portal_cooldown>0)--r->portal_cooldown;
    int feet=gm_world_block(r->world,(int)floor(r->player.ent.posX+r->ox),
                            (int)floor(r->player.ent.posY),(int)floor(r->player.ent.posZ+r->oz));
    int head=gm_world_block(r->world,(int)floor(r->player.ent.posX+r->ox),
                            (int)floor(r->player.ent.posY+1.0),(int)floor(r->player.ent.posZ+r->oz));
    /* vanilla Entity.setPortal: while a cooldown is pending, every in-pane
     * collision REFRESHES it - standing inside the arrival portal never
     * re-arms the transit (walked-return tape 101755Z re-transited at +88). */
    if((feet==90||head==90)&&r->portal_cooldown>0)r->portal_cooldown=100;
    if((feet==119||head==119)&&r->dimension==1&&r->dragon.state.death_processed){
        r->credits=1;r->won=1;
    }else if((feet==119||head==119)&&r->dimension==0){
        if(!r->worlds[2])r->worlds[2]=gm_world_create_type(r->seed,3);
        if(r->worlds[2]){
            r->world=r->worlds[2];r->dimension=1;
            gm_world_ensure(r->world,6,0,1);
            for(int x=98;x<=102;++x)for(int z=-2;z<=2;++z){
                gm_world_set_block(r->world,x,48,z,49);
                for(int y=49;y<=51;++y)gm_world_set_block(r->world,x,y,z,0);
            }
            gm_runtime_set_pose(r,100.5,49.0,0.5,90.0f,0.0f);
            gm_mobs_init(&r->mobs,r->seed^1LL);memset(&r->entities,0,sizeof r->entities);
            gm_dragon_init(&r->dragon,r->world,r->seed);
            r->portal_time=0;r->portal_cooldown=100;
        }
    }else if((feet==90||head==90)&&r->portal_cooldown==0&&(r->dimension==0||r->dimension==-1)){
        /* The integrated server transfers at 80 contacts; the client-visible
         * dimension changes on the following tick (seed-0 natural portal tape:
         * first contact t158, ramp reaches 1.0 at t237, dim changes at t238). */
        if(++r->portal_time>=82){
            GmPlayerView v;gm_runtime_view(r,&v);
            int nd=r->dimension==0?-1:0,wi=nd+1;
            if(!r->worlds[wi])r->worlds[wi]=gm_world_create_type(r->seed,nd==-1?2:0);
            if(r->worlds[wi]){
                double scale=nd==-1?0.125:8.0,tx,ty,tz;
                int nx=(int)floor(v.x*scale),nz=(int)floor(v.z*scale);
                if(gm_portal_find_or_make(r->worlds[wi],nx,nz,&tx,&ty,&tz)){
                    r->world=r->worlds[wi];r->dimension=nd;
                    gm_runtime_set_pose(r,tx,ty,tz,v.yaw,v.pitch);
                    gm_mobs_init(&r->mobs,r->seed^(long long)nd);
                    memset(&r->entities,0,sizeof r->entities);
                    r->portal_cooldown=100;r->portal_time=0;
                }
            }
        }
    }else if(feet!=90&&head!=90)r->portal_time=0;
    if (r->tape_boat_mount_message_ticks > 0)
        r->tape_boat_mount_message_ticks--;
    r->tick++;
}

void gm_runtime_tick_entry_feet(const GmRuntime *r,
                                double *x, double *y, double *z) {
    if (!r) return;
    if (r->te_valid) { *x = r->te_x; *y = r->te_y; *z = r->te_z; return; }
    *x = r->player.ent.posX + (double)r->ox;
    *y = r->player.ent.posY;
    *z = r->player.ent.posZ + (double)r->oz;
}

void gm_runtime_view(const GmRuntime *r, GmPlayerView *out) {
    gm_player_view((const struct PsvPlayer *)&r->player, r->ox, r->oz, out);
    out->dead = r->dead;
    out->deaths = r->deaths;
    out->score = r->score;
    out->death_ticks = r->death_screen_ticks;
    out->fire = r->player_fire_ticks > 0;
    out->creative = 0;
    out->hurt_time = r->mobs.player_hurt_resistant > 0
        ? (r->mobs.player_hurt_resistant < 10
           ? r->mobs.player_hurt_resistant : 10) : 0;
    out->max_hurt_time = 10;
    out->hurt_yaw = 0.0f;
    out->attack_cooldown = 1.0f;
    out->potion_count = 0;
    out->riding_boat = r->tape_boat_ride_id >= 0 ||
                       gm_mobs_boat_riding(&r->mobs);
    out->mount_message_ticks = r->tape_boat_mount_message_ticks;
    int xp=r->mobs.xp_total, level=0;
    for (;;) {
        int cap=level>=30?9*level-158:(level>=15?5*level-38:2*level+7);
        if (xp<cap) {out->xp_level=level;out->xp_frac=cap?(float)xp/(float)cap:0.0f;break;}
        xp-=cap;++level;
    }
}

void gm_runtime_set_pose(GmRuntime *r, double x, double y, double z,
                         float yaw, float pitch) {
    if (!r) return;
    r->ccx = floordiv16((int)floor(x));
    r->ccz = floordiv16((int)floor(z));
    r->ox = r->ccx * 16; r->oz = r->ccz * 16;
    r->player.ent.posX = x - r->ox;
    r->player.ent.posY = y;
    r->player.ent.posZ = z - r->oz;
    r->player.ent.box = psv_player_box(r->player.ent.posX, y, r->player.ent.posZ);
    r->player.ent.motionX = r->player.ent.motionY = r->player.ent.motionZ = 0.0;
    r->player.ent.onGround = 0;
    r->player.fall_distance = 0.0f;
    r->player.yaw = yaw; r->player.pitch = pitch;
    if (r->container == 3 && r->active_chest >= 0)
        chest_live_close(&r->chests[r->active_chest].state);
    gm_container_close(r);
    r->container=0; r->active_furnace=-1; r->active_chest=-1;
    gm_player_dig_reset();
}

void gm_runtime_set_velocity(GmRuntime *r, double x, double y, double z) {
    if (!r) return;
    r->player.ent.motionX=x; r->player.ent.motionY=y; r->player.ent.motionZ=z;
}

void gm_runtime_set_pose_state(GmRuntime *r, double x, double y, double z,
                               float yaw, float pitch, double vx, double vy,
                               double vz, int on_ground, float fall_distance) {
    if (!r) return;
    gm_runtime_set_pose(r,x,y,z,yaw,pitch);
    gm_runtime_set_velocity(r,vx,vy,vz);
    r->player.ent.onGround=on_ground?1:0;
    r->player.fall_distance=fall_distance;
}

void gm_runtime_set_packet_velocity(GmRuntime *r, double x, double y, double z) {
    if (!r) return;
    gm_player_set_packet_velocity((struct PsvPlayer *)&r->player,x,y,z);
}

void gm_runtime_add_velocity(GmRuntime *r, double x, double y, double z) {
    /* SPacketExplosion knockback: handleExplosion adds the packet motion to
     * the local player's current motion (it does not replace it). */
    if (!r) return;
    r->player.ent.motionX += x;
    r->player.ent.motionY += y;
    r->player.ent.motionZ += z;
}

void gm_runtime_set_elytra(GmRuntime *r, int equipped) {
    /* Narrow replay/test hook. Normal play derives flight from chest item 443. */
    if (!r) return;
    r->player.elytra_equipped = equipped ? 1 : 0;
}

void gm_runtime_set_elytra_flag7(GmRuntime *r, int flying) {
    if (!r) return;
    r->player.elytra_flag7_recorded = 1;
    r->player.elytra_flying = flying ? 1 : 0;
    r->player.elytra_flying_pending = 0;
}

void gm_runtime_ent_box(GmRuntime *r, double x, double y, double z,
                        double w, double h) {
    if (!r || r->nghosts >= GM_RUNTIME_GHOSTS) return;
    r->ghosts[r->nghosts].x = x;
    r->ghosts[r->nghosts].y = y;
    r->ghosts[r->nghosts].z = z;
    r->ghosts[r->nghosts].w = w;
    r->ghosts[r->nghosts].h = h;
    r->nghosts++;
}

int gm_runtime_dragon_contact(GmRuntime *r, double min_x, double min_y,
                              double min_z, double max_x, double max_y,
                              double max_z, float damage) {
    if (!r || damage <= 0.0f || min_x >= max_x || min_y >= max_y ||
        min_z >= max_z) return 0;
    const McAABB *pb=(const McAABB *)&r->player.ent.box;
    double lx0=min_x-r->ox,lx1=max_x-r->ox;
    double lz0=min_z-r->oz,lz1=max_z-r->oz;
    if (!(pb->minX < lx1 && pb->maxX > lx0 && pb->minY < max_y &&
          pb->maxY > min_y && pb->minZ < lz1 && pb->maxZ > lz0)) return 0;
    /* Dragon part contact uses causeMobDamage-style path: armor applies. */
    int hit=gm_mobs_attack_player(&r->mobs,(struct PvStats *)&r->vitals,
                                 &r->player.inv, damage, 0);
    r->player.health=r->vitals.health;
    return hit;
}

/* Per-entity continuity for tape ghost render pose (hurt flash + limb swing).
 * Keyed by tape entity id. Render-only; never touches physics. */
#define GM_ENT_ANIM_CAP 64
typedef struct {
    int   id;
    float x, y, z;
    float hp;
    float limb_swing;
    float limb_amount;
    int   hurt_time;
    int   creeper_fuse;
    int   creeper_primed;
    int   used;
} GmEntAnim;
static GmEntAnim g_ent_anim[GM_ENT_ANIM_CAP];

static GmEntAnim *ent_anim_get(int id) {
    if (id < 0) return 0;
    for (int i = 0; i < GM_ENT_ANIM_CAP; ++i)
        if (g_ent_anim[i].used && g_ent_anim[i].id == id) return &g_ent_anim[i];
    for (int i = 0; i < GM_ENT_ANIM_CAP; ++i)
        if (!g_ent_anim[i].used) {
            memset(&g_ent_anim[i], 0, sizeof g_ent_anim[i]);
            g_ent_anim[i].id = id;
            g_ent_anim[i].used = 1;
            g_ent_anim[i].hp = -1.f;
            return &g_ent_anim[i];
        }
    return 0;
}

/* Renderable ghost entities (tape replay, divergence #10): render-only pose
 * records for this tick's frame capture. Never read by gm_runtime_tick.
 * Tracks hurtTime (hp drop -> 10 tick red flash) and limbSwing from position
 * deltas (ModelQuadruped / ModelBiped setRotationAngles). */
void gm_runtime_ent_view(GmRuntime *r, const GmEntityView *view) {
    if (!r || !view || r->nghost_views >= GM_RUNTIME_GHOST_VIEWS) return;
    GmEntityView *v = &r->ghost_views[r->nghost_views++];
    *v = *view;
    GmEntAnim *a = ent_anim_get(view->ent_id);
    if (a) {
        if (!view->tape_pose) {
            if (a->hp >= 0.f && view->health >= 0.f && view->health < a->hp - 1e-4f)
                a->hurt_time = 10;  /* legacy tape inference */
            else if (a->hurt_time > 0)
                a->hurt_time--;
            v->hurt_time = a->hurt_time;
        }
        /* EntityLivingBase.onLivingUpdate limbSwing integrate */
        float dx = view->x - a->x, dz = view->z - a->z;
        float dist = sqrtf(dx * dx + dz * dz) * 4.0f;
        if (dist > 1.0f) dist = 1.0f;
        if (a->hp >= 0.f) {  /* skip first sighting (no prev pos) */
            a->limb_amount += (dist - a->limb_amount) * 0.4f;
            a->limb_swing += a->limb_amount;
        }
        a->x = view->x; a->y = view->y; a->z = view->z;
        a->hp = view->health;
        v->limb_swing = a->limb_swing;
        v->limb_swing_amount = a->limb_amount;
        if (view->type == EW_TYPE_CREEPER && view->creeper_fuse <= 0) {
            double dxp = (double)view->x -
                         (r->player.ent.posX + (double)r->ox);
            double dzp = (double)view->z -
                         (r->player.ent.posZ + (double)r->oz);
            double engage_sq = a->creeper_fuse > 0 ? 49.0 : 9.0;
            if (dxp * dxp + dzp * dzp < engage_sq) {
                /* EntityAICreeperSwell changes the synced state first; the
                 * following EntityCreeper.onUpdate advances the fuse. A tape
                 * first observes that state transition one frame before its
                 * first non-zero flash intensity. */
                if (a->creeper_primed) {
                    if (a->creeper_fuse < 30) a->creeper_fuse++;
                } else {
                    a->creeper_primed = 1;
                }
            } else if (a->creeper_fuse > 0) {
                a->creeper_fuse--;
                if (a->creeper_fuse == 0) a->creeper_primed = 0;
            } else {
                a->creeper_primed = 0;
            }
            v->creeper_fuse = a->creeper_fuse;
        }
    }
}

void gm_runtime_tape_boat_view(GmRuntime *r, int ent_id, double x, double y,
                               double z, double yaw) {
    if (!r || ent_id < 0) return;
    /* While riding, never switch to another nearby boat. On foot select the
     * closest recorded boat so right-click resolves like the client ray hit. */
    if (r->tape_boat.valid && r->tape_boat_ride_id != ent_id) {
        if (r->tape_boat_ride_id >= 0) return;
        double px = r->player.ent.posX + (double)r->ox;
        double pz = r->player.ent.posZ + (double)r->oz;
        double old_dx = r->tape_boat.x - px;
        double old_dy = r->tape_boat.y - r->player.ent.posY;
        double old_dz = r->tape_boat.z - pz;
        double new_dx = x - px;
        double new_dy = y - r->player.ent.posY;
        double new_dz = z - pz;
        if (old_dx * old_dx + old_dy * old_dy + old_dz * old_dz <=
            new_dx * new_dx + new_dy * new_dy + new_dz * new_dz)
            return;
    }
    r->tape_boat.valid = 1;
    r->tape_boat.ent_id = ent_id;
    r->tape_boat.x = x;
    r->tape_boat.y = y;
    r->tape_boat.z = z;
    r->tape_boat.yaw = yaw;
}

void gm_runtime_ent_views_clear(GmRuntime *r) {
    if (!r) return;
    if (r->tape_boat.valid) {
        r->tape_boat_prev_yaw = r->tape_boat.yaw;
        r->tape_boat_prev_yaw_valid = 1;
    }
    r->tape_boat.valid = 0;

    for (int i = 0; i < GM_RUNTIME_FIREBALL_TRACKS; ++i)
        if (r->tape_fireball_impacts[i].active &&
            ++r->tape_fireball_impacts[i].age >= 2)
            r->tape_fireball_impacts[i].active = 0;

    struct { int ent_id; float x, y, z, dx, dy, dz; }
        current[GM_RUNTIME_FIREBALL_TRACKS];
    int ncurrent = 0;
    for (int i = 0; i < r->nghost_views &&
                    ncurrent < GM_RUNTIME_FIREBALL_TRACKS; ++i) {
        const GmEntityView *v = &r->ghost_views[i];
        if (v->type != GM_VIEW_DRAGON_FIREBALL || v->item_id != 385 ||
            v->item_meta < 2)
            continue;
        current[ncurrent].ent_id = v->ent_id;
        current[ncurrent].x = v->x;
        current[ncurrent].y = v->y;
        current[ncurrent].z = v->z;
        current[ncurrent].dx = 0.0f;
        current[ncurrent].dy = 0.0f;
        current[ncurrent].dz = 0.0f;
        for (int j = 0; j < r->ntape_large_fireballs; ++j)
            if (r->tape_large_fireballs[j].ent_id == v->ent_id) {
                current[ncurrent].dx = v->x - r->tape_large_fireballs[j].x;
                current[ncurrent].dy = v->y - r->tape_large_fireballs[j].y;
                current[ncurrent].dz = v->z - r->tape_large_fireballs[j].z;
                break;
            }
        ncurrent++;
    }

    for (int i = 0; i < r->ntape_large_fireballs; ++i) {
        int present = 0;
        for (int j = 0; j < ncurrent; ++j)
            if (current[j].ent_id == r->tape_large_fireballs[i].ent_id) {
                present = 1;
                break;
            }
        if (present) continue;

        /* The recorder keeps only its nearest entity window. A distant
         * disappearance may only mean it fell outside that window; infer an
         * impact only where the last position could affect the player view. */
        double dx = (double)r->tape_large_fireballs[i].x -
                    (r->player.ent.posX + r->ox);
        double dy = (double)r->tape_large_fireballs[i].y -
                    r->player.ent.posY;
        double dz = (double)r->tape_large_fireballs[i].z -
                    (r->player.ent.posZ + r->oz);
        if (dx * dx + dy * dy + dz * dz > 64.0 ||
            r->tape_hurt_time <= 0)
            continue;
        for (int j = 0; j < GM_RUNTIME_FIREBALL_TRACKS; ++j)
            if (!r->tape_fireball_impacts[j].active) {
                r->tape_fireball_impacts[j].active = 1;
                r->tape_fireball_impacts[j].ent_id =
                    r->tape_large_fireballs[i].ent_id;
                r->tape_fireball_impacts[j].age = 1;
                /* The removal packet reaches the client after the server-side
                 * collision. Rewind the last client velocity by two samples
                 * to recover the impact-side position instead of drawing the
                 * puff behind the camera at the extrapolated removal pose. */
                r->tape_fireball_impacts[j].x =
                    r->tape_large_fireballs[i].x -
                    2.0f * r->tape_large_fireballs[i].dx;
                r->tape_fireball_impacts[j].y =
                    r->tape_large_fireballs[i].y -
                    2.0f * r->tape_large_fireballs[i].dy;
                r->tape_fireball_impacts[j].z =
                    r->tape_large_fireballs[i].z -
                    2.0f * r->tape_large_fireballs[i].dz;
                break;
            }
    }

    r->ntape_large_fireballs = ncurrent;
    for (int i = 0; i < ncurrent; ++i) {
        r->tape_large_fireballs[i].ent_id = current[i].ent_id;
        r->tape_large_fireballs[i].x = current[i].x;
        r->tape_large_fireballs[i].y = current[i].y;
        r->tape_large_fireballs[i].z = current[i].z;
        r->tape_large_fireballs[i].dx = current[i].dx;
        r->tape_large_fireballs[i].dy = current[i].dy;
        r->tape_large_fireballs[i].dz = current[i].dz;
    }
    r->nghost_views = 0;
}

int gm_runtime_ghost_views(const GmRuntime *r, GmEntityView *out, int max) {
    if (!r) return 0;
    int n = r->nghost_views < max ? r->nghost_views : max;
    for (int i = 0; i < n; ++i) out[i] = r->ghost_views[i];
    for (int i = 0; i < GM_RUNTIME_FIREBALL_TRACKS && n < max; ++i)
        if (r->tape_fireball_impacts[i].active) {
            GmEntityView *v = &out[n++];
            memset(v, 0, sizeof *v);
            v->type = GM_VIEW_EXPLOSION_LARGE;
            v->x = r->tape_fireball_impacts[i].x;
            v->y = r->tape_fireball_impacts[i].y;
            v->z = r->tape_fireball_impacts[i].z;
            v->ent_id = r->tape_fireball_impacts[i].ent_id;
            v->age = r->tape_fireball_impacts[i].age;
        }
    return n;
}

/* Open GUI screen for tape-replay frame capture (divergence #9). Render-only:
 * never touches r->container / craft grid / furnace so physics stays clean. */
void gm_runtime_gui_view(GmRuntime *r, int container, int mx, int my) {
    if (!r || container < 0 || container > 3) return;
    r->gui_view_active = 1;
    r->gui_view_container = container;
    r->gui_view_mx = mx;
    r->gui_view_my = my;
}

void gm_runtime_gui_view_clear(GmRuntime *r) {
    if (!r) return;
    r->gui_view_active = 0;
    memset(r->tape_gui_slot_active, 0, sizeof r->tape_gui_slot_active);
    r->tape_gui_cursor_active = 0;
    r->tape_furnace_active = 0;
}

int gm_runtime_gui_view_get(const GmRuntime *r, int *container, int *mx, int *my) {
    if (!r || !r->gui_view_active) return 0;
    if (container) *container = r->gui_view_container;
    if (mx) *mx = r->gui_view_mx;
    if (my) *my = r->gui_view_my;
    return 1;
}

static int tape_stack_valid(int item, int count, int meta) {
    return item >= 0 && item <= 4095 && count >= 0 && count <= 64 &&
           meta >= 0 && meta <= 32767 && ((item == 0) == (count == 0));
}

static int tape_stack_enchants_ok(const ICStack *s) {
    int n;
    if (!s) return 0;
    n = s->n_enchants;
    if (n < 0 || n > IC_MAX_ENCHANTS) return 0;
    return 1;
}

int gm_runtime_tape_gui_slot_stack(GmRuntime *r, int slot, ICStack stack) {
    if (!r || slot < 0 || slot >= GMC_SLOT_COUNT ||
        !tape_stack_valid(stack.item, stack.count, stack.meta) ||
        !tape_stack_enchants_ok(&stack)) return 0;
    if (stack.count == 0) stack = ic_empty();
    r->tape_gui_slots[slot] = stack;
    r->tape_gui_slot_active[slot] = 1;
    return 1;
}

int gm_runtime_tape_gui_cursor_stack(GmRuntime *r, ICStack stack) {
    if (!r || !tape_stack_valid(stack.item, stack.count, stack.meta) ||
        !tape_stack_enchants_ok(&stack)) return 0;
    if (stack.count == 0) stack = ic_empty();
    r->tape_gui_cursor = stack;
    r->tape_gui_cursor_active = 1;
    return 1;
}

int gm_runtime_tape_gui_slot(GmRuntime *r, int slot, int item, int count, int meta) {
    return gm_runtime_tape_gui_slot_stack(
        r, slot, count == 0 ? ic_empty() : ic_mk(item, count, meta));
}

int gm_runtime_tape_gui_cursor(GmRuntime *r, int item, int count, int meta) {
    return gm_runtime_tape_gui_cursor_stack(
        r, count == 0 ? ic_empty() : ic_mk(item, count, meta));
}

int gm_runtime_tape_gui_slot_get(const GmRuntime *r, int slot, ICStack *out) {
    if (!r || slot < 0 || slot >= GMC_SLOT_COUNT ||
        !r->tape_gui_slot_active[slot]) return 0;
    if (out) *out = r->tape_gui_slots[slot];
    return 1;
}

int gm_runtime_tape_gui_cursor_get(const GmRuntime *r, ICStack *out) {
    if (!r || !r->tape_gui_cursor_active) return 0;
    if (out) *out = r->tape_gui_cursor;
    return 1;
}

int gm_runtime_tape_furnace(GmRuntime *r, int burn, int current_burn,
                            int cook, int total_cook) {
    if (!r || burn < 0 || current_burn < 0 || cook < 0 || total_cook < 0)
        return 0;
    r->tape_furnace_active = 1;
    r->tape_furnace_burn = burn;
    r->tape_furnace_current_burn = current_burn;
    r->tape_furnace_cook = cook;
    r->tape_furnace_total_cook = total_cook;
    return 1;
}

int gm_runtime_tape_inventory(GmRuntime *r, int slot, int item, int count, int meta) {
    if (!r || !isr_slot_ok(slot) ||
        item < 0 || item > 4095 || count < 0 || count > 64 ||
        meta < 0 || meta > 32767 || ((item == 0) != (count == 0))) return 0;
    if (!r->tape_inv_active) {
        isr_init(&r->tape_inv);
        r->tape_inv_active = 1;
    }
    isr_set_stack(&r->tape_inv, slot,
                  count == 0 ? ic_empty() : ic_mk(item, count, meta));
    return 1;
}

void gm_runtime_tape_player_view(GmRuntime *r, int xp_level, float xp_frac, int air,
                                 float portal, int portal_frame, int portal_phase,
                                 int loading, int texture_animations_pinned,
                                 int fire, int creative, int hurt_time,
                                 int max_hurt_time, float hurt_yaw,
                                 float attack_cooldown) {
    if (!r) return;
    r->tape_xp_active = 1;
    r->tape_xp_level = xp_level;
    r->tape_xp_frac = xp_frac;
    r->tape_air = air;
    r->tape_portal = portal;
    r->tape_portal_frame = portal_frame;
    r->tape_portal_phase = portal_phase;
    r->tape_loading = loading;
    r->tape_texture_animations_pinned = texture_animations_pinned;
    r->tape_fire = fire;
    r->tape_creative = creative;
    r->tape_hurt_time = hurt_time;
    r->tape_max_hurt_time = max_hurt_time;
    r->tape_hurt_yaw = hurt_yaw;
    r->tape_attack_cooldown = attack_cooldown;
}

void gm_runtime_tape_potions_clear(GmRuntime *r) {
    if (r) r->tape_potion_count = 0;
}

int gm_runtime_tape_potion(GmRuntime *r, int id, int amplifier, int duration,
                           int show_particles) {
    if (!r || id < 1 || id > 255 || amplifier < 0 || amplifier > 255 ||
        duration < 0 || r->tape_potion_count >= GM_MAX_POTION_EFFECTS) return 0;
    GmPotionEffectView *p = &r->tape_potions[r->tape_potion_count++];
    p->id = id;
    p->amplifier = amplifier;
    p->duration = duration;
    p->hide_particles = show_particles ? 0 : 1;
    return 1;
}

void gm_runtime_tape_armor(GmRuntime *r, int points) {
    if (!r) return;
    if (points > 20) points = 20;
    r->tape_armor_points = points < 0 ? -1 : points;
}

void gm_runtime_apply_tape_view(const GmRuntime *r, GmPlayerView *view) {
    if (!r || !view) return;
    if (r->tape_inv_active) {
        for (int i = 0; i < 9; ++i) {
            ICStack s = isr_get_stack(&r->tape_inv, i);
            view->hotbar_ids[i] = s.item;
            view->hotbar_counts[i] = s.count;
            view->hotbar_meta[i] = s.meta;
        }
    }
    if (r->tape_xp_active) {
        view->xp_level = r->tape_xp_level;
        view->xp_frac = r->tape_xp_frac;
        view->air = r->tape_air;
        view->portal = r->tape_portal;
        view->portal_frame = r->tape_portal_frame;
        view->portal_phase = r->tape_portal_phase;
        view->loading = r->tape_loading;
        view->texture_animations_pinned = r->tape_texture_animations_pinned;
        view->fire = r->tape_fire;
        view->creative = r->tape_creative;
        view->hurt_time = r->tape_hurt_time;
        view->max_hurt_time = r->tape_max_hurt_time;
        view->hurt_yaw = r->tape_hurt_yaw;
        view->attack_cooldown = r->tape_attack_cooldown;
        view->potion_count = r->tape_potion_count;
        memcpy(view->potions, r->tape_potions,
               (size_t)r->tape_potion_count * sizeof view->potions[0]);
        /* AbstractClientPlayer.getFovModifier reads generic.movementSpeed.
         * SPEED and SLOWNESS are operation-2 modifiers, so each multiplier is
         * applied in sequence before the ratio is averaged with 1. Tape
         * scenarios apply their effects before recording starts, after the
         * 0.5/tick EntityRenderer easing has converged. */
        {
            float speed_ratio = 1.0f;
            int has_speed_modifier = 0;
            for (int i = 0; i < r->tape_potion_count; ++i) {
                const GmPotionEffectView *p = &r->tape_potions[i];
                if (p->id == 1) {
                    speed_ratio *= 1.0f + 0.2f * (float)(p->amplifier + 1);
                    has_speed_modifier = 1;
                } else if (p->id == 2) {
                    speed_ratio *= 1.0f - 0.15f * (float)(p->amplifier + 1);
                    has_speed_modifier = 1;
                }
            }
            if (has_speed_modifier)
                view->fov_mult = (speed_ratio + 1.0f) * 0.5f;
        }
        /* Only the recorder can know the real generic.armor total (NBT
         * AttributeModifiers replace an armor item's defaults), so a tape
         * that carries it wins over the item-id guess. */
        if (r->tape_armor_points >= 0)
            view->armor_points = r->tape_armor_points;
        /* Recorder rows are post-tick. GuiIngame rendered the same health
         * transition one updateCounter earlier (portal_phase proves 1:1). */
        view->hud_transition_lead = 1;
    }
}

/* Absolute camera rotation, position/physics untouched. Human-play tape replay
 * sets the recorded per-tick yaw/pitch directly (mouse input is not physics;
 * accumulating dyaw deltas in float would drift off the recorded values). */
void gm_runtime_set_look(GmRuntime *r, float yaw, float pitch) {
    if (!r) return;
    r->player.yaw = yaw; r->player.pitch = pitch;
}

/* Seed health/food from a recorded tape header so a replayed survival session
 * starts from the recorded vitals instead of a fresh 20/20 player. */
void gm_runtime_set_vitals(GmRuntime *r, float health, int food) {
    if (!r) return;
    if (health > 0.0f && r->dead) {
        /* SPacketRespawn + SPacketUpdateHealth: revive before the first
         * destination tick and discard the old entity's burn/hurt state. */
        r->dead = 0;
        r->death_screen_ticks = 0;
        r->quit_to_title = 0;
        r->player_fire_ticks = 0;
        r->mobs.player_hurt_resistant = 0;
        r->mobs.player_last_damage = 0.0f;
    }
    r->vitals.health = health;
    r->vitals.foodLevel = food;
    r->player.health = health;
    r->player.food = (float)food;
}

static GmWorld *runtime_world_for_dimension(GmRuntime *r, int dimension) {
    if (!r || dimension < -1 || dimension > 1) return NULL;
    int wi = dimension + 1;
    if (!r->worlds[wi]) {
        int world_type = dimension == -1 ? 2 : (dimension == 1 ? 3 : 0);
        r->worlds[wi] = gm_world_create_type(r->seed, world_type);
    }
    return r->worlds[wi];
}

int gm_runtime_set_dimension(GmRuntime *r, int dimension) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world) return 0;
    r->world = world;
    r->dimension = dimension;
    r->win_world = NULL;
    /* an authoritative transfer (tape/script) implies arrival state: the
     * player may be standing inside the paired portal, so arm the cooldown
     * and clear any in-pane contact accumulated before the switch. */
    r->portal_cooldown = 100;
    r->portal_time = 0;
    return 1;
}

void gm_runtime_set_time(GmRuntime *r, long long world_time) {
    if (!r) return;
    r->clock.world_time=world_time;
}

void gm_runtime_set_total_time(GmRuntime *r, long long total_time) {
    if (!r) return;
    gm_world_clock_set_total_time(&r->clock, total_time);
}

void gm_runtime_set_gamerules(GmRuntime *r, const McGameRules *gamerules) {
    if (!r || !gamerules) return;
    r->gamerules = *gamerules;
    r->clock.freeze_daylight = !gamerules->doDaylightCycle;
    r->clock.freeze_weather = !gamerules->doWeatherCycle;
}

int gm_runtime_set_block(GmRuntime *r, int x, int y, int z, int id, int meta) {
    if (!r || !r->world || y < 0 || y > 255 || id < 0 || id > 4095 ||
        meta < 0 || meta > 15) return 0;
    int old_id = gm_world_block(r->world, x, y, z);
    if (old_id == 54 && id != 54)
        runtime_break_chest_te(r, x, y, z);
    gm_world_set_block_meta(r->world, x, y, z, id, meta);
    gm_live_block_changed(&r->entities, r->world, x, y, z);
    gm_fluid_mark(&r->fluids, r->world, r->dimension, x, y, z);
    break_unsupported_plants(r, x, y, z);
    return 1;
}

int gm_runtime_reanchor_block(GmRuntime *r, int x, int y, int z, int id, int meta) {
    if (!r || !r->world || y < 0 || y > 255 || id < 0 || id > 4095 ||
        meta < 0 || meta > 15) return 0;
    int old_id = gm_world_block(r->world, x, y, z);
    if (old_id == 54 && id != 54)
        runtime_break_chest_te(r, x, y, z);
    gm_world_set_block_meta(r->world, x, y, z, id, meta);
    /* No gm_live_block_changed: set_block_post restores recorded client
     * finals. Scheduling BlockFalling here re-runs gravity that the tape
     * already observed (and that local set_block / dig paths already armed),
     * inventing a second fall entity or a second sand/gravel cell. Ordinary
     * player/world mutations still go through gm_runtime_set_block. */
    /* gm_fluid_mark self-filters: no liquid in the 7-cell stencil => no-op. */
    gm_fluid_mark(&r->fluids, r->world, r->dimension, x, y, z);
    /* No break_unsupported_plants: recorded finals already include plant
     * removals; cascading here would invent drops and reorder truth. */
    return 1;
}

int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta) {
    if (!r || !r->world || y < 0 || y > 255 || id < 0 || id > 4095 ||
        meta < 0 || meta > 15) return 0;
    gm_world_load_block_meta(r->world, x, y, z, id, meta);
    r->win_gen = -1;
    return 1;
}

int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius) {
    if (!r || !r->world || radius < 0 || radius > 32) return 0;
    gm_world_ensure(r->world, ccx, ccz, radius);
    return 1;
}


int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world || y < 0 || y > 255 || id < 0 || id > 4095 || meta < 0 || meta > 15)
        return 0;
    gm_world_load_block_meta(world, x, y, z, id, meta);
    if (world == r->world) r->win_gen = -1;
    return 1;
}


int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world || radius < 0 || radius > 32) return 0;
    gm_world_ensure(world, ccx, ccz, radius);
    return 1;
}

int gm_runtime_set_tile_entity(GmRuntime *r, int dim, int x, int y, int z,
                               int entity_type, float rotation) {
    int free_slot = -1;
    if (!r || y < 0 || y > 255 || dim < -1 || dim > 1) return 0;
    for (int i = 0; i < GM_RUNTIME_SPAWNERS; ++i) {
        GmRuntimeSpawnerTE *s = &r->spawners[i];
        if (s->active && s->dim == dim && s->wx == x && s->wy == y &&
            s->wz == z) {
            s->entity_type = entity_type;
            s->mob_rotation = rotation;
            return 1;
        }
        if (!s->active && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    r->spawners[free_slot].active = 1;
    r->spawners[free_slot].dim = dim;
    r->spawners[free_slot].wx = x;
    r->spawners[free_slot].wy = y;
    r->spawners[free_slot].wz = z;
    r->spawners[free_slot].entity_type = entity_type;
    r->spawners[free_slot].mob_rotation = rotation;
    return 1;
}

int gm_runtime_spawner_views(const GmRuntime *r, GmRuntimeSpawnerView *out,
                             int max) {
    int n = 0;
    if (!r || !out || max <= 0) return 0;
    for (int i = 0; i < GM_RUNTIME_SPAWNERS && n < max; ++i) {
        const GmRuntimeSpawnerTE *s = &r->spawners[i];
        if (!s->active || s->dim != r->dimension) continue;
        if (r->world && gm_world_block(r->world, s->wx, s->wy, s->wz) != 52)
            continue;
        out[n].wx = s->wx;
        out[n].wy = s->wy;
        out[n].wz = s->wz;
        out[n].entity_type = s->entity_type;
        out[n].mob_rotation = s->mob_rotation;
        n++;
    }
    return n;
}

int gm_runtime_set_inventory(GmRuntime *r, int slot, int item, int count, int meta) {
    if (!r || !isr_slot_ok(slot) || item < 0 || item > 4095 ||
        count < 0 || count > 64 || meta < 0 || meta > 32767 ||
        ((item == 0) != (count == 0))) return 0;
    isr_set_stack(&r->player.inv, slot,
                  count == 0 ? ic_empty() : ic_mk(item, count, meta));
    if (slot == ISR_ARMOR_CHEST) sync_elytra_from_chest(r);
    return 1;
}

void gm_runtime_set_weather(GmRuntime *r, int raining, int thundering,
                            int rain_time, int thunder_time) {
    if (!r) return;
    r->weather_enabled = 1;
    gm_world_clock_set_weather(&r->clock, raining, thundering,
                               rain_time, thunder_time);
}

void gm_runtime_set_rain_thunder(GmRuntime *r, float rain, float thunder) {
    if (!r) return;
    if (rain < 0.0f) rain = 0.0f;
    if (rain > 1.0f) rain = 1.0f;
    if (thunder < 0.0f) thunder = 0.0f;
    if (thunder > 1.0f) thunder = 1.0f;
    r->rain_strength = rain;
    r->thunder_strength = thunder;
}

int gm_runtime_projectile_views(const GmRuntime *r, GmEntityView *out, int max) {
    if (!r || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES && n < max; ++i) {
        const GmRuntimeProjectile *p = &r->projectiles[i];
        if (!p->active) continue;
        GmEntityView v; memset(&v, 0, sizeof v);
        v.x = (float)p->x; v.y = (float)p->y; v.z = (float)p->z;
        v.health = 1.0f;
        if (p->type == 1 || p->type == 2) {
            /* bow arrows: RenderArrow model, oriented from velocity like
             * vanilla EntityArrow (yaw atan2(vx,vz), pitch atan2(vy,horiz)) */
            v.type = 29; /* ER_TYPE_ARROW */
            double h = sqrt(p->vx * p->vx + p->vz * p->vz);
            v.yaw   = (float)(atan2(p->vx, p->vz) * 180.0 / MC_PI);
            v.pitch = (float)(atan2(p->vy, h) * 180.0 / MC_PI);
        } else if (p->type == 3 || p->type == 5) {
            /* Fireballs use the fire_charge particle icon; the large ghast
             * shot shares this billboard path until scale is carried in views.
             * EntitySmallFireball uses RenderFireball scale 0.5.
             * particle icon. gm_items_emit_billboard selects that exact path
             * from item id 385. Live shots are isFireballFiery (setFire each
             * tick) so flags bit 0 enables renderEntityOnFire layers. */
            v.type = GM_VIEW_BILLBOARD; v.item_id = 385;
            v.flags = 1; /* burn / isBurning */
            if (p->type == 5) v.item_meta = 2; /* large width 1.0 fire overlay */
        } else if (p->type == 4) {
            v.type = GM_VIEW_BILLBOARD; v.item_id = 381; /* eye of ender */
        } else {
            v.type = 20; /* unknown: legacy marker box */
        }
        out[n++] = v;
    }
    return n;
}

int gm_runtime_craft(GmRuntime *r, int grid_width, const int inv_slots[9]) {
    if (!r || (grid_width != 2 && grid_width != 3)) return 0;
    r->parity_craft_attempts++;
    if (grid_width == 3 && r->container != 1) return 0;
    CRStack grid[9];
    int use[ISR_MAIN_SLOTS];
    memset(use, 0, sizeof use);
    for (int i = 0; i < 9; ++i) {
        grid[i] = crf_empty();
        int slot = inv_slots[i];
        if (slot < 0) continue;
        if (slot >= ISR_MAIN_SLOTS) return 0;
        if (grid_width == 2 && (i % 3 >= 2 || i / 3 >= 2)) return 0;
        ICStack s = isr_get_stack(&r->player.inv, slot);
        if (isr_is_empty(&s)) return 0;
        use[slot]++;
        if (use[slot] > s.count) return 0;
        grid[i] = crf_mk(s.item, 1, s.meta);
    }
    CRRecipe recipes[CRF_NRECIPES];
    int nr = crf_build(recipes);
    CRStack result = crf_findMatching(recipes, nr, grid);
    if (crf_isEmpty(result) || result.item == (i32)0xffffffff) return 0;
    IsrInv next = r->player.inv;
    for (int slot = 0; slot < ISR_MAIN_SLOTS; ++slot)
        if (use[slot]) (void)isr_decr_stack_size(&next, slot, use[slot]);
    ICStack output = ic_mk(result.item, result.count, result.meta);
    (void)isr_add_item_stack_to_inventory(&next, &output);
    if (!isr_is_empty(&output)) return 0;
    r->player.inv = next;
    r->parity_craft_successes++;
    r->parity_last_craft = ic_mk(result.item, result.count, result.meta);
    return 1;
}

/* Close any open container TE bookkeeping before switching screens. */
static void runtime_close_container(GmRuntime *r)
{
    if (r->container == 3 && r->active_chest >= 0)
        chest_live_close(&r->chests[r->active_chest].state);
    gm_container_close(r);
    gm_player_set_gui_blocked(0);
    r->active_furnace = -1;
    r->active_chest = -1;
}

/* Drop one chest slot stack as a ground EntityItem, including StoredEnchantments.
 * Uses live overflow hold when the active table is full (not silent discard).
 * Returns 1 if held (active or overflow), 0 if both caps rejected the stack. */
static int runtime_drop_stack(GmRuntime *r, int wx, int wy, int wz, ICStack st)
{
    if (!r || st.item <= 0 || st.count <= 0) return 1;
    if (gm_live_spawn_stack(&r->entities, wx + 0.5, wy + 0.5, wz + 0.5, st, 10))
        return 1;
    /* Last resort: try player inventory so a full-chest break cannot vanish. */
    {
        ICStack rem = st;
        (void)isr_add_item_stack_to_inventory(&r->player.inv, &rem);
        if (isr_is_empty(&rem)) return 1;
    }
    return 0;
}

/* Materialize deferred structure loot (or live TE slots) and drop all stacks.
 * Vanilla: breakBlock -> InventoryHelper after fillWithLoot on first access. */
static void runtime_drop_chest_contents(GmRuntime *r, ChestLive *ch, int wx, int wy, int wz)
{
    int s;
    if (!r || !ch) return;
    chest_live_ensure_loot(ch);
    for (s = 0; s < CHEST_LIVE_SLOTS; ++s) {
        ICStack st = chest_live_get(ch, s);
        runtime_drop_stack(r, wx, wy, wz, st);
    }
}

/* TileEntityChest break: drop every slot (after deferred loot fill) and free
 * the TE so a replacement chest at the same block starts empty. Unopened
 * structure chests with no TE still materialize deferred loot on break. */
static void runtime_break_chest_te(GmRuntime *r, int wx, int wy, int wz)
{
    if (!r || !r->chests) return;
    for (int i = 0; i < r->chests_cap; ++i) {
        GmRuntimeChest *c = &r->chests[i];
        if (!c->active || c->wx != wx || c->wy != wy || c->wz != wz) continue;
        runtime_drop_chest_contents(r, &c->state, wx, wy, wz);
        if (r->container == 3 && r->active_chest == i)
            runtime_close_container(r);
        c->active = 0;
        chest_live_init(&c->state);
        return;
    }
    /* No TE yet: unopened structure chest still drops deferred loot. */
    {
        int tid = -1;
        long long lseed = 0;
        if (gm_stronghold_chest_info(r->seed, wx, wy, wz, &tid, &lseed)) {
            ChestLive tmp;
            chest_live_init(&tmp);
            chest_live_set_loot(&tmp, tid, lseed);
            runtime_drop_chest_contents(r, &tmp, wx, wy, wz);
        }
    }
}

/* Free slot for a new TE. Never evicts a live chest while its block exists.
 * Reclaims slots whose block is gone; grows the table when full. */
static int runtime_chest_free_slot(GmRuntime *r, int wx, int wy, int wz)
{
    int free_slot = -1;
    if (!r || !r->chests) return -1;
    for (int i = 0; i < r->chests_cap; ++i) {
        GmRuntimeChest *c = &r->chests[i];
        if (!c->active) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (c->wx == wx && c->wy == wy && c->wz == wz) return i;
        /* Reclaim only when the world no longer has a chest block here. */
        if (gm_world_block(r->world, c->wx, c->wy, c->wz) != 54) {
            if (r->container == 3 && r->active_chest == i)
                runtime_close_container(r);
            c->active = 0;
            chest_live_init(&c->state);
            if (free_slot < 0) free_slot = i;
        }
    }
    if (free_slot >= 0) return free_slot;
    /* Grow: never drop inventory of a live block-backed TE. */
    {
        int new_cap = r->chests_cap > 0 ? r->chests_cap * 2 : GM_RUNTIME_CHESTS_INITIAL;
        GmRuntimeChest *grown = (GmRuntimeChest *)realloc(
            r->chests, (size_t)new_cap * sizeof(GmRuntimeChest));
        if (!grown) return -1; /* fail safely: no open, no loss */
        memset(grown + r->chests_cap, 0,
               (size_t)(new_cap - r->chests_cap) * sizeof(GmRuntimeChest));
        free_slot = r->chests_cap;
        r->chests = grown;
        r->chests_cap = new_cap;
        return free_slot;
    }
}

int gm_runtime_use_block(GmRuntime *r, int wx, int wy, int wz) {
    if (!r || !r->world) return 0;
    GmPlayerView v; gm_runtime_view(r, &v);
    double dx = (wx + 0.5) - v.x;
    double dy = (wy + 0.5) - (v.y + v.eye_height);
    double dz = (wz + 0.5) - v.z;
    if (dx*dx + dy*dy + dz*dz > 36.0) return 0;
    int id = gm_world_block(r->world, wx, wy, wz);
    if (id == 58) {
        runtime_close_container(r); /* return any live grid/cursor before switching */
        r->container=1; r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        gm_player_set_gui_blocked(1);
        r->parity_container_opens++;
        return 1;
    }
    if (id == 61 || id == 62) {
        runtime_close_container(r);
        int free_slot = -1;
        for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
            GmRuntimeFurnace *f = &r->furnaces[i];
            if (f->active && f->wx==wx && f->wy==wy && f->wz==wz) {
                r->container=2; r->active_furnace=i;
                r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
                gm_player_set_gui_blocked(1);
                r->parity_container_opens++;
                return 1;
            }
            if (!f->active && free_slot < 0) free_slot=i;
        }
        if (free_slot < 0) return 0;
        GmRuntimeFurnace *f=&r->furnaces[free_slot];
        f->active=1; f->wx=wx; f->wy=wy; f->wz=wz; furnace_live_init(&f->state);
        r->container=2; r->active_furnace=free_slot;
        r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        gm_player_set_gui_blocked(1);
        r->parity_container_opens++;
        return 1;
    }
    if (id == 54) {
        runtime_close_container(r);
        if (!r->chests) return 0;
        for (int i = 0; i < r->chests_cap; ++i) {
            GmRuntimeChest *c = &r->chests[i];
            if (c->active && c->wx==wx && c->wy==wy && c->wz==wz) {
                chest_live_open(&c->state);
                r->container=3; r->active_chest=i;
                r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
                gm_player_set_gui_blocked(1);
                r->parity_container_opens++;
                return 1;
            }
        }
        int free_slot = runtime_chest_free_slot(r, wx, wy, wz);
        if (free_slot < 0) return 0;
        GmRuntimeChest *c=&r->chests[free_slot];
        c->active=1; c->wx=wx; c->wy=wy; c->wz=wz;
        chest_live_init(&c->state);
        {
            int tid = -1; long long lseed = 0;
            if (gm_stronghold_chest_info(r->seed, wx, wy, wz, &tid, &lseed))
                chest_live_set_loot(&c->state, tid, lseed);
        }
        chest_live_open(&c->state);
        r->container=3; r->active_chest=free_slot;
        r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        gm_player_set_gui_blocked(1);
        r->parity_container_opens++;
        return 1;
    }
    if(id==120){
        ICStack held=isr_get_stack(&r->player.inv,r->player.inv.current_item);
        if(held.item!=381||held.count<=0||(gm_world_meta(r->world,wx,wy,wz)&4))return 0;
        if(!gm_end_portal_insert_eye(r->world,wx,wy,wz))return 0;
        (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);
        return 1;
    }
    if(id==26){
        if(r->dimension==0){
            long long day=r->clock.world_time/24000LL;
            r->clock.world_time=(day+1)*24000LL;return 1;
        }
        for(int x=wx-1;x<=wx+1;++x)for(int z=wz-1;z<=wz+1;++z)
            if(gm_world_block(r->world,x,wy,z)==26)gm_world_set_block(r->world,x,wy,z,0);
        runtime_explode(r,wx+0.5,wy+0.5,wz+0.5,5.0f);return 1;
    }
    return 0;
}

int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount) {
    if (!r || r->container!=2 || r->active_furnace<0 || amount<=0 ||
        inventory_slot<0 || inventory_slot>=ISR_MAIN_SLOTS) return 0;
    ICStack src=isr_get_stack(&r->player.inv,inventory_slot);
    if (isr_is_empty(&src)) return 0;
    if (src.count>amount) src.count=amount;
    SRStack in=sr_mk(src.item,src.count,src.meta);
    int moved=furnace_live_insert(&r->furnaces[r->active_furnace].state,furnace_slot,in);
    if (moved>0) (void)isr_decr_stack_size(&r->player.inv,inventory_slot,moved);
    return moved;
}

int gm_runtime_container_open(GmRuntime *r, int ctype, int wx, int wy, int wz) {
    if (!r) return 0;
    if (ctype == 0) {
        runtime_close_container(r);
        r->container = 0;
        r->active_furnace = -1;
        r->active_chest = -1;
        return 1;
    }
    if (ctype < 1 || ctype > 3) return 0;
    /* Workbench/furnace/chest: require the world cell and open through the
     * same path as survival use (reach + TE table). */
    if (!r->world) return 0;
    int id = gm_world_block(r->world, wx, wy, wz);
    if (ctype == 1 && id != 58) return 0;
    if (ctype == 2 && id != 61 && id != 62) return 0;
    if (ctype == 3 && id != 54) return 0;
    return gm_runtime_use_block(r, wx, wy, wz) && r->container == ctype;
}

int gm_runtime_container_seed_slot(GmRuntime *r, int gmc_slot,
                                   int item, int count, int meta) {
    if (!r) return 0;
    if (count < 0 || count > 64 || item < 0 || meta < 0 || meta > 15) return 0;
    if (count == 0) item = 0;
    if (gmc_slot >= GMC_GRID0 && gmc_slot < GMC_RESULT) {
        if (r->container != 0 && r->container != 1) return 0;
        int cell = gmc_slot - GMC_GRID0;
        if (r->container == 0 && !(cell == 0 || cell == 1 || cell == 3 || cell == 4))
            return 0;
        r->craft_grid[cell] = count == 0 ? ic_empty()
                                         : ic_mk((i32)item, (i32)count, (i32)meta);
        return 1;
    }
    if (gmc_slot >= GMC_FURNACE0 && gmc_slot < GMC_ARMOR0) {
        if (r->container != 2 || r->active_furnace < 0) return 0;
        FurnaceLive *f = &r->furnaces[r->active_furnace].state;
        SRStack st = count == 0 ? sr_empty()
                                : sr_mk((i32)item, (i32)count, (i32)meta);
        int fs = gmc_slot - GMC_FURNACE0;
        if (fs == 0) f->input = st;
        else if (fs == 1) f->fuel = st;
        else if (fs == 2) f->output = st;
        else return 0;
        return 1;
    }
    if (gmc_slot >= GMC_CHEST0 && gmc_slot < GMC_CHEST0 + GMC_CHEST_SLOTS) {
        if (r->container != 3 || r->active_chest < 0) return 0;
        ChestLive *c = &r->chests[r->active_chest].state;
        /* Authoritative tape seed: skip loot fill so taped contents win. */
        c->loot_filled = 1;
        c->loot_table = CHEST_LOOT_NONE;
        chest_live_set(c, gmc_slot - GMC_CHEST0,
                       count == 0 ? ic_empty()
                                  : ic_mk((i32)item, (i32)count, (i32)meta));
        return 1;
    }
    return 0;
}

void gm_runtime_container_seed_cursor(GmRuntime *r, int item, int count, int meta) {
    if (!r) return;
    if (count <= 0 || item <= 0) {
        gm_player_cursor_set(ic_empty());
        return;
    }
    gm_player_cursor_set(ic_mk((i32)item, (i32)count, (i32)meta));
}

int gm_runtime_container_seed_furnace_prop(GmRuntime *r, int burn,
                                           int current_burn, int cook,
                                           int total_cook) {
    if (!r || r->container != 2 || r->active_furnace < 0) return 0;
    if (burn < 0 || current_burn < 0 || cook < 0 || total_cook < 0) return 0;
    FurnaceLive *f = &r->furnaces[r->active_furnace].state;
    f->burn_time = burn;
    f->current_burn_time = current_burn;
    f->cook_time = cook;
    f->total_cook = total_cook;
    return 1;
}

void gm_runtime_container_force_close(GmRuntime *r) {
    if (!r) return;
    runtime_close_container(r);
    r->container = 0;
    r->active_furnace = -1;
    r->active_chest = -1;
}

int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount) {
    if (!r || r->container!=2 || r->active_furnace<0 || amount<=0) return 0;
    FurnaceLive *f=&r->furnaces[r->active_furnace].state;
    SRStack *src = furnace_slot==0?&f->input:furnace_slot==1?&f->fuel:
                   furnace_slot==2?&f->output:NULL;
    if (!src || sr_isEmpty(*src)) return 0;
    int n=src->count<amount?src->count:amount;
    IsrInv next=r->player.inv;
    ICStack out=ic_mk(src->item,n,src->meta);
    (void)isr_add_item_stack_to_inventory(&next,&out);
    int moved=n-out.count;
    if (moved<=0) return 0;
    (void)furnace_live_extract(f,furnace_slot,moved);
    r->player.inv=next;
    return moved;
}
