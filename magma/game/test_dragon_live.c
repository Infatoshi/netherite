#include "player_survival.h"
#include "game/dragon_live.h"

#include <math.h>
#include <stdio.h>

int main(void){
    GmWorld *w=gm_world_create_type(0,3);if(!w)return 1;gm_world_ensure(w,0,0,1);
    GmDragonLive d;gm_dragon_init(&d,w,0);
    d.state.tick=12;d.state.arena.dragon.ticks_existed=12;
    d.state.arena.dragon.heal_crystal_idx=0;
    {
        GmEntityView views[ED_NUM_CRYSTALS+1];
        int count=gm_dragon_fill_views(&d,views,ED_NUM_CRYSTALS+1);
        if(count!=ED_NUM_CRYSTALS+1||views[0].type!=GM_ENTITY_DRAGON
                ||!views[0].has_heal_beam||views[0].ticks_existed!=12
                ||views[0].heal_x!=(float)d.state.arena.crystals[0].x
                ||views[1].type!=GM_ENTITY_CRYSTAL
                ||views[1].crystal_rot!=12.0f||!views[1].show_bottom){
            fprintf(stderr,"dragon_live: healing beam/render state mismatch\n");return 1;}
    }
    {
        EdArena a;ed_init(&a,1);a.dragon.health=100;a.dragon.ticks_existed=8;
        a.dragon.heal_crystal_idx=0;
        for(int i=0;i<ED_NUM_CRYSTALS;++i)a.crystals[i].alive=0;
        a.crystals[0]=(EdCrystal){0,100,10,1};
        ++a.dragon.ticks_existed;ed_update_healing_crystal(&a);
        ++a.dragon.ticks_existed;ed_update_healing_crystal(&a);
        if(a.dragon.health!=101||a.dragon.heal_crystal_idx!=0){
            fprintf(stderr,"dragon_live: tick-10 healer cadence mismatch\n");return 1;}
        ed_init(&a,3);a.dragon.heal_crystal_idx=-1;
        for(int i=0;i<ED_NUM_CRYSTALS;++i)a.crystals[i].alive=0;
        a.crystals[0]=(EdCrystal){20,100,0,1};a.crystals[1]=(EdCrystal){5,100,0,1};
        ++a.dragon.ticks_existed;ed_update_healing_crystal(&a);
        if(a.dragon.heal_crystal_idx!=-1)return 1;
        ++a.dragon.ticks_existed;ed_update_healing_crystal(&a);
        if(a.dragon.heal_crystal_idx!=1){
            fprintf(stderr,"dragon_live: one-in-ten selection mismatch\n");return 1;}
    }
    d.state.tick=0;
    /* EntityDragon.onCrystalDestroyed runs only after the crystal's nested
     * explosion. The shared split API exposes that ordering exactly. */
    d.state.arena.dragon.health=100.0f;
    d.state.arena.dragon.heal_crystal_idx=0;
    d.state.arena.dragon.phase=ED_PHASE_CIRCLE;
    d.state.arena.dragon.phase_ticks=37;
    d.state.arena.player.x=500.0;
    d.state.arena.player.y=200.0;
    d.state.arena.player.z=500.0;
    if(!ed_mark_crystal_destroyed(&d.state.arena,0)
            ||d.state.arena.dragon.health!=100.0f){
        fprintf(stderr,"dragon_live: crystal mark did not preserve pre-notify health\n");return 1;}
    gm_dragon_crystal_destroyed(&d,0,1,1);
    if(d.state.arena.dragon.health!=90.0f
            ||d.state.arena.dragon.phase!=ED_PHASE_STRAFE
            ||d.state.arena.dragon.phase_ticks!=0){
        fprintf(stderr,"dragon_live: healing-crystal notification mismatch\n");return 1;}
    gm_dragon_init(&d,w,0);
    d.state.arena.dragon.health=100.0f;
    d.state.arena.dragon.heal_crystal_idx=0;
    d.state.arena.dragon.phase=ED_PHASE_CIRCLE;
    d.state.arena.player.x=500.0;
    d.state.arena.player.y=200.0;
    d.state.arena.player.z=500.0;
    if(!ed_mark_crystal_destroyed(&d.state.arena,1))return 1;
    gm_dragon_crystal_destroyed(&d,1,0,1);
    if(d.state.arena.dragon.health!=100.0f
            ||d.state.arena.dragon.phase!=ED_PHASE_CIRCLE){
        fprintf(stderr,"dragon_live: non-healing/distant negative mismatch\n");return 1;}
    gm_dragon_init(&d,w,0);
    for(int i=0;i<ED_NUM_CRYSTALS;++i)d.state.arena.crystals[i].alive=0;
    PsvPlayer p;psv_player_init(&p);isr_init(&p.inv);p.inv.current_item=0;
    isr_set_stack(&p.inv,0,ic_mk(276,1,0));McSinTable st;mc_sin_table_init(&st);
    for(int t=0;t<400&&d.state.arena.dragon.health>0;++t){
        EdDragon *g=&d.state.arena.dragon;
        p.ent.posX=g->x;p.ent.posY=g->y+0.38;p.ent.posZ=g->z-2.5;p.yaw=0;p.pitch=0;
        gm_dragon_player_attack(&d,(const struct PsvPlayer *)&p,0,0,0);
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
    fprintf(stderr,"dragon_live: PASS healer/crystal-notify/melee/death/portal\n");return 0;
}
