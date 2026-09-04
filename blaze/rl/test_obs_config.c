#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "obs_pack.h"
#include "rl_ckpt.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks, loads, saves, save_fail;
static void check(int ok, const char *what) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
  checks++;
}
int nn_load(Nn *nn, const char *path) { (void)nn; (void)path; loads++; return 0; }
int nn_save(const Nn *nn, const char *path) {
  (void)nn; saves++;
  if (save_fail) return -1;
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  fputs("test weights\n", f);
  return fclose(f) ? -1 : 0;
}
const char *nn_last_error(void) { return "test weight failure"; }
static void set(PolicyIoConfig *c, const char *key, const char *value) {
  char err[256];
  check(policy_io_set(c, key, value, err, sizeof err) == 0, key);
}
static void test_config(void) {
  PolicyIoConfig c, d;
  char err[256];
  policy_io_default(&c); d = c;
  check(policy_io_validate(&c, err, sizeof err) == 0, "defaults validate");
  check(policy_io_is_default(&c), "default recognized");
  uint64_t base = policy_io_fingerprint(&c);
  check(base != 0, "default fingerprint present");
  const char *keys[] = {"obs_history","obs_semantic_mask","obs_depth","obs_edges",
    "obs_base_scalars","obs_inventory","obs_pose","obs_clock","obs_pixel_stride",
    "action_yaw_degrees","action_pitch_degrees","action_heads"};
  const char *values[] = {"1","126","0","0","0","0","0","0","2","20","12","510"};
  for (int i = 0; i < 12; i++) {
    c = d; set(&c, keys[i], values[i]);
    check(policy_io_fingerprint(&c) != base && !policy_io_is_default(&c), "every policy choice changes contract");
  }
  const char *badkeys[] = {"obs_history","obs_semantic_mask","obs_depth","obs_pixel_stride",
    "action_yaw_degrees","action_yaw_degrees","action_pitch_degrees","action_heads","obs_clock"};
  const char *badvalues[] = {"3","128","2","3","NaN","0","91","512","1junk"};
  for (int i = 0; i < 9; i++) {
    c = d;
    check(policy_io_set(&c, badkeys[i], badvalues[i], err, sizeof err) == -1, "unsupported value rejected");
    check(policy_io_is_default(&c), "invalid setter leaves configuration unchanged");
  }
  check(policy_io_set(&c, "obs_rgb", "1", err, sizeof err) == 1, "RGB is not a pretend supported knob");
  check(policy_io_set(&c, "recurrent", "1", err, sizeof err) == 1, "recurrent network unsupported");
  FILE *f = tmpfile(); check(f != NULL, "dump stream");
  policy_io_dump(&d, f); rewind(f);
  char line[256]; int count = 0;
  while (fgets(line, sizeof line, f)) {
    char key[128], value[128];
    check(sscanf(line, "%127s = %127s", key, value) == 2, "dump is parseable key/value");
    set(&c, key, value); count++;
  }
  fclose(f);
  check(count == 12 && policy_io_fingerprint(&c) == base, "dump round trips complete contract");
}
static void test_packing(void) {
  enum {N = 2};
  static unsigned short cam[N * ENV_NPIX];
  static uint8_t depth[N * ENV_NPIX], edge[N * ENV_NPIX];
  static uint8_t prior[N * ENV_N_PLANES * ENV_NPIX];
  static uint8_t old[N * ENV_N_CH * ENV_NPIX], got[sizeof old];
  static uint8_t oldframe[sizeof prior], frame[sizeof prior];
  float scal6[N * ENV_SCAL], pose[N * ENV_POSE], wantscal[N * POL_SCAL], scal[N * POL_SCAL];
  int status[N * ENV_STATUS], ep[N] = {3,7};
  uint8_t have[N] = {0,1};
  const int ids[] = {0,17,18,16,1,4,2,3,58,90};
  for (size_t i = 0; i < sizeof cam / sizeof cam[0]; i++) {
    cam[i] = (unsigned short)ids[i % 10]; depth[i] = (uint8_t)(i % 251); edge[i] = (uint8_t)(i % 3);
  }
  for (size_t i = 0; i < sizeof prior; i++) prior[i] = (uint8_t)(i % 239);
  for (int i = 0; i < N * ENV_SCAL; i++) scal6[i] = i * 0.25f;
  for (int i = 0; i < N * ENV_POSE; i++) pose[i] = i * 1.5f;
  for (int i = 0; i < N * ENV_STATUS; i++) status[i] = i % 17;
  PolicyIoConfig c; policy_io_default(&c);
  pack_obs(cam,depth,edge,scal6,pose,status,ep,10,have,prior,N,old,wantscal,oldframe);
  pack_obs_config(&c,cam,depth,edge,scal6,pose,status,ep,10,have,prior,N,got,scal,frame);
  check(!memcmp(old,got,sizeof old) && !memcmp(wantscal,scal,sizeof scal) && !memcmp(oldframe,frame,sizeof frame), "default observations byte-identical to legacy");
  set(&c,"obs_history","1"); set(&c,"obs_semantic_mask","5");
  set(&c,"obs_depth","0"); set(&c,"obs_edges","0");
  set(&c,"obs_base_scalars","0"); set(&c,"obs_inventory","0");
  set(&c,"obs_pose","0"); set(&c,"obs_clock","0");
  for (int stride = 1; stride <= 4; stride *= 2) {
    c.obs_pixel_stride = stride;
    pack_obs_config(&c,cam,depth,edge,scal6,pose,status,ep,10,have,prior,N,got,scal,frame);
    for (int e = 0; e < N; e++) {
      uint8_t *p = got + e * ENV_N_CH * ENV_NPIX;
      check(!memcmp(p,p+ENV_N_PLANES*ENV_NPIX,ENV_N_PLANES*ENV_NPIX), "one-frame history duplicates current");
      for (int ch = 0; ch < ENV_N_PLANES; ch++) for (int y = 0; y < ENV_CAM_H; y++) for (int x = 0; x < ENV_CAM_W; x++) {
        int src = e * ENV_NPIX + (y / stride * stride) * ENV_CAM_W + x / stride * stride;
        int want = ch == 0 ? cam[src] == 17 : ch == 2 ? cam[src] == 16 : 0;
        check(p[ch * ENV_NPIX + y * ENV_CAM_W + x] == want, "mask and coarse sampling match source pixel");
      }
    }
    for (int i = 0; i < N * POL_SCAL; i++) check(scal[i] == 0, "disabled scalar groups zeroed");
  }
  policy_io_default(&c); c.obs_semantic_mask = 0; c.obs_pixel_stride = 4;
  pack_obs_config(&c,cam,depth,edge,scal6,pose,status,ep,10,have,prior,N,got,scal,frame);
  for (int p = 0; p < 7; p++) check(got[ENV_N_CH*ENV_NPIX+p*ENV_NPIX] == 0, "semantic masks apply to prior history too");
  check(got[ENV_N_CH*ENV_NPIX+7*ENV_NPIX+67] == prior[ENV_N_PLANES*ENV_NPIX+7*ENV_NPIX], "prior depth coarse sampling preserves history");
  check(!memcmp(wantscal,scal,sizeof scal), "image ablation leaves enabled scalar groups intact");
  const char *groups[] = {"obs_base_scalars","obs_inventory","obs_pose","obs_clock"};
  const int first[] = {0,6,25,26}, end[] = {6,25,26,27};
  for (int g = 0; g < 4; g++) {
    policy_io_default(&c); set(&c,groups[g],"0");
    pack_obs_config(&c,cam,depth,edge,scal6,pose,status,ep,10,have,prior,N,got,scal,frame);
    check(!memcmp(old,got,sizeof old), "scalar ablation preserves every observation pixel");
    for (int e = 0; e < N; e++) for (int i = 0; i < POL_SCAL; i++)
      check(scal[e*POL_SCAL+i] == (i >= first[g] && i < end[g] ? 0 : wantscal[e*POL_SCAL+i]), "each scalar toggle affects only its advertised group");
  }
}
static void test_actions(void) {
  PolicyIoConfig c; policy_io_default(&c);
  int32_t a[27] = {0,2,0,1,1,1,3,1,9, 2,0,2,0,1,0,2,1,4, -9,9,9,1,0,1,0,0,0};
  double want[39], got[39];
  acts_to_rows(a,3,want); acts_to_rows_config(&c,a,3,got);
  check(!memcmp(want,got,sizeof got), "default actions byte-identical to legacy");
  c.action_yaw_degrees = 45; c.action_pitch_degrees = 5;
  acts_to_rows_config(&c,a,3,got);
  check(got[2] == -45 && got[3] == 5 && got[ENV_ACT+2] == 45 && got[ENV_ACT+3] == -5, "configured angle steps applied");
  check(got[2*ENV_ACT+2] == 0 && got[2*ENV_ACT+3] == 0, "invalid discrete angle inputs stay neutral");
  const int columns[] = {2,3,0,4,7,8,10,11,9};
  for (int h = 0; h < POL_HEADS; h++) {
    c.action_heads = 511 ^ (1 << h); acts_to_rows_config(&c,a,3,got);
    for (int e = 0; e < 3; e++) check(got[e*ENV_ACT+columns[h]] == (h == 6 || h == 8 ? -1 : 0), "disabled head decodes to neutral action");
  }
  c.action_heads = 0; acts_to_rows_config(&c,a,3,got);
  for (int i = 0; i < 39; i++) check(got[i] == (i%ENV_ACT == 9 || i%ENV_ACT == 10 ? -1 : 0), "all-disabled policy is exact noop");
}
static void test_checkpoints(void) {
  char dir[] = "/tmp/netherite-policy-XXXXXX", path[512], side[540], err[256];
  check(mkdtemp(dir) != NULL, "checkpoint temporary directory");
  snprintf(path,sizeof path,"%s/legacy.bin",dir);
  PolicyIoConfig c,d; policy_io_default(&d); c = d;
  check(nn_save(NULL,path) == 0, "legacy weights fixture");
  check(policy_io_checkpoint_check(path,&d,err,sizeof err) == 1, "default legacy load explicitly identified");
  check(rl_ckpt_load_config(NULL,path,&d,err,sizeof err) == 0 && loads == 1, "default legacy load allowed with warning");
  set(&c,"obs_history","1");
  check(rl_ckpt_load_config(NULL,path,&c,err,sizeof err) < 0 && loads == 1, "nondefault legacy load fails before touching weights");
  int before = saves;
  check(rl_ckpt_save_config(NULL,path,&c,err,sizeof err) < 0 && saves == before, "nondefault save cannot overwrite unrelated legacy weights");
  check(rl_ckpt_save_config(NULL,path,&d,err,sizeof err) == 0, "default legacy weights acquire contract on save");
  check(policy_io_checkpoint_check(path,&d,err,sizeof err) == 0, "complete saved contract compatible");
  snprintf(side,sizeof side,"%s.policy.conf",path);
  struct stat st0,st1; check(stat(side,&st0) == 0, "stat original contract");
  check(policy_io_checkpoint_write(path,&d,err,sizeof err) == 0 && stat(side,&st1) == 0 && st0.st_ino == st1.st_ino, "identical metadata write preserves existing file");
  check(policy_io_checkpoint_write(path,&c,err,sizeof err) < 0 && policy_io_checkpoint_check(path,&d,err,sizeof err) == 0, "mismatched writer does not overwrite existing contract");
  snprintf(path,sizeof path,"%s/custom.bin",dir);
  check(rl_ckpt_save_config(NULL,path,&c,err,sizeof err) == 0, "new nondefault weights save with contract");
  check(rl_ckpt_load_config(NULL,path,&c,err,sizeof err) == 0 && loads == 2, "matching nondefault load");
  check(rl_ckpt_load_config(NULL,path,&d,err,sizeof err) < 0 && loads == 2, "mismatch rejected before weight load");
  snprintf(side,sizeof side,"%s.policy.conf",path);
  FILE *f = fopen(side,"a"); check(f != NULL,"open metadata corruption fixture");
  fputs("obs_history = 1\n",f); fclose(f);
  check(policy_io_checkpoint_check(path,&c,err,sizeof err) < 0, "duplicate metadata field rejected");
  f = fopen(side,"w"); check(f != NULL,"write unrelated sidecar");
  fputs("unrelated content\n",f); fclose(f);
  check(policy_io_checkpoint_write(path,&c,err,sizeof err) < 0, "unrelated metadata is not replaced");
  f = fopen(side,"w"); check(f != NULL,"write truncated sidecar");
  fputs("policy_io_version = 1\npolicy_io_fingerprint = 0000000000000000\n",f); fclose(f);
  check(rl_ckpt_load_config(NULL,path,&c,err,sizeof err) < 0 && loads == 2, "incomplete metadata fails before loading weights");
  f = fopen(side,"w"); check(f != NULL,"write fingerprint corruption");
  fputs("policy_io_version = 1\npolicy_io_fingerprint = 0000000000000000\n",f);
  policy_io_dump(&c,f); fclose(f);
  check(policy_io_checkpoint_check(path,&c,err,sizeof err) < 0, "complete metadata with corrupted fingerprint rejected");
  snprintf(path,sizeof path,"%s/failed.bin",dir); save_fail = 1;
  check(rl_ckpt_save_config(NULL,path,&c,err,sizeof err) < 0, "weight save failure propagates");
  snprintf(side,sizeof side,"%s.policy.conf",path);
  check(access(side,F_OK) != 0, "failed weights do not publish metadata"); save_fail = 0;
  snprintf(path,sizeof path,"%s/concurrent.bin",dir);
  pid_t pid = fork(); check(pid >= 0,"fork concurrent metadata writer");
  if (!pid) _exit(policy_io_checkpoint_write(path,&c,err,sizeof err) ? 1 : 0);
  check(policy_io_checkpoint_write(path,&c,err,sizeof err) == 0,"atomic parent metadata publisher");
  int status; check(waitpid(pid,&status,0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,"atomic child metadata publisher");
  check(policy_io_checkpoint_check(path,&c,err,sizeof err) == 0,"concurrent publication leaves complete contract");
  DIR *entries = opendir(dir); check(entries != NULL,"open cleanup directory");
  struct dirent *entry;
  while ((entry = readdir(entries))) {
    if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
    snprintf(path,sizeof path,"%s/%s",dir,entry->d_name);
    check(unlink(path) == 0,"remove checkpoint test artifact");
  }
  closedir(entries); check(rmdir(dir) == 0,"remove checkpoint test directory");
}
int main(void) {
  test_config(); test_packing(); test_actions(); test_checkpoints();
  printf("policy input/output: %d checks PASS\n",checks);
  return 0;
}
