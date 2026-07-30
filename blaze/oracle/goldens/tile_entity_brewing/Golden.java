// Verbatim MC 1.11.2 brewing-stand tick ground truth. Eval-pure: no game launch.
//
// Tick loop copied VERBATIM from the decompiled oracle:
//   net/minecraft/tileentity/TileEntityBrewingStand.java  update(), brewPotions()  (the fuel
//   charge, canBrew gating, brewTime countdown from 400, ingredientID guard).
//
// DOCUMENTED SIMPLIFICATION (the "missing brew graph", NOT ported here nor in
// core/tile_entity_brewing.h): vanilla canBrew()/brewPotions() delegate to
// BrewingRecipeRegistry + PotionHelper, which resolve the full water->awkward->...->tipped
// potion graph via NBT "Potion" ids. This golden and the C impl model only the ONE recipe
// water potion (373, meta WATER=1) + nether wart (372, reagent) -> awkward potion (meta
// AWKWARD=2), with the potion type carried in item meta instead of NBT. For that single
// recipe canBrew()==true exactly when a WATER potion sits over a WART reagent, so the tick
// CONTROL FLOW below is faithful; only the recipe *table* is a cut. Blaze powder (377) fuels
// 20 charges. CUT: world.isRemote / HAS_BOTTLE blockstate, container item, sounds, NBT.
//
// Output format matches cpu/tile_entity_brewing.c: 5 marks * (slot0_meta, slot3_count,
// slot4_count, brew_time, fuel), %016llx.
public class Golden {
    static final int POTION = 373, NETHER_WART = 372, BLAZE_POWDER = 377;
    static final int BREW_TICKS = 400, FUEL_CHARGE = 20;
    static final int PT_WATER = 1, PT_AWKWARD = 2;

    int s0i = POTION, s0c = 1, s0m = PT_WATER;
    int s1i = 0, s1c = 0, s1m = 0;
    int s2i = 0, s2c = 0, s2m = 0;
    int s3i = NETHER_WART, s3c = 1;   // reagent
    int s4i = BLAZE_POWDER, s4c = 1;  // fuel
    int brewTime = 0, fuel = 0, ingredientID = 0;

    // PotionHelper.isReagent for this recipe.
    static boolean isReagent(int item) { return item == NETHER_WART; }
    // A single potion-input stack (count must be 1, as in the vanilla brewing slots).
    static boolean isPotionInput(int item, int count) { return item == POTION && count == 1; }
    // PotionHelper.hasConversions for this recipe: WATER potion + WART reagent -> AWKWARD.
    static boolean hasConversion(int meta, int reagent) {
        return meta == PT_WATER && reagent == NETHER_WART;
    }

    // TileEntityBrewingStand.canBrew() for the one-recipe registry.
    boolean canBrew() {
        if (s3c <= 0 || !isReagent(s3i)) return false;
        if (isPotionInput(s0i, s0c) && hasConversion(s0m, s3i)) return true;
        if (isPotionInput(s1i, s1c) && hasConversion(s1m, s3i)) return true;
        if (isPotionInput(s2i, s2c) && hasConversion(s2m, s3i)) return true;
        return false;
    }

    // TileEntityBrewingStand.brewPotions() for the one-recipe registry: convert every eligible
    // input slot, then shrink the reagent by one.
    void brewPotions() {
        if (isPotionInput(s0i, s0c) && hasConversion(s0m, s3i)) s0m = PT_AWKWARD;
        if (isPotionInput(s1i, s1c) && hasConversion(s1m, s3i)) s1m = PT_AWKWARD;
        if (isPotionInput(s2i, s2c) && hasConversion(s2m, s3i)) s2m = PT_AWKWARD;
        s3c--;
        if (s3c <= 0) { s3i = 0; s3c = 0; }
    }

    // TileEntityBrewingStand.update() VERBATIM (server-tick subset).
    void update() {
        if (fuel <= 0 && s4i == BLAZE_POWDER && s4c > 0) {
            fuel = 20;
            s4c--;                     // itemstack.shrink(1)
            if (s4c <= 0) { s4i = 0; s4c = 0; }
        }
        boolean flag = canBrew();
        boolean flag1 = brewTime > 0;
        int slot3Item = s3i;           // brewingItemStacks.get(3).getItem()
        if (flag1) {
            --brewTime;
            boolean flag2 = brewTime == 0;
            if (flag2 && flag) {
                brewPotions();
            } else if (!flag) {
                brewTime = 0;
            } else if (ingredientID != slot3Item) {
                brewTime = 0;
            }
        } else if (flag && fuel > 0) {
            --fuel;
            brewTime = BREW_TICKS;
            ingredientID = slot3Item;
        }
    }

    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long) v) & 0xFFFFFFFFL)).append('\n');
    }
    void dump(StringBuilder sb) {
        emit(sb, s0m);
        emit(sb, s3c);
        emit(sb, s4c);
        emit(sb, brewTime);
        emit(sb, fuel);
    }

    public static void main(String[] args) {
        Golden b = new Golden();
        StringBuilder sb = new StringBuilder();
        int[] marks = {0, 50, 100, 200, 450};
        int cur = 0;
        for (int m = 0; m < marks.length; ++m) {
            while (cur < marks[m]) { b.update(); cur++; }
            b.dump(sb);
        }
        System.out.print(sb);
    }
}
