/* Reuse the trainer's crop primitive for explicit measurement inputs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../blaze/env/blaze_snapshot.h"
int main(int argc,char**argv){
 if((argc!=4&&argc!=5)||!strcmp(argv[1],argv[2])){fprintf(stderr,"usage: prepare INPUT OUTPUT XZ_SIZE [boundary|death]\n");return 2;}
 CuSnapshot s;char err[1024];int size=atoi(argv[3]);
 if(!blaze_snapshot_load(argv[1],&s,err,sizeof err,0)){fprintf(stderr,"%s\n",err);return 2;}
 if(!blaze_snapshot_crop(&s,size,err,sizeof err,0)){fprintf(stderr,"%s\n",err);blaze_snapshot_free(&s);return 2;}
 if(argc==5){
  if(!strcmp(argv[4],"death"))s.head.health=0;
  else if(!strcmp(argv[4],"boundary")){
   double x=s.head.rx0-s.head.ox+0.31,z=s.head.rz0-s.head.oz+s.head.rnz/2+0.5,dx=x-s.head.px,dz=z-s.head.pz;
   s.head.px=x;s.head.pz=z;s.head.box[0]+=dx;s.head.box[3]+=dx;s.head.box[2]+=dz;s.head.box[5]+=dz;s.head.yaw=90;s.head.pitch=0;s.head.mx=s.head.mz=0;
   int iy=(int)s.head.py-s.head.ry0;
   for(int ix=0;ix<5;ix++)for(int iz=s.head.rnz/2-2;iz<=s.head.rnz/2+2;iz++)for(int y=iy-1;y<iy+4;y++)if(y>=0&&y<s.head.rny)s.cells[((size_t)ix*s.head.rny+y)*s.head.rnz+iz]=(y==iy-1)?(1<<4):0;
  }else{fprintf(stderr,"unknown synthetic case\n");blaze_snapshot_free(&s);return 2;}
 }
 if(!blaze_snapshot_write(argv[2],&s,err,sizeof err)){fprintf(stderr,"%s\n",err);blaze_snapshot_free(&s);return 2;}
 printf("output=%s dims=%dx%dx%d mobs=%u items=%u projectiles=%u furnaces=%u liquid=%d\n",argv[2],s.head.rnx,s.head.rny,s.head.rnz,s.n_mobs,s.head.n_items,s.n_proj,s.n_furn,s.has_liquid);
 blaze_snapshot_free(&s);return 0;
}
