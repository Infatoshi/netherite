#include "player_survival.h"
#include "game/dragon_live.h"

#include <math.h>
#include <stdio.h>

int main(void){
    GmWorld *w=gm_world_create_type(0,3);if(!w)return 1;gm_world_ensure(w,0,0,1);
    GmDragonLive d;gm_dragon_init(&d,w,0);
    for(int i=0;i<ED_NUM_CRYSTALS;++i)d.state.arena.crystals[i].alive=0;
    PsvPlayer p;psv_player_init(&p);isr_init(&p.inv);p.inv.current_item=0;
    isr_set_stack(&p.inv,0,ic_mk(276,1,0));McSinTable st;mc_sin_table_init(&st);
    for(int t=0;t<400&&d.state.arena.dragon.health>0;++t){
        EdDragon *g=&d.state.arena.dragon;
        p.ent.posX=g->x;p.ent.posY=g->y+0.38;p.ent.posZ=g->z-2.5;p.yaw=0;p.pitch=0;
        gm_dragon_player_attack(&d,(const struct PsvPlayer *)&p,0,0);
        gm_dragon_tick(&d,w,(const struct McSinTable *)&st,p.ent.posX,p.ent.posY,p.ent.posZ);
    }
    if(d.state.arena.dragon.health>0){fprintf(stderr,"dragon_live: melee did not kill dragon, hp=%g\n",d.state.arena.dragon.health);return 1;}
    for(int t=0;t<200&&!d.state.death_processed;++t)
        gm_dragon_tick(&d,w,(const struct McSinTable *)&st,0,64,0);
    int portals=0;for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z)portals+=gm_world_block(w,x,63,z)==119;
    gm_world_destroy(w);
    if(!d.state.death_processed||d.state.arena.dragon.death_ticks!=200||portals<1){
        fprintf(stderr,"dragon_live: death=%d ticks=%d portals=%d\n",d.state.death_processed,d.state.arena.dragon.death_ticks,portals);return 1;
    }
    fprintf(stderr,"dragon_live: PASS melee/death/portal\n");return 0;
}
