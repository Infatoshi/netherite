#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- tiny JSON DOM ---- */

typedef enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ } JvType;

typedef struct Jv {
    JvType type;
    int b;
    double n;
    char *s;
    char *key;
    struct Jv *child;
    struct Jv *next;
} Jv;

typedef struct {
    const char *p;
    const char *end;
    char err[128];
} JParse;

static void jv_free(Jv *v)
{
    Jv *c, *n;
    if (!v)
        return;
    free(v->s);
    free(v->key);
    for (c = v->child; c; c = n) {
        n = c->next;
        jv_free(c);
    }
    free(v);
}

static Jv *jv_new(JvType t)
{
    Jv *v = (Jv *)calloc(1, sizeof(*v));
    if (v)
        v->type = t;
    return v;
}

static void skip_ws(JParse *ps)
{
    while (ps->p < ps->end && isspace((unsigned char)*ps->p))
        ps->p++;
}

static int parse_value(JParse *ps, Jv **out);

static int set_err(JParse *ps, const char *msg)
{
    snprintf(ps->err, sizeof(ps->err), "%s", msg);
    return -1;
}

static int parse_string(JParse *ps, char **out)
{
    const char *s;
    char *buf, *d;
    size_t cap;

    if (ps->p >= ps->end || *ps->p != '"')
        return set_err(ps, "expected string");
    ps->p++;
    s = ps->p;
    cap = 0;
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\') {
            ps->p++;
            if (ps->p >= ps->end)
                return set_err(ps, "bad escape");
        }
        ps->p++;
        cap++;
    }
    if (ps->p >= ps->end)
        return set_err(ps, "unterminated string");
    buf = (char *)malloc(cap + 1);
    if (!buf)
        return set_err(ps, "oom");
    d = buf;
    ps->p = s;
    while (ps->p < ps->end && *ps->p != '"') {
        if (*ps->p == '\\') {
            ps->p++;
            switch (*ps->p) {
            case '"':
            case '\\':
            case '/':
                *d++ = *ps->p;
                break;
            case 'b':
                *d++ = '\b';
                break;
            case 'f':
                *d++ = '\f';
                break;
            case 'n':
                *d++ = '\n';
                break;
            case 'r':
                *d++ = '\r';
                break;
            case 't':
                *d++ = '\t';
                break;
            case 'u':
                /* sounds.json does not need \u; keep as '?'. */
                if (ps->p + 4 >= ps->end) {
                    free(buf);
                    return set_err(ps, "bad \\u");
                }
                ps->p += 3;
                *d++ = '?';
                break;
            default:
                free(buf);
                return set_err(ps, "bad escape");
            }
            ps->p++;
        } else {
            *d++ = *ps->p++;
        }
    }
    *d = 0;
    ps->p++;
    *out = buf;
    return 0;
}

static int parse_number(JParse *ps, double *out)
{
    char *end = NULL;
    *out = strtod(ps->p, &end);
    if (end == ps->p)
        return set_err(ps, "bad number");
    ps->p = end;
    return 0;
}

static int parse_array(JParse *ps, Jv *arr)
{
    Jv *tail = NULL;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        return 0;
    }
    for (;;) {
        Jv *item;
        if (parse_value(ps, &item) != 0)
            return -1;
        if (!arr->child)
            arr->child = item;
        else
            tail->next = item;
        tail = item;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            return 0;
        }
        return set_err(ps, "bad array");
    }
}

static int parse_object(JParse *ps, Jv *obj)
{
    Jv *tail = NULL;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        return 0;
    }
    for (;;) {
        Jv *item;
        char *key;
        skip_ws(ps);
        if (parse_string(ps, &key) != 0)
            return -1;
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            free(key);
            return set_err(ps, "expected :");
        }
        ps->p++;
        if (parse_value(ps, &item) != 0) {
            free(key);
            return -1;
        }
        item->key = key;
        if (!obj->child)
            obj->child = item;
        else
            tail->next = item;
        tail = item;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            return 0;
        }
        return set_err(ps, "bad object");
    }
}

static int parse_value(JParse *ps, Jv **out)
{
    Jv *v;
    skip_ws(ps);
    if (ps->p >= ps->end)
        return set_err(ps, "eof");
    if (*ps->p == '"') {
        char *s;
        if (parse_string(ps, &s) != 0)
            return -1;
        v = jv_new(JV_STR);
        if (!v) {
            free(s);
            return set_err(ps, "oom");
        }
        v->s = s;
        *out = v;
        return 0;
    }
    if (*ps->p == '{') {
        ps->p++;
        v = jv_new(JV_OBJ);
        if (!v)
            return set_err(ps, "oom");
        if (parse_object(ps, v) != 0) {
            jv_free(v);
            return -1;
        }
        *out = v;
        return 0;
    }
    if (*ps->p == '[') {
        ps->p++;
        v = jv_new(JV_ARR);
        if (!v)
            return set_err(ps, "oom");
        if (parse_array(ps, v) != 0) {
            jv_free(v);
            return -1;
        }
        *out = v;
        return 0;
    }
    if (ps->end - ps->p >= 4 && strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        *out = jv_new(JV_NULL);
        return *out ? 0 : set_err(ps, "oom");
    }
    if (ps->end - ps->p >= 4 && strncmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        v = jv_new(JV_BOOL);
        if (!v)
            return set_err(ps, "oom");
        v->b = 1;
        *out = v;
        return 0;
    }
    if (ps->end - ps->p >= 5 && strncmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        v = jv_new(JV_BOOL);
        if (!v)
            return set_err(ps, "oom");
        v->b = 0;
        *out = v;
        return 0;
    }
    v = jv_new(JV_NUM);
    if (!v)
        return set_err(ps, "oom");
    if (parse_number(ps, &v->n) != 0) {
        jv_free(v);
        return -1;
    }
    *out = v;
    return 0;
}

static Jv *json_parse(const char *buf, size_t n, char *err, size_t errn)
{
    JParse ps;
    Jv *root = NULL;
    ps.p = buf;
    ps.end = buf + n;
    ps.err[0] = 0;
    if (parse_value(&ps, &root) != 0) {
        snprintf(err, errn, "%s", ps.err);
        jv_free(root);
        return NULL;
    }
    skip_ws(&ps);
    if (ps.p != ps.end) {
        snprintf(err, errn, "trailing data");
        jv_free(root);
        return NULL;
    }
    return root;
}

static Jv *jv_get(const Jv *obj, const char *key)
{
    Jv *c;
    if (!obj || obj->type != JV_OBJ)
        return NULL;
    for (c = obj->child; c; c = c->next) {
        if (c->key && strcmp(c->key, key) == 0)
            return c;
    }
    return NULL;
}

static const char *jv_str(const Jv *v)
{
    return (v && v->type == JV_STR) ? v->s : NULL;
}

static int jv_bool(const Jv *v, int def)
{
    if (!v)
        return def;
    if (v->type == JV_BOOL)
        return v->b;
    return def;
}

static double jv_num(const Jv *v, double def)
{
    if (!v || v->type != JV_NUM)
        return def;
    return v->n;
}

static char *read_all(const char *path, size_t *out_n)
{
    FILE *fp;
    long n;
    char *buf;
    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = 0;
    if (out_n)
        *out_n = (size_t)n;
    return buf;
}

static int atomic_write(const char *path, const char *body)
{
    char tmp[1100];
    FILE *fp;
    size_t n;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return -1;
    fp = fopen(tmp, "wb");
    if (!fp)
        return -1;
    n = strlen(body);
    if (fwrite(body, 1, n, fp) != n) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fflush(fp) != 0 || fclose(fp) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* ---- sound events (same order as magma/game/runtime.h GM_SOUND_*) ---- */

static const char *const k_events[] = {
    NULL,
    "entity.chicken.hurt", "entity.chicken.death",
    "entity.pig.hurt", "entity.pig.death",
    "entity.cow.hurt", "entity.cow.death",
    "entity.sheep.hurt", "entity.sheep.death", "entity.sheep.shear",
    "entity.chicken.egg", "item.bucket.fill", "item.armor.equip_generic",
    "entity.pig.saddle", "entity.lightning.thunder",
    "entity.lightning.impact", "entity.firework.launch",
    "entity.firework.blast", "entity.firework.blast_far",
    "entity.firework.large_blast", "entity.firework.large_blast_far",
    "entity.firework.twinkle", "entity.firework.twinkle_far",
    "block.wood.break", "block.gravel.break", "block.grass.break",
    "block.stone.break", "block.metal.break", "block.glass.break",
    "block.cloth.break", "block.sand.break", "block.snow.break",
    "block.ladder.break", "block.anvil.break", "block.slime.break",
    "block.wood.place", "block.gravel.place", "block.grass.place",
    "block.stone.place", "block.metal.place", "block.glass.place",
    "block.cloth.place", "block.sand.place", "block.snow.place",
    "block.ladder.place", "block.anvil.place", "block.slime.place",
    "block.wood.hit", "block.gravel.hit", "block.grass.hit",
    "block.stone.hit", "block.metal.hit", "block.glass.hit",
    "block.cloth.hit", "block.sand.hit", "block.snow.hit",
    "block.ladder.hit", "block.anvil.hit", "block.slime.hit",
    "entity.bobber.splash", "block.dispenser.dispense",
    "block.dispenser.fail", "block.dispenser.launch",
    "entity.endereye.launch", "entity.firework.shoot",
    "block.iron_door.open", "block.wooden_door.open",
    "block.wooden_trapdoor.open", "block.fence_gate.open",
    "block.fire.extinguish", "block.iron_door.close",
    "block.wooden_door.close", "block.wooden_trapdoor.close",
    "block.fence_gate.close", "entity.ghast.warn", "entity.ghast.shoot",
    "entity.enderdragon.shoot", "entity.blaze.shoot",
    "entity.zombie.attack_door_wood", "entity.zombie.attack_iron_door",
    "entity.zombie.break_door_wood", "entity.wither.break_block",
    "entity.wither.shoot", "entity.bat.takeoff", "entity.zombie.infect",
    "entity.zombie_villager.converted", "block.anvil.destroy",
    "block.anvil.use", "block.anvil.land", "block.portal.travel",
    "block.chorus_flower.grow", "block.chorus_flower.death",
    "block.brewing_stand.brew", "block.iron_trapdoor.close",
    "block.iron_trapdoor.open", "entity.splash_potion.break",
    "entity.enderdragon_fireball.explode", "block.end_gateway.spawn",
    "entity.enderdragon.growl", "entity.villager.yes", "entity.villager.no",
    NULL,
    "record.13", "record.cat", "record.blocks", "record.chirp",
    "record.far", "record.mall", "record.mellohi", "record.stal",
    "record.strad", "record.ward", "record.11", "record.wait",
};
enum { N_EVENTS = (int)(sizeof(k_events) / sizeof(k_events[0])) };

typedef struct {
    char hash[48];
    float volume, pitch;
    int weight, stream;
} Variant;

typedef struct {
    Variant *v;
    int n, cap;
} VarList;

static int var_add(VarList *l, const char *hash, float vol, float pitch,
                   int weight, int stream)
{
    Variant *nv;
    if (l->n == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 64;
        nv = (Variant *)realloc(l->v, (size_t)ncap * sizeof(*nv));
        if (!nv)
            return -1;
        l->v = nv;
        l->cap = ncap;
    }
    snprintf(l->v[l->n].hash, sizeof(l->v[l->n].hash), "%s", hash);
    l->v[l->n].volume = vol;
    l->v[l->n].pitch = pitch;
    l->v[l->n].weight = weight;
    l->v[l->n].stream = stream;
    l->n++;
    return 0;
}

static int resolve(const Jv *entries, const Jv *objects, const char *name,
                   float volume, float pitch, int stream, int depth,
                   VarList *out)
{
    const Jv *entry, *sounds, *item;
    if (depth > 16)
        return -1;
    entry = jv_get(entries, name);
    if (!entry)
        return -1;
    sounds = jv_get(entry, "sounds");
    if (!sounds || sounds->type != JV_ARR)
        return -1;
    for (item = sounds->child; item; item = item->next) {
        const char *child;
        float cv = volume, cp = pitch;
        int cw = 1, cs = stream;
        const char *typ = NULL;
        if (item->type == JV_STR) {
            child = item->s;
        } else if (item->type == JV_OBJ) {
            child = jv_str(jv_get(item, "name"));
            if (!child)
                return -1;
            cv *= (float)jv_num(jv_get(item, "volume"), 1.0);
            cp *= (float)jv_num(jv_get(item, "pitch"), 1.0);
            cw = (int)jv_num(jv_get(item, "weight"), 1.0);
            cs = cs || jv_bool(jv_get(item, "stream"), 0);
            typ = jv_str(jv_get(item, "type"));
        } else {
            return -1;
        }
        if (typ && strcmp(typ, "event") == 0) {
            int before = out->n;
            int i;
            if (resolve(entries, objects, child, cv, cp, cs, depth + 1, out) != 0)
                return -1;
            for (i = before; i < out->n; i++)
                out->v[i].weight *= cw;
        } else {
            char key[256];
            const Jv *obj, *hashv;
            const char *digest;
            if (snprintf(key, sizeof(key), "minecraft/sounds/%s.ogg", child) >=
                (int)sizeof(key))
                return -1;
            obj = jv_get(objects, key);
            hashv = obj ? jv_get(obj, "hash") : NULL;
            digest = jv_str(hashv);
            if (!digest)
                return -1;
            if (var_add(out, digest, cv, cp, cw, cs ? 1 : 0) != 0)
                return -1;
        }
    }
    return 0;
}

static void fmt_float(char *dst, size_t n, float v)
{
    int k = snprintf(dst, n, "%.9g", (double)v);
    int i, has_dot = 0;
    if (k < 0)
        return;
    for (i = 0; dst[i]; i++) {
        if (dst[i] == '.' || dst[i] == 'e' || dst[i] == 'E')
            has_dot = 1;
    }
    if (!has_dot)
        snprintf(dst + strlen(dst), n - strlen(dst), ".0");
    snprintf(dst + strlen(dst), n - strlen(dst), "F");
}

static char *emit(const VarList *vars, const int span[][3])
{
    size_t cap = 8192;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    int i;
    if (!buf)
        return NULL;
    buf[0] = 0;
#define APP(fmt, ...)                                                          \
    do {                                                                       \
        for (;;) {                                                             \
            int _n = snprintf(buf + len, cap - len, fmt, ##__VA_ARGS__);       \
            if (_n < 0) {                                                      \
                free(buf);                                                     \
                return NULL;                                                   \
            }                                                                  \
            if ((size_t)_n + 1 <= cap - len) {                                 \
                len += (size_t)_n;                                             \
                break;                                                         \
            }                                                                  \
            cap *= 2;                                                          \
            {                                                                  \
                char *_nb = (char *)realloc(buf, cap);                         \
                if (!_nb) {                                                    \
                    free(buf);                                                 \
                    return NULL;                                               \
                }                                                              \
                buf = _nb;                                                     \
            }                                                                  \
        }                                                                      \
    } while (0)

    APP("/* GENERATED by make -C magma assets - DO NOT EDIT. */\n");
    APP("#ifndef MAGMA_ASSETS_SOUND_MANIFEST_H\n");
    APP("#define MAGMA_ASSETS_SOUND_MANIFEST_H\n");
    APP("typedef struct {\n");
    APP("    const char *hash; float volume, pitch; int weight, stream;\n");
    APP("} GmSoundAssetVariant;\n");
    APP("typedef struct { int start, count, total_weight; } GmSoundAssetSpan;\n");
    APP("#define GM_SOUND_ASSET_VARIANT_COUNT %d\n", vars->n);
    APP("static const GmSoundAssetVariant gm_sound_asset_variants[] = {\n");
    for (i = 0; i < vars->n; i++) {
        char vf[32], pf[32];
        fmt_float(vf, sizeof(vf), vars->v[i].volume);
        fmt_float(pf, sizeof(pf), vars->v[i].pitch);
        APP("    {\"%s\", %s, %s, %d, %d},\n", vars->v[i].hash, vf, pf,
            vars->v[i].weight, vars->v[i].stream);
    }
    APP("};\n");
    APP("static const GmSoundAssetSpan gm_sound_asset_spans[GM_SOUND_COUNT] = {\n");
    for (i = 0; i < N_EVENTS; i++)
        APP("    {%d, %d, %d},\n", span[i][0], span[i][1], span[i][2]);
    APP("};\n");
    APP("#endif\n");
#undef APP
    return buf;
}

int asset_build_sound(const char *index_path, const char *out_path)
{
    char *idx_buf = NULL, *snd_buf = NULL, *body = NULL;
    Jv *idx = NULL, *snd = NULL;
    char err[128];
    const Jv *objects, *sj, *hashv;
    const char *digest;
    char snd_path[1024];
    size_t root_len;
    VarList vars;
    int span[N_EVENTS][3];
    int i, rc = -1;

    memset(&vars, 0, sizeof(vars));
    memset(span, 0, sizeof(span));
    idx_buf = read_all(index_path, NULL);
    if (!idx_buf) {
        fprintf(stderr, "cannot read index: %s\n", index_path);
        goto done;
    }
    idx = json_parse(idx_buf, strlen(idx_buf), err, sizeof(err));
    if (!idx) {
        fprintf(stderr, "index json: %s\n", err);
        goto done;
    }
    objects = jv_get(idx, "objects");
    sj = objects ? jv_get(objects, "minecraft/sounds.json") : NULL;
    hashv = sj ? jv_get(sj, "hash") : NULL;
    digest = jv_str(hashv);
    if (!digest || strlen(digest) < 2) {
        fprintf(stderr, "sounds.json hash missing\n");
        goto done;
    }
    /* index is ROOT/indexes/1.11.json -> asset root is ROOT */
    {
        const char *c = index_path + strlen(index_path);
        int slashes = 0;
        root_len = 0;
        while (c > index_path) {
            c--;
            if (*c == '/') {
                slashes++;
                if (slashes == 2) {
                    root_len = (size_t)(c - index_path);
                    break;
                }
            }
        }
        if (slashes < 2) {
            fprintf(stderr, "index path has no assets root\n");
            goto done;
        }
    }
    if (snprintf(snd_path, sizeof(snd_path), "%.*s/objects/%c%c/%s",
                 (int)root_len, index_path, digest[0], digest[1],
                 digest) >= (int)sizeof(snd_path))
        goto done;
    snd_buf = read_all(snd_path, NULL);
    if (!snd_buf) {
        fprintf(stderr, "cannot read sounds.json: %s\n", snd_path);
        goto done;
    }
    snd = json_parse(snd_buf, strlen(snd_buf), err, sizeof(err));
    if (!snd) {
        fprintf(stderr, "sounds json: %s\n", err);
        goto done;
    }
    objects = jv_get(idx, "objects");
    for (i = 0; i < N_EVENTS; i++) {
        int start = vars.n;
        int total = 0;
        int j;
        if (!k_events[i]) {
            span[i][0] = start;
            span[i][1] = 0;
            span[i][2] = 0;
            continue;
        }
        if (resolve(snd, objects, k_events[i], 1.0f, 1.0f, 0, 0, &vars) != 0) {
            fprintf(stderr, "resolve failed: %s\n", k_events[i]);
            goto done;
        }
        for (j = start; j < vars.n; j++)
            total += vars.v[j].weight;
        span[i][0] = start;
        span[i][1] = vars.n - start;
        span[i][2] = total;
    }
    body = emit(&vars, span);
    if (!body)
        goto done;
    if (atomic_write(out_path, body) != 0) {
        fprintf(stderr, "write failed: %s\n", out_path);
        goto done;
    }
    rc = 0;
done:
    free(vars.v);
    free(body);
    free(idx_buf);
    free(snd_buf);
    jv_free(idx);
    jv_free(snd);
    return rc;
}

#ifndef SOUND_NO_MAIN
int main(int argc, char **argv)
{
    const char *index_path = NULL;
    const char *out_path = "assets/sound_manifest.h";
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
            index_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
            continue;
        }
        fprintf(stderr, "usage: %s --index PATH [--out PATH]\n", argv[0]);
        return 2;
    }
    if (!index_path) {
        fprintf(stderr, "usage: %s --index PATH [--out PATH]\n", argv[0]);
        return 2;
    }
    if (asset_build_sound(index_path, out_path) != 0)
        return 1;
    return 0;
}
#endif
