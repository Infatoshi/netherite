/* Measurement only: two independent cohorts, one CPU worker and one GPU owner.
 * READY publishes CPU state; EMPTY returns ownership only after render/commit.
 * Fixed scripted actions preserve identical per-world dependencies and work.
 * Policy inference is real but its output does not drive the scripted workload. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <pthread.h>
#include <omp.h>
#include <cuda_runtime_api.h>
#include <cuda_profiler_api.h>
#include <nvtx3/nvToolsExt.h>
#include "../../blaze/env/blaze_abi.h"
#include "../../blaze/rl/env_cuda_stage.h"
#include "../../blaze/rl/obs_pack.h"
#include "../../blaze/rl/rl_ckpt.h"
#include "../../blaze/nn/nn.h"
#include "env_cuda_obs.h"
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void die(const char*s){fprintf(stderr,"FATAL %s\n",s);exit(2);}
static void *mem(size_t n,size_t z){void*p=calloc(n,z);if(!p)die("allocation");return p;}
static void *symbol(void*l,const char*s){void*p=dlsym(l,s);if(!p)die(dlerror());return p;}
static void hash(uint64_t *h,const void*v,size_t n){const unsigned char*p=v;for(size_t i=0;i<n;i++)*h=(*h^p[i])*1099511628211ULL;}
static int n=8,steps=32,warmup=4,repeat=4,threads=8,reset_every=64,policy=1,verify=1,overlap=0;
static int work_move=0,mobs=0,det_ai=0,profile=0,profile_active=0;
static void*(*create_env)(int,int,const BlazeCreateOpts*);
static void(*destroy_env)(void*);
static int(*load_env)(void*,const char*const*,int,char*,int),(*assign_env)(void*,const int*),(*reset_env)(void*,const unsigned char*),(*success_env)(void*,int);
static int(*set_mobs)(void*,int),(*set_rng)(void*,int);
static int(*parity_state)(void*,int,void*),(*commit)(void*,const unsigned short*,const unsigned char*,const unsigned char*);
static BlazeStepFullFn step_env;
static EnvCudaObs*(*create_obs)(int,int,size_t,HybridCamInputsFn,HybridCamFreshFn);
static int(*reset_obs)(EnvCudaObs*,const unsigned char*),(*delta_obs)(EnvCudaObs*,int),(*render_obs)(EnvCudaObs*,void*,int,unsigned short*,unsigned char*,unsigned char*,EnvCudaObsStats*);
static void(*destroy_obs)(EnvCudaObs*);
static Nn *nn;
static int psize;
static unsigned long long(*tick_sum)(void*);
typedef struct {
 void*env;EnvCudaObs*obs;int state; /* 0 EMPTY, 1 CPU, 2 READY, 3 GPU */
 unsigned short*cam;unsigned char*depth,*edge,*done,*mask,*have,*planes,*prior,*scratch;
 float*scal,*rew,*pose,*scalars,*logits,*values,*logp;
 int*status,*epdec;int32_t*acts;double*actions;void*parity;
 uint64_t digest,policy_digest,h2d,d2h,terminals,actual_ticks;double cpu_s,gpu_s,pack_s,upload_s,kernel_s,download_s;
} Cohort;
static Cohort cohort[2];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static void init(Cohort*c,const char*fixture,HybridCamInputsFn inputs,HybridCamFreshFn fresh,int delta){
 BlazeCreateOpts o;blaze_create_opts_default(&o);c->env=create_env(0,n,&o);if(!c->env)die("create");
 char error[1024]={0};if(load_env(c->env,&fixture,1,error,sizeof error)!=1)die(error);
 int*assignment=mem(n,sizeof(int));c->mask=mem(n,1);memset(c->mask,1,n);
 if(mobs&&set_mobs(c->env,1))die("enable mobs");
 if(det_ai&&set_rng(c->env,1))die("enable deterministic AI");
 if(assign_env(c->env,assignment)||success_env(c->env,0)||reset_env(c->env,c->mask))die("initial reset");
 free(assignment);
 size_t capacity=0;for(int e=0;e<n;e++){int x,y,z;if(inputs(c->env,e,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,&x,&y,&z,NULL))die("dimensions");size_t k=(size_t)x*y*z;if(k>capacity)capacity=k;}
 c->obs=create_obs(0,n,capacity,inputs,fresh);if(!c->obs||delta_obs(c->obs,delta))die("bridge create");
 size_t pixels=(size_t)n*ENV_NPIX;c->cam=mem(pixels,2);c->depth=mem(pixels,1);c->edge=mem(pixels,1);c->done=mem(n,1);c->have=mem(n,1);
 c->scal=mem((size_t)n*ENV_SCAL,4);c->rew=mem(n,4);c->pose=mem((size_t)n*ENV_POSE,4);c->status=mem((size_t)n*ENV_STATUS,sizeof(int));c->epdec=mem(n,sizeof(int));c->actions=mem((size_t)n*ENV_ACT,8);
 c->planes=mem(pixels*ENV_N_CH,1);c->prior=mem(pixels*ENV_N_PLANES,1);c->scratch=mem(pixels*ENV_N_PLANES,1);c->scalars=mem((size_t)n*POL_SCAL,4);
 c->logits=mem((size_t)n*34,4);c->values=mem(n,4);c->logp=mem(n,4);c->acts=mem((size_t)n*POL_HEADS,4);c->parity=mem(n,psize);c->digest=c->policy_digest=1469598103934665603ULL;
}
static void cpu(Cohort*c,int t){
 if(profile_active)nvtxRangePushA(c==cohort?"CPU cohort 0":"CPU cohort 1");
 double start=now();int any=0;
 for(int e=0;e<n;e++){c->mask[e]=(t%reset_every==0)||c->done[e];any|=c->mask[e];if(c->mask[e]){c->have[e]=0;c->epdec[e]=0;}}
 if(any&&reset_env(c->env,c->mask))die("CPU reset");
 memset(c->actions,0,(size_t)n*ENV_ACT*8);
 for(int e=0;e<n;e++){double*a=c->actions+(size_t)e*ENV_ACT;a[9]=-1;a[10]=-1;if(work_move){a[0]=(t/8+e)%3?1:0;a[2]=t%8?0:((e%2)?15:-15);a[4]=t%9==0;}}
 unsigned long long before=tick_sum?tick_sum(c->env):0;
 if(step_env(c->env,c->actions,repeat,NULL,NULL,NULL,c->scal,c->rew,c->done,c->pose,c->status))die("CPU step");
 if(tick_sum)c->actual_ticks+=tick_sum(c->env)-before;
 c->cpu_s+=now()-start;
 if(profile_active)nvtxRangePop();
}
static void gpu(Cohort*c){
 if(profile_active)nvtxRangePushA(c==cohort?"GPU stage cohort 0":"GPU stage cohort 1");
 double start=now();EnvCudaObsStats stats={0};
 /* No other thread accesses this cohort between READY and EMPTY. */
 int any=0;for(int e=0;e<n;e++)any|=c->mask[e];if(any&&reset_obs(c->obs,c->mask))die("GPU reset");
 if(render_obs(c->obs,c->env,0,c->cam,c->depth,c->edge,&stats)||commit(c->env,c->cam,c->depth,c->edge))die("render/commit");
 pack_obs(c->cam,c->depth,c->edge,c->scal,c->pose,c->status,c->epdec,1500,c->have,c->prior,n,c->planes,c->scalars,c->scratch);
 memcpy(c->prior,c->scratch,(size_t)n*ENV_N_PLANES*ENV_NPIX);for(int e=0;e<n;e++)c->have[e]=1;for(int e=0;e<n;e++)c->epdec[e]++;
 if(policy&&(nn_forward(nn,c->planes,c->scalars,n,c->logits,c->values)||nn_sample(nn,c->logits,n,NN_SAMPLE_GREEDY,c->acts,c->logp,NULL)))die(nn_last_error());
 c->gpu_s+=now()-start;c->pack_s+=stats.pack_ms*.001;c->upload_s+=stats.upload_ms*.001;c->kernel_s+=stats.kernel_ms*.001;c->download_s+=stats.download_ms*.001;c->h2d+=stats.h2d_bytes;c->d2h+=stats.d2h_bytes;
 for(int e=0;e<n;e++)c->terminals+=c->done[e]!=0;
 if(profile_active)nvtxRangePop();
 /* Digest cost is outside GPU stage but inside total when --verify=1. */
 if(verify){size_t pixels=(size_t)n*ENV_NPIX;
  hash(&c->digest,c->cam,pixels*2);hash(&c->digest,c->depth,pixels);hash(&c->digest,c->edge,pixels);hash(&c->digest,c->rew,n*4);hash(&c->digest,c->done,n);hash(&c->digest,c->status,(size_t)n*ENV_STATUS*sizeof(int));
  for(int e=0;e<n;e++){if(parity_state(c->env,e,(char*)c->parity+(size_t)e*psize))die("parity");}hash(&c->digest,c->parity,(size_t)n*psize);
  if(policy){hash(&c->policy_digest,c->logits,(size_t)n*34*4);hash(&c->policy_digest,c->values,n*4);hash(&c->policy_digest,c->acts,(size_t)n*POL_HEADS*4);}
 }
}
static void *worker(void*unused){(void)unused;omp_set_num_threads(threads);
 for(int t=warmup;t<steps+warmup;t++)for(int k=0;k<2;k++){Cohort*c=cohort+k;pthread_mutex_lock(&lock);while(c->state!=0)pthread_cond_wait(&changed,&lock);c->state=1;pthread_mutex_unlock(&lock);cpu(c,t);pthread_mutex_lock(&lock);c->state=2;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&lock);}
 return NULL;
}
int main(int argc,char**argv){
 const char*fixture="verify/fixtures/port/s10_t0_r64_no_liquid.bsnp",*libpath="out/blaze/env/blaze_cpu.so",*checkpoint=NULL;int delta=1;
 for(int i=1;i<argc;i++){if(i+1>=argc)die("argument value");const char*k=argv[i],*v=argv[++i];
  if(!strcmp(k,"--fixture"))fixture=v;else if(!strcmp(k,"--lib"))libpath=v;else if(!strcmp(k,"--checkpoint"))checkpoint=v;
  else if(!strcmp(k,"--cohort-n"))n=atoi(v);else if(!strcmp(k,"--steps"))steps=atoi(v);else if(!strcmp(k,"--warmup"))warmup=atoi(v);else if(!strcmp(k,"--repeat"))repeat=atoi(v);else if(!strcmp(k,"--threads"))threads=atoi(v);else if(!strcmp(k,"--reset-every"))reset_every=atoi(v);else if(!strcmp(k,"--policy"))policy=atoi(v);else if(!strcmp(k,"--verify"))verify=atoi(v);else if(!strcmp(k,"--overlap"))overlap=atoi(v);else if(!strcmp(k,"--delta"))delta=atoi(v);else if(!strcmp(k,"--move"))work_move=atoi(v);else if(!strcmp(k,"--profile"))profile=atoi(v);else if(!strcmp(k,"--mobs"))mobs=atoi(v);else if(!strcmp(k,"--det-ai"))det_ai=atoi(v);else die("unknown option");
 }
 if(n<1||n>4096||steps<1||warmup<0||repeat<1||threads<1||reset_every<1||(overlap<0||overlap>2)||(profile!=0&&profile!=1))die("invalid config");
 omp_set_num_threads(threads);
 void*lib=dlopen(libpath,RTLD_NOW|RTLD_LOCAL);if(!lib)die(dlerror());
#define LOAD(name,s) name=symbol(lib,s)
 tick_sum=(unsigned long long(*)(void*))dlsym(lib,"blaze_measure_tick_sum");
 LOAD(set_mobs,"blaze_set_mobs_enabled");LOAD(set_rng,"blaze_set_det_entity_rng");LOAD(create_env,"blaze_create");LOAD(destroy_env,"blaze_destroy");LOAD(load_env,"blaze_load_snapshots");LOAD(assign_env,"blaze_assign");LOAD(reset_env,"blaze_reset");LOAD(success_env,"blaze_set_success_item");LOAD(step_env,"blaze_step_full_no_camera");LOAD(commit,"blaze_obs_cam_commit");LOAD(parity_state,"blaze_parity_state");int(*size_fn)(void)=symbol(lib,"blaze_parity_size");psize=size_fn();if(psize<1||psize>1000000)die("parity size");HybridCamInputsFn inputs=symbol(lib,"blaze_obs_cam_inputs");HybridCamFreshFn fresh=symbol(lib,"blaze_obs_cam_fresh");
 void*bridge=dlopen("out/verify/architecture/env_cuda_obs.so",RTLD_NOW|RTLD_LOCAL);if(!bridge)die(dlerror());
 create_obs=symbol(bridge,"env_cuda_obs_create");reset_obs=symbol(bridge,"env_cuda_obs_reset");delta_obs=symbol(bridge,"env_cuda_obs_set_delta");render_obs=symbol(bridge,"env_cuda_obs_render");destroy_obs=symbol(bridge,"env_cuda_obs_destroy");
 init(cohort,fixture,inputs,fresh,delta);init(cohort+1,fixture,inputs,fresh,delta);
 if(policy){NnCreate d={NN_BACKEND_CUDA,0,n,nn_config_default(),NN_PREC_FAST};nn=nn_create(&d);if(!nn)die(nn_last_error());if(checkpoint&&rl_ckpt_load(nn,checkpoint))die(nn_last_error());if(nn_prepare_n(nn,n)||nn_seal(nn))die(nn_last_error());}
 uint64_t reference_digest[2]={0},reference_policy[2]={0};
 printf("AI mobs=%d deterministic=%d natural_spawn=0\n",mobs,det_ai);
 int selected_mode=overlap;
 for(int run=0;run<(selected_mode==2?2:1);run++){overlap=selected_mode==2?run:selected_mode;
 for(int t=0;t<warmup;t++)for(int k=0;k<2;k++){cpu(cohort+k,t);gpu(cohort+k);}
 for(int k=0;k<2;k++){Cohort*c=cohort+k;c->digest=c->policy_digest=1469598103934665603ULL;c->h2d=c->d2h=c->terminals=c->actual_ticks=0;c->cpu_s=c->gpu_s=c->pack_s=c->upload_s=c->kernel_s=c->download_s=0;}
 printf("CONFIG cohorts=2 cohort_n=%d total_envs=%d overlap=%d steps=%d repeat=%d threads=%d delta=%d policy=%d verify=%d reset_every=%d move=%d action_source=scripted warmup=%d\n",n,2*n,overlap,steps,repeat,threads,delta,policy,verify,reset_every,work_move,warmup);fflush(stdout);
 printf("PROFILE enabled=%d ranges=measured_pass_cpu_cohort_gpu_stage_cohort\n",profile);
 if(profile){if(cudaProfilerStart()!=cudaSuccess)die("profiler start");profile_active=1;nvtxRangePushA(overlap?"measured overlap":"measured serial");}
 double start=now();pthread_t thread;
 if(overlap){if(pthread_create(&thread,NULL,worker,NULL))die("worker create");for(int t=warmup;t<steps+warmup;t++)for(int k=0;k<2;k++){Cohort*c=cohort+k;pthread_mutex_lock(&lock);while(c->state!=2)pthread_cond_wait(&changed,&lock);c->state=3;pthread_mutex_unlock(&lock);gpu(c);pthread_mutex_lock(&lock);c->state=0;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&lock);}pthread_join(thread,NULL);}
 else for(int t=warmup;t<steps+warmup;t++)for(int k=0;k<2;k++){cpu(cohort+k,t);gpu(cohort+k);}
 double elapsed=now()-start;
 if(profile){nvtxRangePop();profile_active=0;if(cudaProfilerStop()!=cudaSuccess)die("profiler stop");}
 printf("RESULT elapsed_s=%.9f decisions_per_s=%.9f total_decisions=%d\n",elapsed,2.0*n*steps/elapsed,2*n*steps);
 for(int k=0;k<2;k++){Cohort*c=cohort+k;printf("COHORT id=%d digest=%016llx policy_digest=%016llx cpu_s=%.9f gpu_stage_s=%.9f scan_pack_s=%.9f upload_s=%.9f kernel_s=%.9f download_s=%.9f h2d_bytes=%llu d2h_bytes=%llu terminals=%llu nominal_ticks=%llu actual_ticks=%lld\n",k,(unsigned long long)c->digest,(unsigned long long)c->policy_digest,c->cpu_s,c->gpu_s,c->pack_s,c->upload_s,c->kernel_s,c->download_s,(unsigned long long)c->h2d,(unsigned long long)c->d2h,(unsigned long long)c->terminals,(unsigned long long)n*steps*repeat,tick_sum?(long long)c->actual_ticks:-1LL);}
  if(selected_mode==2 && verify)for(int k=0;k<2;k++) {
   if(!run){reference_digest[k]=cohort[k].digest;reference_policy[k]=cohort[k].policy_digest;}
   else if(reference_digest[k]!=cohort[k].digest || reference_policy[k]!=cohort[k].policy_digest)die("serial/overlap digest mismatch");
  }
 }
 if(selected_mode==2 && verify)puts("PARITY serial_overlap_exact=1 includes=observations_fullstate_policy");
 for(int k=0;k<2;k++){destroy_obs(cohort[k].obs);destroy_env(cohort[k].env);}
 if(nn)nn_destroy(nn);
 return 0;
}
