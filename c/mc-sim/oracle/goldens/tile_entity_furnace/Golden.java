// Verbatim MC 1.11.2 furnace smelt-tick ground truth. Eval-pure: no game launch.
//
// Tick loop copied VERBATIM from the decompiled oracle:
//   net/minecraft/tileentity/TileEntityFurnace.java  update(), canSmelt(), smeltItem()
// One recipe: iron ore (block id 15) -> iron ingot (265), from FurnaceRecipes; coal (263)
// getItemBurnTime()==1600; getCookTime()==200; getInventoryStackLimit()==64.
//
// CUT (matches core/tile_entity_furnace.h): world.isRemote / BlockFurnace.setState, the
// container-item + sponge/water-bucket special cases in smeltItem, markDirty/NBT. Those never
// fire for iron+coal, so the smelt/fuel/cook progression below is faithful. Output format
// matches cpu/tile_entity_furnace.c: 5 marks * (slot0,slot1,slot2 counts, burn, cook), %016llx.
public class Golden {
    static final int IRON_ORE = 15, COAL = 263, IRON_INGOT = 265;
    static final int COOK_TICKS = 200, COAL_BURN = 1600, STACK_LIMIT = 64;

    int s0i = IRON_ORE, s0c = 1;   // input
    int s1i = COAL, s1c = 1;       // fuel
    int s2i = 0, s2c = 0;          // output
    int burnTime = 0, cookTime = 0, totalCookTime = COOK_TICKS;

    boolean isBurning() { return burnTime > 0; }
    // TileEntityFurnace.getItemBurnTime (coal branch).
    static int getItemBurnTime(int fuelItem) { return fuelItem == COAL ? COAL_BURN : 0; }
    // FurnaceRecipes.getSmeltingResult: iron ore -> iron ingot; empty otherwise.
    static int smeltResult(int inItem) { return inItem == IRON_ORE ? IRON_INGOT : 0; }
    int getCookTime() { return COOK_TICKS; }

    // TileEntityFurnace.canSmelt() VERBATIM (single-count recipe result).
    boolean canSmelt() {
        if (s0c <= 0 || s0i == 0) return false;               // slot0 empty
        int result = smeltResult(s0i);
        if (result == 0) return false;                        // no recipe
        if (s2c <= 0 || s2i == 0) return true;                // output empty
        if (s2i != result) return false;                      // isItemEqual
        int total = s2c + 1;                                  // result count == 1
        return total <= STACK_LIMIT;
    }

    // TileEntityFurnace.smeltItem() VERBATIM subset (result count == 1, no sponge/container).
    void smeltItem() {
        if (!canSmelt()) return;
        int result = smeltResult(s0i);
        if (s2c <= 0 || s2i == 0) { s2i = result; s2c = 1; }  // set(2, result.copy())
        else if (s2i == result) s2c += 1;                     // grow(1)
        s0c--;                                                // itemstack.shrink(1)
        if (s0c <= 0) { s0i = 0; s0c = 0; }
    }

    // TileEntityFurnace.update() VERBATIM (server-tick smelt/fuel subset).
    void update() {
        if (isBurning()) --burnTime;
        if (isBurning() || (s1c > 0 && s0c > 0)) {
            if (!isBurning() && canSmelt()) {
                burnTime = getItemBurnTime(s1i);
                if (isBurning()) {
                    s1c--;                                    // fuel shrink(1)
                    if (s1c <= 0) { s1i = 0; s1c = 0; }
                }
            }
            if (isBurning() && canSmelt()) {
                ++cookTime;
                if (cookTime == totalCookTime) {
                    cookTime = 0;
                    totalCookTime = getCookTime();
                    smeltItem();
                }
            } else {
                cookTime = 0;
            }
        } else if (!isBurning() && cookTime > 0) {
            cookTime = clamp(cookTime - 2, 0, totalCookTime);
        }
    }
    static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }
    void dump(StringBuilder sb) {
        emit(sb, s0c);
        emit(sb, s1c);
        emit(sb, s2c);
        emit(sb, burnTime);
        emit(sb, cookTime);
    }

    public static void main(String[] args) {
        Golden f = new Golden();
        StringBuilder sb = new StringBuilder();
        int[] marks = {0, 50, 100, 200, 400};
        int cur = 0;
        for (int m = 0; m < marks.length; ++m) {
            while (cur < marks[m]) { f.update(); cur++; }
            f.dump(sb);
        }
        System.out.print(sb);
    }
}
