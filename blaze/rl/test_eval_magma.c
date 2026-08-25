/* 32-tick scripted Magma --rl-bin vs blaze_cpu.so BOLR gate.
 * Run from repo root. No checkpoint. */
#include "blaze_abi.h"
#include "eval_magma.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char kSnap[] = "verify/fixtures/port/s10_t0_r64_no_liquid.bsnp";
static const char kSo[] = "out/blaze/env/blaze_cpu.so";
static const char kMagma[] = "magma/magma_game";
enum { kTicks = 32, kSeed = 10 };

typedef void *(*CreateFn)(int, int, const BlazeCreateOpts *);
typedef void (*DestroyFn)(void *);
typedef int (*LoadFn)(void *, const char *const *, int, char *, int);
typedef int (*AssignFn)(void *, const int *);
typedef int (*ResetFn)(void *, const unsigned char *);
typedef int (*TickRawFn)(void *, int, const double *, int, void *);
typedef int (*EmitFn)(void *, int, int, void *);
typedef int (*ObsSizeFn)(void);
typedef int (*SetSuccessFn)(void *, int);

static void *must_dlsym(void *lib, const char *name) {
  void *s = dlsym(lib, name);
  if (!s)
    fprintf(stderr, "dlsym %s: %s\n", name, dlerror());
  return s;
}

int main(void) {
  void *lib = NULL;
  void *env = NULL;
  EvalMagma *mag = NULL;
  CreateFn create;
  DestroyFn destroy;
  LoadFn load;
  AssignFn assign;
  ResetFn reset;
  TickRawFn tick_raw;
  EmitFn emit;
  ObsSizeFn obs_size;
  SetSuccessFn set_success;
  BlazeCreateOpts opts;
  const char *paths[1];
  int idx = 0;
  char err[256];
  EvalMagmaObs blaze_obs;
  double a17[17];
  double a13[EM_ACT];
  int t, rc = 1;
  char why[128];

  if (access(kMagma, X_OK) != 0) {
    fprintf(stderr, "FAIL: missing %s (make -C magma game)\n", kMagma);
    return 2;
  }
  if (access(kSnap, R_OK) != 0) {
    fprintf(stderr, "FAIL: missing %s\n", kSnap);
    return 2;
  }
  if (access(kSo, R_OK) != 0) {
    fprintf(stderr, "FAIL: missing %s\n", kSo);
    return 2;
  }

  lib = dlopen(kSo, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
    return 1;
  }
  create = (CreateFn)must_dlsym(lib, "blaze_create");
  destroy = (DestroyFn)must_dlsym(lib, "blaze_destroy");
  load = (LoadFn)must_dlsym(lib, "blaze_load_snapshots");
  assign = (AssignFn)must_dlsym(lib, "blaze_assign");
  reset = (ResetFn)must_dlsym(lib, "blaze_reset");
  tick_raw = (TickRawFn)must_dlsym(lib, "blaze_tick_raw");
  emit = (EmitFn)must_dlsym(lib, "blaze_emit");
  obs_size = (ObsSizeFn)must_dlsym(lib, "blaze_obs_size");
  set_success = (SetSuccessFn)must_dlsym(lib, "blaze_set_success_item");
  if (!create || !destroy || !load || !assign || !reset || !tick_raw || !emit ||
      !obs_size || !set_success)
    goto done;
  if (obs_size() != (int)sizeof(EvalMagmaObs)) {
    fprintf(stderr, "FAIL: BOLR size blaze %d magma-eval %d\n", obs_size(),
            (int)sizeof(EvalMagmaObs));
    goto done;
  }

  blaze_create_opts_default(&opts);
  env = create(0, 1, &opts);
  if (!env) {
    fprintf(stderr, "FAIL: blaze_create\n");
    goto done;
  }
  if (set_success(env, 50) != 0) {
    fprintf(stderr, "FAIL: blaze_set_success_item\n");
    goto done;
  }
  paths[0] = kSnap;
  if (load(env, paths, 1, err, (int)sizeof(err)) < 0) {
    fprintf(stderr, "FAIL: blaze_load_snapshots: %s\n", err);
    goto done;
  }
  if (assign(env, &idx) != 0 || reset(env, NULL) != 0) {
    fprintf(stderr, "FAIL: assign/reset\n");
    goto done;
  }

  mag = eval_magma_open(kMagma, kSnap, kSeed, err, (int)sizeof(err));
  if (!mag) {
    fprintf(stderr, "FAIL: eval_magma_open: %s\n", err);
    goto done;
  }

  memset(a17, 0, sizeof(a17));
  a17[9] = -1.0;
  a17[10] = -1.0;
  memset(a13, 0, sizeof(a13));
  a13[9] = -1.0;
  a13[10] = -1.0;
  if (emit(env, 0, 1, &blaze_obs) != 0) {
    fprintf(stderr, "FAIL: blaze_emit t=0\n");
    goto done;
  }
  if (eval_magma_cmp_gated(eval_magma_obs(mag), &blaze_obs, why, (int)sizeof(why)) !=
      0) {
    fprintf(stderr, "FAIL: t=0 %s\n", why);
    goto done;
  }

  a13[0] = 1.0;
  a17[0] = 1.0;
  for (t = 0; t < kTicks; ++t) {
    if (tick_raw(env, 0, a17, 1, &blaze_obs) != 0) {
      fprintf(stderr, "FAIL: blaze tick %d\n", t);
      goto done;
    }
    if (eval_magma_step(mag, a13, 1) != 0) {
      fprintf(stderr, "FAIL: magma tick %d\n", t);
      goto done;
    }
    if (eval_magma_cmp_gated(eval_magma_obs(mag), &blaze_obs, why,
                             (int)sizeof(why)) != 0) {
      fprintf(stderr, "FAIL: t=%d %s\n", t, why);
      goto done;
    }
  }
  printf("PASS: magma --rl-bin vs blaze_cpu BOLR gated match, %d forward ticks "
         "(s10 t0)\n",
         kTicks);
  rc = 0;

done:
  eval_magma_close(mag);
  if (env && destroy)
    destroy(env);
  if (lib)
    dlclose(lib);
  return rc;
}
