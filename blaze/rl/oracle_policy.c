#define _POSIX_C_SOURCE 200809L
#include "eval_config.h"
#include "eval_oracle.h"
#include "model.h"
#include "nn.h"
#include "obs_pack.h"
#include "oracle_initial.h"
#include "rl_ckpt.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t hash_bytes(uint64_t h, const void *ptr, size_t n) {
  const unsigned char *p = ptr;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= UINT64_C(0x100000001b3);
  }
  return h;
}
static int file_hash(const char *path, uint64_t *h, uint64_t *bytes) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  unsigned char b[8192];
  size_t n;
  *h = UINT64_C(0xcbf29ce484222325);
  *bytes = 0;
  while ((n = fread(b, 1, sizeof b, f))) {
    *h = hash_bytes(*h, b, n);
    *bytes += n;
  }
  int rc = ferror(f) ? -1 : 0;
  if (fclose(f))
    rc = -1;
  return rc;
}
static void json_string(FILE *f, const char *s) {
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
static FILE *exclusive(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return NULL;
  FILE *f = fdopen(fd, "w");
  if (!f)
    close(fd);
  return f;
}
/* Tape paths are local to the Oracle host. This verifies expected file coverage
 * and PNG framing, not pixel equality; the renderer comparison owns pixels. */
static int tape_paths(const char *path, char *frames, size_t cap, char *err,
                      size_t ec) {
  size_t n = strlen(path);
  if (n < 7 || n > 1500 || path[0] != '/' || strcmp(path + n - 6, ".jsonl")) {
    snprintf(err, ec,
             "--tape requires an absolute .jsonl path on the Oracle host");
    return -1;
  }
  if (snprintf(frames, cap, "%.*s_frames", (int)(n - 6), path) >= (int)cap)
    return -1;
  const char *suffix[] = {"", ".geom.jsonl", "_frames", "_world"};
  for (int i = 0; i < 4; i++) {
    char check[2048];
    if (i == 0)
      snprintf(check, sizeof check, "%s", path);
    else
      snprintf(check, sizeof check, "%.*s%s", (int)(n - 6), path, suffix[i]);
    struct stat st;
    if (!lstat(check, &st) || errno != ENOENT) {
      snprintf(err, ec, "tape output already exists or cannot be inspected: %.800s",
               check);
      return -1;
    }
  }
  return 0;
}
static int tape_frames(const char *tape, const char *dir, int64_t expected,
                       char *err, size_t cap) {
  if (expected < 1) {
    snprintf(err, cap, "tape has no verified frames");
    return -1;
  }
  struct stat st;
  if (stat(tape, &st) || !S_ISREG(st.st_mode) || st.st_size == 0) {
    snprintf(err, cap, "recorded tape missing/empty");
    return -1;
  }
  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  for (int64_t i = 0; i < expected; i++) {
    char path[2048];
    snprintf(path, sizeof path, "%s/f_%06" PRId64 ".png", dir, i);
    FILE *f = fopen(path, "rb");
    unsigned char head[24], tail[12];
    int ok = f && fread(head, 1, sizeof head, f) == sizeof head &&
             !memcmp(head, sig, 8) && !memcmp(head + 12, "IHDR", 4) &&
             (head[16] || head[17] || head[18] || head[19]) &&
             (head[20] || head[21] || head[22] || head[23]) &&
             !fseek(f, -12, SEEK_END) && fread(tail, 1, 12, f) == 12 &&
             !memcmp(tail, "\0\0\0\0IEND", 8);
    if (f)
      fclose(f);
    if (!ok) {
      snprintf(err, cap, "missing/incomplete frame: %.800s", path);
      return -1;
    }
  }
  DIR *d = opendir(dir);
  if (!d) {
    snprintf(err, cap, "frame directory unavailable");
    return -1;
  }
  int64_t count = 0;
  struct dirent *entry;
  while ((entry = readdir(d))) {
    size_t n = strlen(entry->d_name);
    if (n >= 4 && !strcmp(entry->d_name + n - 4, ".png"))
      count++;
  }
  closedir(d);
  if (count != expected) {
    snprintf(err, cap,
             "frame count mismatch: expected %" PRId64 " found %" PRId64,
             expected, count);
    return -1;
  }
  return 0;
}

static int parse_int(const char *s, int lo, int hi, int *out) {
  char *end;
  errno = 0;
  long n = strtol(s, &end, 10);
  if (errno || end == s || *end || n < lo || n > hi)
    return -1;
  *out = (int)n;
  return 0;
}
static int initial_boundary(const EvalOracleReceipt *r,
                            const EvalOracleReceipt *expected,
                            const char *phase, char *err, size_t cap) {
#define SAME(field)                                                            \
  do {                                                                         \
    if (r->field != expected->field) {                                         \
      snprintf(err, cap, "%s changed initial %s before first action", phase,   \
               #field);                                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)
  if (!r || !r->policy_locked || !r->have_ticks) {
    snprintf(err, cap, "%s lost frozen initial state", phase);
    return -1;
  }
  SAME(player_tick);
  SAME(server_tick);
  SAME(action_seq);
  SAME(action_fnv);
  SAME(obs.x);
  SAME(obs.y);
  SAME(obs.z);
  SAME(obs.yaw);
  SAME(obs.pitch);
  SAME(obs.dead);
  SAME(have_physics);
  SAME(vx);
  SAME(vy);
  SAME(vz);
  SAME(health);
  SAME(food);
  SAME(fall_distance);
  SAME(on_ground);
  SAME(obs.hotbar_sel);
  SAME(obs.container);
  SAME(inventory_total);
  if (memcmp(r->inventory, expected->inventory, sizeof r->inventory)) {
    snprintf(err, cap, "%s changed initial inventory before first action",
             phase);
    return -1;
  }
  return 0;
#undef SAME
}

static int step_locked(EvalOracle *o, const double *row, char *err, int cap) {
  if (eval_oracle_step(o, row, err, cap))
    return -1;
  const EvalOracleReceipt *r = eval_oracle_receipt(o);
  if (!r || !r->policy_locked) {
    snprintf(err, (size_t)cap, "policy lock disappeared during episode");
    return -1;
  }
  return 0;
}
static int goal(const EvalOracleReceipt *r) { return r->obs.inv_counts[5] > 0; }
static void tick_log(FILE *f, const EvalOracleReceipt *r, int dec, int repeat,
                     int burnin) {
  fprintf(f,
          "{\"kind\":\"tick\",\"decision\":%d,\"repeat_tick\":%d,\"burnin\":%s,"
          "\"player_tick\":%" PRId64 ",\"server_tick\":%" PRId64
          ",\"action_seq\":%" PRIu64 ",\"wooden_pickaxes\":%d,\"dead\":%s}\n",
          dec, repeat, burnin ? "true" : "false", r->player_tick,
          r->server_tick, r->action_seq, r->obs.inv_counts[5],
          r->obs.dead ? "true" : "false");
}

int main(int argc, char **argv) {
  EvalCfg c;
  char err[1024] = "", prefix[2048], event_path[2048], conf_path[2048];
  const char *ip = "127.0.0.1", *tape = NULL, *initial_path = NULL,
             *capture_path = NULL;
  char frames_dir[2048] = "";
  int port = 25575, timeout = 30000, seed_index = 0, attempt = 0,
      require_empty = 1, allow_legacy = 0, dump = 0;
  char **args = calloc((size_t)argc + 1, sizeof *args);
  if (!args)
    return 2;
  int ac = 1;
  args[0] = argv[0];
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    int *dest = NULL, lo = 0, hi = 1;
    if (!strcmp(a, "--capture-oracle-snapshot")) {
      if (++i == argc)
        goto usage;
      capture_path = argv[i];
      continue;
    }
    if (!strcmp(a, "--initial-snapshot")) {
      if (++i == argc)
        goto usage;
      initial_path = argv[i];
      continue;
    }
    if (!strcmp(a, "--tape")) {
      if (++i == argc)
        goto usage;
      tape = argv[i];
      continue;
    }
    if (!strcmp(a, "--oracle-ip")) {
      if (++i == argc)
        goto usage;
      ip = argv[i];
      continue;
    }
    if (!strcmp(a, "--oracle-port")) {
      dest = &port;
      lo = 1;
      hi = 65535;
    } else if (!strcmp(a, "--timeout-ms")) {
      dest = &timeout;
      lo = 1;
      hi = 600000;
    } else if (!strcmp(a, "--seed-index")) {
      dest = &seed_index;
      hi = EVAL_MAX_SEEDS - 1;
    } else if (!strcmp(a, "--attempt-index")) {
      dest = &attempt;
      hi = EVAL_MAX_TRIES - 1;
    } else if (!strcmp(a, "--require-empty-inventory"))
      dest = &require_empty;
    else if (!strcmp(a, "--allow-legacy-contract"))
      dest = &allow_legacy;
    if (dest) {
      if (++i == argc || parse_int(argv[i], lo, hi, dest))
        goto usage;
      continue;
    }
    args[ac++] = argv[i];
  }
  {
    int pr = eval_cfg_parse_argv(&c, ac, args, &dump);
    free(args);
    args = NULL;
    if (pr)
      return pr > 0 ? 0 : 2;
  }
  if (eval_cfg_validate(&c, err, sizeof err))
    goto config_error;
  if (strcmp(c.backend, "cpu") || c.stage < 0 || c.stage > 4 ||
      c.success_item != 270 || seed_index >= c.nseeds || attempt >= c.tries ||
      c.allow_missing) {
    snprintf(err, sizeof err,
             "requires backend=cpu, one stage, success_item=270, valid "
             "seed/attempt index, allow_missing=0");
    goto config_error;
  }
  if (dump) {
    eval_cfg_dump(&c, stdout);
    printf("# oracle %s:%d seed_index=%d attempt_index=%d require_empty=%d\n",
           ip, port, seed_index, attempt, require_empty);
    return 0;
  }
  if (capture_path) {
    if (!initial_path || capture_path[0] != '/' ||
        strlen(capture_path) > 3000) {
      snprintf(err, sizeof err,
               "--capture-oracle-snapshot requires --initial-snapshot and a "
               "new absolute output path");
      goto config_error;
    }
    struct stat st;
    char provenance[3100];
    snprintf(provenance, sizeof provenance, "%s.provenance.json", capture_path);
    if (!lstat(capture_path, &st) || errno != ENOENT ||
        !lstat(provenance, &st) || errno != ENOENT) {
      snprintf(err, sizeof err,
               "captured snapshot/provenance path already exists or cannot be "
               "inspected");
      goto config_error;
    }
  }
  if (tape && tape_paths(tape, frames_dir, sizeof frames_dir, err, sizeof err))
    goto config_error;
  int contract =
      policy_io_checkpoint_check(c.checkpoint, &c.policy, err, sizeof err);
  if (contract < 0)
    goto config_error;
  if (contract == 1 && !allow_legacy) {
    snprintf(
        err, sizeof err,
        "checkpoint policy contract missing; explicit --allow-legacy-contract "
        "1 required for historical exact defaults");
    goto config_error;
  }
  uint64_t ckhash = 0, ckbytes = 0;
  if (file_hash(c.checkpoint, &ckhash, &ckbytes)) {
    snprintf(err, sizeof err, "checkpoint cannot be hashed");
    goto config_error;
  }
  int lane = seed_index * c.tries + attempt, n = lane + 1,
      ep_lim = c.ep_ticks / c.action_repeat;
  NnCreate desc = {0};
  desc.backend = NN_BACKEND_CPU;
  desc.device = 0;
  desc.max_n = n;
  desc.prec = NN_PREC_F32;
  desc.config = nn_config_default();
  desc.config.rng_seed = c.seed + (uint64_t)c.stage;
  Nn *nn = nn_create(&desc);
  if (!nn) {
    snprintf(err, sizeof err, "nn_create: %s", nn_last_error());
    goto config_error;
  }
  if (rl_ckpt_load_config(nn, c.checkpoint, &c.policy, err, sizeof err)) {
    nn_destroy(nn);
    goto config_error;
  }
  err[0] = 0; /* Legacy-contract warning is not an episode failure. */
  /* Forward one real observation; sample dummy preceding lanes to retain the
   * eval RNG index seed_index*tries+attempt without copying sampler code. */
  float *logits = calloc((size_t)n * NN_N_LOGITS, sizeof(float)),
        *logp = calloc((size_t)n, sizeof(float));
  int32_t *acts = calloc((size_t)n * POL_HEADS, sizeof(int32_t));
  if (!logits || !logp || !acts) {
    free(logits);
    free(logp);
    free(acts);
    nn_destroy(nn);
    snprintf(err, sizeof err, "allocation failed");
    goto config_error;
  }
  snprintf(prefix, sizeof prefix, "%s.oracle", c.report);
  snprintf(event_path, sizeof event_path, "%s.decisions.jsonl", c.report);
  snprintf(conf_path, sizeof conf_path, "%s.conf", c.report);
  FILE *report = exclusive(c.report), *events = exclusive(event_path),
       *conf = exclusive(conf_path);
  EvalOracle *o = NULL;
  int rc = 2, locked = 0, success = 0, decisions = 0;
  int64_t start_player = -1, start_server = -1, post_burn = -1,
          last_player = -1, last_server = -1, achievement = -1;
  const char *reason = "infrastructure_error";
  OracleInitial initial = {0};
  char initial_blocks[4096] = "", initial_light[4096] = "";
  uint64_t template_mismatches = 0;
  int template_compared = 0, captured = 0;
  int template_first[5] = {0};
  int recording = 0, frames_verified = 0;
  int64_t recorded_ticks = -1, expected_frames = -1;
  if (!report || !events || !conf) {
    snprintf(
        err, sizeof err,
        "report/config/event paths must be new and parent directories exist");
    goto done;
  }
  eval_cfg_dump(&c, conf);
  fprintf(conf,
          "# oracle_ip=%s port=%d timeout_ms=%d seed_index=%d attempt_index=%d "
          "require_empty_inventory=%d\n",
          ip, port, timeout, seed_index, attempt, require_empty);
  if (fclose(conf)) {
    conf = NULL;
    snprintf(err, sizeof err, "config log failed");
    goto done;
  }
  conf = NULL;
  if (initial_path) {
    if (oracle_initial_load(&initial, initial_path, c.seeds[seed_index],
                            require_empty, err, sizeof err))
      goto done;
    if (capture_path && initial.head.version != 2) {
      snprintf(err, sizeof err,
               "Oracle-derived capture requires v2 initial template");
      goto done;
    }
    if (c.report[0] == '/')
      snprintf(initial_blocks, sizeof initial_blocks, "%s.initial-blocks.u16le",
               c.report);
    else {
      char cwd[2048];
      if (!getcwd(cwd, sizeof cwd)) {
        snprintf(err, sizeof err, "working directory unavailable");
        goto done;
      }
      snprintf(initial_blocks, sizeof initial_blocks,
               "%s/%s.initial-blocks.u16le", cwd, c.report);
    }
    struct stat st;
    if (!lstat(initial_blocks, &st) || errno != ENOENT) {
      snprintf(err, sizeof err,
               "initial block output already exists or cannot be inspected");
      goto done;
    }
  }
  if (capture_path) {
    if (snprintf(initial_light, sizeof initial_light, "%s.light.u8",
                 initial_blocks) >= (int)sizeof initial_light) {
      snprintf(err, sizeof err, "initial light path too long");
      goto done;
    }
    struct stat st;
    if (!lstat(initial_light, &st) || errno != ENOENT) {
      snprintf(err, sizeof err,
               "initial light output already exists or cannot be inspected");
      goto done;
    }
  }
  o = eval_oracle_open(ip, port, timeout, 1, prefix, err, sizeof err);
  if (!o)
    goto done;
  if (eval_oracle_set_step_command(o, "policy_step") ||
      eval_oracle_command(o,
                          "{\"cmd\":\"policy_lock\",\"action\":{\"cam\":1}}\n",
                          1, err, sizeof err))
    goto done;
  locked = 1;
  const EvalOracleReceipt *r = eval_oracle_receipt(o);
  if (!r || !r->policy_locked || !r->have_ticks) {
    snprintf(err, sizeof err,
             "policy_lock did not return locked authoritative tick receipt");
    goto done;
  }
  start_player = last_player = r->player_tick;
  start_server = last_server = r->server_tick;
  if (!r->have_world_seed || r->world_seed != c.seeds[seed_index]) {
    snprintf(err, sizeof err,
             "Oracle world seed does not match configured evaluation seed");
    goto done;
  }
  if ((require_empty && r->inventory_total) || goal(r) || r->obs.dead) {
    snprintf(
        err, sizeof err,
        "initial state violates empty inventory/no target/alive requirement");
    goto done;
  }
  EvalOracleReceipt expected_initial = *r;
  if (initial_path) {
    RlSnapHead *h = &initial.head;
    char request[2048];
    snprintf(request, sizeof request,
             "{\"cmd\":\"policy_initialize\",\"action\":{\"x\":%.17g,\"y\":%."
             "17g,\"z\":%.17g,\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,\"yaw\":%."
             "9g,\"pitch\":%.9g,\"on_ground\":%d,\"fall_distance\":%.9g,"
             "\"food\":%d,\"health\":%.9g}}\n",
             h->px + h->ox, h->py, h->pz + h->oz, h->mx, h->my, h->mz, h->yaw,
             h->pitch, h->on_ground, h->fall_distance, h->food, h->health);
    if (eval_oracle_command(o, request, 1, err, sizeof err))
      goto done;
    r = eval_oracle_receipt(o);
    if (!r->policy_locked || !r->have_ticks || r->player_tick != start_player ||
        r->server_tick != start_server || r->obs.x != h->px + h->ox ||
        r->obs.y != h->py || r->obs.z != h->pz + h->oz ||
        r->obs.yaw != h->yaw || r->obs.pitch != h->pitch || !r->have_physics ||
        r->vx != h->mx || r->vy != h->my || r->vz != h->mz ||
        r->food != h->food || r->health != h->health ||
        r->fall_distance != h->fall_distance || r->on_ground != h->on_ground) {
      snprintf(err, sizeof err,
               "initialized pose or frozen clocks do not match snapshot");
      goto done;
    }
    for (int slot = 0; slot < 36; slot++)
      for (int k = 0; k < 3; k++)
        if (r->inventory[slot][k] != h->inv[slot][k]) {
          snprintf(err, sizeof err,
                   "initial inventory differs at slot %d field %d", slot, k);
          goto done;
        }
    if (h->inv[36][0] || h->inv[36][1]) {
      snprintf(
          err, sizeof err,
          "snapshot offhand is not empty; Oracle receipt cannot verify it");
      goto done;
    }
    expected_initial = *r;
    char *block_request = NULL;
    size_t size = 0;
    FILE *mem = open_memstream(&block_request, &size);
    if (!mem) {
      snprintf(err, sizeof err, "block request allocation failed");
      goto done;
    }
    fprintf(mem,
            "{\"cmd\":\"getblocks_locked\",\"action\":{\"x0\":%d,\"y0\":%d,"
            "\"z0\":%d,\"x1\":%d,\"y1\":%d,\"z1\":%d,\"file\":",
            h->rx0, h->ry0, h->rz0, h->rx0 + h->rnx - 1, h->ry0 + h->rny - 1,
            h->rz0 + h->rnz - 1);
    json_string(mem, initial_blocks);
    if (capture_path) {
      fputs(",\"light_file\":", mem);
      json_string(mem, initial_light);
    }
    fputs("}}\n", mem);
    if (fclose(mem)) {
      free(block_request);
      snprintf(err, sizeof err, "block request serialization failed");
      goto done;
    }
    int br = eval_oracle_command(o, block_request, 0, err, sizeof err);
    free(block_request);
    if (br)
      goto done;
    int64_t bytes;
    if (eval_oracle_control_integer(o, "bytes", &bytes) ||
        bytes != (int64_t)initial.cells * 2) {
      snprintf(err, sizeof err, "Oracle block receipt byte count mismatch");
      goto done;
    }
    int compare_rc =
        oracle_initial_compare(&initial, initial_blocks, err, sizeof err);
    template_compared = initial.compared;
    template_mismatches = initial.mismatches;
    template_first[0] = initial.first_x;
    template_first[1] = initial.first_y;
    template_first[2] = initial.first_z;
    template_first[3] = (int)initial.first_snapshot;
    template_first[4] = (int)initial.first_oracle;
    if (compare_rc && (!capture_path || !initial.compared))
      goto done;
    if (capture_path) {
      int64_t light_bytes;
      if (eval_oracle_control_integer(o, "light_bytes", &light_bytes) ||
          light_bytes != (int64_t)initial.cells) {
        snprintf(err, sizeof err, "Oracle light receipt byte count mismatch");
        goto done;
      }
      if (oracle_fixture_write(initial_path, initial_blocks, initial_light,
                               capture_path, err, sizeof err))
        goto done;
      captured = 1;
      oracle_initial_free(&initial);
      if (oracle_initial_load(&initial, capture_path, c.seeds[seed_index],
                              require_empty, err, sizeof err) ||
          oracle_initial_compare(&initial, initial_blocks, err, sizeof err))
        goto done;
      err[0] = 0; /* Original mismatch is preserved separately, never called
                     parity. */
    }
  }
  if (eval_oracle_observe(o, err, sizeof err))
    goto done;
  r = eval_oracle_receipt(o);
  if (initial_boundary(r, &expected_initial, "capture/pre-rollout", err,
                       sizeof err))
    goto done;
  if (tape) {
    char *request = NULL;
    size_t request_size = 0;
    FILE *mem = open_memstream(&request, &request_size);
    if (!mem) {
      snprintf(err, sizeof err, "tape request allocation failed");
      goto done;
    }
    fputs("{\"cmd\":\"recstart\",\"action\":{\"file\":", mem);
    json_string(mem, tape);
    fputs(",\"frames_every\":1,\"dig_trace\":1,\"contract\":1}}\n", mem);
    if (fclose(mem)) {
      free(request);
      snprintf(err, sizeof err, "tape request serialization failed");
      goto done;
    }
    int tr = eval_oracle_command(o, request, 0, err, sizeof err);
    free(request);
    if (tr)
      goto done;
    recording = 1;
    if (eval_oracle_observe(o, err, sizeof err))
      goto done;
    r = eval_oracle_receipt(o);
    if (initial_boundary(r, &expected_initial, "recstart", err, sizeof err))
      goto done;
  }
  uint8_t planes[ENV_N_CH * ENV_NPIX],
      prior[ENV_N_PLANES * ENV_NPIX] = {0}, scratch[ENV_N_PLANES * ENV_NPIX],
                           have_prior = 0;
  float scal6[ENV_SCAL], pose[ENV_POSE], scalars[POL_SCAL], value;
  int status[ENV_STATUS], ep_dec = 0;
  double row[13];
  int32_t noop[9] = {1, 1, 1, 0, 0, 0, 0, 0, 0};
  acts_to_rows_config(&c.policy, noop, 1, row);
  for (int j = 0; j < c.action_repeat; j++) {
    if (step_locked(o, row, err, sizeof err))
      goto done;
    r = eval_oracle_receipt(o);
    last_player = r->player_tick;
    last_server = r->server_tick;
    tick_log(events, r, -1, j, 1);
    if (r->obs.dead || goal(r)) {
      snprintf(err, sizeof err,
               "dead or goal already reached during noop burn-in");
      goto done;
    }
  }
  post_burn = last_player;
  for (int dec = 0; dec < ep_lim; dec++) {
    r = eval_oracle_receipt(o);
    eval_magma_fill_policy(&r->obs, NULL, NULL, NULL, pose, status, scal6);
    pack_obs_config(&c.policy, r->obs.cam, r->obs.depth, r->obs.edge, scal6,
                    pose, status, &ep_dec, ep_lim, &have_prior, prior, 1,
                    planes, scalars, scratch);
    float *real_logits = logits + (size_t)lane * NN_N_LOGITS;
    if (nn_forward(nn, planes, scalars, 1, real_logits, &value) ||
        !isfinite(value)) {
      snprintf(err, sizeof err, "NN forward failed/nonfinite: %s",
               nn_last_error());
      goto done;
    }
    for (int k = 0; k < NN_N_LOGITS; k++)
      if (!isfinite(real_logits[k])) {
        snprintf(err, sizeof err, "nonfinite policy logits");
        goto done;
      }
    if (nn_sample(nn, logits, n,
                  c.deterministic ? NN_SAMPLE_GREEDY : NN_SAMPLE_GUMBEL, acts,
                  logp, NULL) ||
        !isfinite(logp[lane])) {
      snprintf(err, sizeof err, "NN sampling failed/nonfinite: %s",
               nn_last_error());
      goto done;
    }
    int32_t *action = acts + (size_t)lane * POL_HEADS;
    acts_to_rows_config(&c.policy, action, 1, row);
    uint64_t ph = hash_bytes(UINT64_C(0xcbf29ce484222325), planes,
                             sizeof planes),
             sh = hash_bytes(UINT64_C(0xcbf29ce484222325), scalars,
                             sizeof scalars);
    fprintf(
        events,
        "{\"kind\":\"decision\",\"decision\":%d,\"rng_lane\":%d,\"player_tick_"
        "before\":%" PRId64 ",\"server_tick_before\":%" PRId64
        ",\"planes_fnv64\":\"%016" PRIx64 "\",\"scalars_fnv64\":\"%016" PRIx64
        "\",\"sample_logp\":%.9g,\"value\":%.9g,\"heads\":[",
        dec, lane, r->player_tick, r->server_tick, ph, sh, logp[lane], value);
    for (int k = 0; k < POL_HEADS; k++)
      fprintf(events, "%s%d", k ? "," : "", action[k]);
    fputs("],\"act13\":[", events);
    for (int k = 0; k < 13; k++)
      fprintf(events, "%s%.17g", k ? "," : "", row[k]);
    fputs("],\"logits\":[", events);
    for (int k = 0; k < NN_N_LOGITS; k++)
      fprintf(events, "%s%.9g", k ? "," : "", real_logits[k]);
    fputs("],\"head_probabilities\":[", events);
    for (int h = 0; h < NN_N_HEAD; h++) {
      int off = NN_HEAD_OFF[h], w = NN_HEAD_WIDTHS[h];
      double max = real_logits[off], sum = 0;
      for (int k = 1; k < w; k++)
        if (real_logits[off + k] > max)
          max = real_logits[off + k];
      for (int k = 0; k < w; k++)
        sum += exp(real_logits[off + k] - max);
      for (int k = 0; k < w; k++)
        fprintf(events, "%s%.17g", h || k ? "," : "",
                exp(real_logits[off + k] - max) / sum);
    }
    fputs("]}\n", events);
    if (fflush(events) || ferror(events)) {
      snprintf(err, sizeof err, "decision log failed");
      goto done;
    }
    decisions = dec + 1;
    for (int j = 0; j < c.action_repeat; j++) {
      if (j) {
        row[2] = row[3] = 0;
        row[10] = -1;
        row[11] = row[12] = 0;
      }
      if (step_locked(o, row, err, sizeof err))
        goto done;
      r = eval_oracle_receipt(o);
      last_player = r->player_tick;
      last_server = r->server_tick;
      tick_log(events, r, dec, j, 0);
      if (goal(r)) {
        success = 1;
        achievement = last_player;
        reason = "goal";
        break;
      }
      if (r->obs.dead) {
        reason = "death";
        break;
      }
    }
    memcpy(prior, scratch, sizeof prior);
    have_prior = 1;
    ep_dec++;
    if (success || r->obs.dead)
      break;
  }
  if (!success && strcmp(reason, "death"))
    reason = "decision_limit";
  rc = success ? 0 : 3;
done:
  if (o && recording && eval_oracle_receipt(o)) {
    char tape_err[1024] = "";
    expected_frames = last_player - start_player;
    int stop = eval_oracle_command(o, "{\"cmd\":\"recstop\",\"action\":{}}\n",
                                   0, tape_err, sizeof tape_err);
    if (!stop)
      stop = eval_oracle_control_integer(o, "ticks", &recorded_ticks);
    if (!stop && recorded_ticks != expected_frames) {
      snprintf(tape_err, sizeof tape_err,
               "recorded tick count mismatch: expected %" PRId64
               " received %" PRId64,
               expected_frames, recorded_ticks);
      stop = -1;
    }
    if (!stop)
      stop = tape_frames(tape, frames_dir, expected_frames, tape_err,
                         sizeof tape_err);
    if (stop) {
      if (rc != 2)
        snprintf(err, sizeof err, "tape verification failed: %.950s",
                 *tape_err ? tape_err : "missing stop tick count");
      rc = 2;
      reason = "infrastructure_error";
    } else
      frames_verified = 1;
  }
  if (o && locked) {
    char unlock_err[256];
    if (eval_oracle_command(o, "{\"cmd\":\"policy_unlock\",\"action\":{}}\n", 0,
                            unlock_err, sizeof unlock_err) &&
        rc != 2) {
      snprintf(err, sizeof err, "unlock failed: %s", unlock_err);
      rc = 2;
      reason = "infrastructure_error";
    }
  }
  eval_oracle_close(o);
  uint64_t after_hash = 0, after_bytes = 0;
  if (file_hash(c.checkpoint, &after_hash, &after_bytes) ||
      after_hash != ckhash || after_bytes != ckbytes) {
    snprintf(err, sizeof err, "checkpoint changed during episode");
    rc = 2;
    reason = "infrastructure_error";
  }
  if (events && (fflush(events) || ferror(events))) {
    snprintf(err, sizeof err, "event log failed");
    rc = 2;
    reason = "infrastructure_error";
  }
  if (report) {
    fprintf(report,
            "{\"schema\":\"netherite.oracle_policy.v1\",\"success_item\":270,"
            "\"achieved\":%s,\"valid\":%s,\"reason\":",
            success ? "true" : "false", rc != 2 ? "true" : "false");
    json_string(report, reason);
    fputs(",\"error\":", report);
    json_string(report, err);
    fputs(",\"initial_snapshot\":", report);
    if (initial_path)
      json_string(report, initial_path);
    else
      fputs("null", report);
    fputs(",\"oracle_derived_fixture\":", report);
    if (captured)
      json_string(report, capture_path);
    else
      fputs("null", report);
    fputs(",\"oracle_derived_provenance\":", report);
    if (captured) {
      char p[3100];
      snprintf(p, sizeof p, "%s.provenance.json", capture_path);
      json_string(report, p);
    } else
      fputs("null", report);
    fputs(",\"comparison_fixture\":", report);
    if (initial_path)
      json_string(report, captured ? capture_path : initial_path);
    else
      fputs("null", report);
    fprintf(report,
            ",\"original_template_blocks_compared\":%s,\"original_template_"
            "block_mismatches\":%" PRIu64
            ",\"original_template_first_mismatch\":[%d,%d,%d,%d,%d]",
            template_compared ? "true" : "false", template_mismatches,
            template_first[0], template_first[1], template_first[2],
            template_first[3], template_first[4]);
    fputs(",\"initial_blocks\":", report);
    if (initial_path)
      json_string(report, initial_blocks);
    else
      fputs("null", report);
    fprintf(report,
            ",\"initial_blocks_compared\":%s,\"initial_block_cells\":%zu,"
            "\"initial_block_mismatches\":%" PRIu64
            ",\"initial_first_mismatch\":[%d,%d,%d,%u,%u]",
            initial.compared ? "true" : "false", initial.cells,
            initial.mismatches, initial.first_x, initial.first_y,
            initial.first_z, initial.first_snapshot, initial.first_oracle);
    fputs(",\"tape\":", report);
    if (tape)
      json_string(report, tape);
    else
      fputs("null", report);
    fputs(",\"frames_dir\":", report);
    if (tape)
      json_string(report, frames_dir);
    else
      fputs("null", report);
    fprintf(report,
            ",\"frames_every\":1,\"expected_frames\":%" PRId64
            ",\"recorded_ticks\":%" PRId64 ",\"frame_files_verified\":%s",
            expected_frames, recorded_ticks,
            frames_verified ? "true" : "false");
    fputs(",\"checkpoint\":", report);
    json_string(report, c.checkpoint);
    fprintf(report,
            ",\"checkpoint_fnv64\":\"%016" PRIx64
            "\",\"checkpoint_bytes\":%" PRIu64
            ",\"policy_contract\":\"%016" PRIx64
            "\",\"world_seed\":%d,\"seed_index\":%d,\"attempt_index\":%d,\"rng_"
            "seed\":%" PRIu64 ",\"rng_lane\":%d,\"decisions\":%d,\"action_"
            "repeat\":%d,\"initial_player_tick\":%" PRId64
            ",\"initial_server_tick\":%" PRId64
            ",\"post_burn_player_tick\":%" PRId64
            ",\"last_verified_player_tick\":%" PRId64
            ",\"last_verified_server_tick\":%" PRId64
            ",\"achievement_player_tick\":%" PRId64
            ",\"measured_total_ticks\":%" PRId64 "}\n",
            ckhash, ckbytes, policy_io_fingerprint(&c.policy),
            c.seeds[seed_index], seed_index, attempt, desc.config.rng_seed,
            lane, decisions, c.action_repeat, start_player, start_server,
            post_burn, last_player, last_server, achievement,
            rc != 2 && start_player >= 0 ? last_player - start_player : -1);
    if (fclose(report))
      rc = 2;
  }
  if (events)
    fclose(events);
  if (conf)
    fclose(conf);
  free(logits);
  free(logp);
  free(acts);
  nn_destroy(nn);
  oracle_initial_free(&initial);
  fprintf(stderr,
          "oracle-policy: %s report=%s decisions=%d measured_ticks=%" PRId64
          "%s%s\n",
          rc == 0   ? "goal achieved"
          : rc == 3 ? "goal not achieved"
                    : "failed",
          c.report, decisions,
          rc != 2 && start_player >= 0 ? last_player - start_player : -1,
          *err ? " error=" : "", err);
  return rc;
usage:
  free(args);
  fprintf(stderr,
          "oracle-policy: invalid arguments; eval --conf/--set plus "
          "--oracle-ip --oracle-port --timeout-ms --seed-index --attempt-index "
          "--require-empty-inventory --allow-legacy-contract --tape "
          "ABSOLUTE.jsonl\n");
  return 2;
config_error:
  fprintf(stderr, "oracle-policy: %s\n", err);
  return 2;
}
