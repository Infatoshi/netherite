#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "eval_magma.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int calls, differs, fail_callback;
static int observe(void *ctx,const double *a,const EvalMagmaObs *o) {
  (void)ctx;
  ++calls;
  assert(o->tick==calls);
  if(calls>1) assert(a[2]==0 && a[3]==0 && a[10]==-1 && a[11]==0 && a[12]==0);
  EvalMagmaObs bad=*o; char why[80]; bad.x+=1;
  assert(eval_magma_cmp_gated(o,&bad,why,sizeof why)!=0);
  ++differs;
  return fail_callback ? -1 : 0;
}
/* Protocol fixture only: no simulation or synthetic pixel evidence. */
static int protocol_fixture(int argc,char **argv) {
  for(int i=1;i+1<argc;++i) if(!strcmp(argv[i],"--frames-out")) assert(mkdir(argv[i+1],0700)==0);
  EvalMagmaObs o={0}; o.magic=EM_MAGIC;
  assert(fwrite(&o,sizeof o,1,stdout)==1); fflush(stdout);
  char line[2048];
  while(fgets(line,sizeof line,stdin)) { ++o.tick; o.x+=1; assert(fwrite(&o,sizeof o,1,stdout)==1); fflush(stdout); }
  return 0;
}
int main(int argc,char **argv) {
  if(argc>1 && !strcmp(argv[1],"--rl-bin")) return protocol_fixture(argc,argv);
  char dir[]="/tmp/netherite-eval-trace-XXXXXX",err[512],p[1024];
  assert(mkdtemp(dir));
  snprintf(p,sizeof p,"%s/fixture.bsnp",dir); FILE *snap=fopen(p,"w"); assert(snap); fclose(snap);
  EvalMagma *m=eval_magma_open_trace(argv[0],p,10,dir,err,sizeof err);
  if(!m) { fprintf(stderr,"%s\n",err); return 1; }
  eval_magma_set_tick_callback(m,observe,NULL);
  double a[13]={1,0,2,3,0,0,0,1,0,-1,4,1,1};
  assert(eval_magma_step(m,a,3)==0); assert(calls==3 && differs==3);
  eval_magma_close(m);
  struct stat st;
  snprintf(p,sizeof p,"%s/magma.bolr",dir); assert(!stat(p,&st) && st.st_size==4*(off_t)sizeof(EvalMagmaObs));
  snprintf(p,sizeof p,"%s/actions.jsonl",dir); FILE *f=fopen(p,"r"); assert(f);
  int n=0; char line[4096]; while(fgets(line,sizeof line,f)) ++n; fclose(f); assert(n==3);
  snprintf(p,sizeof p,"%s/magma_state.jsonl",dir); f=fopen(p,"r"); assert(f);n=0;
  while(fgets(line,sizeof line,f)) { assert(strstr(line,"\"velocity\":null")); ++n; } fclose(f); assert(n==4);
  snprintf(p,sizeof p,"%s/frames",dir); assert(!stat(p,&st)&&S_ISDIR(st.st_mode)); assert(!rmdir(p));
  const char *files[]={"magma.bolr","actions.jsonl","magma_state.jsonl","magma.pary","magma.stderr","fixture.bsnp"};
  for(unsigned i=0;i<sizeof files/sizeof files[0];++i) {snprintf(p,sizeof p,"%s/%s",dir,files[i]);assert(!unlink(p));}
  assert(!rmdir(dir));
  calls=differs=0; fail_callback=1;
  m=eval_magma_open(argv[0],argv[0],10,err,sizeof err); assert(m);
  eval_magma_set_tick_callback(m,observe,NULL);
  assert(eval_magma_step(m,a,3)==-1 && calls==1); eval_magma_close(m);
  puts("eval trace protocol: 3 actions, 4 raw/state records, 3 callbacks, injected mismatches detected; frame path forwarded");
  return 0;
}
