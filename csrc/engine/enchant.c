#include "enchant.h"

#include "trace.h"

#include <stdlib.h>
#include <string.h>

#include "enchants.h"
#include "itemtag.h"
#include "items.h"
#include "jrand.h"
#include "nbtjson.h"

#define NENCH ((int)(sizeof ENCHANTS / sizeof ENCHANTS[0]))

/* The weight of one effect id, as EnchantmentData carries it (the
 * enchantment's own weight). */
static int ENCHANTS_WEIGHT(int effect_id)
{
    for (int i = 0; i < NENCH; ++i)
        if (ENCHANTS[i].effect_id == effect_id) return ENCHANTS[i].weight;
    return 0;
}

/* ---- EnumEnchantmentType.canEnchantItem ---- */

/* p_77557_1_ is an item id from items.h. The item classes the game switches on
 * map onto item_kind: ItemArmor -> ITEM_ARMOR, ItemSword -> ITEM_SWORD,
 * ItemTool -> ITEM_TOOL, ItemBow and ItemFishingRod are plain items with
 * distinct names. */
static int can_enchant_item(int type, int item_id)
{
    const struct item_def *it = &ITEMS[item_id];

    if (type == E_ALL) return 1;
    if (type == E_BREAKABLE && it->max_damage > 0) return 1;

    if (it->kind == ITEM_ARMOR)
    {
        if (type == E_ARMOR) return 1;
        /* armorType: 0 helmet, 1 chest, 2 legs, 3 boots */
        if (it->armor_type == 0) return type == E_ARMOR_HEAD;
        if (it->armor_type == 2) return type == E_ARMOR_LEGS;
        if (it->armor_type == 1) return type == E_ARMOR_TORSO;
        if (it->armor_type == 3) return type == E_ARMOR_FEET;
        return 0;
    }
    if (it->kind == ITEM_SWORD) return type == E_WEAPON;
    if (it->kind == ITEM_TOOL) return type == E_DIGGER;
    if (!strcmp(it->class_name, "ItemBow")) return type == E_BOW;
    if (!strcmp(it->class_name, "ItemFishingRod")) return type == E_FISHING_ROD;
    return 0;
}

/* ---- the Java HashMap mapEnchantmentData builds ----
 *
 * java.util.HashMap (Java 8, what the oracle runs): table of power-of-two size,
 * default capacity 16, load factor 0.75, so the resize threshold is
 * capacity - capacity/4; a new key lands in bucket (h ^ (h >>> 16)) & (n - 1)
 * with the node appended at the tail of that bucket's chain. Iteration is
 * buckets ascending, chain order inside a bucket. Keys are the effect ids (all
 * under 256), so their hash is the value itself and the spread keeps the low
 * bits. */
#define HM_MIN 16
/* at most one node per enchantment (the keys are distinct effect ids), so the
 * table stops at 64 buckets: fixed arrays in the map, no allocation */
#define HM_MAX 64

struct hm_entry {
    int key;
    struct enchant_data data;
    int next;   /* index of the next node in this bucket, -1 for the end */
};

struct hashmap {
    int buckets[HM_MAX];
    int n;             /* table size, a power of two */
    int size;
    int threshold;
    struct hm_entry entries[HM_MAX];
    int nentries;
};

static void hm_resize(struct hashmap *m);

static void hm_init(struct hashmap *m)
{
    m->n = HM_MIN;
    for (int i = 0; i < m->n; ++i) m->buckets[i] = -1;
    m->size = 0;
    m->threshold = HM_MIN - HM_MIN / 4;
    m->nentries = 0;
}

static int hm_bucket(const struct hashmap *m, int key)
{
    unsigned h = (unsigned)key;
    h ^= h >> 16;
    return (int)(h & (unsigned)(m->n - 1));
}

/* Append a node to the tail of its bucket's chain, the Java 8 behavior. */
static void hm_put(struct hashmap *m, int key, struct enchant_data data)
{
    /* a repeated key replaces the value in place, as HashMap.put does */
    for (int i = 0; i < m->nentries; ++i)
    {
        if (m->entries[i].key == key)
        {
            m->entries[i].data = data;
            return;
        }
    }

    if (m->size + 1 > m->threshold) hm_resize(m);

    if (m->nentries == HM_MAX) abort();

    int idx = m->nentries++;
    m->entries[idx].key = key;
    m->entries[idx].data = data;
    m->entries[idx].next = -1;

    int b = hm_bucket(m, key);
    if (m->buckets[b] < 0)
    {
        m->buckets[b] = idx;
    }
    else
    {
        int t = m->buckets[b];
        while (m->entries[t].next >= 0) t = m->entries[t].next;
        m->entries[t].next = idx;
    }
    ++m->size;
}

/* Java 8 resize: doubles the table, then re-links every node by its (new)
 * bucket, keeping the chain order of the old table (the low/high split walks
 * each old bucket's chain in order). */
static void hm_resize(struct hashmap *m)
{
    int old_n = m->n;
    int old[HM_MAX];
    if (old_n * 2 > HM_MAX) abort();
    memcpy(old, m->buckets, sizeof(int) * (size_t)old_n);
    m->n = old_n * 2;
    m->threshold = m->n - m->n / 4;
    for (int i = 0; i < m->n; ++i) m->buckets[i] = -1;

    /* walk the old table bucket by bucket, chain in order; each node keeps its
     * relative order inside its new bucket (the tail append below does that) */
    for (int b = 0; b < old_n; ++b)
    {
        for (int i = old[b]; i >= 0; )
        {
            int nxt = m->entries[i].next;   /* read before the chain is rewritten */
            int nb = hm_bucket(m, m->entries[i].key);
            m->entries[i].next = -1;
            if (m->buckets[nb] < 0)
            {
                m->buckets[nb] = i;
            }
            else
            {
                int t = m->buckets[nb];
                while (m->entries[t].next >= 0) t = m->entries[t].next;
                m->entries[t].next = i;
            }
            i = nxt;
        }
    }
}

/* The map's iteration order: buckets ascending, chain order inside a bucket. */
static void hm_order(const struct hashmap *m, int *out, int *n)
{
    int k = 0;
    for (int b = 0; b < m->n; ++b)
    {
        for (int i = m->buckets[b]; i >= 0; i = m->entries[i].next) out[k++] = i;
    }
    *n = k;
}

/* ---- the enchantment list ---- */

/* mapEnchantmentData: every (enchantment, level) whose window contains the
 * level, keyed by effect id (a later level of the same id replaces the
 * earlier), in the HashMap above. Returns the iteration order in `order`. */
static void map_enchantment_data(struct hashmap *m, int level, int item_id)
{
    int is_book = (item_id == 340);   /* Items.book */

    hm_init(m);
    for (int i = 0; i < NENCH; ++i)
    {
        const struct enchantment_def *e = &ENCHANTS[i];
        if (!(can_enchant_item(e->type, item_id) || is_book)) continue;

        for (int lv = e->min_level; lv <= e->max_level; ++lv)
        {
            if (level >= e->min_enchantability[lv - 1] && level <= e->max_enchantability[lv - 1])
            {
                struct enchant_data d;
                d.effect_id = e->effect_id;
                d.level = lv;
                hm_put(m, e->effect_id, d);
            }
        }
    }
}

int can_apply_together(int effect_id, int other_effect_id)
{
    int i, j;
    for (i = 0; i < NENCH; ++i) if (ENCHANTS[i].effect_id == effect_id) break;
    for (j = 0; j < NENCH; ++j) if (ENCHANTS[j].effect_id == other_effect_id) break;
    if (i == NENCH || j == NENCH) return 0;
    return (CAN_APPLY_TOGETHER[i] >> j) & 1u;
}

/* WeightedRandom.getRandomItem(Random, Collection) over the values: draw
 * nextInt(totalWeight), then walk the collection subtracting each weight until
 * the running value goes negative. */
static int pick_weighted(jrand *rand, const struct hashmap *m, const int *order, int n)
{
    int total = 0;
    for (int i = 0; i < n; ++i) total += ENCHANTS_WEIGHT(m->entries[order[i]].data.effect_id);
    int k = jr_int_n(rand, total);
    for (int i = 0; i < n; ++i)
    {
        k -= ENCHANTS_WEIGHT(m->entries[order[i]].data.effect_id);
        if (k < 0) return order[i];
    }
    return -1;
}

/* Item.getItemEnchantability: ItemTool and ItemSword read their tool material,
 * ItemArmor reads its ArmorMaterial (not in items.h, so the five armor sets
 * are listed per item id), ItemBook is fixed at 1, everything else 0. */
static int item_enchantability(int item_id)
{
    const struct item_def *it = &ITEMS[item_id];

    if (it->kind == ITEM_SWORD || it->kind == ITEM_TOOL)
        return TOOL_MATERIALS[it->tool_material].enchantability;

    switch (item_id)
    {
        case 261: return 1;                                  /* bow */
        case 298: case 299: case 300: case 301: return 15;  /* leather */
        case 302: case 303: case 304: case 305: return 12;  /* chain */
        case 306: case 307: case 308: case 309: return 9;   /* iron */
        case 310: case 311: case 312: case 313: return 10;  /* diamond */
        case 314: case 315: case 316: case 317: return 25;  /* gold */
        case 340: return BOOK_ENCHANTABILITY;               /* book */
        default: return 0;
    }
}

int build_enchantment_list(jrand *rand, int item, int level,
                           struct enchant_data *out, int max)
{
    /* buildEnchantmentList: the item's enchantability halves, then two draws.
     * `level` is addRandomEnchantment's third argument, the level budget; the
     * item's own enchantability comes from the item. */
    int e = item_enchantability(item);
    if (e <= 0) return 0;

    e /= 2;
    e = 1 + jr_int_n(rand, (e >> 1) + 1) + jr_int_n(rand, (e >> 1) + 1);
    int budget = e + level;

    float f = (jr_float(rand) + jr_float(rand) - 1.0F) * 0.15F;
    int lvl = (int)((float)budget * (1.0F + f) + 0.5F);
    if (lvl < 1) lvl = 1;

    struct hashmap m;
    map_enchantment_data(&m, lvl, item);
    int order[64], n = 0;
    hm_order(&m, order, &n);

    int count = 0;
    if (n > 0)
    {
        int first = pick_weighted(rand, &m, order, n);

        if (first >= 0)
        {
            out[count++] = m.entries[first].data;

            /* the repeat loop: while nextInt(50) <= lvl, drop every candidate
             * that the newest pick conflicts with, then pick again */
            for (int var11 = lvl; jr_int_n(rand, 50) <= var11; var11 >>= 1)
            {
                int live[64], ln = 0;
                for (int i = 0; i < n; ++i)
                {
                    int idx = order[i];
                    int id = m.entries[idx].data.effect_id;
                    int keep = 1;
                    for (int j = 0; j < count; ++j)
                    {
                        /* existing.canApplyTogether(candidate) */
                        if (!can_apply_together(out[j].effect_id, id))
                        {
                            keep = 0;
                            break;
                        }
                    }
                    if (keep) live[ln++] = idx;
                }

                if (ln > 0 && count < max)
                {
                    /* WeightedRandom over the surviving values, in map order */
                    int total = 0;
                    for (int i = 0; i < ln; ++i) total += ENCHANTS_WEIGHT(m.entries[live[i]].data.effect_id);
                    int k = jr_int_n(rand, total);
                    int pick = -1;
                    for (int i = 0; i < ln; ++i)
                    {
                        k -= ENCHANTS_WEIGHT(m.entries[live[i]].data.effect_id);
                        if (k < 0) { pick = live[i]; break; }
                    }
                    if (pick >= 0) out[count++] = m.entries[pick].data;
                }
            }
        }
    }

    return count;
}

/* ItemEnchantedBook.addEnchantment into a fresh NBTTagList: one compound per
 * distinct id, a repeat raising its level in place, in first-pick order. */
static nbt *book_tag(const struct enchant_data *list, int n)
{
    nbt *stored = nbt_new_list();
    for (int i = 0; i < n; ++i)
    {
        int found = 0;
        for (int k = 0; k < i; ++k)
        {
            if (list[k].effect_id == list[i].effect_id) { found = 1; break; }
        }
        if (found) continue;

        int lv = list[i].level;
        for (int k = i + 1; k < n; ++k)
            if (list[k].effect_id == list[i].effect_id && list[k].level > lv) lv = list[k].level;

        nbt *c = nbt_new_compound();
        nbt_put(c, "id", nbt_new_short(list[i].effect_id));
        nbt_put(c, "lvl", nbt_new_short(lv));
        nbt_list_add(stored, c);
    }

    nbt *tag = nbt_new_compound();
    nbt_put(tag, "StoredEnchantments", stored);
    return tag;
}

/* The whole enchanted book stack as canonical NBT: ItemStack.writeToNBT of
 * Items.enchanted_book with the tag above. */
char *add_random_enchantment_book(jrand *rand, int enchantability)
{
    struct enchant_data list[64];
    int n = build_enchantment_list(rand, 340, enchantability, list, 64);
    nbt *root = nbt_new_compound();
    nbt_put(root, "id", nbt_new_short(403));      /* enchanted_book */
    nbt_put(root, "Count", nbt_new_byte(1));
    nbt_put(root, "Damage", nbt_new_short(0));
    nbt_put(root, "tag", book_tag(list, n));
    char *s = nbt_render(root);
    nbt_free(root);
    return s;
}

/* The tag compound of the same stack, which is what a chest slot carries. */
int enchant_list_tag(int tag, const struct enchant_data *list, int n)
{
    for (int i = 0; i < n; ++i) tag = itag_add_ench(tag, list[i].effect_id, list[i].level);
    return tag;
}

char *add_random_enchantment_tag(jrand *rand, int enchantability)
{
    struct enchant_data list[64];
    int n = build_enchantment_list(rand, 340, enchantability, list, 64);
    nbt *tag = book_tag(list, n);
    char *s = nbt_render(tag);
    nbt_free(tag);
    return s;
}