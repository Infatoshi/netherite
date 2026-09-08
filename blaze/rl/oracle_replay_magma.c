#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "eval_oracle.h"
#include "port_parity.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_ACTIONS 1000000u
static uint64_t hash_bytes(uint64_t h, const void *ptr, size_t n) {
  const unsigned char *p = ptr;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= UINT64_C(0x100000001b3);
  }
  return h;
}
static void string(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\')
      fprintf(f, "\\%c", c);
    else if (c < 32)
      fprintf(f, "\\u%04x", c);
    else
      fputc(c, f);
  }
  fputc('"', f);
}
static int parse_seed(const char *v, int *seed) {
  char *end;
  errno = 0;
  long n = strtol(v, &end, 10);
  if (errno || end == v || *end || n < 0 || n > 2147483647L)
    return -1;
  *seed = (int)n;
  return 0;
}
static int records(const char *path, size_t size, size_t count, int parity,
                   char *err, size_t cap) {
  FILE *f = fopen(path, "rb");
  struct stat st;
  if (!f || fstat(fileno(f), &st) || st.st_size != (off_t)(size * count)) {
    if (f)
      fclose(f);
    snprintf(err, cap, "record count/size mismatch: %s", path);
    return -1;
  }
  if (parity) {
    BpParityRecord r;
    for (size_t i = 0; i < count; i++)
      if (fread(&r, sizeof r, 1, f) != 1 || r.magic != BP_PARITY_MAGIC ||
          r.version != BP_PARITY_VERSION || r.size != sizeof r ||
          r.nsubsystems != BP_NSUBSYSTEMS) {
        fclose(f);
        snprintf(err, cap, "invalid PARY record %zu", i);
        return -1;
      }
  }
  fclose(f);
  return 0;
}
static int frames(const char *dir, size_t n, char *err, size_t cap) {
  char base[2048];
  snprintf(base, sizeof base, "%s/frames", dir);
  for (size_t i = 0; i < n; i++) {
    char path[2200];
    snprintf(path, sizeof path, "%s/frame_%06zu.ppm", base, i);
    FILE *f = fopen(path, "rb");
    int w = 0, h = 0, max = 0;
    char magic[3] = {0};
    struct stat st;
    int ok = f && fscanf(f, "%2s %d %d %d", magic, &w, &h, &max) == 4 &&
             !strcmp(magic, "P6") && w > 0 && h > 0 && w <= 16384 &&
             h <= 16384 && max == 255 && fgetc(f) == '\n' &&
             !fstat(fileno(f), &st) &&
             st.st_size - ftell(f) == (off_t)((size_t)w * h * 3);
    if (f)
      fclose(f);
    if (!ok) {
      snprintf(err, cap, "missing/incomplete PPM frame %zu", i);
      return -1;
    }
  }
  DIR *d = opendir(base);
  if (!d) {
    snprintf(err, cap, "missing frame directory");
    return -1;
  }
  size_t count = 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t len = strlen(e->d_name);
    if (len >= 4 && !strcmp(e->d_name + len - 4, ".ppm"))
      count++;
  }
  closedir(d);
  if (count != n) {
    snprintf(err, cap, "unexpected extra PPM frames");
    return -1;
  }
  return 0;
}
int main(int argc, char **argv) {
  const char *requests = NULL, *snapshot = NULL, *bin = NULL, *conf = NULL,
             *trace = NULL;
  int seed = -1;
  char err[1024] = "";
  for (int i = 1; i < argc; i++) {
    const char **dest = !strcmp(argv[i], "--requests")     ? &requests
                        : !strcmp(argv[i], "--snapshot")   ? &snapshot
                        : !strcmp(argv[i], "--magma-bin")  ? &bin
                        : !strcmp(argv[i], "--magma-conf") ? &conf
                        : !strcmp(argv[i], "--trace-dir")  ? &trace
                                                           : NULL;
    if (!strcmp(argv[i], "--seed")) {
      if (++i == argc || seed >= 0 || parse_seed(argv[i], &seed))
        goto usage;
      continue;
    }
    if (!dest || *dest || ++i == argc)
      goto usage;
    *dest = argv[i];
  }
  if (!requests || !snapshot || !bin || !conf || !trace || seed < 0 ||
      strlen(trace) > 1500)
    goto usage;
  FILE *f = fopen(requests, "rb");
  if (!f) {
    snprintf(err, sizeof err, "request log unreadable");
    goto early;
  }
  double *actions = NULL;
  size_t expected = 0, capacity = 0, lines = 0, controls = 0;
  uint64_t request_hash = UINT64_C(0xcbf29ce484222325), request_bytes = 0;
  char line[65538];
  while (fgets(line, sizeof line, f)) {
    size_t len = strlen(line);
    lines++;
    if (!len || line[len - 1] != '\n') {
      snprintf(err, sizeof err,
               "request line %zu is truncated, oversized or contains NUL",
               lines);
      goto bad_source;
    }
    request_hash = hash_bytes(request_hash, line, len);
    request_bytes += len;
    double a[13];
    int type = eval_oracle_parse_request(line, a, err, sizeof err);
    if (type < 0)
      goto bad_source;
    if (!type) {
      controls++;
      continue;
    }
    if (expected == MAX_ACTIONS) {
      snprintf(err, sizeof err, "request action limit exceeded");
      goto bad_source;
    }
    if (expected == capacity) {
      size_t next = capacity ? capacity * 2 : 256;
      if (next > MAX_ACTIONS)
        next = MAX_ACTIONS;
      double *p = realloc(actions, next * 13 * sizeof *p);
      if (!p) {
        snprintf(err, sizeof err, "action allocation failed");
        goto bad_source;
      }
      actions = p;
      capacity = next;
    }
    memcpy(actions + expected * 13, a, sizeof a);
    expected++;
  }
  if (ferror(f) || !expected) {
    snprintf(err, sizeof err,
             "request log read failed or contains zero actions");
    goto bad_source;
  }
  fclose(f);
  f = NULL;
  f = fopen(snapshot, "rb");
  RlSnapHead header;
  uint64_t snapshot_hash = UINT64_C(0xcbf29ce484222325), snapshot_bytes = 0;
  if (!f || fread(&header, sizeof header, 1, f) != 1 ||
      memcmp(header.magic, "BSNP", 4) || header.version < 1 ||
      header.version > BLAZE_SNAP_VERSION || header.seed != seed) {
    snprintf(err, sizeof err, "snapshot header/seed invalid");
    if (f)
      fclose(f);
    free(actions);
    goto early;
  }
  rewind(f);
  unsigned char buf[8192];
  size_t got;
  while ((got = fread(buf, 1, sizeof buf, f))) {
    snapshot_hash = hash_bytes(snapshot_hash, buf, got);
    snapshot_bytes += got;
  }
  int read_error = ferror(f);
  fclose(f);
  f = NULL;
  if (read_error) {
    snprintf(err, sizeof err, "snapshot read failed");
    free(actions);
    goto early;
  }
  if (mkdir(trace, 0700)) {
    snprintf(err, sizeof err, "trace-dir must be a new writable directory");
    free(actions);
    goto early;
  }
  EvalMagma *m =
      eval_magma_open_config(bin, snapshot, seed, trace, conf, err, sizeof err);
  size_t executed = 0, attempted = 0;
  int goal_count = 0;
  int64_t first_goal = -1, first_goal_tick = -1, initial_tick = -1,
          last_tick = -1;
  int valid = 0, rc = 2, terminal_dead = 0;
  const char *reason = "replay_failure";
  if (!m)
    goto report;
  initial_tick = last_tick = eval_magma_obs(m)->tick;
  if (eval_magma_obs(m)->dead) {
    snprintf(err, sizeof err, "snapshot begins in a dead state");
    goto close;
  }
  if (eval_magma_obs(m)->inv_counts[5]) {
    snprintf(err, sizeof err, "snapshot already has a wooden pickaxe");
    goto close;
  }
  for (size_t i = 0; i < expected; i++) {
    attempted = i + 1;
    if (eval_magma_step(m, actions + i * 13, 1)) {
      snprintf(err, sizeof err, "Magma failed at requested action %zu", i);
      goto close;
    }
    const EvalMagmaObs *o = eval_magma_obs(m);
    if (o->tick != last_tick + 1 || !isfinite(o->x) || !isfinite(o->y) ||
        !isfinite(o->z) || !isfinite(o->yaw) || !isfinite(o->pitch)) {
      snprintf(err, sizeof err, "Magma tick/state invalid at action %zu", i);
      goto close;
    }
    executed++;
    last_tick = o->tick;
    goal_count = o->inv_counts[5];
    if (goal_count > 0 && first_goal < 0) {
      first_goal = (int64_t)i + 1;
      first_goal_tick = o->tick;
    }
    if (executed % 100 == 0)
      fprintf(stderr, "oracle-replay-magma: alive executed=%zu/%zu tick=%lld\n",
              executed, expected, o->tick);
    if (o->dead) {
      terminal_dead = 1;
      break;
    }
  }
  valid = 1;
close:
  eval_magma_close(m);
  if (valid) {
    char path[2048];
    snprintf(path, sizeof path, "%s/magma.bolr", trace);
    if (records(path, sizeof(EvalMagmaObs), executed + 1, 0, err, sizeof err))
      valid = 0;
    snprintf(path, sizeof path, "%s/magma.pary", trace);
    if (valid &&
        records(path, sizeof(BpParityRecord), executed + 1, 1, err, sizeof err))
      valid = 0;
    if (valid && frames(trace, executed, err, sizeof err))
      valid = 0;
  }
  rc = valid ? (terminal_dead ? 4 : first_goal >= 0 ? 0 : 3) : 2;
  reason = valid ? (terminal_dead ? "engine_death" : "replay_complete")
                 : "replay_failure";
report:;
  char report_path[2048];
  snprintf(report_path, sizeof report_path, "%s/replay.json", trace);
  FILE *out = fopen(report_path, "wx");
  if (!out) {
    snprintf(err, sizeof err, "replay report creation failed");
    rc = 2;
  } else {
    fprintf(
        out,
        "{\"schema\":\"netherite.oracle_same_action_replay.v1\",\"mode\":"
        "\"same_action_replay_no_policy_inference\",\"valid\":%s,\"requests\":",
        valid ? "true" : "false");
    string(out, requests);
    fputs(",\"snapshot\":", out);
    string(out, snapshot);
    fputs(",\"magma_conf\":", out);
    string(out, conf);
    fputs(",\"reason\":", out);
    string(out, reason);
    fprintf(out,
            ",\"terminal_engine_dead\":%s,\"complete_action_replay\":%s,"
            "\"attempted_actions\":%zu,\"verified_frame_count\":%zu",
            terminal_dead ? "true" : "false",
            valid && !terminal_dead && executed == expected ? "true" : "false",
            attempted, valid ? executed : 0);
    fputs(",\"error\":", out);
    string(out, err);
    fprintf(out,
            ",\"request_fnv64\":\"%016" PRIx64 "\",\"request_bytes\":%" PRIu64
            ",\"snapshot_fnv64\":\"%016" PRIx64 "\",\"snapshot_bytes\":%" PRIu64
            ",\"request_lines\":%zu,\"control_lines\":%zu,\"expected_actions\":"
            "%zu,\"executed_ticks\":%zu,\"initial_magma_tick\":%" PRId64
            ",\"last_magma_tick\":%" PRId64
            ",\"success_item\":270,\"final_goal_count\":%d,\"first_goal_"
            "action\":%" PRId64 ",\"first_goal_tick\":%" PRId64
            ",\"frames_verified\":%s,\"source_note\":\"Source policy_step "
            "requests replayed in order including burn-in, stopping on engine "
            "death. Oracle acceptance is "
            "established by the paired Oracle receipt report.\"}\n",
            request_hash, request_bytes, snapshot_hash, snapshot_bytes, lines,
            controls, expected, executed, initial_tick, last_tick, goal_count,
            first_goal, first_goal_tick, valid ? "true" : "false");
    if (fclose(out))
      rc = 2;
  }
  free(actions);
  fprintf(stderr,
          "oracle-replay-magma: rc=%d reason=%s expected=%zu executed=%zu "
          "goal_count=%d "
          "first_goal_action=%" PRId64 " report=%s%s%s\n",
          rc, reason, expected, executed, goal_count, first_goal, report_path,
          *err ? " error=" : "", err);
  return rc;
bad_source:
  fclose(f);
  free(actions);
early:
  fprintf(stderr, "oracle-replay-magma: %s\n", err);
  return 2;
usage:
  fprintf(stderr,
          "usage: oracle-replay-magma --requests FILE --snapshot BSNP "
          "--magma-bin PATH --magma-conf PATH --trace-dir NEWDIR --seed N\n");
  return 2;
}
