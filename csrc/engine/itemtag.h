/* ItemStack.stackTagCompound, the one tag representation every native stack
 * carries (the player's inventory, containers, crafting, trades, loot, item
 * entities, mob equipment, item frames, packets and snapshots).
 *
 * A stack holds an int: 0 is Java's null tag (hasTagCompound false), any
 * other value an index into one fixed-capacity store of interned compounds.
 * Interning is by the compound's canonical NBT text (nbtjson.h: keys sorted),
 * so two tags are NBTTagCompound.equals exactly when their indices are equal,
 * and ItemStack.areItemStackTagsEqual is an int compare. Entries are never
 * mutated: a write (addEnchantment, a dye, a rename) interns the new compound
 * and the stack takes its index, which is also what ItemStack.copy's tag copy
 * amounts to (two Java copies never share a mutable compound).
 *
 * The store is flat (an entry array, a text arena, a hash index), with no
 * per-stack allocation, so a GPU env can mirror it as three buffers; each
 * entry also carries the keys the game reads decoded (the ench and
 * StoredEnchantments lists, display's color and Name, RepairCost). Nothing
 * is ever freed: a survival session reaches a few distinct tags per enchanted
 * item, dyed piece or book, far under the capacity, and running out stops the
 * process with a message rather than dropping a tag.
 *
 * Writers into an entity's or a player's NBT rebuild the compound from the
 * text; the reached keys ("ench" "display" "StoredEnchantments" "RepairCost",
 * display's "color" "Name" "Lore", an ench entry's "id" "lvl") sit in
 * distinct HashMap buckets, so the sorted insertion order the text gives
 * iterates as Java's own. */
#ifndef NETHERITE_ITEMTAG_H
#define NETHERITE_ITEMTAG_H

#include <stdint.h>

#include "nbtjson.h"

#define ITAG_CAP 16384
#define ITAG_TEXT_CAP (4u << 20)
#define ITAG_MAX_ENCH 12

struct itag_ench { short id, lvl; };

struct itag {
    uint32_t off, len;              /* the canonical text in the arena */
    uint64_t hash;
    int next;                       /* the hash chain */
    /* the "ench" list (getEnchantmentTagList): has_ench when the key holds
     * a list (isItemEnchanted), its entries' id and lvl as getShort reads
     * them */
    uint8_t has_ench, nench;
    struct itag_ench ench[ITAG_MAX_ENCH];
    /* ItemEnchantedBook.func_92110_g's "StoredEnchantments" list */
    uint8_t has_stored, nstored;
    struct itag_ench stored[ITAG_MAX_ENCH];
    /* hasKey("display", 10); its "color" int (hasKey 3) and "Name" string
     * (hasKey 8, name_off into the arena, name_len bytes) */
    uint8_t has_display, has_color, has_name;
    int color;
    uint32_t name_off, name_len;
    /* getRepairCost: "RepairCost" as an int, 0 when absent */
    int repair_cost;
    /* getBoolean("Unbreakable"): any numeric tag, nonzero */
    uint8_t unbreakable;
};

/* The interned index of a compound (NULL: 0), or of its canonical text. A
 * text that is not canonical NBT, or a tree whose text does not parse back
 * (a string holding a quote), stops the process. */
int itag_from_tree(const nbt *tag);
int itag_from_text(const char *text);

/* The item compound's "tag" key (ItemStack.readFromNBT's hasKey("tag", 10)),
 * 0 when it has none. */
int itag_from_item(const nbt *item);

/* The entry, NULL for 0. */
const struct itag *itag_get(int t);

/* The canonical text, NULL for 0; it lives as long as the process. */
const char *itag_text(int t);

/* A new tree of the compound (the caller owns it; in an nbt scratch build it
 * comes from the scratch), NULL for 0. */
nbt *itag_tree(int t);

/* ItemStack.writeToNBT's tail: item["tag"] = the compound, when there is one. */
void itag_put(nbt *item, int t);
/* The same through a writer (nbtw.h): the "tag" field of the open compound. */
struct nbtw;
void itag_w(struct nbtw *w, int t);

/* EnchantmentHelper.getEnchantmentLevel over the "ench" list: the first
 * entry with the id, 0 for none. */
int itag_ench_level(int t, int id);
int itag_nench(int t);
struct itag_ench itag_ench_at(int t, int i);
/* ItemStack.isItemEnchanted: hasKey("ench", 9). */
int itag_enchanted(int t);

/* ItemArmor.hasColor's tag half and getColor's value (default when there is
 * no color). */
int itag_has_color(int t);
int itag_color(int t, int fallback);
/* ItemStack.hasDisplayName's Name, NULL when there is none (a copy into buf,
 * n bytes, NUL-terminated). */
const char *itag_name(int t, char *buf, int n);

/* The writes, each returning the new stack's index. */
/* ItemStack.addEnchantment: the ench list appended with {id: short, lvl:
 * short((byte)lvl)}, created (and the tag) when missing. */
int itag_add_ench(int t, int id, int lvl);
/* ItemEnchantedBook.addEnchantment: the StoredEnchantments entry, a higher
 * level replacing an equal id's in place, else appended. */
int itag_add_stored(int t, int id, int lvl);
/* ItemArmor.func_82813_b: tag and display created when missing,
 * display.color set. */
int itag_set_color(int t, int color);
/* ItemArmor.removeColor: display's color key removed (the compound, empty or
 * not, stays); a tag without a display compound is unchanged. */
int itag_remove_color(int t);
/* ItemStack.setStackDisplayName: tag and display created when missing,
 * display.Name set. */
int itag_set_name(int t, const char *name);

#endif
