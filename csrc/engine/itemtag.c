/* See itemtag.h. */
#include "env.h"
#include "itemtag.h"
#include "nbtw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITAG_BUCKETS (2 * ITAG_CAP)

#define entries (nw_env->itemtag.entries)
/* 0 is the null tag */
#define nentries (nw_env->itemtag.nentries)
/* entry index + 1, 0 empty */
#define heads (nw_env->itemtag.heads)
#define arena (nw_env->itemtag.arena)
#define arena_used (nw_env->itemtag.arena_used)

static uint64_t text_hash(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
    return h;
}

/* The region cache's journal (regioncache.h): the build it captures reuses
 * or makes these tags in this order. */
static int noted(int idx)
{
    if (!nw_env->itemtag.journal_on) return idx;
    if (nw_env->itemtag.njournal == nw_env->itemtag.capjournal)
    {
        nw_env->itemtag.capjournal = nw_env->itemtag.capjournal ? nw_env->itemtag.capjournal * 2 : 64;
        nw_env->itemtag.journal = realloc(nw_env->itemtag.journal,
            (size_t)nw_env->itemtag.capjournal * sizeof *nw_env->itemtag.journal);
        if (nw_env->itemtag.journal == NULL) abort();
    }
    nw_env->itemtag.journal[nw_env->itemtag.njournal++] = idx;
    return idx;
}

static int lookup(const char *text, size_t n, uint64_t h)
{
    for (int e = heads[h & (ITAG_BUCKETS - 1)]; e; e = entries[e - 1].next)
    {
        const struct itag *t = &entries[e - 1];
        if (t->hash == h && t->len == n && memcmp(arena + t->off, text, n) == 0) return e - 1;
    }
    return 0;
}

static uint32_t arena_add(const char *s, size_t n)
{
    if (arena_used + n + 1 > ITAG_TEXT_CAP)
    {
        fprintf(stderr, "itemtag: the tag text arena is full (%u bytes)\n", ITAG_TEXT_CAP);
        exit(2);
    }
    uint32_t off = arena_used;
    memcpy(arena + off, s, n);
    arena[off + n] = 0;
    arena_used += (uint32_t)n + 1;
    return off;
}

/* NBTTagCompound.func_150297_b(key, type) for the types read here. */
static const nbt *key_of(const nbt *comp, const char *key, nbt_type type)
{
    const nbt *v = comp != NULL && nbt_kind(comp) == NBT_COMPOUND ? nbt_get(comp, key) : NULL;
    return v != NULL && nbt_kind(v) == type ? v : NULL;
}

/* getShort: any numeric tag, 0 when absent. */
static short get_short(const nbt *comp, const char *key)
{
    const nbt *v = nbt_kind(comp) == NBT_COMPOUND ? nbt_get(comp, key) : NULL;
    if (v == NULL) return 0;
    switch (nbt_kind(v))
    {
        case NBT_BYTE: case NBT_SHORT: case NBT_INT: case NBT_LONG: return (short)nbt_int_value(v);
        default: return 0;
    }
}

static int decode_list(const nbt *list, struct itag_ench *out)
{
    int n = 0;
    for (int i = 0; i < nbt_list_size(list); ++i)
    {
        const nbt *e = nbt_list_get(list, i);
        if (n == ITAG_MAX_ENCH)
        {
            fprintf(stderr, "itemtag: more than %d enchantments on one stack\n", ITAG_MAX_ENCH);
            exit(2);
        }
        /* getCompoundTagAt: a non-compound element reads as an empty one */
        out[n].id = nbt_kind(e) == NBT_COMPOUND ? get_short(e, "id") : 0;
        out[n].lvl = nbt_kind(e) == NBT_COMPOUND ? get_short(e, "lvl") : 0;
        ++n;
    }
    return n;
}

static void decode(struct itag *t, const nbt *tag)
{
    const nbt *ench = key_of(tag, "ench", NBT_LIST);
    const nbt *stored = key_of(tag, "StoredEnchantments", NBT_LIST);
    const nbt *display = key_of(tag, "display", NBT_COMPOUND);
    const nbt *color = key_of(display, "color", NBT_INT);
    const nbt *name = key_of(display, "Name", NBT_STRING);
    const nbt *repair = key_of(tag, "RepairCost", NBT_INT);

    t->has_ench = ench != NULL;
    t->nench = ench ? (uint8_t)decode_list(ench, t->ench) : 0;
    t->has_stored = stored != NULL;
    t->nstored = stored ? (uint8_t)decode_list(stored, t->stored) : 0;
    t->has_display = display != NULL;
    t->has_color = color != NULL;
    t->color = color ? (int)nbt_int_value(color) : 0;
    t->has_name = name != NULL;
    if (name)
    {
        const char *s = nbt_string_value(name);
        t->name_len = (uint32_t)strlen(s);
        t->name_off = arena_add(s, t->name_len);
    }
    t->repair_cost = repair ? (int)nbt_int_value(repair) : 0;
    t->unbreakable = (signed char)get_short(tag, "Unbreakable") != 0;
}

/* The index of canonical text, interning it (tree, when given, is its
 * parse; else the text is parsed here). */
static int intern(const char *text, const nbt *tree)
{
    size_t n = strlen(text);
    uint64_t h = text_hash(text, n);
    int found = lookup(text, n, h);
    if (found) return noted(found);

    nbt *own = NULL;
    if (tree == NULL)
    {
        own = nbt_parse(text);
        if (own == NULL || nbt_kind(own) != NBT_COMPOUND)
        {
            fprintf(stderr, "itemtag: not a canonical NBT compound: %s\n", text);
            exit(2);
        }
        tree = own;
    }
    if (nentries == ITAG_CAP)
    {
        fprintf(stderr, "itemtag: the tag store is full (%d distinct tags)\n", ITAG_CAP);
        exit(2);
    }

    int idx = nentries++;
    struct itag *t = &entries[idx];
    memset(t, 0, sizeof *t);
    t->len = (uint32_t)n;
    t->off = arena_add(text, n);
    t->hash = h;
    decode(t, tree);
    t->next = heads[h & (ITAG_BUCKETS - 1)];
    heads[h & (ITAG_BUCKETS - 1)] = idx + 1;
    nbt_free(own);
    return noted(idx);
}

int itag_from_text(const char *text)
{
    if (text == NULL) return 0;
    return intern(text, NULL);
}

int itag_from_tree(const nbt *tag)
{
    if (tag == NULL) return 0;
    if (nbt_kind(tag) != NBT_COMPOUND)
    {
        fprintf(stderr, "itemtag: a stack tag that is not a compound\n");
        exit(2);
    }
    char *text = nbt_render(tag);
    size_t n = strlen(text);
    int found = lookup(text, n, text_hash(text, n));
    if (!found)
    {
        /* the text must carry the tree back (a string holding a quote does
         * not); intern parses it, and refuses what does not parse */
        found = intern(text, NULL);
    }
    else noted(found);
    free(text);
    return found;
}

int itag_from_item(const nbt *item)
{
    const nbt *tag = key_of(item, "tag", NBT_COMPOUND);
    return tag ? itag_from_tree(tag) : 0;
}

const struct itag *itag_get(int t)
{
    return t > 0 && t < nentries ? &entries[t] : NULL;
}

const char *itag_text(int t)
{
    const struct itag *e = itag_get(t);
    return e ? arena + e->off : NULL;
}

nbt *itag_tree(int t)
{
    const char *text = itag_text(t);
    return text ? nbt_parse(text) : NULL;
}

void itag_put(nbt *item, int t)
{
    if (t) nbt_put(item, "tag", itag_tree(t));
}

static nbt *take_tree(const nbt *v)
{
    return (nbt *)v;
}

void itag_w(struct nbtw *w, int t)
{
    if (!t) return;
    if (!nbtw_is_bin(w))
    {
        /* the tree output keeps the parsed tree */
        nbtw_put_tree(w, "tag", itag_tree(t), take_tree);
        return;
    }
    /* the binary one writes it from the scratch region: a block freed to the
     * trees' own free lists here (the harness, outside an image's heap)
     * would come back in the tick's next tree, outside the image */
    nbt_scratch_begin();
    nbt *tree = itag_tree(t);
    if (tree != NULL) nbtw_put_tree(w, "tag", tree, take_tree);
    nbt_scratch_end();
}

int itag_ench_level(int t, int id)
{
    const struct itag *e = itag_get(t);
    if (e == NULL) return 0;
    for (int i = 0; i < e->nench; ++i)
        if (e->ench[i].id == id) return e->ench[i].lvl;
    return 0;
}

int itag_nench(int t)
{
    const struct itag *e = itag_get(t);
    return e ? e->nench : 0;
}

struct itag_ench itag_ench_at(int t, int i)
{
    return itag_get(t)->ench[i];
}

int itag_enchanted(int t)
{
    const struct itag *e = itag_get(t);
    return e != NULL && e->has_ench;
}

int itag_has_color(int t)
{
    const struct itag *e = itag_get(t);
    return e != NULL && e->has_color;
}

int itag_color(int t, int fallback)
{
    const struct itag *e = itag_get(t);
    return e != NULL && e->has_color ? e->color : fallback;
}

const char *itag_name(int t, char *buf, int n)
{
    const struct itag *e = itag_get(t);
    if (e == NULL || !e->has_name) return NULL;
    snprintf(buf, (size_t)n, "%.*s", (int)e->name_len, arena + e->name_off);
    return buf;
}

/* ---- writes: the compound as a tree, changed, interned again ---- */

static nbt *copy_tree(const nbt *v)
{
    char *text = nbt_render(v);
    nbt *out = nbt_parse(text);
    free(text);
    return out;
}

static void set_key(nbt *comp, const char *key, nbt *v)
{
    if (!nbt_replace(comp, key, v)) (nbt_put)(comp, key, v);
}

static nbt *open_tag(int t)
{
    return t ? itag_tree(t) : nbt_new_compound();
}

static int close_tag(nbt *tag)
{
    int t = itag_from_tree(tag);
    nbt_free(tag);
    return t;
}

static nbt *ench_entry(int id, int lvl)
{
    nbt *e = nbt_new_compound();
    nbt_put(e, "id", nbt_new_short((short)id));
    nbt_put(e, "lvl", nbt_new_short((short)lvl));
    return e;
}

int itag_add_ench(int t, int id, int lvl)
{
    nbt *tag = open_tag(t);
    const nbt *old = key_of(tag, "ench", NBT_LIST);
    nbt *list = old ? copy_tree(old) : nbt_new_list();
    /* getTagList("ench", 10): a list of another element type reads as a new
     * detached list, which the append does not reach */
    if (nbt_list_size(list) > 0 && nbt_kind(nbt_list_get(list, 0)) != NBT_COMPOUND)
    {
        nbt_free(list);
        nbt_free(tag);
        return t;
    }
    nbt_list_add(list, ench_entry(id, (short)(signed char)lvl));
    set_key(tag, "ench", list);
    return close_tag(tag);
}

int itag_add_stored(int t, int id, int lvl)
{
    nbt *tag = open_tag(t);
    const nbt *old = key_of(tag, "StoredEnchantments", NBT_LIST);
    nbt *list = nbt_new_list();
    int found = 0;
    for (int i = 0; old != NULL && i < nbt_list_size(old); ++i)
    {
        const nbt *e = nbt_list_get(old, i);
        if (!found && nbt_kind(e) == NBT_COMPOUND && get_short(e, "id") == (short)id)
        {
            found = 1;
            if (get_short(e, "lvl") < lvl)
            {
                nbt *c = copy_tree(e);
                set_key(c, "lvl", nbt_new_short((short)lvl));
                nbt_list_add(list, c);
                continue;
            }
        }
        nbt_list_add(list, copy_tree(e));
    }
    if (!found) nbt_list_add(list, ench_entry(id, lvl));
    set_key(tag, "StoredEnchantments", list);
    return close_tag(tag);
}

/* getCompoundTag("display") put back when it was not a compound, with key
 * set to v. */
static int display_set(int t, const char *key, nbt *v)
{
    nbt *tag = open_tag(t);
    const nbt *old = key_of(tag, "display", NBT_COMPOUND);
    nbt *display = old ? copy_tree(old) : nbt_new_compound();
    set_key(display, key, v);
    set_key(tag, "display", display);
    return close_tag(tag);
}

int itag_set_color(int t, int color)
{
    return display_set(t, "color", nbt_new_int(color));
}

int itag_set_name(int t, const char *name)
{
    return display_set(t, "Name", nbt_new_string(name));
}

int itag_remove_color(int t)
{
    const struct itag *e = itag_get(t);
    if (e == NULL || !e->has_display) return t;
    nbt *tag = itag_tree(t);
    const nbt *old = key_of(tag, "display", NBT_COMPOUND);
    if (nbt_get(old, "color") == NULL) { nbt_free(tag); return t; }
    nbt *display = nbt_new_compound();
    for (int i = 0; i < nbt_field_count(old); ++i)
        if (strcmp(nbt_field_key(old, i), "color") != 0)
            (nbt_put)(display, nbt_field_key(old, i), copy_tree(nbt_field_value(old, i)));
    set_key(tag, "display", display);
    return close_tag(tag);
}
