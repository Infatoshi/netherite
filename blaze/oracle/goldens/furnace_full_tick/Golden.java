// Verbatim MC 1.11.2 furnace tick ground truth over the full KEEP smelt/fuel tables.
// Eval-pure: no game launch. Args: [seed=12345] [nticks=400], matching cpu/furnace_full_tick.c.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/tileentity/TileEntityFurnace.java  update(), canSmelt(), smeltItem(),
//                                                     getItemBurnTime()
//   net/minecraft/item/crafting/FurnaceRecipes.java   getSmeltingResult(), compareItemStacks(),
//                                                     the KEEP ores/food recipe registration order
// SANCTIONED REGISTRY SUBSTITUTION (same shim as goldens/smelting_recipes + core/smelting_recipes.h):
// Item/Block objects -> legacy integer ids. Matching algorithm + recipe DATA are verbatim MC.
//
// CUT (matches core/furnace_full_tick.h): world.isRemote / BlockFurnace.setState, the container
// item + sponge/water-bucket special cases, markDirty/NBT, getSmeltingExperience. getCookTime is a
// constant 200 for every input (TileEntityFurnace.getCookTime default). Output format matches
// cpu/furnace_full_tick.c: nticks * (slot0,slot1,slot2 counts, burn_time, cook_time), %016llx.
public class Golden {
    static final int WILDCARD = 32767, STACK_LIMIT = 64, COOK_TICKS = 200;
    static final int SENTINEL = -1;   // (i32)0xffffffff no-match marker

    // ---- block ids (core/mc_blocks.h) ----
    static final int IRON_ORE = 15, GOLD_ORE = 14, DIAMOND_ORE = 56, COAL_ORE = 16,
        REDSTONE_ORE = 73, LAPIS_ORE = 21, QUARTZ_ORE = 153, EMERALD_ORE = 129;
    // ---- item ids (crafting_recipes convention; core/smelting_recipes.h) ----
    static final int PLANKS = 5, LOG = 17, COAL = 263, DIAMOND = 264, IRON_INGOT = 265,
        GOLD_INGOT = 266, STICK = 280, REDSTONE = 336, LAVA_BUCKET = 332,
        PORKCHOP = 319, COOKED_PORKCHOP = 320, FISH = 359, COOKED_FISH = 360, DYE = 361,
        BEEF = 373, COOKED_BEEF = 374, CHICKEN = 375, COOKED_CHICKEN = 376,
        MUTTON = 377, COOKED_MUTTON = 378, RABBIT = 379, COOKED_RABBIT = 380,
        ROTTEN_FLESH = 384, BLAZE_ROD = 386, EMERALD = 408, POTATO = 412, BAKED_POTATO = 413,
        QUARTZ = 426, CHORUS_FRUIT = 454, CHORUS_FRUIT_POPPED = 455, DYE_BLUE = 4;

    static class Stack {
        int item, count, meta;
        Stack(int i, int c, int m) { item = i; count = c; meta = m; }
        Stack copy() { return new Stack(item, count, meta); }
    }
    static Stack empty() { return new Stack(0, 0, 0); }
    static Stack mk(int i, int c, int m) { return new Stack(i, c, m); }
    static boolean isEmpty(Stack s) { return s.item == 0 || s.count <= 0; }

    // ---- FurnaceRecipes (KEEP ores + food), registration order ----
    static class Recipe { Stack in, out; Recipe(Stack i, Stack o) { in = i; out = o; } }
    static Recipe[] buildRecipes() {
        return new Recipe[] {
            new Recipe(mk(IRON_ORE, 1, WILDCARD),     mk(IRON_INGOT, 1, 0)),
            new Recipe(mk(GOLD_ORE, 1, WILDCARD),     mk(GOLD_INGOT, 1, 0)),
            new Recipe(mk(DIAMOND_ORE, 1, WILDCARD),  mk(DIAMOND, 1, 0)),
            new Recipe(mk(COAL_ORE, 1, WILDCARD),     mk(COAL, 1, 0)),
            new Recipe(mk(REDSTONE_ORE, 1, WILDCARD), mk(REDSTONE, 1, 0)),
            new Recipe(mk(LAPIS_ORE, 1, WILDCARD),    mk(DYE, 1, DYE_BLUE)),
            new Recipe(mk(QUARTZ_ORE, 1, WILDCARD),   mk(QUARTZ, 1, 0)),
            new Recipe(mk(EMERALD_ORE, 1, WILDCARD),  mk(EMERALD, 1, 0)),
            new Recipe(mk(PORKCHOP, 1, WILDCARD),     mk(COOKED_PORKCHOP, 1, 0)),
            new Recipe(mk(BEEF, 1, WILDCARD),         mk(COOKED_BEEF, 1, 0)),
            new Recipe(mk(CHICKEN, 1, WILDCARD),      mk(COOKED_CHICKEN, 1, 0)),
            new Recipe(mk(RABBIT, 1, WILDCARD),       mk(COOKED_RABBIT, 1, 0)),
            new Recipe(mk(MUTTON, 1, WILDCARD),       mk(COOKED_MUTTON, 1, 0)),
            new Recipe(mk(POTATO, 1, WILDCARD),       mk(BAKED_POTATO, 1, 0)),
            new Recipe(mk(CHORUS_FRUIT, 1, WILDCARD), mk(CHORUS_FRUIT_POPPED, 1, 0)),
            new Recipe(mk(FISH, 1, 0),                mk(COOKED_FISH, 1, 0)),
            new Recipe(mk(FISH, 1, 1),                mk(COOKED_FISH, 1, 1)),
        };
    }
    // FurnaceRecipes.compareItemStacks (verbatim).
    static boolean compare(Stack a, Stack b) {
        return a.item == b.item && (b.meta == WILDCARD || b.meta == a.meta);
    }
    static Stack getSmeltingResult(Recipe[] R, Stack in) {
        for (Recipe r : R) if (compare(in, r.in)) return r.out;
        return mk(SENTINEL, 0, 0);
    }
    // TileEntityFurnace.getItemBurnTime (KEEP fuels + Material.WOOD LOG/PLANKS).
    static int getItemBurnTime(Stack s) {
        if (isEmpty(s)) return 0;
        int id = s.item;
        if (id == COAL) return 1600;
        if (id == STICK) return 100;
        if (id == LAVA_BUCKET) return 20000;
        if (id == BLAZE_ROD) return 2400;
        if (id == LOG || id == PLANKS) return 300;
        return 0;
    }
    static Stack[] smeltBattery() {
        return new Stack[] {
            mk(IRON_ORE, 1, 0), mk(GOLD_ORE, 1, 0), mk(DIAMOND_ORE, 1, 0), mk(COAL_ORE, 1, 0),
            mk(REDSTONE_ORE, 1, 0), mk(LAPIS_ORE, 1, 0), mk(QUARTZ_ORE, 1, 0), mk(EMERALD_ORE, 1, 0),
            mk(PORKCHOP, 1, 0), mk(BEEF, 1, 0), mk(CHICKEN, 1, 0), mk(RABBIT, 1, 0),
            mk(MUTTON, 1, 0), mk(POTATO, 1, 0), mk(CHORUS_FRUIT, 1, 0),
            mk(FISH, 1, 0), mk(FISH, 1, 1), mk(FISH, 1, 2), mk(FISH, 1, 3),
            mk(IRON_INGOT, 1, 0), mk(ROTTEN_FLESH, 1, 0), mk(COAL, 1, 0),
            mk(IRON_ORE, 1, 7), mk(FISH, 1, 0), empty(),
        };
    }
    static Stack[] fuelBattery() {
        return new Stack[] {
            mk(COAL, 1, 0), mk(STICK, 1, 0), mk(LOG, 1, 0), mk(PLANKS, 1, 0),
            mk(LAVA_BUCKET, 1, 0), mk(BLAZE_ROD, 1, 0), mk(DIAMOND, 1, 0), mk(IRON_INGOT, 1, 0),
        };
    }

    // ---- furnace (FftFurnace) ----
    Recipe[] R = buildRecipes();
    Stack slot0, slot1, slot2;
    int burnTime, currentBurn, cookTime, totalCook;

    boolean isBurning() { return burnTime > 0; }

    boolean canSmelt() {
        if (isEmpty(slot0)) return false;
        Stack result = getSmeltingResult(R, slot0);
        if (result.item == SENTINEL || result.count <= 0) return false;
        if (isEmpty(slot2)) return true;
        if (slot2.item != result.item || slot2.meta != result.meta) return false;
        return slot2.count + result.count <= STACK_LIMIT;
    }
    void smelt() {
        if (!canSmelt()) return;
        Stack result = getSmeltingResult(R, slot0);
        if (isEmpty(slot2)) slot2 = result.copy();
        else if (slot2.item == result.item && slot2.meta == result.meta) slot2.count += result.count;
        slot0.count--;
        if (slot0.count <= 0) slot0 = empty();
    }

    void init(long seed) {
        Stack[] smeltIn = smeltBattery();
        Stack[] fuelIn = fuelBattery();
        int ci = (int) (Long.remainderUnsigned(seed, 17));
        int fi = (int) (Long.remainderUnsigned(Long.divideUnsigned(seed, 17), 6));
        int inCnt = 1 + (int) (Long.remainderUnsigned(Long.divideUnsigned(seed, 102), 8));
        int fuCnt = 1 + (int) (Long.remainderUnsigned(Long.divideUnsigned(seed, 816), 4));
        slot0 = smeltIn[ci].copy(); slot0.count = inCnt;
        slot1 = fuelIn[fi].copy(); slot1.count = fuCnt;
        slot2 = empty();
        burnTime = 0; currentBurn = 0; cookTime = 0; totalCook = COOK_TICKS;
    }

    // fft_tick == TileEntityFurnace.update() smelt/fuel subset (verbatim).
    void tick() {
        if (isBurning()) burnTime--;
        if (isBurning() || (!isEmpty(slot1) && !isEmpty(slot0))) {
            if (!isBurning() && canSmelt()) {
                int burn = getItemBurnTime(slot1);
                burnTime = burn; currentBurn = burn;
                if (isBurning()) {
                    slot1.count--;
                    if (slot1.count <= 0) slot1 = empty();
                }
            }
            if (isBurning() && canSmelt()) {
                cookTime++;
                if (cookTime >= totalCook) { cookTime = 0; totalCook = COOK_TICKS; smelt(); }
            } else {
                cookTime = 0;
            }
        } else if (!isBurning() && cookTime > 0) {
            int v = cookTime - 2;
            if (v < 0) v = 0;
            if (v > totalCook) v = totalCook;
            cookTime = v;
        }
    }

    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseUnsignedLong(args[0]) : 12345L;
        int nticks = args.length > 1 ? Integer.parseInt(args[1]) : 400;
        Golden f = new Golden();
        f.init(seed);
        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < nticks; ++t) {
            f.tick();
            emit(sb, f.slot0.count);
            emit(sb, f.slot1.count);
            emit(sb, f.slot2.count);
            emit(sb, f.burnTime);
            emit(sb, f.cookTime);
        }
        System.out.print(sb);
    }
}
