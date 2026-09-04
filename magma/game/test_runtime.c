#include "game/runtime.h"
#include "game/portal_live.h"
#include "game/structures_live.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static int pose_hits(GmRuntime *r,int tx,int ty,int tz,double sx,double sy,double sz,float yaw,float pitch){
    int hx,hy,hz,ax,ay,az;
    gm_runtime_set_pose(r,sx,sy,sz,yaw,pitch);
    gm_world_fill_window(r->world,r->ccx,r->ccz,(struct Chunk *)r->window);
    int hit=psv_raycast(r->window,&r->sin_table,&r->player,&hx,&hy,&hz,&ax,&ay,&az);
    return hit>=0 && hx+r->ox==tx && hy==ty && hz+r->oz==tz;
}

int main(void) {
    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    GmRuntime r;
    char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;
    {
        GmRuntime other;
        GmConfig flat = cfg;
        flat.world = GM_WORLD_SUPERFLAT;
        gm_player_cursor_set(&r.ctl, ic_mk(1, 9, 0));
        gm_player_set_gui_blocked(&r.ctl, 1);
        int initialized = gm_runtime_init(&other, &flat, err, sizeof err);
        CHECK(initialized, "second runtime initializes independently");
        if (initialized) {
            CHECK(gm_player_cursor(&r.ctl).count == 9 &&
                  !gm_player_left_click_allows(&r.ctl, 1),
                  "second runtime initialization preserves first cursor and cooldown");
            CHECK(gm_player_cursor(&other.ctl).count == 0 &&
                  gm_player_left_click_allows(&other.ctl, 1),
                  "second runtime starts with its own clean controller");
            gm_runtime_destroy(&other);
            CHECK(gm_player_cursor(&r.ctl).count == 9,
                  "destroying second runtime preserves first controller");
        }
        gm_player_cursor_set(&r.ctl, ic_empty());
        gm_player_set_gui_blocked(&r.ctl, 0);
    }
    {
        GmPlayerCtlSnap ctl;memset(&ctl,0,sizeof ctl);ctl.hurt_vel_reset=1;
        gm_player_ctl_dig_import(&r.ctl, &ctl);
        gm_player_clear_inferred_hurt_velocity(&r.ctl);
        gm_player_ctl_dig_export(&r.ctl, &ctl);
        CHECK(!ctl.hurt_vel_reset,
              "recorded EntityTracker velocity clears inferred fall resend");
    }
    CHECK(isr_hotbar_total(&r.player.inv) + isr_main_total(&r.player.inv) == 0,
          "authoritative runtime starts with empty inventory");
    r.player_fire_ticks = 2;
    {
        GmPlayerView fv;gm_runtime_view(&r,&fv);
        CHECK(fv.fire==1&&fv.creative==0,
              "live Entity fire ticks expose first-person burning state");
    }
    r.player_fire_ticks = 0;
    CHECK(gm_runtime_tape_inventory(&r,0,17,2,0),"tape inventory accepts hotbar stack");
    CHECK(gm_runtime_tape_inventory(&r,40,442,1,0),"tape inventory accepts offhand stack");
    CHECK(gm_runtime_tape_inventory(&r,38,443,1,12),
          "tape inventory accepts elytra chest slot 38 with meta");
    CHECK(isr_get_stack(&r.tape_inv,38).meta==12,
          "tape chest elytra preserves durability meta");
    gm_runtime_set_elytra(&r, 1);
    CHECK(r.player.elytra_equipped == 1,
          "set_elytra arms EntityEquipmentSlot.CHEST == Items.ELYTRA for travel");
    gm_runtime_set_elytra(&r, 0);
    CHECK(r.player.elytra_equipped == 0, "set_elytra clears chest equipment flag");
    r.player.elytra_flying_pending = 1;
    gm_runtime_set_elytra_flag7(&r, 1);
    CHECK(r.player.elytra_flag7_recorded == 1 && r.player.elytra_flying == 1 &&
          r.player.elytra_flying_pending == 0,
          "recorded flag-7 event enables authoritative mode and clears prediction");
    gm_runtime_set_elytra_flag7(&r, 0);
    CHECK(r.player.elytra_flying == 0,
          "recorded flag-7 clear applies the metadata value");
    CHECK(gm_runtime_set_inventory(&r,38,443,1,0),"live set_inventory places elytra in chest");
    CHECK(r.player.elytra_equipped==1,"chest elytra arms flight eligibility");
    CHECK(gm_runtime_set_inventory(&r,38,0,0,0),"clear chest");
    /* empty chest leaves set_elytra hook; clear explicitly for the next checks */
    gm_runtime_set_elytra(&r, 0);
    gm_runtime_tape_player_view(&r,7,0.625f,123,0.5f,17,1234,1,1,1,0,9,
                                10,27.5f,0.4f);
    gm_runtime_tape_potions_clear(&r);
    CHECK(gm_runtime_tape_potion(&r,20,0,157,1),
          "tape potion accepts wither effect");
    CHECK(gm_runtime_tape_potion(&r,11,4,1000,0),
          "tape potion accepts a hidden-particle effect");
    {
        GmPlayerView tv;gm_runtime_view(&r,&tv);gm_runtime_apply_tape_view(&r,&tv);
        CHECK(tv.hotbar_ids[0]==17&&tv.hotbar_counts[0]==2,
              "post-tick tape inventory overrides render hotbar only");
        CHECK(isr_get_stack(&r.player.inv,0).item==0,
              "render inventory does not mutate current-tick simulation state");
        CHECK(tv.xp_level==7&&fabsf(tv.xp_frac-0.625f)<1e-6f&&tv.air==123,
              "recorded XP and air override the rendered player view");
        /* GuiIngame.renderPotionEffects gates the icon on doesShowParticles. */
        CHECK(tv.potion_count==2&&tv.potions[0].hide_particles==0&&
              tv.potions[1].hide_particles==1,
              "recorded showParticles flag reaches the HUD view");
        /* AttributeModifiers NBT can zero an armor item: the tape wins. */
        CHECK(tv.armor_points==0,"no armor override leaves the derived value");
        gm_runtime_tape_armor(&r,0);
        {
            GmPlayerView av;gm_runtime_view(&r,&av);gm_runtime_apply_tape_view(&r,&av);
            CHECK(av.armor_points==0,"recorded armor total 0 overrides the guess");
        }
        gm_runtime_tape_armor(&r,7);
        {
            GmPlayerView av;gm_runtime_view(&r,&av);gm_runtime_apply_tape_view(&r,&av);
            CHECK(av.armor_points==7,"recorded armor total overrides the guess");
        }
        gm_runtime_tape_armor(&r,-1);
        CHECK(tv.portal==0.5f&&tv.portal_frame==17&&tv.portal_phase==1234&&tv.loading==1&&
              tv.texture_animations_pinned==1,
              "recorded portal and loading state override the rendered player view");
        CHECK(tv.fire==1&&tv.creative==0&&tv.hurt_time==9&&
              tv.max_hurt_time==10&&fabsf(tv.hurt_yaw-27.5f)<1e-6f,
              "recorded fire and hurt state override the rendered player view");
        CHECK(fabsf(tv.attack_cooldown-0.4f)<1e-6f&&tv.potion_count==2&&
              tv.potions[0].id==20&&tv.potions[0].duration==157,
              "recorded cooldown and potion state override the rendered player view");
    }
    {
        ICStack got;
        CHECK(gm_runtime_tape_gui_slot(&r,GMC_GRID0,5,3,2),
              "tape GUI slot accepts exact stack");
        CHECK(gm_runtime_tape_gui_cursor(&r,17,2,1),
              "tape GUI cursor accepts exact stack");
        CHECK(gm_runtime_tape_furnace(&r,80,1600,100,200),
              "tape furnace progress accepts nonnegative fields");
        CHECK(gm_runtime_tape_gui_slot_get(&r,GMC_GRID0,&got)&&
              got.item==5&&got.count==3&&got.meta==2,
              "tape GUI slot round-trips");
        CHECK(gm_runtime_tape_gui_cursor_get(&r,&got)&&
              got.item==17&&got.count==2&&got.meta==1,
              "tape GUI cursor round-trips");
        {
            ICStack book = ic_mk(403, 1, 0);
            book.n_enchants = 2;
            book.enchants[0].id = 16; book.enchants[0].level = 3;
            book.enchants[1].id = 34; book.enchants[1].level = 1;
            CHECK(gm_runtime_tape_gui_slot_stack(&r, GMC_CHEST0, book),
                  "tape GUI slot accepts StoredEnchantments subset");
            CHECK(gm_runtime_tape_gui_cursor_stack(&r, book),
                  "tape GUI cursor accepts StoredEnchantments subset");
            CHECK(gm_runtime_tape_gui_slot_get(&r, GMC_CHEST0, &got) &&
                  got.item == 403 && got.n_enchants == 2 &&
                  got.enchants[0].id == 16 && got.enchants[0].level == 3 &&
                  got.enchants[1].id == 34 && got.enchants[1].level == 1,
                  "tape GUI slot retains multi-enchant payload");
            CHECK(gm_runtime_tape_gui_cursor_get(&r, &got) &&
                  got.n_enchants == 2 && got.enchants[1].id == 34,
                  "tape GUI cursor retains multi-enchant payload");
        }
        gm_runtime_gui_view_clear(&r);
        CHECK(!gm_runtime_tape_gui_slot_get(&r,GMC_GRID0,&got)&&
              !gm_runtime_tape_gui_cursor_get(&r,&got)&&!r.tape_furnace_active,
              "per-tick GUI render truth clears atomically");
    }
    {
        GmEntityView src;memset(&src,0,sizeof src);
        src.type=10;src.x=1;src.y=64;src.z=2;src.yaw=30;src.health=8;src.ent_id=91;
        src.tape_pose=1;src.head_yaw=55;src.pitch=12;src.hurt_time=4;
        src.death_time=2;src.flags=3;src.sheared=1;src.fleece_color=14;
        src.graze_y=0.75f;src.graze_x=1.1f;
        gm_runtime_ent_view(&r,&src);
        GmEntityView got[1];
        CHECK(gm_runtime_ghost_views(&r,got,1)==1&&got[0].head_yaw==55&&
              got[0].hurt_time==4&&got[0].sheared==1&&got[0].fleece_color==14,
              "oracle entity pose/state survives runtime without inference");
        gm_runtime_ent_views_clear(&r);
    }
    {
        GmAction use; memset(&use,0,sizeof use); use.use=1; use.hotbar_sel=-1;
        GmAction idle; memset(&idle,0,sizeof idle); idle.hotbar_sel=-1;
        GmAction sneak=idle; sneak.sneak=1;
        gm_runtime_set_pose(&r,0.5,3.0,0.5,0,20);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.56,3.5,0);
        gm_runtime_tick(&r,use);
        CHECK(r.tape_boat_ride_id<0&&r.tape_boat_mount_pending==10163,
              "tape boat click waits one client tick for mount response");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.55,3.5,0);
        gm_runtime_tick(&r,idle);
        CHECK(r.tape_boat_ride_id==10163&&
              fabs(r.player.ent.posX+r.ox-0.5)<1e-12&&
              fabs(r.player.ent.posY-
                   (3.55-0.44999998807907104))<1e-12&&
              fabs(r.player.ent.posZ+r.oz-3.5)<1e-12,
              "mounted tape player follows exact boat pose at vanilla offset");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.54,3.5,0);
        gm_runtime_tick(&r,sneak);
        CHECK(r.tape_boat_ride_id==10163&&r.tape_boat_dismount_pending,
              "tape boat sneak waits one client tick for dismount response");
        gm_runtime_ent_views_clear(&r);
        gm_runtime_tape_boat_view(&r,10163,0.5,3.53,3.5,0);
        gm_runtime_tick(&r,idle);
        CHECK(r.tape_boat_ride_id<0&&!r.tape_boat_dismount_pending,
              "tape boat dismount response clears passenger relationship");
    }
    {
        GmEntityView src; memset(&src,0,sizeof src);
        src.type=EW_TYPE_CREEPER;src.health=20;src.ent_id=9201;
        src.x=(float)(r.player.ent.posX+r.ox);
        src.y=(float)r.player.ent.posY;
        src.z=(float)(r.player.ent.posZ+r.oz)+2.5f;
        GmEntityView got[1]; int first=-1,last=-1;
        for(int t=0;t<12;++t){
            gm_runtime_ent_view(&r,&src);
            CHECK(gm_runtime_ghost_views(&r,got,1)==1,
                  "near tape creeper remains a render-only ghost");
            if(t==0)first=got[0].creeper_fuse;
            if(t==11)last=got[0].creeper_fuse;
            gm_runtime_ent_views_clear(&r);
        }
        CHECK(first==0&&last==11,
              "tape creeper fuse starts one frame after the proximity transition");
    }
    {
        GmEntityView fireball; memset(&fireball,0,sizeof fireball);
        fireball.type=GM_VIEW_DRAGON_FIREBALL;fireball.item_id=385;
        fireball.item_meta=2;fireball.ent_id=3578;
        fireball.x=(float)(r.player.ent.posX+r.ox)+1;
        fireball.y=(float)r.player.ent.posY+1;
        fireball.z=(float)(r.player.ent.posZ+r.oz);
        gm_runtime_ent_view(&r,&fireball);
        gm_runtime_ent_views_clear(&r);
        gm_runtime_ent_view(&r,&fireball);
        gm_runtime_ent_views_clear(&r);
        gm_runtime_ent_views_clear(&r);
        GmEntityView got[1];
        CHECK(gm_runtime_ghost_views(&r,got,1)==1&&
              got[0].type==GM_VIEW_EXPLOSION_LARGE&&got[0].ent_id==3578&&
              got[0].age==1,
              "nearby large-fireball removal latches its impact particle");
        gm_runtime_ent_views_clear(&r);
        CHECK(gm_runtime_ghost_views(&r,got,1)==0,
              "large-fireball impact latch is limited to its anchored puff");
    }

    /* Pin a naturally exposed iron vein for the next binary progression tape. */
    int iron_found=0, iron_x=0, iron_y=0, iron_z=0;
    double iron_sx=0,iron_sy=0,iron_sz=0; float iron_yaw=0,iron_pitch=0;
    static const int qdx[4]={0,0,-1,1}, qdz[4]={-1,1,0,0};
    static const float qyaw[4]={0,180,-90,90};
    for(int x=-32;x<=47 && !iron_found;++x) for(int z=-32;z<=47 && !iron_found;++z)
      for(int y=2;y<80 && !iron_found;++y) if(gm_world_block(r.world,x,y,z)==15)
        for(int d=0;d<4 && !iron_found;++d){
          int sx=x+qdx[d]*2,sz=z+qdz[d]*2;
          if(gm_world_block(r.world,x+qdx[d],y,z+qdz[d])!=0) continue;
          for(int fy=y-2;fy<=y+1 && !iron_found;++fy)
            if(gm_world_block(r.world,sx,fy-1,sz)!=0 &&
               gm_world_block(r.world,sx,fy,sz)==0 && gm_world_block(r.world,sx,fy+1,sz)==0){
              float p=(float)(atan2((fy+PSV_EYE_HEIGHT)-(y+0.5),2.0)*180.0/3.14159265358979323846);
              if(!pose_hits(&r,x,y,z,sx+0.5,fy,sz+0.5,qyaw[d],p))continue;
              iron_found=1;iron_x=x;iron_y=y;iron_z=z;
              iron_sx=sx+0.5;iron_sy=fy;iron_sz=sz+0.5;iron_yaw=qyaw[d];iron_pitch=p;
            }
        }
    CHECK(iron_found,"default streamed world contains naturally exposed iron ore");
    if(iron_found) fprintf(stderr,"runtime: exposed iron=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
        iron_x,iron_y,iron_z,iron_sx,iron_sy,iron_sz,iron_yaw,iron_pitch);
    int stone_found=0,stone_x=0,stone_y=0,stone_z=0; double stone_sx=0,stone_sy=0,stone_sz=0;
    float stone_yaw=0,stone_pitch=0;
    for(int x=-32;x<=47 && !stone_found;++x) for(int z=-32;z<=47 && !stone_found;++z)
      for(int y=2;y<80 && !stone_found;++y) if(gm_world_block(r.world,x,y,z)==1)
        for(int d=0;d<4 && !stone_found;++d){int sx=x+qdx[d]*2,sz=z+qdz[d]*2;
          if(gm_world_block(r.world,x+qdx[d],y,z+qdz[d])!=0)continue;
          for(int fy=y-2;fy<=y+1 && !stone_found;++fy)
            if(gm_world_block(r.world,sx,fy-1,sz)!=0&&gm_world_block(r.world,sx,fy,sz)==0&&gm_world_block(r.world,sx,fy+1,sz)==0){
              stone_found=1;stone_x=x;stone_y=y;stone_z=z;stone_sx=sx+0.5;stone_sy=fy;stone_sz=sz+0.5;stone_yaw=qyaw[d];
              stone_pitch=(float)(atan2((fy+PSV_EYE_HEIGHT)-(y+0.5),2.0)*180.0/3.14159265358979323846);}}
    CHECK(stone_found,"default streamed world contains naturally exposed stone");
    if(stone_found)fprintf(stderr,"runtime: exposed stone=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
      stone_x,stone_y,stone_z,stone_sx,stone_sy,stone_sz,stone_yaw,stone_pitch);

    int lx = 0, ly = 0, lz = 0, found = 0;
    double stand_x = 0.0, stand_y = 0.0, stand_z = 0.0;
    float stand_yaw = 0.0f, stand_pitch = 0.0f;
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    static const float yaw[4] = {0.0f, 180.0f, -90.0f, 90.0f};
    for (int x = 0; x < 32 && !found; ++x)
        for (int z = 0; z < 32 && !found; ++z)
            for (int y = 1; y < 128; ++y)
                if (gm_world_block(r.world, x, y, z) == 17) {
                    for (int d = 0; d < 4 && !found; ++d) {
                        int sx = x + dx[d] * 2, sz = z + dz[d] * 2;
                        if (gm_world_block(r.world, x + dx[d], y, z + dz[d]) != 0)
                            continue;
                        for (int fy = y - 2; fy <= y + 1 && !found; ++fy) {
                            if (fy < 1 || gm_world_block(r.world, sx, fy - 1, sz) == 0 ||
                                gm_world_block(r.world, sx, fy, sz) != 0 ||
                                gm_world_block(r.world, sx, fy + 1, sz) != 0) continue;
                            lx = x; ly = y; lz = z;
                            stand_x = sx + 0.5; stand_y = fy; stand_z = sz + 0.5;
                            stand_yaw = yaw[d];
                            stand_pitch = (float)(atan2((fy + PSV_EYE_HEIGHT) - (y + 0.5), 2.0)
                                                * 180.0 / 3.14159265358979323846);
                            found = 1;
                        }
                    }
                    if (found) break;
                }
    CHECK(found, "default streamed world contains a generated oak log");
    if (found) {
        fprintf(stderr, "runtime: generated log=(%d,%d,%d) stand=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.2f\n",
                lx, ly, lz, stand_x, stand_y, stand_z, stand_yaw, stand_pitch);
        /* Stand south of the generated log and look at its center. Travel is a
         * legal test hook; the log and all progression remain natural. */
        gm_runtime_set_pose(&r, stand_x, stand_y, stand_z, stand_yaw, stand_pitch);
        GmAction a; memset(&a, 0, sizeof a); a.attack = 1; a.hotbar_sel = -1;
        for (int t = 0; t < 200 && gm_world_block(r.world, lx, ly, lz) == 17; ++t)
            gm_runtime_tick(&r, a);
        CHECK(gm_world_block(r.world, lx, ly, lz) == 0,
              "shared runtime breaks the generated log");
        CHECK(isr_hotbar_total(&r.player.inv) + isr_main_total(&r.player.inv) == 0,
              "generated log drop is not injected into inventory");
        CHECK(r.entities.n_active == 1 && r.entities.ents[0].item == 17,
              "generated log creates a live log item entity");
        if (r.entities.n_active == 1) {
            GmLiveEnt *e = &r.entities.ents[0];
            gm_runtime_set_pose(&r, e->x, e->y, e->z, 0.0f, 0.0f);
            memset(&a, 0, sizeof a); a.hotbar_sel = -1;
            for (int t = 0; t < 20 && r.entities.n_active; ++t) gm_runtime_tick(&r, a);
            ICStack got = isr_get_stack(&r.player.inv, 0);
            CHECK(got.item == 17 && got.count == 1,
                  "player collects the natural generated-log entity");
            int grid[9]; for (int i = 0; i < 9; ++i) grid[i] = -1;
            grid[0] = 0;
            CHECK(gm_runtime_craft(&r, 2, grid), "player 2x2 crafts collected log");
            got = isr_get_stack(&r.player.inv, 0);
            CHECK(got.item == 5 && got.count == 4, "log becomes four oak planks");
            CHECK(!gm_runtime_craft(&r, 3, grid), "3x3 crafting rejected without table container");

            grid[0]=grid[1]=grid[3]=grid[4]=0;
            CHECK(gm_runtime_craft(&r,2,grid), "2x2 crafts a crafting table");
            got=isr_get_stack(&r.player.inv,0);
            CHECK(got.item==58 && got.count==1, "crafting table output retained");
            (void)isr_decr_stack_size(&r.player.inv,0,1);
            gm_world_set_block_meta(r.world,1,72,2,58,0);
            gm_runtime_set_pose(&r,0.5,72,2.5,0,0);
            CHECK(gm_runtime_use_block(&r,1,72,2), "reachable crafting table opens 3x3 container");
            isr_set_stack(&r.player.inv,1,ic_mk(5,3,0));
            isr_set_stack(&r.player.inv,2,ic_mk(280,2,0));
            for (int i=0;i<9;++i) grid[i]=-1;
            grid[0]=grid[1]=grid[2]=1; grid[4]=grid[7]=2;
            CHECK(gm_runtime_craft(&r,3,grid), "table container crafts wooden pickaxe");
            int have_pick=0;
            for (int i=0;i<ISR_MAIN_SLOTS;++i) if (isr_get_stack(&r.player.inv,i).item==270) have_pick=1;
            CHECK(have_pick, "wooden pickaxe output retained");

            gm_world_set_block_meta(r.world, 1,72,2,61,3);
            gm_runtime_set_pose(&r,0.5,72,2.5,0,0);
            CHECK(gm_runtime_use_block(&r,1,72,2), "reachable furnace opens live container");
            isr_set_stack(&r.player.inv,10,ic_mk(15,1,0));
            isr_set_stack(&r.player.inv,11,ic_mk(263,1,0));
            CHECK(gm_runtime_furnace_insert(&r,0,10,1)==1, "insert iron ore from inventory");
            CHECK(gm_runtime_furnace_insert(&r,1,11,1)==1, "insert coal from inventory");
            memset(&a,0,sizeof a); a.hotbar_sel=-1;
            for (int t=0;t<200;++t) gm_runtime_tick(&r,a);
            CHECK(r.furnaces[r.active_furnace].state.output.item==265 &&
                  r.furnaces[r.active_furnace].state.output.count==1,
                  "authoritative world ticks smelt iron ingot");
            CHECK(gm_runtime_furnace_extract(&r,2,1)==1,
                  "extract furnace result to inventory");
            int have_ingot=0;
            for (int i=0;i<ISR_MAIN_SLOTS;++i) {
                got=isr_get_stack(&r.player.inv,i);
                if (got.item==265 && got.count==1) have_ingot=1;
            }
            CHECK(have_ingot, "extracted iron ingot retained");
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"route recipe runtime initializes");
    if(r.world){
        gm_world_set_block(r.world,9,4,8,58);gm_runtime_set_pose(&r,8.5,4,8.5,-90,0);
        CHECK(gm_runtime_use_block(&r,9,4,8),"route recipe table opens");
        int grid[9];for(int i=0;i<9;++i)grid[i]=-1;
        isr_set_stack(&r.player.inv,0,ic_mk(265,3,0));
        grid[0]=grid[2]=grid[4]=0;
        CHECK(gm_runtime_craft(&r,3,grid),"three iron ingots craft an empty bucket");
        isr_set_stack(&r.player.inv,1,ic_mk(35,3,0));
        isr_set_stack(&r.player.inv,2,ic_mk(5,3,0));
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=grid[1]=grid[2]=1;grid[3]=grid[4]=grid[5]=2;
        CHECK(gm_runtime_craft(&r,3,grid),"wool and planks craft a bed");
        isr_set_stack(&r.player.inv,3,ic_mk(369,1,0));
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=3;
        CHECK(gm_runtime_craft(&r,2,grid),"blaze rod crafts two blaze powder");
        isr_set_stack(&r.player.inv,4,ic_mk(368,1,0));
        int powder=-1;for(int i=0;i<ISR_MAIN_SLOTS;++i)
            if(isr_get_stack(&r.player.inv,i).item==377)powder=i;
        for(int i=0;i<9;++i)grid[i]=-1;
        grid[0]=4;grid[1]=powder;
        CHECK(powder>=0&&gm_runtime_craft(&r,2,grid),"pearl and blaze powder craft an eye of ender");
        int bucket=0,bed=0,eye=0;
        for(int i=0;i<ISR_MAIN_SLOTS;++i){ICStack s=isr_get_stack(&r.player.inv,i);
            bucket+=s.item==325?s.count:0;bed+=s.item==355?s.count:0;eye+=s.item==381?s.count:0;}
        CHECK(bucket==1&&bed==1&&eye==1,"all route recipe outputs enter survival inventory");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"eye throw runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(381,2,0));
        gm_runtime_set_pose(&r,8.5,72.0,8.5,0,0);
        GmAction use;memset(&use,0,sizeof use);use.do_place=1;use.use=1;use.hotbar_sel=0;
        gm_runtime_tick(&r,use);
        int eyes=0;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].type==4)eyes++;
        CHECK(eyes==1&&isr_get_stack(&r.player.inv,0).count==1,
              "survival use throws and consumes one eye of ender");
        int sx,sz;CHECK(gm_stronghold_locate(r.seed,0,&sx,&sz),"eye target stronghold locates");
        GmRuntimeProjectile *eye=NULL;for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r.projectiles[i].active&&r.projectiles[i].type==4)eye=&r.projectiles[i];
        CHECK(eye&&((eye->vx>0)==(sx>8))&&((eye->vz>0)==(sz>8)),
              "eye flies toward the generated seed stronghold");
    }
    gm_runtime_destroy(&r);

    cfg.world=GM_WORLD_SUPERFLAT;
    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"portal runtime initializes");
    if(r.world){
        for(int x=6;x<10;++x){gm_world_set_block(r.world,x,4,8,49);gm_world_set_block(r.world,x,8,8,49);}
        for(int y=5;y<8;++y){gm_world_set_block(r.world,6,y,8,49);gm_world_set_block(r.world,9,y,8,49);}
        gm_world_set_block(r.world,7,5,8,51);
        CHECK(gm_portal_ignite(r.world,7,5,8)==6,"verified frame detection lights source portal");
        gm_runtime_set_pose(&r,7.5,5.0,8.5,0,0);
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        for(int t=0;t<82&&r.dimension==0;++t)gm_runtime_tick(&r,idle);
        CHECK(r.dimension==-1,"standing in portal performs client-visible Nether transition");
        CHECK(r.world==r.worlds[0]&&r.world!=r.worlds[1],"runtime swaps to persistent Nether world");
        GmPlayerView nv;gm_runtime_view(&r,&nv);
        CHECK(gm_world_block(r.world,(int)floor(nv.x),(int)floor(nv.y),(int)floor(nv.z))==90,
              "destination portal is linked at scaled Nether coordinate");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"End transition runtime initializes");
    if(r.world){
        gm_world_set_block(r.world,8,4,8,49);gm_world_set_block(r.world,8,5,8,119);
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,0);
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        gm_runtime_tick(&r,idle);
        CHECK(r.dimension==1&&r.world==r.worlds[2],"End portal block transitions into persistent End world");
        CHECK(gm_world_block(r.world,100,48,0)==49,"End arrival platform is generated");
        r.dragon.state.arena.dragon.health=0.0f; /* component hook: death sequence is under test */
        for(int t=0;t<200;++t)gm_runtime_tick(&r,idle);
        CHECK(r.dragon.state.death_processed&&r.dragon.state.arena.dragon.death_ticks==200,
              "dragon runs the full 200-tick death animation");
        CHECK(gm_world_block(r.world,1,63,0)==119,"dragon death creates active exit podium");
        int dragon_xp=0,dragon_orbs=0;
        for(int i=0;i<GM_XP_ORBS;++i)if(!r.mobs.xp_orbs[i].dead&&r.mobs.xp_orbs[i].xpValue>0){
            dragon_xp+=r.mobs.xp_orbs[i].xpValue;++dragon_orbs;
        }
        CHECK(dragon_orbs>1&&dragon_xp==12000,
              "dragon death splits the exact completion XP budget into live orbs");
        gm_runtime_set_pose(&r,1.5,63.0,0.5,0,0);gm_runtime_tick(&r,idle);
        CHECK(r.won&&r.credits,"entering generated exit portal reaches credits and won terminal");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bucket runtime initializes");
    if(r.world){
        gm_world_set_block_meta(r.world,8,5,10,9,0);
        isr_set_stack(&r.player.inv,0,ic_mk(325,1,0));
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,29.25f);
        GmAction use;memset(&use,0,sizeof use);use.do_place=1;use.hotbar_sel=0;
        gm_runtime_tick(&r,use);
        CHECK(isr_get_stack(&r.player.inv,0).item==326&&gm_world_block(r.world,8,5,10)==0,
              "empty bucket collects a looked-at water source");
        gm_world_set_block_meta(r.world,9,4,10,10,0);
        /* ItemBucket.onItemRightClick uses Item.rayTrace(..., false) for a
         * filled bucket, so liquids are ignored and the terrain hit face
         * chooses the placement cell. Aim at the ground's top face: 46.7
         * degrees hits the next ground block's side and vanilla refuses the
         * resulting solid placement target in tryPlaceContainedLiquid. */
        gm_runtime_set_pose(&r,8.5,5.0,8.5,0,50.0f);gm_runtime_tick(&r,use);
        CHECK(isr_get_stack(&r.player.inv,0).item==325,"water bucket returns empty bucket after placement");
        CHECK(gm_world_block(r.world,9,4,10)==49,"water-lava source reaction creates obsidian");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"hostile behavior runtime initializes");
    if(r.world){
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0,0);
        CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_SKELETON,8.5,4.0,16.5)>0,
              "skeleton component target spawns");
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
        float hp=r.vitals.health;int saw_hostile_arrow=0;
        for(int t=0;t<30&&r.vitals.health==hp;++t){
            gm_runtime_tick(&r,idle);
            for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
                if(r.projectiles[i].active&&r.projectiles[i].type==2)saw_hostile_arrow=1;
        }
        CHECK(saw_hostile_arrow,"skeleton creates a visible ballistic projectile");
        CHECK(r.vitals.health<hp,"skeleton projectile damages the player");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"creeper runtime initializes");
    if(r.world){
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0,0);
        CHECK(gm_mobs_spawn(&r.mobs,EW_TYPE_CREEPER,8.5,4.0,10.8)>0,
              "creeper component target spawns");
        GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;float hp=r.vitals.health;
        for(int t=0;t<30;++t)gm_runtime_tick(&r,idle);
        CHECK(gm_mobs_alive(&r.mobs)==0,"creeper is consumed after its 30-tick fuse");
        CHECK(r.vitals.health<hp,"creeper explosion applies verified explosion damage");
        CHECK(r.parity_ex_blasts>=1u,"creeper explosion records a blast");
        CHECK(r.player.ent.motionX!=0.0||r.player.ent.motionY!=0.0||
              r.player.ent.motionZ!=0.0||
              r.parity_ex_kb_x!=0.0||r.parity_ex_kb_y!=0.0||r.parity_ex_kb_z!=0.0,
              "creeper explosion applies doExplosionA knockback");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bow runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(261,1,0));
        isr_set_stack(&r.player.inv,1,ic_mk(262,2,0));
        CHECK(gm_mobs_spawn(&r.mobs,GM_MOB_SHEEP,8.5,4.0,14.5)>0,
              "stationary projectile target spawns");
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0.0f,6.8f);
        GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
        for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
        GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
        gm_runtime_tick(&r,release);
        CHECK(isr_get_stack(&r.player.inv,1).count==1,
              "bow release consumes exactly one survival arrow");
        int visible=0;
        for(int t=0;t<8;++t){
            GmEntityView arrows[GM_RUNTIME_PROJECTILES];
            if(gm_runtime_projectile_views(&r,arrows,GM_RUNTIME_PROJECTILES)>0)visible=1;
            gm_runtime_tick(&r,release);
        }
        GmEntityView target[EW_MAX_ENTITIES];
        int nt=gm_mobs_fill_views(&r.mobs,target,EW_MAX_ENTITIES);
        CHECK(visible,"flying arrow is exposed as a runtime entity view");
        CHECK(nt==1&&target[0].health<20.0f,
              "swept arrow flight damages an entity without endpoint tunneling");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bow findAmmo runtime initializes");
    if(r.world){
        /* ItemBow.findAmmo: off-hand before main inventory scan. */
        isr_set_stack(&r.player.inv,0,ic_mk(261,1,0));
        isr_set_stack(&r.player.inv,1,ic_mk(262,8,0));
        isr_set_stack(&r.player.inv,ISR_OFFHAND_SLOT,ic_mk(262,3,0));
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0.0f,6.8f);
        { GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
          for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
          GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
          gm_runtime_tick(&r,release); }
        CHECK(isr_get_stack(&r.player.inv,ISR_OFFHAND_SLOT).count==2,
              "findAmmo consumes off-hand arrows first");
        CHECK(isr_get_stack(&r.player.inv,1).count==8,
              "findAmmo leaves hotbar arrows when off-hand has ammo");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"creative bow runtime initializes");
    if(r.world){
        r.tape_creative=1;
        isr_set_stack(&r.player.inv,0,ic_mk(261,1,0));
        isr_set_stack(&r.player.inv,1,ic_mk(262,4,0));
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0.0f,6.8f);
        { GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
          for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
          GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
          gm_runtime_tick(&r,release); }
        CHECK(isr_get_stack(&r.player.inv,1).count==4,
              "creative onPlayerStoppedUsing does not shrink arrows");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"infinity bow runtime initializes");
    if(r.world){
        ICStack bow=ic_mk(261,1,0);
        bow.n_enchants=1; bow.enchants[0].id=51; bow.enchants[0].level=1;
        isr_set_stack(&r.player.inv,0,bow);
        isr_set_stack(&r.player.inv,1,ic_mk(262,4,0));
        gm_runtime_set_pose(&r,8.5,4.0,8.5,0.0f,6.8f);
        { GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
          for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
          GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
          gm_runtime_tick(&r,release); }
        CHECK(isr_get_stack(&r.player.inv,1).count==4,
              "Infinity skips ItemArrow shrink");
        isr_set_stack(&r.player.inv,1,ic_mk(440,4,0));
        { GmAction draw;memset(&draw,0,sizeof draw);draw.use=1;draw.hotbar_sel=0;
          for(int t=0;t<20;++t)gm_runtime_tick(&r,draw);
          GmAction release;memset(&release,0,sizeof release);release.hotbar_sel=0;
          gm_runtime_tick(&r,release); }
        CHECK(isr_get_stack(&r.player.inv,1).count==3,
              "Infinity still consumes tipped arrows (not ItemArrow class)");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"arrow pickup runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,8,ic_mk(262,10,0));
        r.player.inv.current_item=0;
        r.projectiles[0].active=1;
        r.projectiles[0].type=1;
        r.projectiles[0].x=r.player.ent.posX+(double)r.ox;
        r.projectiles[0].y=r.player.ent.posY+0.5;
        r.projectiles[0].z=r.player.ent.posZ+(double)r.oz;
        r.proj_in_ground[0]=1;
        r.proj_shake[0]=2;
        r.proj_pickup[0]=1;
        { GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
          gm_runtime_tick(&r,idle);
          CHECK(r.projectiles[0].active==1 && isr_get_stack(&r.player.inv,8).count==10,
                "arrowShake>0 blocks onCollideWithPlayer pickup");
          gm_runtime_tick(&r,idle);
          CHECK(!r.projectiles[0].active && isr_get_stack(&r.player.inv,8).count==11,
                "ALLOWED inGround arrow merges into the existing stack"); }
        r.projectiles[1].active=1;
        r.projectiles[1].type=1;
        r.projectiles[1].x=r.player.ent.posX+(double)r.ox;
        r.projectiles[1].y=r.player.ent.posY+0.5;
        r.projectiles[1].z=r.player.ent.posZ+(double)r.oz;
        r.proj_in_ground[1]=1;
        r.proj_shake[1]=0;
        r.proj_pickup[1]=2;
        { GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
          gm_runtime_tick(&r,idle);
          CHECK(r.projectiles[1].active==1 && isr_get_stack(&r.player.inv,8).count==11,
                "CREATIVE_ONLY pickupStatus stays in the world in survival"); }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"food runtime initializes");
    if(r.world){
        r.vitals.foodLevel=10;r.vitals.saturation=0;r.player.food=10;
        isr_set_stack(&r.player.inv,0,ic_mk(364,1,0));
        GmAction eat;memset(&eat,0,sizeof eat);eat.use=1;eat.hotbar_sel=0;
        for(int t=0;t<32;++t)gm_runtime_tick(&r,eat);
        CHECK(r.vitals.foodLevel==18&&isr_get_stack(&r.player.inv,0).count==0,
              "holding use for 32 ticks consumes cooked food and restores hunger");
    }
    gm_runtime_destroy(&r);

    {
        float regen_off_health=0.0f,regen_off_saturation=0.0f;
        cfg.mobs=0;
        CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
              "gamerule-off runtime initializes");
        if(r.world){
            McGameRules gr=mc_gamerules_default();
            gr.naturalRegeneration=0;
            gr.doDaylightCycle=0;
            gr.doWeatherCycle=0;
            gm_runtime_set_time(&r,6000);
            gm_runtime_set_total_time(&r,1000);
            gm_runtime_set_weather(&r,1,1,100,200);
            gm_runtime_set_gamerules(&r,&gr);
            gm_runtime_set_vitals(&r,10.0f,20);
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int t=0;t<11;++t)gm_runtime_tick(&r,idle);
            regen_off_health=r.vitals.health;
            regen_off_saturation=r.vitals.saturation;
            CHECK(fabsf(regen_off_health-10.0f)<1e-6f&&
                  fabsf(regen_off_saturation-5.0f)<1e-6f,
                  "naturalRegeneration false suppresses saturated healing");
            CHECK(r.clock.world_time==6000&&r.clock.total_time==1011,
                  "doDaylightCycle false freezes world time only");
            CHECK(r.clock.rain_time==100&&r.clock.thunder_time==200&&
                  r.clock.raining&&r.clock.thundering,
                  "doWeatherCycle false freezes weather state and timers");
        }
        gm_runtime_destroy(&r);

        CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
              "gamerule-on runtime initializes");
        if(r.world){
            McGameRules gr=mc_gamerules_default();
            gm_runtime_set_time(&r,6000);
            gm_runtime_set_total_time(&r,1000);
            gm_runtime_set_weather(&r,1,1,100,200);
            gm_runtime_set_gamerules(&r,&gr);
            gm_runtime_set_vitals(&r,10.0f,20);
            GmAction idle;memset(&idle,0,sizeof idle);idle.hotbar_sel=-1;
            for(int t=0;t<11;++t)gm_runtime_tick(&r,idle);
            CHECK(r.vitals.health>regen_off_health+0.1f&&
                  r.vitals.saturation<regen_off_saturation-0.1f,
                  "naturalRegeneration true heals and consumes saturation");
            CHECK(r.clock.world_time==6011&&r.clock.total_time==1011,
                  "doDaylightCycle true advances world time");
            CHECK(r.clock.rain_time!=100||r.clock.thunder_time!=200,
                  "doWeatherCycle true advances weather timers");
        }
        gm_runtime_destroy(&r);
        cfg.mobs=1;
    }

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),
          "rain-thunder runtime initializes");
    if(r.world){
        CHECK(r.rain_strength==0.f&&r.thunder_strength==0.f,
              "live rain/thunder start at 0");
        gm_runtime_set_rain_thunder(&r,1.f,1.f);
        CHECK(r.rain_strength==1.f&&r.thunder_strength==1.f,
              "set_rain_thunder stores tape strengths");
        gm_runtime_set_rain_thunder(&r,-1.f,2.f);
        CHECK(r.rain_strength==0.f&&r.thunder_strength==1.f,
              "set_rain_thunder clamps to [0,1]");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r,&cfg,err,sizeof err),"bed runtime initializes");
    if(r.world){
        isr_set_stack(&r.player.inv,0,ic_mk(355,1,0));gm_runtime_set_pose(&r,8.5,5,8.5,0,60);
        GmAction place;memset(&place,0,sizeof place);place.do_place=1;place.hotbar_sel=0;
        gm_runtime_tick(&r,place);int bx=0,by=0,bz=0,bedparts=0;
        for(int x=6;x<=10;++x)for(int y=4;y<=6;++y)for(int z=7;z<=12;++z)
            if(gm_world_block(r.world,x,y,z)==26){bx=x;by=y;bz=z;++bedparts;}
        CHECK(bedparts==2,"bed item places linked foot and head blocks");
        r.dimension=-1;float hp=r.vitals.health;
        CHECK(gm_runtime_use_block(&r,bx,by,bz),"using bed outside Overworld triggers explosion");
        CHECK(gm_world_block(r.world,bx,by,bz)==0&&r.vitals.health<hp,
              "bed explosion removes bed and applies verified explosion damage");
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "spawner TE runtime initializes");
    if (r.world) {
        CHECK(gm_runtime_load_block(&r, 8, 5, 12, 52, 0),
              "load mob-spawner block 52");
        CHECK(gm_runtime_set_tile_entity(&r, 0, 8, 5, 12, 7, 0.0f),
              "stores a blaze spawner TE");
        {
            GmRuntimeSpawnerView v[8];
            int n = gm_runtime_spawner_views(&r, v, 8);
            CHECK(n == 1 && v[0].entity_type == 7 && v[0].wx == 8 &&
                  v[0].wy == 5 && v[0].wz == 12,
                  "spawner view is the stored blaze, not a heuristic");
        }
        CHECK(gm_runtime_set_tile_entity(&r, 0, 9, 5, 12, -1, 0.0f),
              "unknown spawn id is stored as no cached entity");
        CHECK(gm_runtime_load_block(&r, 9, 5, 12, 52, 0),
              "second spawner cell");
        {
            GmRuntimeSpawnerView v[8];
            int n = gm_runtime_spawner_views(&r, v, 8);
            int saw_none = 0;
            for (int i = 0; i < n; ++i)
                if (v[i].wx == 9 && v[i].entity_type == -1) saw_none = 1;
            CHECK(n == 2 && saw_none,
                  "null cached entity is visible to the renderer as type -1");
        }
        CHECK(gm_runtime_set_tile_entity(&r, -1, 8, 5, 12, 7, 0.0f),
              "nether-dimension TE stores");
        {
            GmRuntimeSpawnerView v[8];
            int n = gm_runtime_spawner_views(&r, v, 8);
            CHECK(n == 2, "overworld views ignore a nether-dimension TE");
        }
        CHECK(gm_runtime_load_block(&r, 8, 5, 12, 0, 0),
              "remove first spawner block");
        {
            GmRuntimeSpawnerView v[8];
            int n = gm_runtime_spawner_views(&r, v, 8);
            CHECK(n == 1 && v[0].wx == 9,
                  "TESR skips a TE whose cell is no longer block 52");
        }
    }
    gm_runtime_destroy(&r);
    if (fail) return 1;
    fprintf(stderr, "runtime: PASS\n");
    return 0;
}
