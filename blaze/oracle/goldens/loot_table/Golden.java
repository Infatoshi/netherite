// Verbatim MC 1.11.2 loot table core (vanilla ground truth, eval-pure).
// Sources (decompiled oracle, logic copied VERBATIM into this standalone driver):
//   net/minecraft/world/storage/loot/RandomValueRange.java  generateInt / generateFloat
//   net/minecraft/util/math/MathHelper.java                 floor, getInt, nextFloat
//   net/minecraft/world/storage/loot/LootEntry.java         getEffectiveWeight
//   net/minecraft/world/storage/loot/LootPool.java          createLootRoll, generateLoot
//   net/minecraft/world/storage/loot/LootEntryItem.java     addLoot (stack create + split)
//   net/minecraft/world/storage/loot/LootTable.java         generateLootForPools (pools only)
//   net/minecraft/world/storage/loot/functions/SetCount.java
//   net/minecraft/world/storage/loot/functions/LootingEnchantBonus.java
//
// Embedded tables match core/loot_table.h (not JSON). Conditions always true.
// Looting uses a simplified LootContext stand-in: hasKiller + lootingLevel (no Entity graph).
// Item registry -> legacy int ids. Max stack = 64 for the whole subset.
// Output: LT_NUM_TABLES * LT_N_ROLLS * (1 + LT_MAX_STACKS*3) lines of %08x (n, then item/count/meta).

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class Golden {
    static final int AIR = 0, APPLE = 260, BREAD = 297, COAL = 263, IRON_INGOT = 265,
        GOLD_INGOT = 266, DIAMOND = 264, STICK = 280, ROTTEN_FLESH = 367, BONE = 352;
    static final int MAX_STACK = 64;
    static final int FN_SET_COUNT = 1, FN_LOOTING = 2;
    static final int NUM_TABLES = 3, N_ROLLS = 8, MAX_STACKS = 16;

    // ---- verbatim MathHelper subset ----
    static int floor(float value) {
        int i = (int)value;
        return value < (float)i ? i - 1 : i;
    }

    static int getInt(Random random, int minimum, int maximum) {
        return minimum >= maximum ? minimum : random.nextInt(maximum - minimum + 1) + minimum;
    }

    static float nextFloat(Random random, float minimum, float maximum) {
        return minimum >= maximum ? minimum : random.nextFloat() * (maximum - minimum) + minimum;
    }

    // Java Math.round(float)
    static int round(float a) {
        return floor(a + 0.5f);
    }

    // ---- RandomValueRange ----
    static class RandomValueRange {
        final float min, max;
        RandomValueRange(float min, float max) { this.min = min; this.max = max; }
        int generateInt(Random rand) {
            return getInt(rand, floor(this.min), floor(this.max));
        }
        float generateFloat(Random rand) {
            return nextFloat(rand, this.min, this.max);
        }
    }

    // ---- context stand-in ----
    static class LootContext {
        final float luck;
        final boolean hasKiller;
        final int lootingLevel;
        LootContext(float luck, boolean hasKiller, int lootingLevel) {
            this.luck = luck;
            this.hasKiller = hasKiller;
            this.lootingLevel = lootingLevel;
        }
        float getLuck() { return luck; }
        int getLootingModifier() { return lootingLevel; }
    }

    // ---- stack ----
    static class ItemStack {
        int item, count, meta;
        ItemStack(int item, int count, int meta) {
            this.item = item; this.count = count; this.meta = meta;
        }
        static ItemStack of(int item) { return new ItemStack(item, 1, 0); }
        boolean isEmpty() { return item == AIR || count <= 0; }
        int getCount() { return count; }
        void setCount(int c) { count = c; }
        void grow(int q) { setCount(count + q); }
        int getMaxStackSize() { return MAX_STACK; }
        ItemStack copy() { return new ItemStack(item, count, meta); }
    }

    // ---- functions ----
    static abstract class LootFunction {
        abstract ItemStack apply(ItemStack stack, Random rand, LootContext context);
    }

    static class SetCount extends LootFunction {
        final RandomValueRange countRange;
        SetCount(RandomValueRange countRange) { this.countRange = countRange; }
        ItemStack apply(ItemStack stack, Random rand, LootContext context) {
            stack.setCount(this.countRange.generateInt(rand));
            return stack;
        }
    }

    static class LootingEnchantBonus extends LootFunction {
        final RandomValueRange count;
        final int limit;
        LootingEnchantBonus(RandomValueRange count, int limit) {
            this.count = count; this.limit = limit;
        }
        ItemStack apply(ItemStack stack, Random rand, LootContext context) {
            // LootingEnchantBonus: if killer is EntityLivingBase ...
            if (!context.hasKiller) return stack;
            int i = context.getLootingModifier();
            if (i == 0) return stack;
            float f = (float)i * this.count.generateFloat(rand);
            stack.grow(round(f));
            if (this.limit != 0 && stack.getCount() > this.limit)
                stack.setCount(this.limit);
            return stack;
        }
    }

    // ---- entry ----
    static class LootEntry {
        final int item, meta, weight, quality;
        final LootFunction[] functions;
        LootEntry(int item, int meta, int weight, int quality, LootFunction[] functions) {
            this.item = item; this.meta = meta; this.weight = weight; this.quality = quality;
            this.functions = functions;
        }
        int getEffectiveWeight(float luck) {
            return Math.max(floor((float)this.weight + (float)this.quality * luck), 0);
        }
        void addLoot(List<ItemStack> stacks, Random rand, LootContext context) {
            ItemStack itemstack = new ItemStack(this.item, 1, this.meta);
            for (LootFunction lootfunction : this.functions)
                itemstack = lootfunction.apply(itemstack, rand, context);
            if (!itemstack.isEmpty()) {
                if (itemstack.getCount() < MAX_STACK) {
                    stacks.add(itemstack);
                } else {
                    int i = itemstack.getCount();
                    while (i > 0) {
                        ItemStack itemstack1 = itemstack.copy();
                        itemstack1.setCount(Math.min(itemstack.getMaxStackSize(), i));
                        i -= itemstack1.getCount();
                        stacks.add(itemstack1);
                    }
                }
            }
        }
    }

    // ---- pool ----
    static class LootPool {
        final LootEntry[] lootEntries;
        final RandomValueRange rolls, bonusRolls;
        LootPool(LootEntry[] entries, RandomValueRange rolls, RandomValueRange bonusRolls) {
            this.lootEntries = entries;
            this.rolls = rolls;
            this.bonusRolls = bonusRolls;
        }
        // createLootRoll VERBATIM (conditions always true)
        void createLootRoll(List<ItemStack> stacks, Random rand, LootContext context) {
            List<LootEntry> list = new ArrayList<LootEntry>();
            int i = 0;
            for (LootEntry lootentry : this.lootEntries) {
                int j = lootentry.getEffectiveWeight(context.getLuck());
                if (j > 0) {
                    list.add(lootentry);
                    i += j;
                }
            }
            if (i != 0 && !list.isEmpty()) {
                int k = rand.nextInt(i);
                for (LootEntry lootentry1 : list) {
                    k -= lootentry1.getEffectiveWeight(context.getLuck());
                    if (k < 0) {
                        lootentry1.addLoot(stacks, rand, context);
                        return;
                    }
                }
            }
        }
        // generateLoot VERBATIM (pool conditions always true)
        void generateLoot(List<ItemStack> stacks, Random rand, LootContext context) {
            int i = this.rolls.generateInt(rand) + floor(this.bonusRolls.generateFloat(rand) * context.getLuck());
            for (int j = 0; j < i; ++j)
                this.createLootRoll(stacks, rand, context);
        }
    }

    // ---- table ----
    static class LootTable {
        final LootPool[] pools;
        LootTable(LootPool[] pools) { this.pools = pools; }
        List<ItemStack> generateLootForPools(Random rand, LootContext context) {
            List<ItemStack> list = new ArrayList<ItemStack>();
            for (LootPool lootpool : this.pools)
                lootpool.generateLoot(list, rand, context);
            return list;
        }
    }

    static RandomValueRange rvr(float a, float b) { return new RandomValueRange(a, b); }
    static LootFunction[] fns(LootFunction... f) { return f; }

    static LootTable tableGet(int id) {
        if (id == 0) {
            return new LootTable(new LootPool[] {
                new LootPool(new LootEntry[] {
                    new LootEntry(APPLE, 0, 1, 0, fns(new SetCount(rvr(1, 1)))),
                    new LootEntry(BREAD, 0, 3, 0, fns(new SetCount(rvr(1, 2))))
                }, rvr(1, 1), rvr(0, 0))
            });
        } else if (id == 1) {
            return new LootTable(new LootPool[] {
                new LootPool(new LootEntry[] {
                    new LootEntry(IRON_INGOT, 0, 2, 0, fns(new SetCount(rvr(1, 3)))),
                    new LootEntry(GOLD_INGOT, 0, 1, 0, fns(new SetCount(rvr(2, 2)))),
                    new LootEntry(COAL, 0, 5, 0, fns())
                }, rvr(2, 4), rvr(0, 0))
            });
        } else if (id == 2) {
            return new LootTable(new LootPool[] {
                new LootPool(new LootEntry[] {
                    new LootEntry(DIAMOND, 0, 1, 2, fns(new SetCount(rvr(1, 1)))),
                    new LootEntry(STICK, 0, 10, 0, fns(new SetCount(rvr(1, 4))))
                }, rvr(1, 1), rvr(0, 0)),
                new LootPool(new LootEntry[] {
                    new LootEntry(ROTTEN_FLESH, 0, 1, 0, fns(
                        new SetCount(rvr(0, 2)),
                        new LootingEnchantBonus(rvr(0, 1), 0))),
                    new LootEntry(BONE, 0, 1, 0, fns(new SetCount(rvr(1, 1))))
                }, rvr(1, 1), rvr(0, 0))
            });
        }
        return new LootTable(new LootPool[0]);
    }

    static LootContext scenarioCtx(int tableId, int rollIdx) {
        if (tableId == 2) {
            float luck = (rollIdx & 1) != 0 ? 5.0f : 0.0f;
            int looting = rollIdx % 4;
            return new LootContext(luck, true, looting);
        }
        return new LootContext(0.0f, false, 0);
    }

    static long scenarioSeed(int tableId, int rollIdx) {
        return 0x4C4F4F54L + (long)tableId * 10007L + (long)rollIdx * 17L;
    }

    static void u(StringBuilder sb, int v) {
        sb.append(String.format("%08x", ((long)v) & 0xFFFFFFFFL)).append('\n');
    }

    static void runOne(int tableId, int rollIdx, StringBuilder sb) {
        LootTable table = tableGet(tableId);
        LootContext ctx = scenarioCtx(tableId, rollIdx);
        Random rand = new Random(scenarioSeed(tableId, rollIdx));
        List<ItemStack> stacks = table.generateLootForPools(rand, ctx);
        int n = stacks.size();
        if (n > MAX_STACKS) n = MAX_STACKS;
        u(sb, n);
        for (int i = 0; i < MAX_STACKS; ++i) {
            if (i < n) {
                ItemStack s = stacks.get(i);
                u(sb, s.item);
                u(sb, s.count);
                u(sb, s.meta);
            } else {
                u(sb, 0); u(sb, 0); u(sb, 0);
            }
        }
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        if (args.length >= 2) {
            runOne(Integer.parseInt(args[0]), Integer.parseInt(args[1]), sb);
        } else {
            for (int t = 0; t < NUM_TABLES; ++t)
                for (int r = 0; r < N_ROLLS; ++r)
                    runOne(t, r, sb);
        }
        System.out.print(sb);
    }
}
