#define _POSIX_C_SOURCE 200809L
#include "eval_oracle.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define MAX_JSON (1024 * 1024)
#define MAX_TOK 32768
/* Small strict JSON recognizer. Tokens retain subtree end offsets, allowing
 * field lookup without substring matching or accepting nested impostors. */
typedef struct {
  int a, b, end, n;
  char kind;
} Tok;
typedef struct {
  const char *s;
  int p, n;
  Tok *t;
} Parser;
static int fail(char *e, int n, const char *s) {
  if (e && n > 0)
    snprintf(e, (size_t)n, "%s", s);
  return -1;
}
static void ws(Parser *p) {
  while (p->s[p->p] == ' ' || p->s[p->p] == '\t' || p->s[p->p] == '\r' ||
         p->s[p->p] == '\n')
    p->p++;
}
static int value(Parser *p, int depth) {
  if (depth > 64 || p->n == MAX_TOK)
    return -1;
  ws(p);
  int i = p->n++;
  Tok *t = &p->t[i];
  t->a = p->p;
  t->n = 0;
  t->kind = p->s[p->p];
  char c = p->s[p->p];
  if (c == '{' || c == '[') {
    p->p++;
    ws(p);
    char close = c == '{' ? '}' : ']';
    if (p->s[p->p] != close)
      for (;;) {
        if (c == '{') {
          if (p->s[p->p] != '"' || value(p, depth + 1) < 0)
            return -1;
          ws(p);
          if (p->s[p->p++] != ':')
            return -1;
        }
        if (value(p, depth + 1) < 0)
          return -1;
        t->n++;
        ws(p);
        if (p->s[p->p] != ',')
          break;
        p->p++;
        ws(p);
      }
    if (p->s[p->p++] != close)
      return -1;
  } else if (c == '"') {
    p->p++;
    for (;;) {
      unsigned char x = (unsigned char)p->s[p->p++];
      if (!x || x < 32)
        return -1;
      if (x == '"')
        break;
      if (x == '\\') {
        x = (unsigned char)p->s[p->p++];
        if (x == 'u') {
          for (int k = 0; k < 4; k++) {
            char h = p->s[p->p++];
            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') ||
                  (h >= 'A' && h <= 'F')))
              return -1;
          }
        } else if (!x || !strchr("\"\\/bfnrt", x))
          return -1;
      }
    }
  } else if (c == 't' || c == 'f' || c == 'n') {
    const char *lit = c == 't' ? "true" : c == 'f' ? "false" : "null";
    size_t n = strlen(lit);
    if (strncmp(p->s + p->p, lit, n))
      return -1;
    p->p += (int)n;
  } else {
    if (c == '-')
      p->p++;
    if (p->s[p->p] == '0')
      p->p++;
    else {
      if (p->s[p->p] < '1' || p->s[p->p] > '9')
        return -1;
      while (p->s[p->p] >= '0' && p->s[p->p] <= '9')
        p->p++;
    }
    if (p->s[p->p] == '.') {
      p->p++;
      int start = p->p;
      while (p->s[p->p] >= '0' && p->s[p->p] <= '9')
        p->p++;
      if (start == p->p)
        return -1;
    }
    if (p->s[p->p] == 'e' || p->s[p->p] == 'E') {
      p->p++;
      if (p->s[p->p] == '+' || p->s[p->p] == '-')
        p->p++;
      int start = p->p;
      while (p->s[p->p] >= '0' && p->s[p->p] <= '9')
        p->p++;
      if (start == p->p)
        return -1;
    }
    char *end;
    errno = 0;
    double number_value = strtod(p->s + t->a, &end);
    if (errno || !isfinite(number_value) || end != p->s + p->p)
      return -1;
    t->kind = 'N';
  }
  t->b = p->p;
  t->end = p->n;
  return i;
}
static int field(Parser *p, int obj, const char *name) {
  if (obj < 0 || p->t[obj].kind != '{')
    return -1;
  int found = -1;
  for (int i = obj + 1; i < p->t[obj].end;) {
    Tok *k = &p->t[i];
    int v = i + 1;
    if (k->b - k->a == (int)strlen(name) + 2 &&
        !memcmp(p->s + k->a + 1, name, strlen(name))) {
      if (found >= 0)
        return -2;
      found = v;
    }
    i = p->t[v].end;
  }
  return found;
}
static int number(Parser *p, int i, double *v) {
  if (i < 0 || p->t[i].kind != 'N')
    return -1;
  char *end;
  errno = 0;
  *v = strtod(p->s + p->t[i].a, &end);
  return errno || !isfinite(*v) || end != p->s + p->t[i].b ? -1 : 0;
}
static int integer(Parser *p, int i, int64_t *v) {
  if (i < 0 || p->t[i].kind != 'N')
    return -1;
  char *end;
  errno = 0;
  long long x = strtoll(p->s + p->t[i].a, &end, 10);
  if (errno || end != p->s + p->t[i].b)
    return -1;
  *v = x;
  return 0;
}
static int ints(Parser *p, int i, int *dst, int n, int lo, int hi) {
  if (i < 0 || p->t[i].kind != '[' || p->t[i].n != n)
    return -1;
  for (int k = 0, j = i + 1; k < n; k++, j = p->t[j].end) {
    int64_t v;
    if (integer(p, j, &v) || v < lo || v > hi)
      return -1;
    dst[k] = (int)v;
  }
  return 0;
}
int eval_oracle_parse(const char *json, EvalOracleReceipt *out, char *err,
                      int cap) {
  if (!json || !out || strlen(json) > MAX_JSON)
    return fail(err, cap, "invalid response length");
  Parser p = {json, 0, 0, calloc(MAX_TOK, sizeof(Tok))};
  if (!p.t)
    return fail(err, cap, "out of memory");
  int rc = -1;
  EvalOracleReceipt r;
  memset(&r, 0, sizeof r);
  r.obs.magic = EM_MAGIC;
#define BAD(s)                                                                 \
  do {                                                                         \
    fail(err, cap, s);                                                         \
    goto done;                                                                 \
  } while (0)
#define FIELD(s) field(&p, 0, s)
  if (value(&p, 0) != 0)
    BAD("malformed JSON");
  ws(&p);
  if (json[p.p] || p.t[0].kind != '{')
    BAD("trailing JSON or non-object");
  int ok = FIELD("ok");
  if (ok < 0 || p.t[ok].kind != 't' || FIELD("error") != -1 ||
      FIELD("cam_error") != -1)
    BAD("Java error or missing ok");
  const char *pos[] = {"x", "y", "z", "yaw", "pitch"};
  double v[5];
  for (int k = 0; k < 5; k++)
    if (number(&p, FIELD(pos[k]), &v[k]))
      BAD("missing/nonfinite pose");
  r.obs.x = v[0];
  r.obs.y = v[1];
  r.obs.z = v[2];
  r.obs.yaw = (float)v[3];
  r.obs.pitch = (float)v[4];
  if (!isfinite(r.obs.yaw) || !isfinite(r.obs.pitch) || v[4] < -90 || v[4] > 90)
    BAD("invalid camera angles");
  int d = FIELD("dead");
  if (d < 0 || (p.t[d].kind != 't' && p.t[d].kind != 'f'))
    BAD("invalid dead");
  r.obs.dead = p.t[d].kind == 't';
  int64_t n;
  if (integer(&p, FIELD("hotbar_sel"), &n) || n < 0 || n > 8)
    BAD("invalid hotbar_sel");
  r.obs.hotbar_sel = (int)n;
  if (integer(&p, FIELD("container"), &n) || n < 0 || n > 3)
    BAD("invalid container");
  r.obs.container = (int)n;
  if (ints(&p, FIELD("inv_counts"), r.obs.inv_counts, 9, 0, 2304))
    BAD("invalid inv_counts");
  int tmp[EM_NPIX];
  const char *planes[] = {"cam", "depth", "edge"};
  for (int k = 0; k < 3; k++) {
    if (ints(&p, FIELD(planes[k]), tmp, EM_NPIX, 0,
             k == 0   ? 65535
             : k == 1 ? 255
                      : 1))
      BAD("invalid camera plane length/value");
    for (int j = 0; j < EM_NPIX; j++) {
      if (k == 0)
        r.obs.cam[j] = (unsigned short)tmp[j];
      else if (k == 1)
        r.obs.depth[j] = (unsigned char)tmp[j];
      else
        r.obs.edge[j] = (unsigned char)tmp[j];
    }
  }
  int a = FIELD("coal");
  if (a < 0 || p.t[a].kind != '[' || p.t[a].n > EM_NCOAL)
    BAD("invalid coal list");
  for (int j = a + 1, k = 0; j < p.t[a].end; j = p.t[j].end, k++)
    if (ints(&p, j, r.obs.coal[k], 3, INT_MIN, INT_MAX))
      BAD("invalid coal coordinate");
  a = FIELD("inventory");
  if (a < 0 || p.t[a].kind != '[' || p.t[a].n > 36)
    BAD("invalid inventory");
  uint64_t seen = 0;
  int measured_counts[9] = {0};
  static const int tracked_ids[9] = {17, 5, 280, 4, 58, 270, 274, 263, 50};
  for (int j = a + 1; j < p.t[a].end; j = p.t[j].end) {
    int64_t slot, id, count;
    if (integer(&p, field(&p, j, "slot"), &slot) || slot < 0 || slot > 35 ||
        integer(&p, field(&p, j, "id"), &id) || id < 1 || id > 65535 ||
        integer(&p, field(&p, j, "count"), &count) || count < 1 || count > 64)
      BAD("invalid inventory slot");
    if (seen & (UINT64_C(1) << slot))
      BAD("duplicate inventory slot");
    seen |= UINT64_C(1) << slot;
    r.inventory_total += (int)count;
    for (int k = 0; k < 9; k++)
      if (id == tracked_ids[k])
        measured_counts[k] += (int)count;
    if (slot < 9) {
      r.obs.hotbar_ids[slot] = (int)id;
      r.obs.hotbar_counts[slot] = (int)count;
    }
  }
  for (int k = 0; k < 9; k++)
    if (measured_counts[k] != r.obs.inv_counts[k])
      BAD("inventory/count receipt mismatch");
  if (integer(&p, FIELD("policy_action_seq"), &n) || n < 0)
    BAD("missing action sequence");
  r.action_seq = (uint64_t)n;
  a = FIELD("policy_action_fnv64");
  if (a < 0 || p.t[a].kind != '"' || p.t[a].b - p.t[a].a < 3 ||
      p.t[a].b - p.t[a].a > 18)
    BAD("invalid action digest");
  for (int j = p.t[a].a + 1; j < p.t[a].b - 1; j++) {
    char c = json[j];
    int h = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                   : -1;
    if (h < 0)
      BAD("invalid action digest");
    r.action_fnv = (r.action_fnv << 4) | (unsigned)h;
  }
  a = FIELD("time");
  if (integer(&p, field(&p, a, "total_time"), &r.world_time) ||
      r.world_time < 0)
    BAD("missing world total_time");
  int pt = FIELD("player_tick"), st = FIELD("server_tick");
  if (pt != -1 || st != -1) {
    if (integer(&p, pt, &r.player_tick) || integer(&p, st, &r.server_tick) ||
        r.player_tick < 0 || r.server_tick < 0)
      BAD("incomplete/invalid tick counters");
    r.have_ticks = 1;
  }
  a = FIELD("policy_locked");
  if (a != -1) {
    if (a < 0 || (p.t[a].kind != 't' && p.t[a].kind != 'f'))
      BAD("invalid policy_locked");
    r.policy_locked = p.t[a].kind == 't';
  }
  a = FIELD("world_seed");
  if (a != -1) {
    if (integer(&p, a, &r.world_seed))
      BAD("invalid world_seed");
    r.have_world_seed = 1;
  }
  r.obs.tick = r.have_ticks ? r.player_tick : r.world_time;
  *out = r;
  rc = 0;
done:
  free(p.t);
  return rc;
#undef BAD
#undef FIELD
}
struct EvalOracle {
  int fd, timeout, strict, ready, poison, policy_step;
  FILE *req, *resp;
  char *partial, *control;
  EvalOracleReceipt r;
};
static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int wait_fd(int fd, short events, int64_t deadline) {
  for (;;) {
    int64_t left = deadline - now_ms();
    if (left <= 0)
      return -1;
    struct pollfd p = {fd, events, 0};
    int rc = poll(&p, 1, (int)left);
    if (rc < 0 && errno == EINTR)
      continue;
    return rc > 0 && (p.revents & (events | POLLHUP)) ? 0 : -1;
  }
}
static FILE *log_open(const char *prefix, const char *suffix) {
  size_t n = strlen(prefix) + strlen(suffix) + 1;
  char *path = malloc(n);
  if (!path)
    return NULL;
  snprintf(path, n, "%s%s", prefix, suffix);
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  free(path);
  if (fd < 0)
    return NULL;
  FILE *f = fdopen(fd, "w");
  if (!f)
    close(fd);
  return f;
}
void eval_oracle_close(EvalOracle *o) {
  if (!o)
    return;
  if (o->fd >= 0)
    close(o->fd);
  if (o->req)
    fclose(o->req);
  if (o->resp)
    fclose(o->resp);
  free(o->partial);
  free(o->control);
  free(o);
}
EvalOracle *eval_oracle_open(const char *ip, int port, int timeout, int strict,
                             const char *prefix, char *err, int cap) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  if (!ip || !prefix || !*prefix || port < 1 || port > 65535 || timeout < 1 ||
      timeout > 600000 || inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    fail(err, cap, "invalid Oracle endpoint/config");
    return NULL;
  }
  addr.sin_port = htons((uint16_t)port);
  EvalOracle *o = calloc(1, sizeof *o);
  if (!o) {
    fail(err, cap, "out of memory");
    return NULL;
  }
  o->fd = -1;
  o->timeout = timeout;
  o->strict = strict;
  size_t n = strlen(prefix) + 9;
  o->partial = malloc(n);
  if (!o->partial)
    goto bad;
  snprintf(o->partial, n, "%s.partial", prefix);
  o->req = log_open(prefix, ".requests.jsonl");
  o->resp = log_open(prefix, ".responses.jsonl");
  if (!o->req || !o->resp)
    goto bad;
  o->fd = socket(AF_INET, SOCK_STREAM, 0);
  if (o->fd < 0 || fcntl(o->fd, F_SETFL, O_NONBLOCK) < 0 ||
      fcntl(o->fd, F_SETFD, FD_CLOEXEC) < 0)
    goto bad;
#ifdef SO_NOSIGPIPE
  int one = 1;
  if (setsockopt(o->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one))
    goto bad;
#endif
  int rc = connect(o->fd, (struct sockaddr *)&addr, sizeof addr);
  if (rc < 0 && errno != EINPROGRESS)
    goto bad;
  if (rc < 0) {
    if (wait_fd(o->fd, POLLOUT, now_ms() + timeout))
      goto bad;
    int e = 0;
    socklen_t len = sizeof e;
    if (getsockopt(o->fd, SOL_SOCKET, SO_ERROR, &e, &len) || e)
      goto bad;
  }
  return o;
bad:
  fail(err, cap, "Oracle connect/log creation failed");
  eval_oracle_close(o);
  return NULL;
}
static int exchange(EvalOracle *o, const char *request, int obs_reply,
                    char *err, int cap) {
  if (!o || o->poison)
    return fail(err, cap, "Oracle transport unavailable/poisoned");
  o->poison = 1;
  if (fputs(request, o->req) == EOF || fflush(o->req))
    return fail(err, cap, "request log failed");
  int64_t deadline = now_ms() + o->timeout;
  size_t len = strlen(request), sent = 0;
  while (sent < len) {
    if (wait_fd(o->fd, POLLOUT, deadline))
      return fail(err, cap, "Oracle send timeout");
#ifdef MSG_NOSIGNAL
    ssize_t n = send(o->fd, request + sent, len - sent, MSG_NOSIGNAL);
#else
    ssize_t n = send(o->fd, request + sent, len - sent, 0);
#endif
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n <= 0)
      return fail(err, cap, "Oracle send failed");
    sent += (size_t)n;
  }
  char *buf = malloc(MAX_JSON + 1);
  if (!buf)
    return fail(err, cap, "out of memory");
  size_t got = 0;
  int complete = 0;
  while (got < MAX_JSON) {
    if (wait_fd(o->fd, POLLIN, deadline))
      break;
    ssize_t n = recv(o->fd, buf + got, MAX_JSON - got, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n <= 0)
      break;
    char *newline = memchr(buf + got, '\n', (size_t)n);
    got += (size_t)n;
    if (newline) {
      complete = (newline == buf + got - 1);
      break;
    }
  }
  buf[got] = 0;
  int rc = -1;
  if (!complete || memchr(buf, 0, got)) {
    int fd = open(o->partial, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      size_t off = 0;
      while (off < got) {
        ssize_t n = write(fd, buf + off, got - off);
        if (n < 0 && errno == EINTR)
          continue;
        if (n <= 0)
          break;
        off += (size_t)n;
      }
      close(fd);
    }
    fail(err, cap, "Oracle incomplete/oversized/NUL response");
  } else if (fwrite(buf, 1, got, o->resp) != got || fflush(o->resp))
    fail(err, cap, "response log failed");
  else {
    if (obs_reply)
      rc = eval_oracle_parse(buf, &o->r, err, cap);
    else {
      Parser p = {buf, 0, 0, calloc(MAX_TOK, sizeof(Tok))};
      if (p.t && value(&p, 0) == 0) {
        ws(&p);
        int ok = field(&p, 0, "ok");
        if (!buf[p.p] && ok >= 0 && p.t[ok].kind == 't' &&
            field(&p, 0, "error") == -1 && field(&p, 0, "snapshot_error") == -1)
          rc = 0;
      }
      free(p.t);
      if (rc)
        fail(err, cap, "invalid/error control response");
    }
    if (!rc && !obs_reply) {
      char *copy = strdup(buf);
      if (!copy) {
        rc = fail(err, cap, "control receipt allocation failed");
      } else {
        free(o->control);
        o->control = copy;
      }
    }
    if (!rc)
      o->poison = 0;
  }
  free(buf);
  return rc;
}
int eval_oracle_command(EvalOracle *o, const char *request, int obs_reply,
                        char *err, int cap) {
  if (!request || !*request || strlen(request) > 65536 ||
      request[strlen(request) - 1] != '\n' ||
      strchr(request, '\n') != request + strlen(request) - 1)
    return fail(err, cap, "invalid control request framing");
  int rc = exchange(o, request, obs_reply, err, cap);
  if (!rc && obs_reply)
    o->ready = 1;
  return rc;
}
int eval_oracle_control_integer(const EvalOracle *o, const char *key,
                                int64_t *out) {
  if (!o || !o->control || o->poison || !key || !out)
    return -1;
  Parser p = {o->control, 0, 0, calloc(MAX_TOK, sizeof(Tok))};
  if (!p.t)
    return -1;
  int rc = value(&p, 0) == 0 ? integer(&p, field(&p, 0, key), out) : -1;
  free(p.t);
  return rc;
}
int eval_oracle_observe(EvalOracle *o, char *err, int cap) {
  int rc =
      exchange(o, "{\"cmd\":\"obs\",\"action\":{\"cam\":1}}\n", 1, err, cap);
  if (!rc)
    o->ready = 1;
  return rc;
}
int eval_oracle_set_step_command(EvalOracle *o, const char *cmd) {
  if (!o || !cmd || o->ready)
    return -1;
  if (!strcmp(cmd, "step"))
    o->policy_step = 0;
  else if (!strcmp(cmd, "policy_step"))
    o->policy_step = 1;
  else
    return -1;
  return 0;
}
const EvalOracleReceipt *eval_oracle_receipt(const EvalOracle *o) {
  return o && o->ready && !o->poison ? &o->r : NULL;
}
int eval_oracle_step(EvalOracle *o, const double a[13], char *err, int cap) {
  if (!o || !o->ready || o->poison || !a)
    return fail(err, cap, "observe before step");
  if (o->strict && !o->r.have_ticks)
    return fail(err, cap,
                "strict transfer requires player_tick and server_tick");
  for (int k = 0; k < 13; k++)
    if (!isfinite(a[k]))
      return fail(err, cap, "nonfinite action");
  if ((a[0] != -1 && a[0] != 0 && a[0] != 1) ||
      (a[1] != -1 && a[1] != 0 && a[1] != 1))
    return fail(err, cap, "invalid movement action");
  for (int k = 4; k < 13; k++) {
    int lo = k == 9 || k == 10 ? -1 : 0, hi = k == 9 ? 8 : k == 10 ? 7 : 1;
    if (a[k] < lo || a[k] > hi || floor(a[k]) != a[k])
      return fail(err, cap, "invalid discrete action");
  }
  char action[1024], request[1100];
  int n = snprintf(action, sizeof action,
                   "{\"forward\":%d,\"back\":%d,\"left\":%d,\"right\":%d,"
                   "\"dyaw\":%.9g,\"dpitch\":%.9g,\"jump\":%d,\"sneak\":%d,"
                   "\"sprint\":%d,\"attack\":%d,\"use\":%d,\"hotbar\":%d,"
                   "\"craft\":%d,\"interact\":%d,\"smelt\":%d,\"cam\":1}",
                   a[0] > 0, a[0] < 0, a[1] > 0, a[1] < 0, a[2], a[3],
                   (int)a[4], (int)a[5], (int)a[6], (int)a[7], (int)a[8],
                   (int)a[9], (int)a[10], (int)a[11], (int)a[12]);
  if (n < 0 || (size_t)n >= sizeof action)
    return fail(err, cap, "action serialization overflow");
  snprintf(request, sizeof request, "{\"cmd\":\"%s\",\"action\":%s}\n",
           o->policy_step ? "policy_step" : "step", action);
  EvalOracleReceipt before = o->r;
  uint64_t hash = before.action_fnv;
  for (int k = 0; k < n; k++) {
    hash ^= (unsigned char)action[k];
    hash *= UINT64_C(0x100000001b3);
  }
  if (exchange(o, request, 1, err, cap))
    return -1;
  if (o->r.action_seq != before.action_seq + 1 || o->r.action_fnv != hash) {
    o->poison = 1;
    return fail(err, cap, "Oracle action acknowledgement mismatch");
  }
  if (o->strict &&
      (!o->r.have_ticks || o->r.player_tick - before.player_tick != 1 ||
       o->r.server_tick - before.server_tick != 1)) {
    o->poison = 1;
    return fail(err, cap, "Oracle tick drift (expected client=1 server=1)");
  }
  return 0;
}
