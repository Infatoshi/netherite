// Eval-pure Java 1.11.2 oracle for simple_dungeon.json generation and
// LootTable.fillInventory. Ports LootPool, SetCount, EnchantRandomly, and the
// exact 27-slot shuffle path while using java.util.Random directly.
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Random;

public class Golden {
    static final int SLOTS = 27, MAX_ENCHANTS = 8;
    static final long[] SEEDS = {
        0L, 42L, 12345L, -6024556974586992056L
    };
    static final int[] ENCHANT_ID = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        16, 17, 18, 19, 20, 21, 22,
        32, 33, 34, 35, 48, 49, 50, 51, 61, 62, 70, 71
    };
    static final int[] ENCHANT_MAX = {
        4, 4, 4, 4, 4, 3, 1, 3, 3, 2, 1,
        5, 5, 5, 2, 2, 3, 3,
        5, 1, 3, 3, 5, 2, 1, 1, 3, 3, 1, 1
    };

    static class Stack {
        int item, count, meta, nEnchants;
        int[] enchId = new int[MAX_ENCHANTS];
        int[] enchLvl = new int[MAX_ENCHANTS];
        Stack(int item, int count, int meta) {
            this.item = item; this.count = count; this.meta = meta;
        }
        Stack copy() {
            Stack s = new Stack(item, count, meta);
            s.nEnchants = nEnchants;
            for (int i = 0; i < nEnchants; ++i) {
                s.enchId[i] = enchId[i]; s.enchLvl[i] = enchLvl[i];
            }
            return s;
        }
        boolean isEmpty() { return item == 0 || count <= 0; }
        Stack splitStack(int amount) {
            int take = Math.min(amount, count);
            Stack out = copy(); out.count = take;
            count -= take;
            if (count <= 0) { item = 0; count = 0; meta = 0; nEnchants = 0; }
            return out;
        }
    }

    static int getInt(Random r, int min, int max) {
        return min >= max ? min : r.nextInt(max - min + 1) + min;
    }
    static Stack stack(int item) { return new Stack(item, 1, 0); }
    static Stack counted(Random r, int item, int min, int max) {
        return new Stack(item, getInt(r, min, max), 0);
    }
    static Stack enchantedBook(Random r) {
        int i = r.nextInt(ENCHANT_ID.length);
        Stack s = new Stack(403, 1, 0);
        s.nEnchants = 1;
        s.enchId[0] = ENCHANT_ID[i];
        s.enchLvl[0] = getInt(r, 1, ENCHANT_MAX[i]);
        return s;
    }

    static Stack rare(Random r) {
        int k = r.nextInt(127);
        if ((k -= 20) < 0) return stack(329);
        if ((k -= 15) < 0) return stack(322);
        if ((k -= 2) < 0) return new Stack(322, 1, 1);
        if ((k -= 15) < 0) return stack(2256);
        if ((k -= 15) < 0) return stack(2257);
        if ((k -= 20) < 0) return stack(421);
        if ((k -= 10) < 0) return stack(418);
        if ((k -= 15) < 0) return stack(417);
        if ((k -= 5) < 0) return stack(419);
        return enchantedBook(r);
    }
    static Stack common(Random r) {
        int k = r.nextInt(125);
        if ((k -= 10) < 0) return counted(r, 265, 1, 4);
        if ((k -= 5) < 0) return counted(r, 266, 1, 4);
        if ((k -= 20) < 0) return stack(297);
        if ((k -= 20) < 0) return counted(r, 296, 1, 4);
        if ((k -= 10) < 0) return stack(325);
        if ((k -= 15) < 0) return counted(r, 331, 1, 4);
        if ((k -= 15) < 0) return counted(r, 263, 1, 4);
        if ((k -= 10) < 0) return counted(r, 362, 2, 4);
        if ((k -= 10) < 0) return counted(r, 361, 2, 4);
        return counted(r, 435, 2, 4);
    }
    static Stack mob(Random r) {
        int k = r.nextInt(40);
        if ((k -= 10) < 0) return counted(r, 352, 1, 8);
        if ((k -= 10) < 0) return counted(r, 289, 1, 8);
        if ((k -= 10) < 0) return counted(r, 367, 1, 8);
        return counted(r, 287, 1, 8);
    }

    // LootTable.generateLootForPools in JSON pool order.
    static List<Stack> generate(Random r) {
        List<Stack> out = new ArrayList<Stack>();
        int rolls = getInt(r, 1, 3);
        for (int i = 0; i < rolls; ++i) out.add(rare(r));
        rolls = getInt(r, 1, 4);
        for (int i = 0; i < rolls; ++i) out.add(common(r));
        for (int i = 0; i < 3; ++i) out.add(mob(r));
        return out;
    }

    // LootTable.shuffleItems verbatim. freeSlots remains a gate value.
    static void shuffleItems(List<Stack> stacks, int freeSlots, Random rand) {
        List<Stack> multi = new ArrayList<Stack>();
        Iterator<Stack> it = stacks.iterator();
        while (it.hasNext()) {
            Stack s = it.next();
            if (s.isEmpty()) it.remove();
            else if (s.count > 1) { multi.add(s); it.remove(); }
        }
        freeSlots -= stacks.size();
        while (freeSlots > 0 && !multi.isEmpty()) {
            Stack item2 = multi.remove(getInt(rand, 0, multi.size() - 1));
            Stack item1 = item2.splitStack(getInt(rand, 1, item2.count / 2));
            if (item2.count > 1 && rand.nextBoolean()) multi.add(item2);
            else stacks.add(item2);
            if (item1.count > 1 && rand.nextBoolean()) multi.add(item1);
            else stacks.add(item1);
        }
        stacks.addAll(multi);
        Collections.shuffle(stacks, rand);
    }

    // fillInventory: generate first, then shuffle empty slots, then items.
    static Stack[] fill(long seed) {
        Random r = new Random(seed);
        List<Stack> generated = generate(r);
        List<Integer> empty = new ArrayList<Integer>();
        for (int i = 0; i < SLOTS; ++i) empty.add(Integer.valueOf(i));
        Collections.shuffle(empty, r);
        shuffleItems(generated, empty.size(), r);
        Stack[] slots = new Stack[SLOTS];
        for (int i = 0; i < SLOTS; ++i) slots[i] = new Stack(0, 0, 0);
        for (Stack s : generated) {
            if (empty.isEmpty()) break;
            int slot = empty.remove(empty.size() - 1).intValue();
            slots[slot] = s.copy();
        }
        return slots;
    }

    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long)v) & 0xffffffffL)).append('\n');
    }
    static void emit(StringBuilder sb, Stack s) {
        u(sb, s.item); u(sb, s.count); u(sb, s.meta); u(sb, s.nEnchants);
        u(sb, s.nEnchants > 0 ? s.enchId[0] : 0);
        u(sb, s.nEnchants > 0 ? s.enchLvl[0] : 0);
        u(sb, s.nEnchants > 1 ? s.enchId[1] : 0);
        u(sb, s.nEnchants > 1 ? s.enchLvl[1] : 0);
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        for (long seed : SEEDS) {
            Stack[] slots = fill(seed);
            int nonempty = 0;
            for (Stack s : slots) { emit(sb, s); if (!s.isEmpty()) nonempty++; }
            u(sb, nonempty);
        }
        System.out.print(sb);
    }
}
