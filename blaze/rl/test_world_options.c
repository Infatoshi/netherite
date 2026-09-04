#define _XOPEN_SOURCE 700
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include <world_recipe.h>
#include "blaze_snapshot.h"
#include "blaze_io.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static char scratch[4096];
static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr,"FAIL: %s\n",what); exit(1); }
  checks++;
}
static int remove_private(const char *path) {
  struct stat st;
  if (lstat(path,&st)) return -1;
  if (!S_ISDIR(st.st_mode)) return unlink(path);
  DIR *d = opendir(path); if (!d) return -1;
  struct dirent *e; int rc = 0;
  while ((e = readdir(d))) {
    char child[4096];
    if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
    if (snprintf(child,sizeof child,"%s/%s",path,e->d_name) >= (int)sizeof child || remove_private(child)) rc = -1;
  }
  closedir(d);
  return rmdir(path) || rc ? -1 : 0;
}
static void cleanup(void) {
  if (scratch[0] && (chdir("/") || remove_private(scratch))) {
    fprintf(stderr,"test cleanup failed: %s\n",scratch); _Exit(2);
  }
}
static int entries(const char *path) {
  DIR *d = opendir(path);
  if (!d) return errno == ENOENT ? 0 : -1;
  struct dirent *e; int count = 0;
  while ((e = readdir(d))) if (strcmp(e->d_name,".") && strcmp(e->d_name,"..")) count++;
  closedir(d); return count;
}
static uint64_t hash(const char *path) {
  FILE *f = fopen(path,"rb"); check(f != NULL,"open snapshot hash");
  unsigned char buf[8192]; size_t n;
  uint64_t h = UINT64_C(14695981039346656037);
  while ((n = fread(buf,1,sizeof buf,f)))
    for (size_t i = 0; i < n; i++) h = (h ^ buf[i]) * UINT64_C(1099511628211);
  check(!ferror(f) && fclose(f) == 0,"read complete snapshot hash"); return h;
}
static CuSnapshot *load(const char *path) {
  CuSnapshot *s = calloc(1,sizeof *s); char err[1024] = {0};
  check(s != NULL,"snapshot allocation");
  int ok = blaze_snapshot_load(path,s,err,sizeof err,0);
  if (!ok) fprintf(stderr,"load: %s\n",err);
  check(ok && s->cells != NULL,"load complete native snapshot"); return s;
}
static void release(CuSnapshot *s) { blaze_snapshot_free(s); free(s); }
static void prepare(WorldRecipe *r, const char *const *sources, int count, const WorldRecipeOptions *o) {
  char err[1024] = {0};
  int rc = world_recipe_prepare_options(r,sources,count,o,err,sizeof err);
  if (rc) fprintf(stderr,"prepare: %s\n",err);
  check(rc == 0,"world options prepare succeeds");
}
static void input_path(char *out, size_t cap, const char *root, const char *name) {
  char path[4096];
  check(snprintf(path,sizeof path,"%s/%s",root,name) < (int)sizeof path,"fixture path fits");
  check(cap >= 4096,"fixture destination fits");
  if (!realpath(path,out)) { fprintf(stderr,"fixture %s: %s\n",path,strerror(errno)); check(0,"resolve actual native fixture"); }
}
int main(int argc, char **argv) {
  char source[4096], portal[4096], nether[4096], side[4096], err[2048] = {0};
  const char *root = argc > 1 ? argv[1] : "verify/fixtures/port";
  input_path(source,sizeof source,root,"s10_t0_r64_no_liquid.bsnp");
  input_path(portal,sizeof portal,root,"s10_t0_r64_portals.bsnp");
  input_path(nether,sizeof nether,root,"s10_t0_r64_nether.bsnp");
  input_path(side,sizeof side,root,"s10_t0_r64_portals.bsnp.banks");
  const char *original[] = {source,portal,nether,side};
  uint64_t before[4]; for (int i = 0; i < 4; i++) before[i] = hash(original[i]);
  const char *tmp = getenv("TMPDIR"); if (!tmp || !tmp[0]) tmp = "/tmp";
  check(snprintf(scratch,sizeof scratch,"%s/netherite-world-options-XXXXXX",tmp) < (int)sizeof scratch,"temporary path fits");
  check(mkdtemp(scratch) != NULL,"fresh private output root");
  char absolute[4096];
  check(realpath(scratch,absolute) != NULL,"resolve private output root");
  strcpy(scratch,absolute); check(chdir(scratch) == 0,"enter private output root");
  atexit(cleanup);
  WorldRecipe *baseline = calloc(1,sizeof *baseline), *r = calloc(1,sizeof *r), *again = calloc(1,sizeof *again);
  check(baseline && r && again,"world recipe allocations");
  const char *sources[] = {source};
  WorldRecipeOptions o = {0};
  prepare(r,sources,1,&o);
  check(r->count == 1 && !strcmp(r->paths[0],source) && !r->directory[0] && r->max_cells == 0,
        "zero options preserve original path and allocate no world");
  check(entries("out/blaze/rl/worlds") == 0,"zero options create no output directory");
  check(world_recipe_prepare(baseline,sources,1,64,err,sizeof err) == 0,"legacy crop prepares baseline");
  o.world_size = 64; prepare(r,sources,1,&o);
  check(hash(r->paths[0]) == hash(baseline->paths[0]),"zero resource and jitter options preserve legacy crop bytes");
  CuSnapshot *base = load(baseline->paths[0]);
  int logs = 0;
  size_t cells = (size_t)base->head.rnx * base->head.rny * base->head.rnz;
  for (size_t i = 0; i < cells; i++) {
    int id = base->cells[i] >> 4;
    if (id == 17 || id == 162) logs++;
  }
  check(logs > 0 && base->ncoal > 0,"fixture has retained logs and coal for boundary constraint tests");
  o.min_logs = logs; o.min_coal = (int)base->ncoal; prepare(r,sources,1,&o);
  check(hash(r->paths[0]) == hash(baseline->paths[0]),"exact resource threshold passes without changing world");
  for (int which = 0; which < 2; which++) {
    o.min_logs = logs + !which; o.min_coal = (int)base->ncoal + which;
    int dirs = entries("out/blaze/rl/worlds");
    check(world_recipe_prepare_options(r,sources,1,&o,err,sizeof err) < 0 && err[0],"one above retained resource count fails");
    check(!r->directory[0] && r->count == 0 && entries("out/blaze/rl/worlds") == dirs,"failed resource constraint leaves no partial output");
  }
  CuSnapshot *empty = load(source);
  for (size_t i = 0, n = (size_t)empty->head.rnx * empty->head.rny * empty->head.rnz; i < n; i++) {
    int id = empty->cells[i] >> 4;
    if (id == 17 || id == 162 || id == 16) empty->cells[i] = 0;
  }
  empty->ncoal = 0;
  check(blaze_snapshot_write("empty.bsnp",empty,err,sizeof err),"write private fixture with resources removed");
  release(empty);
  const char *partial[] = {source,"empty.bsnp"};
  for (int which = 0; which < 2; which++) {
    o.min_logs = !which; o.min_coal = which;
    int dirs = entries("out/blaze/rl/worlds");
    check(world_recipe_prepare_options(r,partial,2,&o,err,sizeof err) < 0,"later resource failure rejects whole source batch");
    check(!r->directory[0] && r->count == 0 && entries("out/blaze/rl/worlds") == dirs,"later failure removes already written first world");
  }
  o.min_logs = o.min_coal = 0; o.yaw_jitter = 70; o.pitch_jitter = 40; o.seed = 47;
  prepare(r,sources,1,&o); prepare(again,sources,1,&o);
  check(hash(r->paths[0]) == hash(again->paths[0]),"same source and seed produce exactly identical prepared jitter bytes");
  check(symlink(source,"same-input.bsnp") == 0,"create private alias to unchanged source");
  const char *reordered[] = {"same-input.bsnp",source};
  prepare(again,reordered,2,&o);
  check(hash(r->paths[0]) == hash(again->paths[0]) && hash(r->paths[0]) == hash(again->paths[1]),
        "jitter depends on source content and seed rather than path or batch order");
  CuSnapshot *jitter = load(r->paths[0]);
  check(fabsf(jitter->head.yaw-base->head.yaw) <= o.yaw_jitter &&
        fabsf(jitter->head.pitch-base->head.pitch) <= o.pitch_jitter &&
        jitter->head.pitch >= -90 && jitter->head.pitch <= 90,"jitter stays within requested angles and legal pitch");
  check(jitter->head.yaw != base->head.yaw || jitter->head.pitch != base->head.pitch,"nonzero jitter materially changes camera direction");
  jitter->head.yaw = base->head.yaw; jitter->head.pitch = base->head.pitch;
  check(blaze_snapshot_write("normalized.bsnp",jitter,err,sizeof err),"write comparison with restored angles");
  check(hash("normalized.bsnp") == hash(baseline->paths[0]),"jitter changes only camera angles in serialized snapshot");
  release(jitter);
  uint64_t jitter_hash = hash(r->paths[0]); o.seed++;
  prepare(again,sources,1,&o);
  check(hash(again->paths[0]) != jitter_hash,"different seed changes materialized jitter");
  const char *portals[] = {portal};
  o.min_logs = 1; o.min_coal = 1;
  prepare(r,portals,1,&o);
  char bank[1024], end[1024]; const char *prepared[] = {r->paths[0]};
  check(cu_resolve_banks(prepared,1,"","","","",bank,end,err,sizeof err) == 0 && bank[0],"prepared portal resolves private dimension bank");
  CuSnapshot *original_bank = load(nether), *prepared_bank = load(bank);
  check(original_bank->head.yaw == prepared_bank->head.yaw && original_bank->head.pitch == prepared_bank->head.pitch,
        "dimension banks do not receive starting-world camera jitter");
  check(prepared_bank->ncoal == 0,"resource constraints do not reject resource-free dimension bank");
  release(original_bank); release(prepared_bank);
  const float bad[] = {-1,NAN,INFINITY,181};
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
    o.yaw_jitter = bad[i]; int dirs = entries("out/blaze/rl/worlds");
    check(world_recipe_prepare_options(again,sources,1,&o,err,sizeof err) < 0,"invalid jitter rejected");
    check(entries("out/blaze/rl/worlds") == dirs,"invalid options do not create output");
  }
  for (int i = 0; i < 4; i++) check(hash(original[i]) == before[i],"original world or bank input unchanged");
  release(base); free(baseline); free(r); free(again);
  cleanup(); scratch[0] = 0;
  printf("test_world_options: %d checks PASS\n",checks);
  return 0;
}
