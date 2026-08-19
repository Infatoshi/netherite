#include "jar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    char *name;
    unsigned int method;
    unsigned int crc32;
    unsigned int comp_size;
    unsigned int uncomp_size;
    unsigned int local_offset;
} JarEntry;

struct AssetJar {
    unsigned char *map;
    size_t size;
    JarEntry *entries;
    size_t n_entries;
};

static unsigned int rd_u16(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int inflate_raw(const unsigned char *src, size_t src_len,
                       unsigned char *dst, size_t dst_len) {
    z_stream strm;
    int rc;

    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return -1;
    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = dst;
    strm.avail_out = (uInt)dst_len;
    rc = inflate(&strm, Z_FINISH);
    if (rc != Z_STREAM_END || strm.total_out != dst_len) {
        inflateEnd(&strm);
        return -1;
    }
    inflateEnd(&strm);
    return 0;
}

AssetJar *asset_jar_open(const char *path) {
    FILE *fp;
    AssetJar *jar;
    long flen;
    size_t nread;
    size_t i;
    size_t scan;
    size_t eocd;
    unsigned int cd_size, cd_offset, n_total;
    size_t pos;
    const unsigned char *p;

    if (!path)
        return NULL;
    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    flen = ftell(fp);
    if (flen < 22) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    jar = (AssetJar *)calloc(1, sizeof(AssetJar));
    if (!jar) {
        fclose(fp);
        return NULL;
    }
    jar->size = (size_t)flen;
    jar->map = (unsigned char *)malloc(jar->size);
    if (!jar->map) {
        free(jar);
        fclose(fp);
        return NULL;
    }
    nread = fread(jar->map, 1, jar->size, fp);
    fclose(fp);
    if (nread != jar->size) {
        asset_jar_close(jar);
        return NULL;
    }

    /* EOCD: signature 0x06054b50, comment length may vary — scan from end. */
    eocd = (size_t)-1;
    scan = jar->size >= 65557u ? jar->size - 65557u : 0;
    for (i = jar->size - 22; i + 1 > scan; i--) {
        if (rd_u32(jar->map + i) == 0x06054b50u) {
            unsigned int comment_len = rd_u16(jar->map + i + 20);
            if (i + 22u + comment_len == jar->size) {
                eocd = i;
                break;
            }
        }
        if (i == 0)
            break;
    }
    if (eocd == (size_t)-1) {
        asset_jar_close(jar);
        return NULL;
    }
    p = jar->map + eocd;
    /* Reject ZIP64 markers in EOCD field widths we rely on. */
    if (rd_u16(p + 4) != 0 || rd_u16(p + 6) != 0) {
        /* multi-disk: reject */
        asset_jar_close(jar);
        return NULL;
    }
    n_total = rd_u16(p + 10);
    cd_size = rd_u32(p + 12);
    cd_offset = rd_u32(p + 16);
    if (rd_u16(p + 8) != n_total) {
        asset_jar_close(jar);
        return NULL;
    }
    if (n_total == 0xFFFFu || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
        /* ZIP64 */
        asset_jar_close(jar);
        return NULL;
    }
    if ((size_t)cd_offset + (size_t)cd_size > jar->size ||
        (size_t)cd_offset + (size_t)cd_size > eocd) {
        asset_jar_close(jar);
        return NULL;
    }

    jar->entries = (JarEntry *)calloc(n_total, sizeof(JarEntry));
    if (!jar->entries) {
        asset_jar_close(jar);
        return NULL;
    }
    jar->n_entries = 0;
    pos = cd_offset;
    for (i = 0; i < n_total; i++) {
        unsigned int sig, flags, method, name_len, extra_len, comment_len;
        unsigned int crc, csz, usz, local_off;
        JarEntry *e;
        size_t j;

        if (pos + 46 > cd_offset + cd_size) {
            asset_jar_close(jar);
            return NULL;
        }
        p = jar->map + pos;
        sig = rd_u32(p);
        if (sig != 0x02014b50u) {
            asset_jar_close(jar);
            return NULL;
        }
        flags = rd_u16(p + 8);
        method = rd_u16(p + 10);
        crc = rd_u32(p + 16);
        csz = rd_u32(p + 20);
        usz = rd_u32(p + 24);
        name_len = rd_u16(p + 28);
        extra_len = rd_u16(p + 30);
        comment_len = rd_u16(p + 32);
        local_off = rd_u32(p + 42);
        if (pos + 46u + name_len + extra_len + comment_len > cd_offset + cd_size) {
            asset_jar_close(jar);
            return NULL;
        }
        if (flags & 1u) {
            /* encrypted */
            asset_jar_close(jar);
            return NULL;
        }
        if (method != 0 && method != 8) {
            asset_jar_close(jar);
            return NULL;
        }
        if (csz == 0xFFFFFFFFu || usz == 0xFFFFFFFFu || local_off == 0xFFFFFFFFu) {
            asset_jar_close(jar);
            return NULL;
        }
        e = &jar->entries[jar->n_entries];
        e->name = (char *)malloc(name_len + 1);
        if (!e->name) {
            asset_jar_close(jar);
            return NULL;
        }
        memcpy(e->name, p + 46, name_len);
        e->name[name_len] = '\0';
        for (j = 0; j < jar->n_entries; j++) {
            if (strcmp(jar->entries[j].name, e->name) == 0) {
                free(e->name);
                e->name = NULL;
                asset_jar_close(jar);
                return NULL;
            }
        }
        e->method = method;
        e->crc32 = crc;
        e->comp_size = csz;
        e->uncomp_size = usz;
        e->local_offset = local_off;
        jar->n_entries++;
        pos += 46u + name_len + extra_len + comment_len;
    }
    return jar;
}

void asset_jar_close(AssetJar *jar) {
    size_t i;
    if (!jar)
        return;
    if (jar->entries) {
        for (i = 0; i < jar->n_entries; i++)
            free(jar->entries[i].name);
        free(jar->entries);
    }
    free(jar->map);
    free(jar);
}

void asset_data_free(void *data) {
    free(data);
}

int asset_jar_read(AssetJar *jar, const char *member, unsigned char **data, size_t *size) {
    size_t i;
    JarEntry *e = NULL;
    const unsigned char *lp;
    unsigned int sig, flags, method, name_len, extra_len;
    unsigned int crc, csz, usz;
    size_t data_off;
    unsigned char *out;
    unsigned long got_crc;

    if (!jar || !member || !data || !size)
        return -1;
    *data = NULL;
    *size = 0;
    for (i = 0; i < jar->n_entries; i++) {
        if (strcmp(jar->entries[i].name, member) == 0) {
            e = &jar->entries[i];
            break;
        }
    }
    if (!e)
        return -1;
    if ((size_t)e->local_offset + 30 > jar->size)
        return -1;
    lp = jar->map + e->local_offset;
    sig = rd_u32(lp);
    if (sig != 0x04034b50u)
        return -1;
    flags = rd_u16(lp + 6);
    method = rd_u16(lp + 8);
    crc = rd_u32(lp + 14);
    csz = rd_u32(lp + 18);
    usz = rd_u32(lp + 22);
    name_len = rd_u16(lp + 26);
    extra_len = rd_u16(lp + 28);
    if (flags & 1u)
        return -1;
    if (method != e->method)
        return -1;
    /* Prefer central-directory sizes when local has data descriptor bit. */
    if (flags & 8u) {
        csz = e->comp_size;
        usz = e->uncomp_size;
        crc = e->crc32;
    } else {
        if (csz != e->comp_size || usz != e->uncomp_size || crc != e->crc32)
            return -1;
    }
    data_off = (size_t)e->local_offset + 30u + name_len + extra_len;
    if (data_off + (size_t)csz > jar->size)
        return -1;
    out = (unsigned char *)malloc(usz ? usz : 1);
    if (!out)
        return -1;
    if (method == 0) {
        if (csz != usz) {
            free(out);
            return -1;
        }
        memcpy(out, jar->map + data_off, usz);
    } else if (method == 8) {
        if (inflate_raw(jar->map + data_off, csz, out, usz) != 0) {
            free(out);
            return -1;
        }
    } else {
        free(out);
        return -1;
    }
    got_crc = crc32(0L, Z_NULL, 0);
    got_crc = crc32(got_crc, out, usz);
    if ((unsigned int)got_crc != crc) {
        free(out);
        return -1;
    }
    *data = out;
    *size = usz;
    return 0;
}
