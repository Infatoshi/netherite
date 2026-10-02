/* Common fail-closed input check for every oracle-backed test executable.
 * The test-specific comparisons still run after this preflight. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include "../engine/tape.h"

int gate_test_main(int argc, char **argv);

struct input { const char *test; const char *file; int min_lines; };
static const struct input inputs[] = {
    {"activate", "cases.bin", 0}, {"animals", "ticks.bin.gz", 0}, {"biome_spawn", "manifest.json", 0},
    {"chunkload", "events.jsonl", 1}, {"clientworld", "clientrows.jsonl.gz", 1},
    {"containers", "player.jsonl.gz", 1}, {"crafting", "cases.jsonl", 1},
    {"det", "steps.txt.gz", 1}, {"dig", "cases.bin", 0},
    {"dragon", "ticks.txt", 1}, {"eyes", "ticks.txt", 1}, {"lightning", "ticks.txt", 1},
    {"dims", "terrain.bin.gz", 0}, {"drops", "lines.jsonl", 1},
    {"explosions", "cases.txt.gz", 1}, {"fallhang", "ticks.bin.gz", 0},
    {"fortress_spawn", "points.bin.gz", 0},
    {"feature", "cases.bin", 0}, {"ghasts", "ticks.bin.gz", 0},
    {"harvest", "cases.bin", 0}, {"harvestdrops", "lines.jsonl", 1},
    {"interleave", "manifest.json", 0}, {"relocate", "manifest.json", 0},
    {"hostiles", "ticks.bin.gz", 0}, {"items", "ticks.bin.gz", 0},
    {"itemuse", "cases.bin", 0}, {"jmath", "values.txt", 1}, {"loot", "draws.jsonl", 1},
    {"meshes", "sections.bin", 0}, {"move", "moves.bin", 0},
    {"paths", "cases.bin", 0}, {"place", "cases.bin", 0},
    {"populate", "calls.bin", 0}, {"portals", "cases.bin", 0},
    {"potions", "ticks.bin.gz", 0}, {"probe", "ops.bin", 0},
    {"projectiles", "ticks.bin.gz", 0}, {"raypick", "cases.bin.gz", 0},
    {"render", "golden.png", 0},
    {"renderstate", "frames.jsonl", 1},
    {"sblocks", "steps.bin", 0}, {"seedworld", "chunkstate.jsonl", 1},
    {"servertick", "tickrows.jsonl.gz", 1}, {"slimes", "ticks.bin.gz", 0},
    {"snapshots", "tape.jsonl", 2}, {"spawning", "tickrows.jsonl.gz", 1},
    {"structures", "starts.jsonl", 1}, {"ticks", "cases.bin", 0},
    {"rendertick", "atlas.json", 0}, {"hudscreens", "state/frames.jsonl", 1},
    {"tileticks", "ticks.bin", 0}, {"trades", "lines.jsonl", 1},
    {"villagers", "ticks.bin.gz", 0}, {"worldgen", "genbiomes.bin.gz", 0}
};

static int check_file(const char *dir, const char *file, int min_lines)
{
    char path[4096];
    if (snprintf(path, sizeof path, "%s/%s", dir, file) >= (int)sizeof path) return 0;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size == 0) return 0;
    int gz = strlen(file) > 3 && !strcmp(file + strlen(file) - 3, ".gz");
    if (gz)
    {
        gzFile f = gzopen(path, "rb");
        if (!f) return 0;
        char b[8192];
        int n = gzread(f, b, sizeof b);
        int ok = n > 0;
        if (min_lines)
        {
            int lines = 0;
            while (n > 0)
            {
                for (int i = 0; i < n; ++i) lines += b[i] == '\n';
                if (lines >= min_lines) break;
                n = gzread(f, b, sizeof b);
            }
            ok = lines >= min_lines;
        }
        if (gzclose(f) != Z_OK) ok = 0;
        return ok;
    }
    if (!min_lines) return 1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int c, lines = 0;
    while ((c = fgetc(f)) != EOF && lines < min_lines) lines += c == '\n';
    fclose(f);
    return lines >= min_lines;
}

static const char *count_key(const char *name)
{
    const char *cases[] = {"activate", "crafting", "dig", "drops", "explosions", "harvest",
                           "harvestdrops", "itemuse", "jmath", "paths", "place", "portals", "ticks", "feature"};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        if (!strcmp(name, cases[i])) return "cases";
    const char *ticks[] = {"animals", "chunkload", "dragon", "fallhang", "ghasts", "hostiles", "items", "lightning",
                           "potions", "projectiles", "slimes", "tileticks", "villagers", "spawning", "servertick"};
    for (size_t i = 0; i < sizeof ticks / sizeof ticks[0]; ++i)
        if (!strcmp(name, ticks[i])) return "ticks";
    if (!strcmp(name, "containers")) return "totalClicks";
    if (!strcmp(name, "det") || !strcmp(name, "sblocks")) return "steps";
    if (!strcmp(name, "move") || !strcmp(name, "probe")) return "ops";
    if (!strcmp(name, "populate")) return "calls";
    if (!strcmp(name, "raypick")) return "rays";
    if (!strcmp(name, "meshes")) return "records";
    if (!strcmp(name, "renderstate")) return "frames";
    if (!strcmp(name, "seedworld") || !strcmp(name, "snapshots")) return "chunks";
    return NULL;
}

static int check_count(const char *dir, const char *name)
{
    const char *key = count_key(name);
    if (!key) return 1;
    char path[4096], pattern[128];
    if (snprintf(path, sizeof path, "%s/manifest.json", dir) >= (int)sizeof path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long size = ftell(f);
    if (size <= 0 || size > 1048576 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    int ok = fread(buf, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    buf[size] = 0;
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    char *p = ok ? strstr(buf, pattern) : NULL;
    if (p) p = strchr(p + strlen(pattern), ':');
    if (p) { char *end; long long n = strtoll(p + 1, &end, 10); ok = end != p + 1 && n > 0; }
    else ok = 0;
    free(buf);
    return ok;
}

static int declared_name_exists(const char *dir, const char *f)
{
    if (!f || strpbrk(f, " /\\\t\n")) return 1;
    size_t n = strlen(f);
    const char *exts[] = {".bin", ".gz", ".jsonl", ".nbt", ".png"};
    int file = 0;
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; ++i)
    {
        size_t e = strlen(exts[i]);
        if (n > e && !strcmp(f + n - e, exts[i])) file = 1;
    }
    if (!file) return 1;
    char path[4096];
    struct stat st;
    if (snprintf(path, sizeof path, "%s/%s", dir, f) >= (int)sizeof path ||
        stat(path, &st) || !S_ISREG(st.st_mode))
    {
        fprintf(stderr, "FAIL declared golden missing: %s/%s\n", dir, f);
        return 0;
    }
    return 1;
}

static int declared_files_exist(const char *dir, const struct jval *v)
{
    if (!v) return 1;
    if (v->kind == J_OBJ)
    {
        for (int i = 0; i < v->nfields; ++i)
            if (!declared_name_exists(dir, v->fields[i].key) ||
                !declared_files_exist(dir, v->fields[i].val)) return 0;
    }
    else if (v->kind == J_ARR)
    {
        for (int i = 0; i < v->nitems; ++i)
            if (!declared_files_exist(dir, v->items[i])) return 0;
    }
    else if (v->kind == J_STR && !declared_name_exists(dir, v->str)) return 0;
    return 1;
}

static const char *expected_kind(const char *name)
{
    const char *direct[] = {"animals", "dragon", "ghasts", "potions", "items", "itemuse", "ticks",
                            "fallhang", "paths", "sblocks", "villagers", "hostiles", "slimes",
                            "tileticks", "place", "projectiles", "feature", "raypick", "jmath"};
    for (size_t i = 0; i < sizeof direct / sizeof direct[0]; ++i)
        if (!strcmp(name, direct[i])) return name;
    if (!strcmp(name, "probe")) return "setblock";
    if (!strcmp(name, "servertick")) return "netherite-snapshot";
    return NULL;
}

static int check_declarations(const char *dir, const char *name)
{
    char path[4096];
    if (snprintf(path, sizeof path, "%s/manifest.json", dir) >= (int)sizeof path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) { if (f) fclose(f); return 0; }
    long size = ftell(f);
    if (size <= 0 || size > 1048576 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    int ok = fread(buf, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    buf[size] = 0;
    if (!ok) { free(buf); return 0; }
    struct jval *manifest = json_parse(buf);
    if (!manifest) return 0;
    ok = declared_files_exist(dir, manifest);
    const char *kind = expected_kind(name);
    if (kind)
    {
        const char *got = json_str(json_get(manifest, "kind"));
        if (!got || strcmp(got, kind))
        {
            fprintf(stderr, "FAIL %s: expected manifest kind %s, got %s\n", name, kind, got ? got : "(none)");
            ok = 0;
        }
    }
    if (!strcmp(name, "explosions"))
    {
        const char *area = json_str(json_get(manifest, "area"));
        if (!area || strcmp(area, "explosions"))
        {
            fprintf(stderr, "FAIL explosions: wrong or missing manifest area\n");
            ok = 0;
        }
    }
    json_free(manifest);
    return ok;
}

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (strncmp(base, "test_", 5)) { fprintf(stderr, "FAIL unexpected test name %s\n", base); return 2; }
    const char *name = base + 5;
    const struct input *spec = NULL;
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; ++i)
        if (!strcmp(name, inputs[i].test)) { spec = &inputs[i]; break; }
    if (!spec || argc < 2) { fprintf(stderr, "FAIL %s: no recording directory\n", base); return 2; }
    int last = argc - 1;
    if (last >= 3 && !strcmp(argv[last - 1], "--trace")) last -= 2;
    const char *dir = argv[last];
    if (!check_file(dir, "manifest.json", 0))
    {
        fprintf(stderr, "FAIL %s: missing or empty manifest.json in %s\n", base, dir);
        return 2;
    }
    if (!check_file(dir, spec->file, spec->min_lines))
    {
        fprintf(stderr, "FAIL %s: missing or empty %s in %s\n", base, spec->file, dir);
        return 2;
    }
    if (!check_count(dir, name) || !check_declarations(dir, name))
    {
        fprintf(stderr, "FAIL %s: zero comparison count or missing declared golden in %s\n", base, dir);
        return 2;
    }
    return gate_test_main(argc, argv);
}
