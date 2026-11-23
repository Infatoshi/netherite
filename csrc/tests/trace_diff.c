/* First differing line of two matched trace files (Java oracle vs native port).
 *   trace_diff A.trace B.trace
 * Prints "identical: N lines" and exits 0, or the 1-based number of the first
 * differing line, the up-to-3 lines before it and the differing line from each
 * file, each d:/f: token followed by its decimal value, and exits 1. Names come
 * from the file names with a .trace suffix dropped. */
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 8192
#define CONTEXT 3

/* Copy s to out, appending "(decimal)" after every d:/f: token. */
static void annotate(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; s[i] && o + 1 < n; )
    {
        if ((s[i] == 'd' || s[i] == 'f') && s[i + 1] == ':' && (i == 0 || s[i - 1] == ' '))
        {
            int digits = s[i] == 'd' ? 16 : 8;
            char hex[17];
            size_t j = 0;
            while (j < (size_t)digits && isxdigit((unsigned char)s[i + 2 + j]))
            {
                hex[j] = s[i + 2 + j];
                ++j;
            }
            if (j == (size_t)digits)
            {
                hex[j] = 0;
                o += snprintf(out + o, n - o, "%c:%s(", s[i], hex);
                uint64_t bits = strtoull(hex, 0, 16);
                if (digits == 16)
                {
                    double v;
                    memcpy(&v, &bits, sizeof v);
                    o += snprintf(out + o, n - o, "%.17g", v);
                }
                else
                {
                    uint32_t b32 = (uint32_t)bits;
                    float v;
                    memcpy(&v, &b32, sizeof v);
                    o += snprintf(out + o, n - o, "%.9g", (double)v);
                }
                o += snprintf(out + o, n - o, ")");
                i += 2 + digits;
                continue;
            }
        }
        out[o++] = s[i++];
    }
    out[o] = 0;
}

static int getline_trim(FILE *f, char *buf)
{
    if (!fgets(buf, MAXLINE, f)) return 0;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return 1;
}

static const char *short_name(const char *path, char *buf, size_t n)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    if (len > 6 && !strcmp(base + len - 6, ".trace")) len -= 6;
    if (len >= n) len = n - 1;
    memcpy(buf, base, len);
    buf[len] = 0;
    return buf;
}

/* The up-to-CONTEXT lines before line n (1-based n), then line n itself, which
 * is cur. ring holds the last CONTEXT lines read, indexed by (line-1)%CONTEXT. */
static void show(const char *name, char (*ring)[MAXLINE], long n, const char *cur)
{
    static char buf[MAXLINE + 512];
    for (long i = n - CONTEXT + 1 > 1 ? n - CONTEXT + 1 : 1; i <= n + 1; ++i)
    {
        const char *line = i == n + 1 ? cur : ring[(i - 1) % CONTEXT];
        annotate(line, buf, sizeof buf);
        printf("%-5s %ld: %s\n", name, i, buf);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: trace_diff A.trace B.trace\n");
        return 2;
    }
    FILE *fa = fopen(argv[1], "rb");
    if (!fa)
    {
        perror(argv[1]);
        return 2;
    }
    FILE *fb = fopen(argv[2], "rb");
    if (!fb)
    {
        perror(argv[2]);
        return 2;
    }
    char na[64], nb[64];
    short_name(argv[1], na, sizeof na);
    short_name(argv[2], nb, sizeof nb);

    static char ring_a[CONTEXT][MAXLINE], ring_b[CONTEXT][MAXLINE];
    static char la[MAXLINE], lb[MAXLINE];
    long n = 0;
    for (;;)
    {
        int ha = getline_trim(fa, la), hb = getline_trim(fb, lb);
        if (!ha && !hb)
        {
            printf("identical: %ld lines\n", n);
            return 0;
        }
        if (!ha || !hb)
        {
            const char *first = ha ? nb : na;
            const char *other = ha ? na : nb;
            printf("%s ends first at line %ld: its file has %ld lines, %s continues\n", first, n + 1, n, other);
            return 1;
        }
        if (strcmp(la, lb))
        {
            printf("first difference at line %ld\n", n + 1);
            show(na, ring_a, n, la);
            show(nb, ring_b, n, lb);
            return 1;
        }
        snprintf(ring_a[n % CONTEXT], MAXLINE, "%s", la);
        snprintf(ring_b[n % CONTEXT], MAXLINE, "%s", lb);
        ++n;
    }
}