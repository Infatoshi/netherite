#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "oracle_initial.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static RlSnapHead header(void) {
  RlSnapHead h = {0};
  memcpy(h.magic, "BSNP", 4);
  h.version = 2;
  h.seed = 10;
  h.rx0 = h.ox = 100;
  h.rz0 = h.oz = 200;
  h.ry0 = 64;
  h.rnx = 2;
  h.rny = 3;
  h.rnz = 8;
  h.px = h.pz = .5;
  h.py = 65;
  h.health = 20;
  h.food = 20;
  h.yaw = 180;
  return h;
}
static void template_write(const char *path, RlSnapHead h) {
  FILE *f = fopen(path, "wb");
  assert(f);
  unsigned char zeros[96] = {0};
  unsigned count = 0;
  assert(fwrite(&h, sizeof h, 1, f) == 1);
  assert(fwrite(zeros, 1, 96, f) == 96);
  assert(fwrite(&count, 4, 1, f) == 1);
  assert(fwrite(zeros, 1, 48, f) == 48);
  fclose(f);
}
static void inputs(const char *blocks, const char *light, int allcoal) {
  FILE *b = fopen(blocks, "wb"), *l = fopen(light, "wb");
  assert(b && l);
  for (int y = 0; y < 3; y++)
    for (int z = 0; z < 8; z++)
      for (int x = 0; x < 2; x++) {
        unsigned i = (unsigned)((x * 3 + y) * 8 + z),
                 v = allcoal || i == 3 || i == 41 ? 256 : i;
        unsigned char pair[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
        assert(fwrite(pair, 1, 2, b) == 2);
        fputc((int)(240 - (i % 16)), l);
      }
  fclose(b);
  fclose(l);
}
int main(void) {
  char dir[] = "/tmp/netherite-oracle-fixture-XXXXXX";
  assert(mkdtemp(dir));
  char source[1024], blocks[1024], light[1024], out[1024], out2[1024],
      meta[1100], err[512];
  snprintf(source, sizeof source, "%s/template.bsnp", dir);
  snprintf(blocks, sizeof blocks, "%s/java.u16le", dir);
  snprintf(light, sizeof light, "%s/light.u8", dir);
  snprintf(out, sizeof out, "%s/derived\"quoted.bsnp", dir);
  snprintf(out2, sizeof out2, "%s/allcoal.bsnp", dir);
  RlSnapHead h = header();
  template_write(source, h);
  inputs(blocks, light, 0);
  assert(!oracle_fixture_write(source, blocks, light, out, err, sizeof err));
  CuSnapshot snap;
  assert(blaze_snapshot_load(out, &snap, err, sizeof err, 0));
  assert(!memcmp(&h, &snap.head, sizeof h));
  assert(snap.ncoal == 2 && snap.coal[0] == 100 && snap.coal[1] == 64 &&
         snap.coal[2] == 203 && snap.coal[3] == 101 && snap.coal[4] == 66 &&
         snap.coal[5] == 201);
  for (unsigned i = 0; i < 48; i++) {
    assert(snap.cells[i] == (i == 3 || i == 41 ? 256 : i));
    assert(snap.light[i] == 240 - i % 16);
  }
  assert(snap.n_mobs == 0 && snap.n_orbs == 0);
  blaze_snapshot_free(&snap);
  assert(oracle_fixture_write(source, blocks, light, out, err, sizeof err));
  assert(blaze_snapshot_load(out, &snap, err, sizeof err, 0));
  blaze_snapshot_free(&snap);
  snprintf(meta, sizeof meta, "%s.provenance.json", out);
  FILE *f = fopen(meta, "r");
  assert(f);
  char text[8192];
  assert(fgets(text, sizeof text, f));
  fclose(f);
  assert(strstr(text, "derived\\\"quoted.bsnp"));
  assert(strstr(text, "\"not_imported\":[\"biomes\",\"entities\",\"RNG "
                      "cursors\",\"world clocks\",\"scheduled updates\"]"));
  assert(!unlink(meta));
  inputs(blocks, light, 1);
  assert(!oracle_fixture_write(source, blocks, light, out2, err, sizeof err));
  assert(blaze_snapshot_load(out2, &snap, err, sizeof err, 0));
  assert(snap.ncoal == 48);
  for (unsigned i = 0; i < 48; i++)
    assert(snap.cells[i] == 256);
  blaze_snapshot_free(&snap);
  snprintf(meta, sizeof meta, "%s.provenance.json", out2);
  assert(!unlink(meta));
  assert(!unlink(out2));
  h.inv[0][0] = 17;
  h.inv[0][1] = 1;
  template_write(source, h);
  assert(oracle_fixture_write(source, blocks, light, out2, err, sizeof err));
  assert(access(out2, F_OK));
  h = header();
  h.version = 11;
  template_write(source, h);
  assert(oracle_fixture_write(source, blocks, light, out2, err, sizeof err));
  h = header();
  template_write(source, h);
  assert(!truncate(light, 47));
  assert(oracle_fixture_write(source, blocks, light, out2, err, sizeof err));
  assert(access(out2, F_OK));
  assert(!unlink(source));
  assert(!unlink(blocks));
  assert(!unlink(light));
  assert(!unlink(out));
  assert(!rmdir(dir));
  puts("oracle-fixture: native reload, transpose, uncapped48coal, provenance "
       "and rejection checks PASS");
  return 0;
}
