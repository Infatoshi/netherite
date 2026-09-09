/* Architecture measurement driver. Fixed actions, identical native policy work,
 * explicit reset/terminal counts. Record/compare is excluded from timed work.
 * Binary receipts are versioned local-machine artifacts, not portable tapes. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <omp.h>
#include <sys/resource.h>
#include <cuda_profiler_api.h>
#include <cuda_runtime_api.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../blaze/env/blaze_abi.h"
#include "../../blaze/rl/env_cuda_stage.h"
#include "../../blaze/rl/obs_pack.h"
#include "../../blaze/rl/rl_ckpt.h"
#include "../../blaze/nn/nn.h"
#include "env_cuda_obs.h"

static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void fail(const char*s){fprintf(stderr,"FATAL %s\n",s);exit(2);}
static void *alloc(size_t n,size_t z){void*p=calloc(n,z);if(!p)fail("allocation");return p;}
static void *sym(void*l,const char*s){void*p=dlsym(l,s);if(!p){fprintf(stderr,"missing %s: %s\n",s,dlerror());exit(2);}return p;}
static uint64_t digest(const void*p,size_t n){const unsigned char*b=p;uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++)h=(h^b[i])*1099511628211ULL;return h;}
static uint64_t whole=1469598103934665603ULL;
static FILE *record,*reference;
static int ctl_fd=-1,ack_fd=-1;
static void perf_command(const char*s){if(ctl_fd<0)return;size_t z=strlen(s);if(write(ctl_fd,s,z)!=(ssize_t)z)fail("perf control write");char c;do{if(read(ack_fd,&c,1)!=1)fail("perf ack read");}while(c!='\n');}

static int tick_index;
static void field(const char*name,const void*p,size_t n){
 const unsigned char*bytes=p;for(size_t i=0;i<n;i++)whole=(whole^bytes[i])*1099511628211ULL;
 if(record&&fwrite(p,1,n,record)!=n)fail("record write");
 if(reference){unsigned char*b=alloc(n,1);if(fread(b,1,n,reference)!=n)fail("truncated reference");if(memcmp(p,b,n)){size_t i=0;while(i<n&&((unsigned char*)p)[i]==b[i])i++;fprintf(stderr,"MISMATCH step=%d field=%s byte=%zu actual=%u reference=%u\n",tick_index,name,i,((unsigned char*)p)[i],b[i]);exit(3);}free(b);}
}
#define FN(ret,name,args) ret(*name)args=(ret(*)args)sym(lib,"blaze_" #name)
int main(int argc,char**argv){
 int n=8,steps=32,warm=4,repeat=4,threads=8,split=0,policy=0,reset_every=256,op=0,mobs=0,delta=0,profile=0,busy_every=0,grain=0,det_ai=0;
 const char*mode="cpu",*fixture="verify/fixtures/port/s10_t0_r64_no_liquid.bsnp",*work="idle",*libpath=NULL,*ckpt=NULL,*recpath=NULL,*refpath=NULL,*replaypath=NULL,*perfctl=NULL,*perfack=NULL,*cubin=NULL,*fine_dir=NULL,*rows_path=NULL;
 for(int i=1;i<argc;i++){if(i+1>=argc)fail("arguments require values");const char*k=argv[i],*v=argv[++i];
  if(!strcmp(k,"--mode"))mode=v;else if(!strcmp(k,"--fixture"))fixture=v;else if(!strcmp(k,"--work"))work=v;else if(!strcmp(k,"--lib"))libpath=v;else if(!strcmp(k,"--checkpoint"))ckpt=v;else if(!strcmp(k,"--record"))recpath=v;else if(!strcmp(k,"--reference"))refpath=v;else if(!strcmp(k,"--replay"))replaypath=v;else if(!strcmp(k,"--perf-control"))perfctl=v;else if(!strcmp(k,"--perf-ack"))perfack=v;else if(!strcmp(k,"--cubin"))cubin=v;else if(!strcmp(k,"--fine"))fine_dir=v;else if(!strcmp(k,"--action-rows"))rows_path=v;
  else if(!strcmp(k,"--n"))n=atoi(v);else if(!strcmp(k,"--steps"))steps=atoi(v);else if(!strcmp(k,"--warmup"))warm=atoi(v);else if(!strcmp(k,"--repeat"))repeat=atoi(v);else if(!strcmp(k,"--threads"))threads=atoi(v);else if(!strcmp(k,"--split"))split=atoi(v);else if(!strcmp(k,"--policy"))policy=atoi(v);else if(!strcmp(k,"--reset-every"))reset_every=atoi(v);else if(!strcmp(k,"--op"))op=atoi(v);else if(!strcmp(k,"--mobs"))mobs=atoi(v);else if(!strcmp(k,"--det-ai"))det_ai=atoi(v);else if(!strcmp(k,"--delta"))delta=atoi(v);else if(!strcmp(k,"--profile"))profile=atoi(v);else if(!strcmp(k,"--busy-every"))busy_every=atoi(v);else if(!strcmp(k,"--grain"))grain=atoi(v);else fail("unknown option");
 }
 int gpu=!strcmp(mode,"cuda"),hybrid=!strcmp(mode,"hybrid");
 if((!gpu&&!hybrid&&strcmp(mode,"cpu"))||n<1||n>4096||steps<1||warm<0||repeat<1||threads<1||reset_every<1)fail("invalid config");
 if(strcmp(work,"idle")&&strcmp(work,"move")&&strcmp(work,"edit")&&strcmp(work,"policy")&&strcmp(work,"boundary")&&strcmp(work,"mine")&&strcmp(work,"place"))fail("invalid workload");
 if(!strcmp(work,"policy")&&!policy)fail("policy workload requires --policy 1");
 omp_set_num_threads(threads);
 if(!libpath)libpath=gpu?"out/blaze/env/blaze_cuda.so":"out/blaze/env/blaze_cpu.so";
 void*lib=dlopen(libpath,RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);if(!lib)fail(dlerror());
 FN(void*,create,(int,int,const BlazeCreateOpts*));FN(void,destroy,(void*));FN(int,load_snapshots,(void*,const char*const*,int,char*,int));FN(int,assign,(void*,const int*));FN(int,reset,(void*,const unsigned char*));FN(int,set_success_item,(void*,int));
 BlazeStepFullFn step=(BlazeStepFullFn)sym(lib,hybrid?"blaze_step_full_no_camera":"blaze_step_full");
 FN(int,parity_size,(void));FN(int,parity_state,(void*,int,void*));
 BlazeCreateOpts opts;blaze_create_opts_default(&opts);opts.op_trace=op;
 double start=now();void*env=create(0,n,&opts);if(!env)fail("env create");
 if(gpu&&split){int(*sel)(void*,int)=sym(lib,"blaze_measure_set_split");if(sel(env,split))fail("split select");}
 char err[1024]={0};char*fixture_copy=strdup(fixture),*save=NULL;const char*paths[16];int path_count=0;if(!fixture_copy)fail("fixture allocation");for(char*p=strtok_r(fixture_copy,",",&save);p;p=strtok_r(NULL,",",&save)){if(path_count==16)fail("too many fixtures");paths[path_count++]=p;}if(!path_count||load_snapshots(env,paths,path_count,err,sizeof err)!=path_count)fail(err);
 int*assignment=alloc(n,sizeof(int));unsigned char*mask=alloc(n,1);memset(mask,1,n);
 if(mobs){int(*setmob)(void*,int)=sym(lib,"blaze_set_mobs_enabled");if(setmob(env,1))fail("enable generic AI");}
 if(det_ai){if(gpu)fail("deterministic task/path AI is not implemented by CUDA");int(*setrng)(void*,int)=sym(lib,"blaze_set_det_entity_rng");if(setrng(env,1))fail("enable deterministic AI");}
 for(int i=0;i<n;i++)assignment[i]=(busy_every>0&&path_count==2)?((i%busy_every)==0?1:0):i%path_count;
 if(assign(env,assignment)||set_success_item(env,0)||reset(env,mask))fail("initial reset");
 void*dispatch=NULL;int(*driver_release)(void)=NULL;
 if(grain){if(!gpu||!split||!cubin)fail("grain requires CUDA split and cubin");dispatch=dlopen("out/verify/architecture/driver_dispatch.so",RTLD_NOW|RTLD_LOCAL);if(!dispatch)fail(dlerror());int(*configure)(void*,const char*,int)=sym(dispatch,"blaze_driver_configure");driver_release=sym(dispatch,"blaze_driver_release");if(configure(env,cubin,grain))fail("driver configure");step=(BlazeStepFullFn)sym(dispatch,"blaze_driver_step_full");if(fine_dir){char pre[2048],pure[2048],after[2048];snprintf(pre,sizeof pre,"%s/pre_actions.cubin",fine_dir);snprintf(pure,sizeof pure,"%s/pure_player.cubin",fine_dir);snprintf(after,sizeof after,"%s/after_player.cubin",fine_dir);int(*configure_fine)(const char*,const char*,const char*)=sym(dispatch,"blaze_driver_configure_fine");if(configure_fine(pre,pure,after))fail("fine player configure");}}
 if(fine_dir&&!grain)fail("fine requires driver grain");

 size_t pixels=(size_t)n*ENV_NPIX;
 unsigned short*cam=alloc(pixels,2);unsigned char*depth=alloc(pixels,1),*edge=alloc(pixels,1),*done=alloc(n,1);
 float*scal=alloc((size_t)n*ENV_SCAL,4),*rew=alloc(n,4),*pose=alloc((size_t)n*ENV_POSE,4);int*status=alloc((size_t)n*ENV_STATUS,sizeof(int));double*actions=alloc((size_t)n*ENV_ACT,8);
 EnvCudaStage stage={0};if(gpu&&env_cuda_stage_create(&stage,n,0))fail("GPU staging");
 void*bridge=NULL;EnvCudaObs*obs=NULL;
 EnvCudaObs*(*obs_create)(int,int,size_t,HybridCamInputsFn,HybridCamFreshFn)=NULL;
 int(*render)(EnvCudaObs*,void*,int,unsigned short*,unsigned char*,unsigned char*,EnvCudaObsStats*)=NULL;
 void(*obs_destroy)(EnvCudaObs*)=NULL;int(*commit)(void*,const unsigned short*,const unsigned char*,const unsigned char*)=NULL;
 int(*obs_reset)(EnvCudaObs*,const unsigned char*)=NULL;
 if(hybrid){bridge=dlopen("out/verify/architecture/env_cuda_obs.so",RTLD_NOW|RTLD_LOCAL);if(!bridge)fail(dlerror());obs_create=sym(bridge,"env_cuda_obs_create");render=sym(bridge,"env_cuda_obs_render");obs_destroy=sym(bridge,"env_cuda_obs_destroy");commit=sym(lib,"blaze_obs_cam_commit");HybridCamInputsFn inputs=sym(lib,"blaze_obs_cam_inputs");HybridCamFreshFn fresh=sym(lib,"blaze_obs_cam_fresh");size_t maxcells=0;for(int i=0;i<n;i++){int x,y,z;if(inputs(env,i,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,&x,&y,&z,NULL))fail("camera dimensions");size_t c=(size_t)x*y*z;if(c>maxcells)maxcells=c;}obs=obs_create(0,n,maxcells,inputs,fresh);if(!obs)fail("bridge create");}
 uint8_t*planes=alloc((size_t)n*ENV_N_CH*ENV_NPIX,1),*prior=alloc((size_t)n*ENV_N_PLANES*ENV_NPIX,1),*scratch=alloc((size_t)n*ENV_N_PLANES*ENV_NPIX,1),*have=alloc(n,1);
 float*scalars=alloc((size_t)n*POL_SCAL,4),*logits=alloc((size_t)n*34,4),*values=alloc(n,4),*logp=alloc(n,4);int32_t*acts=alloc((size_t)n*POL_HEADS,4);int*epdec=alloc(n,sizeof(int));
 Nn*nn=NULL;if(policy){NnCreate d={NN_BACKEND_CUDA,0,n,nn_config_default(),NN_PREC_FAST};nn=nn_create(&d);if(!nn)fail(nn_last_error());if(ckpt&&rl_ckpt_load(nn,ckpt))fail(nn_last_error());if(nn_prepare_n(nn,n)||nn_seal(nn))fail(nn_last_error());}
 size_t gpu_free_init=0,gpu_total=0;if(gpu||hybrid||policy){if(cudaMemGetInfo(&gpu_free_init,&gpu_total)!=cudaSuccess)fail("GPU memory query");}
 int psize=parity_size();if(psize<1||psize>1000000)fail("parity size");void*parity=alloc(n,psize);
 if(recpath){record=fopen(recpath,"wb");if(!record)fail("record open");}if(refpath){reference=fopen(refpath,"rb");if(!reference)fail("reference open");}
 uint32_t header[8]={0x41524348,2,(uint32_t)n,(uint32_t)steps,(uint32_t)warm,(uint32_t)repeat,(uint32_t)reset_every,(uint32_t)psize};tick_index=-1;field("header",header,sizeof header);
 double*replay_actions=NULL;
 if(replaypath){FILE*f=fopen(replaypath,"rb");if(!f)fail("replay open");uint32_t h[8];if(fread(h,1,sizeof h,f)!=sizeof h||memcmp(h,header,sizeof h))fail("replay header");size_t action_bytes=(size_t)n*ENV_ACT*8, row_bytes=action_bytes+pixels*4+(size_t)n*(ENV_SCAL*4+4+1+ENV_POSE*4+ENV_STATUS*sizeof(int)+psize);if(fseek(f,0,SEEK_END)||ftell(f)!=(long)(sizeof h+(size_t)(steps+warm)*row_bytes))fail("replay length");replay_actions=alloc((size_t)(steps+warm)*n*ENV_ACT,8);for(int t=0;t<steps+warm;t++){if(fseek(f,(long)(sizeof h+(size_t)t*row_bytes),SEEK_SET)||fread(replay_actions+(size_t)t*n*ENV_ACT,1,action_bytes,f)!=action_bytes)fail("replay actions");}fclose(f);}

 if(rows_path){if(replay_actions)fail("choose one action input");FILE*f=fopen(rows_path,"rb");uint32_t h[4];if(!f||fread(h,1,sizeof h,f)!=sizeof h||h[0]!=0x41524f57||h[1]!=1||h[2]!=(uint32_t)(steps+warm)||h[3]!=(uint32_t)n)fail("action rows header");size_t count=(size_t)(steps+warm)*n*ENV_ACT;replay_actions=alloc(count,8);if(fread(replay_actions,8,count,f)!=count||fgetc(f)!=EOF)fail("action rows length");fclose(f);}
 printf("CONFIG mode=%s split=%d n=%d steps=%d warmup=%d repeat=%d threads=%d policy=%d workload=%s reset_every=%d init_s=%.6f fixture=%s lib=%s\n",mode,split,n,steps,warm,repeat,threads,policy,work,reset_every,now()-start,fixture,libpath);
 if(hybrid){obs_reset=sym(bridge,"env_cuda_obs_reset");int(*set_delta)(EnvCudaObs*,int)=sym(bridge,"env_cuda_obs_set_delta");if(set_delta(obs,delta))fail("delta select");}
 printf("DISPATCH scalar_grain=%d cubin=%s fine=%s\n",grain,cubin?cubin:"original",fine_dir?fine_dir:"none");
 printf("BRIDGE delta=%d transfer_columns=hybrid_poststep_only policy_uses_host_ABI=1\n",delta);
 printf("DETAIL mobs=%d det_entity_rng=%d natural_spawn=0 op_trace=%d timing_excludes_reset=1 parity_capture=%d\n",mobs,det_ai,op,record!=NULL||reference!=NULL);
 printf("step,env_ms,render_ms,pack_ms,policy_ms,reset_ms,total_ms,terminal_lanes,hybrid_poststep_h2d_bytes,hybrid_poststep_d2h_bytes\n");fflush(stdout);
 if(perfctl||perfack){if(!perfctl||!perfack)fail("both perf fifo paths required");ctl_fd=open(perfctl,O_WRONLY);ack_fd=open(perfack,O_RDONLY);if(ctl_fd<0||ack_fd<0)fail("perf fifo open");}
 double sum=0;uint64_t terminals=0,subticks=0;
 for(int t=0;t<steps+warm;t++){
  if(profile&&t==warm&&cudaProfilerStart()!=cudaSuccess)fail("profiler start");
  double t0=now(),resetms=0,render_ms=0,packms=0,nnms=0;EnvCudaObsStats stats={0};
  int any=0;for(int i=0;i<n;i++){mask[i]=(t==0||t==warm||(t>warm&&(t-warm)%reset_every==0)||done[i]);any|=mask[i];}
  if(any){double a=now();if(reset(env,mask))fail("reset");for(int i=0;i<n;i++)if(mask[i]){have[i]=0;epdec[i]=0;}if(hybrid&&obs_reset(obs,mask))fail("reset observation cache");resetms=(now()-a)*1000;}
  if(t==warm)perf_command("enable\n");
  memset(actions,0,(size_t)n*ENV_ACT*8);
  for(int i=0;i<n;i++){double*a=actions+(size_t)i*ENV_ACT;a[9]=-1;a[10]=-1;if(strcmp(work,"idle")){a[0]=((t/8+i)%3)==0?0:1;a[2]=(t%8==0)?((i%2)?15:-15):0;a[4]=(t%9==0);if(!strcmp(work,"edit")){a[7]=1;a[8]=(t%11==0);}}}
  if(!strcmp(work,"boundary"))for(int i=0;i<n;i++){double*a=actions+(size_t)i*ENV_ACT;memset(a,0,ENV_ACT*8);a[0]=1;a[9]=-1;a[10]=-1;}
  if(!strcmp(work,"mine")||!strcmp(work,"place"))for(int i=0;i<n;i++){double*a=actions+(size_t)i*ENV_ACT;memset(a,0,ENV_ACT*8);a[9]=!strcmp(work,"place")?0:-1;a[10]=-1;a[7]=!strcmp(work,"mine");a[8]=!strcmp(work,"place")&&(t%2==0);}
  if(!strcmp(work,"policy")&&t>0)acts_to_rows(acts,n,actions);
  if(!strcmp(work,"policy"))for(int i=0;i<n;i++)if(mask[i]){double*a=actions+(size_t)i*ENV_ACT;memset(a,0,ENV_ACT*8);a[9]=-1;a[10]=-1;}
  if(replay_actions)memcpy(actions,replay_actions+(size_t)t*n*ENV_ACT,(size_t)n*ENV_ACT*8);
  double a=now();int rc=gpu?env_cuda_stage_step_full(&stage,step,env,actions,repeat,cam,depth,edge,scal,rew,done,pose,status):step(env,actions,repeat,cam,depth,edge,scal,rew,done,pose,status);if(rc)fail("step_full");double envms=(now()-a)*1000;
  if(hybrid){a=now();if(render(obs,env,0,cam,depth,edge,&stats)||commit(env,cam,depth,edge))fail("hybrid render");render_ms=(now()-a)*1000;}
  a=now();pack_obs(cam,depth,edge,scal,pose,status,epdec,1500,have,prior,n,planes,scalars,scratch);memcpy(prior,scratch,(size_t)n*ENV_N_PLANES*ENV_NPIX);memset(have,1,n);for(int i=0;i<n;i++)epdec[i]++;packms=(now()-a)*1000;
  if(policy){a=now();if(nn_forward(nn,planes,scalars,n,logits,values)||nn_sample(nn,logits,n,NN_SAMPLE_GUMBEL,acts,logp,NULL))fail(nn_last_error());nnms=(now()-a)*1000;}
  double total=(now()-t0)*1000-resetms;int term=0;for(int i=0;i<n;i++)term+=done[i]!=0;
  if(t>=warm){sum+=total;terminals+=term;subticks+=(uint64_t)n*repeat;printf("%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%zu,%zu\n",t-warm,envms,render_ms,packms,nnms,resetms,total,term,stats.h2d_bytes,stats.d2h_bytes);fflush(stdout);}
  if(record||reference){tick_index=t;field("actions",actions,(size_t)n*ENV_ACT*8);field("cam",cam,pixels*2);field("depth",depth,pixels);field("edge",edge,pixels);field("scalars",scal,(size_t)n*ENV_SCAL*4);field("reward",rew,(size_t)n*4);field("done",done,n);field("pose",pose,(size_t)n*ENV_POSE*4);field("status",status,(size_t)n*ENV_STATUS*sizeof(int));for(int i=0;i<n;i++)if(parity_state(env,i,(char*)parity+(size_t)i*psize))fail("parity read");field("parity",parity,(size_t)n*psize);}
 }
 perf_command("disable\n");
 if(ctl_fd>=0){close(ctl_fd);close(ack_fd);}
 if(profile&&cudaProfilerStop()!=cudaSuccess)fail("profiler stop");
 if(reference&&fgetc(reference)!=EOF)fail("trailing reference data");
 size_t gpu_free_end=0;if(gpu||hybrid||policy){if(cudaMemGetInfo(&gpu_free_end,&gpu_total)!=cudaSuccess)fail("GPU memory query");printf("MEMORY gpu_used_init_bytes=%zu gpu_used_end_bytes=%zu\n",gpu_total-gpu_free_init,gpu_total-gpu_free_end);}
 struct rusage ru;getrusage(RUSAGE_SELF,&ru);
 printf("RESULT total_ms=%.6f nominal_subticks=%llu terminal_lanes=%llu decisions_per_s=%.6f nominal_ticks_per_s=%.6f maxrss_kib=%ld digest=%016llx compared=%d digest_scope=%s\n",sum,(unsigned long long)subticks,(unsigned long long)terminals,(double)n*steps*1000/sum,(double)subticks*1000/sum,ru.ru_maxrss,(unsigned long long)whole,reference!=NULL,(record||reference)?"trajectory":"header_only");
 if(op){int(*op_count)(void)=sym(lib,"blaze_op_count"),(*op_trace)(void*,unsigned long long*)=sym(lib,"blaze_op_trace");int k=op_count();unsigned long long*counts=alloc((size_t)n*k,sizeof(*counts));if(op_trace(env,counts))fail("op read");for(int j=0;j<k;j++){unsigned long long s=0;for(int i=0;i<n;i++)s+=counts[(size_t)i*k+j];printf("OP index=%d count=%llu\n",j,s);}free(counts);}
 if(record&&fclose(record))fail("record close");
 if(reference)fclose(reference);
 if(nn)nn_destroy(nn);
 if(obs)obs_destroy(obs);
 if(gpu)env_cuda_stage_destroy(&stage);
 if(driver_release&&driver_release())fail("driver release");
 destroy(env);return 0;
}
