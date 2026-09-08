/* Local protocol fixtures only. This never connects to a Minecraft client. */
#define main oracle_transport_suite_main
#include "test_eval_oracle.c"
#undef main
#include "nn.h"
#include "obs_config.h"
#include <fcntl.h>

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
  char report[1024], port[32];
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
          "--require-empty-inventory", mode == 1 ? "1" : "0", (char *)NULL);
    _exit(127);
  }
  int fd = accept(sock, NULL, NULL);
  assert(fd >= 0);
  close(sock);
  FILE *f = fdopen(fd, "r+");
  assert(f);
  char req[4096];
  uint64_t seq = 0, hash = UINT64_C(0xcbf29ce484222325);
  while (fgets(req, sizeof req, f)) {
    if (strstr(req, "policy_unlock")) {
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
      assert(strstr(req, "policy_lock"));
    char *s = fixture(1, mode == 3 && seq == 2 ? 1 : 0, seq, hash),
         *v = replace(s, "{\"ok\":true,",
                      "{\"ok\":true,\"policy_locked\":true,\"world_seed\":10,");
    free(s);
    s = v;
    if ((mode == 0 && seq == 2) || (mode == 4 && seq == 4)) {
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
  int want = (mode == 0 || mode == 4) ? 0 : mode == 2 ? 3 : 2;
  assert(WEXITSTATUS(status) == want);
  FILE *r = fopen(report, "r");
  assert(r);
  char buf[8192];
  assert(fgets(buf, sizeof buf, r));
  fclose(r);
  assert(strstr(buf, (mode == 0 || mode == 4) ? "\"achieved\":true"
                                              : "\"achieved\":false"));
  assert(strstr(buf, mode == 0 || mode == 2 || mode == 4 ? "\"valid\":true"
                                                         : "\"valid\":false"));
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
  for (int mode = 0; mode < 5; mode++)
    run_driver(argv[1], dir, ckpt, mode);
  assert(!unlink(ckpt));
  snprintf(meta, sizeof meta, "%s.policy.conf", ckpt);
  assert(!unlink(meta));
  assert(!rmdir(dir));
  puts("oracle-policy: local mock success, empty-start rejection, limit, "
       "tick-drift PASS");
  return 0;
}
