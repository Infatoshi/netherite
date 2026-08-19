#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Compact SHA-256. */
typedef struct {
    uint32_t s[8];
    uint64_t bits;
    unsigned char buf[64];
    size_t n;
} Sha256;

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_init(Sha256 *c)
{
    static const uint32_t iv[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(c->s, iv, sizeof(iv));
    c->bits = 0;
    c->n = 0;
}

static void sha256_block(Sha256 *c, const unsigned char *p)
{
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    uint32_t w[64], a, b, c2, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->s[0];
    b = c->s[1];
    c2 = c->s[2];
    d = c->s[3];
    e = c->s[4];
    f = c->s[5];
    g = c->s[6];
    h = c->s[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c2) ^ (b & c2);
        uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c2;
        c2 = b;
        b = a;
        a = t1 + t2;
    }
    c->s[0] += a;
    c->s[1] += b;
    c->s[2] += c2;
    c->s[3] += d;
    c->s[4] += e;
    c->s[5] += f;
    c->s[6] += g;
    c->s[7] += h;
}

static void sha256_update(Sha256 *c, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    c->bits += (uint64_t)n * 8;
    while (n) {
        size_t take = 64 - c->n;
        if (take > n)
            take = n;
        memcpy(c->buf + c->n, p, take);
        c->n += take;
        p += take;
        n -= take;
        if (c->n == 64) {
            sha256_block(c, c->buf);
            c->n = 0;
        }
    }
}

static void sha256_final(Sha256 *c, unsigned char out[32])
{
    unsigned char pad[64];
    uint64_t bits = c->bits;
    size_t i;
    pad[0] = 0x80;
    memset(pad + 1, 0, 63);
    if (c->n > 55) {
        sha256_update(c, pad, 64 - c->n);
        sha256_update(c, pad + 1, 56);
    } else {
        sha256_update(c, pad, 56 - c->n);
    }
    for (i = 0; i < 8; i++)
        pad[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_update(c, pad, 8);
    for (i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)(c->s[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->s[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->s[i] >> 8);
        out[i * 4 + 3] = (unsigned char)c->s[i];
    }
}

static int find_key(const char *line, const char *key, double *out)
{
    const char *p = strstr(line, key);
    char *end = NULL;
    if (!p)
        return 0;
    p += strlen(key);
    *out = strtod(p, &end);
    return end != p;
}

int main(int argc, char **argv)
{
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int ticks = 0;
    int have_pose = 0;
    double x = 0, y = 0, z = 0;
    Sha256 sha;
    unsigned char digest[32];
    char hex[65];
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <tape.jsonl>\n", argv[0]);
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    sha256_init(&sha);
    while ((n = getline(&line, &cap, fp)) > 0) {
        sha256_update(&sha, line, (size_t)n);
        if (!have_pose && strstr(line, "\"header\"") != NULL) {
            if (find_key(line, "\"x\":", &x) && find_key(line, "\"y\":", &y) &&
                find_key(line, "\"z\":", &z))
                have_pose = 1;
        }
        if (n > 5 && strncmp(line, "{\"t\":", 5) == 0)
            ticks++;
    }
    free(line);
    fclose(fp);
    if (!have_pose) {
        fprintf(stderr, "no header pose\n");
        return 1;
    }
    sha256_final(&sha, digest);
    for (i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    printf("ticks %d\n", ticks);
    printf("pose %.9g %.9g %.9g\n", x, y, z);
    printf("hash %s\n", hex);
    return 0;
}
