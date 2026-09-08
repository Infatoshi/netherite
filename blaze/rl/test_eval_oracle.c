#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "eval_oracle.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
static char *fixture(int ticks, int drift, uint64_t seq, uint64_t hash) {
  char *s = NULL;
  size_t n = 0;
  FILE *f = open_memstream(&s, &n);
  assert(f);
  fprintf(f,
          "{\"ok\":true,\"x\":8.5,\"y\":66,\"z\":8.5,\"yaw\":180,\"pitch\":0,"
          "\"dead\":false,\"hotbar_sel\":1,\"container\":0,\"inv_counts\":[2,0,"
          "0,0,0,0,0,0,0],\"inventory\":[{\"slot\":1,\"id\":17,\"count\":2}],"
          "\"coal\":[[1,64,2]],\"policy_action_seq\":%llu,\"policy_action_"
          "fnv64\":\"%llx\",\"time\":{\"total_time\":%llu}",
          (unsigned long long)seq, (unsigned long long)hash,
          (unsigned long long)(100 + seq));
  if (ticks)
    fprintf(f, ",\"player_tick\":%llu,\"server_tick\":%llu",
            (unsigned long long)(10 + seq),
            (unsigned long long)(100 + seq + drift));
  const char *names[] = {"cam", "depth", "edge"};
  for (int k = 0; k < 3; k++) {
    fprintf(f, ",\"%s\":[", names[k]);
    for (int j = 0; j < EM_NPIX; j++)
      fprintf(f, "%s%d", j ? "," : "", k == 0 ? 17 : k == 1 ? 8 : 0);
    fputc(']', f);
  }
  fputs("}\n", f);
  assert(!fclose(f));
  return s;
}
static void parser_tests(void) {
  char e[256];
  EvalOracleReceipt r;
  char *s = fixture(1, 0, 0, UINT64_C(0xcbf29ce484222325));
  assert(!eval_oracle_parse(s, &r, e, sizeof e));
  assert(r.have_ticks && r.obs.cam[2303] == 17 && r.obs.hotbar_ids[1] == 17 &&
         r.obs.hotbar_counts[1] == 2 && r.obs.coal[0][1] == 64);
  size_t n = strlen(s);
  s[n - 2] = 0;
  assert(eval_oracle_parse(s, &r, e, sizeof e));
  s[n - 2] = '}';
  assert(eval_oracle_parse("{\"ok\":false,\"error\":\"no world\"}", &r, e,
                           sizeof e));
  assert(eval_oracle_parse("{\"ok\":true,\"x\":NaN}", &r, e, sizeof e));
  const char *bad[] = {"",
                       "{",
                       "[",
                       "{\"x\"",
                       "{\"x\":",
                       "{\"x\":\"\\u",
                       "{\"x\":\"\\",
                       "{\"x\":-",
                       "{\"x\":1e",
                       "{\"x\":1.}",
                       "{\"x\":01}",
                       "{} trailing"};
  for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++)
    assert(eval_oracle_parse(bad[k], &r, e, sizeof e));
  char *q = strstr(s, "8.5");
  memcpy(q, "1e9", 3);
  assert(!eval_oracle_parse(s, &r, e, sizeof e));
  memcpy(q, "nan", 3);
  assert(eval_oracle_parse(s, &r, e, sizeof e));
  memcpy(q, "8.5", 3);
  q = strstr(s, "\"edge\":[");
  q[8] = '2';
  assert(eval_oracle_parse(s, &r, e, sizeof e));
  free(s);
  s = fixture(0, 0, 0, 0);
  assert(!eval_oracle_parse(s, &r, e, sizeof e));
  assert(!r.have_ticks);
  free(s);
  assert(eval_oracle_parse("{\"ok\":true,\"x\":1e999}", &r, e, sizeof e));
}
/* mode0 valid; 1 missing strict ticks; 2 drift; 3 bad ack; 4 truncated;
 * 5 malformed; 6 deadline; 7 wrong plane length. */
static void transport_test(const char *dir, int mode) {
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
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    int fd = accept(sock, NULL, NULL);
    if (fd < 0)
      _exit(2);
    close(sock);
    FILE *f = fdopen(fd, "r+");
    if (!f)
      _exit(3);
    char req[2048];
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int j = 0; j < 2; j++) {
      if (!fgets(req, sizeof req, f))
        break;
      if (j) {
        if (!strstr(req, "\"cmd\":\"policy_step\"") ||
            !strstr(req, "\"forward\":0,\"back\":1,\"left\":1,\"right\":0"))
          _exit(5);
        char *start = strstr(req, "\"action\":") + 9;
        char *end = strrchr(req, '}');
        if (!start || !end)
          _exit(4);
        for (char *p = start; p < end; p++) {
          hash ^= (unsigned char)*p;
          hash *= UINT64_C(0x100000001b3);
        }
      }
      char *s = fixture(mode != 1, j && mode == 2 ? 2 : 0, (uint64_t)j,
                        hash + (j && mode == 3));
      if (mode == 6) {
        sleep(1);
        free(s);
        break;
      }
      if (mode == 4) {
        fwrite(s, 1, strlen(s) / 2, f);
        fflush(f);
        free(s);
        break;
      }
      if (mode == 5) {
        fputs("{invalid}\n", f);
        fflush(f);
        free(s);
        break;
      }
      if (mode == 7) {
        char *p = strstr(s, "\"cam\":[");
        p[7] = ']';
      }
      fputs(s, f);
      fflush(f);
      free(s);
    }
    fclose(f);
    _exit(0);
  }
  close(sock);
  char prefix[1024], err[256];
  snprintf(prefix, sizeof prefix, "%s/case%d", dir, mode);
  EvalOracle *o =
      eval_oracle_open("127.0.0.1", ntohs(a.sin_port), mode == 6 ? 100 : 3000,
                       1, prefix, err, sizeof err);
  assert(o);
  assert(!eval_oracle_set_step_command(o, "policy_step"));
  int rc = eval_oracle_observe(o, err, sizeof err);
  if (mode >= 4) {
    assert(rc);
    assert(!eval_oracle_receipt(o));
  } else {
    assert(!rc);
    double act[13] = {0};
    act[0] = -1;
    act[1] = 1;
    act[9] = -1;
    act[10] = -1;
    rc = eval_oracle_step(o, act, err, sizeof err);
    if (mode == 0)
      assert(!rc);
    else
      assert(rc);
  }
  eval_oracle_close(o);
  int status;
  assert(waitpid(pid, &status, 0) == pid);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  const char *suffix[] = {".requests.jsonl", ".responses.jsonl", ".partial"};
  for (int k = 0; k < 3; k++) {
    char p[1100];
    snprintf(p, sizeof p, "%s%s", prefix, suffix[k]);
    unlink(p);
  }
}
int main(void) {
  parser_tests();
  char dir[] = "/tmp/netherite-oracle-test-XXXXXX";
  assert(mkdtemp(dir));
  for (int i = 0; i < 8; i++)
    transport_test(dir, i);
  assert(!rmdir(dir));
  puts("eval_oracle: parser and 8 local transport scenarios PASS");
  return 0;
}
