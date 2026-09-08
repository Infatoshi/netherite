#define _POSIX_C_SOURCE 200809L
#include "oracle_initial.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int fail(char *err, size_t cap, const char *msg) {
  snprintf(err, cap, "%s", msg);
  return -1;
}
void oracle_initial_free(OracleInitial *s) {
  if (s) {
    free(s->blocks);
    s->blocks = NULL;
  }
}
int oracle_initial_load(OracleInitial *s, const char *path, int64_t seed,
                        int empty, char *err, size_t cap) {
  memset(s, 0, sizeof *s);
  FILE *f = fopen(path, "rb");
  if (!f)
    return fail(err, cap, "initial snapshot unreadable");
  int rc = -1;
  RlSnapHead *h = &s->head;
  if (fread(h, sizeof *h, 1, f) != 1 || memcmp(h->magic, "BSNP", 4) ||
      h->version < 1 || h->version > BLAZE_SNAP_VERSION) {
    fail(err, cap, "invalid/truncated snapshot header");
    goto done;
  }
  if (h->seed != seed) {
    fail(err, cap, "snapshot seed differs from selected evaluation seed");
    goto done;
  }
  if (h->rnx < 1 || h->rnx > 256 || h->rny < 1 || h->rny > 256 || h->rnz < 1 ||
      h->rnz > 256 || h->ry0 < 0 || h->ry0 > 256 - h->rny ||
      h->rx0 < -30000000 || h->rx0 > 30000000 - h->rnx || h->rz0 < -30000000 ||
      h->rz0 > 30000000 - h->rnz || h->n_items > BLAZE_SNAP_MAX_ITEMS) {
    fail(err, cap, "snapshot dimensions/origin/item count out of bounds");
    goto done;
  }
  double x = h->px + h->ox, z = h->pz + h->oz;
  if (!isfinite(x) || !isfinite(h->py) || !isfinite(z) || !isfinite(h->yaw) ||
      !isfinite(h->pitch) || !isfinite(h->mx) || !isfinite(h->my) ||
      !isfinite(h->mz) || !isfinite(h->fall_distance) || !isfinite(h->health) ||
      x < h->rx0 || x >= h->rx0 + h->rnx || h->py < h->ry0 ||
      h->py >= h->ry0 + h->rny || z < h->rz0 || z >= h->rz0 + h->rnz ||
      h->pitch < -90 || h->pitch > 90 || h->fall_distance < 0 ||
      h->health <= 0 || h->health > 20 || h->food < 0 || h->food > 20 ||
      h->on_ground < 0 || h->on_ground > 1) {
    fail(err, cap, "snapshot initial pose/vitals invalid");
    goto done;
  }
  for (int i = 0; i < 37; i++) {
    if (h->inv[i][0] < 0 || h->inv[i][0] > 65535 || h->inv[i][1] < 0 ||
        h->inv[i][1] > 64 || h->inv[i][2] < 0 ||
        ((h->inv[i][0] == 0) != (h->inv[i][1] == 0))) {
      fail(err, cap, "invalid snapshot inventory slot");
      goto done;
    }
    if (empty && (h->inv[i][0] || h->inv[i][1])) {
      fail(err, cap, "snapshot is not empty inventory");
      goto done;
    }
  }
  if (empty && h->n_items) {
    fail(err, cap, "snapshot has loose item entities");
    goto done;
  }
  s->cells = (size_t)h->rnx * h->rny * h->rnz;
  s->blocks = malloc(s->cells * 2);
  if (!s->blocks) {
    fail(err, cap, "snapshot block allocation failed");
    goto done;
  }
  if (fseek(f, (long)(h->n_items * sizeof(RlSnapItem)), SEEK_CUR) ||
      fread(s->blocks, 2, s->cells, f) != s->cells) {
    fail(err, cap, "truncated snapshot block volume");
    goto done;
  }
  rc = 0;
done:
  fclose(f);
  if (rc)
    oracle_initial_free(s);
  return rc;
}
int oracle_initial_compare(OracleInitial *s, const char *path, char *err,
                           size_t cap) {
  if (!s || !s->blocks)
    return fail(err, cap, "initial snapshot not loaded");
  FILE *f = fopen(path, "rb");
  if (!f)
    return fail(err, cap, "Oracle initial block dump missing");
  struct stat st;
  if (fstat(fileno(f), &st) || st.st_size != (off_t)(s->cells * 2)) {
    fclose(f);
    return fail(err, cap, "Oracle block dump size mismatch");
  }
  const RlSnapHead *h = &s->head;
  s->mismatches = 0;
  s->compared = 0;
  for (int y = 0; y < h->rny; y++)
    for (int z = 0; z < h->rnz; z++)
      for (int x = 0; x < h->rnx; x++) {
        unsigned char b[2];
        if (fread(b, 1, 2, f) != 2) {
          fclose(f);
          return fail(err, cap, "Oracle block dump truncated");
        }
        unsigned actual = (unsigned)b[0] | ((unsigned)b[1] << 8),
                 expected = s->blocks[((size_t)x * h->rny + y) * h->rnz + z];
        if (actual != expected) {
          if (!s->mismatches) {
            s->first_x = h->rx0 + x;
            s->first_y = h->ry0 + y;
            s->first_z = h->rz0 + z;
            s->first_snapshot = expected;
            s->first_oracle = actual;
          }
          s->mismatches++;
        }
      }
  fclose(f);
  s->compared = 1;
  if (s->mismatches) {
    snprintf(err, cap,
             "initial block volume differs: %llu cells; first (%d,%d,%d) "
             "snapshot=%u Oracle=%u",
             (unsigned long long)s->mismatches, s->first_x, s->first_y,
             s->first_z, s->first_snapshot, s->first_oracle);
    return -1;
  }
  return 0;
}
