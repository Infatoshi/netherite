#include "player_survival.h"
#include "game/dragon_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"
#include "projectile_motion.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

static float held_damage(const PsvPlayer *p){
    int id=isr_get_stack(&p->inv,p->inv.current_item).item;
    if(id==268)return mc_combat_weapon_raw(1);
    if(id==272)return mc_combat_weapon_raw(2);
    if(id==267)return mc_combat_weapon_raw(3);
    if(id==276)return mc_combat_weapon_raw(4);
    return mc_combat_weapon_raw(0);
}
static void damage_held_weapon(PsvPlayer *p){
    int slot=p->inv.current_item;ICStack held=isr_get_stack(&p->inv,slot);
    ITAStack tool=ita_mk(held.item,held.meta);ita_hit_entity(&tool);
    int max=ita_stack_max_damage(&tool);if(max<=0)return;
    if(tool.damage>max)(void)isr_decr_stack_size(&p->inv,slot,1);
    else{held.meta=tool.damage;isr_set_stack(&p->inv,slot,held);}
}

static void apply_podium(const EdeWorld *s,GmWorld *w){
    for(int x=-4;x<=4;++x)for(int y=62;y<=95;++y)for(int z=-4;z<=4;++z){
        double dist=sqrt((double)(x*x+(y-63)*(y-63)+z*z));
        int torch=(y==65&&((abs(x)==1&&z==0)||(abs(z)==1&&x==0)));
        if(dist>3.5&&!torch)continue;
        u16 st=ede_get_world(s,x,y,z);
        gm_world_set_block_meta(w,x,y,z,mc_state_id(st),mc_state_meta(st));
    }
}

void gm_dragon_generate_spike(
        GmDragonLive *d, GmWorld *world, int index,
        int invulnerable, int beam_to_origin) {
    int cx, cz, radius, height, guarded;
    if (!d || !world || index < 0 || index >= ED_NUM_CRYSTALS) return;
    ed_pillar_spec(
        d->state.arena.seed, index, &cx, &cz,
        &radius, &height, &guarded);
    for (int x = cx - radius; x <= cx + radius; ++x)
        for (int y = 0; y <= height + 10; ++y)
            for (int z = cz - radius; z <= cz + radius; ++z) {
                int dx = x - cx, dz = z - cz;
                if (dx * dx + dz * dz <= radius * radius + 1
                        && y < height)
                    gm_world_load_block_meta(world, x, y, z, 49, 0);
                else if (y > 65)
                    gm_world_load_block_meta(world, x, y, z, 0, 0);
            }
    if (guarded)
        for (int x = -2; x <= 2; ++x)
            for (int z = -2; z <= 2; ++z) {
                if (abs(x) == 2 || abs(z) == 2)
                    for (int y = 0; y < 3; ++y)
                        gm_world_load_block_meta(
                            world, cx + x, height + y, cz + z, 101, 0);
                gm_world_load_block_meta(
                    world, cx + x, height + 3, cz + z, 101, 0);
            }
    gm_world_load_block_meta(world, cx, height, cz, 7, 0);
    gm_world_finish_bulk_edit(
        world, (int)floor((double)cx / 16.0),
        (int)floor((double)cz / 16.0), 1);
    EdCrystal *crystal = &d->state.arena.crystals[index];
    crystal->x = (double)cx + 0.5;
    crystal->y = (double)height + 1.0;
    crystal->z = (double)cz + 0.5;
    crystal->alive = 1;
    crystal->invulnerable = invulnerable != 0;
    crystal->has_beam = beam_to_origin != 0;
    crystal->beam_x = 0;
    crystal->beam_y = 128;
    crystal->beam_z = 0;
}

void gm_dragon_set_podium(GmDragonLive *d, GmWorld *world, int active) {
    if (!d || !d->initialized || !world) return;
    ede_generate_podium(&d->state, 0, d->state.exit_portal_y, 0, active);
    d->state.portal_generated = active != 0;
    apply_podium(&d->state, world);
}

void gm_dragon_reset_spike_crystals(GmDragonLive *d) {
    if (!d || !d->initialized) return;
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        d->state.arena.crystals[i].invulnerable = 0;
        d->state.arena.crystals[i].has_beam = 0;
    }
}

void gm_dragon_respawn(GmDragonLive *d, long long seed, float yaw) {
    if (!d || !d->initialized) return;
    ed_init(&d->state.arena, (u64)seed);
    EdDragon *dragon = &d->state.arena.dragon;
    dragon->health = dragon->max_health;
    dragon->x = 0.0;
    dragon->y = 128.0;
    dragon->z = 0.0;
    dragon->yaw = yaw;
    dragon->phase = ED_PHASE_CIRCLE;
    dragon->phase_ticks = 0;
    dragon->target_x = 0.0;
    dragon->target_y = 80.0;
    dragon->target_z = 50.0;
    d->state.tick = 0;
    d->state.seed = (u64)seed;
    d->state.previously_killed = 1;
    d->state.portal_generated = 0;
    d->state.egg_placed = 0;
    d->state.death_processed = 0;
    d->world_applied = 0;
    d->player_attack_cooldown = 0;
}

void gm_dragon_init(GmDragonLive *d,GmWorld *world,long long seed){
    memset(d,0,sizeof *d);EdeScenario sc={(u64)seed,0,0};ede_init_scene(&d->state,&sc);
    d->initialized=1;apply_podium(&d->state,world);
    for(int i=0;i<ED_NUM_CRYSTALS;++i)
        gm_dragon_generate_spike(d,world,i,0,0);
}

static void set_crystal_hit(
        GmDragonCrystalHit *hit, int index, const EdCrystal *crystal) {
    if (!hit) return;
    hit->index=index;hit->x=crystal->x;hit->y=crystal->y;hit->z=crystal->z;
}

void gm_dragon_crystal_destroyed(GmDragonLive *d,int index,
                                 int source_is_player,
                                 int player_can_be_targeted){
    if(!d||!d->initialized)return;
    ed_on_crystal_destroyed(&d->state.arena,index,source_is_player,
                            player_can_be_targeted);
}

int gm_dragon_player_attack(GmDragonLive *d,const struct PsvPlayer *player_,
                            int ox,int oz,GmDragonCrystalHit *crystal_hit){
    if(!d||!d->initialized||!player_)return 0;
    const PsvPlayer *p=(const PsvPlayer *)player_;
    double px=p->ent.posX+ox,py=p->ent.posY+PSV_EYE_HEIGHT,pz=p->ent.posZ+oz;
    double yr=p->yaw*MC_PI/180.0,pr=p->pitch*MC_PI/180.0;
    double dx=-sin(yr)*cos(pr),dy=-sin(pr),dz=cos(yr)*cos(pr);
    int crystal=-1;double best=4.0;
    for(int i=0;i<ED_NUM_CRYSTALS;++i)if(d->state.arena.crystals[i].alive){
        EdCrystal *c=&d->state.arena.crystals[i];double vx=c->x-px,vy=c->y-py,vz=c->z-pz;
        double t=vx*dx+vy*dy+vz*dz;if(t<0||t>best)continue;
        double ex=vx-t*dx,ey=vy-t*dy,ez=vz-t*dz;
        if(ex*ex+ey*ey+ez*ez<=1.0){crystal=i;best=t;}
    }
    EdDragon *g=&d->state.arena.dragon;int hit_dragon=0;
    if(g->alive&&g->death_ticks==0){double vx=g->x-px,vy=(g->y+2)-py,vz=g->z-pz;
        double t=vx*dx+vy*dy+vz*dz;if(t>=0&&t<=best){double ex=vx-t*dx,ey=vy-t*dy,ez=vz-t*dz;
            if(ex*ex+ey*ey+ez*ez<=9.0){hit_dragon=1;best=t;}}}
    if(crystal<0&&!hit_dragon)return 0;
    if(d->player_attack_cooldown>0)return 1;
    int result=1;
    if(crystal>=0){EdCrystal *c=&d->state.arena.crystals[crystal];
        set_crystal_hit(crystal_hit,crystal,c);
        if(!ed_mark_crystal_destroyed(&d->state.arena,crystal))return 0;
        result=2;
    }
    else {g->health-=held_damage(p);if(g->health<0)g->health=0;}
    damage_held_weapon((PsvPlayer *)p);
    d->player_attack_cooldown=10;return result;
}

int gm_dragon_player_attack_target(
        const GmDragonLive *d, const struct PsvPlayer *player_,
        int ox, int oz, double *distance) {
    const PsvPlayer *p;
    const EdDragon *g;
    double px, py, pz, yr, pr, dx, dy, dz, best = 4.0;
    int found = 0;
    if (!d || !d->initialized || !player_) return 0;
    p = (const PsvPlayer *)player_;
    px = p->ent.posX + ox;
    py = p->ent.posY + PSV_EYE_HEIGHT;
    pz = p->ent.posZ + oz;
    yr = p->yaw * MC_PI / 180.0;
    pr = p->pitch * MC_PI / 180.0;
    dx = -sin(yr) * cos(pr);
    dy = -sin(pr);
    dz = cos(yr) * cos(pr);
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        const EdCrystal *c = &d->state.arena.crystals[i];
        double vx, vy, vz, t, ex, ey, ez;
        if (!c->alive) continue;
        vx = c->x - px; vy = c->y - py; vz = c->z - pz;
        t = vx * dx + vy * dy + vz * dz;
        if (t < 0.0 || t > best) continue;
        ex = vx - t * dx; ey = vy - t * dy; ez = vz - t * dz;
        if (ex * ex + ey * ey + ez * ez <= 1.0) {
            best = t;
            found = 1;
        }
    }
    g = &d->state.arena.dragon;
    if (g->alive && g->death_ticks == 0) {
        double vx = g->x - px;
        double vy = g->y + 2.0 - py;
        double vz = g->z - pz;
        double t = vx * dx + vy * dy + vz * dz;
        if (t >= 0.0 && t <= best) {
            double ex = vx - t * dx;
            double ey = vy - t * dy;
            double ez = vz - t * dz;
            if (ex * ex + ey * ey + ez * ez <= 9.0) {
                best = t;
                found = 1;
            }
        }
    }
    if (!found) return 0;
    if (distance) *distance = best;
    return 1;
}

int gm_dragon_tick(GmDragonLive *d,GmWorld *world,const struct McSinTable *st_,
                   double px,double py,double pz){
    if(!d||!d->initialized)return 0;
    if(d->player_attack_cooldown>0)--d->player_attack_cooldown;
    d->state.arena.player.x=px;d->state.arena.player.y=py;d->state.arena.player.z=pz;
    const McSinTable *st=(const McSinTable *)st_;
    EdDragon *g=&d->state.arena.dragon;
    if(g->health<=0||g->death_ticks>0)ede_on_death_tick(&d->state);else ed_tick_dragon(&d->state.arena,st);
    ++d->state.tick;
    if(d->state.death_processed&&!d->world_applied){apply_podium(&d->state,world);d->world_applied=1;return 1;}
    return 0;
}

int gm_dragon_fill_views(const GmDragonLive *d,GmEntityView *out,int max){
    if(!d||!d->initialized||!out||max<=0)return 0;
    int n=0;const EdDragon *g=&d->state.arena.dragon;
    if(g->alive&&n<max){
        GmEntityView view;memset(&view,0,sizeof view);
        view.type=GM_ENTITY_DRAGON;view.x=(float)g->x;view.y=(float)g->y;
        view.z=(float)g->z;view.yaw=g->yaw;view.health=g->health;
        view.ticks_existed=g->ticks_existed;
        if(g->heal_crystal_idx>=0&&g->heal_crystal_idx<ED_NUM_CRYSTALS){
            const EdCrystal *c=&d->state.arena.crystals[g->heal_crystal_idx];
            if(c->alive){view.has_heal_beam=1;view.heal_x=(float)c->x;
                view.heal_y=(float)c->y;view.heal_z=(float)c->z;
                view.heal_crystal_ticks=(int)d->state.tick;}
        }
        out[n++]=view;
    }
    for(int i=0;i<ED_NUM_CRYSTALS&&n<max;++i)if(d->state.arena.crystals[i].alive){
        const EdCrystal *c=&d->state.arena.crystals[i];GmEntityView view;
        memset(&view,0,sizeof view);view.type=GM_ENTITY_CRYSTAL;
        view.x=(float)c->x;view.y=(float)c->y;view.z=(float)c->z;
        view.health=5;view.crystal_rot=(float)d->state.tick;view.show_bottom=1;
        view.has_beam=c->has_beam;
        view.beam_x=c->beam_x;view.beam_y=c->beam_y;
        view.beam_z=c->beam_z;
        out[n++]=view;
    }return n;
}

int gm_dragon_damage_near(GmDragonLive *d,double x,double y,double z,
                          double radius,float damage,
                          GmDragonCrystalHit *crystal_hit){
    if(!d||!d->initialized)return 0;
    double r2=radius*radius;
    for(int i=0;i<ED_NUM_CRYSTALS;++i)if(d->state.arena.crystals[i].alive){EdCrystal *c=&d->state.arena.crystals[i];
        double dx=c->x-x,dy=c->y-y,dz=c->z-z;if(dx*dx+dy*dy+dz*dz<=r2){
            set_crystal_hit(crystal_hit,i,c);
            if(!ed_mark_crystal_destroyed(&d->state.arena,i))return 0;
            return 2;}}
    EdDragon *g=&d->state.arena.dragon;double dx=g->x-x,dy=(g->y+2)-y,dz=g->z-z;
    if(g->alive&&g->death_ticks==0&&dx*dx+dy*dy+dz*dz<=r2*9.0){g->health-=damage;if(g->health<0)g->health=0;return 1;}
    return 0;
}

static McAABB dragon_part_box(
        double x, double y, double z, double width, double height) {
    double half = width * 0.5;
    return mc_aabb_make(
        x - half, y, z - half, x + half, y + height, z + half);
}

static int dragon_part_boxes(const EdDragon *g, McAABB boxes[8]) {
    if (!g || !g->alive || g->death_ticks != 0) return 0;
    double yaw = (double)g->yaw * MC_PI / 180.0;
    double fx = g->head_x - g->x;
    double fz = g->head_z - g->z;
    double flen = sqrt(fx * fx + fz * fz);
    if (flen <= 1.0e-12) {
        fx = -sin(yaw);
        fz = cos(yaw);
    } else {
        fx /= flen;
        fz /= flen;
    }
    double wx = fz, wz = -fx;
    boxes[0] = dragon_part_box(
        g->head_x, g->head_y, g->head_z, 1.0, 1.0);
    boxes[1] = dragon_part_box(
        g->x + fx * 5.5, g->y + 2.75, g->z + fz * 5.5, 3.0, 3.0);
    boxes[2] = dragon_part_box(
        g->x + fx * 0.5, g->y, g->z + fz * 0.5, 5.0, 3.0);
    boxes[3] = dragon_part_box(
        g->x - fx * 3.5, g->y + 1.5, g->z - fz * 3.5, 2.0, 2.0);
    boxes[4] = dragon_part_box(
        g->x - fx * 5.5, g->y + 1.5, g->z - fz * 5.5, 2.0, 2.0);
    boxes[5] = dragon_part_box(
        g->x - fx * 7.5, g->y + 1.5, g->z - fz * 7.5, 2.0, 2.0);
    boxes[6] = dragon_part_box(
        g->x + wx * 4.5, g->y + 2.0, g->z + wz * 4.5, 4.0, 2.0);
    boxes[7] = dragon_part_box(
        g->x - wx * 4.5, g->y + 2.0, g->z - wz * 4.5, 4.0, 3.0);
    return 8;
}

int gm_dragon_projectile_intercept(
        const GmDragonLive *d, double sx, double sy, double sz,
        double ex, double ey, double ez, int *target, double *distance_sq) {
    if (!d || !d->initialized) return 0;
    const double expand = 0.30000001192092896;
    McAABB boxes[8];
    int count = dragon_part_boxes(&d->state.arena.dragon, boxes);
    int best = 0;
    double best_dist = 0.0;
    for (int i = 0; i < count; ++i) {
        double hx, hy, hz, dx, dy, dz, dist;
        int side;
        boxes[i].minX -= expand; boxes[i].minY -= expand;
        boxes[i].minZ -= expand; boxes[i].maxX += expand;
        boxes[i].maxY += expand; boxes[i].maxZ += expand;
        if (!pm_aabb_intercept(
                &boxes[i], sx, sy, sz, ex, ey, ez,
                &hx, &hy, &hz, &side))
            continue;
        dx = hx - sx; dy = hy - sy; dz = hz - sz;
        dist = dx * dx + dy * dy + dz * dz;
        if (best != 0 && !(dist < best_dist)) continue;
        best = i + 1;
        best_dist = dist;
    }
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        const EdCrystal *c = &d->state.arena.crystals[i];
        double hx, hy, hz, dx, dy, dz, dist;
        int side;
        if (!c->alive) continue;
        McAABB box = dragon_part_box(c->x, c->y, c->z, 2.0, 2.0);
        box.minX -= expand; box.minY -= expand; box.minZ -= expand;
        box.maxX += expand; box.maxY += expand; box.maxZ += expand;
        if (!pm_aabb_intercept(
                &box, sx, sy, sz, ex, ey, ez,
                &hx, &hy, &hz, &side))
            continue;
        dx = hx - sx; dy = hy - sy; dz = hz - sz;
        dist = dx * dx + dy * dy + dz * dz;
        if (best != 0 && !(dist < best_dist)) continue;
        best = -(i + 1);
        best_dist = dist;
    }
    if (best == 0) return 0;
    if (target) *target = best;
    if (distance_sq) *distance_sq = best_dist;
    return 1;
}

int gm_dragon_small_fireball_hit(
        GmDragonLive *d, int target, GmDragonCrystalHit *crystal_hit) {
    if (!d || !d->initialized || target == 0) return 0;
    if (target > 0)
        return target <= 8 && d->state.arena.dragon.alive;
    int index = -target - 1;
    if (index < 0 || index >= ED_NUM_CRYSTALS
            || !d->state.arena.crystals[index].alive)
        return 0;
    EdCrystal *c = &d->state.arena.crystals[index];
    set_crystal_hit(crystal_hit,index,c);
    if(!ed_mark_crystal_destroyed(&d->state.arena,index))return 0;
    return 2;
}
