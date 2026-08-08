// Verbatim MC 1.11.2 enchant-table offer RNG (vanilla ground truth, eval-pure).
// Sources (decompiled oracle, logic copied into this standalone driver):
//   net/minecraft/inventory/ContainerEnchantment.java
//     onCraftMatrixChanged (level roll + clue), getEnchantmentList
//   net/minecraft/enchantment/EnchantmentHelper.java
//     calcItemStackEnchantability, buildEnchantmentList, getEnchantmentDatas, removeIncompatible
//   net/minecraft/enchantment/Enchantment*.java (min/max/level/rarity/treasure/compat)
//   net/minecraft/util/WeightedRandom.java
//   net/minecraft/util/math/MathHelper.clamp + Java Math.round(float)
//
// Embedded enchantment registry = vanilla registerEnchantments order. Item kinds:
//   book (ench=1), diamond sword (10), iron pickaxe (14). Bookshelves power is an input
//   (0/5/15); Forge hooks are identity. allowTreasure=false. Output: %08x per field.

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Random;

public class Golden {
    static final int ITEM_BOOK = 0, ITEM_DIAMOND_SWORD = 1, ITEM_IRON_PICK = 2, N_ITEMS = 3;
    static final int MAX_LIST = 16;
    static final int N_SEEDS = 7, N_SHELVES = 3;
    static final int[] SEEDS = { 0, 1, 42, 12345, 0x12345678, -1, 999999 };
    static final int[] SHELVES = { 0, 5, 15 };

    // type bits
    static final int T_ARMOR = 1, T_ARMOR_FEET = 2, T_ARMOR_LEGS = 4, T_ARMOR_CHEST = 8,
        T_ARMOR_HEAD = 16, T_WEAPON = 32, T_DIGGER = 64, T_FISHING = 128, T_BREAKABLE = 256,
        T_BOW = 512, T_WEARABLE = 1024, T_ALL = 2048;
    static final int CAP_SWORD = T_WEAPON | T_BREAKABLE;
    static final int CAP_PICK = T_DIGGER | T_BREAKABLE;

    static final int G_NONE = 0, G_DAMAGE = 1, G_PROT = 2, G_SILK = 3, G_LOOTBONUS = 5,
        G_INFINITY = 6, G_MENDING = 7, G_FROST = 8, G_DEPTH = 9;
    static final int PROT_ALL = 0, PROT_FIRE = 1, PROT_FALL = 2, PROT_BLAST = 3, PROT_PROJ = 4;

    static final class Def {
        final int id, weight, maxLevel, treasure, typeBits, group, protType;
        Def(int id, int w, int maxL, int treas, int type, int group, int prot) {
            this.id = id; this.weight = w; this.maxLevel = maxL; this.treasure = treas;
            this.typeBits = type; this.group = group; this.protType = prot;
        }
    }

    static final Def[] DEFS = {
        new Def(0, 10, 4, 0, T_ARMOR, G_PROT, PROT_ALL),
        new Def(1, 5, 4, 0, T_ARMOR, G_PROT, PROT_FIRE),
        new Def(2, 5, 4, 0, T_ARMOR_FEET, G_PROT, PROT_FALL),
        new Def(3, 2, 4, 0, T_ARMOR, G_PROT, PROT_BLAST),
        new Def(4, 5, 4, 0, T_ARMOR, G_PROT, PROT_PROJ),
        new Def(5, 2, 3, 0, T_ARMOR_HEAD, G_NONE, -1),
        new Def(6, 2, 1, 0, T_ARMOR_HEAD, G_NONE, -1),
        new Def(7, 1, 3, 0, T_ARMOR_CHEST, G_NONE, -1),
        new Def(8, 2, 3, 0, T_ARMOR_FEET, G_DEPTH, -1),
        new Def(9, 2, 2, 1, T_ARMOR_FEET, G_FROST, -1),
        new Def(10, 1, 1, 1, T_WEARABLE, G_NONE, -1),
        new Def(16, 10, 5, 0, T_WEAPON, G_DAMAGE, -1),
        new Def(17, 5, 5, 0, T_WEAPON, G_DAMAGE, -1),
        new Def(18, 5, 5, 0, T_WEAPON, G_DAMAGE, -1),
        new Def(19, 5, 2, 0, T_WEAPON, G_NONE, -1),
        new Def(20, 2, 2, 0, T_WEAPON, G_NONE, -1),
        new Def(21, 2, 3, 0, T_WEAPON, G_LOOTBONUS, -1),
        new Def(22, 2, 3, 0, T_WEAPON, G_NONE, -1),
        new Def(32, 10, 5, 0, T_DIGGER, G_NONE, -1),
        new Def(33, 1, 1, 0, T_DIGGER, G_SILK, -1),
        new Def(34, 5, 3, 0, T_BREAKABLE, G_NONE, -1),
        new Def(35, 2, 3, 0, T_DIGGER, G_LOOTBONUS, -1),
        new Def(48, 10, 5, 0, T_BOW, G_NONE, -1),
        new Def(49, 2, 2, 0, T_BOW, G_NONE, -1),
        new Def(50, 2, 1, 0, T_BOW, G_NONE, -1),
        new Def(51, 1, 1, 0, T_BOW, G_INFINITY, -1),
        new Def(61, 2, 3, 0, T_FISHING, G_LOOTBONUS, -1),
        new Def(62, 2, 3, 0, T_FISHING, G_NONE, -1),
        new Def(70, 2, 1, 1, T_BREAKABLE, G_MENDING, -1),
        new Def(71, 1, 1, 1, T_ALL, G_NONE, -1),
    };

    static Def findDef(int id) {
        for (Def d : DEFS) if (d.id == id) return d;
        return null;
    }

    static int baseMin(int level) { return 1 + level * 10; }

    static int minEnch(int id, int level) {
        switch (id) {
        case 0: return 1 + (level - 1) * 11;
        case 1: return 10 + (level - 1) * 8;
        case 2: return 5 + (level - 1) * 6;
        case 3: return 5 + (level - 1) * 8;
        case 4: return 3 + (level - 1) * 6;
        case 5: return 10 * level;
        case 6: return 1;
        case 7: return 10 + 20 * (level - 1);
        case 8: return level * 10;
        case 9: return level * 10;
        case 10: return 25;
        case 16: return 1 + (level - 1) * 11;
        case 17: return 5 + (level - 1) * 8;
        case 18: return 5 + (level - 1) * 8;
        case 19: return 5 + 20 * (level - 1);
        case 20: return 10 + 20 * (level - 1);
        case 21: return 15 + (level - 1) * 9;
        case 22: return 5 + (level - 1) * 9;
        case 32: return 1 + 10 * (level - 1);
        case 33: return 15;
        case 34: return 5 + (level - 1) * 8;
        case 35: return 15 + (level - 1) * 9;
        case 48: return 1 + (level - 1) * 10;
        case 49: return 12 + (level - 1) * 20;
        case 50: return 20;
        case 51: return 20;
        case 61: return 15 + (level - 1) * 9;
        case 62: return 15 + (level - 1) * 9;
        case 70: return level * 25;
        case 71: return 25;
        default: return 1 + level * 10;
        }
    }

    static int maxEnch(int id, int level) {
        switch (id) {
        case 0: return minEnch(id, level) + 11;
        case 1: return minEnch(id, level) + 8;
        case 2: return minEnch(id, level) + 6;
        case 3: return minEnch(id, level) + 8;
        case 4: return minEnch(id, level) + 6;
        case 5: return minEnch(id, level) + 30;
        case 6: return minEnch(id, level) + 40;
        case 7: case 19: case 20: case 21: case 32: case 33: case 34: case 35:
        case 61: case 62:
            return baseMin(level) + 50;
        case 8: return minEnch(id, level) + 15;
        case 9: return minEnch(id, level) + 15;
        case 10: return 50;
        case 16: case 17: case 18:
            return minEnch(id, level) + 20;
        case 22: return minEnch(id, level) + 15;
        case 48: return minEnch(id, level) + 15;
        case 49: return minEnch(id, level) + 25;
        case 50: return 50;
        case 51: return 50;
        case 70: return minEnch(id, level) + 50;
        case 71: return 50;
        default: return minEnch(id, level) + 5;
        }
    }

    static int itemEnchantability(int kind) {
        if (kind == ITEM_BOOK) return 1;
        if (kind == ITEM_DIAMOND_SWORD) return 10;
        if (kind == ITEM_IRON_PICK) return 14;
        return 0;
    }

    static int itemCaps(int kind) {
        if (kind == ITEM_DIAMOND_SWORD) return CAP_SWORD;
        if (kind == ITEM_IRON_PICK) return CAP_PICK;
        return 0;
    }

    static boolean typeMatches(int typeBits, int caps) {
        if ((typeBits & T_ALL) != 0) return caps != 0;
        return (typeBits & caps) != 0;
    }

    static boolean canApplyAtTable(Def d, int itemKind) {
        if (itemKind == ITEM_BOOK) return true;
        return typeMatches(d.typeBits, itemCaps(itemKind));
    }

    static boolean canApplyTogether(Def a, Def b) {
        if (a.id == b.id) return false;
        if (a.group == G_PROT && b.group == G_PROT) {
            if (a.protType == b.protType) return false;
            if (a.protType == PROT_FALL || b.protType == PROT_FALL) return true;
            return false;
        }
        if (a.group == G_DAMAGE && b.group == G_DAMAGE) return false;
        if ((a.group == G_SILK && b.group == G_LOOTBONUS) ||
            (b.group == G_SILK && a.group == G_LOOTBONUS)) return false;
        if ((a.group == G_INFINITY && b.group == G_MENDING) ||
            (b.group == G_INFINITY && a.group == G_MENDING)) return false;
        if ((a.group == G_FROST && b.group == G_DEPTH) ||
            (b.group == G_FROST && a.group == G_DEPTH)) return false;
        return true;
    }

    static int clamp(int num, int min, int max) {
        return num < min ? min : (num > max ? max : num);
    }

    static final class EnchData {
        final int id, level, weight;
        EnchData(int id, int level, int weight) {
            this.id = id; this.level = level; this.weight = weight;
        }
    }

    // calcItemStackEnchantability VERBATIM
    static int calcLevel(Random rand, int enchantNum, int power, int itemKind) {
        int i = itemEnchantability(itemKind);
        if (i <= 0) return 0;
        if (power > 15) power = 15;
        int j = rand.nextInt(8) + 1 + (power >> 1) + rand.nextInt(power + 1);
        return enchantNum == 0 ? Math.max(j / 3, 1)
             : (enchantNum == 1 ? j * 2 / 3 + 1 : Math.max(j, power * 2));
    }

    static List<EnchData> getEnchantmentDatas(int cost, int itemKind, boolean allowTreasure) {
        List<EnchData> list = new ArrayList<EnchData>();
        for (Def d : DEFS) {
            if (d.treasure != 0 && !allowTreasure) continue;
            if (!canApplyAtTable(d, itemKind)) continue;
            for (int lvl = d.maxLevel; lvl >= 1; --lvl) {
                if (cost >= minEnch(d.id, lvl) && cost <= maxEnch(d.id, lvl)) {
                    list.add(new EnchData(d.id, lvl, d.weight));
                    break;
                }
            }
        }
        return list;
    }

    static int totalWeight(List<EnchData> list) {
        int i = 0;
        for (EnchData e : list) i += e.weight;
        return i;
    }

    static EnchData getRandomItem(Random random, List<EnchData> list) {
        int total = totalWeight(list);
        if (total <= 0) throw new IllegalArgumentException();
        int w = random.nextInt(total);
        for (EnchData e : list) {
            w -= e.weight;
            if (w < 0) return e;
        }
        return null;
    }

    static void removeIncompatible(List<EnchData> list, EnchData chosen) {
        Def a = findDef(chosen.id);
        Iterator<EnchData> it = list.iterator();
        while (it.hasNext()) {
            Def b = findDef(it.next().id);
            if (a == null || b == null || !canApplyTogether(a, b)) it.remove();
        }
    }

    // buildEnchantmentList VERBATIM
    static List<EnchData> buildEnchantmentList(Random randomIn, int itemKind, int level,
                                                 boolean allowTreasure) {
        List<EnchData> list = new ArrayList<EnchData>();
        int i = itemEnchantability(itemKind);
        if (i <= 0) return list;
        int cost = level + 1 + randomIn.nextInt(i / 4 + 1) + randomIn.nextInt(i / 4 + 1);
        float f = (randomIn.nextFloat() + randomIn.nextFloat() - 1.0F) * 0.15F;
        cost = clamp(Math.round((float)cost + (float)cost * f), 1, Integer.MAX_VALUE);
        List<EnchData> list1 = getEnchantmentDatas(cost, itemKind, allowTreasure);
        if (!list1.isEmpty()) {
            list.add(getRandomItem(randomIn, list1));
            while (randomIn.nextInt(50) <= cost) {
                removeIncompatible(list1, list.get(list.size() - 1));
                if (list1.isEmpty()) break;
                list.add(getRandomItem(randomIn, list1));
                cost /= 2;
            }
        }
        return list;
    }

    // getEnchantmentList VERBATIM
    static List<EnchData> getEnchantmentList(Random rand, int xpSeed, int slot, int level,
                                               int itemKind) {
        rand.setSeed((long)(xpSeed + slot));
        List<EnchData> list = buildEnchantmentList(rand, itemKind, level, false);
        if (itemKind == ITEM_BOOK && list.size() > 1) {
            list.remove(rand.nextInt(list.size()));
        }
        return list;
    }

    static void emit(int v) {
        System.out.printf("%08x\n", v);
    }

    static void runOne(int xpSeed, int power, int itemKind) {
        Random rand = new Random();
        int[] levels = new int[3];
        int[] clueId = new int[] { -1, -1, -1 };
        int[] clueLvl = new int[] { -1, -1, -1 };
        @SuppressWarnings("unchecked")
        List<EnchData>[] lists = (List<EnchData>[]) new List[3];

        rand.setSeed((long)xpSeed);
        for (int i1 = 0; i1 < 3; ++i1) {
            levels[i1] = calcLevel(rand, i1, power, itemKind);
            if (levels[i1] < i1 + 1) levels[i1] = 0;
        }
        for (int j1 = 0; j1 < 3; ++j1) {
            lists[j1] = new ArrayList<EnchData>();
            if (levels[j1] > 0) {
                List<EnchData> list = getEnchantmentList(rand, xpSeed, j1, levels[j1], itemKind);
                lists[j1] = list;
                if (list != null && !list.isEmpty()) {
                    EnchData ed = list.get(rand.nextInt(list.size()));
                    clueId[j1] = ed.id;
                    clueLvl[j1] = ed.level;
                }
            }
        }

        for (int s = 0; s < 3; ++s) emit(levels[s]);
        for (int s = 0; s < 3; ++s) {
            List<EnchData> list = lists[s];
            int n = list.size();
            emit(n);
            for (int i = 0; i < MAX_LIST; ++i) {
                if (i < n) {
                    emit(list.get(i).id);
                    emit(list.get(i).level);
                } else {
                    emit(0);
                    emit(0);
                }
            }
            emit(clueId[s]);
            emit(clueLvl[s]);
        }
    }

    public static void main(String[] args) {
        for (int si = 0; si < N_SEEDS; ++si)
            for (int sh = 0; sh < N_SHELVES; ++sh)
                for (int it = 0; it < N_ITEMS; ++it)
                    runOne(SEEDS[si], SHELVES[sh], it);
    }
}
