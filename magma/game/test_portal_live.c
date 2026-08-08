#include "game/portal_live.h"

#include <math.h>
#include <stdio.h>

int main(void){
    if(gm_portal_transfer_coordinate(248.5,0.125)!=31.0
            ||gm_portal_transfer_coordinate(-248.5,0.125)!=-31.0
            ||gm_portal_transfer_coordinate(400000000.5,0.125)!=29999872.0
            ||gm_portal_transfer_coordinate(-400000000.5,0.125)!=-29999872.0){
        fprintf(stderr,"portal_live: transfer truncation/border clamp mismatch\n");
        return 1;
    }
    GmWorld *w=gm_world_create_type(0,1);if(!w)return 1;
    gm_world_ensure(w,0,0,2);
    for(int x=0;x<4;++x){gm_world_set_block(w,x,70,0,49);gm_world_set_block(w,x,74,0,49);}
    for(int y=71;y<74;++y){gm_world_set_block(w,0,y,0,49);gm_world_set_block(w,3,y,0,49);}
    gm_world_set_block(w,1,71,0,51);
    int placed=gm_portal_ignite(w,1,71,0),count=0;
    for(int x=1;x<=2;++x)for(int y=71;y<=73;++y)
        if(gm_world_block(w,x,y,0)==90&&gm_world_meta(w,x,y,0)==1)++count;
    if(placed!=6||count!=6){fprintf(stderr,"portal_live: placed=%d count=%d\n",placed,count);return 1;}
    if(!gm_portal_block_valid(w,1,71,0,1)
            ||!gm_portal_block_valid(w,2,73,0,1)){
        fprintf(stderr,"portal_live: valid x frame rejected\n");return 1;
    }
    /* Vanilla permits all four decorative frame corners to be absent. This
     * is the ten-obsidian speedrun frame, ignited from its middle row. */
    for(int x=21;x<=22;++x){
        gm_world_set_block(w,x,70,4,49);
        gm_world_set_block(w,x,74,4,49);
    }
    for(int y=71;y<=73;++y){
        gm_world_set_block(w,20,y,4,49);
        gm_world_set_block(w,23,y,4,49);
    }
    gm_world_set_block(w,21,72,4,51);
    placed=gm_portal_ignite(w,21,72,4);count=0;
    for(int x=21;x<=22;++x)for(int y=71;y<=73;++y)
        count+=gm_world_block(w,x,y,4)==90;
    if(placed!=6||count!=6){
        fprintf(stderr,"portal_live: cornerless placed=%d count=%d\n",
                placed,count);return 1;
    }
    gm_world_set_block(w,0,72,0,0);
    if(gm_portal_block_valid(w,1,71,0,1)){
        fprintf(stderr,"portal_live: broken x frame accepted\n");return 1;
    }
    for(int z=0;z<4;++z){gm_world_set_block(w,10,70,z,49);gm_world_set_block(w,10,74,z,49);}
    for(int y=71;y<74;++y){gm_world_set_block(w,10,y,0,49);gm_world_set_block(w,10,y,3,49);}
    gm_world_set_block(w,10,71,1,51);
    placed=gm_portal_ignite(w,10,71,1);count=0;
    for(int z=1;z<=2;++z)for(int y=71;y<=73;++y)
        if(gm_world_block(w,10,y,z)==90&&gm_world_meta(w,10,y,z)==2)++count;
    if(placed!=6||count!=6){fprintf(stderr,"portal_live: z placed=%d count=%d\n",placed,count);return 1;}
    if(!gm_portal_block_valid(w,10,71,1,2)
            ||!gm_portal_block_valid(w,10,73,2,2)){
        fprintf(stderr,"portal_live: valid z frame rejected\n");return 1;
    }
    gm_world_set_block(w,10,72,0,0);
    if(gm_portal_block_valid(w,10,71,1,2)){
        fprintf(stderr,"portal_live: broken z frame accepted\n");return 1;
    }
    for(int x=7;x<=29;++x){gm_world_set_block(w,x,70,10,49);gm_world_set_block(w,x,92,10,49);}
    for(int y=71;y<=91;++y){gm_world_set_block(w,7,y,10,49);gm_world_set_block(w,29,y,10,49);}
    gm_world_set_block(w,28,91,10,51);
    placed=gm_portal_ignite(w,28,91,10);count=0;
    for(int x=8;x<=28;++x)for(int y=71;y<=91;++y)
        if(gm_world_block(w,x,y,10)==90&&gm_world_meta(w,x,y,10)==1)++count;
    if(placed!=441||count!=441){fprintf(stderr,"portal_live: max x placed=%d count=%d\n",placed,count);return 1;}
    if(!gm_portal_block_valid(w,8,71,10,1)
            ||!gm_portal_block_valid(w,28,91,10,1)){
        fprintf(stderr,"portal_live: valid max x frame rejected\n");return 1;
    }
    for(int z=7;z<=29;++z){gm_world_set_block(w,35,70,z,49);gm_world_set_block(w,35,92,z,49);}
    for(int y=71;y<=91;++y){gm_world_set_block(w,35,y,7,49);gm_world_set_block(w,35,y,29,49);}
    gm_world_set_block(w,35,91,28,51);
    placed=gm_portal_ignite(w,35,91,28);count=0;
    for(int z=8;z<=28;++z)for(int y=71;y<=91;++y)
        if(gm_world_block(w,35,y,z)==90&&gm_world_meta(w,35,y,z)==2)++count;
    if(placed!=441||count!=441){fprintf(stderr,"portal_live: max z placed=%d count=%d\n",placed,count);return 1;}
    for(int x=7;x<=29;++x){
        gm_world_set_block(w,x,70,15,49);
        if(x!=18)gm_world_set_block(w,x,92,15,49);
    }
    for(int y=71;y<=91;++y){gm_world_set_block(w,7,y,15,49);gm_world_set_block(w,29,y,15,49);}
    gm_world_set_block(w,28,91,15,51);
    placed=gm_portal_ignite(w,28,91,15);count=0;
    for(int x=8;x<=28;++x)for(int y=71;y<=91;++y)
        count+=gm_world_block(w,x,y,15)==90;
    if(placed!=0||count!=0){fprintf(stderr,"portal_live: broken max placed=%d count=%d\n",placed,count);return 1;}
    const int fx[12]={4,5,6,4,5,6,3,3,3,7,7,7};
    const int fz[12]={3,3,3,7,7,7,4,5,6,4,5,6};
    const int ff[12]={2,2,2,0,0,0,1,1,1,3,3,3};
    for(int i=0;i<12;++i)gm_world_set_block_meta(w,fx[i],70,fz[i],120,ff[i]);
    for(int i=0;i<12;++i){
        int r=gm_end_portal_insert_eye(w,fx[i],70,fz[i]);
        if(r!=(i==11?2:1)){fprintf(stderr,"end portal insert %d result %d\n",i,r);return 1;}
    }
    int end_count=0;
    for(int x=0;x<11;++x)for(int z=0;z<11;++z)if(gm_world_block(w,x,70,z)==119)++end_count;
    gm_world_destroy(w);
    if(end_count!=9){fprintf(stderr,"portal_live: end count=%d\n",end_count);return 1;}

    GmWorld *search=gm_world_create_type(0,1);if(!search)return 1;
    gm_world_ensure(search,0,0,9);
    gm_world_set_block_meta(search,0,200,0,90,1);
    for(int y=100;y<=102;++y)
        gm_world_set_block_meta(search,10,y,0,90,1);
    double px=0.0,py=0.0,pz=0.0;
    if(!gm_portal_find_existing(
            search,0,100,0,&px,&py,&pz)
            ||px!=10.5||py!=100.0||pz!=0.5){
        fprintf(stderr,
            "portal_live: 3d nearest selected %.17g %.17g %.17g\n",
            px,py,pz);return 1;
    }
    gm_world_destroy(search);

    search=gm_world_create_type(0,1);if(!search)return 1;
    gm_world_ensure(search,0,0,9);
    gm_world_set_block_meta(search,-1,80,0,90,1);
    gm_world_set_block_meta(search,1,80,0,90,1);
    if(!gm_portal_find_existing(
            search,0,80,0,&px,&py,&pz)
            ||px!=-0.5||py!=80.0||pz!=0.5){
        fprintf(stderr,
            "portal_live: scan-order tie selected %.17g %.17g %.17g\n",
            px,py,pz);return 1;
    }
    gm_world_destroy(search);

    search=gm_world_create_type(0,1);if(!search)return 1;
    uint64_t portal_seed=UINT64_C(0x5deece66d);
    uint64_t seed_before=portal_seed;
    if(!gm_portal_find_or_make(
            search,8.25,5.0,8.75,&portal_seed,&px,&py,&pz)){
        fprintf(stderr,"portal_live: exact portal construction failed\n");
        return 1;
    }
    uint64_t expected_seed=(seed_before*UINT64_C(0x5deece66d)+11)
        &((UINT64_C(1)<<48)-1);
    int made_portal=0,made_obsidian=0,portal_meta=-1;
    for(int x=-16;x<=32;++x)for(int y=0;y<=20;++y)
        for(int z=-16;z<=32;++z){
            int id=gm_world_block(search,x,y,z);
            if(id==90){
                ++made_portal;
                if(portal_meta<0)portal_meta=gm_world_meta(search,x,y,z);
                else if(portal_meta!=gm_world_meta(search,x,y,z)){
                    fprintf(stderr,"portal_live: mixed constructed axes\n");
                    return 1;
                }
            }else if(id==49)++made_obsidian;
        }
    if(portal_seed!=expected_seed||made_portal!=6||made_obsidian!=14
            ||(portal_meta!=1&&portal_meta!=2)){
        fprintf(stderr,
            "portal_live: construction seed=%llu portal=%d obsidian=%d meta=%d\n",
            (unsigned long long)portal_seed,made_portal,made_obsidian,
            portal_meta);return 1;
    }
    seed_before=portal_seed;
    double px2=0.0,py2=0.0,pz2=0.0;
    if(!gm_portal_find_or_make(
            search,8.25,5.0,8.75,&portal_seed,&px2,&py2,&pz2)
            ||portal_seed!=seed_before||px2!=px||py2!=py||pz2!=pz){
        fprintf(stderr,"portal_live: existing portal consumed make RNG\n");
        return 1;
    }
    {
        double vec_x,vec_y,x=px,y=py,z=pz,vx=0.25,vz=-0.5;
        float yaw=37.5f;
        int direction;
        if(!gm_portal_capture_entry(
                    search,(int)px,(int)py,(int)pz,
                    px+0.125,py+1.75,pz+0.375,
                    &vec_x,&vec_y,&direction)
                ||!gm_portal_place_player_existing(
                    search,(int)px,(int)py,(int)pz,
                    vec_x,vec_y,direction,&x,&y,&z,&vx,&vz,&yaw)
                ||(x==floor(x)+0.5&&z==floor(z)+0.5
                    &&y==floor(y))){
            fprintf(stderr,
                "portal_live: player placement lost exact doubles %.17g %.17g %.17g\n",
                x,y,z);return 1;
        }
    }
    {
        GmPortalCache cache={0};
        uint64_t cached_seed=portal_seed;
        double cx,cy,cz;
        if(!gm_portal_find_or_make_cached(
                    search,&cache,0,8.25,5.0,8.75,&cached_seed,
                    &cx,&cy,&cz)
                ||cache.count!=1||cached_seed!=portal_seed
                ||cx!=px||cy!=py||cz!=pz){
            fprintf(stderr,"portal_live: initial cache insert mismatch\n");
            return 1;
        }
        for(int x=(int)px-4;x<=(int)px+4;++x)
            for(int y=(int)py-1;y<=(int)py+4;++y)
                for(int z=(int)pz-4;z<=(int)pz+4;++z)
                    if(gm_world_block(search,x,y,z)==90)
                        gm_world_set_block(search,x,y,z,0);
        if(!gm_portal_find_or_make_cached(
                    search,&cache,1,8.25,5.0,8.75,&cached_seed,
                    &cx,&cy,&cz)
                ||cx!=px||cy!=py||cz!=pz
                ||cache.entries[0].last_update_time!=1){
            fprintf(stderr,"portal_live: cached stale coordinate not reused\n");
            return 1;
        }
        gm_portal_cache_prune(&cache,300);
        if(cache.count!=1){
            fprintf(stderr,"portal_live: strict cache cutoff removed equality\n");
            return 1;
        }
        gm_portal_cache_prune(&cache,400);
        if(cache.count!=0){
            fprintf(stderr,"portal_live: stale cache entry survived cutoff\n");
            return 1;
        }
        gm_portal_cache_clear(&cache);
        if(cache.entries||cache.count||cache.cap){
            fprintf(stderr,"portal_live: cache clear retained allocation\n");
            return 1;
        }
    }
    gm_world_destroy(search);
    fprintf(stderr,"portal_live: PASS\n");return 0;
}
