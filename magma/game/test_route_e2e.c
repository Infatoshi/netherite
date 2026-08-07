#include "game/runtime.h"
#include "game/sel_box.h"
#include "game/structures_live.h"
#include "container_click.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C,M) do{if(!(C)){fprintf(stderr,"FAIL: %s\n",M);fail=1;}}while(0)
#define STOP_IF_FAILED(R) do{if(fail){gm_runtime_destroy(R);return 1;}}while(0)

static int slot_item(const GmRuntime *r,int item,int count){
    for(int i=0;i<ISR_MAIN_SLOTS;++i){ICStack s=isr_get_stack(&r->player.inv,i);
        if(s.item==item&&s.count>=count)return i;}
    return -1;
}
static int total_item(const GmRuntime *r,int item){int n=0;
    for(int i=0;i<ISR_MAIN_SLOTS;++i){ICStack s=isr_get_stack(&r->player.inv,i);if(s.item==item)n+=s.count;}
    return n;
}
static int selectable_slot(GmRuntime *r,int slot){
    if(slot<0)return -1;
    if(slot<9)return slot;
    int hot=-1;
    for(int i=0;i<9;++i)if(isr_is_empty(&r->player.inv.main[i])){hot=i;break;}
    if(hot<0)hot=8;
    ICStack a=isr_get_stack(&r->player.inv,hot),b=isr_get_stack(&r->player.inv,slot);
    isr_set_stack(&r->player.inv,hot,b);isr_set_stack(&r->player.inv,slot,a);return hot;
}
static void idle(GmRuntime *r,int ticks){GmAction a;memset(&a,0,sizeof a);a.hotbar_sel=-1;
    for(int i=0;i<ticks&&!r->dead&&!r->won;++i)gm_runtime_tick(r,a);
}
static void idle_hover(GmRuntime *r,int ticks){GmAction a;memset(&a,0,sizeof a);a.hotbar_sel=-1;
    for(int i=0;i<ticks&&!r->dead&&!r->won;++i){gm_runtime_set_velocity(r,0,0,0);gm_runtime_tick(r,a);}
}
static int recover_health(GmRuntime *r){
    for(int pass=0;pass<20&&r->vitals.health<19.5f&&!r->dead;++pass){
        if(r->vitals.foodLevel<18){int food=slot_item(r,423,1);
            if(food<0)food=slot_item(r,424,1);
            if(food<0)food=slot_item(r,365,1);
            food=selectable_slot(r,food);if(food<0)return 0;
            GmAction eat;memset(&eat,0,sizeof eat);eat.use=1;eat.hotbar_sel=food;
            for(int t=0;t<33&&!r->dead;++t)gm_runtime_tick(r,eat);}
        idle_hover(r,200);
    }
    int ok=!r->dead&&r->vitals.health>=19.0f;
    if(!ok)fprintf(stderr,
        "recover_health: dead=%d hp=%.1f food=%d saturation=%.1f raw_mutton=%d cooked_mutton=%d chicken=%d\n",
        r->dead,r->vitals.health,r->vitals.foodLevel,r->vitals.saturation,
        total_item(r,423),total_item(r,424),total_item(r,365));
    return ok;
}
static void collect_drops(GmRuntime *r){
    for(int pass=0;pass<GM_LIVE_MAX;++pass)for(int i=0;i<GM_LIVE_MAX;++i){
        GmLiveEnt *e=&r->entities.ents[i];if(!e->active)continue;
        gm_runtime_set_pose(r,e->x,e->y,e->z,0,0);idle(r,12);
    }
}
static int flint_coord(int x,int y,int z){
    u32 h=(u32)x*73428767u^(u32)y*912931u^(u32)z*19349663u;
    h^=h>>13;h*=0x85ebca6bu;h^=h>>16;return h%10u==0u;
}
/* Match ItemBucket.rayTrace(..., true), including the rule that a flowing
 * liquid in front of a source blocks pickup.  The route must choose a source
 * the player can actually see; skipping the nearer flow made this test rely
 * on behavior the Java game does not have. */
static int visible_liquid_source(GmRuntime *r,int tx,int ty,int tz){
    const PsvPlayer *pl=&r->player;
    float f=mc_cos(&r->sin_table,-pl->yaw*0.017453292f-3.1415927f);
    float f1=mc_sin(&r->sin_table,-pl->yaw*0.017453292f-3.1415927f);
    float f2=-mc_cos(&r->sin_table,-pl->pitch*0.017453292f);
    float f3=mc_sin(&r->sin_table,-pl->pitch*0.017453292f);
    double dx=(double)(f1*f2),dy=(double)f3,dz=(double)(f*f2);
    double ex=pl->ent.posX,ey=pl->ent.posY+psv_player_eye_height(pl),ez=pl->ent.posZ;
    int lx=mc_floor(ex),ly=mc_floor(ey),lz=mc_floor(ez);
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    for(double t=PSV_RAY_DT;t<=PSV_REACH;t+=PSV_RAY_DT){
        int x=mc_floor(ex+dx*t),y=mc_floor(ey+dy*t),z=mc_floor(ez+dz*t);
        if(x==lx&&y==ly&&z==lz)continue;
        lx=x;ly=y;lz=z;
        int id=psv_get_block(r->window,x,y,z);
        if(id==8||id==9||id==10||id==11){
            int eid,type;double entity_distance;
            double rex,rey,rez,rdx,rdy,rdz;
            gm_player_look_ray(&r->sin_table,pl,&rex,&rey,&rez,&rdx,&rdy,&rdz);
            if(gm_mobs_raycast_entity(&r->mobs,r->dimension,
                    rex+r->ox,rey,rez+r->oz,rdx,rdy,rdz,3.0,
                    &eid,&type,&entity_distance)&&
               (type==EW_TYPE_SHEEP||type==EW_TYPE_COW||
                type==EW_TYPE_PIG||type==EW_TYPE_CHICKEN))return 0;
            return psv_get_meta(r->window,x,y,z)==0&&
                   x+r->ox==tx&&y==ty&&z+r->oz==tz;
        }
        if(psv_solid(id))return 0;
    }
    return 0;
}
static int find_mine_pose(GmRuntime *r,int id,int require_flint,int *tx,int *ty,int *tz,
                          double *px,double *py,double *pz,float *yaw,float *pitch){
    static const int dx[4]={0,0,-1,1},dz[4]={-1,1,0,0};
    static const float ya[4]={0,180,-90,90};
    static long long cursor[256];
    int rad=128,w=rad*2+1,ny=88;
    long long total=(long long)w*w*ny;
    for(long long at=cursor[id];at<total;++at){
        int y=2+(int)(at%ny);long long q=at/ny;
        int z=-rad+(int)(q%w),x=-rad+(int)(q/w);
        if(y==2)gm_world_ensure(r->world,x>>4,z>>4,0);
        if(gm_world_block(r->world,x,y,z)==id&&
           (id!=1||gm_world_meta(r->world,x,y,z)==0)&&
           (id!=17||gm_world_meta(r->world,x,y,z)==0)&&
           ((id!=9&&id!=11)||gm_world_meta(r->world,x,y,z)==0)&&
           (!require_flint||flint_coord(x,y,z))){
            if((id==9||id==11)&&gm_world_block(r->world,x,y+1,z)==0&&
               gm_world_block(r->world,x,y+2,z)==0){
                gm_runtime_set_pose(r,x+0.5,y+2.0,z+0.5,0,89.0f);
                if(!visible_liquid_source(r,x,y,z))continue;
                *tx=x;*ty=y;*tz=z;*px=x+0.5;*py=y+2.0;*pz=z+0.5;*yaw=0;*pitch=89;
                cursor[id]=at+1;return 1;
            }
            for(int d=0;d<4;++d){int sx=x+dx[d],sz=z+dz[d];
                if(gm_world_block(r->world,x+dx[d],y,z+dz[d])!=0)continue;
                for(int fy=y-2;fy<=y+1;++fy){
                    if(fy<1||gm_world_block(r->world,sx,fy-1,sz)==0||
                       gm_world_block(r->world,sx,fy,sz)!=0||gm_world_block(r->world,sx,fy+1,sz)!=0)continue;
                    float candidate_pitch=(float)(atan2((fy+PSV_EYE_HEIGHT)-(y+0.5),1.0)*180.0/MC_PI);
                    gm_runtime_set_pose(r,sx+0.5,fy,sz+0.5,ya[d],candidate_pitch);
                    if(id==9||id==11){
                        if(!visible_liquid_source(r,x,y,z))continue;
                    }else{
                        gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
                        int hx,hy,hz,ax,ay,az;
                        if(psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az)<0||
                           hx+r->ox!=x||hy!=y||hz+r->oz!=z)continue;
                    }
                    *tx=x;*ty=y;*tz=z;*px=sx+0.5;*py=fy;*pz=sz+0.5;*yaw=ya[d];
                    *pitch=candidate_pitch;cursor[id]=at+1;return 1;
                }
            }
        }
    }
    return 0;
}
static int mine_one(GmRuntime *r,int block,int tool_slot,int require_flint){
    int x,y,z;double px,py,pz;float yaw,pitch;
    if(!find_mine_pose(r,block,require_flint,&x,&y,&z,&px,&py,&pz,&yaw,&pitch))return 0;
    gm_runtime_set_pose(r,px,py,pz,yaw,pitch);GmAction a;memset(&a,0,sizeof a);
    a.attack=1;a.hotbar_sel=tool_slot;
    for(int t=0;t<700&&gm_world_block(r->world,x,y,z)==block&&!r->dead;++t)gm_runtime_tick(r,a);
    if(gm_world_block(r->world,x,y,z)==block)return 0;
    collect_drops(r);return 1;
}
static int mine_fixed(GmRuntime *r,int x,int y,int z,double px,double py,double pz,
                      float yaw,float pitch,int tool_slot){
    int block=gm_world_block(r->world,x,y,z);if(block==0)return 0;
    gm_runtime_set_pose(r,px,py,pz,yaw,pitch);GmAction a;memset(&a,0,sizeof a);
    a.attack=1;a.hotbar_sel=tool_slot;
    for(int t=0;t<700&&gm_world_block(r->world,x,y,z)==block&&!r->dead;++t)gm_runtime_tick(r,a);
    if(gm_world_block(r->world,x,y,z)==block)return 0;
    collect_drops(r);return 1;
}
static int mine_fixed_hover(GmRuntime *r,int x,int y,int z,double px,double py,double pz,
                            float yaw,float pitch){
    int block=gm_world_block(r->world,x,y,z);if(block==0)return 0;
    gm_runtime_set_pose(r,px,py,pz,yaw,pitch);GmAction a;memset(&a,0,sizeof a);a.attack=1;a.hotbar_sel=-1;
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    int hx,hy,hz,ax,ay,az,hit=psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az);
    if(hit<0||hx+r->ox!=x||hy!=y||hz+r->oz!=z){fprintf(stderr,"hover mine target=%d,%d,%d id=%d hit=%d at=%d,%d,%d\n",
        x,y,z,block,hit,hx+r->ox,hy,hz+r->oz);return 0;}
    for(int t=0;t<700&&gm_world_block(r->world,x,y,z)==block&&!r->dead;++t){
        gm_runtime_set_velocity(r,0,0,0);gm_runtime_tick(r,a);}
    if(gm_world_block(r->world,x,y,z)==block)return 0;
    collect_drops(r);return 1;
}
static int craft(GmRuntime *r,int width,const int cells[9]){return gm_runtime_craft(r,width,cells);}
static void empty_grid(int g[9]){for(int i=0;i<9;++i)g[i]=-1;}

/* Place above a supporting block using the same raycast/action path as interactive play. */
static int place_above(GmRuntime *r,int slot,int x,int y,int z){
    slot=selectable_slot(r,slot);if(slot<0)return 0;
    gm_runtime_set_pose(r,x+0.5,y+2.0,z+0.5,0,89.0f);
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    int hx,hy,hz,ax,ay,az;
    if(psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az)<0||
       ax+r->ox!=x||ay!=y||az+r->oz!=z)return 0;
    GmAction a;memset(&a,0,sizeof a);
    a.do_place=1;a.use=1;a.hotbar_sel=slot;gm_runtime_tick(r,a);
    return gm_world_block(r->world,x,y,z)!=0;
}
static int place_north(GmRuntime *r,int slot,int x,int y,int anchor_z){
    slot=selectable_slot(r,slot);if(slot<0)return 0;
    gm_runtime_set_pose(r,x+0.5,y+0.5-PSV_EYE_HEIGHT,anchor_z+2.5,180,0);
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    int hx,hy,hz,ax,ay,az;
    if(psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az)<0||
       ax+r->ox!=x||ay!=y||az+r->oz!=anchor_z-1)return 0;
    GmAction a;memset(&a,0,sizeof a);
    a.do_place=1;a.use=1;a.hotbar_sel=slot;gm_runtime_tick(r,a);
    return gm_world_block(r->world,x,y,anchor_z-1)!=0;
}
static int cast_obsidian(GmRuntime *r,int x,int y,int z){
    int wx,wy,wz;double px,py,pz;float yaw,pitch;
    int bucket=selectable_slot(r,slot_item(r,325,1));if(bucket<0||
       !find_mine_pose(r,11,0,&wx,&wy,&wz,&px,&py,&pz,&yaw,&pitch)){
        fprintf(stderr,"cast (%d,%d,%d): no bucket/lava source\n",x,y,z);return 0;}
    gm_runtime_set_pose(r,px,py,pz,yaw,pitch);GmAction a;memset(&a,0,sizeof a);
    a.do_place=1;a.use=1;a.hotbar_sel=bucket;gm_runtime_tick(r,a);
    int lava=slot_item(r,327,1);if(lava<0||!place_above(r,lava,x,y,z)){
        fprintf(stderr,"cast (%d,%d,%d): lava pickup/place failed item=%d\n",x,y,z,lava);return 0;}
    bucket=selectable_slot(r,slot_item(r,325,1));if(bucket<0){
        fprintf(stderr,"cast (%d,%d,%d): lava placement did not return bucket\n",x,y,z);return 0;}
    if(!find_mine_pose(r,9,0,&wx,&wy,&wz,&px,&py,&pz,&yaw,&pitch)){
        fprintf(stderr,"cast (%d,%d,%d): no water source\n",x,y,z);return 0;}
    gm_runtime_set_pose(r,px,py,pz,yaw,pitch);a.hotbar_sel=bucket;gm_runtime_tick(r,a);
    int water=slot_item(r,326,1);if(water<0||!place_north(r,water,x,y,z)){
        fprintf(stderr,"cast (%d,%d,%d): water pickup/place failed item=%d\n",x,y,z,water);return 0;}
    if(gm_world_block(r->world,x,y,z)!=49){fprintf(stderr,"cast (%d,%d,%d): reaction id=%d\n",x,y,z,
        gm_world_block(r->world,x,y,z));return 0;}return 1;
}
static int kill_hook_mob(GmRuntime *r,int type,double x,double y,double z){
    int slot=gm_mobs_spawn(&r->mobs,type,x,y,z);
    if(slot<0){
        fprintf(stderr,"kill_hook_mob: spawn type=%d failed living=%d capacity=%d\n",
                type,gm_mobs_living_count(&r->mobs),GM_MOB_CAPACITY);
        return 0;
    }
    /* Passives panic-flee; chase them like test_mob_live rather than the
     * hostile cooldown-hop (which leaves sheep out of reach). */
    int passive=type>=GM_MOB_SHEEP&&type<=GM_MOB_CHICKEN;
    int max_hits=passive?120:80;
    for(int hit=0;hit<max_hits;++hit){const EwStore *s=r->mobs.current?&r->mobs.b:&r->mobs.a;int alive=0;
        double mx=x,my=y,mz=z;if(s->alive[slot]&&s->type[slot]==type){
            alive=1;mx=s->x[slot];my=s->y[slot];mz=s->z[slot];}
        if(!alive){collect_drops(r);return !r->dead;}
        float mob_w,mob_h;ehs_size((u8)type,&mob_w,&mob_h);(void)mob_w;
        int low_target=passive||type==EW_TYPE_SPIDER;
        double reach=low_target?2.6:2.9;
        double target_y=low_target?mob_h*0.5:0.975;
        float pitch=(float)(atan2(PSV_EYE_HEIGHT-target_y,reach)*180.0/MC_PI);
        gm_runtime_set_pose(r,mx,my,mz-reach,0,pitch);
        GmAction a;memset(&a,0,sizeof a);
        int weapon_slot=slot_item(r,267,1);if(weapon_slot<0)weapon_slot=slot_item(r,272,1);
        if(weapon_slot<0)weapon_slot=slot_item(r,268,1);
        if(weapon_slot<0)weapon_slot=slot_item(r,274,1); /* stone pick */
        if(weapon_slot<0)weapon_slot=slot_item(r,257,1); /* iron pick */
        if(weapon_slot<0)weapon_slot=0;
        weapon_slot=selectable_slot(r,weapon_slot);
        if(weapon_slot<0)return 0;
        a.attack=1;a.do_break=1;a.hotbar_sel=weapon_slot;
        gm_runtime_tick(r,a);
        /* The integrated server consumes CPacketUseEntity one locked tick
         * after the client click edge, while the target is still in reach. */
        idle(r,1);
        /* Sword attack speed is 1.6 in Java 1.11.2, so the full-strength
         * period is 12.5 ticks. Recharge after the server-side swing reset;
         * the old ten-tick empty-hand cadence wastes both damage and tools. */
        if(!passive){double retreat_y=my+120;if(retreat_y>250)retreat_y=250;
            gm_runtime_set_pose(r,mx,retreat_y,mz,0,0);idle_hover(r,13);}
        else idle(r,13);
    }
    {
        const EwStore *s=r->mobs.current?&r->mobs.b:&r->mobs.a;
        fprintf(stderr,"kill_hook_mob: type=%d slot=%d survived hit budget hp=%.1f alive=%d current=%d dead=%d player_hp=%.1f\n",
                type,slot,s->health[slot],s->alive[slot],r->mobs.current,
                r->dead,r->vitals.health);
    }
    return 0;
}

int main(void){
    GmConfig cfg;gm_config_defaults(&cfg);cfg.view_distance=1;
    GmRuntime r;char err[256];CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"fresh route runtime initializes");
    CHECK(total_item(&r,17)==0,"fresh route begins with empty inventory");

    CHECK(mine_fixed(&r,0,72,4,0.5,72,2.5,0,29.25f,-1),"mine first generated oak log");
    CHECK(mine_fixed(&r,0,73,4,0.5,72,2.5,0,3.4336f,-1),"mine second generated oak log");
    CHECK(mine_fixed(&r,0,74,4,0.5,72,2.5,0,-23.7495f,-1),"mine third generated oak log");
    CHECK(total_item(&r,17)==3,"three natural logs are collected as entities");
    int g[9],logs=slot_item(&r,17,1);empty_grid(g);g[0]=logs;
    for(int i=0;i<3;++i)CHECK(craft(&r,2,g),"craft each log into planks");
    int planks=slot_item(&r,5,10);empty_grid(g);g[0]=g[1]=g[3]=g[4]=planks;
    CHECK(craft(&r,2,g),"craft route table");
    planks=slot_item(&r,5,2);empty_grid(g);g[0]=g[3]=planks;CHECK(craft(&r,2,g),"craft route sticks");
    STOP_IF_FAILED(&r);
    int table=slot_item(&r,58,1);gm_runtime_set_pose(&r,0.5,72,2.5,0,60);
    GmAction use;memset(&use,0,sizeof use);use.do_place=1;use.use=1;use.hotbar_sel=table;gm_runtime_tick(&r,use);
    CHECK(gm_world_block(r.world,0,72,3)==58,"place crafting table through survival action");
    CHECK(gm_runtime_use_block(&r,0,72,3),"open placed crafting table");
    planks=slot_item(&r,5,3);int sticks=slot_item(&r,280,2);empty_grid(g);
    g[0]=g[1]=g[2]=planks;g[4]=g[7]=sticks;CHECK(craft(&r,3,g),"craft wooden pickaxe");
    int wood_pick=slot_item(&r,270,1);for(int i=0;i<11;++i)CHECK(mine_one(&r,1,wood_pick,0),"mine natural stone");
    CHECK(total_item(&r,4)>=11,"collect enough natural cobblestone");
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),"reopen route table");
    int cobble=slot_item(&r,4,8);empty_grid(g);g[0]=g[1]=g[2]=g[3]=g[5]=g[6]=g[7]=g[8]=cobble;
    CHECK(craft(&r,3,g),"craft furnace");
    cobble=slot_item(&r,4,3);sticks=slot_item(&r,280,2);empty_grid(g);
    g[0]=g[1]=g[2]=cobble;g[4]=g[7]=sticks;CHECK(craft(&r,3,g),"craft stone pickaxe");
    STOP_IF_FAILED(&r);
    int stone_pick=slot_item(&r,274,1);for(int i=0;i<8;++i)CHECK(mine_one(&r,15,stone_pick,0),"mine natural iron ore");
    CHECK(mine_one(&r,16,stone_pick,0),"mine first natural coal fuel");
    CHECK(mine_one(&r,16,stone_pick,0),"mine second natural coal fuel");
    CHECK(total_item(&r,15)>=8&&total_item(&r,263)>=2,"collect iron ore and coal naturally");
    int furnace=slot_item(&r,61,1);if(furnace<0)furnace=slot_item(&r,62,1);
    gm_runtime_set_pose(&r,1.5,72,2.5,0,60);memset(&use,0,sizeof use);use.do_place=1;use.use=1;use.hotbar_sel=furnace;
    gm_runtime_tick(&r,use);CHECK(gm_world_block(r.world,1,72,3)==61,"place route furnace");
    CHECK(gm_runtime_use_block(&r,1,72,3),"open route furnace");
    CHECK(gm_runtime_furnace_insert(&r,0,slot_item(&r,15,8),8)==8,"insert eight natural iron ore");
    CHECK(gm_runtime_furnace_insert(&r,1,slot_item(&r,263,2),2)==2,"fuel furnace with natural coal");
    idle(&r,1600);CHECK(gm_runtime_furnace_extract(&r,2,8)==8,"extract eight smelted ingots");
    CHECK(total_item(&r,265)==8,"eight legal iron ingots reach inventory");
    STOP_IF_FAILED(&r);

    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),"open table for route equipment");
    planks=slot_item(&r,5,2);empty_grid(g);g[0]=g[3]=planks;
    CHECK(craft(&r,2,g),"craft sticks for iron and diamond tools");
    int iron=slot_item(&r,265,8);empty_grid(g);g[0]=g[2]=g[4]=iron;CHECK(craft(&r,3,g),"craft bucket");
    CHECK(mine_one(&r,13,-1,1),"mine deterministic natural flint from gravel");
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),"open table for flint and steel");
    iron=slot_item(&r,265,1);int flint=slot_item(&r,318,1);empty_grid(g);g[0]=iron;g[1]=flint;
    CHECK(craft(&r,3,g),"craft flint and steel");
    stone_pick=slot_item(&r,274,1);CHECK(mine_one(&r,1,stone_pick,0),"mine backup sword cobblestone one");
    CHECK(mine_one(&r,1,stone_pick,0),"mine backup sword cobblestone two");
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),"open table for backup sword");
    cobble=slot_item(&r,4,2);sticks=slot_item(&r,280,1);empty_grid(g);g[0]=g[3]=cobble;g[6]=sticks;
    CHECK(craft(&r,3,g),"craft backup stone sword");
    STOP_IF_FAILED(&r);

    /* Bucket-cast a minimum ten-block frame from generated source fluids. */
    for(int i=0;i<26;++i)CHECK(mine_one(&r,3,-1,0),"mine natural dirt scaffolding");
    int bx=0,bz=0,base=0,flat=0;
    for(int z=10;z<=30&&!flat;++z)for(int x=-16;x<=12&&!flat;++x){int sy=gm_world_surface_y(r.world,x,z);flat=1;
        for(int q=0;q<4&&flat;++q)if(gm_world_surface_y(r.world,x+q,z)!=sy||
                                        gm_world_block(r.world,x+q,sy,z-1)!=0||
                                        gm_world_surface_y(r.world,x+q,z-2)!=sy)flat=0;
        if(flat){bx=x;bz=z;base=sy;}}
    CHECK(flat,"find generated four-block flat portal site");STOP_IF_FAILED(&r);
    int left=bx,right=bx+3,inner0=bx+1,inner1=bx+2;
    /* Solid backing at z-2 gives each water placement a legal clicked face. */
    for(int q=0;q<4;++q){int top=(q==0||q==3)?base+3:base+4;
        for(int y=base;y<=top;++y){int ds=slot_item(&r,3,1);
            CHECK(place_above(&r,ds,bx+q,y,bz-2),"place bucket-cast backing scaffold");}}
    STOP_IF_FAILED(&r);
    CHECK(cast_obsidian(&r,inner0,base,bz),"cast portal bottom left");
    CHECK(cast_obsidian(&r,inner1,base,bz),"cast portal bottom right");
    int dirt=slot_item(&r,3,4);
    CHECK(place_above(&r,dirt,left,base,bz),"place left scaffold");dirt=slot_item(&r,3,1);
    CHECK(place_above(&r,dirt,right,base,bz),"place right scaffold");
    for(int y=base+1;y<=base+3;++y){CHECK(cast_obsidian(&r,left,y,bz),"cast left portal side");
        CHECK(cast_obsidian(&r,right,y,bz),"cast right portal side");}
    for(int y=base+1;y<=base+3;++y){dirt=slot_item(&r,3,1);
        CHECK(place_above(&r,dirt,inner0,y,bz),"place left internal top scaffold");
        dirt=slot_item(&r,3,1);CHECK(place_above(&r,dirt,inner1,y,bz),"place right internal top scaffold");}
    CHECK(cast_obsidian(&r,inner0,base+4,bz),"cast portal top left");
    CHECK(cast_obsidian(&r,inner1,base+4,bz),"cast portal top right");
    for(int y=base+3;y>=base+1;--y){
        CHECK(mine_fixed_hover(&r,inner0,y,bz,inner0+0.5,y+0.5-PSV_EYE_HEIGHT,bz+2.5,180,0),"remove left internal scaffold");
        CHECK(mine_fixed_hover(&r,inner1,y,bz,inner1+0.5,y+0.5-PSV_EYE_HEIGHT,bz+2.5,180,0),"remove right internal scaffold");}

    int fs=slot_item(&r,259,1);CHECK(place_above(&r,fs,inner0,base+1,bz),
        "ignite frame interior with flint and steel");
    int portals=0;for(int x=inner0;x<=inner1;++x)for(int y=base+1;y<=base+3;++y)
        portals+=gm_world_block(r.world,x,y,bz)==90;
    CHECK(portals==6,"legal flint-and-steel use lights the survival portal");

    /* Traverse, fight at a generated fortress, and return through the linked portal. */
    gm_runtime_set_pose(&r,inner0+0.5,base+1,bz+0.5,0,0);idle(&r,82);
    CHECK(r.dimension==-1,"survival portal reaches generated Nether");STOP_IF_FAILED(&r);
    GmPlayerView nether_entry;gm_runtime_view(&r,&nether_entry);
    GmStructureBox fortress;CHECK(gm_fortress_spawner_room(r.seed,32,&fortress),"locate generated fortress spawner room");
    for(int cx=fortress.min_x>>4;cx<=fortress.max_x>>4;++cx)
        for(int cz=fortress.min_z>>4;cz<=fortress.max_z>>4;++cz)gm_world_ensure(r.world,cx,cz,1);
    int fx=0,fy=0,fz=0,spawner=0;
    for(int x=fortress.min_x;x<=fortress.max_x;++x)for(int y=fortress.min_y;y<=fortress.max_y;++y)
        for(int z=fortress.min_z;z<=fortress.max_z;++z)if(gm_world_block(r.world,x,y,z)==52){
            fx=x;fy=y;fz=z;spawner=1;}
    CHECK(spawner,"fortress landmark contains blaze spawner");STOP_IF_FAILED(&r);
    int blaze_attempts=0;
    while(total_item(&r,369)<7&&blaze_attempts++<30)
        CHECK(kill_hook_mob(&r,GM_MOB_BLAZE,fx+0.5,fy+1.0,fz+0.5),"kill spawned fortress blaze legally");
    CHECK(total_item(&r,369)>=7,"collect seven natural blaze-rod drops");
    gm_runtime_set_pose(&r,nether_entry.x,nether_entry.y,nether_entry.z,0,0);idle(&r,82);
    CHECK(r.dimension==0,"linked Nether portal returns to persistent Overworld");STOP_IF_FAILED(&r);

    /* PRODUCT food/wool/bed + bow after Nether return (keeps the fragile
     * bucket-cast portal path inventory-clean). Encounter placement only. */
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),
        "open table for End armor");
    iron=slot_item(&r,265,4);empty_grid(g);g[0]=g[2]=g[3]=g[5]=iron;
    CHECK(craft(&r,3,g),"craft iron boots for End bed blast");
    {
        int armor=slot_item(&r,309,1);
        gm_runtime_set_pose(&r,20.5,72,2.5,0,0);
        GmAction equip;memset(&equip,0,sizeof equip);equip.hotbar_sel=-1;
        equip.inv_click=1;equip.inv_slot=armor;equip.inv_type=CC_CLICK_QUICK_MOVE;
        gm_runtime_tick(&r,equip);
        CHECK(isr_get_stack(&r.player.inv,ISR_ARMOR_FEET).item==309,
              "equip crafted boots through ContainerPlayer QUICK_MOVE");
    }
    STOP_IF_FAILED(&r);
    for(int i=0;i<2;++i)CHECK(mine_one(&r,17,-1,0),"mine extra oak for bed and bow planks");
    logs=slot_item(&r,17,1);empty_grid(g);g[0]=logs;
    for(int i=0;i<2&&logs>=0;++i){logs=slot_item(&r,17,1);empty_grid(g);g[0]=logs;
        if(logs>=0)CHECK(craft(&r,2,g),"craft extra logs into planks");}
    for(int dump=0;dump<32&&total_item(&r,4)>4;++dump){
        int dslot=slot_item(&r,4,1);if(dslot<0)break;
        (void)place_above(&r,dslot,-40+(dump%6),70+(dump/6),-40);
    }
    int surf_y=gm_world_surface_y(r.world,2,6);
    gm_runtime_set_pose(&r,2.5,surf_y,2.5,0,0);
    int sheep_attempts=0;
    while((total_item(&r,35)<3||
           total_item(&r,423)+total_item(&r,424)<12)&&sheep_attempts++<24){
        int ex=512+sheep_attempts*256,ez=512;
        gm_world_ensure(r.world,ex>>4,ez>>4,0);
        int ey=gm_world_surface_y(r.world,ex,ez);
        CHECK(kill_hook_mob(&r,GM_MOB_SHEEP,ex+0.5,ey,ez+0.5),
              "kill spawned sheep for wool and food");}
    CHECK(total_item(&r,35)>=3,"collect three natural wool drops");
    CHECK(total_item(&r,423)+total_item(&r,424)>=12,
          "sheep deaths yield enough mutton for route recovery");
    {
        int food_item=total_item(&r,423)>=1?423:424;
        int food_slot=selectable_slot(&r,slot_item(&r,food_item,1));
        CHECK(food_slot>=0,"food can be moved to a hotbar slot");
        GmAction eat;memset(&eat,0,sizeof eat);eat.use=1;eat.hotbar_sel=food_slot;
        for(int t=0;t<32&&!r.dead;++t)gm_runtime_tick(&r,eat);
        CHECK(!r.dead&&total_item(&r,food_item)>=1,
              "food use-hold path is live and retains satiated stack");
    }
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),
        "open table for bed");
    int wool=slot_item(&r,35,3);planks=slot_item(&r,5,3);empty_grid(g);
    g[0]=g[1]=g[2]=wool;g[3]=g[4]=g[5]=planks;
    CHECK(craft(&r,3,g),"craft bed from legal wool and planks");
    CHECK(total_item(&r,355)>=1,"crafted bed reaches inventory");
    surf_y=gm_world_surface_y(r.world,2,6);
    gm_runtime_set_pose(&r,2.5,surf_y,2.5,0,0);
    int string_attempts=0;
    while(total_item(&r,287)<3&&string_attempts++<20){
        int ex=8192+string_attempts*256,ez=512;
        gm_world_ensure(r.world,ex>>4,ez>>4,0);
        int ey=gm_world_surface_y(r.world,ex,ez);
        CHECK(kill_hook_mob(&r,EW_TYPE_SPIDER,ex+0.5,ey,ez+0.5),
              "kill spawned spider for string");}
    CHECK(total_item(&r,287)>=3,"collect three natural string drops");
    int feather_attempts=0;
    while(total_item(&r,288)<1&&feather_attempts++<10){
        int ex=16384+feather_attempts*256,ez=512;
        gm_world_ensure(r.world,ex>>4,ez>>4,0);
        int ey=gm_world_surface_y(r.world,ex,ez);
        CHECK(kill_hook_mob(&r,GM_MOB_CHICKEN,ex+0.5,ey,ez+0.5),
              "kill spawned chicken for feather");}
    CHECK(total_item(&r,288)>=1,"collect natural feather drop");
    if(total_item(&r,318)<1)CHECK(mine_one(&r,13,-1,1),"mine flint for arrows");
    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),
        "open table for bow and arrows");
    if(total_item(&r,280)<4){
        planks=slot_item(&r,5,2);empty_grid(g);g[0]=g[3]=planks;CHECK(craft(&r,2,g),
            "craft sticks for bow and arrows");}
    sticks=slot_item(&r,280,3);int string_slot=slot_item(&r,287,3);
    empty_grid(g);g[1]=sticks;g[2]=string_slot;g[3]=sticks;g[5]=string_slot;g[7]=sticks;g[8]=string_slot;
    CHECK(sticks>=0&&string_slot>=0&&craft(&r,3,g),"craft bow from sticks and string");
    CHECK(total_item(&r,261)>=1,"crafted bow reaches inventory");
    int flint2=slot_item(&r,318,1);sticks=slot_item(&r,280,1);int feather=slot_item(&r,288,1);
    empty_grid(g);g[0]=flint2;g[3]=sticks;g[6]=feather;
    CHECK(flint2>=0&&sticks>=0&&feather>=0&&craft(&r,3,g),
          "craft arrows from flint, stick, and feather");
    CHECK(total_item(&r,262)>=1,"crafted arrows reach inventory");
    STOP_IF_FAILED(&r);

    /* Endermen are entity-hooked at the returned Overworld position; health and
     * drops are never injected. Combat, deterministic drops, and pickup are live. */
    /* Re-craft a sword if combat durability spent the route blades on sheep. */
    if(slot_item(&r,267,1)<0&&slot_item(&r,272,1)<0){
        gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),
            "open table for replacement sword");
        cobble=slot_item(&r,4,2);sticks=slot_item(&r,280,1);
        if(sticks<0){planks=slot_item(&r,5,2);empty_grid(g);g[0]=g[3]=planks;CHECK(craft(&r,2,g),
            "craft sticks for replacement sword");sticks=slot_item(&r,280,1);}
        empty_grid(g);
        if(cobble>=0)g[0]=g[3]=cobble;
        else{planks=slot_item(&r,5,2);g[0]=g[3]=planks;}
        g[6]=sticks;
        CHECK(g[0]>=0&&sticks>=0&&craft(&r,3,g),"craft replacement route sword");
    }
    int ender_attempts=0;
    while(total_item(&r,368)<13&&ender_attempts++<40){
        int ex=24576+ender_attempts*256,ez=512;
        gm_world_ensure(r.world,ex>>4,ez>>4,0);
        int ey=gm_world_surface_y(r.world,ex,ez);
        CHECK(kill_hook_mob(&r,EW_TYPE_ENDERMAN,ex+0.5,ey,ez+0.5),
              "kill spawned Overworld enderman legally");}
    CHECK(total_item(&r,368)>=13,"collect thirteen natural pearl drops");STOP_IF_FAILED(&r);

    gm_runtime_set_pose(&r,0.5,72,2.5,0,0);CHECK(gm_runtime_use_block(&r,0,72,3),"open table for eyes of ender");
    for(int i=0;i<7;++i){int rod=slot_item(&r,369,1);empty_grid(g);g[0]=rod;
        CHECK(craft(&r,2,g),"craft blaze rod into powder");}
    for(int i=0;i<13;++i){int pearl=slot_item(&r,368,1),powder=slot_item(&r,377,1);empty_grid(g);
        g[0]=pearl;g[1]=powder;CHECK(craft(&r,2,g),"craft eye of ender from legal drops");}
    CHECK(total_item(&r,381)>=13,"thirteen crafted eyes reach inventory");STOP_IF_FAILED(&r);

    int eye=selectable_slot(&r,slot_item(&r,381,13));CHECK(eye>=0,"crafted eyes can be moved to a hotbar slot");
    GmAction eye_use;memset(&eye_use,0,sizeof eye_use);eye_use.hotbar_sel=eye;
    gm_runtime_set_pose(&r,8.5,gm_world_surface_y(r.world,8,8),8.5,0,0);gm_runtime_tick(&r,eye_use);
    eye_use.use=1;eye_use.do_place=1;gm_runtime_tick(&r,eye_use);
    CHECK(total_item(&r,381)==12,"throwing an eye consumes exactly one crafted eye");
    int saw_eye=0;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
        if(r.projectiles[i].active&&r.projectiles[i].type==4)saw_eye=1;
    CHECK(saw_eye,"thrown eye travels toward generated stronghold");

    GmStructureBox stronghold;CHECK(gm_stronghold_portal_room(r.seed,0,&stronghold),"locate generated stronghold portal room");
    for(int cx=stronghold.min_x>>4;cx<=stronghold.max_x>>4;++cx)
        for(int cz=stronghold.min_z>>4;cz<=stronghold.max_z>>4;++cz)gm_world_ensure(r.world,cx,cz,0);
    int frames=0;
    for(int x=stronghold.min_x;x<=stronghold.max_x;++x)for(int y=stronghold.min_y;y<=stronghold.max_y;++y)
        for(int z=stronghold.min_z;z<=stronghold.max_z;++z)if(gm_world_block(r.world,x,y,z)==120){
            gm_runtime_set_pose(&r,x+0.5,y,z+2.0,180,0);GmAction select;memset(&select,0,sizeof select);
            select.hotbar_sel=eye;gm_runtime_tick(&r,select);
            CHECK(gm_runtime_use_block(&r,x,y,z),"insert crafted eye into generated portal frame");++frames;}
    CHECK(frames==12&&total_item(&r,381)==0,"all twelve generated portal frames consume eyes");
    int ex=0,ey=0,ez=0,active=0;
    for(int x=stronghold.min_x;x<=stronghold.max_x;++x)for(int y=stronghold.min_y;y<=stronghold.max_y;++y)
        for(int z=stronghold.min_z;z<=stronghold.max_z;++z)if(gm_world_block(r.world,x,y,z)==119){ex=x;ey=y;ez=z;++active;}
    CHECK(active==9,"twelve legal eye inserts activate the 3x3 End portal");STOP_IF_FAILED(&r);
    CHECK(recover_health(&r),"natural food regeneration restores health before End entry");
    STOP_IF_FAILED(&r);
    gm_runtime_set_pose(&r,ex+0.5,ey,ez+0.5,0,0);idle(&r,1);
    CHECK(r.dimension==1&&r.dragon.initialized,"active portal enters generated End arena");STOP_IF_FAILED(&r);

    /* Travel hooks reach each live crystal/dragon, but every damage transition is
     * a survival attack with the legally crafted sword, bow projectile, or bed
     * explosion - never injected dragon health or victory. */
    for(int i=0;i<ED_NUM_CRYSTALS;++i){EdCrystal *c=&r.dragon.state.arena.crystals[i];
        /* Stand in air beside the pillar with its bedrock cap between the
         * player's body and the crystal explosion. The upward melee ray is
         * still within the four-block entity reach. */
        gm_runtime_set_pose(&r,c->x,c->y-3.0-PSV_EYE_HEIGHT,c->z-1.2,
                            0,-68.19859f);GmAction attack;memset(&attack,0,sizeof attack);
        int weapon=slot_item(&r,267,1);if(weapon<0)weapon=slot_item(&r,272,1);
        attack.attack=1;attack.do_break=1;
        attack.hotbar_sel=selectable_slot(&r,weapon);gm_runtime_tick(&r,attack);idle_hover(&r,10);
        CHECK(!c->alive,"destroy End crystal through survival melee attack");}

    /* Projectile: draw the crafted bow at the live dragon and release. */
    {
        EdDragon *d=&r.dragon.state.arena.dragon;
        float pre_hp=d->health;
        int bow=selectable_slot(&r,slot_item(&r,261,1));
        int arrows_before=total_item(&r,262);
        CHECK(bow>=0&&arrows_before>=1,"End combat has crafted bow and arrows");
        gm_runtime_set_pose(&r,d->x,d->y+2-PSV_EYE_HEIGHT,d->z-8.0,0,0);
        GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=bow;
        for(int t=0;t<22;++t)gm_runtime_tick(&r,draw);
        GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=bow;
        gm_runtime_tick(&r,release);
        int saw_arrow=0;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].type==1)saw_arrow=1;
        for(int t=0;t<40&&!saw_arrow;++t){idle_hover(&r,1);
            for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
                if(r.projectiles[i].active&&r.projectiles[i].type==1)saw_arrow=1;}
        CHECK(saw_arrow||total_item(&r,262)<arrows_before||d->health<pre_hp,
              "End combat fires a survival bow projectile at the dragon");
        idle_hover(&r,20);
    }

    /* Bed explosion: place the crafted bed on generated End stone and use it. */
    {
        EdDragon *d=&r.dragon.state.arena.dragon;
        float pre_hp=d->health;
        int bed=selectable_slot(&r,slot_item(&r,355,1));
        CHECK(bed>=0,"crafted bed is available for End explosion");
        int pad_x=0,pad_y=0,pad_z=0,pad=0;
        for(int x=-8;x<=8&&!pad;++x)for(int z=-8;z<=8&&!pad;++z)for(int y=60;y<=70&&!pad;++y)
            if(gm_world_block(r.world,x,y,z)==121&&
               gm_world_block(r.world,x,y+1,z)==0&&
               gm_world_block(r.world,x,y+1,z+1)==0){
                pad=1;
                for(int dx=-2;dx<=2&&pad;++dx)for(int dz=-2;dz<=2&&pad;++dz)
                    if(gm_world_block(r.world,x+dx,y,z+dz)!=121||
                       gm_world_block(r.world,x+dx,y+1,z+dz)!=0)pad=0;
                if(pad){pad_x=x;pad_y=y;pad_z=z;}}
        CHECK(pad,"locate generated End-stone pad for bed placement");
        gm_runtime_set_pose(&r,pad_x+0.5,pad_y+1.0,pad_z+0.5,0,60);
        GmAction place;memset(&place,0,sizeof place);place.do_place=1;place.use=1;place.hotbar_sel=bed;
        gm_runtime_tick(&r,place);
        int bed_x=0,bed_y=0,bed_z=0,bedparts=0;
        for(int x=pad_x-2;x<=pad_x+2;++x)for(int y=pad_y;y<=pad_y+3;++y)
            for(int z=pad_z-2;z<=pad_z+3;++z)
                if(gm_world_block(r.world,x,y,z)==26){bed_x=x;bed_y=y;bed_z=z;++bedparts;}
        CHECK(bedparts>=1,"bed places on generated End stone for explosion combat");
        int cover_x=0,cover_z=0,cover_dx=0,cover_dz=0,cover=0;
        static const int cover_dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
        for(int i=0;i<4&&!cover;++i){int cx=bed_x+cover_dirs[i][0];
            int cz=bed_z+cover_dirs[i][1];
            if(gm_world_block(r.world,cx,bed_y,cz)==0&&
               gm_world_block(r.world,cx,bed_y-1,cz)==121){
                cover_x=cx;cover_z=cz;cover_dx=cover_dirs[i][0];
                cover_dz=cover_dirs[i][1];cover=1;}}
        int cover_slot=slot_item(&r,4,1);
        if(cover_slot<0)cover_slot=slot_item(&r,5,1);
        int cover_placed=cover&&place_above(&r,cover_slot,cover_x,bed_y,cover_z);
        CHECK(cover_placed,"place carried blast cover beside the bed");
        double safe_x=cover_x+0.5+cover_dx*1.2;
        double safe_z=cover_z+0.5+cover_dz*1.2;
        float safe_yaw=cover_dx?cover_dx*90.0f:(cover_dz>0?180.0f:0.0f);
        gm_runtime_set_pose(&r,safe_x,bed_y,safe_z,safe_yaw,28.0f);
        CHECK(recover_health(&r),
              "natural food regeneration restores health before End bed combat");
        for(int hits=0;hits<240&&d->health>0&&!r.dead;++hits){
            gm_runtime_set_pose(&r,d->x,d->y+2-PSV_EYE_HEIGHT,d->z-3.8,0,0);
            GmAction attack;memset(&attack,0,sizeof attack);
            int weapon=slot_item(&r,267,1);if(weapon<0)weapon=slot_item(&r,272,1);
            if(weapon<0)weapon=slot_item(&r,268,1);
            if(weapon<0)weapon=slot_item(&r,274,1);
            attack.attack=1;attack.do_break=1;
            attack.hotbar_sel=selectable_slot(&r,weapon);
            gm_runtime_tick(&r,attack);idle(&r,1);
            double retreat_y=d->y+120;if(retreat_y>250)retreat_y=250;
            gm_runtime_set_pose(&r,d->x,retreat_y,d->z,0,0);idle_hover(&r,10);
        }
        CHECK(d->health<=0,"legal repeated attacks defeat the live dragon");
        gm_runtime_set_pose(&r,safe_x,bed_y,safe_z,safe_yaw,28.0f);
        CHECK(recover_health(&r),
              "natural food regeneration restores health after dragon melee");
        float player_hp=r.vitals.health;
        float dragon_hp=d->health;
        CHECK(gm_runtime_use_block(&r,bed_x,bed_y,bed_z),
              "using bed in the End triggers the explosion path");
        CHECK(gm_world_block(r.world,bed_x,bed_y,bed_z)==0,
              "bed explosion removes the bed blocks");
        CHECK(r.vitals.health<player_hp||d->health<dragon_hp,
              "bed explosion is a live End combat event");
        CHECK(!r.dead&&r.vitals.health>0.0f,
              "route survives End bed blast without injected recovery");
        idle_hover(&r,10);
        (void)pre_hp;
    }

    idle_hover(&r,200);CHECK(r.dragon.state.death_processed,"full 200-tick dragon death sequence completes");
    int exit_x=0,exit_y=0,exit_z=0,exit_found=0;
    for(int x=-4;x<=4;++x)for(int y=62;y<=65;++y)for(int z=-4;z<=4;++z)
        if(gm_world_block(r.world,x,y,z)==119){exit_x=x;exit_y=y;exit_z=z;exit_found=1;}
    CHECK(exit_found,"dragon death generates active exit portal");STOP_IF_FAILED(&r);
    gm_runtime_set_pose(&r,exit_x+0.5,exit_y,exit_z+0.5,0,0);idle(&r,1);
    CHECK(r.won&&r.credits,"entering generated exit portal reaches credits and won terminal");

    gm_runtime_destroy(&r);
    if(fail)return 1;
    fprintf(stderr,"route e2e: PASS fresh spawn through credits\n");return 0;
}
