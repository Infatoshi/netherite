/* config.yaml's reader (worldconf.h): WorldConf.java's parse, check and
 * registry, line for line. */
#include "worldconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tape.h"

/* WorldConf.KEYS, in config.yaml's order */
const struct wconf_key WCONF_KEYS[WC_NKEYS] = {
    {"world.seed", "1", "long"},
    {"world.difficulty", "normal", "enum:peaceful|easy|normal|hard"},
    {"world.render_distance", "4", "int:2:16"},
    {"world.daylight_cycle", "on", "onoff"},
    {"world.keep_inventory", "off", "onoff"},
    {"world.villages", "on", "onoff"},
    {"world.horses", "off", "onoff"},
    {"world.wolves", "off", "onoff"},
    {"world.ocelots", "off", "onoff"},
    {"world.built_golems", "off", "onoff"},
    {"world.wither", "off", "onoff"},
    {"world.minecarts", "off", "onoff"},
    {"world.boats", "off", "onoff"},
    {"world.redstone", "off", "onoff"},
    {"world.enchanting", "off", "onoff"},
    {"world.anvil", "off", "onoff"},
    {"world.brewing", "off", "onoff"},
    {"world.beacon", "off", "onoff"},
    {"world.maps", "off", "onoff"},
    {"world.fishing", "off", "onoff"},
    {"world.fireworks", "off", "onoff"},
    {"world.book_editing", "off", "onoff"},
    {"world.sign_editing", "off", "onoff"},
    {"client.graphics", "fast", "enum:fast|fancy"},
    {"client.particles", "minimal", "enum:all|decreased|minimal"},
    {"client.observation", "128x128", "size"},
    {"client.ticks_per_step", "4", "int:1:1000"},
    {"client.hud", "off", "onoff"},
    {"client.gamma", "0.0", "enum:0.0|0.25|0.5|0.75|1.0"},
    {"client.camera", "pixels", "enum:pixels|semantic|both"},
    {"client.semantic", "64x36", "size"},
    {"client.semantic_range", "64", "int:4:256"},
    {"engine.device_mesh", "0", "enum:0|1|2"},
    {"engine.device_generation", "off", "onoff"},
    {"engine.device_light", "off", "onoff"},
    {"pipeline.kind", "fresh", "name"},
    {"pipeline.start", "out/java/snapshots/fresh-play-s1", "path"},
    {"pipeline.worlds", "128", "int:1:65536"},
    {"pipeline.groups", "2", "int:1:65536"},
    {"pipeline.cards", "0", "cards"},
    {"pipeline.episode_seconds", "60", "int:1:86400"},
    {"actions.click_interval", "1", "int:1:100"},
    {"actions.mask_close_grid", "off", "onoff"},
    {"actions.mask_noop_clicks", "off", "onoff"},
    {"actions.move_stack", "off", "onoff"},
    {"actions.item_select", "off", "onoff"},
    {"actions.recipes", "off", "onoff"},
    {"actions.hold_attack", "off", "onoff"},
    {"actions.look_target", "off", "onoff"},
    {"actions.look_ticks", "3", "int:1:100"},
};

static const char *const DIFFICULTIES[4] = {"peaceful", "easy", "normal", "hard"};

static int fail(char *err, size_t n, const char *fmt, const char *a, int line, const char *b, const char *c)
{
    char where[600];
    if (line > 0) snprintf(where, sizeof where, "%s:%d", a, line);
    else snprintf(where, sizeof where, "%s", a);
    snprintf(err, n, fmt, where, b ? b : "", c ? c : "");
    return -1;
}

void wconf_defaults(struct wconf *c)
{
    for (int k = 0; k < WC_NKEYS; ++k) snprintf(c->val[k], WCONF_VAL, "%s", WCONF_KEYS[k].def);
    c->path[0] = 0;
}

int wconf_index(const char *key)
{
    for (int k = 0; k < WC_NKEYS; ++k)
        if (!strcmp(WCONF_KEYS[k].key, key)) return k;
    return -1;
}

static int is_name_start(char ch) { return ch >= 'a' && ch <= 'z'; }
static int is_name(char ch) { return is_name_start(ch) || (ch >= '0' && ch <= '9') || ch == '_'; }
static int is_digit(char ch) { return ch >= '0' && ch <= '9'; }
static int is_value(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || is_name(ch) || ch == '.' || ch == '/' || ch == '+' || ch == '-' || ch == ',';
}

/* -?[0-9]{1,max} */
static int is_int(const char *v, int max)
{
    const char *p = v + (*v == '-');
    int d = 0;
    for (; *p; ++p, ++d)
        if (!is_digit(*p)) return 0;
    return d >= 1 && d <= max;
}

/* WorldConf.check */
static int check(int k, const char *v)
{
    const char *kind = WCONF_KEYS[k].kind;
    if (!strcmp(kind, "onoff")) return !strcmp(v, "on") || !strcmp(v, "off");
    if (!strcmp(kind, "long")) return is_int(v, 19);
    if (!strncmp(kind, "int:", 4))
    {
        int lo = atoi(kind + 4), hi = atoi(strchr(kind + 4, ':') + 1);
        return is_int(v, 9) && atoi(v) >= lo && atoi(v) <= hi;
    }
    if (!strcmp(kind, "size"))
    {
        const char *x = strchr(v, 'x');
        if (!x) return 0;
        size_t a = (size_t)(x - v), b = strlen(x + 1);
        if (a < 2 || a > 4 || b < 2 || b > 4) return 0;
        for (const char *p = v; *p; ++p)
            if (p != x && !is_digit(*p)) return 0;
        return 1;
    }
    if (!strcmp(kind, "name"))
    {
        if (!is_name_start(*v)) return 0;
        for (const char *p = v; *p; ++p)
            if (!is_name(*p)) return 0;
        return 1;
    }
    if (!strcmp(kind, "path")) return *v != 0;
    if (!strcmp(kind, "cards"))
    {
        /* [0-9]{1,2}(,[0-9]{1,2})* */
        for (const char *p = v;;)
        {
            int d = 0;
            while (is_digit(*p)) ++p, ++d;
            if (d < 1 || d > 2) return 0;
            if (*p == 0) return 1;
            if (*p++ != ',') return 0;
        }
    }
    /* enum:A|B|... */
    size_t len = strlen(v);
    for (const char *p = kind + 5; *p;)
    {
        const char *bar = strchr(p, '|');
        size_t l = bar ? (size_t)(bar - p) : strlen(p);
        if (l == len && !strncmp(p, v, l)) return 1;
        if (!bar) break;
        p = bar + 1;
    }
    return 0;
}

static int put(struct wconf *c, const char *key, const char *v, const char *where, int line, char *err, size_t n)
{
    int k = wconf_index(key);
    if (k < 0) return fail(err, n, "%s: unknown key %s%s", where, line, key, NULL);
    if (strlen(v) >= WCONF_VAL || !check(k, v))
    {
        char msg[256];
        snprintf(msg, sizeof msg, "%s takes %s, not ", key, WCONF_KEYS[k].kind);
        return fail(err, n, "%s: %s%s", where, line, msg, v);
    }
    snprintf(c->val[k], WCONF_VAL, "%s", v);
    return 0;
}

/* " *" or " +#.*" to the end */
static int trailer(const char *p)
{
    const char *q = p;
    while (*q == ' ') ++q;
    return *q == 0 || (*q == '#' && q > p);
}

#define WC_SYNTAX "%s: expected a part (sim: or policy:), a section (name:) or an entry (key: value) at its indent%s%s"

/* name ":" trailer at buf; the name's length, or 0 */
static size_t header(const char *buf)
{
    size_t l = 0;
    if (!is_name_start(buf[0])) return 0;
    while (is_name(buf[l])) ++l;
    return buf[l] == ':' && trailer(buf + l + 1) && l < 48 ? l : 0;
}

/* Two forms. A file whose first line at column 0 is sim: or policy: is a
 * run file: its parts at column 0, each part's sections two spaces in and
 * their entries four; sim's are the registry's, policy's (the trainer's,
 * mcsr-speedrun runconf.py) are held to the grammar only. Any other file
 * is sim's sections at column 0, entries two spaces in. */
int wconf_parse(struct wconf *c, const char *text, const char *name, char *err, size_t n)
{
    char part[64] = "", section[64] = "", seen[512] = ",", keys[8192] = ",";
    int line = 0, parts = -1;
    for (const char *s = text; *s;)
    {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        char buf[1024];
        ++line;
        if (len >= sizeof buf) return fail(err, n, "%s: a line over %s bytes%s", name, line, "1023", NULL);
        memcpy(buf, s, len);
        buf[len] = 0;
        s = e ? e + 1 : s + len;
        if (len && buf[len - 1] == '\r') buf[--len] = 0;
        if (strchr(buf, '\t')) return fail(err, n, "%s: a tab (indent with spaces)%s%s", name, line, NULL, NULL);
        const char *t = buf;
        while (*t == ' ') ++t;
        if (*t == 0 || *t == '#') continue;
        int indent = (int)(t - buf);
        if (indent == 0)
        {
            size_t l = header(buf);
            if (!l) return fail(err, n, WC_SYNTAX, name, line, NULL, NULL);
            char h[64], tag[80];
            memcpy(h, buf, l);
            h[l] = 0;
            int is_part = !strcmp(h, "sim") || !strcmp(h, "policy");
            if (parts < 0) parts = is_part;
            if (parts)
            {
                if (!is_part) return fail(err, n, "%s: unknown part %s (sim or policy)%s", name, line, h, NULL);
                snprintf(tag, sizeof tag, ",%s,", h);
                if (strstr(seen, tag)) return fail(err, n, "%s: part %s twice%s", name, line, h, NULL);
                snprintf(seen + strlen(seen), sizeof seen - strlen(seen), "%s,", h);
                snprintf(part, sizeof part, "%s", h);
                section[0] = 0;
                continue;
            }
            /* a section of the old form (sim's) */
        }
        else if (parts > 0 && indent == 2 && is_name_start(*t) && header(t))
        {
            /* a part's section */
        }
        else
        {
            /* an entry: two spaces in (four in a run file), name ": " value trailer */
            if (indent != (parts > 0 ? 4 : 2) || !is_name_start(*t)) return fail(err, n, WC_SYNTAX, name, line, NULL, NULL);
            size_t k0 = (size_t)indent, k1 = k0;
            while (is_name(buf[k1])) ++k1;
            if (buf[k1] != ':' || buf[k1 + 1] != ' ' || !is_value(buf[k1 + 2]) || k1 - k0 >= 48)
                return fail(err, n, WC_SYNTAX, name, line, NULL, NULL);
            size_t v0 = k1 + 2, v1 = v0;
            while (is_value(buf[v1])) ++v1;
            if (!trailer(buf + v1)) return fail(err, n, WC_SYNTAX, name, line, NULL, NULL);
            if (!section[0]) return fail(err, n, "%s: an entry before any section%s%s", name, line, NULL, NULL);
            char key[160], tag[164];
            snprintf(key, sizeof key, "%s%s%s.%.*s", part, part[0] ? "." : "", section, (int)(k1 - k0), buf + k0);
            buf[v1] = 0;
            snprintf(tag, sizeof tag, ",%s,", key);
            if (strstr(keys, tag)) return fail(err, n, "%s: %s twice%s", name, line, key, NULL);
            if (strlen(keys) + strlen(key) + 2 > sizeof keys) return fail(err, n, "%s: too many keys%s%s", name, line, NULL, NULL);
            if (strcmp(part, "policy") && put(c, key + (part[0] ? 4 : 0), buf + v0, name, line, err, n)) return -1;
            snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%s,", key);
            continue;
        }
        /* a section: sim's (the old form's at column 0, a run file's under sim:) is the registry's */
        size_t l = header(t);
        char tag[96], pre[64];
        snprintf(section, sizeof section, "%.*s", (int)l, t);
        if (strcmp(part, "policy"))
        {
            snprintf(pre, sizeof pre, "%s.", section);
            int known = 0;
            for (int k = 0; k < WC_NKEYS; ++k)
                if (!strncmp(WCONF_KEYS[k].key, pre, strlen(pre))) known = 1;
            if (!known) return fail(err, n, "%s: unknown section %s%s", name, line, section, NULL);
        }
        snprintf(tag, sizeof tag, ",%s%s%s,", part, part[0] ? "." : "", section);
        if (strstr(seen, tag)) return fail(err, n, "%s: section %s twice%s", name, line, section, NULL);
        if (strlen(seen) + strlen(tag) + 1 > sizeof seen) return fail(err, n, "%s: too many sections%s%s", name, line, NULL, NULL);
        snprintf(seen + strlen(seen), sizeof seen - strlen(seen), "%s", tag + 1);
    }
    return 0;
}

int wconf_load(struct wconf *c, const char *path, char *err, size_t n)
{
    wconf_defaults(c);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, n, "%s: does not open%s%s", path, 0, NULL, NULL);
    char *text = NULL;
    size_t len = 0, cap = 0;
    for (;;)
    {
        if (len + 4096 + 1 > cap)
        {
            cap = cap ? cap * 2 : 16384;
            char *t = realloc(text, cap);
            if (!t) { free(text); fclose(f); return fail(err, n, "%s: out of memory%s%s", path, 0, NULL, NULL); }
            text = t;
        }
        size_t got = fread(text + len, 1, 4096, f);
        len += got;
        if (got < 4096) break;
    }
    fclose(f);
    text[len] = 0;
    int rc = strlen(text) != len ? fail(err, n, "%s: a NUL byte%s%s", path, 0, NULL, NULL) : wconf_parse(c, text, path, err, n);
    free(text);
    if (rc == 0) snprintf(c->path, sizeof c->path, "%s", path);
    return rc;
}

int wconf_set(struct wconf *c, const char *kv, char *err, size_t n)
{
    const char *eq = strchr(kv, '=');
    if (!eq || eq == kv) return fail(err, n, "--set %s: expected key=value%s%s", kv, 0, NULL, NULL);
    char key[128];
    int l = (int)(eq - kv);
    /* sim.section.key is section.key; policy's keys are the trainer's */
    if (l > 4 && !strncmp(kv, "sim.", 4)) kv += 4, l -= 4;
    if (memchr(kv, '.', (size_t)l)) snprintf(key, sizeof key, "%.*s", l, kv);
    else snprintf(key, sizeof key, "world.%.*s", l, kv);
    return put(c, key, eq + 1, "--set", 0, err, n);
}

int wconf_unimplemented(const struct wconf *c, char *err, size_t n)
{
    /* every sim.actions key is implemented: click_interval .. recipes by
     * lane/guiact's action compiler (csrc/runtime/rlact.h), hold_attack ..
     * look_ticks by lane/moveact's (csrc/runtime/moveact.h); a key added
     * later is refused here until its lane implements it */
    (void)c;
    (void)err;
    (void)n;
    return 0;
}

const char *wconf_get(const struct wconf *c, int k) { return c->val[k]; }
int wconf_int(const struct wconf *c, int k) { return atoi(c->val[k]); }
int wconf_on(const struct wconf *c, int k) { return !strcmp(c->val[k], "on"); }

int wconf_difficulty(const struct wconf *c)
{
    for (int i = 0; i < 4; ++i)
        if (!strcmp(c->val[WC_DIFFICULTY], DIFFICULTIES[i])) return i;
    return 2;
}

void wconf_size(const struct wconf *c, int *w, int *h)
{
    *w = atoi(c->val[WC_OBSERVATION]);
    *h = atoi(strchr(c->val[WC_OBSERVATION], 'x') + 1);
}

void wconf_semantic(const struct wconf *c, int *camera, int *w, int *h, int *range)
{
    const char *m = c->val[WC_CAMERA];
    *camera = !strcmp(m, "semantic") ? 1 : !strcmp(m, "both") ? 2 : 0;
    *w = atoi(c->val[WC_SEMANTIC]);
    *h = atoi(strchr(c->val[WC_SEMANTIC], 'x') + 1);
    *range = atoi(c->val[WC_SEMANTIC_RANGE]);
}

int wconf_device_mesh(const struct wconf *c) { return atoi(c->val[WC_DEVICE_MESH]); }

void wconf_start_path(const struct wconf *c, char *out, size_t n)
{
    const char *s = c->val[WC_START];
    if (s[0] == '/' || !c->path[0])
    {
        snprintf(out, n, "%s", s);
        return;
    }
    /* WorldConf.root: the file's directory, configs/'s parent for a named file */
    char dir[512];
    snprintf(dir, sizeof dir, "%s", c->path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, ".");
    const char *base = strrchr(dir, '/');
    base = base ? base + 1 : dir;
    if (!strcmp(base, "configs"))
    {
        if (base == dir) snprintf(dir, sizeof dir, ".");
        else dir[base - dir - 1] = 0;
    }
    snprintf(out, n, "%s/%s", dir, s);
}

int wconf_match_start(const struct wconf *c, const struct jval *world, const struct jval *options, int check_seed,
                      char *err, size_t n)
{
    if (!world) { snprintf(err, n, "the start names no world"); return 0; }
    /* the tape's world keys: the seed, the switches (an unsupported one runs
     * off), the game rules (absent at vanilla's) */
    for (int k = WC_SEED; k < WC_GRAPHICS; ++k)
    {
        if (k == WC_DIFFICULTY || k == WC_RENDER_DISTANCE || (k == WC_SEED && !check_seed)) continue;
        const char *name = strchr(WCONF_KEYS[k].key, '.') + 1;
        const char *want = k >= WC_UNSUPPORTED0 ? "off" : c->val[k];
        const char *got = json_str(json_get(world, name));
        if (!got && (k == WC_DAYLIGHT_CYCLE || k == WC_KEEP_INVENTORY)) got = WCONF_KEYS[k].def;
        if (!got || strcmp(got, want))
        {
            snprintf(err, n, "world.%s is %s in the config, %s in the start", name, want, got ? got : "absent");
            return 0;
        }
    }
    int64_t d = 2, rd = 4;
    if (options) json_int(json_get(options, "difficulty"), &d);
    if (options) json_int(json_get(options, "rd"), &rd);
    if (d != wconf_difficulty(c))
    {
        snprintf(err, n, "world.difficulty is %s in the config, %s in the start", c->val[WC_DIFFICULTY],
                 d >= 0 && d < 4 ? DIFFICULTIES[d] : "?");
        return 0;
    }
    if (rd != wconf_int(c, WC_RENDER_DISTANCE))
    {
        snprintf(err, n, "world.render_distance is %s in the config, %d in the start", c->val[WC_RENDER_DISTANCE], (int)rd);
        return 0;
    }
    return 1;
}
