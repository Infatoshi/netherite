/* Native, unmasked decoding/comparison of the existing PARY wire format. */
#include "port_parity.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const field_names[BP_NDEBUG] = {
 [BP_DBG_PLAYER_X]="player_x",[BP_DBG_PLAYER_Y]="player_y",[BP_DBG_PLAYER_Z]="player_z",
 [BP_DBG_MOTION_X]="motion_x",[BP_DBG_MOTION_Y]="motion_y",[BP_DBG_MOTION_Z]="motion_z",
 [BP_DBG_YAW]="yaw",[BP_DBG_PITCH]="pitch",[BP_DBG_ON_GROUND]="on_ground",
 [BP_DBG_FALL_DISTANCE]="fall_distance",[BP_DBG_SPRINTING]="sprinting",
 [BP_DBG_SPRINT_TIMER]="sprint_timer",[BP_DBG_HEALTH]="health",[BP_DBG_FOOD]="food",
 [BP_DBG_EXHAUSTION]="exhaustion",[BP_DBG_DIG_PROGRESS]="dig_progress",
 [BP_DBG_DIG_HX]="dig_hx",[BP_DBG_DIG_HY]="dig_hy",[BP_DBG_DIG_HZ]="dig_hz",
 [BP_DBG_DIG_HITTING]="dig_hitting",[BP_DBG_DIG_DELAY]="dig_delay",
 [BP_DBG_ATK_PREV]="attack_previous",[BP_DBG_LEFT_CLICK_COUNTER]="left_click_counter",
 [BP_DBG_RC_DELAY]="right_click_delay",[BP_DBG_USE_PREV]="use_previous",
 [BP_DBG_HURT_VEL_RESET]="hurt_velocity_reset",[BP_DBG_SERVER_MOTION_X]="server_motion_x",
 [BP_DBG_SERVER_MOTION_Z]="server_motion_z",[BP_DBG_CONTAINER]="container_and_opens",
 [BP_DBG_CONTAINER_WX]="container_wx_and_craft_attempts",
 [BP_DBG_CONTAINER_WY]="container_wy_and_cursor_item",
 [BP_DBG_CONTAINER_WZ]="container_wz_and_cursor_count_meta"
};
typedef struct { uint64_t records_a,records_b,differences; } TraceResult;

/* 1 record, 0 clean EOF, -1 malformed/truncated/input error. */
static int read_record(FILE *f,BpParityRecord *p) {
 size_t n=fread(p,1,sizeof *p,f);
 if(!n) return ferror(f)?-1:0;
 if(n!=sizeof *p || p->magic!=BP_PARITY_MAGIC || p->version!=BP_PARITY_VERSION ||
    p->size!=sizeof *p || p->nsubsystems!=BP_NSUBSYSTEMS) return -1;
 return 1;
}
static void dump_header(FILE *f) {
 fputs("record\ttick\timplemented_mask\tmeasured_mask\tactive_mask",f);
 for(unsigned i=0;i<BP_NDEBUG;++i) fprintf(f,"\t%s_bits\t%s_value",field_names[i],field_names[i]);
 for(unsigned i=0;i<BP_NSUBSYSTEMS;++i) fprintf(f,"\t%s_digest\t%s_evidence",bp_subsystem_names[i],bp_subsystem_names[i]);
 fputc('\n',f);
}
static void dump_record(FILE *f,uint64_t row,const BpParityRecord *p) {
 fprintf(f,"%" PRIu64 "\t%" PRId64 "\t%016" PRIx64 "\t%016" PRIx64 "\t%016" PRIx64,row,p->tick,p->implemented_mask,p->measured_mask,p->active_mask);
 for(unsigned i=0;i<BP_NDEBUG;++i) {
  uint64_t bits=p->debug_bits[i]; fprintf(f,"\t%016" PRIx64 "\t",bits);
  if(i<=BP_DBG_MOTION_Z || i==BP_DBG_SERVER_MOTION_X || i==BP_DBG_SERVER_MOTION_Z) {
   double v; memcpy(&v,&bits,sizeof v); fprintf(f,"%.17g",v);
  } else if(i==BP_DBG_YAW || i==BP_DBG_PITCH || i==BP_DBG_FALL_DISTANCE || i==BP_DBG_HEALTH || i==BP_DBG_EXHAUSTION || i==BP_DBG_DIG_PROGRESS) {
   uint32_t lo=(uint32_t)bits; float v; memcpy(&v,&lo,sizeof v); fprintf(f,"%.9g",(double)v);
  } else if(i>=BP_DBG_CONTAINER) fprintf(f,"%" PRId32 ",%" PRId32,(int32_t)bits,(int32_t)(bits>>32));
  else fprintf(f,"%" PRId32,(int32_t)bits);
 }
 for(unsigned i=0;i<BP_NSUBSYSTEMS;++i) fprintf(f,"\t%016" PRIx64 "\t%" PRIu32,p->digest[i],p->evidence[i]);
 fputc('\n',f);
}
static void difference(FILE *f,TraceResult *r,uint64_t row,const char *kind,const char *name,uint64_t a,uint64_t b) {
 if(a==b) return;
 ++r->differences;
 fprintf(f,"%" PRIu64 "\t%s\t%s\t%016" PRIx64 "\t%016" PRIx64 "\n",row,kind,name,a,b);
}
/* Compare every field of every record, including all subsystem digests.
 * Decoded floats are presentation only: equality always compares raw bits. */
static int trace_compare(FILE *a,FILE *b,FILE *ta,FILE *tb,FILE *diff,TraceResult *r) {
 memset(r,0,sizeof *r); dump_header(ta); dump_header(tb);
 fputs("record\tkind\tfield\ta_bits\tb_bits\n",diff);
 for(uint64_t row=0;;++row) {
  BpParityRecord x,y; int ax=read_record(a,&x),by=read_record(b,&y);
  if(ax<0||by<0) {fprintf(stderr,"trace_dump: malformed/truncated PARY at record %" PRIu64 " (%s)\n",row,ax<0?"a":"b");return 2;}
  if(!ax&&!by) break;
  if(ax) {++r->records_a;dump_record(ta,row,&x);}
  if(by) {++r->records_b;dump_record(tb,row,&y);}
  if(!ax||!by) {difference(diff,r,row,"coverage","record_present",ax,by);continue;}
  difference(diff,r,row,"header","tick",(uint64_t)x.tick,(uint64_t)y.tick);
  difference(diff,r,row,"coverage","implemented_mask",x.implemented_mask,y.implemented_mask);
  difference(diff,r,row,"coverage","measured_mask",x.measured_mask,y.measured_mask);
  difference(diff,r,row,"coverage","active_mask",x.active_mask,y.active_mask);
  for(unsigned i=0;i<BP_NDEBUG;++i) difference(diff,r,row,"debug",field_names[i],x.debug_bits[i],y.debug_bits[i]);
  for(unsigned i=0;i<BP_NSUBSYSTEMS;++i) {
   difference(diff,r,row,"digest",bp_subsystem_names[i],x.digest[i],y.digest[i]);
   difference(diff,r,row,"evidence",bp_subsystem_names[i],x.evidence[i],y.evidence[i]);
  }
 }
 if(!r->records_a||!r->records_b) {fputs("trace_dump: empty input is not evidence\n",stderr);return 2;}
 if(ferror(ta)||ferror(tb)||ferror(diff)||fflush(ta)||fflush(tb)||fflush(diff)) return 2;
 return r->differences?1:0;
}
#ifndef TRACE_DUMP_NO_MAIN
int main(int argc,char **argv) {
 const char *ap=NULL,*bp=NULL,*prefix=NULL;
 for(int i=1;i<argc;++i) {
  if(i+1<argc&&!strcmp(argv[i],"--a")) ap=argv[++i];
  else if(i+1<argc&&!strcmp(argv[i],"--b")) bp=argv[++i];
  else if(i+1<argc&&!strcmp(argv[i],"--out")) prefix=argv[++i];
  else {fputs("usage: trace_dump --a A.pary --b B.pary --out NEW_PREFIX\n",stderr);return 2;}
 }
 if(!ap||!bp||!prefix) {fputs("trace_dump: --a, --b and --out required\n",stderr);return 2;}
 FILE *a=fopen(ap,"rb"),*b=fopen(bp,"rb");
 if(!a||!b) {perror("trace_dump input");if(a)fclose(a);if(b)fclose(b);return 2;}
 FILE *out[3]={0}; const char *suffix[]={".a.tsv",".b.tsv",".diff.tsv"}; int rc=2; TraceResult r={0};
 for(int i=0;i<3;++i) {
  char path[4096]; int n=snprintf(path,sizeof path,"%s%s",prefix,suffix[i]);
  if(n<0||n>=(int)sizeof path || !(out[i]=fopen(path,"wx"))) {perror("trace_dump output (must be new)");goto done;}
 }
 rc=trace_compare(a,b,out[0],out[1],out[2],&r);
 done:
 fclose(a);fclose(b);
 for(int i=0;i<3;++i) if(out[i]&&fclose(out[i])) rc=2;
 printf("records_a=%" PRIu64 " records_b=%" PRIu64 " differences=%" PRIu64 " rc=%d\n",r.records_a,r.records_b,r.differences,rc);
 return rc;
}
#endif
