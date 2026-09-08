#define _POSIX_C_SOURCE 200809L
#include "oracle_initial.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define FNV_START UINT64_C(0xcbf29ce484222325)
static uint64_t hash(uint64_t h, const void *p, size_t n) {
  const unsigned char *b = p;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= UINT64_C(0x100000001b3);
  }
  return h;
}
static int fail(char *err, size_t cap, const char *msg) {
  snprintf(err, cap, "%s", msg);
  return -1;
}
static void quoted(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\')
      fprintf(f, "\\%c", c);
    else if (c < 32)
      fprintf(f, "\\u%04x", c);
    else
      fputc(c, f);
  }
  fputc('"', f);
}
static unsigned char *read_exact(const char *path, size_t bytes, char *err,
                                 size_t cap) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fail(err, cap, "Java dump unreadable");
    return NULL;
  }
  struct stat st;
  if (fstat(fileno(f), &st) || st.st_size != (off_t)bytes) {
    fclose(f);
    fail(err, cap, "Java dump has wrong byte length");
    return NULL;
  }
  unsigned char *b = malloc(bytes);
  if (!b || fread(b, 1, bytes, f) != bytes) {
    free(b);
    fclose(f);
    fail(err, cap, "Java dump truncated/allocation failed");
    return NULL;
  }
  fclose(f);
  return b;
}
static int write_hash(FILE *f, const void *data, size_t n, uint64_t *fnv,
                      uint64_t *bytes) {
  if (fwrite(data, 1, n, f) != n)
    return -1;
  *fnv = hash(*fnv, data, n);
  *bytes += n;
  return 0;
}
static FILE *create_new(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return NULL;
  FILE *f = fdopen(fd, "wb");
  if (!f)
    close(fd);
  return f;
}
int oracle_fixture_write(const char *template_path, const char *blocks_path,
                         const char *light_path, const char *out, char *err,
                         size_t cap) {
  if (!template_path || !blocks_path || !light_path || !out || !*out ||
      strlen(out) > 3000)
    return fail(err, cap, "invalid fixture paths");
  uint16_t endian = 1;
  if (*(unsigned char *)&endian != 1)
    return fail(err, cap, "BSNP writer requires little-endian host");
  FILE *source = fopen(template_path, "rb");
  if (!source)
    return fail(err, cap, "template unreadable");
  RlSnapHead h;
  if (fread(&h, sizeof h, 1, source) != 1) {
    fclose(source);
    return fail(err, cap, "template header truncated");
  }
  if (h.version != 2) {
    fclose(source);
    return fail(err, cap,
                "oracle-fixture requires a v2 template; refusing implicit "
                "trailer loss");
  }
  OracleInitial s;
  if (oracle_initial_load(&s, template_path, h.seed, 1, err, cap)) {
    fclose(source);
    return -1;
  }
  int rc = -1;
  FILE *dest = NULL, *meta = NULL;
  int own_dest = 0, own_meta = 0;
  unsigned char *raw = NULL, *rawlight = NULL, *light = NULL;
  char *meta_path = NULL;
  uint64_t source_hash = FNV_START, source_bytes = 0, output_hash = FNV_START,
           output_bytes = 0;
  unsigned ncoal = 0;
  /* v2 has exactly header, block volume, u32 coal count, xyz coal rows, light.
   * Validate the whole template before opening either output. */
  if (fseek(source, (long)(sizeof h + s.cells * 2), SEEK_SET)) {
    fail(err, cap, "template block seek failed");
    goto done;
  }
  unsigned old_coal;
  if (fread(&old_coal, sizeof old_coal, 1, source) != 1 || old_coal > s.cells) {
    fail(err, cap, "template coal count invalid");
    goto done;
  }
  struct stat st;
  uint64_t want = sizeof h + s.cells * 3 + sizeof old_coal +
                  (uint64_t)old_coal * 3 * sizeof(int);
  if (fstat(fileno(source), &st) || st.st_size < 0 ||
      (uint64_t)st.st_size != want) {
    fail(err, cap, "v2 template truncated or has undeclared trailing bytes");
    goto done;
  }
  rewind(source);
  unsigned char buf[8192];
  size_t got;
  while ((got = fread(buf, 1, sizeof buf, source))) {
    source_hash = hash(source_hash, buf, got);
    source_bytes += got;
  }
  if (ferror(source)) {
    fail(err, cap, "template read failed");
    goto done;
  }
  raw = read_exact(blocks_path, s.cells * 2, err, cap);
  if (!raw)
    goto done;
  rawlight = read_exact(light_path, s.cells, err, cap);
  if (!rawlight)
    goto done;
  light = malloc(s.cells);
  if (!light) {
    fail(err, cap, "light allocation failed");
    goto done;
  }
  for (int y = 0; y < h.rny; y++)
    for (int z = 0; z < h.rnz; z++)
      for (int x = 0; x < h.rnx; x++) {
        size_t ji = ((size_t)y * h.rnz + z) * h.rnx + x,
               si = ((size_t)x * h.rny + y) * h.rnz + z;
        unsigned v = (unsigned)raw[ji * 2] | ((unsigned)raw[ji * 2 + 1] << 8);
        s.blocks[si] = (uint16_t)v;
        light[si] = rawlight[ji];
        if ((v >> 4) == 16)
          ncoal++;
      }
  meta_path = malloc(strlen(out) + 17);
  if (!meta_path) {
    fail(err, cap, "provenance path allocation failed");
    goto done;
  }
  sprintf(meta_path, "%s.provenance.json", out);
  dest = create_new(out);
  if (!dest) {
    fail(err, cap, "output must be a new writable path");
    goto done;
  }
  own_dest = 1;
  meta = create_new(meta_path);
  if (!meta) {
    fail(err, cap, "provenance output already exists or is unwritable");
    goto done;
  }
  own_meta = 1;
  if (write_hash(dest, &h, sizeof h, &output_hash, &output_bytes) ||
      write_hash(dest, s.blocks, s.cells * 2, &output_hash, &output_bytes) ||
      write_hash(dest, &ncoal, sizeof ncoal, &output_hash, &output_bytes)) {
    fail(err, cap, "fixture write failed");
    goto done;
  }
  /* Uncapped, strictly ascending x,y,z, exactly rl_snapshot_write ordering. */
  for (int x = 0; x < h.rnx; x++)
    for (int y = 0; y < h.rny; y++)
      for (int z = 0; z < h.rnz; z++)
        if ((s.blocks[((size_t)x * h.rny + y) * h.rnz + z] >> 4) == 16) {
          int xyz[3] = {h.rx0 + x, h.ry0 + y, h.rz0 + z};
          if (write_hash(dest, xyz, sizeof xyz, &output_hash, &output_bytes)) {
            fail(err, cap, "coal list write failed");
            goto done;
          }
        }
  if (write_hash(dest, light, s.cells, &output_hash, &output_bytes) ||
      fflush(dest) || fsync(fileno(dest))) {
    fail(err, cap, "fixture flush failed");
    goto done;
  }
  fputs(
      "{\"schema\":\"netherite.oracle_fixture.v1\",\"version\":2,\"template\":",
      meta);
  quoted(meta, template_path);
  fputs(",\"blocks\":", meta);
  quoted(meta, blocks_path);
  fputs(",\"light\":", meta);
  quoted(meta, light_path);
  fputs(",\"output\":", meta);
  quoted(meta, out);
  fprintf(
      meta,
      ",\"template_fnv64\":\"%016" PRIx64 "\",\"template_bytes\":%" PRIu64
      ",\"blocks_fnv64\":\"%016" PRIx64
      "\",\"blocks_bytes\":%zu,\"light_fnv64\":\"%016" PRIx64
      "\",\"light_bytes\":%zu,\"output_fnv64\":\"%016" PRIx64
      "\",\"output_bytes\":%" PRIu64
      ",\"seed\":%lld,\"region\":[%d,%d,%d,%d,%d,%d],\"coal_count\":%u,\"input_"
      "order\":\"y,z,x\",\"output_order\":\"x,y,z\",\"light_encoding\":\"sky<<"
      "4|block\",\"preserved\":[\"entire template v2 header including pose and "
      "empty inventory\"],\"imported\":[\"block states\",\"packed "
      "light\"],\"derived\":[\"complete coal coordinate "
      "list\"],\"not_imported\":[\"biomes\",\"entities\",\"RNG "
      "cursors\",\"world clocks\",\"scheduled updates\"],\"limitations\":\"v2 "
      "reader defaults apply to omitted state; this is terrain/light transfer, "
      "not a full Java state snapshot\"}\n",
      source_hash, source_bytes, hash(FNV_START, raw, s.cells * 2), s.cells * 2,
      hash(FNV_START, rawlight, s.cells), s.cells, output_hash, output_bytes,
      h.seed, h.rx0, h.ry0, h.rz0, h.rnx, h.rny, h.rnz, ncoal);
  if (fflush(meta) || ferror(meta) || fsync(fileno(meta))) {
    fail(err, cap, "provenance write failed");
    goto done;
  }
  rc = 0;
done:
  fclose(source);
  if (dest && fclose(dest))
    rc = -1;
  if (meta && fclose(meta))
    rc = -1;
  if (rc) {
    if (own_dest)
      unlink(out);
    if (own_meta)
      unlink(meta_path);
  }
  free(meta_path);
  free(raw);
  free(rawlight);
  free(light);
  oracle_initial_free(&s);
  return rc;
}
#ifndef ORACLE_FIXTURE_NO_MAIN
int main(int argc, char **argv) {
  const char *template_path = NULL, *blocks = NULL, *light = NULL, *out = NULL;
  for (int i = 1; i < argc; i++) {
    const char **dst = !strcmp(argv[i], "--template") ? &template_path
                       : !strcmp(argv[i], "--blocks") ? &blocks
                       : !strcmp(argv[i], "--light")  ? &light
                       : !strcmp(argv[i], "--out")    ? &out
                                                      : NULL;
    if (!dst || ++i == argc || *dst) {
      fprintf(stderr, "usage: oracle-fixture --template EMPTY_V2.bsnp --blocks "
                      "JAVA.u16le --light JAVA.u8 --out NEW.bsnp\n");
      return 2;
    }
    *dst = argv[i];
  }
  char err[512] = "";
  if (oracle_fixture_write(template_path, blocks, light, out, err,
                           sizeof err)) {
    fprintf(stderr, "oracle-fixture: %s\n", err);
    return 2;
  }
  printf("%s\n%s.provenance.json\n", out, out);
  return 0;
}
#endif
