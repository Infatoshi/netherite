/* Checkpoint and fixture I/O: explicit LE fields, length checks, payload hash.
 * Schema 1 is weights-only (no optimizer resume state).
 * Every FP32 is encoded as IEEE-754 uint32 bits in little-endian order. */
#include "fixture.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- host LE writers / readers (no native struct I/O) ---- */

static void wr_u32(uint8_t **pp, uint32_t v) {
  uint8_t *p = *pp;
  p[0] = (uint8_t)(v & 0xffu);
  p[1] = (uint8_t)((v >> 8) & 0xffu);
  p[2] = (uint8_t)((v >> 16) & 0xffu);
  p[3] = (uint8_t)((v >> 24) & 0xffu);
  *pp = p + 4;
}

static void wr_u64(uint8_t **pp, uint64_t v) {
  uint8_t *p = *pp;
  for (int i = 0; i < 8; ++i)
    p[i] = (uint8_t)((v >> (8 * i)) & 0xffu);
  *pp = p + 8;
}

static void wr_i32(uint8_t **pp, int32_t v) { wr_u32(pp, (uint32_t)v); }

/* Encode host float as IEEE-754 bit pattern, then write those bits LE. */
static void wr_f32(uint8_t **pp, float f) {
  uint32_t bits = 0;
  memcpy(&bits, &f, sizeof(bits));
  wr_u32(pp, bits);
}

static int rd_u32(const uint8_t **pp, const uint8_t *end, uint32_t *out) {
  const uint8_t *p = *pp;
  if ((size_t)(end - p) < 4)
    return -1;
  *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
  *pp = p + 4;
  return 0;
}

static int rd_u64(const uint8_t **pp, const uint8_t *end, uint64_t *out) {
  const uint8_t *p = *pp;
  if ((size_t)(end - p) < 8)
    return -1;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= (uint64_t)p[i] << (8 * i);
  *out = v;
  *pp = p + 8;
  return 0;
}

static int rd_i32(const uint8_t **pp, const uint8_t *end, int32_t *out) {
  uint32_t u;
  if (rd_u32(pp, end, &u))
    return -1;
  *out = (int32_t)u;
  return 0;
}

/* Read LE IEEE-754 bits and rebuild a host float. */
static int rd_f32(const uint8_t **pp, const uint8_t *end, float *out) {
  uint32_t bits = 0;
  if (rd_u32(pp, end, &bits))
    return -1;
  memcpy(out, &bits, sizeof(*out));
  return 0;
}

uint64_t nn_fnv1a64(const void *data, size_t n) {
  const uint8_t *p = (const uint8_t *)data;
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; ++i) {
    h ^= (uint64_t)p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

/* Philox-ish mix for Gumbel uniforms (cpolicy_fwd.cu u01). */
float nn_hash_u01(uint64_t seed, uint32_t a, uint32_t b, uint32_t c) {
  uint64_t x = seed ^ (uint64_t)a * 0x9E3779B97F4A7C15ULL;
  x ^= (uint64_t)b * 0xBF58476D1CE4E5B9ULL;
  x ^= (uint64_t)c * 0x94D049BB133111EBULL;
  x ^= x >> 33;
  x *= 0xFF51AFD7ED558CCDULL;
  x ^= x >> 33;
  x *= 0xC4CEB9FE1A85EC53ULL;
  x ^= x >> 33;
  const float u = ((x >> 40) + 0.5f) * (1.0f / 16777216.0f);
  if (u < 1e-7f)
    return 1e-7f;
  if (u > 1.f - 1e-7f)
    return 1.f - 1e-7f;
  return u;
}

float nn_gumbel0(float u) {
  float e = -logf(u < 1e-20f ? 1e-20f : u);
  if (e < 1e-20f)
    e = 1e-20f;
  return -logf(e);
}

size_t nn_fixture_param_count(void) { return nn_model_param_floats(); }

void nn_fixture_pack_params(const float *const *tensors, float *dst) {
  size_t off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const size_t n = NN_TENSOR_FLOATS[t];
    memcpy(dst + off, tensors[t], n * sizeof(float));
    off += n;
  }
}

void nn_fixture_unpack_params(const float *src, float **tensors) {
  size_t off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const size_t n = NN_TENSOR_FLOATS[t];
    memcpy(tensors[t], src + off, n * sizeof(float));
    off += n;
  }
}

void nn_fixture_init_weights(float **tensors, uint64_t seed) {
  size_t global = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const size_t n = NN_TENSOR_FLOATS[t];
    float *w = tensors[t];
    /* Scale: keep conv/fc well-behaved for tests. */
    float scale = 0.02f;
    if (t == NN_T_CONV1_W || t == NN_T_CONV2_W)
      scale = 0.05f;
    if (t == NN_T_FC_W)
      scale = 0.01f;
    if (t == NN_T_HEADS_W || t == NN_T_VALUE_W)
      scale = 0.02f;
    if (t == NN_T_CONV1_B || t == NN_T_CONV2_B || t == NN_T_FC_B ||
        t == NN_T_HEADS_B || t == NN_T_VALUE_B)
      scale = 0.01f;
    for (size_t i = 0; i < n; ++i) {
      float u = nn_hash_u01(seed, (uint32_t)t, (uint32_t)(global + i), 0u);
      w[i] = (u - 0.5f) * 2.f * scale;
    }
    global += n;
  }
}

/* Header + table size upper bound for stack/heap buffer. */
static size_t table_bytes_max(void) {
  size_t n = 4 + 4 + 8 + 4; /* magic, schema, model_hash, n_tensors */
  for (int t = 0; t < NN_T_COUNT; ++t) {
    size_t name_len = strlen(NN_TENSOR_NAMES[t]);
    n += 4 + name_len + 4 + 4 + (size_t)NN_TENSOR_NDIM[t] * 4 + 8 + 8;
  }
  n += 8; /* payload_nbytes */
  return n;
}

int nn_fixture_save(const char *path, const float *const *tensors) {
  if (!path || !tensors)
    return -1;

  size_t payload_nbytes = 0;
  for (int t = 0; t < NN_T_COUNT; ++t)
    payload_nbytes += NN_TENSOR_FLOATS[t] * sizeof(float);

  const size_t hdr_cap = table_bytes_max() + 64;
  uint8_t *hdr = (uint8_t *)malloc(hdr_cap);
  if (!hdr)
    return -1;
  uint8_t *p = hdr;

  wr_u32(&p, (uint32_t)NN_CKPT_MAGIC);
  wr_u32(&p, (uint32_t)NN_CKPT_SCHEMA);
  wr_u64(&p, nn_model_hash());
  wr_u32(&p, (uint32_t)NN_T_COUNT);

  uint64_t offset = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const char *name = NN_TENSOR_NAMES[t];
    const uint32_t name_len = (uint32_t)strlen(name);
    wr_u32(&p, name_len);
    memcpy(p, name, name_len);
    p += name_len;
    wr_u32(&p, (uint32_t)NN_DTYPE_F32);
    wr_u32(&p, (uint32_t)NN_TENSOR_NDIM[t]);
    for (int d = 0; d < NN_TENSOR_NDIM[t]; ++d)
      wr_i32(&p, NN_TENSOR_SHAPE[t][d]);
    const uint64_t nbytes = (uint64_t)NN_TENSOR_FLOATS[t] * sizeof(float);
    wr_u64(&p, offset);
    wr_u64(&p, nbytes);
    offset += nbytes;
  }
  wr_u64(&p, payload_nbytes);

  const size_t hdr_len = (size_t)(p - hdr);
  if (hdr_len > hdr_cap) {
    free(hdr);
    return -1;
  }

  /* Assemble payload: each float as IEEE-754 bits, little-endian. */
  uint8_t *payload = (uint8_t *)malloc(payload_nbytes);
  if (!payload) {
    free(hdr);
    return -1;
  }
  uint8_t *pp = payload;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const size_t n = NN_TENSOR_FLOATS[t];
    for (size_t i = 0; i < n; ++i)
      wr_f32(&pp, tensors[t][i]);
  }
  if ((size_t)(pp - payload) != payload_nbytes) {
    free(hdr);
    free(payload);
    return -1;
  }
  const uint64_t payload_hash = nn_fnv1a64(payload, payload_nbytes);

  FILE *f = fopen(path, "wb");
  if (!f) {
    free(hdr);
    free(payload);
    return -1;
  }
  int rc = 0;
  if (fwrite(hdr, 1, hdr_len, f) != hdr_len)
    rc = -1;
  if (!rc && fwrite(payload, 1, payload_nbytes, f) != payload_nbytes)
    rc = -1;
  if (!rc) {
    uint8_t hash_le[8];
    uint8_t *hp = hash_le;
    wr_u64(&hp, payload_hash);
    if (fwrite(hash_le, 1, 8, f) != 8)
      rc = -1;
  }
  fclose(f);
  free(hdr);
  free(payload);
  return rc;
}

int nn_fixture_load(const char *path, float **tensors) {
  if (!path || !tensors)
    return -1;

  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }
  const size_t file_len = (size_t)sz;
  /* Minimum: magic+schema+hash+n_tensors + payload_nbytes + payload_hash */
  if (file_len < 4 + 4 + 8 + 4 + 8 + 8) {
    fclose(f);
    return -1;
  }

  uint8_t *buf = (uint8_t *)malloc(file_len);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (fread(buf, 1, file_len, f) != file_len) {
    free(buf);
    fclose(f);
    return -1;
  }
  fclose(f);

  const uint8_t *p = buf;
  const uint8_t *end = buf + file_len;

  uint32_t magic = 0, schema = 0, n_tensors = 0;
  uint64_t model_hash = 0;
  if (rd_u32(&p, end, &magic) || rd_u32(&p, end, &schema) ||
      rd_u64(&p, end, &model_hash) || rd_u32(&p, end, &n_tensors)) {
    free(buf);
    return -1;
  }
  if (magic != (uint32_t)NN_CKPT_MAGIC || schema != (uint32_t)NN_CKPT_SCHEMA ||
      model_hash != nn_model_hash() || n_tensors != (uint32_t)NN_T_COUNT) {
    free(buf);
    return -1;
  }

  uint64_t expect_off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    uint32_t name_len = 0, dtype = 0, ndim = 0;
    if (rd_u32(&p, end, &name_len)) {
      free(buf);
      return -1;
    }
    if ((size_t)(end - p) < name_len) {
      free(buf);
      return -1;
    }
    if (name_len != strlen(NN_TENSOR_NAMES[t]) ||
        memcmp(p, NN_TENSOR_NAMES[t], name_len) != 0) {
      free(buf);
      return -1;
    }
    p += name_len;
    if (rd_u32(&p, end, &dtype) || rd_u32(&p, end, &ndim)) {
      free(buf);
      return -1;
    }
    if (dtype != (uint32_t)NN_DTYPE_F32 ||
        ndim != (uint32_t)NN_TENSOR_NDIM[t]) {
      free(buf);
      return -1;
    }
    for (int d = 0; d < NN_TENSOR_NDIM[t]; ++d) {
      int32_t dim = 0;
      if (rd_i32(&p, end, &dim)) {
        free(buf);
        return -1;
      }
      if (dim != NN_TENSOR_SHAPE[t][d]) {
        free(buf);
        return -1;
      }
    }
    uint64_t offset = 0, nbytes = 0;
    if (rd_u64(&p, end, &offset) || rd_u64(&p, end, &nbytes)) {
      free(buf);
      return -1;
    }
    const uint64_t want = (uint64_t)NN_TENSOR_FLOATS[t] * sizeof(float);
    if (offset != expect_off || nbytes != want) {
      free(buf);
      return -1;
    }
    expect_off += want;
  }

  uint64_t payload_nbytes = 0;
  if (rd_u64(&p, end, &payload_nbytes)) {
    free(buf);
    return -1;
  }
  if (payload_nbytes != expect_off) {
    free(buf);
    return -1;
  }
  /* Payload + trailing hash must fit. */
  if ((size_t)(end - p) < payload_nbytes + 8) {
    free(buf);
    return -1;
  }
  const uint8_t *payload = p;
  p += payload_nbytes;
  uint64_t file_hash = 0;
  if (rd_u64(&p, end, &file_hash)) {
    free(buf);
    return -1;
  }
  /* Reject trailing garbage and truncated tail (hash must consume end). */
  if (p != end) {
    free(buf);
    return -1;
  }
  if (nn_fnv1a64(payload, (size_t)payload_nbytes) != file_hash) {
    free(buf);
    return -1;
  }

  const uint8_t *pay = payload;
  const uint8_t *pay_end = payload + payload_nbytes;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    const size_t n = NN_TENSOR_FLOATS[t];
    for (size_t i = 0; i < n; ++i) {
      if (rd_f32(&pay, pay_end, &tensors[t][i]) != 0) {
        free(buf);
        return -1;
      }
    }
  }
  if (pay != pay_end) {
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
}
