#include "env_cuda_obs.h"
#include "../../blaze/core/obs_camera.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
struct Camera { OcRegion region; double x,y,z; float yaw,pitch; int fresh; };
struct Span { size_t start,count; };
struct EnvCudaObs {
 int device,n,delta; size_t cap; Span *spans; unsigned char *valid; size_t *counts; HybridCamInputsFn inputs; HybridCamFreshFn fresh;
 u16 *hcells,*dcells,*cam; u8 *depth,*edge; Camera *hcameras,*dcameras;
 McSinTable *trig; cudaStream_t stream; cudaEvent_t ev[4];
};
static double now_ms() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
#define CK(call) do { cudaError_t err=(call); if(err!=cudaSuccess) { fprintf(stderr,"hybrid CUDA: %s: %s\n",#call,cudaGetErrorString(err)); return -1; } } while(0)
__global__ void hybrid_semantic_camera(const Camera *c,const McSinTable *trig,int n,u16 *cam,u8 *depth,u8 *edge) {
 size_t i=(size_t)blockIdx.x*blockDim.x+threadIdx.x; int e=(int)(i/OC_NPIX),p=(int)(i%OC_NPIX);
 if(e>=n || !c[e].fresh) return;
 Camera v=c[e]; oc_pixel(&v.region,trig,v.x,v.y,v.z,v.yaw,v.pitch,p%OC_W,p/OC_W,cam+i,depth+i,edge+i);
}
extern "C" void env_cuda_obs_destroy(EnvCudaObs *s) {
 if(!s)return; cudaSetDevice(s->device); if(s->stream)cudaStreamSynchronize(s->stream);
 cudaFreeHost(s->hcells);cudaFreeHost(s->hcameras);cudaFree(s->dcells);cudaFree(s->dcameras);
 cudaFree(s->cam);cudaFree(s->depth);cudaFree(s->edge);cudaFree(s->trig);
 for(int i=0;i<4;i++)if(s->ev[i])cudaEventDestroy(s->ev[i]);
 if(s->stream)cudaStreamDestroy(s->stream); free(s->spans);free(s->valid);free(s->counts);free(s);
}
extern "C" int env_cuda_obs_reset(EnvCudaObs *s,const unsigned char *mask) {
 if(!s)return -1; CK(cudaSetDevice(s->device));
 for(int e=0;e<s->n;e++) if(!mask||mask[e]) {
  if(s->valid)s->valid[e]=0;
  size_t off=(size_t)e*OC_NPIX;
  CK(cudaMemsetAsync(s->cam+off,0,OC_NPIX*sizeof(u16),s->stream));
  CK(cudaMemsetAsync(s->depth+off,255,OC_NPIX,s->stream));
  CK(cudaMemsetAsync(s->edge+off,0,OC_NPIX,s->stream));
 }
 CK(cudaStreamSynchronize(s->stream)); return 0;
}
static int allocate(EnvCudaObs *s) {
 CK(cudaSetDevice(s->device)); CK(cudaStreamCreateWithFlags(&s->stream,cudaStreamNonBlocking));
 for(int i=0;i<4;i++)CK(cudaEventCreate(&s->ev[i]));
 size_t nc=(size_t)s->n*s->cap,np=(size_t)s->n*OC_NPIX;
 s->spans=(Span*)calloc((size_t)s->n*(s->cap/4096+2),sizeof(Span));
 s->valid=(unsigned char*)calloc(s->n,1);s->counts=(size_t*)calloc(s->n,sizeof(size_t));
 if(!s->spans||!s->valid||!s->counts)return -1;
 CK(cudaMallocHost(&s->hcells,nc*sizeof(u16))); CK(cudaMalloc(&s->dcells,nc*sizeof(u16)));
 CK(cudaMallocHost(&s->hcameras,s->n*sizeof(Camera)));CK(cudaMalloc(&s->dcameras,s->n*sizeof(Camera)));
 CK(cudaMalloc(&s->cam,np*sizeof(u16)));CK(cudaMalloc(&s->depth,np));CK(cudaMalloc(&s->edge,np));
 CK(cudaMalloc(&s->trig,sizeof(McSinTable))); McSinTable t;mc_sin_table_init(&t);
 CK(cudaMemcpy(s->trig,&t,sizeof(t),cudaMemcpyHostToDevice));return env_cuda_obs_reset(s,NULL);
}
extern "C" EnvCudaObs *env_cuda_obs_create(int device,int n,size_t cap,HybridCamInputsFn inputs,HybridCamFreshFn fresh) {
 if(n<=0||!cap||!inputs||!fresh||cap>SIZE_MAX/(size_t)n/sizeof(u16))return NULL;
 EnvCudaObs *s=(EnvCudaObs*)calloc(1,sizeof(*s));if(!s)return NULL;
 s->device=device;s->n=n;s->cap=cap;s->inputs=inputs;s->fresh=fresh;
 if(allocate(s)){env_cuda_obs_destroy(s);return NULL;} return s;
}
extern "C" int env_cuda_obs_render(EnvCudaObs *s,void *env,int force,u16 *cam,u8 *depth,u8 *edge,EnvCudaObsStats *stats) {
 if(!s||!env)return -1; EnvCudaObsStats z={};double start=now_ms();size_t used=0,nspans=0;
 CK(cudaSetDevice(s->device));
 for(int e=0;e<s->n;e++) {
  Camera *c=s->hcameras+e; const u16 *cells=NULL; int f=s->fresh(env,e);if(f<0)return -1;
  memset(c,0,sizeof(*c));c->fresh=force||f;
  if(!c->fresh)continue;
  OcRegion *r=&c->region;
  if(s->inputs(env,e,&c->x,&c->y,&c->z,&c->yaw,&c->pitch,&r->x0,&r->y0,&r->z0,&r->nx,&r->ny,&r->nz,&cells))return -1;
  if(!cells||r->nx<=0||r->ny<=0||r->nz<=0)return -1;
  size_t count=(size_t)r->nx*r->ny*r->nz;if(count>s->cap)return -1;
  size_t base=(size_t)e*s->cap, first=nspans, changed=0;
  if(!s->delta || !s->valid[e] || s->counts[e]!=count) {
   memcpy(s->hcells+base,cells,count*sizeof(u16));s->spans[nspans++]={base,count};changed=count;
  } else {
   for(size_t p=0;p<count;p+=4096) {
    size_t len=count-p<4096?count-p:4096;
    if(memcmp(s->hcells+base+p,cells+p,len*sizeof(u16))) {
     memcpy(s->hcells+base+p,cells+p,len*sizeof(u16));changed+=len;
     if(nspans>first && s->spans[nspans-1].start+s->spans[nspans-1].count==base+p) s->spans[nspans-1].count+=len;
     else s->spans[nspans++]={base+p,len};
    }
   }
   /* Dense fallback bounds dispatch count when most pages changed. */
   if(changed>count/2){nspans=first;s->spans[nspans++]={base,count};changed=count;}
  }
  s->valid[e]=1;s->counts[e]=count;r->cells=s->dcells+base;used+=changed;
 }
 z.pack_ms=now_ms()-start; z.h2d_bytes=used*sizeof(u16)+s->n*sizeof(Camera);
 CK(cudaEventRecord(s->ev[0],s->stream));
 for(size_t j=0;j<nspans;j++) { Span p=s->spans[j];
  CK(cudaMemcpyAsync(s->dcells+p.start,s->hcells+p.start,p.count*sizeof(u16),cudaMemcpyHostToDevice,s->stream));
 }
 CK(cudaMemcpyAsync(s->dcameras,s->hcameras,s->n*sizeof(Camera),cudaMemcpyHostToDevice,s->stream));
 CK(cudaEventRecord(s->ev[1],s->stream));size_t np=(size_t)s->n*OC_NPIX;
 hybrid_semantic_camera<<<(unsigned)((np+127)/128),128,0,s->stream>>>(s->dcameras,s->trig,s->n,s->cam,s->depth,s->edge);
 CK(cudaGetLastError());CK(cudaEventRecord(s->ev[2],s->stream));
 if(cam){CK(cudaMemcpyAsync(cam,s->cam,np*sizeof(u16),cudaMemcpyDeviceToHost,s->stream));z.d2h_bytes+=np*sizeof(u16);}
 if(depth){CK(cudaMemcpyAsync(depth,s->depth,np,cudaMemcpyDeviceToHost,s->stream));z.d2h_bytes+=np;}
 if(edge){CK(cudaMemcpyAsync(edge,s->edge,np,cudaMemcpyDeviceToHost,s->stream));z.d2h_bytes+=np;}
 CK(cudaEventRecord(s->ev[3],s->stream));CK(cudaStreamSynchronize(s->stream));
 float t;CK(cudaEventElapsedTime(&t,s->ev[0],s->ev[1]));z.upload_ms=t;
 CK(cudaEventElapsedTime(&t,s->ev[1],s->ev[2]));z.kernel_ms=t;
 CK(cudaEventElapsedTime(&t,s->ev[2],s->ev[3]));z.download_ms=t;if(stats)*stats=z;return 0;
}
extern "C" const u16 *env_cuda_obs_cam(EnvCudaObs *s){return s?s->cam:NULL;}
extern "C" const u8 *env_cuda_obs_depth(EnvCudaObs *s){return s?s->depth:NULL;}
extern "C" const u8 *env_cuda_obs_edge(EnvCudaObs *s){return s?s->edge:NULL;}

extern "C" int env_cuda_obs_set_delta(EnvCudaObs *s,int enabled) {
 if(!s || (enabled!=0 && enabled!=1))return -1;
 s->delta=enabled;memset(s->valid,0,s->n);return 0;
}
