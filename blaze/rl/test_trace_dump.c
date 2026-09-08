#define TRACE_DUMP_NO_MAIN
#include "trace_dump.c"
#include <assert.h>
static int run(const BpParityRecord *a,size_t an,const BpParityRecord *b,size_t bn,TraceResult *r) {
 FILE *fa=tmpfile(),*fb=tmpfile(),*ta=tmpfile(),*tb=tmpfile(),*d=tmpfile(); assert(fa&&fb&&ta&&tb&&d);
 assert(fwrite(a,1,an,fa)==an && fwrite(b,1,bn,fb)==bn);rewind(fa);rewind(fb);
 int rc=trace_compare(fa,fb,ta,tb,d,r);
 fclose(fa);fclose(fb);fclose(ta);fclose(tb);fclose(d);return rc;
}
int main(void) {
 BpParityRecord a[2],b[2];TraceResult r;
 for(int i=0;i<2;++i) bp_record_init(&a[i],i);
 memcpy(b,a,sizeof a);
 assert(run(a,sizeof a,b,sizeof b,&r)==0 && r.records_a==2 && !r.differences);
 b[1].debug_bits[BP_DBG_MOTION_Y]^=1;
 assert(run(a,sizeof a,b,sizeof b,&r)==1 && r.differences==1);
 memcpy(b,a,sizeof a); b[1].digest[BP_WEATHER]^=1;
 assert(run(a,sizeof a,b,sizeof b,&r)==1 && r.differences==1);
 assert(run(a,sizeof a,a,sizeof a[0],&r)==1 && r.records_a==2 && r.records_b==1);
 assert(run(a,sizeof a,a,sizeof a-1,&r)==2);
 b[0].version=999;assert(run(a,sizeof a,b,sizeof b,&r)==2);
 b[0]=a[0];b[0].size=1;assert(run(a,sizeof a,b,sizeof b,&r)==2);
 assert(run(a,0,b,0,&r)==2);
 puts("trace_dump: exact, single-bit motion/weather, unequal count, truncation, invalid headers, empty input passed");return 0;
}
