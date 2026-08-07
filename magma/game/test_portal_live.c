#include "game/portal_live.h"

#include <stdio.h>

int main(void){
    GmWorld *w=gm_world_create_type(0,1);if(!w)return 1;
    gm_world_ensure(w,0,0,2);
    for(int x=0;x<4;++x){gm_world_set_block(w,x,70,0,49);gm_world_set_block(w,x,74,0,49);}
    for(int y=71;y<74;++y){gm_world_set_block(w,0,y,0,49);gm_world_set_block(w,3,y,0,49);}
    gm_world_set_block(w,1,71,0,51);
    int placed=gm_portal_ignite(w,1,71,0),count=0;
    for(int x=1;x<=2;++x)for(int y=71;y<=73;++y)
        if(gm_world_block(w,x,y,0)==90&&gm_world_meta(w,x,y,0)==1)++count;
    if(placed!=6||count!=6){fprintf(stderr,"portal_live: placed=%d count=%d\n",placed,count);return 1;}
    for(int z=0;z<4;++z){gm_world_set_block(w,10,70,z,49);gm_world_set_block(w,10,74,z,49);}
    for(int y=71;y<74;++y){gm_world_set_block(w,10,y,0,49);gm_world_set_block(w,10,y,3,49);}
    gm_world_set_block(w,10,71,1,51);
    placed=gm_portal_ignite(w,10,71,1);count=0;
    for(int z=1;z<=2;++z)for(int y=71;y<=73;++y)
        if(gm_world_block(w,10,y,z)==90&&gm_world_meta(w,10,y,z)==2)++count;
    if(placed!=6||count!=6){fprintf(stderr,"portal_live: z placed=%d count=%d\n",placed,count);return 1;}
    for(int x=7;x<=29;++x){gm_world_set_block(w,x,70,10,49);gm_world_set_block(w,x,92,10,49);}
    for(int y=71;y<=91;++y){gm_world_set_block(w,7,y,10,49);gm_world_set_block(w,29,y,10,49);}
    gm_world_set_block(w,28,91,10,51);
    placed=gm_portal_ignite(w,28,91,10);count=0;
    for(int x=8;x<=28;++x)for(int y=71;y<=91;++y)
        if(gm_world_block(w,x,y,10)==90&&gm_world_meta(w,x,y,10)==1)++count;
    if(placed!=441||count!=441){fprintf(stderr,"portal_live: max x placed=%d count=%d\n",placed,count);return 1;}
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
    fprintf(stderr,"portal_live: PASS\n");return 0;
}
