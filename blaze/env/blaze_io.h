#ifndef BLAZE_IO_H
#define BLAZE_IO_H

/* Host-only snapshot path resolution shared by CPU and CUDA loaders. */
#include <stdio.h>
#include <string.h>

static void cu_trim_inplace(char *s) {
    char *a = s, *e;
    if (!s) return;
    while (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\n' || e[-1] == '\r'))
        *--e = 0;
}

static int cu_join_snap_dir(char *out, size_t cap, const char *snap,
                            const char *rel) {
    const char *slash;
    size_t n, rl;
    if (!out || cap == 0 || !rel || !rel[0]) return -1;
    rl = strlen(rel);
    if (rel[0] == '/') {
        if (rl + 1 > cap) return -1;
        memcpy(out, rel, rl + 1);
        return 0;
    }
    slash = snap ? strrchr(snap, '/') : NULL;
    if (!slash) {
        if (rl + 1 > cap) return -1;
        memcpy(out, rel, rl + 1);
        return 0;
    }
    n = (size_t)(slash - snap);
    if (n + 1 + rl + 1 > cap) return -1;
    memcpy(out, snap, n);
    out[n] = '/';
    memcpy(out + n + 1, rel, rl + 1);
    return 0;
}

/* Sidecar PATH.banks names sibling nether/end banks. Missing sidecar is
 * not an error; a named path that cannot be resolved is. */
static int cu_read_banks_sidecar(const char *snap_path,
                                 char *nether_out, size_t ncap,
                                 char *end_out, size_t ecap,
                                 char *err, int err_cap) {
    char side[1080];
    char line[1024];
    FILE *f;
    if (!snap_path || !snap_path[0]) return 0;
    if (snprintf(side, sizeof side, "%s.banks", snap_path) >= (int)sizeof side)
        return 0;
    f = fopen(side, "r");
    if (!f) return 0;
    while (fgets(line, (int)sizeof line, f)) {
        char *sep, *key, *val, resolved[1024];
        cu_trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        sep = strchr(line, '=');
        if (sep) {
            *sep = 0;
            key = line;
            val = sep + 1;
        } else {
            key = line;
            val = line;
            while (*val && *val != ' ' && *val != '\t') val++;
            if (*val) *val++ = 0;
            else val = (char *)"";
        }
        cu_trim_inplace(key);
        cu_trim_inplace(val);
        if (!val[0]) continue;
        if (cu_join_snap_dir(resolved, sizeof resolved, snap_path, val) != 0) {
            fclose(f);
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "dimension bank path too long in %s", side);
            return -1;
        }
        if (!strcmp(key, "nether") && nether_out && ncap && !nether_out[0]) {
            if (strlen(resolved) + 1 > ncap) {
                fclose(f);
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "nether bank path too long in %s", side);
                return -1;
            }
            memcpy(nether_out, resolved, strlen(resolved) + 1);
        } else if (!strcmp(key, "end") && end_out && ecap && !end_out[0]) {
            if (strlen(resolved) + 1 > ecap) {
                fclose(f);
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "end bank path too long in %s", side);
                return -1;
            }
            memcpy(end_out, resolved, strlen(resolved) + 1);
        }
    }
    fclose(f);
    return 0;
}

/* Resolve the complete load request before any snapshot slot is changed.
 * One vector currently has one immutable bank per dimension. Conflicting
 * per-snapshot banks must be rejected, including within a single batch. */
static int cu_resolve_banks(const char *const *paths, int count,
                            const char *nether_opt, const char *end_opt,
                            const char *nether_loaded, const char *end_loaded,
                            char nether[1024], char end[1024],
                            char *err, int err_cap) {
    const char *initial[2] = {nether_opt[0] ? nether_opt : nether_loaded,
                             end_opt[0] ? end_opt : end_loaded};
    char *dest[2] = {nether, end};
    const char *opts[2] = {nether_opt, end_opt};
    for (int j = 0; j < 2; ++j) {
        if (strlen(initial[j]) >= 1024) return -1;
        strcpy(dest[j], initial[j]);
    }
    for (int i = 0; i < count; ++i) {
        char sn[1024] = {0}, se[1024] = {0};
        const char *sc[2] = {sn, se};
        if (!paths || !paths[i] ||
            cu_read_banks_sidecar(paths[i], sn, sizeof sn, se, sizeof se,
                                  err, err_cap)) return -1;
        for (int j = 0; j < 2; ++j) {
            if (opts[j][0] || !sc[j][0]) continue;
            if (dest[j][0] && strcmp(dest[j], sc[j])) {
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap, "conflicting %s dimension banks",
                             j ? "end" : "nether");
                return -1;
            }
            strcpy(dest[j], sc[j]);
        }
    }
    return 0;
}

#endif
