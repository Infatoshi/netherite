#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "eval_oracle.h"
#include "port_parity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static const char *noop =
    "{\"cmd\":\"policy_step\",\"action\":{\"forward\":0,\"back\":0,\"left\":0,"
    "\"right\":0,\"dyaw\":0,\"dpitch\":0,\"jump\":0,\"sneak\":0,\"sprint\":0,"
    "\"attack\":0,\"use\":0,\"hotbar\":-1,\"craft\":-1,\"interact\":0,"
    "\"smelt\":0,\"cam\":1}}\n";
static const char *action =
    "{\"cmd\":\"policy_step\",\"action\":{\"forward\":1,\"back\":0,\"left\":1,"
    "\"right\":0,\"dyaw\":15,\"dpitch\":-10,\"jump\":1,\"sneak\":0,\"sprint\":"
    "0,\"attack\":1,\"use\":1,\"hotbar\":4,\"craft\":3,\"interact\":1,"
    "\"smelt\":0,\"cam\":1}}\n";
static int mock(int argc, char **argv) {
  const char *snap = NULL, *frames = NULL;
  int pfd = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--snapshot-in"))
      snap = argv[++i];
    else if (!strcmp(argv[i], "--frames-out"))
      frames = argv[++i];
    else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
      const char *s = argv[++i];
      if (!strncmp(s, "port_parity_fd=", 15))
        pfd = atoi(s + 15);
    }
  }
  assert(snap && frames && pfd >= 0);
  assert(!mkdir(frames, 0700));
  FILE *pary = fdopen(pfd, "wb");
  assert(pary);
  EvalMagmaObs o = {0};
  o.magic = EM_MAGIC;
  BpParityRecord p;
  bp_record_init(&p, 0);
  assert(fwrite(&p, sizeof p, 1, pary) == 1);
  fflush(pary);
  assert(fwrite(&o, sizeof o, 1, stdout) == 1);
  fflush(stdout);
  char line[4096];
  int n = 0;
  while (fgets(line, sizeof line, stdin)) {
    n++;
    if (n == 2) {
      assert(strstr(line, "\"forward\":1") && strstr(line, "\"strafe\":-1") &&
             strstr(line, "\"dyaw\":15") && strstr(line, "\"dpitch\":-10") &&
             strstr(line, "\"craft\":3"));
    }
    o.tick = n;
    if (strstr(snap, "case2") && n == 2)
      o.tick++;
    if (n == 2)
      o.inv_counts[5] = 1;
    bp_record_init(&p, o.tick);
    assert(fwrite(&p, sizeof p, 1, pary) == 1);
    fflush(pary);
    if (!(strstr(snap, "case3") && n == 2)) {
      char file[2048];
      snprintf(file, sizeof file, "%s/frame_%06d.ppm", frames, n - 1);
      FILE *f = fopen(file, "wb");
      assert(f);
      fputs("P6\n1 1\n255\n", f);
      fputc(1, f);
      fputc(2, f);
      fputc(3, f);
      fclose(f);
    }
    if (strstr(snap, "case1") && n == 2) {
      fwrite(&o, 1, 100, stdout);
      fflush(stdout);
      fclose(pary);
      return 0;
    }
    assert(fwrite(&o, sizeof o, 1, stdout) == 1);
    fflush(stdout);
  }
  fclose(pary);
  return 0;
}
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--rl-bin"))
      return mock(argc, argv);
  assert(argc == 2);
  double a[13];
  char err[512];
  assert(eval_oracle_parse_request(action, a, err, sizeof err) == 1);
  assert(a[0] == 1 && a[1] == -1 && a[2] == 15 && a[3] == -10 && a[9] == 4 &&
         a[10] == 3);
  assert(eval_oracle_parse_request("{\"cmd\":\"unknown_step\",\"action\":{}}",
                                   a, err, sizeof err) < 0);
  char dir[] = "/tmp/netherite-oracle-replay-XXXXXX";
  assert(mkdtemp(dir));
  for (int mode = 0; mode < 7; mode++) {
    char req[1024], snap[1024], conf[1024], trace[1024];
    snprintf(req, sizeof req, "%s/requests%d.jsonl", dir, mode);
    snprintf(snap, sizeof snap, "%s/case%d.bsnp", dir, mode);
    snprintf(conf, sizeof conf, "%s/magma%d.conf", dir, mode);
    snprintf(trace, sizeof trace, "%s/trace%d", dir, mode);
    FILE *f = fopen(req, "w");
    assert(f);
    fputs("{\"cmd\":\"policy_lock\",\"action\":{\"cam\":1}}\n", f);
    fputs(noop, f);
    if (mode == 4)
      fwrite(action, 1, strlen(action) - 1, f);
    else if (mode == 5)
      fputs("{\"cmd\":\"policy_step\",\"action\":{}}\n", f);
    else if (mode == 6)
      fputs("{\"cmd\":\"step_unknown\",\"action\":{}}\n", f);
    else
      fputs(action, f);
    if (mode != 4)
      fputs("{\"cmd\":\"policy_unlock\",\"action\":{}}\n", f);
    fclose(f);
    f = fopen(snap, "wb");
    assert(f);
    RlSnapHead h = {0};
    memcpy(h.magic, "BSNP", 4);
    h.version = 2;
    h.seed = 10;
    assert(fwrite(&h, sizeof h, 1, f) == 1);
    fclose(f);
    f = fopen(conf, "w");
    assert(f);
    fputs("width=1\nheight=1\n", f);
    fclose(f);
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
      execl(argv[1], argv[1], "--requests", req, "--snapshot", snap,
            "--magma-bin", argv[0], "--magma-conf", conf, "--trace-dir", trace,
            "--seed", "10", (char *)NULL);
      _exit(127);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    assert(WEXITSTATUS(status) == (mode == 0 ? 0 : 2));
    if (mode < 4) {
      char path[1200];
      snprintf(path, sizeof path, "%s/replay.json", trace);
      f = fopen(path, "r");
      assert(f);
      char text[4096];
      assert(fgets(text, sizeof text, f));
      fclose(f);
      assert(strstr(text, mode == 0 ? "\"valid\":true" : "\"valid\":false"));
      assert(strstr(text, "\"expected_actions\":2"));
      if (mode == 0)
        assert(strstr(text, "\"executed_ticks\":2") &&
               strstr(text, "\"first_goal_action\":2"));
      const char *files[] = {"replay.json",   "magma.bolr",
                             "magma.pary",    "magma.stderr",
                             "actions.jsonl", "magma_state.jsonl"};
      for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", trace, files[i]);
        assert(!unlink(path));
      }
      for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/frames/frame_%06d.ppm", trace, i);
        if (!(mode == 3 && i == 1))
          assert(!unlink(path));
      }
      snprintf(path, sizeof path, "%s/frames", trace);
      assert(!rmdir(path));
      assert(!rmdir(trace));
    } else
      assert(access(trace, F_OK));
    assert(!unlink(req));
    assert(!unlink(snap));
    assert(!unlink(conf));
  }
  assert(!rmdir(dir));
  puts("oracle-replay-magma: same-action mapping/count, truncated BOLR/source, "
       "tick drift, missing frame, malformed/unknown request PASS");
  return 0;
}
