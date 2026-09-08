/* Local protocol fixtures only. This never connects to a Minecraft client. */
#define main oracle_transport_suite_main
#include "test_eval_oracle.c"
#undef main
#include "blaze_snapshot.h"
#include "nn.h"
#include "obs_config.h"
#include <fcntl.h>
#include <sys/stat.h>

static char *replace(const char *s, const char *old, const char *newtext) {
  const char *at = strstr(s, old);
  assert(at);
  size_t pre = (size_t)(at - s),
         n = strlen(s) - strlen(old) + strlen(newtext) + 1;
  char *r = malloc(n);
  assert(r);
  memcpy(r, s, pre);
  strcpy(r + pre, newtext);
  strcpy(r + pre + strlen(newtext), at + strlen(old));
  return r;
}
static void run_driver(const char *binary, const char *dir, const char *ckpt,
                       int mode) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  assert(sock >= 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(!bind(sock, (struct sockaddr *)&a, sizeof a));
  socklen_t len = sizeof a;
  assert(!getsockname(sock, (struct sockaddr *)&a, &len));
  assert(!listen(sock, 1));
  char report[1024], port[32], tape[1024], frames[1024], snapshot_path[1024];
  snprintf(snapshot_path, sizeof snapshot_path, "%s/initial%d.bsnp", dir, mode);
  if (mode >= 9) {
    RlSnapHead h = {0};
    memcpy(h.magic, "BSNP", 4);
    h.version = 11;
    h.seed = 10;
    h.px = 8.5;
    h.py = 66;
    h.pz = 8.5;
    h.yaw = 180;
    h.health = 20;
    h.food = 20;
    h.on_ground = 1;
    h.rx0 = h.rz0 = 8;
    h.ry0 = 65;
    h.rnx = h.rny = h.rnz = 2;
    h.inv[1][0] = 17;
    h.inv[1][1] = 2;
    FILE *sf = fopen(snapshot_path, "wb");
    assert(sf);
    assert(fwrite(&h, sizeof h, 1, sf) == 1);
    for (uint16_t k = 0; k < 8; k++)
      assert(fwrite(&k, 2, 1, sf) == 1);
    fclose(sf);
  }
  snprintf(tape, sizeof tape, "%s/capture%d%s.jsonl", dir, mode,
           mode == 8 ? "\"quote" : "");
  snprintf(frames, sizeof frames, "%.*s_frames", (int)strlen(tape) - 6, tape);
  snprintf(report, sizeof report, "%s/driver%d.json", dir, mode);
  snprintf(port, sizeof port, "%d", ntohs(a.sin_port));
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    close(sock);
    execl(binary, binary, "--checkpoint", ckpt, "--report", report, "--backend",
          "cpu", "--seeds", "10", "--set", "success_item=270", "--set",
          "episode_decisions=1", "--set",
          mode == 4 ? "action_repeat=2" : "action_repeat=1", "--set",
          mode == 4 ? "deterministic=0" : "deterministic=1", "--set",
          mode == 4 ? "tries=2" : "tries=1", "--attempt-index",
          mode == 4 ? "1" : "0", "--oracle-port", port,
          "--require-empty-inventory", mode == 1 ? "1" : "0",
          mode >= 5 ? "--tape" : NULL, mode >= 5 ? tape : NULL,
          mode >= 9 ? "--initial-snapshot" : NULL,
          mode >= 9 ? snapshot_path : NULL, (char *)NULL);
    _exit(127);
  }
  int fd = accept(sock, NULL, NULL);
  assert(fd >= 0);
  close(sock);
  FILE *f = fdopen(fd, "r+");
  assert(f);
  char req[4096];
  int recording = 0, stopped = 0, initialized = 0, blocks_done = 0;
  uint64_t seq = 0, hash = UINT64_C(0xcbf29ce484222325);
  while (fgets(req, sizeof req, f)) {
    if (strstr(req, "policy_initialize")) {
      assert(mode >= 9 && seq == 0 && !initialized);
      initialized = 1;
    }
    if (strstr(req, "getblocks_locked")) {
      assert(initialized && !blocks_done && seq == 0);
      blocks_done = 1;
      char path[1200];
      snprintf(path, sizeof path, "%s.initial-blocks.u16le", report);
      FILE *bf = fopen(path, "wb");
      assert(bf);
      for (int y = 0; y < 2; y++)
        for (int z = 0; z < 2; z++)
          for (int x = 0; x < 2; x++) {
            unsigned char b[2] = {(unsigned char)((x * 2 + y) * 2 + z), 0};
            if (mode == 10 && !x && !y && !z)
              b[0] = 255;
            assert(fwrite(b, 1, 2, bf) == 2);
          }
      fclose(bf);
      fputs("{\"ok\":true,\"bytes\":16}\n", f);
      fflush(f);
      continue;
    }
    if (strstr(req, "recstart")) {
      assert(mode >= 5 && !recording && seq == 0);
      if (mode >= 9)
        assert(initialized && blocks_done);
      if (mode == 8)
        assert(strstr(req, "\\\"quote"));
      assert(strstr(req, "\"frames_every\":1") &&
             strstr(req, "\"dig_trace\":1") && strstr(req, "\"contract\":1"));
      recording = 1;
      FILE *tf = fopen(tape, "w");
      assert(tf);
      fputs("{\"header\":1}\n", tf);
      fclose(tf);
      assert(!mkdir(frames, 0700));
      fputs("{\"ok\":true}\n", f);
      fflush(f);
      continue;
    }
    if (strstr(req, "recstop")) {
      assert(recording && !stopped);
      stopped = 1;
      for (uint64_t i = 0; i < seq; i++) {
        if (mode == 6 && i == 1)
          continue;
        char path[1200];
        snprintf(path, sizeof path, "%s/f_%06llu.png", frames,
                 (unsigned long long)i);
        FILE *pf = fopen(path, "wb");
        assert(pf);
        unsigned char png[36] = {137, 80,  78,  71,  13,  10,  26,  10, 0,
                                 0,   0,   13,  'I', 'H', 'D', 'R', 0,  0,
                                 0,   1,   0,   0,   0,   1,   0,   0,  0,
                                 0,   'I', 'E', 'N', 'D', 174, 66,  96, 130};
        assert(fwrite(png, 1, sizeof png, pf) == sizeof png);
        fclose(pf);
      }
      fprintf(f, "{\"ok\":true,\"ticks\":%llu}\n",
              (unsigned long long)(seq + (mode == 7)));
      fflush(f);
      continue;
    }
    if (strstr(req, "policy_unlock")) {
      if (mode >= 5 && mode != 10)
        assert(stopped);
      if (mode == 10)
        assert(!recording && seq == 0 && blocks_done);
      fputs("{\"ok\":true,\"policy_locked\":false}\n", f);
      fflush(f);
      break;
    }
    if (strstr(req, "policy_step")) {
      char *start = strstr(req, "\"action\":") + 9, *end = strrchr(req, '}');
      for (char *p = start; p < end; p++) {
        hash ^= (unsigned char)*p;
        hash *= UINT64_C(0x100000001b3);
      }
      seq++;
    } else
      assert(strstr(req, "policy_initialize") || strstr(req, "policy_lock") ||
             strstr(req, "\"cmd\":\"obs\""));
    char *s = fixture(1, mode == 3 && seq == 2 ? 1 : 0, seq, hash),
         *v = replace(s, "{\"ok\":true,",
                      "{\"ok\":true,\"policy_locked\":true,\"world_seed\":10,");
    free(s);
    s = v;
    if (((mode == 0 || mode >= 5) && seq == 2) || (mode == 4 && seq == 4)) {
      v = replace(s, "[2,0,0,0,0,0,0,0,0]", "[0,0,0,0,0,1,0,0,0]");
      free(s);
      s = v;
      v = replace(s, "\"id\":17,\"count\":2", "\"id\":270,\"count\":1");
      free(s);
      s = v;
    }
    fputs(s, f);
    fflush(f);
    free(s);
  }
  fclose(f);
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status));
  int want = (mode == 0 || mode == 4 || mode == 5 || mode == 8 || mode == 9) ? 0
             : mode == 2 ? 3
                         : 2;
  assert(WEXITSTATUS(status) == want);
  FILE *r = fopen(report, "r");
  assert(r);
  char buf[8192];
  assert(fgets(buf, sizeof buf, r));
  fclose(r);
  assert(strstr(buf, (mode == 0 || mode == 4 || (mode >= 5 && mode <= 9))
                         ? "\"achieved\":true"
                         : "\"achieved\":false"));
  assert(strstr(buf, mode == 0 || mode == 2 || mode == 4 || mode == 5 ||
                             mode == 8 || mode == 9
                         ? "\"valid\":true"
                         : "\"valid\":false"));
  if (mode >= 5 && mode <= 9) {
    assert(stopped);
    assert(strstr(buf, mode == 5 || mode == 8 || mode == 9
                           ? "\"frame_files_verified\":true"
                           : "\"frame_files_verified\":false"));
    for (uint64_t i = 0; i < seq; i++) {
      char path[1200];
      snprintf(path, sizeof path, "%s/f_%06llu.png", frames,
               (unsigned long long)i);
      if (!(mode == 6 && i == 1))
        assert(!unlink(path));
    }
    assert(!rmdir(frames));
    assert(!unlink(tape));
  }
  if (mode >= 9) {
    assert(initialized && blocks_done);
    assert(strstr(buf, "\"initial_blocks_compared\":true"));
    assert(strstr(buf, mode == 9 ? "\"initial_block_mismatches\":0"
                                 : "\"initial_block_mismatches\":1"));
    char path[1200];
    snprintf(path, sizeof path, "%s.initial-blocks.u16le", report);
    assert(!unlink(path));
    assert(!unlink(snapshot_path));
  }
  const char *suffix[] = {"", ".conf", ".decisions.jsonl",
                          ".oracle.requests.jsonl", ".oracle.responses.jsonl"};
  for (int i = 0; i < 5; i++) {
    char path[1200];
    snprintf(path, sizeof path, "%s%s", report, suffix[i]);
    assert(!unlink(path));
  }
}
int main(int argc, char **argv) {
  assert(argc == 2);
  char dir[] = "/tmp/netherite-oracle-driver-XXXXXX";
  assert(mkdtemp(dir));
  char ckpt[1024], meta[1100];
  snprintf(ckpt, sizeof ckpt, "%s/mock.bin", dir);
  NnCreate desc = {0};
  desc.backend = NN_BACKEND_CPU;
  desc.max_n = 1;
  desc.config = nn_config_default();
  Nn *nn = nn_create(&desc);
  assert(nn);
  assert(!nn_save(nn, ckpt));
  nn_destroy(nn);
  PolicyIoConfig c;
  policy_io_default(&c);
  char err[512];
  assert(!policy_io_checkpoint_write(ckpt, &c, err, sizeof err));
  for (int mode = 0; mode < 11; mode++)
    run_driver(argv[1], dir, ckpt, mode);
  assert(!unlink(ckpt));
  snprintf(meta, sizeof meta, "%s.policy.conf", ckpt);
  assert(!unlink(meta));
  assert(!rmdir(dir));
  puts("oracle-policy: local mock success, empty-start rejection, limit, "
       "tick-drift PASS");
  return 0;
}
