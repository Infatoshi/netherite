/* Bounded bridge test: shared CPU pixel reference, edits, freshness, reset. */
#include "env_cuda_obs.h"
#include "../../blaze/core/obs_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 3
#define VOL (24*32*24)
static u16 cells[N][VOL];
static int fresh[N];
static double phase;
static int inputs(void *h,int e,double *x,double *y,double *z,float *yaw,float *pitch,int *x0,int *y0,int *z0,int *nx,int *ny,int *nz,const u16 **c) {
 (void)h;*x=0.25+phase;*y=10.625;*z=-0.3;*yaw=37.3f+e*71.0f;*pitch=5.7f+e*13.5f;
 *x0=-12;*y0=0;*z0=-12;*nx=24;*ny=32;*nz=24;*c=cells[e];return 0;
}
static int freshness(void*h,int e){(void)h;return fresh[e];}
int main(void) {
 McSinTable st;mc_sin_table_init(&st);u16 expected[N*OC_NPIX],actual[N*OC_NPIX];u8 dep[N*OC_NPIX],edg[N*OC_NPIX],ad[N*OC_NPIX],ae[N*OC_NPIX];
 memset(expected,0,sizeof(expected));memset(dep,0,sizeof(dep));memset(edg,0,sizeof(edg));
 EnvCudaObs*s=env_cuda_obs_create(0,N,VOL,inputs,freshness);if(!s)return 1;
 for(int t=0;t<8;t++) {
  phase=t*.03125;
  for(int e=0;e<N;e++) {
   fresh[e]=(t%3!=e);for(int j=0;j<VOL;j++)cells[e][j]=(j%71==t)?(u16)((1+e*15)<<4):0;
   if(t==0||t==7||fresh[e]) {
    OcRegion r;double x,y,z;float yaw,pitch;inputs(s,e,&x,&y,&z,&yaw,&pitch,&r.x0,&r.y0,&r.z0,&r.nx,&r.ny,&r.nz,&r.cells);
    for(int p=0;p<OC_NPIX;p++)oc_pixel(&r,&st,x,y,z,yaw,pitch,p%OC_W,p/OC_W,expected+e*OC_NPIX+p,dep+e*OC_NPIX+p,edg+e*OC_NPIX+p);
   }
  }
  EnvCudaObsStats stats;if(env_cuda_obs_render(s,s,t==0||t==7,actual,ad,ae,&stats))return 2;
  if(memcmp(expected,actual,sizeof(expected))||memcmp(dep,ad,sizeof(dep))||memcmp(edg,ae,sizeof(edg))){fprintf(stderr,"mismatch tick %d\n",t);return 3;}
  printf("tick=%d pixels=%d exact=1 h2d=%zu d2h=%zu\n",t,N*OC_NPIX,stats.h2d_bytes,stats.d2h_bytes);
 }
 env_cuda_obs_destroy(s);return 0;
}
