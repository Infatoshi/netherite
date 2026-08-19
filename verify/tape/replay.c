#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif

#include "mca.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef REPO_ROOT
#define REPO_ROOT "."
#endif

#define CANON_STEM "20260721T215812Z_fast_s0_survival_default_rd8_77b5b462"
#define WALL_CAP_S 180
#define MAX_TICKS 65536
#define INV_SLOTS 41
#define POOL_R 8

#define TOL_XYZ 1e-9
#define TOL_VEL 1e-9
#define TOL_HP 1e-4

typedef struct {
    int t;
    double x, y, z, yaw, pitch, vx, vy, vz;
    double hp;
    int og, food, dim;
    int have_ry;
    double ry, rp;
    int have_wfnv;
    unsigned long long wfnv;
    int have_wfa;
    int wfa[3];
    double in_f, in_s;
    int jump, sneak, sprint, atk, use, hb;
    int have_inv;
    int inv_item[INV_SLOTS];
    int inv_meta[INV_SLOTS];
    int inv_count[INV_SLOTS];
} Tick;

typedef struct {
    long long seed;
    long long world_time;
    long long total_time;
    double x, y, z, yaw, pitch, vx, vy, vz;
    double hp;
    int og, food, dim, look_phase;
    char world[64];
    char skin[16];
} Header;

typedef struct {
    int tick;
    double x, y, z, vx, vy, vz, health;
    int on_ground, food, dim;
    int have_hash;
    unsigned long long nearby_hash;
    int have_anchor;
    int anchor[3];
} State;

static int ifloor(double x)
{
    int i = (int)x;
    if (x >= 0 || (double)i == x)
        return i;
    return i - 1;
}

/* Python // on a block coord. C / truncates toward zero. */
static int floordiv16(int x)
{
    if (x >= 0)
        return x / 16;
    return -(((-x) + 15) / 16);
}

static int iabs(int x)
{
    return x < 0 ? -x : x;
}

static int sgn(double v)
{
    if (v > 1e-6)
        return 1;
    if (v < -1e-6)
        return -1;
    return 0;
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

static int find_int(const char *line, const char *key, int *out)
{
    const char *p = strstr(line, key);
    char *end = NULL;
    long v;
    if (!p)
        return 0;
    p += strlen(key);
    v = strtol(p, &end, 10);
    if (end == p)
        return 0;
    *out = (int)v;
    return 1;
}

static int find_ll(const char *line, const char *key, long long *out)
{
    const char *p = strstr(line, key);
    char *end = NULL;
    if (!p)
        return 0;
    p += strlen(key);
    *out = strtoll(p, &end, 10);
    return end != p;
}

static int find_str(const char *line, const char *key, char *out, size_t cap)
{
    const char *p = strstr(line, key);
    size_t n = 0;
    if (!p)
        return 0;
    p += strlen(key);
    if (*p != '"')
        return 0;
    p++;
    while (p[n] && p[n] != '"' && n + 1 < cap)
        n++;
    if (p[n] != '"')
        return 0;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

static int find_hex(const char *line, const char *key, unsigned long long *out)
{
    const char *p = strstr(line, key);
    char *end = NULL;
    if (!p)
        return 0;
    p += strlen(key);
    if (*p == '"')
        p++;
    *out = strtoull(p, &end, 16);
    return end != p;
}

static int parse_inv(const char *line, Tick *tk)
{
    const char *p = strstr(line, "\"inv\":[");
    int i;
    if (!p)
        return 0;
    p += 7;
    for (i = 0; i < INV_SLOTS; i++) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == ']')
            return 1;
        if (*p == '0' && (p[1] == ',' || p[1] == ']' || p[1] == 0)) {
            tk->inv_item[i] = tk->inv_meta[i] = tk->inv_count[i] = 0;
            p++;
        } else if (*p == '[') {
            char *end = NULL;
            p++;
            tk->inv_item[i] = (int)strtol(p, &end, 10);
            if (end == p)
                return 0;
            p = end;
            if (*p == ',')
                p++;
            tk->inv_meta[i] = (int)strtol(p, &end, 10);
            if (end == p)
                return 0;
            p = end;
            if (*p == ',')
                p++;
            tk->inv_count[i] = (int)strtol(p, &end, 10);
            if (end == p)
                return 0;
            p = end;
            if (*p == ']')
                p++;
        } else {
            return 0;
        }
        if (*p == ',')
            p++;
    }
    return 1;
}

static int parse_in(const char *line, Tick *tk)
{
    const char *p = strstr(line, "\"in\":{");
    char buf[256];
    size_t n;
    const char *q;
    if (!p)
        return 0;
    p += 5;
    q = strchr(p, '}');
    if (!q || (size_t)(q - p + 2) >= sizeof buf)
        return 0;
    n = (size_t)(q - p + 1);
    memcpy(buf, p, n);
    buf[n] = 0;
    find_key(buf, "\"f\":", &tk->in_f);
    find_key(buf, "\"s\":", &tk->in_s);
    find_int(buf, "\"jump\":", &tk->jump);
    find_int(buf, "\"sneak\":", &tk->sneak);
    find_int(buf, "\"sprint\":", &tk->sprint);
    find_int(buf, "\"atk\":", &tk->atk);
    find_int(buf, "\"use\":", &tk->use);
    find_int(buf, "\"hb\":", &tk->hb);
    return 1;
}

static int parse_wfa(const char *line, Tick *tk)
{
    const char *p = strstr(line, "\"wfa\":[");
    char *end = NULL;
    if (!p)
        return 0;
    p += 7;
    tk->wfa[0] = (int)strtol(p, &end, 10);
    if (end == p)
        return 0;
    p = end;
    if (*p == ',')
        p++;
    tk->wfa[1] = (int)strtol(p, &end, 10);
    if (end == p)
        return 0;
    p = end;
    if (*p == ',')
        p++;
    tk->wfa[2] = (int)strtol(p, &end, 10);
    tk->have_wfa = 1;
    return 1;
}

static int parse_anchor(const char *line, State *st)
{
    const char *p = strstr(line, "\"nearby_anchor\":[");
    char *end = NULL;
    if (!p)
        return 0;
    p += 17;
    st->anchor[0] = (int)strtol(p, &end, 10);
    if (end == p)
        return 0;
    p = end;
    if (*p == ',')
        p++;
    st->anchor[1] = (int)strtol(p, &end, 10);
    if (end == p)
        return 0;
    p = end;
    if (*p == ',')
        p++;
    st->anchor[2] = (int)strtol(p, &end, 10);
    st->have_anchor = 1;
    return 1;
}

static int load_tape(const char *path, Header *hdr, Tick **out, int *nout)
{
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    Tick *ticks = NULL;
    int ntick = 0, atick = 0;
    int have_hdr = 0;

    memset(hdr, 0, sizeof *hdr);
    hdr->hp = 20.0;
    hdr->food = 20;
    memcpy(hdr->world, "default", 8);
    memcpy(hdr->skin, "default", 8);

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0;
    }
    while ((n = getline(&line, &cap, fp)) > 0) {
        if (!have_hdr) {
            if (strstr(line, "\"header\"") == NULL) {
                fprintf(stderr, "tape missing header\n");
                free(line);
                fclose(fp);
                return 0;
            }
            find_ll(line, "\"seed\":", &hdr->seed);
            find_ll(line, "\"world_time\":", &hdr->world_time);
            find_ll(line, "\"total_time\":", &hdr->total_time);
            find_key(line, "\"x\":", &hdr->x);
            find_key(line, "\"y\":", &hdr->y);
            find_key(line, "\"z\":", &hdr->z);
            find_key(line, "\"yaw\":", &hdr->yaw);
            find_key(line, "\"pitch\":", &hdr->pitch);
            find_key(line, "\"vx\":", &hdr->vx);
            find_key(line, "\"vy\":", &hdr->vy);
            find_key(line, "\"vz\":", &hdr->vz);
            find_key(line, "\"hp\":", &hdr->hp);
            find_int(line, "\"og\":", &hdr->og);
            find_int(line, "\"food\":", &hdr->food);
            find_int(line, "\"dim\":", &hdr->dim);
            find_int(line, "\"look_phase\":", &hdr->look_phase);
            find_str(line, "\"skin\":", hdr->skin, sizeof hdr->skin);
            {
                char w[64];
                if (find_str(line, "\"world\":", w, sizeof w)) {
                    size_t wl = strlen(w);
                    if (wl >= 5 && strcmp(w + wl - 5, "_flat") == 0)
                        memcpy(hdr->world, "superflat", 10);
                    else
                        memcpy(hdr->world, "default", 8);
                }
            }
            have_hdr = 1;
            continue;
        }
        if (n < 5 || strncmp(line, "{\"t\":", 5) != 0)
            continue;
        if (ntick == atick) {
            int na = atick ? atick * 2 : 256;
            Tick *np = (Tick *)realloc(ticks, (size_t)na * sizeof(Tick));
            if (!np) {
                free(line);
                free(ticks);
                fclose(fp);
                return 0;
            }
            ticks = np;
            atick = na;
        }
        {
            Tick *tk = &ticks[ntick];
            memset(tk, 0, sizeof *tk);
            if (!find_int(line, "\"t\":", &tk->t) ||
                !find_key(line, "\"x\":", &tk->x) ||
                !find_key(line, "\"y\":", &tk->y) ||
                !find_key(line, "\"z\":", &tk->z)) {
                fprintf(stderr, "bad tick row\n");
                free(line);
                free(ticks);
                fclose(fp);
                return 0;
            }
            find_key(line, "\"yaw\":", &tk->yaw);
            find_key(line, "\"pitch\":", &tk->pitch);
            find_key(line, "\"vx\":", &tk->vx);
            find_key(line, "\"vy\":", &tk->vy);
            find_key(line, "\"vz\":", &tk->vz);
            find_key(line, "\"hp\":", &tk->hp);
            find_int(line, "\"og\":", &tk->og);
            find_int(line, "\"food\":", &tk->food);
            find_int(line, "\"dim\":", &tk->dim);
            if (find_key(line, "\"ry\":", &tk->ry) &&
                find_key(line, "\"rp\":", &tk->rp))
                tk->have_ry = 1;
            if (find_hex(line, "\"wfnv\":", &tk->wfnv))
                tk->have_wfnv = 1;
            parse_wfa(line, tk);
            parse_in(line, tk);
            if (parse_inv(line, tk))
                tk->have_inv = 1;
            ntick++;
        }
    }
    free(line);
    fclose(fp);
    if (!have_hdr || ntick <= 0) {
        free(ticks);
        fprintf(stderr, "no ticks in %s\n", path);
        return 0;
    }
    *out = ticks;
    *nout = ntick;
    return 1;
}

static int look_before_move(const Tick *row, const Tick *prev, double old_yaw,
                            int last_move)
{
    int move_now = sgn(row->in_f) || sgn(row->in_s);
    int look_changed = (row->yaw != old_yaw);
    double ax, az, yr, ex, ez, norm, scale, dx, dz, old_e, new_e;

    if (!move_now || !look_changed)
        return 0;
    if (last_move && prev) {
        ax = row->x - prev->x - prev->vx;
        az = row->z - prev->z - prev->vz;
        if (ax * ax + az * az < 1e-8)
            return 0;
        yr = old_yaw * (3.14159265358979323846 / 180.0);
        ex = row->in_s * cos(yr) - row->in_f * sin(yr);
        ez = row->in_f * cos(yr) + row->in_s * sin(yr);
        norm = ex * ex + ez * ez;
        if (norm <= 0)
            return 0;
        scale = (ax * ex + az * ez) / norm;
        if (scale < 0)
            scale = 0;
        dx = ax - scale * ex;
        dz = az - scale * ez;
        old_e = dx * dx + dz * dz;
        yr = row->yaw * (3.14159265358979323846 / 180.0);
        ex = row->in_s * cos(yr) - row->in_f * sin(yr);
        ez = row->in_f * cos(yr) + row->in_s * sin(yr);
        norm = ex * ex + ez * ez;
        if (norm <= 0)
            return 0;
        scale = (ax * ex + az * ez) / norm;
        if (scale < 0)
            scale = 0;
        dx = ax - scale * ex;
        dz = az - scale * ez;
        new_e = dx * dx + dz * dz;
        return new_e < old_e * 0.25;
    }
    return 1;
}

/* START arrival only (replay_tape.snapshot_arrival_events). One
 * snapshot_region at the header pose plus cache snapshot_block rows whose
 * chunk is within radius. Later dim/ppos arrivals are skipped. */
static int emit_snap(FILE *f, const char *cache, const Header *hdr, int radius,
                     int *nwritten, int *first_r)
{
    FILE *sf;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int scx = floordiv16(ifloor(hdr->x));
    int scz = floordiv16(ifloor(hdr->z));
    int sdim = hdr->dim;
    int wrote = 0;
    int fr = -1;

    sf = fopen(cache, "rb");
    if (!sf)
        return 0;
    fprintf(f,
            "{\"tick\":0,\"type\":\"snapshot_region\",\"dim\":%d,"
            "\"cx\":%d,\"cz\":%d,\"radius\":%d}\n",
            sdim, scx, scz, radius);
    wrote++;
    while ((n = getline(&line, &cap, sf)) > 0) {
        int x, z, dim = 0, bx, bz;
        if (strstr(line, "\"type\":\"snapshot_region\"")) {
            int r;
            if (fr < 0 && find_int(line, "\"radius\":", &r))
                fr = r;
            continue;
        }
        if (!strstr(line, "\"type\":\"snapshot_block\""))
            continue;
        if (!find_int(line, "\"x\":", &x) || !find_int(line, "\"z\":", &z))
            continue;
        find_int(line, "\"dim\":", &dim);
        if (dim != sdim)
            continue;
        bx = floordiv16(x);
        bz = floordiv16(z);
        if (iabs(bx - scx) > radius || iabs(bz - scz) > radius)
            continue;
        if (fwrite(line, 1, (size_t)n, f) != (size_t)n) {
            free(line);
            fclose(sf);
            return 0;
        }
        if (n > 0 && line[n - 1] != '\n')
            fputc('\n', f);
        wrote++;
    }
    free(line);
    fclose(sf);
    if (nwritten)
        *nwritten = wrote;
    if (first_r)
        *first_r = fr;
    return 1;
}

static int emit_script(const char *path, const Header *hdr, const Tick *ticks,
                       int n, const char *snap_path, int snap_r, int *snap_n,
                       int *first_r)
{
    FILE *f = fopen(path, "w");
    int i, last_hb = 0, last_move = 0;
    double last_yaw, last_pitch;
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    last_yaw = hdr->yaw;
    last_pitch = hdr->pitch;
    fprintf(f, "{\"tick\":0,\"type\":\"set_time\",\"value\":%lld}\n",
            hdr->world_time);
    fprintf(f, "{\"tick\":0,\"type\":\"set_total_time\",\"value\":%lld}\n",
            hdr->total_time);
    fprintf(f, "{\"tick\":0,\"type\":\"set_dimension\",\"dimension\":%d}\n",
            hdr->dim);
    fprintf(f, "{\"tick\":0,\"type\":\"set_skin\",\"skin\":\"%s\"}\n",
            hdr->skin);
    fprintf(f,
            "{\"tick\":0,\"type\":\"set_pose\",\"x\":%.17g,\"y\":%.17g,"
            "\"z\":%.17g,\"yaw\":%.17g,\"pitch\":%.17g}\n",
            hdr->x, hdr->y, hdr->z, hdr->yaw, hdr->pitch);
    fprintf(f,
            "{\"tick\":0,\"type\":\"set_velocity\",\"x\":%.17g,\"y\":%.17g,"
            "\"z\":%.17g,\"on_ground\":%d}\n",
            hdr->vx, hdr->vy, hdr->vz, hdr->og);
    fprintf(f,
            "{\"tick\":0,\"type\":\"set_vitals\",\"health\":%.17g,\"food\":%d}\n",
            hdr->hp, hdr->food);
    if (n > 0 && ticks[0].have_inv) {
        for (i = 0; i < INV_SLOTS; i++) {
            fprintf(f,
                    "{\"tick\":0,\"type\":\"set_inventory\",\"slot\":%d,"
                    "\"item\":%d,\"count\":%d,\"meta\":%d}\n",
                    i, ticks[0].inv_item[i], ticks[0].inv_count[i],
                    ticks[0].inv_meta[i]);
        }
    }
    if (snap_path) {
        if (!emit_snap(f, snap_path, hdr, snap_r, snap_n, first_r)) {
            fclose(f);
            return 0;
        }
    } else {
        if (snap_n)
            *snap_n = 0;
        if (first_r)
            *first_r = -1;
    }
    for (i = 0; i < n; i++) {
        const Tick *tk = &ticks[i];
        const Tick *prev = i ? &ticks[i - 1] : NULL;
        int t = tk->t;
        int pre;
        if (hdr->look_phase && tk->have_ry) {
            fprintf(f,
                    "{\"tick\":%d,\"type\":\"set_look_pre\",\"yaw\":%.17g,"
                    "\"pitch\":%.17g}\n",
                    t, tk->ry, tk->rp);
            fprintf(f,
                    "{\"tick\":%d,\"type\":\"set_look\",\"yaw\":%.17g,"
                    "\"pitch\":%.17g}\n",
                    t, tk->yaw, tk->pitch);
        } else {
            pre = look_before_move(tk, prev, last_yaw, last_move);
            fprintf(f,
                    "{\"tick\":%d,\"type\":\"%s\",\"yaw\":%.17g,\"pitch\":%.17g}\n",
                    t, pre ? "set_look_pre" : "set_look", tk->yaw, tk->pitch);
        }
        {
            int sf = sgn(tk->in_f);
            int ss = -sgn(tk->in_s);
            int any = sf || ss || tk->jump || tk->sneak || tk->sprint ||
                      tk->atk || tk->use || (tk->hb != last_hb);
            if (any) {
                fprintf(f, "{\"tick\":%d,\"type\":\"action\"", t);
                if (sf)
                    fprintf(f, ",\"forward\":%d", sf);
                if (ss)
                    fprintf(f, ",\"strafe\":%d", ss);
                if (tk->jump)
                    fprintf(f, ",\"jump\":1");
                if (tk->sneak)
                    fprintf(f, ",\"sneak\":1");
                if (tk->sprint)
                    fprintf(f, ",\"sprint\":1");
                if (tk->atk)
                    fprintf(f, ",\"attack\":1");
                if (tk->use)
                    fprintf(f, ",\"use\":1");
                if (tk->hb != last_hb) {
                    fprintf(f, ",\"hotbar\":%d", tk->hb);
                    last_hb = tk->hb;
                }
                fprintf(f, "}\n");
            }
        }
        last_yaw = tk->yaw;
        last_pitch = tk->pitch;
        last_move = sgn(tk->in_f) || sgn(tk->in_s);
        (void)last_pitch;
    }
    fclose(f);
    return 1;
}

static int write_conf(const char *path, const Header *hdr, int ticks,
                      const char *script, const char *state)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    fprintf(f, "seed = %lld\n", hdr->seed);
    fprintf(f, "world = %s\n", hdr->world);
    fprintf(f, "mobs = 0\n");
    fprintf(f, "daylight = 0\n");
    fprintf(f, "headless = 1\n");
    fprintf(f, "ticks = %d\n", ticks);
    fprintf(f, "script = %s\n", script);
    fprintf(f, "state_out = %s\n", state);
    fprintf(f, "render = off\n");
    fprintf(f, "pace = unlimited\n");
    fprintf(f, "backend = cpu\n");
    fclose(f);
    return 1;
}

static int mkdir_p(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 1;
    return 0;
}

static char *abspath_dup(const char *p)
{
    char buf[PATH_MAX];
    if (!realpath(p, buf))
        return NULL;
    return strdup(buf);
}

static int run_magma(const char *bin, const char *conf)
{
    pid_t pid;
    int status = 0;
    char *bindir;
    char *slash;
    char *bin_abs;
    char *conf_abs;

    bin_abs = abspath_dup(bin);
    conf_abs = abspath_dup(conf);
    if (!bin_abs || !conf_abs) {
        fprintf(stderr, "realpath failed for magma or conf\n");
        free(bin_abs);
        free(conf_abs);
        return -1;
    }
    bindir = strdup(bin_abs);
    slash = strrchr(bindir, '/');
    if (slash)
        *slash = 0;
    pid = fork();
    if (pid < 0) {
        free(bin_abs);
        free(conf_abs);
        free(bindir);
        return -1;
    }
    if (pid == 0) {
        char *argv[4];
        alarm(WALL_CAP_S);
        if (chdir(bindir) != 0)
            _exit(127);
        argv[0] = bin_abs;
        argv[1] = "--conf";
        argv[2] = conf_abs;
        argv[3] = NULL;
        execv(bin_abs, argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        free(bin_abs);
        free(conf_abs);
        free(bindir);
        return -1;
    }
    free(bin_abs);
    free(conf_abs);
    free(bindir);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

static int load_state(const char *path, State **out, int *nout)
{
    FILE *fp;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    State *rows = NULL;
    int nr = 0, ar = 0;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    while ((n = getline(&line, &cap, fp)) > 0) {
        State *s;
        if (nr == ar) {
            int na = ar ? ar * 2 : 64;
            State *np = (State *)realloc(rows, (size_t)na * sizeof(State));
            if (!np) {
                free(line);
                free(rows);
                fclose(fp);
                return 0;
            }
            rows = np;
            ar = na;
        }
        s = &rows[nr];
        memset(s, 0, sizeof *s);
        if (!find_int(line, "\"tick\":", &s->tick) ||
            !find_key(line, "\"x\":", &s->x) ||
            !find_key(line, "\"y\":", &s->y) ||
            !find_key(line, "\"z\":", &s->z)) {
            continue;
        }
        find_key(line, "\"vx\":", &s->vx);
        find_key(line, "\"vy\":", &s->vy);
        find_key(line, "\"vz\":", &s->vz);
        find_key(line, "\"health\":", &s->health);
        find_int(line, "\"on_ground\":", &s->on_ground);
        find_int(line, "\"food\":", &s->food);
        find_int(line, "\"dim\":", &s->dim);
        if (find_hex(line, "\"nearby_hash\":", &s->nearby_hash))
            s->have_hash = 1;
        parse_anchor(line, s);
        nr++;
    }
    free(line);
    fclose(fp);
    *out = rows;
    *nout = nr;
    return nr > 0;
}

static double absd(double a)
{
    return a < 0 ? -a : a;
}

static int first_div(const Tick *ticks, int nt, const State *rows, int nr,
                     int *ot, const char **ofield, double *otape, double *omagma,
                     double *od)
{
    int n = nt < nr ? nt : nr;
    int i;
    static const struct {
        const char *name;
        int lagged;
        double tol;
    } fields[] = {
        {"x", 0, TOL_XYZ},  {"y", 0, TOL_XYZ},  {"z", 0, TOL_XYZ},
        {"vx", 0, TOL_VEL}, {"vy", 0, TOL_VEL}, {"vz", 0, TOL_VEL},
        {"og", 0, 0},       {"hp", 1, TOL_HP},  {"food", 1, 0},
        {"dim", 0, 0},
    };
    for (i = 0; i < n; i++) {
        const Tick *j = &ticks[i];
        size_t fi;
        for (fi = 0; fi < sizeof fields / sizeof fields[0]; fi++) {
            double jv = 0, best, d;
            double cands[3];
            int nc = 1;
            if (strcmp(fields[fi].name, "x") == 0)
                jv = j->x, cands[0] = rows[i].x;
            else if (strcmp(fields[fi].name, "y") == 0)
                jv = j->y, cands[0] = rows[i].y;
            else if (strcmp(fields[fi].name, "z") == 0)
                jv = j->z, cands[0] = rows[i].z;
            else if (strcmp(fields[fi].name, "vx") == 0)
                jv = j->vx, cands[0] = rows[i].vx;
            else if (strcmp(fields[fi].name, "vy") == 0)
                jv = j->vy, cands[0] = rows[i].vy;
            else if (strcmp(fields[fi].name, "vz") == 0)
                jv = j->vz, cands[0] = rows[i].vz;
            else if (strcmp(fields[fi].name, "og") == 0)
                jv = j->og, cands[0] = rows[i].on_ground;
            else if (strcmp(fields[fi].name, "hp") == 0)
                jv = j->hp, cands[0] = rows[i].health;
            else if (strcmp(fields[fi].name, "food") == 0)
                jv = j->food, cands[0] = rows[i].food;
            else
                jv = j->dim, cands[0] = rows[i].dim;
            if (fields[fi].lagged && i > 0) {
                if (strcmp(fields[fi].name, "hp") == 0)
                    cands[nc++] = rows[i - 1].health;
                else
                    cands[nc++] = rows[i - 1].food;
            }
            if (fields[fi].lagged && i + 1 < n) {
                if (strcmp(fields[fi].name, "hp") == 0)
                    cands[nc++] = rows[i + 1].health;
                else
                    cands[nc++] = rows[i + 1].food;
            }
            best = cands[0];
            {
                int k;
                for (k = 1; k < nc; k++) {
                    if (absd(jv - cands[k]) < absd(jv - best))
                        best = cands[k];
                }
            }
            d = absd(jv - best);
            if (d > fields[fi].tol) {
                *ot = i;
                *ofield = fields[fi].name;
                *otape = jv;
                *omagma = best;
                *od = d;
                return 1;
            }
        }
    }
    return 0;
}

static int file_ok(const char *p)
{
    return p && p[0] && access(p, X_OK) == 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --tape PATH [--ticks N] [--out DIR] [--magma PATH] "
            "[--world PATH] [--dry-run]\n",
            argv0);
}

int main(int argc, char **argv)
{
    const char *tape = NULL;
    const char *world = NULL;
    const char *magma = NULL;
    const char *out = NULL;
    int ticks_lim = 32, dry = 0, i, ntick = 0, nuse, nstate = 0;
    char tape_buf[4096], world_buf[4096], magma_buf[4096], out_buf[4096];
    char script[4096], statep[4096], conf[4096], snap_buf[4096];
    const char *snap = NULL;
    int snap_n = 0, first_r = -1, snap_r = POOL_R, shrunk = 0;
    Header hdr;
    Tick *ticks = NULL;
    State *states = NULL;
    McaStore store;
    int magma_rc = 0;

    memset(&store, 0, sizeof store);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tape") == 0 && i + 1 < argc)
            tape = argv[++i];
        else if (strcmp(argv[i], "--world") == 0 && i + 1 < argc)
            world = argv[++i];
        else if (strcmp(argv[i], "--magma") == 0 && i + 1 < argc)
            magma = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out = argv[++i];
        else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc)
            ticks_lim = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dry-run") == 0)
            dry = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (ticks_lim < 1 || ticks_lim > MAX_TICKS) {
        fprintf(stderr, "bad --ticks\n");
        return 2;
    }
    if (!tape) {
        snprintf(tape_buf, sizeof tape_buf, "%s/verify/tapes/%s.jsonl",
                 REPO_ROOT, CANON_STEM);
        tape = tape_buf;
    }
    if (!world) {
        size_t tl = strlen(tape);
        if (tl > 6 && strcmp(tape + tl - 6, ".jsonl") == 0) {
            snprintf(world_buf, sizeof world_buf, "%.*s_world", (int)(tl - 6),
                     tape);
            world = world_buf;
        }
    }
    if (!out) {
        snprintf(out_buf, sizeof out_buf, "%s/out/verify/replay_last",
                 REPO_ROOT);
        out = out_buf;
    }
    if (!magma) {
        snprintf(magma_buf, sizeof magma_buf, "%s/magma/magma_game", REPO_ROOT);
        magma = magma_buf;
    }
    if (!load_tape(tape, &hdr, &ticks, &ntick))
        return 1;
    nuse = ntick < ticks_lim ? ntick : ticks_lim;
    if (!mkdir_p(out)) {
        char parent[4096];
        snprintf(parent, sizeof parent, "%s/out", REPO_ROOT);
        mkdir_p(parent);
        snprintf(parent, sizeof parent, "%s/out/verify", REPO_ROOT);
        mkdir_p(parent);
        if (!mkdir_p(out)) {
            fprintf(stderr, "mkdir %s: %s\n", out, strerror(errno));
            free(ticks);
            return 1;
        }
    }
    snprintf(script, sizeof script, "%s/magma_script.jsonl", out);
    snprintf(statep, sizeof statep, "%s/magma_state.jsonl", out);
    snprintf(conf, sizeof conf, "%s/magma_run.conf", out);
    snprintf(snap_buf, sizeof snap_buf, "%s.snapshot_patch.jsonl", tape);
    if (access(snap_buf, R_OK) == 0)
        snap = snap_buf;
    if (!emit_script(script, &hdr, ticks, nuse, snap, snap_r, &snap_n,
                     &first_r)) {
        free(ticks);
        return 1;
    }
    {
        char script_abs[PATH_MAX], state_abs[PATH_MAX];
        FILE *sf;
        if (!realpath(script, script_abs)) {
            fprintf(stderr, "realpath script failed\n");
            free(ticks);
            return 1;
        }
        sf = fopen(statep, "w");
        if (sf)
            fclose(sf);
        if (!realpath(statep, state_abs)) {
            fprintf(stderr, "realpath state failed\n");
            free(ticks);
            return 1;
        }
        memcpy(script, script_abs, sizeof script);
        memcpy(statep, state_abs, sizeof statep);
    }
    printf("tape %s\n", tape);
    printf("ticks %d (of %d)\n", nuse, ntick);
    printf("script %s\n", script);
    if (snap)
        printf("snapshot_patch events %d\n", snap_n);
    else
        printf("snapshot_patch none\n");
    if (dry) {
        printf("dry-run 1\n");
        free(ticks);
        return 0;
    }
    if (!file_ok(magma)) {
        fprintf(stderr, "magma binary missing or not executable: %s\n", magma);
        free(ticks);
        return 1;
    }
    if (!write_conf(conf, &hdr, nuse, script, statep)) {
        free(ticks);
        return 1;
    }
    printf("magma %s\n", magma);
    fflush(stdout);
    magma_rc = run_magma(magma, conf);
    if (magma_rc == 128 + SIGALRM && snap && !shrunk) {
        int nr = first_r >= 0 ? first_r : 4;
        if (nr < snap_r) {
            snap_r = nr;
            shrunk = 1;
            if (!emit_script(script, &hdr, ticks, nuse, snap, snap_r, &snap_n,
                             &first_r)) {
                free(ticks);
                return 1;
            }
            printf("snapshot_patch shrink radius %d\n", snap_r);
            printf("snapshot_patch events %d\n", snap_n);
            fflush(stdout);
            magma_rc = run_magma(magma, conf);
        }
    }
    printf("magma_rc %d\n", magma_rc);
    if (!load_state(statep, &states, &nstate)) {
        fprintf(stderr, "magma produced no state rows\n");
        free(ticks);
        return 1;
    }
    printf("state_rows %d\n", nstate);
    {
        int dt = 0;
        const char *field = NULL;
        double tv = 0, mv = 0, d = 0;
        if (first_div(ticks, nuse, states, nstate, &dt, &field, &tv, &mv, &d))
            printf("first_div tick %d field %s tape=%.17g magma=%.17g |d|=%.3g\n",
                   dt, field, tv, mv, d);
        else
            printf("first_div none\n");
    }
    if (world && access(world, F_OK) == 0) {
        McaPose *poses = (McaPose *)calloc((size_t)nuse, sizeof(McaPose));
        int pi;
        if (!poses) {
            free(states);
            free(ticks);
            return 1;
        }
        for (pi = 0; pi < nuse; pi++) {
            poses[pi].t = ticks[pi].t;
            poses[pi].ax = ifloor(ticks[pi].x);
            poses[pi].ay = ifloor(ticks[pi].y);
            poses[pi].az = ifloor(ticks[pi].z);
        }
        if (mca_load(&store, world, poses, nuse, 8, 4)) {
            if (states[0].have_anchor && states[0].have_hash) {
                unsigned long long h = mca_nearby_hash(
                    &store, states[0].anchor[0], states[0].anchor[1],
                    states[0].anchor[2]);
                printf("nearby_hash magma=%016llx mca=%016llx match %d\n",
                       states[0].nearby_hash, h,
                       states[0].nearby_hash == h);
            }
            if (ticks[0].have_wfnv) {
                unsigned long long h = mca_nearby_hash(
                    &store, ifloor(ticks[0].x), ifloor(ticks[0].y),
                    ifloor(ticks[0].z));
                printf("wfnv tape=%016llx mca=%016llx match %d\n",
                       ticks[0].wfnv, h, ticks[0].wfnv == h);
            }
            mca_store_free(&store);
        } else {
            printf("mca_load failed\n");
        }
        free(poses);
    } else {
        printf("mca skipped (no world)\n");
    }
    free(states);
    free(ticks);
    return 0;
}
