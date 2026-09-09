/* Reuse the trainer's crop primitive for explicit measurement inputs. */
#include <stdio.h>
#include <stdlib.h>
#include "../../blaze/env/blaze_snapshot.h"
int main(int argc,char**argv){
 if(argc!=4){fprintf(stderr,"usage: prepare INPUT OUTPUT XZ_SIZE\n");return 2;}
 CuSnapshot s;char err[1024];int size=atoi(argv[3]);
 if(!blaze_snapshot_load(argv[1],&s,err,sizeof err,0)){fprintf(stderr,"%s\n",err);return 2;}
 if(!blaze_snapshot_crop(&s,size,err,sizeof err,0)||!blaze_snapshot_write(argv[2],&s,err,sizeof err)){fprintf(stderr,"%s\n",err);blaze_snapshot_free(&s);return 2;}
 printf("output=%s dims=%dx%dx%d mobs=%u items=%u projectiles=%u furnaces=%u liquid=%d\n",argv[2],s.head.rnx,s.head.rny,s.head.rnz,s.n_mobs,s.head.n_items,s.n_proj,s.n_furn,s.has_liquid);
 blaze_snapshot_free(&s);return 0;
}
