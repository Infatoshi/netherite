/* The HUD and screen logic the hudscreens recordings pin, checked against the
 * Java values RenderStateProbe wrote at each frame (state/frames.jsonl):
 *   - GuiNewChat.func_146237_a: every drawn line (its text and counter) the
 *     native split builds from the recorded messages;
 *   - GuiWinGame.initGui: the credits' line list, obfuscation lengths and
 *     wrapping included;
 *   - InventoryEffectRenderer: the effect column's order, the client's
 *     HashMap iteration (potion_map) over the recorded effects;
 *   - RenderItem.renderItemOverlayIntoGUI: each item's max damage (ITEMS);
 *   - ColorizerFoliage and the block-coloured items: the item colour
 *     item_block_color gives against the recorded getColorFromItemStack;
 *   - BossStatus: the dragon's formatted name;
 *   - GuiContainer.keyTyped: the key-to-click rule (a fixed case list).
 * Negative controls run on every recording: a chat width one short, a
 * credits user renamed and a reversed effect insertion must each differ.
 *
 *   test_hudscreens RECORDING_DIR */
#define _POSIX_C_SOURCE 200809L
#include "../engine/chat.h"
#include "../engine/container.h"
#include "../engine/font.h"
#include "../engine/gui_screens.h"
#include "../engine/item_color.h"
#include "../engine/items.h"
#include "../engine/potion.h"
#include "../engine/raster.h"
#include "../engine/tape.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void check(int ok, const char *what, const char *detail)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        if (failures <= 20) printf("FAIL test_hudscreens: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
    }
}

static char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *b = malloc((size_t)len + 1);
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f);
    if (b) b[len] = 0;
    if (n) *n = (size_t)len;
    return b;
}

static float jf(const struct jval *v, float d)
{
    uint32_t b;
    if (!v || !json_float(v, &b)) return d;
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static int ji(const struct jval *v, int d)
{
    int64_t n;
    if (!v || !json_int(v, &n)) return d;
    return (int)n;
}

/* the chat lines the native split builds from the frame's messages */
static int split_lines(const struct font_metrics *m, const struct jval *chat, int width_delta,
                       struct chat *out)
{
    memset(out, 0, sizeof *out);
    float scale = jf(json_get(chat, "sc"), 1.0f);
    int width = (int)(chat_width(jf(json_get(chat, "wd"), 1.0f)) / scale) + width_delta;
    const struct jval *msgs = json_get(chat, "msgs");
    for (int i = json_len(msgs) - 1; i >= 0; --i)
    {
        const struct jval *mj = json_at(msgs, i), *parts = json_get(mj, "parts");
        const char *fmt[CHAT_PARTS], *text[CHAT_PARTS];
        int n = json_len(parts) < CHAT_PARTS ? json_len(parts) : CHAT_PARTS;
        for (int k = 0; k < n; ++k)
        {
            fmt[k] = json_str(json_at(json_at(parts, k), 0));
            text[k] = json_str(json_at(json_at(parts, k), 1));
            if (!fmt[k]) fmt[k] = "";
            if (!text[k]) text[k] = "";
        }
        struct chat_message msg;
        chat_message_set(&msg, ji(json_get(mj, "t"), 0), ji(json_get(mj, "id"), 0), n, fmt, text);
        chat_add(out, m, &msg, width, ji(json_get(chat, "col"), 1));
    }
    return out->nline;
}

static int lines_equal(const struct chat *c, const struct jval *want)
{
    if (c->nline != json_len(want)) return 0;
    for (int i = 0; i < c->nline; ++i)
    {
        const struct jval *w = json_at(want, i);
        const char *t = json_str(json_at(w, 1));
        if (!t || strcmp(t, c->line[i].text) || ji(json_at(w, 0), -1) != c->line[i].counter) return 0;
    }
    return 1;
}

static void check_item(const struct jval *it, const char *where)
{
    if (!it || it->kind != J_OBJ) return;
    int id = ji(json_get(it, "id"), 0), meta = ji(json_get(it, "dmg"), 0);
    const struct jval *maxd = json_get(it, "maxd");
    char d[160];
    if (maxd && id > 0 && id < 4096)
    {
        snprintf(d, sizeof d, "%s item %d max damage %d, native %d", where, id, ji(maxd, -1), ITEMS[id].max_damage);
        check(ji(maxd, -1) == (int)ITEMS[id].max_damage, "durability", d);
    }
    int c = item_block_color(id, meta, -1);
    if (c >= 0 && id != 31)
    {
        snprintf(d, sizeof d, "%s item %d:%d colour %d, native %d", where, id, meta, ji(json_get(it, "tint"), -1), c);
        check(ji(json_get(it, "tint"), -1) == c, "item colour", d);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test_hudscreens RECORDING_DIR\n"); return 2; }
    const char *dir = argv[1];
    char p[2048];

    /* the font widths the recording's own ascii.png gives */
    snprintf(p, sizeof p, "%s/state/gui.json", dir);
    char *gt = read_all(p, NULL);
    struct jval *gui = gt ? json_parse(gt) : NULL;
    if (!gui) { printf("FAIL test_hudscreens: no state/gui.json in %s\n", dir); return 1; }
    const struct jval *am = json_get(gui, "ascii");
    snprintf(p, sizeof p, "%s/state/gui_ascii.rgba", dir);
    size_t an = 0;
    unsigned char *ascii = (unsigned char *)read_all(p, &an);
    int aw = ji(json_get(am, "w"), 0), ah = ji(json_get(am, "h"), 0);
    if (!ascii || an != (size_t)aw * ah * 4) { printf("FAIL test_hudscreens: no font in %s\n", dir); return 1; }
    struct font_metrics fm;
    font_metrics_load(&fm, ascii, aw, ah);

    /* the item table: every icon record's max damage and block colour */
    snprintf(p, sizeof p, "%s/state/item_table.json", dir);
    char *tt = read_all(p, NULL);
    struct jval *table = tt ? json_parse(tt) : NULL;
    for (int id = 0; table && id < json_len(table); ++id)
    {
        const struct jval *vars = json_at(table, id);
        for (int k = 0; vars && vars->kind == J_ARR && k < json_len(vars); ++k)
            check_item(json_at(vars, k), "item_table");
    }

    snprintf(p, sizeof p, "%s/state/frames.jsonl", dir);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("FAIL test_hudscreens: no frames in %s\n", dir); return 1; }
    char *line = NULL;
    size_t cap = 0;
    int frames = 0, chats = 0, credits = 0, effects = 0, bosses = 0;
    while (getline(&line, &cap, f) >= 0)
    {
        struct jval *row = json_parse(strdup(line));
        if (!row) continue;
        ++frames;
        const struct jval *hud = json_get(row, "hud");
        char d[256];
        int t = ji(json_get(row, "t"), -1);

        const struct jval *chat = json_get(hud, "chat");
        if (chat && json_len(json_get(chat, "msgs")) > 0)
        {
            struct chat c;
            split_lines(&fm, chat, 0, &c);
            snprintf(d, sizeof d, "frame t=%d: %d native lines, %d recorded", t, c.nline,
                     json_len(json_get(chat, "lines")));
            check(lines_equal(&c, json_get(chat, "lines")), "chat lines", d);
            chat_free(&c);
            /* negative control: at a quarter of the width the lines wrap */
            split_lines(&fm, chat, -3 * chat_width(jf(json_get(chat, "wd"), 1.0f)) / 4, &c);
            check(!lines_equal(&c, json_get(chat, "lines")), "chat negative control", "a narrower split still matched");
            chat_free(&c);
            ++chats;
        }

        const struct jval *screen = json_get(hud, "guiScreen");
        const char *kind = json_str(json_get(screen, "kind"));
        if (kind && !strcmp(kind, "credits"))
        {
            const char *user = json_str(json_get(screen, "user"));
            int n = 0;
            char **lines = raster_hud_credits_lines(dir, user, &n);
            const struct jval *want = json_get(screen, "lines");
            int same = lines && n == json_len(want);
            for (int i = 0; same && i < n; ++i)
                same = json_str(json_at(want, i)) && !strcmp(lines[i], json_str(json_at(want, i)));
            snprintf(d, sizeof d, "frame t=%d: %d native lines, %d recorded", t, n, json_len(want));
            check(same, "credits lines", d);
            check(n * 12 == ji(json_get(screen, "total"), -1), "credits total", d);
            for (int i = 0; i < n; ++i) free(lines[i]);
            free(lines);
            /* negative control: another player name changes the poem */
            lines = raster_hud_credits_lines(dir, "Steve", &n);
            same = lines && n == json_len(want);
            for (int i = 0; same && i < n; ++i)
                same = json_str(json_at(want, i)) && !strcmp(lines[i], json_str(json_at(want, i)));
            check(!same, "credits negative control", "another user's list matched");
            for (int i = 0; i < n; ++i) free(lines[i]);
            free(lines);
            ++credits;
        }

        const struct jval *eff = json_get(screen, "effects");
        if (eff && json_len(eff) > 0)
        {
            /* the effects go in by id; the map's bucket order must be Java's */
            struct potion_map m;
            potion_map_init(&m);
            int ids[32], n = json_len(eff) < 32 ? json_len(eff) : 32, high = 0;
            for (int i = 0; i < n; ++i) { ids[i] = ji(json_at(json_at(eff, i), 0), 0); high |= ids[i] >= 16; }
            for (int id = 0; id < 32; ++id)
                for (int i = 0; i < n; ++i)
                    if (ids[i] == id)
                    {
                        struct potion_effect e = {0};
                        e.id = (uint8_t)id;
                        potion_map_put(&m, &e);
                    }
            int got[32], k = 0, sorted = 1;
            uint8_t order[POT_COUNT];
            int norder = potion_map_order(&m, order);
            for (int i = 0; i < norder; ++i) got[k++] = order[i];
            int same = k == n;
            for (int i = 0; i < n; ++i) same &= got[i] == ids[i];
            for (int i = 1; i < n; ++i) sorted &= ids[i - 1] < ids[i];
            snprintf(d, sizeof d, "frame t=%d", t);
            check(same, "effect order", d);
            /* negative control: with an id past 15 the list is not in id order */
            if (high) check(!sorted, "effect order negative control", "the recorded list is in id order");
            ++effects;
        }

        const struct jval *inv = json_get(hud, "inv");
        for (int i = 0; i < json_len(inv); ++i) check_item(json_at(inv, i), "hotbar");

        const struct jval *mobs = json_get(row, "mobs");
        for (int i = 0; i < json_len(mobs); ++i)
        {
            const struct jval *mb = json_at(mobs, i);
            const char *cls = json_str(json_get(mb, "class"));
            if (!cls || strcmp(cls, "EntityDragon") || !json_get(mb, "bossName")) continue;
            check(!strcmp(json_str(json_get(mb, "bossName")), "Ender Dragon\302\247r"), "boss name",
                  json_str(json_get(mb, "bossName")));
            ++bosses;
        }
        json_free(row);
    }
    free(line);
    fclose(f);

    /* GuiContainer.keyTyped over a player container with a stack in slot 9 */
    struct container c;
    container_init(&c, CONTAINER_PLAYER, 0, 0);
    /* container slot 9 is the main inventory's first slot, inventory index 9 */
    c.player.slot[9].item = 4;
    c.player.slot[9].count = 64;
    check(container_slot(&c, 9) != NULL, "container slot 9 holds the stack", NULL);
    int button = -1, mode = -1;
    check(gui_screen_key_click(&c, 9, 1, 0, 0, 1, &button, &mode) && button == 1 && mode == 2, "key swap", NULL);
    check(!gui_screen_key_click(&c, 9, 1, 0, 0, 0, &button, &mode), "key swap with a cursor stack", NULL);
    check(gui_screen_key_click(&c, 12, 3, 0, 0, 1, &button, &mode) && mode == 2, "key swap over an empty slot", NULL);
    check(gui_screen_key_click(&c, 9, -1, 1, 0, 1, &button, &mode) && button == 0 && mode == 4, "key drop", NULL);
    check(gui_screen_key_click(&c, 9, -1, 1, 1, 1, &button, &mode) && button == 1 && mode == 4, "key ctrl drop", NULL);
    check(!gui_screen_key_click(&c, 12, -1, 1, 0, 1, &button, &mode), "key drop over an empty slot", NULL);
    check(!gui_screen_key_click(&c, -1, 2, 1, 0, 1, &button, &mode), "key with no slot", NULL);
    container_free(&c);

    printf("hudscreens %s: %d frames, %d chat frames, %d credits frames, %d effect columns, %d boss names, %d checks\n",
           dir, frames, chats, credits, effects, bosses, checks);
    if (frames == 0) { printf("FAIL test_hudscreens: no frames\n"); return 1; }
    if (failures) { printf("FAIL test_hudscreens: %d of %d checks\n", failures, checks); return 1; }
    printf("PASS test_hudscreens %s\n", dir);
    json_free(table);
    json_free(gui);
    return 0;
}
