// Verbatim MC 1.11.2 LootTable.fillInventory materialization ground truth.
// Eval-pure hand-port of decompiled oracle-src (not a live Mojang process).
//
// Logic from:
//   net/minecraft/world/storage/loot/LootTable.java  fillInventory / shuffleItems
//   net/minecraft/util/math/MathHelper.java           getInt(Random,min,max)
//   java.util.Collections.shuffle via Random.nextInt Fisher-Yates
//   java.util.Random (48-bit LCG) seeded with structure loot nextLong
//
// Fixed pre-rolled stacks (including multi-enchant books) are placed into a 27-slot
// chest. StoredEnchantments (id/level pairs) must survive splitStack + slot assignment.
// Vanilla order: getEmptySlotsRandomized (shuffle empties) THEN shuffleItems.
//
// CUT / OPEN: world-layout seed parity (C sh_place_blocks vs Java
// StructureStrongholdPieces nextLong capture); full generateLootForPools of the
// embedded stronghold JSON tables with EnchantWithLevels (covered separately by
// loot_table + enchant_table goldens; C integration in magma test_chest_loot).
//
// Output: 3 seeds * 2 stack-sets * (27 slots * 8 fields + nonempty) as %08x.
// Per slot fields: item, count, meta, n_enchants, e0id, e0lvl, e1id, e1lvl.

import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Random;

public class Golden {
    static final int SLOTS = 27, MAX_ENCHANTS = 8, MAX_STACKS = 64;
    static final int[] SEEDS = { 0, 42, 12345 };
    static final int APPLE = 260, BREAD = 297, IRON = 265, COAL = 263, DIAMOND = 264,
        BOOK = 340, ENCHANTED_BOOK = 403, PAPER = 339, COMPASS = 345;

    static class Stack {
        int item, count, meta, nEnchants;
        int[] enchId = new int[MAX_ENCHANTS];
        int[] enchLvl = new int[MAX_ENCHANTS];
        Stack(int i, int c, int m) { item = i; count = c; meta = m; nEnchants = 0; }
        Stack copy() {
            Stack s = new Stack(item, count, meta);
            s.nEnchants = nEnchants;
            for (int e = 0; e < nEnchants; ++e) {
                s.enchId[e] = enchId[e]; s.enchLvl[e] = enchLvl[e];
            }
            return s;
        }
        boolean isEmpty() { return item == 0 || count <= 0; }
        /* ItemStack.splitStack: copy (incl. tags) then shrink. */
        Stack splitStack(int amount) {
            int take = Math.min(amount, count);
            Stack out = copy();
            out.count = take;
            count -= take;
            if (count <= 0) { item = 0; count = 0; meta = 0; nEnchants = 0; }
            return out;
        }
    }
    static Stack empty() { return new Stack(0, 0, 0); }
    static Stack mk(int i, int c, int m) { return new Stack(i, c, m); }
    static Stack mkBookMulti() {
        Stack s = mk(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 2;
        s.enchId[0] = 16; s.enchLvl[0] = 3;
        s.enchId[1] = 34; s.enchLvl[1] = 1;
        return s;
    }
    static Stack mkBookSharp5() {
        Stack s = mk(ENCHANTED_BOOK, 1, 0);
        s.nEnchants = 1;
        s.enchId[0] = 16; s.enchLvl[0] = 5;
        return s;
    }

    static Stack[] stackSet(int set) {
        if (set == 0) {
            return new Stack[] {
                mk(APPLE, 20, 0), mk(BREAD, 5, 0), mk(IRON, 3, 0),
                mk(COAL, 12, 0), mk(DIAMOND, 1, 0)
            };
        }
        return new Stack[] {
            mkBookMulti(), mkBookSharp5(), mkBookMulti(),
            mk(BOOK, 2, 0), mk(PAPER, 7, 0), mk(COMPASS, 1, 0)
        };
    }

    /* MathHelper.getInt(Random, min, max) VERBATIM. */
    static int mathGetInt(Random r, int minimum, int maximum) {
        return minimum >= maximum ? minimum : r.nextInt(maximum - minimum + 1) + minimum;
    }

    /* LootTable.shuffleItems VERBATIM (1.11.2). freeSlots is a gate only. */
    static void shuffleItems(List<Stack> stacks, int freeSlots, Random rand) {
        List<Stack> multi = new ArrayList<Stack>();
        Iterator<Stack> it = stacks.iterator();
        while (it.hasNext()) {
            Stack s = it.next();
            if (s.isEmpty()) {
                it.remove();
            } else if (s.count > 1) {
                multi.add(s);
                it.remove();
            }
        }
        freeSlots = freeSlots - stacks.size();
        while (freeSlots > 0 && multi.size() > 0) {
            Stack item2 = multi.remove(mathGetInt(rand, 0, multi.size() - 1));
            int i = mathGetInt(rand, 1, item2.count / 2);
            Stack item1 = item2.splitStack(i);
            if (item2.count > 1 && rand.nextBoolean()) multi.add(item2);
            else stacks.add(item2);
            if (item1.count > 1 && rand.nextBoolean()) multi.add(item1);
            else stacks.add(item1);
        }
        stacks.addAll(multi);
        Collections.shuffle(stacks, rand);
    }

    /* LootTable.fillInventory subset: empty-slots shuffle first, then shuffleItems. */
    static Stack[] fillFromStacks(Stack[] src, long lootSeed) {
        Stack[] slots = new Stack[SLOTS];
        for (int i = 0; i < SLOTS; ++i) slots[i] = empty();
        Random r = new Random(lootSeed);
        List<Integer> emptyIdx = new ArrayList<Integer>();
        for (int i = 0; i < SLOTS; ++i) emptyIdx.add(Integer.valueOf(i));
        Collections.shuffle(emptyIdx, r);
        List<Stack> list = new ArrayList<Stack>();
        for (int i = 0; i < src.length; ++i) list.add(src[i].copy());
        shuffleItems(list, emptyIdx.size(), r);
        for (int i = 0; i < list.size() && !emptyIdx.isEmpty(); ++i) {
            Stack s = list.get(i);
            if (s.isEmpty()) {
                emptyIdx.remove(emptyIdx.size() - 1);
                continue;
            }
            int slot = emptyIdx.remove(emptyIdx.size() - 1).intValue();
            slots[slot] = s.copy();
        }
        return slots;
    }

    static void emitStack(StringBuilder sb, Stack s) {
        u(sb, s.item);
        u(sb, s.count);
        u(sb, s.meta);
        u(sb, s.nEnchants);
        u(sb, s.nEnchants > 0 ? s.enchId[0] : 0);
        u(sb, s.nEnchants > 0 ? s.enchLvl[0] : 0);
        u(sb, s.nEnchants > 1 ? s.enchId[1] : 0);
        u(sb, s.nEnchants > 1 ? s.enchLvl[1] : 0);
    }
    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (int si = 0; si < SEEDS.length; ++si) {
            for (int set = 0; set < 2; ++set) {
                Stack[] src = stackSet(set);
                Stack[] chest = fillFromStacks(src, SEEDS[si]);
                int nonempty = 0;
                for (int i = 0; i < SLOTS; ++i) {
                    emitStack(sb, chest[i]);
                    if (!chest[i].isEmpty()) nonempty++;
                }
                u(sb, nonempty);
            }
        }
        System.out.print(sb);
    }
}
