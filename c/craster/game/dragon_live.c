#include "player_survival.h"
#include "game/dragon_live.h"

#include "combat_math.h"
#include "items_tools_armor.h"

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

void gm_dragon_init(GmDragonLive *d,GmWorld *world,long long seed){
    memset(d,0,sizeof *d);EdeScenario sc={(u64)seed,0,0};ede_init_scene(&d->state,&sc);
    d->initialized=1;apply_podium(&d->state,world);
    for(int i=0;i<ED_NUM_CRYSTALS;++i){
        double x,y,z;ed_pillar_crystal_pos(i,&x,&y,&z);
        int bx=(int)floor(x),bz=(int)floor(z),top=(int)floor(y)-2;
        for(int by=45;by<=top;++by)gm_world_set_block(world,bx,by,bz,49);
        gm_world_set_block(world,bx,top+1,bz,7);
    }
}

int gm_dragon_player_attack(GmDragonLive *d,const struct PsvPlayer *player_,int ox,int oz){
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
    if(crystal>=0)d->state.arena.crystals[crystal].alive=0;
    else {g->health-=held_damage(p);if(g->health<0)g->health=0;}
    damage_held_weapon((PsvPlayer *)p);
    d->player_attack_cooldown=10;return 1;
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
    if(g->alive&&n<max)out[n++]=(GmEntityView){GM_ENTITY_DRAGON,(float)g->x,(float)g->y,(float)g->z,g->yaw,g->health};
    for(int i=0;i<ED_NUM_CRYSTALS&&n<max;++i)if(d->state.arena.crystals[i].alive){
        const EdCrystal *c=&d->state.arena.crystals[i];out[n++]=(GmEntityView){GM_ENTITY_CRYSTAL,(float)c->x,(float)c->y,(float)c->z,0,5};
    }return n;
}

int gm_dragon_damage_near(GmDragonLive *d,double x,double y,double z,double radius,float damage){
    if(!d||!d->initialized)return 0;
    double r2=radius*radius;
    for(int i=0;i<ED_NUM_CRYSTALS;++i)if(d->state.arena.crystals[i].alive){EdCrystal *c=&d->state.arena.crystals[i];
        double dx=c->x-x,dy=c->y-y,dz=c->z-z;if(dx*dx+dy*dy+dz*dz<=r2){c->alive=0;return 1;}}
    EdDragon *g=&d->state.arena.dragon;double dx=g->x-x,dy=(g->y+2)-y,dz=g->z-z;
    if(g->alive&&g->death_ticks==0&&dx*dx+dy*dy+dz*dz<=r2*9.0){g->health-=damage;if(g->health<0)g->health=0;return 1;}
    return 0;
}
