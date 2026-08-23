/* CPU capture from a container-bearing snapshot, including overwrite of a
 * slot whose baked ncont is smaller than the live env (the CUDA death). */
#define _POSIX_C_SOURCE 200809L
#include "blaze_abi.h"
#include "blaze_snapshot.h"

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fails;

static void expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    g_fails++;
  }
}

static void expect_eq_i(int a, int b, const char *msg) {
  if (a != b) {
    fprintf(stderr, "FAIL: %s (got %d want %d)\n", msg, a, b);
    g_fails++;
  }
}

static void *must(void *lib, const char *name) {
  void *p;
  const char *err;
  dlerror();
  p = dlsym(lib, name);
  err = dlerror();
  if (err != NULL || !p) {
    fprintf(stderr, "dlsym %s: %s\n", name, err ? err : "null");
    exit(2);
  }
  return p;
}

enum { RNX = 16, RNY = 32, RNZ = 16 };

static long cell_i(int wx, int wy, int wz) {
  return ((long)wx * RNY + (long)wy) * RNZ + (long)wz;
}

static int write_bsnp_ex(const char *path, int ntab, const int *wx,
                         const int *wy, const int *wz, unsigned ncoal) {
  RlSnapHead h;
  long vol = (long)RNX * RNY * RNZ;
  unsigned short *cells;
  unsigned char *light;
  int *coal = NULL;
  FILE *f;
  int x, y, z, t;
  unsigned u;
  cells = (unsigned short *)calloc((size_t)vol, sizeof *cells);
  light = (unsigned char *)calloc((size_t)vol, 1);
  if (ncoal)
    coal = (int *)malloc((size_t)ncoal * 3u * sizeof *coal);
  if (!cells || !light || (ncoal && !coal)) {
    free(cells);
    free(light);
    free(coal);
    return -1;
  }
  memset(&h, 0, sizeof h);
  memcpy(h.magic, "BSNP", 4);
  h.version = BLAZE_SNAP_VERSION;
  h.px = 8.5;
  h.py = 5.0;
  h.pz = 8.5;
  h.box[0] = 8.2;
  h.box[1] = 5.0;
  h.box[2] = 8.2;
  h.box[3] = 8.8;
  h.box[4] = 6.8;
  h.box[5] = 8.8;
  h.on_ground = 1;
  h.health = 20.f;
  h.food = 20;
  h.saturation = 5.f;
  h.dig_hx = INT_MIN;
  h.rnx = RNX;
  h.rny = RNY;
  h.rnz = RNZ;
  for (x = 0; x < RNX; ++x)
    for (y = 0; y <= 4; ++y)
      for (z = 0; z < RNZ; ++z)
        cells[cell_i(x, y, z)] = (unsigned short)(1 << 4); /* stone floor */
  for (t = 0; t < ntab; ++t)
    cells[cell_i(wx[t], wy[t], wz[t])] = (unsigned short)(58 << 4);
  /* lex-ascending (x,y,z) so blaze_build_ore_xy accepts the list */
  for (u = 0; u < ncoal; ++u) {
    coal[u * 3u + 0] = (int)(u % (unsigned)RNX);
    coal[u * 3u + 1] = 1;
    coal[u * 3u + 2] = (int)(u / (unsigned)RNX);
  }
  f = fopen(path, "wb");
  if (!f || fwrite(&h, sizeof h, 1, f) != 1 ||
      fwrite(cells, sizeof *cells, (size_t)vol, f) != (size_t)vol ||
      fwrite(&ncoal, sizeof ncoal, 1, f) != 1 ||
      (ncoal && fwrite(coal, sizeof *coal, (size_t)ncoal * 3u, f) !=
                    (size_t)ncoal * 3u) ||
      fwrite(light, 1, (size_t)vol, f) != (size_t)vol) {
    if (f)
      fclose(f);
    free(cells);
    free(light);
    free(coal);
    return -1;
  }
  {
    unsigned n_mobs = 0, n_orbs = 0;
    if (h.version >= 3 && fwrite(&n_mobs, sizeof n_mobs, 1, f) != 1) {
      fclose(f);
      free(cells);
      free(light);
      free(coal);
      return -1;
    }
    if (h.version >= BLAZE_SNAP_VERSION_ORBS &&
        fwrite(&n_orbs, sizeof n_orbs, 1, f) != 1) {
      fclose(f);
      free(cells);
      free(light);
      free(coal);
      return -1;
    }
    if (h.version >= BLAZE_SNAP_VERSION_WORLD_RAND) {
      unsigned long long wr = 0;
      if (fwrite(&wr, sizeof wr, 1, f) != 1) {
        fclose(f);
        free(cells);
        free(light);
        free(coal);
        return -1;
      }
    }
    if (h.version >= BLAZE_SNAP_VERSION_UPDATE_LCG) {
      int lcg = 0;
      if (fwrite(&lcg, sizeof lcg, 1, f) != 1) {
        fclose(f);
        free(cells);
        free(light);
        free(coal);
        return -1;
      }
    }
    if (h.version >= BLAZE_SNAP_VERSION_BIOME) {
      size_t bvol = (size_t)RNX * (size_t)RNZ;
      unsigned char *biome = (unsigned char *)malloc(bvol);
      int ok;
      if (!biome) {
        fclose(f);
        free(cells);
        free(light);
        free(coal);
        return -1;
      }
      memset(biome, BLAZE_SNAP_BIOME_PLAINS, bvol);
      ok = fwrite(biome, 1, bvol, f) == bvol;
      free(biome);
      if (!ok) {
        fclose(f);
        free(cells);
        free(light);
        free(coal);
        return -1;
      }
    }
  }
  fclose(f);
  free(cells);
  free(light);
  free(coal);
  return 0;
}

static int write_bsnp(const char *path, int ntab, const int *wx, const int *wy,
                      const int *wz) {
  return write_bsnp_ex(path, ntab, wx, wy, wz, 0);
}

typedef struct {
  void *lib;
  void *(*create)(int, int, const BlazeCreateOpts *);
  void (*destroy)(void *);
  int (*load)(void *, const char *const *, int, char *, int);
  int (*assign)(void *, const int *);
  int (*reset)(void *, const unsigned char *);
  int (*capture)(void *, int, int);
  int (*step_full)(void *, const double *, int, unsigned short *,
                   unsigned char *, unsigned char *, float *, float *,
                   unsigned char *, float *, int *);
} Fns;

static void fns_load(Fns *f) {
  memset(f, 0, sizeof *f);
  f->lib = dlopen("out/blaze/env/blaze_cpu.so", RTLD_NOW | RTLD_LOCAL);
  if (!f->lib) {
    fprintf(stderr, "dlopen blaze_cpu.so: %s\n", dlerror());
    exit(2);
  }
  f->create = (void *(*)(int, int, const BlazeCreateOpts *))must(
      f->lib, "blaze_create");
  f->destroy = (void (*)(void *))must(f->lib, "blaze_destroy");
  f->load = (int (*)(void *, const char *const *, int, char *, int))must(
      f->lib, "blaze_load_snapshots");
  f->assign = (int (*)(void *, const int *))must(f->lib, "blaze_assign");
  f->reset =
      (int (*)(void *, const unsigned char *))must(f->lib, "blaze_reset");
  f->capture = (int (*)(void *, int, int))must(f->lib, "blaze_capture");
  f->step_full =
      (int (*)(void *, const double *, int, unsigned short *, unsigned char *,
               unsigned char *, float *, float *, unsigned char *, float *,
               int *))must(f->lib, "blaze_step_full");
}

static int interact_container(Fns *f, void *env) {
  double act[13];
  int status[17];
  memset(act, 0, sizeof act);
  act[11] = 1.0; /* interact */
  memset(status, 0, sizeof status);
  if (f->step_full(env, act, 1, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                   status) != 0)
    return -1;
  return status[11];
}

static void *ready_env(Fns *f, const char *const *paths, int npaths,
                       int assign_slot) {
  void *env;
  char err[256];
  int idx = assign_slot;
  env = f->create(0, 1, NULL);
  if (!env)
    return NULL;
  if (f->load(env, paths, npaths, err, (int)sizeof err) < 0) {
    fprintf(stderr, "load: %s\n", err);
    f->destroy(env);
    return NULL;
  }
  if (f->assign(env, &idx) != 0 || f->reset(env, NULL) != 0) {
    f->destroy(env);
    return NULL;
  }
  return env;
}

int main(void) {
  Fns f;
  char dir[128];
  char p_none[160], p_one[160], p_two[160];
  const int one_wx[] = {10}, one_wy[] = {4}, one_wz[] = {8};
  const int two_wx[] = {10, 6}, two_wy[] = {4, 4}, two_wz[] = {8, 8};
  CuSnapshot chk;
  char err[256];

  snprintf(dir, sizeof dir, "/tmp/blaze_capcont_%d", (int)getpid());
  if (mkdir(dir, 0700) != 0) {
    perror("mkdir");
    return 2;
  }
  snprintf(p_none, sizeof p_none, "%s/none.bsnp", dir);
  snprintf(p_one, sizeof p_one, "%s/one.bsnp", dir);
  snprintf(p_two, sizeof p_two, "%s/two.bsnp", dir);
  if (write_bsnp(p_none, 0, NULL, NULL, NULL) != 0 ||
      write_bsnp(p_one, 1, one_wx, one_wy, one_wz) != 0 ||
      write_bsnp(p_two, 2, two_wx, two_wy, two_wz) != 0) {
    fprintf(stderr, "write_bsnp failed\n");
    return 2;
  }
  expect_true(blaze_snapshot_load(p_none, &chk, err, (int)sizeof err, 0),
              "load none");
  expect_eq_i(chk.ncont, 0, "none ncont");
  blaze_snapshot_free(&chk);
  expect_true(blaze_snapshot_load(p_one, &chk, err, (int)sizeof err, 0),
              "load one");
  expect_eq_i(chk.ncont, 1, "one ncont");
  blaze_snapshot_free(&chk);
  expect_true(blaze_snapshot_load(p_two, &chk, err, (int)sizeof err, 0),
              "load two");
  expect_eq_i(chk.ncont, 2, "two ncont");
  blaze_snapshot_free(&chk);

  fns_load(&f);

  {
    const char *paths[1] = {p_none};
    void *env = ready_env(&f, paths, 1, 0);
    expect_true(env != NULL, "none create");
    if (env) {
      expect_eq_i(interact_container(&f, env), 0, "none: no table to open");
      f.destroy(env);
    }
  }

  {
    const char *paths[1] = {p_one};
    void *env = ready_env(&f, paths, 1, 0);
    expect_true(env != NULL, "one create");
    if (env) {
      expect_eq_i(f.capture(env, 0, 0), 0, "overwrite same 1-cont slot");
      expect_eq_i(f.reset(env, NULL), 0, "reset after same-slot capture");
      expect_eq_i(interact_container(&f, env), 1,
                  "same-slot capture keeps table");
      f.destroy(env);
    }
  }

  {
    /* Capture a 1-cont env into a 0-cont baked slot (t0 overwrite). */
    const char *paths[2] = {p_none, p_one};
    void *env = ready_env(&f, paths, 2, 1);
    int z = 0;
    expect_true(env != NULL, "none+one create");
    if (env) {
      expect_eq_i(f.capture(env, 0, 0), 0, "capture 1-cont into 0-cont slot");
      expect_eq_i(f.assign(env, &z), 0, "assign captured slot");
      expect_eq_i(f.reset(env, NULL), 0, "reset from captured 0-slot");
      expect_eq_i(interact_container(&f, env), 1,
                  "captured-into-empty slot opens table");
      f.destroy(env);
    }
  }

  {
    /* Capture a 2-cont env into a 1-cont baked slot (the CUDA overflow). */
    const char *paths[2] = {p_one, p_two};
    void *env = ready_env(&f, paths, 2, 1);
    int z = 0;
    expect_true(env != NULL, "one+two create");
    if (env) {
      expect_eq_i(interact_container(&f, env), 1, "two-cont source opens");
      expect_eq_i(f.reset(env, NULL), 0, "re-reset source before capture");
      expect_eq_i(f.capture(env, 0, 0), 0, "capture 2-cont into 1-cont slot");
      expect_eq_i(f.assign(env, &z), 0, "assign grown slot");
      expect_eq_i(f.reset(env, NULL), 0, "reset from grown slot");
      expect_eq_i(interact_container(&f, env), 1,
                  "grown-slot capture still opens table");
      f.destroy(env);
    }
  }

  {
    const char *paths[1] = {p_one};
    void *env = ready_env(&f, paths, 1, 0);
    expect_true(env != NULL, "append create");
    if (env) {
      expect_eq_i(f.capture(env, 0, 1), 0, "append new slot");
      f.destroy(env);
    }
  }

  {
    /* Live env aliases slot coal (env->ore). Capture from a longer-ncoal
     * sibling must not free that buffer: the next capture from the live env
     * memcpy's e->ore. This is the CUDA trainer SEGV (libcuda D2D from a
     * cudaFree'd coal pointer). */
    char p_lo[160], p_hi[160];
    snprintf(p_lo, sizeof p_lo, "%s/coal_lo.bsnp", dir);
    snprintf(p_hi, sizeof p_hi, "%s/coal_hi.bsnp", dir);
    expect_eq_i(write_bsnp_ex(p_lo, 0, NULL, NULL, NULL, 4), 0, "write coal_lo");
    expect_eq_i(write_bsnp_ex(p_hi, 0, NULL, NULL, NULL, 9), 0, "write coal_hi");
    {
      const char *paths[2] = {p_lo, p_hi};
      void *env = f.create(0, 2, NULL);
      char lerr[256];
      int idx[2] = {0, 1};
      expect_true(env != NULL, "coal n=2 create");
      if (env) {
        expect_eq_i(f.load(env, paths, 2, lerr, (int)sizeof lerr) < 0 ? -1 : 0,
                    0, "coal load");
        expect_eq_i(f.assign(env, idx), 0, "coal assign lo/hi");
        expect_eq_i(f.reset(env, NULL), 0, "coal reset");
        expect_eq_i(f.capture(env, 1, 0), 0,
                    "capture hi-ncoal into lo slot (live env0 aliases lo)");
        expect_eq_i(f.capture(env, 0, 1), 0,
                    "capture from env still aliased to overwritten slot");
        expect_eq_i(f.reset(env, NULL), 0, "reset after coal overwrite");
        f.destroy(env);
      }
    }
    unlink(p_lo);
    unlink(p_hi);
  }

  dlclose(f.lib);
  unlink(p_none);
  unlink(p_one);
  unlink(p_two);
  rmdir(dir);
  if (g_fails) {
    fprintf(stderr, "%d fail(s)\n", g_fails);
    return 1;
  }
  printf("test_capture_cont: ok\n");
  return 0;
}
