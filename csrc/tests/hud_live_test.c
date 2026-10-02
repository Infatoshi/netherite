/* Compare every generated live HUD field against one oracle frame's hud. */
#define _POSIX_C_SOURCE 200809L
#include "../engine/raster.h"
#include "../engine/env.h"
#include "../engine/tape.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int num(const struct jval *o, const char *k)
{
    int64_t n = 0;
    json_int(json_get(o, k), &n);
    return (int)n;
}

static float flt(const struct jval *o, const char *k)
{
    uint32_t u = 0;
    float f;
    json_float(json_get(o, k), &u);
    memcpy(&f, &u, 4);
    return f;
}

static void slot(struct hud_live_item *out, const struct jval *item)
{
    if (!item || item->kind == J_NULL) return;
    out->id = num(item, "id");
    out->meta = num(item, "dmg");
    out->count = num(item, "n");
}

/* The top-level fields the live emitter leaves to the renderer's own state:
 * BossStatus (the dragon's draw sets it, the HUD counts it down) and the chat
 * (the live client's messages, recorded ones carry the probe's own lines). */
static int live_only(const char *path, const char *key)
{
    return !strcmp(path, "hud") && (!strcmp(key, "boss") || !strcmp(key, "bosst") ||
                                     !strcmp(key, "bossh") || !strcmp(key, "chat"));
}

/* the emitter writes a null guiScreen where the probe leaves the key out */
static int absent(const char *path, const struct jval *o, int i)
{
    return live_only(path, o->fields[i].key) ||
           (!strcmp(o->fields[i].key, "guiScreen") && o->fields[i].val->kind == J_NULL);
}

static int count_fields(const struct jval *o, const char *path)
{
    int n = 0;
    for (int i = 0; i < o->nfields; ++i) n += !absent(path, o, i);
    return n;
}

static int equal(const struct jval *a, const struct jval *b, const char *path)
{
    if (!a || !b || a->kind != b->kind)
    {
        fprintf(stderr, "FAIL %s: kind or presence\n", path);
        return 0;
    }
    if (a->kind == J_OBJ)
    {
        if (count_fields(a, path) != count_fields(b, path))
        {
            fprintf(stderr, "FAIL %s: %d vs %d fields\n", path, count_fields(a, path), count_fields(b, path));
            return 0;
        }
        for (int i = 0; i < a->nfields; ++i)
        {
            if (absent(path, a, i)) continue;
            char next[256];
            snprintf(next, sizeof next, "%s.%s", path, a->fields[i].key);
            if (!equal(a->fields[i].val, json_get(b, a->fields[i].key), next)) return 0;
        }
        return 1;
    }
    if (a->kind == J_ARR)
    {
        if (a->nitems != b->nitems) { fprintf(stderr, "FAIL %s: array length\n", path); return 0; }
        for (int i = 0; i < a->nitems; ++i)
        {
            char next[256];
            snprintf(next, sizeof next, "%s[%d]", path, i);
            if (!equal(a->items[i], b->items[i], next)) return 0;
        }
        return 1;
    }
    int ok = a->kind == J_NULL ||
        (a->kind == J_NUM && a->has_num && b->has_num && a->num == b->num) ||
        (a->kind == J_STR && !strcmp(a->str, b->str)) ||
        (a->kind == J_BOOL && a->boolean == b->boolean);
    if (!ok) fprintf(stderr, "FAIL %s: %.*s vs %.*s\n", path,
                     (int)a->rawlen, a->raw, (int)b->rawlen, b->raw);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: hud_live_test SCENE\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", argv[1]);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0) { free(last); last = strdup(line); }
    free(line); fclose(f);
    if (!last) return 1;
    struct jval *row = json_parse(last);
    const struct jval *hud = json_get(row, "hud"), *bar = json_get(hud, "bar");
    const struct jval *disp = json_get(row, "disp");
    struct hud_live_state s = {0};
    int64_t n = 0;
    json_int(json_at(disp, 0), &n); s.width = (int)n;
    json_int(json_at(disp, 1), &n); s.height = (int)n;
    s.scale = num(hud, "sf"); s.fancy = num(hud, "fancy");
    s.hide = num(hud, "hide"); s.screen = num(hud, "screen");
    s.update_counter = num(hud, "uc"); s.current_slot = num(hud, "cur");
    s.vignette = flt(hud, "vign"); s.brightness = flt(hud, "bright");
    json_int(json_get(json_get(hud, "toast"), "now"), &n); s.clock_ms = n;
    {
        const struct jval *t = json_get(hud, "toast");
        json_int(json_get(t, "on"), &n); s.toast_on = (int)n;
        json_int(json_get(t, "l"), &n); s.toast_l = n;
        json_int(json_get(t, "desc"), &n); s.toast_desc = (int)n;
        s.toast_title = json_str(json_get(t, "title"));
        s.toast_sub = json_str(json_get(t, "sub"));
        const struct jval *ti = json_get(t, "item");
        if (ti && json_get(ti, "n") != NULL)
        {
            slot(&s.toast_item, ti);
            s.toast_has_item = 1;
        }
    }
    s.health = flt(bar, "hp"); s.previous_health = flt(bar, "prevhp");
    s.max_health = flt(bar, "mhp"); s.absorption = flt(bar, "abs");
    s.hurt_resistant = num(bar, "hurtres"); s.armor_value = num(bar, "armorv");
    s.food = num(bar, "food"); s.previous_food = num(bar, "pfood");
    s.saturation = flt(bar, "sat"); s.air = num(bar, "air");
    s.in_water = num(bar, "inwater"); s.hardcore = num(bar, "hardcore");
    s.xp = flt(bar, "xp"); s.level = num(bar, "lvl"); s.xp_cap = num(bar, "xpc");
    s.show_hud = num(bar, "hud"); s.survival = num(bar, "surv");
    s.poison = num(bar, "poison"); s.wither = num(bar, "wither");
    s.regen = num(bar, "regen"); s.hunger = num(bar, "hunger");
    s.tooltip_enabled = num(hud, "tooltips");
    s.gui_screen = json_get(hud, "guiScreen");
    s.highlight_ticks = num(hud, "hlt");
    slot(&s.highlighted, json_get(hud, "hl"));
    s.highlight_name = json_str(json_get(hud, "hlname"));
    const struct jval *inv = json_get(hud, "inv"), *armor = json_get(hud, "armor");
    for (int i = 0; i < 9; ++i) slot(&s.inventory[i], json_at(inv, i));
    for (int i = 0; i < 4; ++i) slot(&s.armor[i], json_at(armor, i));
    struct jval *made = json_parse(raster_hud_live_json(&s, argv[1]));
    int ok = equal(hud, made, "hud");
    json_free(made); json_free(row);
    if (ok) printf("PASS %s: every hud field matches\n", argv[1]);
    return ok ? 0 : 1;
}
