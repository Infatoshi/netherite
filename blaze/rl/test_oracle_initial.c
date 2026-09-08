#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "oracle_initial.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static RlSnapHead head(void) {
  RlSnapHead h = {0};
  memcpy(h.magic, "BSNP", 4);
  h.version = 11;
  h.seed = 10;
  h.ox = h.rx0 = 100;
  h.oz = h.rz0 = 200;
  h.ry0 = 64;
  h.rnx = 2;
  h.rny = 3;
  h.rnz = 4;
  h.px = .5;
  h.py = 65;
  h.pz = .5;
  h.yaw = 180;
  h.health = 20;
  h.food = 20;
  h.on_ground = 1;
  return h;
}
static void snapshot(const char *p, RlSnapHead h, int short_blocks) {
  FILE *f = fopen(p, "wb");
  assert(f);
  assert(fwrite(&h, sizeof h, 1, f) == 1);
  for (int i = 0; i < (short_blocks ? 23 : 24); i++) {
    uint16_t v = (uint16_t)i;
    assert(fwrite(&v, 2, 1, f) == 1);
  }
  fclose(f);
}
int main(void) {
  char dir[] = "/tmp/netherite-oracle-initial-XXXXXX";
  assert(mkdtemp(dir));
  char snap[1024], blocks[1024], err[512];
  snprintf(snap, sizeof snap, "%s/source.bsnp", dir);
  snprintf(blocks, sizeof blocks, "%s/java.u16le", dir);
  OracleInitial s;
  RlSnapHead h = head();
  snapshot(snap, h, 0);
  assert(!oracle_initial_load(&s, snap, 10, 1, err, sizeof err));
  assert(s.cells == 24);
  FILE *f = fopen(blocks, "wb");
  assert(f);
  for (int y = 0; y < 3; y++)
    for (int z = 0; z < 4; z++)
      for (int x = 0; x < 2; x++) {
        unsigned v = (unsigned)((x * 3 + y) * 4 + z);
        unsigned char b[2] = {(unsigned char)v, 0};
        assert(fwrite(b, 1, 2, f) == 2);
      }
  fclose(f);
  assert(!oracle_initial_compare(&s, blocks, err, sizeof err));
  assert(s.compared && !s.mismatches);
  f = fopen(blocks, "r+b");
  assert(f);
  fputc(255, f);
  fclose(f);
  assert(oracle_initial_compare(&s, blocks, err, sizeof err));
  assert(s.mismatches == 1 && s.first_x == 100 && s.first_y == 64 &&
         s.first_z == 200 && s.first_snapshot == 0 && s.first_oracle == 255);
  oracle_initial_free(&s);
  for (int mode = 0; mode < 8; mode++) {
    h = head();
    if (mode == 0)
      h.seed = 11;
    if (mode == 1)
      h.px = NAN;
    if (mode == 2)
      h.rnx = 2147483647;
    if (mode == 3) {
      h.inv[36][0] = 17;
      h.inv[36][1] = 1;
    }
    if (mode == 4)
      h.n_items = 1;
    if (mode == 5)
      h.pitch = 91;
    if (mode == 6)
      h.health = INFINITY;
    snapshot(snap, h, mode == 7);
    assert(oracle_initial_load(&s, snap, 10, 1, err, sizeof err));
  }
  assert(!unlink(snap));
  assert(!unlink(blocks));
  assert(!rmdir(dir));
  puts("oracle_initial: transpose, mismatch coordinate and 8 invalid snapshots "
       "PASS");
  return 0;
}
