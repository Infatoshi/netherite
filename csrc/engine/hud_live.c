/* The live player's HUD inputs, serialized to the RenderStateProbe shape so
 * recorded and playable frames call the same raster_hud_draw implementation. */
#define _POSIX_C_SOURCE 200809L
#include "raster.h"
#include "env.h"
#include "tape.h"
#include "chat.h"

#include <stdarg.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct writer { char *s; size_t n, cap; };

static void add(struct writer *w, const char *fmt, ...)
{
    for (;;)
    {
        va_list ap;
        va_start(ap, fmt);
        int need = vsnprintf(w->s + w->n, w->cap - w->n, fmt, ap);
        va_end(ap);
        if (need < 0) { fprintf(stderr, "hud_live: format error\n"); exit(1); }
        if ((size_t)need < w->cap - w->n) { w->n += (size_t)need; return; }
        w->cap = (w->cap + (size_t)need + 1) * 2;
        char *s = realloc(w->s, w->cap);
        if (!s) { fprintf(stderr, "hud_live: allocation failed\n"); exit(1); }
        w->s = s;
    }
}

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = 0;
    fclose(f);
    return s;
}

static struct jval *table;
static char table_scene[1024];
static struct jval *names;
static char names_scene[1024];
static int shared_tables;   /* table and names are the shared store's */
/* the loads (lane/guiact: GUI draws on several threads; a scene's tables
 * are only read once loaded) */
static pthread_mutex_t tables_lock = PTHREAD_MUTEX_INITIALIZER;

const struct jval *(*raster_hud_shared_json)(const char *path);

/* PATH parsed: the shared store's (never freed here), else this file's own */
static struct jval *table_load(const char *path)
{
    if (raster_hud_shared_json)
    {
        shared_tables = 1;
        return (struct jval *)raster_hud_shared_json(path);
    }
    char *s = read_text(path);
    if (!s) { fprintf(stderr, "hud_live: cannot read %s\n", path); return NULL; }
    struct jval *j = json_parse(s);
    if (!j) { fprintf(stderr, "hud_live: invalid %s\n", path); exit(1); }
    return j;
}

const char *raster_hud_item_name(const char *scene, int id, int meta)
{
    pthread_mutex_lock(&tables_lock);
    if (strcmp(scene, names_scene))
    {
        char path[1200];
        snprintf(path, sizeof path, "%s/state/item_names.json", scene);
        if (!shared_tables) json_free(names);
        names = table_load(path);
        if (!names) { names_scene[0] = 0; pthread_mutex_unlock(&tables_lock); return NULL; }
        snprintf(names_scene, sizeof names_scene, "%s", scene);
    }
    pthread_mutex_unlock(&tables_lock);
    if (id < 0 || id >= json_len(names)) return NULL;
    const struct jval *variants = json_at(names, id);
    if (!variants || variants->kind == J_NULL) return NULL;
    if (meta < 0 || meta >= json_len(variants)) meta = 0;
    return json_str(json_at(variants, meta));
}

const struct jval *raster_hud_item_entry(const char *scene, int id, int meta)
{
    pthread_mutex_lock(&tables_lock);
    if (strcmp(scene, table_scene))
    {
        char path[1200];
        snprintf(path, sizeof path, "%s/state/item_table.json", scene);
        if (!shared_tables) json_free(table);
        table = table_load(path);
        if (!table) { table_scene[0] = 0; pthread_mutex_unlock(&tables_lock); return NULL; }
        snprintf(table_scene, sizeof table_scene, "%s", scene);
    }
    pthread_mutex_unlock(&tables_lock);
    if (id < 0 || id >= json_len(table)) return NULL;
    const struct jval *variants = json_at(table, id);
    if (!variants || variants->kind == J_NULL) return NULL;
    if (meta < 0 || meta >= json_len(variants)) meta = 0;
    return json_at(variants, meta);
}

static void item_json(struct writer *w, const char *scene, const struct hud_live_item *it)
{
    if (it->count <= 0) { add(w, "null"); return; }
    const struct jval *entry = raster_hud_item_entry(scene, it->id, it->meta);
    if (!entry || entry->kind == J_NULL)
    {
        fprintf(stderr, "hud_live: item %d:%d has no oracle icon entry\n", it->id, it->meta);
        add(w, "null");
        return;
    }
    add(w, "{");
    for (int i = 0; i < entry->nfields; ++i)
    {
        /* the stack's own count and damage, not the icon variant's */
        if (!strcmp(entry->fields[i].key, "n") || !strcmp(entry->fields[i].key, "dmg")) continue;
        char *raw = json_raw(entry->fields[i].val);
        add(w, "\"%s\":%s,", entry->fields[i].key, raw);
        free(raw);
    }
    /* the stack's tag in the item tag store (draw_stack's own colours) */
    add(w, "\"dmg\":%d,\"n\":%d,\"tag\":%d", it->meta, it->count, it->tag);
    if (it->pop > 0.0f) add(w, ",\"pop\":\"f:%08x\"", fbits(it->pop));
    add(w, "}");
}

static void string_json(struct writer *w, const char *s)
{
    if (!s) { add(w, "null"); return; }
    add(w, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        if (*p == '"' || *p == '\\') add(w, "\\%c", *p);
        else if (*p < 32) add(w, "\\u%04x", *p);
        else add(w, "%c", *p);
    }
    add(w, "\"");
}

char *raster_hud_live_json(const struct hud_live_state *s, const char *scene)
{
    struct writer w = {malloc(4096), 0, 4096};
    if (!w.s) return NULL;
    w.s[0] = 0;
    int scale = s->scale > 0 ? s->scale : 1;
    int sw = (s->width + scale - 1) / scale;
    int sh = (s->height + scale - 1) / scale;
    add(&w, "{\"sw\":%d,\"sh\":%d,\"swd\":\"d:%016llx\",\"shd\":\"d:%016llx\","
        "\"sf\":%d,\"hide\":%d,\"screen\":%d,\"fancy\":%d,\"scale\":%d,",
        sw, sh, (unsigned long long)dbits((double)s->width / scale),
        (unsigned long long)dbits((double)s->height / scale), scale, s->hide, s->screen, s->fancy, scale);
    add(&w, "\"toast\":{\"on\":%d,\"l\":%lld,\"now\":%lld,\"desc\":%d,\"title\":", s->toast_on, s->toast_l, s->clock_ms, s->toast_desc);
    string_json(&w, s->toast_title);
    add(&w, ",\"sub\":");
    string_json(&w, s->toast_sub);
    add(&w, ",\"item\":");
    if (s->toast_has_item) item_json(&w, scene, &s->toast_item);
    else add(&w, "null");
    add(&w, "},");
    add(&w, "\"bar\":{\"hp\":\"f:%08x\",\"prevhp\":\"f:%08x\",\"mhp\":\"f:%08x\","
        "\"abs\":\"f:%08x\",\"hurtres\":%d,\"armorv\":%d,\"food\":%d,\"pfood\":%d,"
        "\"sat\":\"f:%08x\",\"air\":%d,\"inwater\":%d,\"hardcore\":%d,"
        "\"xp\":\"f:%08x\",\"lvl\":%d,\"xpc\":%d,\"hud\":%d,\"surv\":%d,"
        "\"horse\":0,\"poison\":%d,\"wither\":%d,\"regen\":%d,\"hunger\":%d,"
        "\"mount\":%d,\"mhp2\":\"f:%08x\",\"mmax\":\"f:%08x\"},",
        fbits(s->health), fbits(s->previous_health), fbits(s->max_health), fbits(s->absorption),
        s->hurt_resistant, s->armor_value, s->food, s->previous_food, fbits(s->saturation),
        s->air, s->in_water, s->hardcore, fbits(s->xp), s->level, s->xp_cap,
        s->show_hud, s->survival, s->poison, s->wither, s->regen, s->hunger,
        s->mount, fbits(s->mount_health), fbits(s->mount_max));
    add(&w, "\"slp\":%d,", s->sleep_timer);
    add(&w, "\"uc\":%d,\"vign\":\"f:%08x\",\"bright\":\"f:%08x\","
        "\"rec\":\"\","
        "\"recup\":0,\"recplay\":0,\"tooltips\":%d,\"hlt\":%d,\"hl\":",
        s->update_counter, fbits(s->vignette), fbits(s->brightness),
        s->tooltip_enabled, s->highlight_ticks);
    item_json(&w, scene, &s->highlighted);
    add(&w, ",\"hlname\":");
    string_json(&w, s->highlight_name);
    add(&w, ",\"inv\":[");
    for (int i = 0; i < 9; ++i)
    {
        if (i) add(&w, ",");
        item_json(&w, scene, &s->inventory[i]);
    }
    add(&w, "],\"armor\":[");
    for (int i = 0; i < 4; ++i)
    {
        if (i) add(&w, ",");
        item_json(&w, scene, &s->armor[i]);
    }
    add(&w, "],\"cur\":%d,", s->current_slot);
    /* GuiNewChat at GameSettings' defaults (opacity, scale and width 1,
     * unfocused height 0.44366196); the boss bar's fields are left out, so
     * the HUD counts down the status the dragon's draw set */
    add(&w, "\"chat\":{\"on\":1,\"vis\":0,\"col\":1,\"op\":\"f:3f800000\",\"sc\":\"f:3f800000\","
        "\"wd\":\"f:3f800000\",\"hf\":\"f:3f800000\",\"hu\":\"f:3ee327a9\",\"open\":%d,\"scroll\":%d,\"sflag\":%d,\"msgs\":[",
        s->chat_open, s->chat_scroll, s->chat_scrolled);
    for (int i = 0; s->chat && i < s->chat->nmsg; ++i)
    {
        const struct chat_message *m = &s->chat->msg[i];
        add(&w, "%s{\"t\":%d,\"id\":%d,\"parts\":[", i ? "," : "", m->counter, m->id);
        for (int k = 0; k < m->nparts; ++k)
        {
            add(&w, "%s[", k ? "," : "");
            string_json(&w, m->fmt[k]);
            add(&w, ",");
            string_json(&w, m->text[k]);
            add(&w, "]");
        }
        add(&w, "]}");
    }
    add(&w, "]},");
    add(&w, "\"guiScreen\":");
    if (s->gui_screen)
    {
        char *raw = json_raw(s->gui_screen);
        add(&w, "%s", raw);
        free(raw);
    }
    else add(&w, "null");
    add(&w, ",");
    add(&w, "\"pumpkin\":%d,\"portal\":\"f:%08x\",\"tportal\":%d}",
        s->pumpkin_overlay && s->third_person == 0 ? 1 : 0, fbits(s->portal), s->confusion);
    return w.s;
}


/* The toast-only draw's inputs: raster_hud_draw with hide=1 draws no HUD and no
 * GuiScreen, so the caller can layer the toast over the open screen the way
 * vanilla's runGameLoop does (Minecraft.java:1056, after the screen). */
char *raster_hud_toast_json(const struct hud_live_state *s, const char *scene)
{
    struct writer w = {malloc(4096), 0, 4096};
    if (!w.s) return NULL;
    w.s[0] = 0;
    int scale = s->scale > 0 ? s->scale : 1;
    int sw = (s->width + scale - 1) / scale;
    int sh = (s->height + scale - 1) / scale;
    add(&w, "{\"sw\":%d,\"sh\":%d,\"swd\":\"d:%016llx\",\"shd\":\"d:%016llx\","
        "\"sf\":%d,\"hide\":1,\"screen\":0,\"fancy\":%d,\"scale\":%d,",
        sw, sh, (unsigned long long)dbits((double)s->width / scale),
        (unsigned long long)dbits((double)s->height / scale), scale, s->fancy, scale);
    add(&w, "\"toast\":{\"on\":%d,\"l\":%lld,\"now\":%lld,\"desc\":%d,\"title\":", s->toast_on, s->toast_l, s->clock_ms, s->toast_desc);
    string_json(&w, s->toast_title);
    add(&w, ",\"sub\":");
    string_json(&w, s->toast_sub);
    add(&w, ",\"item\":");
    if (s->toast_has_item) item_json(&w, scene, &s->toast_item);
    else add(&w, "null");
    add(&w, "}}");
    return w.s;
}

int raster_hud_live(unsigned char *rgb, int w, int h, const char *scene,
                    const struct hud_live_state *state)
{
    char *s = raster_hud_live_json(state, scene);
    if (!s) return 0;
    struct jval *j = json_parse(s);
    if (!j) { fprintf(stderr, "hud_live: invalid generated JSON\n"); return 0; }
    int ok = raster_hud_draw(rgb, w, h, scene, j);
    json_free(j);
    return ok;
}
