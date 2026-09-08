#define _DEFAULT_SOURCE
#include "eval_magma.h"

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#define EM_PI 3.14159265358979323846
#define EM_EYE 1.62

struct EvalMagma {
  pid_t pid;
  FILE *in;  /* parent writes JSON */
  FILE *out; /* parent reads BOLR */
  EvalMagmaObs obs;
  FILE *raw, *actions, *states;
  EvalMagmaTickFn tick_fn;
  void *tick_ctx;
};

static void err_set(char *err, int cap, const char *msg) {
  if (!err || cap <= 0)
    return;
  snprintf(err, (size_t)cap, "%s", msg);
}

static int read_exact(FILE *f, void *dst, size_t n) {
  unsigned char *p = (unsigned char *)dst;
  size_t got = 0;
  while (got < n) {
    size_t r = fread(p + got, 1, n - got, f);
    if (r == 0)
      return -1;
    got += r;
  }
  return 0;
}

static int read_bolr(FILE *f, EvalMagmaObs *o) {
  unsigned char win[4];
  unsigned magic;
  if (read_exact(f, win, 4) != 0)
    return -1;
  for (;;) {
    memcpy(&magic, win, 4);
    if (magic == EM_MAGIC)
      break;
    {
      unsigned char b;
      if (read_exact(f, &b, 1) != 0)
        return -1;
      win[0] = win[1];
      win[1] = win[2];
      win[2] = win[3];
      win[3] = b;
    }
  }
  o->magic = magic;
  if (read_exact(f, (char *)o + 4, sizeof(*o) - 4) != 0)
    return -1;
  if (o->magic != EM_MAGIC)
    return -1;
  return 0;
}

static int write_act(FILE *f, const double *a) {
  if (!f || !a)
    return -1;
  if (fprintf(f,
              "{\"forward\":%.9g,\"strafe\":%.9g,\"dyaw\":%.9g,\"dpitch\":%.9g,"
              "\"jump\":%d,\"sneak\":%d,\"sprint\":%d,\"attack\":%d,\"use\":%d,"
              "\"hotbar\":%d,\"craft\":%d,\"interact\":%d,\"smelt\":%d,"
              "\"cam\":1}\n",
              a[0], a[1], a[2], a[3], (int)a[4], (int)a[5], (int)a[6],
              (int)a[7], (int)a[8], (int)a[9], (int)a[10], (int)a[11],
              (int)a[12]) < 0)
    return -1;
  if (fflush(f) != 0)
    return -1;
  return 0;
}

static double wrap180(double a) {
  a = fmod(a + 180.0, 360.0);
  if (a < 0.0)
    a += 360.0;
  return a - 180.0;
}

static int nearest_coal(const EvalMagmaObs *o, double *ry, double *rp,
                        double *dist) {
  double ex = o->x, ey = o->y + EM_EYE, ez = o->z;
  double bd = 0.0;
  int have = 0, i;
  for (i = 0; i < EM_NCOAL; ++i) {
    double dx, dy, dz, d;
    if (o->coal[i][0] == 0 && o->coal[i][1] == 0 && o->coal[i][2] == 0)
      break;
    dx = o->coal[i][0] + 0.5 - ex;
    dy = o->coal[i][1] + 0.5 - ey;
    dz = o->coal[i][2] + 0.5 - ez;
    d = sqrt(dx * dx + dy * dy + dz * dz);
    if (!have || d < bd) {
      double dd = d > 1e-9 ? d : 1e-9;
      if (ry)
        *ry = wrap180(atan2(-dx, dz) * (180.0 / EM_PI) - (double)o->yaw);
      if (rp)
        *rp = -asin(dy / dd) * (180.0 / EM_PI) - (double)o->pitch;
      if (dist)
        *dist = d;
      bd = d;
      have = 1;
    }
  }
  return have;
}

static int trace_obs(EvalMagma *m, const double *action) {
  if (!m->raw) return 0;
  if (action && write_act(m->actions, action)) return -1;
  if (fwrite(&m->obs, sizeof m->obs, 1, m->raw) != 1 || fflush(m->raw)) return -1;
  const EvalMagmaObs *o = &m->obs;
  fprintf(m->states, "{\"tick\":%lld,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,\"yaw\":%.9g,\"pitch\":%.9g,\"dead\":%d,\"container\":%d,\"hotbar_selected\":%d,\"velocity\":null,\"health\":null,\"rng\":null,\"inventory_counts\":[", o->tick,o->x,o->y,o->z,(double)o->yaw,(double)o->pitch,o->dead,o->container,o->hotbar_sel);
  for(int i=0;i<EM_INV;++i) fprintf(m->states,"%s%d",i?",":"",o->inv_counts[i]);
  fputs("],\"hotbar\":[",m->states);
  for(int i=0;i<EM_INV;++i) fprintf(m->states,"%s[%d,%d]",i?",":"",o->hotbar_ids[i],o->hotbar_counts[i]);
  fputs("]}\n",m->states);
  return ferror(m->states) || fflush(m->states) ? -1 : 0;
}

void eval_magma_set_tick_callback(EvalMagma *m, EvalMagmaTickFn fn, void *ctx) {
  if (m) { m->tick_fn=fn; m->tick_ctx=ctx; }
}

EvalMagma *eval_magma_open(const char *bin, const char *snap, int seed,
                          char *err, int err_cap) {
  return eval_magma_open_trace(bin,snap,seed,NULL,err,err_cap);
}

EvalMagma *eval_magma_open_trace(const char *bin, const char *snap, int seed,
                           const char *trace_dir, char *err, int err_cap) {
  return eval_magma_open_config(bin,snap,seed,trace_dir,NULL,err,err_cap);
}

EvalMagma *eval_magma_open_config(const char *bin, const char *snap, int seed,
                           const char *trace_dir, const char *magma_conf,
                           char *err, int err_cap) {
  EvalMagma *m;
  int pin[2] = {-1, -1}, pout[2] = {-1, -1};
  pid_t pid;
  char seedbuf[32], frames[2048], parity_path[2048], stderr_path[2048];
  if (trace_dir && (snprintf(frames,sizeof frames,"%s/frames",trace_dir) >= (int)sizeof frames ||
      snprintf(parity_path,sizeof parity_path,"%s/magma.pary",trace_dir) >= (int)sizeof parity_path ||
      snprintf(stderr_path,sizeof stderr_path,"%s/magma.stderr",trace_dir) >= (int)sizeof stderr_path)) return NULL;

  if (!bin || !bin[0] || !snap || !snap[0]) {
    err_set(err, err_cap, "magma bin/snap empty");
    return NULL;
  }
  if (access(bin, X_OK) != 0) {
    err_set(err, err_cap, "magma_game not executable");
    return NULL;
  }
  if (access(snap, R_OK) != 0) {
    err_set(err, err_cap, "snapshot not readable");
    return NULL;
  }
  if (magma_conf && *magma_conf) {
    struct stat st;
    if (stat(magma_conf,&st) || !S_ISREG(st.st_mode) || access(magma_conf,R_OK)) {
      err_set(err,err_cap,"magma config not a readable regular file"); return NULL;
    }
  }
  if (pipe(pin) != 0 || pipe(pout) != 0) {
    err_set(err, err_cap, "pipe failed");
    return NULL;
  }
  signal(SIGPIPE, SIG_IGN);
  pid = fork();
  if (pid < 0) {
    err_set(err, err_cap, "fork failed");
    close(pin[0]);
    close(pin[1]);
    close(pout[0]);
    close(pout[1]);
    return NULL;
  }
  if (pid == 0) {
    char *argv[24], parity_arg[64];
    int n = 0;
    snprintf(seedbuf, sizeof(seedbuf), "%d", seed);
    if (dup2(pin[0], 0) < 0 || dup2(pout[1], 1) < 0)
      _exit(127);
    close(pin[0]);
    close(pin[1]);
    close(pout[0]);
    close(pout[1]);
    argv[n++] = (char *)bin;
    if (magma_conf && *magma_conf) { argv[n++]="--conf"; argv[n++]=(char *)magma_conf; }
    argv[n++] = "--rl-bin";
    argv[n++] = "--render";
    argv[n++] = "off";
    argv[n++] = "--pace";
    argv[n++] = "unlimited";
    argv[n++] = "--mobs";
    argv[n++] = "off";
    argv[n++] = "--snapshot-in";
    argv[n++] = (char *)snap;
    argv[n++] = "--seed";
    argv[n++] = seedbuf;
    if (trace_dir) {
      int pfd=open(parity_path,O_WRONLY|O_CREAT|O_EXCL,0600);
      if (pfd < 0) _exit(126);
      snprintf(parity_arg,sizeof parity_arg,"port_parity_fd=%d",pfd);
      argv[n++]="--set"; argv[n++]=parity_arg;
      argv[n++]="--frames-out"; argv[n++]=frames;
    }
    argv[n] = NULL;
    {
      int dn = trace_dir ? open(stderr_path,O_WRONLY|O_CREAT|O_EXCL,0600) : open("/dev/null", O_WRONLY);
      if (dn >= 0) {
        dup2(dn, 2);
        close(dn);
      }
    }
    execv(bin, argv);
    _exit(127);
  }
  close(pin[0]);
  close(pout[1]);
  m = (EvalMagma *)calloc(1, sizeof(*m));
  if (!m) {
    err_set(err, err_cap, "oom");
    close(pin[1]);
    close(pout[0]);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return NULL;
  }
  m->pid = pid;
  m->in = fdopen(pin[1], "w");
  m->out = fdopen(pout[0], "r");
  if (!m->in || !m->out) {
    err_set(err, err_cap, "fdopen failed");
    eval_magma_close(m);
    return NULL;
  }
  setvbuf(m->in, NULL, _IOLBF, 0);
  setvbuf(m->out, NULL, _IONBF, 0);
  if (read_bolr(m->out, &m->obs) != 0) {
    int st = 0;
    if (waitpid(pid, &st, WNOHANG) > 0) {
      m->pid = 0;
      if (err && err_cap > 0)
        snprintf(err, (size_t)err_cap, "first BOLR: magma exited %d",
                 WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    } else
      err_set(err, err_cap, "first BOLR read failed");
    eval_magma_close(m);
    return NULL;
  }
  if (trace_dir) {
    char path[2048];
    snprintf(path,sizeof path,"%s/magma.bolr",trace_dir); m->raw=fopen(path,"wbx");
    snprintf(path,sizeof path,"%s/actions.jsonl",trace_dir); m->actions=fopen(path,"wx");
    snprintf(path,sizeof path,"%s/magma_state.jsonl",trace_dir); m->states=fopen(path,"wx");
    if (!m->raw || !m->actions || !m->states || trace_obs(m,NULL)) {
      err_set(err,err_cap,"trace output failed"); eval_magma_close(m); return NULL;
    }
  }
  return m;
}

void eval_magma_close(EvalMagma *m) {
  int st;
  if (!m)
    return;
  if (m->in)
    fclose(m->in);
  if (m->out)
    fclose(m->out);
  if (m->pid > 0) {
    int ended=0;
    for(int i=0;i<100;++i) {
      if(waitpid(m->pid,&st,WNOHANG)!=0) { ended=1; break; }
      struct timespec pause={0,10000000}; nanosleep(&pause,NULL);
    }
    if(!ended) { kill(m->pid,SIGTERM); waitpid(m->pid,&st,0); }
  }
  if(m->raw) fclose(m->raw);
  if(m->actions) fclose(m->actions);
  if(m->states) fclose(m->states);
  free(m);
}

int eval_magma_step(EvalMagma *m, const double *act13, int repeat) {
  int i;
  double a[EM_ACT];
  if (!m || !act13 || repeat < 1)
    return -1;
  memcpy(a, act13, sizeof a);
  for (i = 0; i < repeat; ++i) {
    if (i > 0) {
      a[2] = 0.0;
      a[3] = 0.0;
      a[10] = -1.0;
      a[11] = 0.0;
      a[12] = 0.0;
    }
    if (write_act(m->in, a) != 0)
      return -1;
    if (read_bolr(m->out, &m->obs) != 0)
      return -1;
    if (trace_obs(m,a) || (m->tick_fn && m->tick_fn(m->tick_ctx,a,&m->obs))) return -1;
  }
  return 0;
}

const EvalMagmaObs *eval_magma_obs(const EvalMagma *m) {
  return m ? &m->obs : NULL;
}

void eval_magma_fill_policy(const EvalMagmaObs *o, unsigned short *cam,
                            unsigned char *dep, unsigned char *edg, float *pose,
                            int *status, float *scal6) {
  int i, sel;
  if (!o)
    return;
  if (cam)
    memcpy(cam, o->cam, sizeof o->cam);
  if (dep)
    memcpy(dep, o->depth, sizeof o->depth);
  if (edg)
    memcpy(edg, o->edge, sizeof o->edge);
  if (pose) {
    pose[0] = (float)o->x;
    pose[1] = (float)o->y;
    pose[2] = (float)o->z;
    pose[3] = o->yaw;
    pose[4] = o->pitch;
  }
  if (status) {
    memset(status, 0, 17 * sizeof(int));
    for (i = 0; i < EM_INV; ++i)
      status[i] = o->inv_counts[i];
    sel = o->hotbar_sel;
    if (sel < 0)
      sel = 0;
    if (sel > 8)
      sel = 8;
    status[9] = sel;
    status[10] = o->hotbar_ids[sel];
    status[11] = o->container;
  }
  if (scal6) {
    double ry = 0.0, rp = 0.0, dist = 0.0, pr;
    int have = nearest_coal(o, &ry, &rp, &dist);
    pr = (double)o->pitch * (EM_PI / 180.0);
    if (!have) {
      scal6[0] = 0.0f;
      scal6[1] = 0.0f;
      scal6[2] = 0.0f;
      scal6[3] = 1.0f;
    } else {
      scal6[0] = (float)sin(ry * (EM_PI / 180.0));
      scal6[1] = (float)cos(ry * (EM_PI / 180.0));
      scal6[2] = (float)(rp / 90.0);
      scal6[3] = (float)((dist < 24.0 ? dist : 24.0) / 24.0);
    }
    scal6[4] = (float)sin(pr);
    scal6[5] = (float)cos(pr);
  }
}

int eval_magma_cmp_gated(const EvalMagmaObs *a, const EvalMagmaObs *b,
                         char *why, int why_cap) {
  int i;
  if (!a || !b) {
    err_set(why, why_cap, "null obs");
    return -1;
  }
  if (a->dead != b->dead) {
    snprintf(why, (size_t)why_cap, "dead %d vs %d", a->dead, b->dead);
    return 1;
  }
  if (a->x != b->x || a->y != b->y || a->z != b->z) {
    snprintf(why, (size_t)why_cap,
             "pose xyz magma=(%.9g,%.9g,%.9g) blaze=(%.9g,%.9g,%.9g)", a->x,
             a->y, a->z, b->x, b->y, b->z);
    return 1;
  }
  if (a->yaw != b->yaw || a->pitch != b->pitch) {
    snprintf(why, (size_t)why_cap, "yaw/pitch magma=%g/%g blaze=%g/%g", a->yaw,
             a->pitch, b->yaw, b->pitch);
    return 1;
  }
  if (a->container != b->container) {
    snprintf(why, (size_t)why_cap, "container %d vs %d", a->container,
             b->container);
    return 1;
  }
  if (a->hotbar_sel != b->hotbar_sel) {
    snprintf(why, (size_t)why_cap, "hotbar_sel %d vs %d", a->hotbar_sel,
             b->hotbar_sel);
    return 1;
  }
  for (i = 0; i < EM_INV; ++i) {
    if (a->hotbar_ids[i] != b->hotbar_ids[i] ||
        a->hotbar_counts[i] != b->hotbar_counts[i]) {
      snprintf(why, (size_t)why_cap, "hotbar[%d] id/count %d/%d vs %d/%d", i,
               a->hotbar_ids[i], a->hotbar_counts[i], b->hotbar_ids[i],
               b->hotbar_counts[i]);
      return 1;
    }
    if (a->inv_counts[i] != b->inv_counts[i]) {
      snprintf(why, (size_t)why_cap, "inv_counts[%d] %d vs %d", i,
               a->inv_counts[i], b->inv_counts[i]);
      return 1;
    }
  }
  if (memcmp(a->coal, b->coal, sizeof a->coal) != 0) {
    err_set(why, why_cap, "coal");
    return 1;
  }
  if (memcmp(a->cam, b->cam, sizeof a->cam) != 0) {
    int n = 0;
    for (i = 0; i < EM_NPIX; ++i)
      if (a->cam[i] != b->cam[i])
        n++;
    snprintf(why, (size_t)why_cap, "cam %d px", n);
    return 1;
  }
  if (memcmp(a->depth, b->depth, sizeof a->depth) != 0) {
    err_set(why, why_cap, "depth");
    return 1;
  }
  if (memcmp(a->edge, b->edge, sizeof a->edge) != 0) {
    err_set(why, why_cap, "edge");
    return 1;
  }
  return 0;
}
