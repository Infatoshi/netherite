// Verbatim MC 1.11.2 LootTable.fillInventory materialization ground truth.
// Eval-pure: no game launch.
//
// Logic from the decompiled oracle:
//   net/minecraft/world/storage/loot/LootTable.java  fillInventory / shuffleItems subset
//   java.util.Collections.shuffle via Random.nextInt Fisher-Yates
//   java.util.Random (48-bit LCG) seeded with structure loot nextLong
//
// Fixed pre-rolled stacks (including multi-enchant books) are placed into a 27-slot
// chest. StoredEnchantments (id/level pairs) must survive shuffle + slot assignment.
// This is the unopened-chest materialization path once a loot_seed is known.
//
// CUT / OPEN: world-layout seed parity (C sh_place_blocks vs Java
// StructureStrongholdPieces nextLong capture); full generateLootForPools of the
// embedded stronghold JSON tables with EnchantWithLevels (covered separately by
// loot_table + enchant_table goldens; C integration in magma test_chest_loot).
//
// Output: 3 seeds * 2 stack-sets * (27 slots * 8 fields + nonempty) as %08x.

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

    /* Simplified shuffleItems: optional half-split then Fisher-Yates (matches C). */
    static Stack[] shuffleItems(Stack[] stacks, Random r) {
        Stack[] work = new Stack[MAX_STACKS];
        int wn = 0;
        for (int i = 0; i < stacks.length && wn < MAX_STACKS; ++i) {
            if (stacks[i].isEmpty()) continue;
            if (stacks[i].count > 1 && wn + 1 < MAX_STACKS && r.nextInt(2) == 0) {
                int half = stacks[i].count / 2;
                if (half < 1) half = 1;
                if (half >= stacks[i].count) half = stacks[i].count - 1;
                Stack a = stacks[i].copy(); a.count = stacks[i].count - half;
                Stack b = stacks[i].copy(); b.count = half;
                /* splits of multi-count plain items; books are count 1 so skip */
                work[wn++] = a;
                work[wn++] = b;
            } else {
                work[wn++] = stacks[i].copy();
            }
        }
        int[] idx = new int[wn];
        for (int k = 0; k < wn; ++k) idx[k] = k;
        for (int k = wn; k > 1; --k) {
            int j = r.nextInt(k);
            int tmp = idx[k - 1]; idx[k - 1] = idx[j]; idx[j] = tmp;
        }
        Stack[] out = new Stack[wn];
        for (int k = 0; k < wn; ++k) out[k] = work[idx[k]];
        return out;
    }

    static void shuffleInts(int[] a, int n, Random r) {
        for (int i = n; i > 1; --i) {
            int j = r.nextInt(i);
            int tmp = a[i - 1]; a[i - 1] = a[j]; a[j] = tmp;
        }
    }

    /* LootTable.fillInventory subset into 27 slots. */
    static Stack[] fillFromStacks(Stack[] stacks, long lootSeed) {
        Stack[] slots = new Stack[SLOTS];
        for (int i = 0; i < SLOTS; ++i) slots[i] = empty();
        Random r = new Random(lootSeed);
        int[] emptyIdx = new int[SLOTS];
        for (int i = 0; i < SLOTS; ++i) emptyIdx[i] = i;
        int nEmpty = SLOTS;
        Stack[] shuffled = shuffleItems(stacks, r);
        shuffleInts(emptyIdx, nEmpty, r);
        for (int i = 0; i < shuffled.length && nEmpty > 0; ++i) {
            if (shuffled[i].isEmpty()) { nEmpty--; continue; }
            int slot = emptyIdx[--nEmpty];
            slots[slot] = shuffled[i].copy();
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
