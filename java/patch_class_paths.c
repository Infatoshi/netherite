/*
 * Rewrite CONSTANT_Utf8 path constants in .class files (constant-pool parse).
 *
 * Usage: patch_class_paths OLD NEW FILE...
 * Replaces every occurrence of OLD with NEW inside Utf8 entries, fixing lengths.
 *
 * Build with -DPATCH_CLASS_PATHS_SELFTEST for the self-test binary.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
        CP_UTF8 = 1,
        CP_INTEGER = 3,
        CP_FLOAT = 4,
        CP_LONG = 5,
        CP_DOUBLE = 6,
        CP_CLASS = 7,
        CP_STRING = 8,
        CP_FIELDREF = 9,
        CP_METHODREF = 10,
        CP_INTERFACE_METHODREF = 11,
        CP_NAME_AND_TYPE = 12,
        CP_METHOD_HANDLE = 15,
        CP_METHOD_TYPE = 16,
        CP_DYNAMIC = 17,
        CP_INVOKE_DYNAMIC = 18,
        CP_MODULE = 19,
        CP_PACKAGE = 20,
};

/* Read big-endian u16 from two bytes. */
static unsigned u16be(const unsigned char *p)
{
        return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

/* Write big-endian u16. */
static void put_u16be(unsigned char *p, unsigned v)
{
        p[0] = (unsigned char)((v >> 8) & 0xff);
        p[1] = (unsigned char)(v & 0xff);
}

/*
 * CLI validation shared by the tool and the self-test.
 * Requires argc >= 4 and a non-empty OLD. Returns 0 if ok, -1 if bad.
 */
static int cli_validate(int argc, char **argv)
{
        if (argc < 4)
                return -1;
        if (argv[1][0] == '\0')
                return -1;
        return 0;
}

/*
 * Grow *buf so *cap >= need. Doubles capacity until large enough.
 * Overflow-safe: refuses *cap * 2 when *cap > SIZE_MAX/2.
 * Returns 0 on success, -1 on overflow or OOM.
 */
static int buf_ensure(unsigned char **buf, size_t *cap, size_t need)
{
        size_t nc;
        unsigned char *nb;

        if (need <= *cap)
                return 0;
        nc = *cap ? *cap : 64;
        while (nc < need) {
                if (nc > SIZE_MAX / 2)
                        return -1;
                nc *= 2;
        }
        nb = (unsigned char *)realloc(*buf, nc);
        if (!nb)
                return -1;
        *buf = nb;
        *cap = nc;
        return 0;
}

/*
 * Non-overlapping left-to-right replace (Python bytes.replace).
 * old_len must be non-zero (caller / CLI rejects empty OLD).
 * On success sets *out (malloc'd) and *out_len; returns 0.
 * Returns -1 on OOM, empty OLD, or result length > 65535.
 */
static int utf8_replace(const unsigned char *s, size_t slen,
                        const unsigned char *old, size_t old_len,
                        const unsigned char *newb, size_t new_len,
                        unsigned char **out, size_t *out_len, int *had_match)
{
        size_t cap, n, i;
        unsigned char *buf;
        const unsigned char *p, *end;

        *had_match = 0;
        *out = NULL;
        *out_len = 0;
        if (old_len == 0)
                return -1;

        /* First pass: count matches and compute size. */
        n = 0;
        p = s;
        end = s + slen;
        while ((size_t)(end - p) >= old_len) {
                if (memcmp(p, old, old_len) == 0) {
                        n++;
                        p += old_len;
                } else {
                        p++;
                }
        }
        if (n == 0) {
                buf = (unsigned char *)malloc(slen ? slen : 1);
                if (!buf)
                        return -1;
                if (slen)
                        memcpy(buf, s, slen);
                *out = buf;
                *out_len = slen;
                return 0;
        }
        *had_match = 1;

        if (new_len > old_len) {
                size_t delta = new_len - old_len;
                size_t grow;

                /* Division guard before delta * n. */
                if (n > 0 && delta > SIZE_MAX / n)
                        return -1;
                grow = delta * n;
                if (slen > SIZE_MAX - grow)
                        return -1;
                cap = slen + grow;
        } else {
                size_t delta = old_len - new_len;
                size_t shrink;

                if (n > 0 && delta > SIZE_MAX / n)
                        return -1;
                shrink = delta * n;
                if (shrink > slen)
                        return -1;
                cap = slen - shrink;
        }
        if (cap > 65535)
                return -1;

        buf = (unsigned char *)malloc(cap ? cap : 1);
        if (!buf)
                return -1;

        i = 0;
        p = s;
        while (p < end) {
                if ((size_t)(end - p) >= old_len &&
                    memcmp(p, old, old_len) == 0) {
                        if (new_len)
                                memcpy(buf + i, newb, new_len);
                        i += new_len;
                        p += old_len;
                } else {
                        buf[i++] = *p++;
                }
        }
        *out = buf;
        *out_len = i;
        return 0;
}

/*
 * Patch class file bytes. On success returns 0 and sets *out_data (malloc'd),
 * *out_size, *changed (number of Utf8 entries that contained OLD).
 * On error returns -1 and sets *err (static string or caller-owned via errno).
 * Does not free *out_data on failure (always NULL).
 */
static int patch_class(const unsigned char *data, size_t size,
                       const unsigned char *old, size_t old_len,
                       const unsigned char *newb, size_t new_len,
                       unsigned char **out_data, size_t *out_size, int *changed,
                       const char **err)
{
        unsigned count, n;
        size_t i, out_cap, out_len;
        unsigned char *out;
        int ch = 0;

        *out_data = NULL;
        *out_size = 0;
        *changed = 0;
        *err = NULL;

        if (old_len == 0) {
                *err = "empty OLD";
                return -1;
        }
        if (size < 10) {
                *err = "truncated class file";
                return -1;
        }
        if (data[0] != 0xca || data[1] != 0xfe || data[2] != 0xba ||
            data[3] != 0xbe) {
                *err = "not a class file";
                return -1;
        }

        count = u16be(data + 8);
        if (count == 0) {
                *err = "constant_pool_count is zero";
                return -1;
        }
        /* Output may grow; start with size + headroom for longer replacements. */
        if (size > SIZE_MAX - 4096)
                out_cap = size;
        else
                out_cap = size + 4096;
        out = (unsigned char *)malloc(out_cap ? out_cap : 1);
        if (!out) {
                *err = "out of memory";
                return -1;
        }
        memcpy(out, data, 10);
        out_len = 10;
        i = 10;
        n = 1;

        while (n < count) {
                unsigned tag;
                size_t need;

                if (i >= size) {
                        free(out);
                        *err = "truncated constant pool";
                        return -1;
                }
                tag = data[i];

                if (tag == CP_UTF8) {
                        unsigned ln;
                        size_t new_slen;
                        unsigned char *repl;
                        int had;
                        size_t entry_need;

                        if (i + 3 > size) {
                                free(out);
                                *err = "truncated Utf8 length";
                                return -1;
                        }
                        ln = u16be(data + i + 1);
                        if (i + 3 + ln > size) {
                                free(out);
                                *err = "truncated Utf8 bytes";
                                return -1;
                        }
                        if (utf8_replace(data + i + 3, ln, old, old_len, newb,
                                         new_len, &repl, &new_slen, &had) != 0) {
                                free(out);
                                *err = "Utf8 replacement too long or OOM";
                                return -1;
                        }
                        if (had)
                                ch++;
                        entry_need = 3 + new_slen;
                        if (out_len > SIZE_MAX - entry_need) {
                                free(repl);
                                free(out);
                                *err = "out of memory";
                                return -1;
                        }
                        if (buf_ensure(&out, &out_cap, out_len + entry_need) != 0) {
                                free(repl);
                                free(out);
                                *err = "out of memory";
                                return -1;
                        }
                        out[out_len++] = 1;
                        put_u16be(out + out_len, (unsigned)new_slen);
                        out_len += 2;
                        if (new_slen)
                                memcpy(out + out_len, repl, new_slen);
                        out_len += new_slen;
                        free(repl);
                        i += 3 + ln;
                        n += 1;
                        continue;
                }

                /* Fixed-size entries (including tag byte). */
                if (tag == CP_CLASS || tag == CP_STRING || tag == CP_METHOD_TYPE ||
                    tag == CP_MODULE || tag == CP_PACKAGE) {
                        need = 3;
                } else if (tag == CP_METHOD_HANDLE) {
                        need = 4;
                } else if (tag == CP_INTEGER || tag == CP_FLOAT ||
                           tag == CP_FIELDREF || tag == CP_METHODREF ||
                           tag == CP_INTERFACE_METHODREF ||
                           tag == CP_NAME_AND_TYPE || tag == CP_DYNAMIC ||
                           tag == CP_INVOKE_DYNAMIC) {
                        need = 5;
                } else if (tag == CP_LONG || tag == CP_DOUBLE) {
                        /* Double-slot entry must leave a reserved following index. */
                        if (n + 1 >= count) {
                                free(out);
                                *err = "Long/Double missing reserved slot";
                                return -1;
                        }
                        need = 9;
                } else {
                        free(out);
                        *err = "unknown cp tag";
                        return -1;
                }

                if (i + need > size) {
                        free(out);
                        *err = "truncated constant pool entry";
                        return -1;
                }
                if (out_len > SIZE_MAX - need) {
                        free(out);
                        *err = "out of memory";
                        return -1;
                }
                if (buf_ensure(&out, &out_cap, out_len + need) != 0) {
                        free(out);
                        *err = "out of memory";
                        return -1;
                }
                memcpy(out + out_len, data + i, need);
                out_len += need;
                i += need;
                n += (tag == CP_LONG || tag == CP_DOUBLE) ? 2u : 1u;
        }

        /* Remainder of the class file after the constant pool. */
        if (i > size) {
                free(out);
                *err = "truncated class file";
                return -1;
        }
        {
                size_t rest = size - i;
                if (out_len > SIZE_MAX - rest) {
                        free(out);
                        *err = "out of memory";
                        return -1;
                }
                if (buf_ensure(&out, &out_cap, out_len + rest) != 0) {
                        free(out);
                        *err = "out of memory";
                        return -1;
                }
                if (rest)
                        memcpy(out + out_len, data + i, rest);
                out_len += rest;
        }

        *out_data = out;
        *out_size = out_len;
        *changed = ch;
        return 0;
}

#ifndef PATCH_CLASS_PATHS_SELFTEST

/* Write patched bytes via a temporary sibling, then rename. Keeps mode. */
static int write_atomic(const char *path, const unsigned char *data, size_t size,
                        mode_t mode, const char **err)
{
        char *tmp = NULL;
        size_t plen;
        int fd = -1;
        size_t off;
        ssize_t w;

        plen = strlen(path);
        /* path + ".tmp.XXXXXX" + NUL */
        tmp = (char *)malloc(plen + 16);
        if (!tmp) {
                *err = "out of memory";
                return -1;
        }
        memcpy(tmp, path, plen);
        memcpy(tmp + plen, ".tmp.XXXXXX", 12);

        fd = mkstemp(tmp);
        if (fd < 0) {
                free(tmp);
                *err = "mkstemp failed";
                return -1;
        }
        if (fchmod(fd, mode) != 0) {
                close(fd);
                unlink(tmp);
                free(tmp);
                *err = "fchmod failed";
                return -1;
        }
        off = 0;
        while (off < size) {
                w = write(fd, data + off, size - off);
                if (w < 0) {
                        if (errno == EINTR)
                                continue;
                        close(fd);
                        unlink(tmp);
                        free(tmp);
                        *err = "write failed";
                        return -1;
                }
                off += (size_t)w;
        }
        if (fsync(fd) != 0) {
                close(fd);
                unlink(tmp);
                free(tmp);
                *err = "fsync failed";
                return -1;
        }
        if (close(fd) != 0) {
                unlink(tmp);
                free(tmp);
                *err = "close failed";
                return -1;
        }
        if (rename(tmp, path) != 0) {
                unlink(tmp);
                free(tmp);
                *err = "rename failed";
                return -1;
        }
        free(tmp);
        return 0;
}

static int patch_file(const char *path, const unsigned char *old, size_t old_len,
                      const unsigned char *newb, size_t new_len)
{
        FILE *f;
        unsigned char *data = NULL, *out = NULL;
        size_t size = 0, out_size = 0, nread;
        int changed = 0;
        const char *err = NULL;
        struct stat st;
        long flen;

        if (stat(path, &st) != 0) {
                fprintf(stderr, "%s: stat: %s\n", path, strerror(errno));
                return -1;
        }
        f = fopen(path, "rb");
        if (!f) {
                fprintf(stderr, "%s: open: %s\n", path, strerror(errno));
                return -1;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
                fprintf(stderr, "%s: seek: %s\n", path, strerror(errno));
                fclose(f);
                return -1;
        }
        flen = ftell(f);
        if (flen < 0) {
                fprintf(stderr, "%s: ftell failed\n", path);
                fclose(f);
                return -1;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
                fprintf(stderr, "%s: seek: %s\n", path, strerror(errno));
                fclose(f);
                return -1;
        }
        size = (size_t)flen;
        data = (unsigned char *)malloc(size ? size : 1);
        if (!data) {
                fprintf(stderr, "%s: out of memory\n", path);
                fclose(f);
                return -1;
        }
        nread = fread(data, 1, size, f);
        fclose(f);
        if (nread != size) {
                fprintf(stderr, "%s: short read\n", path);
                free(data);
                return -1;
        }

        if (patch_class(data, size, old, old_len, newb, new_len, &out, &out_size,
                        &changed, &err) != 0) {
                fprintf(stderr, "%s: %s\n", path, err ? err : "patch failed");
                free(data);
                return -1;
        }
        free(data);

        if (changed) {
                if (write_atomic(path, out, out_size, st.st_mode & 07777, &err) !=
                    0) {
                        fprintf(stderr, "%s: %s\n", path, err ? err : "write failed");
                        free(out);
                        return -1;
                }
        }
        free(out);
        printf("%s: %d utf8 entries patched\n", path, changed);
        return 0;
}

static void usage(const char *argv0)
{
        fprintf(stderr, "Usage: %s OLD NEW FILE...\n", argv0);
}

int main(int argc, char **argv)
{
        const unsigned char *old, *newb;
        size_t old_len, new_len;
        int i, rc = 0;

        if (cli_validate(argc, argv) != 0) {
                usage(argv[0]);
                return 2;
        }
        old = (const unsigned char *)argv[1];
        newb = (const unsigned char *)argv[2];
        old_len = strlen(argv[1]);
        new_len = strlen(argv[2]);

        for (i = 3; i < argc; i++) {
                if (patch_file(argv[i], old, old_len, newb, new_len) != 0)
                        rc = 1;
        }
        return rc;
}

#else /* PATCH_CLASS_PATHS_SELFTEST */

/* Return 1 if needle occurs in haystack. */
static int bytes_contains(const unsigned char *hay, size_t hlen,
                          const unsigned char *needle, size_t nlen)
{
        size_t i;
        if (nlen == 0)
                return 1;
        if (nlen > hlen)
                return 0;
        for (i = 0; i + nlen <= hlen; i++) {
                if (memcmp(hay + i, needle, nlen) == 0)
                        return 1;
        }
        return 0;
}

/* --- minimal class-file builders for the self-test --- */

static void append_bytes(unsigned char **buf, size_t *len, size_t *cap,
                         const void *src, size_t n)
{
        if (*len + n > *cap) {
                size_t nc = *cap ? *cap * 2 : 64;
                unsigned char *nb;
                while (nc < *len + n)
                        nc *= 2;
                nb = (unsigned char *)realloc(*buf, nc);
                if (!nb) {
                        fprintf(stderr, "OOM in test builder\n");
                        exit(1);
                }
                *buf = nb;
                *cap = nc;
        }
        memcpy(*buf + *len, src, n);
        *len += n;
}

static void append_u8(unsigned char **buf, size_t *len, size_t *cap, unsigned v)
{
        unsigned char b = (unsigned char)v;
        append_bytes(buf, len, cap, &b, 1);
}

static void append_u16(unsigned char **buf, size_t *len, size_t *cap, unsigned v)
{
        unsigned char b[2];
        put_u16be(b, v);
        append_bytes(buf, len, cap, b, 2);
}

/* Build: magic + versions + cp_count + entries... + 8 zero trail bytes. */
static unsigned char *build_class(unsigned cp_count, const unsigned char *cp,
                                  size_t cp_len, size_t *out_len)
{
        unsigned char *buf = NULL;
        size_t len = 0, cap = 0;
        unsigned char trail[8];

        append_u8(&buf, &len, &cap, 0xca);
        append_u8(&buf, &len, &cap, 0xfe);
        append_u8(&buf, &len, &cap, 0xba);
        append_u8(&buf, &len, &cap, 0xbe);
        append_u16(&buf, &len, &cap, 0);    /* minor */
        append_u16(&buf, &len, &cap, 52);   /* major (Java 8) */
        append_u16(&buf, &len, &cap, cp_count);
        append_bytes(&buf, &len, &cap, cp, cp_len);
        memset(trail, 0, sizeof trail);
        append_bytes(&buf, &len, &cap, trail, sizeof trail);
        *out_len = len;
        return buf;
}

static void cp_utf8(unsigned char **buf, size_t *len, size_t *cap, const char *s)
{
        size_t n = strlen(s);
        append_u8(buf, len, cap, CP_UTF8);
        append_u16(buf, len, cap, (unsigned)n);
        append_bytes(buf, len, cap, s, n);
}

static void cp_long(unsigned char **buf, size_t *len, size_t *cap)
{
        unsigned char z[8];
        memset(z, 0, 8);
        append_u8(buf, len, cap, CP_LONG);
        append_bytes(buf, len, cap, z, 8);
}

static void cp_class(unsigned char **buf, size_t *len, size_t *cap, unsigned idx)
{
        append_u8(buf, len, cap, CP_CLASS);
        append_u16(buf, len, cap, idx);
}

static int expect_ok(const char *name, const unsigned char *data, size_t size,
                     const char *old, const char *newb, int want_changed,
                     const char *want_utf8)
{
        unsigned char *out = NULL;
        size_t out_size = 0;
        int changed = 0;
        const char *err = NULL;
        size_t i, wlen;

        if (patch_class(data, size, (const unsigned char *)old, strlen(old),
                        (const unsigned char *)newb, strlen(newb), &out, &out_size,
                        &changed, &err) != 0) {
                fprintf(stderr, "FAIL %s: unexpected error: %s\n", name,
                        err ? err : "?");
                free(out);
                return 1;
        }
        if (changed != want_changed) {
                fprintf(stderr, "FAIL %s: changed=%d want=%d\n", name, changed,
                        want_changed);
                free(out);
                return 1;
        }
        if (want_utf8) {
                wlen = strlen(want_utf8);
                /* Scan Utf8 entries in output for exact match of want_utf8. */
                {
                        unsigned count = u16be(out + 8);
                        unsigned n = 1;
                        int found = 0;
                        i = 10;
                        while (n < count) {
                                unsigned tag = out[i];
                                if (tag == CP_UTF8) {
                                        unsigned ln = u16be(out + i + 1);
                                        if (ln == wlen &&
                                            memcmp(out + i + 3, want_utf8, wlen) ==
                                                0)
                                                found = 1;
                                        /* Also ensure OLD is gone when NEW differs. */
                                        if (strlen(old) &&
                                            bytes_contains(out + i + 3, ln,
                                                           (const unsigned char *)
                                                               old,
                                                           strlen(old))) {
                                                fprintf(stderr,
                                                        "FAIL %s: OLD still in "
                                                        "Utf8\n",
                                                        name);
                                                free(out);
                                                return 1;
                                        }
                                        i += 3 + ln;
                                        n += 1;
                                } else if (tag == CP_LONG || tag == CP_DOUBLE) {
                                        i += 9;
                                        n += 2;
                                } else if (tag == CP_CLASS || tag == CP_STRING ||
                                           tag == CP_METHOD_TYPE ||
                                           tag == CP_MODULE || tag == CP_PACKAGE) {
                                        i += 3;
                                        n += 1;
                                } else if (tag == CP_METHOD_HANDLE) {
                                        i += 4;
                                        n += 1;
                                } else {
                                        i += 5;
                                        n += 1;
                                }
                        }
                        if (!found) {
                                fprintf(stderr, "FAIL %s: want Utf8 %s not found\n",
                                        name, want_utf8);
                                free(out);
                                return 1;
                        }
                }
        }
        free(out);
        fprintf(stderr, "ok %s\n", name);
        return 0;
}

static int expect_err(const char *name, const unsigned char *data, size_t size,
                      const char *old, const char *newb)
{
        unsigned char *out = NULL;
        size_t out_size = 0;
        int changed = 0;
        const char *err = NULL;

        if (patch_class(data, size, (const unsigned char *)old, strlen(old),
                        (const unsigned char *)newb, strlen(newb), &out, &out_size,
                        &changed, &err) == 0) {
                fprintf(stderr, "FAIL %s: expected error, got ok changed=%d\n",
                        name, changed);
                free(out);
                return 1;
        }
        free(out);
        fprintf(stderr, "ok %s (%s)\n", name, err ? err : "error");
        return 0;
}

/* Verify non-Utf8 payload bytes are untouched across a successful patch. */
static int expect_preserve_non_utf8(const char *name, const unsigned char *data,
                                    size_t size, const char *old, const char *newb)
{
        unsigned char *out = NULL;
        size_t out_size = 0;
        int changed = 0;
        const char *err = NULL;
        unsigned count, n;
        size_t i_in, i_out;

        if (patch_class(data, size, (const unsigned char *)old, strlen(old),
                        (const unsigned char *)newb, strlen(newb), &out, &out_size,
                        &changed, &err) != 0) {
                fprintf(stderr, "FAIL %s: %s\n", name, err ? err : "?");
                free(out);
                return 1;
        }
        /* Walk both pools in lockstep; fixed entries must match. */
        count = u16be(data + 8);
        n = 1;
        i_in = 10;
        i_out = 10;
        while (n < count) {
                unsigned tag = data[i_in];
                if (tag != out[i_out]) {
                        fprintf(stderr, "FAIL %s: tag mismatch at n=%u\n", name, n);
                        free(out);
                        return 1;
                }
                if (tag == CP_UTF8) {
                        unsigned ln_in = u16be(data + i_in + 1);
                        unsigned ln_out = u16be(out + i_out + 1);
                        i_in += 3 + ln_in;
                        i_out += 3 + ln_out;
                        n += 1;
                } else if (tag == CP_LONG || tag == CP_DOUBLE) {
                        if (memcmp(data + i_in, out + i_out, 9) != 0) {
                                fprintf(stderr, "FAIL %s: long/double mutated\n",
                                        name);
                                free(out);
                                return 1;
                        }
                        i_in += 9;
                        i_out += 9;
                        n += 2;
                } else {
                        size_t need;
                        if (tag == CP_CLASS || tag == CP_STRING ||
                            tag == CP_METHOD_TYPE || tag == CP_MODULE ||
                            tag == CP_PACKAGE)
                                need = 3;
                        else if (tag == CP_METHOD_HANDLE)
                                need = 4;
                        else
                                need = 5;
                        if (memcmp(data + i_in, out + i_out, need) != 0) {
                                fprintf(stderr, "FAIL %s: fixed entry mutated\n",
                                        name);
                                free(out);
                                return 1;
                        }
                        i_in += need;
                        i_out += need;
                        n += 1;
                }
        }
        /* Trailing class body must match. */
        {
                size_t rest_in = size - i_in;
                size_t rest_out = out_size - i_out;
                if (rest_in != rest_out ||
                    (rest_in && memcmp(data + i_in, out + i_out, rest_in) != 0)) {
                        fprintf(stderr, "FAIL %s: trailing bytes mutated\n", name);
                        free(out);
                        return 1;
                }
        }
        free(out);
        fprintf(stderr, "ok %s\n", name);
        return 0;
}

int main(void)
{
        int fails = 0;
        unsigned char *cp = NULL, *cls = NULL;
        size_t cp_len = 0, cp_cap = 0, cls_len = 0;

        /* same-length */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "hello");
        cls = build_class(2, cp, cp_len, &cls_len);
        fails += expect_ok("same-length", cls, cls_len, "hello", "world", 1, "world");
        free(cls);
        free(cp);

        /* shorter */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "foobar");
        cls = build_class(2, cp, cp_len, &cls_len);
        fails += expect_ok("shorter", cls, cls_len, "foobar", "baz", 1, "baz");
        free(cls);
        free(cp);

        /* longer */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "hi");
        cls = build_class(2, cp, cp_len, &cls_len);
        fails += expect_ok("longer", cls, cls_len, "hi", "hello", 1, "hello");
        free(cls);
        free(cp);

        /* double-slot Long + Utf8: cp_count accounts for Long taking two slots.
         * indices: 1=Long (skips 2), 3=Utf8 => count=4 */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_long(&cp, &cp_len, &cp_cap);
        cp_utf8(&cp, &cp_len, &cp_cap, "/home/old/path");
        cls = build_class(4, cp, cp_len, &cls_len);
        fails += expect_ok("double-slot", cls, cls_len, "/home/old", "/Users/new", 1,
                           "/Users/new/path");
        fails += expect_preserve_non_utf8("double-slot-preserve", cls, cls_len,
                                          "/home/old", "/Users/new");
        free(cls);
        free(cp);

        /* no match */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "alpha");
        cls = build_class(2, cp, cp_len, &cls_len);
        fails += expect_ok("no-match", cls, cls_len, "zzz", "yyy", 0, "alpha");
        free(cls);
        free(cp);

        /* OLD longer than the Utf8 value: no match, no UB on pointer arithmetic */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "ab");
        cls = build_class(2, cp, cp_len, &cls_len);
        fails += expect_ok("old-longer-than-utf8", cls, cls_len, "abcdef", "X", 0,
                           "ab");
        free(cls);
        free(cp);

        /* bad magic */
        {
                unsigned char bad[] = {0x00, 0x01, 0x02, 0x03, 0, 0, 0, 52, 0, 1};
                fails += expect_err("bad-magic", bad, sizeof bad, "a", "b");
        }

        /* truncation: valid header, Utf8 claims more bytes than present */
        {
                unsigned char trunc[] = {
                    0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 52, 0, 2, /* count=2 */
                    1,                                        /* Utf8 */
                    0, 10,                                    /* length 10 */
                    'a', 'b', 'c'                             /* only 3 bytes */
                };
                fails += expect_err("truncation", trunc, sizeof trunc, "a", "b");
        }

        /* unknown tag */
        {
                unsigned char unk[] = {
                    0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 52, 0, 2, 99, 0, 0};
                fails += expect_err("unknown-tag", unk, sizeof unk, "a", "b");
        }

        /* Class ref + Utf8 path: ensure Class entry bytes preserved */
        cp = NULL;
        cp_len = cp_cap = 0;
        cp_utf8(&cp, &cp_len, &cp_cap, "com/old/Foo");
        cp_class(&cp, &cp_len, &cp_cap, 1);
        cls = build_class(3, cp, cp_len, &cls_len);
        fails += expect_ok("class-ref", cls, cls_len, "com/old", "com/new", 1,
                           "com/new/Foo");
        fails += expect_preserve_non_utf8("class-ref-preserve", cls, cls_len,
                                          "com/old", "com/new");
        free(cls);
        free(cp);

        /* empty OLD: CLI validation rejects; patch path also fails */
        {
                char *av_empty[] = {"patch_class_paths", "", "new", "T.class"};
                char *av_ok[] = {"patch_class_paths", "old", "new", "T.class"};
                char *av_short[] = {"patch_class_paths", "old", "new"};

                if (cli_validate(4, av_empty) == 0) {
                        fprintf(stderr, "FAIL empty-old-cli: accepted empty OLD\n");
                        fails++;
                } else {
                        fprintf(stderr, "ok empty-old-cli\n");
                }
                if (cli_validate(3, av_short) == 0) {
                        fprintf(stderr, "FAIL empty-old-cli: accepted short argc\n");
                        fails++;
                } else {
                        fprintf(stderr, "ok short-argc-cli\n");
                }
                if (cli_validate(4, av_ok) != 0) {
                        fprintf(stderr, "FAIL empty-old-cli: rejected valid args\n");
                        fails++;
                } else {
                        fprintf(stderr, "ok valid-cli\n");
                }
        }
        {
                unsigned char *out = NULL;
                size_t out_size = 0;
                int changed = 0;
                const char *err = NULL;
                unsigned char tiny[] = {
                    0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 52, 0, 1};

                if (patch_class(tiny, sizeof tiny, (const unsigned char *)"", 0,
                                (const unsigned char *)"x", 1, &out, &out_size,
                                &changed, &err) == 0) {
                        fprintf(stderr, "FAIL empty-old-patch: expected error\n");
                        free(out);
                        fails++;
                } else {
                        free(out);
                        fprintf(stderr, "ok empty-old-patch\n");
                }
        }

        /* zero constant_pool_count */
        {
                unsigned char zc[] = {0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 52, 0, 0};
                fails += expect_err("zero-cp-count", zc, sizeof zc, "a", "b");
        }

        /* Long in the last available slot (count=2 => only index 1; no reserved) */
        {
                unsigned char bad_long[10 + 9 + 8];
                size_t bl = 0;

                bad_long[bl++] = 0xca;
                bad_long[bl++] = 0xfe;
                bad_long[bl++] = 0xba;
                bad_long[bl++] = 0xbe;
                bad_long[bl++] = 0;
                bad_long[bl++] = 0;
                bad_long[bl++] = 0;
                bad_long[bl++] = 52;
                bad_long[bl++] = 0;
                bad_long[bl++] = 2; /* count=2: only slot 1 */
                bad_long[bl++] = CP_LONG;
                memset(bad_long + bl, 0, 8);
                bl += 8;
                memset(bad_long + bl, 0, 8);
                bl += 8;
                fails += expect_err("invalid-final-long", bad_long, bl, "a", "b");
        }

        /* replacement length over 65535 */
        {
                char *big;
                unsigned char *out = NULL;
                size_t out_size = 0;
                int changed = 0;
                const char *err = NULL;

                cp = NULL;
                cp_len = cp_cap = 0;
                cp_utf8(&cp, &cp_len, &cp_cap, "x");
                cls = build_class(2, cp, cp_len, &cls_len);
                big = (char *)malloc(65536 + 1);
                if (!big) {
                        fprintf(stderr, "FAIL over-65535: OOM building NEW\n");
                        fails++;
                } else {
                        memset(big, 'Z', 65536);
                        big[65536] = '\0';
                        if (patch_class(cls, cls_len, (const unsigned char *)"x", 1,
                                        (const unsigned char *)big, 65536, &out,
                                        &out_size, &changed, &err) == 0) {
                                fprintf(stderr,
                                        "FAIL over-65535: expected error\n");
                                free(out);
                                fails++;
                        } else {
                                free(out);
                                fprintf(stderr, "ok over-65535 (%s)\n",
                                        err ? err : "error");
                        }
                        free(big);
                }
                free(cls);
                free(cp);
        }

        if (fails) {
                fprintf(stderr, "%d test(s) failed\n", fails);
                return 1;
        }
        fprintf(stderr, "all patch_class_paths self-tests passed\n");
        return 0;
}

#endif /* PATCH_CLASS_PATHS_SELFTEST */
