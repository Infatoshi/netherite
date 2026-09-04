#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#include <train_recipe.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks;
static char scratch[4096];
static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr,"FAIL: %s\n",what); exit(1); }
  checks++;
}
static void put(const char *name, const char *text) {
  FILE *f = fopen(name,"w"); check(f != NULL,"create private phase file");
  check(fputs(text,f) >= 0 && fclose(f) == 0,"write complete phase file");
}
static void set(TrainConfig *c, const char *key, const char *value) {
  check(tr_cfg_set(c,key,value) == 0,key);
}
static void reject(const TrainConfig *c, TrainRecipe *r, const char *what) {
  char err[512] = {0};
  check(tr_recipe_load(c,r,err,sizeof err) < 0 && err[0],what);
}
static void cleanup(void) {
  if (!scratch[0]) return;
  /* The only files this test creates are these three fixed names. */
  unlink("first.conf"); unlink("second.conf"); unlink("bad.conf");
  if (chdir("/")) return;
  if (rmdir(scratch)) { perror("remove private recipe directory"); _Exit(2); }
}
int main(void) {
  const char *tmp = getenv("TMPDIR");
  if (!tmp || !tmp[0]) tmp = "/tmp";
  check(snprintf(scratch,sizeof scratch,"%s/netherite-train-recipe-XXXXXX",tmp) < (int)sizeof scratch,
        "temporary path fits");
  check(mkdtemp(scratch) != NULL,"fresh private recipe directory");
  char absolute[4096];
  check(realpath(scratch,absolute) != NULL,"resolve private recipe directory");
  strcpy(scratch,absolute);
  check(chdir(scratch) == 0,"enter private recipe directory");
  atexit(cleanup);
  TrainConfig base, saved;
  TrainRecipe *r = calloc(1,sizeof *r);
  check(r != NULL,"recipe allocation");
  tr_cfg_defaults(&base);
  set(&base,"lr","0.005"); set(&base,"action_repeat","6");
  set(&base,"reward.time_cost","0.02"); set(&base,"max_chunks","19");
  char err[512];
  check(tr_recipe_load(&base,r,err,sizeof err) == 0 && r->count == 1,
        "no schedule is one complete global phase");
  check(r->phase[0].max_chunks == 19 && r->phase[0].lr == base.lr && !r->source[0][0],
        "unscheduled budget and learning rate preserved");
  put("first.conf","max_chunks = 3\nmax_ticks = 1234\nlr = 0.001\naction_repeat = 2\nreward.time_cost = 0.2\n");
  put("second.conf","max_chunks = 7\nmax_wall = 12.5\nworld_min_logs = 2\n");
  set(&base,"phase_files"," first.conf , second.conf "); saved = base;
  check(tr_recipe_load(&base,r,err,sizeof err) == 0 && r->count == 2,"load ordered phase schedule");
  check(!memcmp(&base,&saved,sizeof base),"loading schedule never mutates global recipe");
  check(!strcmp(r->source[0],"first.conf") && !strcmp(r->source[1],"second.conf"),"phase order follows listed paths");
  check(r->phase[0].max_chunks == 3 && r->phase[0].max_ticks == 1234 && r->phase[0].max_wall == 0,
        "phase one gets its own explicit budgets");
  check(r->phase[1].max_chunks == 7 && r->phase[1].max_ticks == 0 && r->phase[1].max_wall == 12.5f,
        "phase two does not inherit previous tick budget");
  check(r->phase[1].lr == base.lr && r->phase[1].action_repeat == base.action_repeat &&
        r->phase[1].reward.time_cost == base.reward.time_cost,
        "second phase inherits global optimizer control and rewards independently");
  check(r->phase[0].lr != base.lr && r->phase[0].reward.time_cost != base.reward.time_cost &&
        r->phase[1].world_min_logs == 2,"phase changes are materialized rather than ignored");
  set(&base,"phase_files","second.conf,first.conf");
  check(tr_recipe_load(&base,r,err,sizeof err) == 0 && r->phase[0].max_chunks == 7 && r->phase[1].max_ticks == 1234,
        "reordering phases reorders budgets without hidden state");
  const char *badlists[] = {" ",",first.conf","first.conf,","first.conf,,second.conf","missing.conf"};
  for (size_t i = 0; i < sizeof badlists / sizeof *badlists; i++) {
    set(&base,"phase_files",badlists[i]); reject(&base,r,"empty or missing phase rejected before training");
  }
  char list[1024] = "";
  put("first.conf","max_chunks = 2\n");
  for (int i = 0; i < TR_RECIPE_MAX_PHASES; i++) strcat(list,i ? ",first.conf" : "first.conf");
  set(&base,"phase_files",list);
  check(tr_recipe_load(&base,r,err,sizeof err) == 0 && r->count == TR_RECIPE_MAX_PHASES,"maximum supported phase count works");
  strcat(list,",first.conf"); set(&base,"phase_files",list);
  reject(&base,r,"too many phases rejected without truncation");
  put("bad.conf","phase_files = first.conf\n"); set(&base,"phase_files","bad.conf");
  reject(&base,r,"nested schedule rejected");
  const char *conflicts[] = {
    "backend = cuda\n", "device = 1\n", "n_envs = 4\n", "rollout_steps = 8\n",
    "mb = 2\n", "nn_prec = f32\n", "tail_mb = drop\n", "seed = 41\n",
    "obs_history = 1\n", "obs_depth = 0\n", "action_yaw_degrees = 30\n",
    "action_heads = 255\n", "init_from = other.bin\n", "eval_conf = other.conf\n",
    "eval_executable = other-eval\n", "checkpoint = other-output.bin\n"
  };
  set(&base,"phase_files","first.conf,bad.conf");
  for (size_t i = 0; i < sizeof conflicts / sizeof *conflicts; i++) {
    put("bad.conf",conflicts[i]); reject(&base,r,"frozen policy backend or allocation mismatch rejected before training");
  }
  const char *badvalues[] = {
    "reward.time_cost = NaN\n", "reward.death_penalty = -1\n",
    "reward.shaping_scale = 1000001\n", "reward.unknown = 1\n",
    "obs_history = 3\n", "obs_depth = 2\n", "obs_pixel_stride = 3\n",
    "action_yaw_degrees = inf\n", "action_heads = 512\n", "obs_rgb = 1\n",
    "spawn_pitch_jitter = 91\n", "world_min_coal = -1\n", "max_ticks = nonsense\n",
    "obs_depth = 1 trailing\n", "reward.time_cost = 0.1 trailing\n", "max_chunks = 3 trailing\n"
  };
  set(&base,"phase_files","bad.conf");
  for (size_t i = 0; i < sizeof badvalues / sizeof *badvalues; i++) {
    put("bad.conf",badvalues[i]); reject(&base,r,"strict reward observation and budget parsing in phase files");
  }
  set(&base,"phase_files",""); set(&base,"checkpoint_metric","eval_success");
  reject(&base,r,"evaluation checkpoint selection requires configured evaluation before training");
  free(r);
  printf("test_train_recipe: %d checks PASS\n",checks);
  return 0;
}
