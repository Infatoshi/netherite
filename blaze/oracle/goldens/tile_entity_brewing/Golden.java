// Eval-pure Minecraft 1.11.2 brewing oracle.
// Source contracts: PotionHelper.init/doReaction, VanillaBrewingRecipe, and
// TileEntityBrewingStand.update/brewPotions. PotionType identity is emitted as
// the declaration-order integer used by the native kernel.
public class Golden {
    static final int AIR=0, GUNPOWDER=289, REDSTONE=331, GLOWSTONE=348,
        FISH=349, SUGAR=353, GHAST_TEAR=370, NETHER_WART=372, POTION=373,
        GLASS_BOTTLE=374, SPIDER_EYE=375, FERMENTED_EYE=376,
        BLAZE_POWDER=377, MAGMA_CREAM=378, SPECKLED_MELON=382,
        GOLDEN_CARROT=396, RABBIT_FOOT=414, DRAGON_BREATH=437,
        SPLASH_POTION=438, LINGERING_POTION=441;
    static final int EMPTY=0, WATER=1, MUNDANE=2, THICK=3, AWKWARD=4,
        NIGHT_VISION=5, LONG_NIGHT_VISION=6, INVISIBILITY=7,
        LONG_INVISIBILITY=8, LEAPING=9, LONG_LEAPING=10,
        STRONG_LEAPING=11, FIRE_RESISTANCE=12, LONG_FIRE_RESISTANCE=13,
        SWIFTNESS=14, LONG_SWIFTNESS=15, STRONG_SWIFTNESS=16,
        SLOWNESS=17, LONG_SLOWNESS=18, WATER_BREATHING=19,
        LONG_WATER_BREATHING=20, HEALING=21, STRONG_HEALING=22,
        HARMING=23, STRONG_HARMING=24, POISON=25, LONG_POISON=26,
        STRONG_POISON=27, REGENERATION=28, LONG_REGENERATION=29,
        STRONG_REGENERATION=30, STRENGTH=31, LONG_STRENGTH=32,
        STRONG_STRENGTH=33, WEAKNESS=34, LONG_WEAKNESS=35, LUCK=36;

    static final class Stack {
        int item, count, meta;
        Stack(int i, int c, int m) {
            if (i <= 0 || c <= 0) { item=0; count=0; meta=0; }
            else { item=i; count=c; meta=m; }
        }
        boolean empty() { return item <= 0 || count <= 0; }
    }

    static Stack empty() { return new Stack(0,0,0); }
    static boolean potionItem(int item) {
        return item==POTION || item==SPLASH_POTION || item==LINGERING_POTION;
    }
    static boolean validInput(Stack s) {
        // BrewingRecipeRegistry additionally requires max stack size 1;
        // glass bottles stack to 64 despite VanillaBrewingRecipe naming them.
        return s.count==1 && potionItem(s.item) && s.meta>=EMPTY
            && s.meta<=LUCK;
    }
    static boolean reagentMatches(Stack s, int item, int meta) {
        return s.item==item && s.count>0 && (meta<0 || s.meta==meta);
    }
    static boolean reagent(Stack s) {
        if (s.count<=0) return false;
        switch (s.item) {
        case NETHER_WART: case GOLDEN_CARROT: case REDSTONE:
        case FERMENTED_EYE: case RABBIT_FOOT: case GLOWSTONE:
        case MAGMA_CREAM: case SUGAR: case SPECKLED_MELON:
        case SPIDER_EYE: case GHAST_TEAR: case BLAZE_POWDER:
        case GUNPOWDER: case DRAGON_BREATH: return true;
        case FISH: return s.meta==3;
        default: return false;
        }
    }

    // input type, reagent item, reagent meta (-1 wildcard), output type.
    static final int[][] TYPE_RULES = {
        {WATER,SPECKLED_MELON,-1,MUNDANE},
        {WATER,GHAST_TEAR,-1,MUNDANE},
        {WATER,RABBIT_FOOT,-1,MUNDANE},
        {WATER,BLAZE_POWDER,-1,MUNDANE},
        {WATER,SPIDER_EYE,-1,MUNDANE},
        {WATER,SUGAR,-1,MUNDANE},
        {WATER,MAGMA_CREAM,-1,MUNDANE},
        {WATER,GLOWSTONE,-1,THICK},
        {WATER,REDSTONE,-1,MUNDANE},
        {WATER,NETHER_WART,-1,AWKWARD},
        {AWKWARD,GOLDEN_CARROT,-1,NIGHT_VISION},
        {NIGHT_VISION,REDSTONE,-1,LONG_NIGHT_VISION},
        {NIGHT_VISION,FERMENTED_EYE,-1,INVISIBILITY},
        {LONG_NIGHT_VISION,FERMENTED_EYE,-1,LONG_INVISIBILITY},
        {INVISIBILITY,REDSTONE,-1,LONG_INVISIBILITY},
        {AWKWARD,MAGMA_CREAM,-1,FIRE_RESISTANCE},
        {FIRE_RESISTANCE,REDSTONE,-1,LONG_FIRE_RESISTANCE},
        {AWKWARD,RABBIT_FOOT,-1,LEAPING},
        {LEAPING,REDSTONE,-1,LONG_LEAPING},
        {LEAPING,GLOWSTONE,-1,STRONG_LEAPING},
        {LEAPING,FERMENTED_EYE,-1,SLOWNESS},
        {LONG_LEAPING,FERMENTED_EYE,-1,LONG_SLOWNESS},
        {SLOWNESS,REDSTONE,-1,LONG_SLOWNESS},
        {SWIFTNESS,FERMENTED_EYE,-1,SLOWNESS},
        {LONG_SWIFTNESS,FERMENTED_EYE,-1,LONG_SLOWNESS},
        {AWKWARD,SUGAR,-1,SWIFTNESS},
        {SWIFTNESS,REDSTONE,-1,LONG_SWIFTNESS},
        {SWIFTNESS,GLOWSTONE,-1,STRONG_SWIFTNESS},
        {AWKWARD,FISH,3,WATER_BREATHING},
        {WATER_BREATHING,REDSTONE,-1,LONG_WATER_BREATHING},
        {AWKWARD,SPECKLED_MELON,-1,HEALING},
        {HEALING,GLOWSTONE,-1,STRONG_HEALING},
        {HEALING,FERMENTED_EYE,-1,HARMING},
        {STRONG_HEALING,FERMENTED_EYE,-1,STRONG_HARMING},
        {HARMING,GLOWSTONE,-1,STRONG_HARMING},
        {POISON,FERMENTED_EYE,-1,HARMING},
        {LONG_POISON,FERMENTED_EYE,-1,HARMING},
        {STRONG_POISON,FERMENTED_EYE,-1,STRONG_HARMING},
        {AWKWARD,SPIDER_EYE,-1,POISON},
        {POISON,REDSTONE,-1,LONG_POISON},
        {POISON,GLOWSTONE,-1,STRONG_POISON},
        {AWKWARD,GHAST_TEAR,-1,REGENERATION},
        {REGENERATION,REDSTONE,-1,LONG_REGENERATION},
        {REGENERATION,GLOWSTONE,-1,STRONG_REGENERATION},
        {AWKWARD,BLAZE_POWDER,-1,STRENGTH},
        {STRENGTH,REDSTONE,-1,LONG_STRENGTH},
        {STRENGTH,GLOWSTONE,-1,STRONG_STRENGTH},
        {WATER,FERMENTED_EYE,-1,WEAKNESS},
        {WEAKNESS,REDSTONE,-1,LONG_WEAKNESS}
    };

    static Stack output(Stack input, Stack ingredient) {
        if (!validInput(input) || !reagent(ingredient) || !potionItem(input.item))
            return empty();
        if (input.item==POTION && ingredient.item==GUNPOWDER)
            return new Stack(SPLASH_POTION,1,input.meta);
        if (input.item==SPLASH_POTION && ingredient.item==DRAGON_BREATH)
            return new Stack(LINGERING_POTION,1,input.meta);
        for (int[] r : TYPE_RULES)
            if (input.meta==r[0] && reagentMatches(ingredient,r[1],r[2]))
                return new Stack(input.item,1,r[3]);
        return empty();
    }

    static final class Stand {
        Stack[] slots = {empty(),empty(),empty(),empty(),empty()};
        int brewTime, fuel, ingredientID, bottleBits, brewEvents, containerDrops;
        boolean canBrew() {
            if (slots[3].empty()) return false;
            for (int i=0;i<3;i++) if (!output(slots[i],slots[3]).empty()) return true;
            return false;
        }
        int bottles() {
            return (slots[0].empty()?0:1) | (slots[1].empty()?0:2)
                | (slots[2].empty()?0:4);
        }
        void brew() {
            Stack ingredient=slots[3];
            for (int i=0;i<3;i++) {
                Stack result=output(slots[i],ingredient);
                if (!result.empty()) slots[i]=result;
            }
            slots[3].count--;
            if (slots[3].count<=0) slots[3]=empty();
            if (ingredient.item==DRAGON_BREATH) {
                if (slots[3].empty()) slots[3]=new Stack(GLASS_BOTTLE,1,0);
                else containerDrops++;
            }
            brewEvents++;
        }
        void tick() {
            if (fuel<=0 && slots[4].item==BLAZE_POWDER && slots[4].count>0) {
                fuel=20;
                if (--slots[4].count<=0) slots[4]=empty();
            }
            boolean can=canBrew();
            int ingredient=slots[3].item;
            if (brewTime>0) {
                --brewTime;
                if (brewTime==0 && can) brew();
                else if (!can) brewTime=0;
                else if (ingredientID!=ingredient) brewTime=0;
            } else if (can && fuel>0) {
                --fuel;
                brewTime=400;
                ingredientID=ingredient;
            }
            bottleBits=bottles();
        }
    }

    // item, count, type, reagent, reagent meta. Same complete table plus five
    // registry boundary cases as the native battery.
    static final int[][] CASES = {
        {POTION,1,AWKWARD,GUNPOWDER,0},
        {SPLASH_POTION,1,AWKWARD,DRAGON_BREATH,0},
        {POTION,1,WATER,SPECKLED_MELON,0}, {POTION,1,WATER,GHAST_TEAR,0},
        {POTION,1,WATER,RABBIT_FOOT,0}, {POTION,1,WATER,BLAZE_POWDER,0},
        {POTION,1,WATER,SPIDER_EYE,0}, {POTION,1,WATER,SUGAR,0},
        {POTION,1,WATER,MAGMA_CREAM,0}, {POTION,1,WATER,GLOWSTONE,0},
        {POTION,1,WATER,REDSTONE,0}, {POTION,1,WATER,NETHER_WART,0},
        {POTION,1,AWKWARD,GOLDEN_CARROT,0},
        {POTION,1,NIGHT_VISION,REDSTONE,0},
        {POTION,1,NIGHT_VISION,FERMENTED_EYE,0},
        {POTION,1,LONG_NIGHT_VISION,FERMENTED_EYE,0},
        {POTION,1,INVISIBILITY,REDSTONE,0},
        {POTION,1,AWKWARD,MAGMA_CREAM,0},
        {POTION,1,FIRE_RESISTANCE,REDSTONE,0},
        {POTION,1,AWKWARD,RABBIT_FOOT,0},
        {POTION,1,LEAPING,REDSTONE,0},
        {POTION,1,LEAPING,GLOWSTONE,0},
        {POTION,1,LEAPING,FERMENTED_EYE,0},
        {POTION,1,LONG_LEAPING,FERMENTED_EYE,0},
        {POTION,1,SLOWNESS,REDSTONE,0},
        {POTION,1,SWIFTNESS,FERMENTED_EYE,0},
        {POTION,1,LONG_SWIFTNESS,FERMENTED_EYE,0},
        {POTION,1,AWKWARD,SUGAR,0},
        {POTION,1,SWIFTNESS,REDSTONE,0},
        {POTION,1,SWIFTNESS,GLOWSTONE,0},
        {POTION,1,AWKWARD,FISH,3},
        {POTION,1,WATER_BREATHING,REDSTONE,0},
        {POTION,1,AWKWARD,SPECKLED_MELON,0},
        {POTION,1,HEALING,GLOWSTONE,0},
        {POTION,1,HEALING,FERMENTED_EYE,0},
        {POTION,1,STRONG_HEALING,FERMENTED_EYE,0},
        {POTION,1,HARMING,GLOWSTONE,0},
        {POTION,1,POISON,FERMENTED_EYE,0},
        {POTION,1,LONG_POISON,FERMENTED_EYE,0},
        {POTION,1,STRONG_POISON,FERMENTED_EYE,0},
        {POTION,1,AWKWARD,SPIDER_EYE,0},
        {POTION,1,POISON,REDSTONE,0},
        {POTION,1,POISON,GLOWSTONE,0},
        {POTION,1,AWKWARD,GHAST_TEAR,0},
        {POTION,1,REGENERATION,REDSTONE,0},
        {POTION,1,REGENERATION,GLOWSTONE,0},
        {POTION,1,AWKWARD,BLAZE_POWDER,0},
        {POTION,1,STRENGTH,REDSTONE,0},
        {POTION,1,STRENGTH,GLOWSTONE,0},
        {POTION,1,WATER,FERMENTED_EYE,0},
        {POTION,1,WEAKNESS,REDSTONE,0},
        {GLASS_BOTTLE,1,EMPTY,NETHER_WART,0},
        {POTION,2,WATER,NETHER_WART,0},
        {POTION,1,AWKWARD,FISH,0},
        {1,1,WATER,NETHER_WART,0},
        {POTION,1,WATER,AIR,0}
    };

    static void emit(StringBuilder sb, int v) {
        sb.append(String.format("%016x", ((long)v)&0xffffffffL)).append('\n');
    }
    static void emitState(StringBuilder sb, Stand b) {
        for (Stack s : b.slots) { emit(sb,s.item); emit(sb,s.count); emit(sb,s.meta); }
        emit(sb,b.brewTime); emit(sb,b.fuel); emit(sb,b.ingredientID);
        emit(sb,b.bottleBits); emit(sb,b.brewEvents); emit(sb,b.containerDrops);
    }
    static Stand base() {
        Stand b=new Stand();
        b.slots[0]=new Stack(POTION,1,WATER);
        b.slots[3]=new Stack(NETHER_WART,1,0);
        b.slots[4]=new Stack(BLAZE_POWDER,1,0);
        return b;
    }

    public static void main(String[] args) {
        StringBuilder sb=new StringBuilder();
        for (int[] c : CASES) {
            Stack r=output(new Stack(c[0],c[1],c[2]),new Stack(c[3],1,c[4]));
            emit(sb,r.item); emit(sb,r.count); emit(sb,r.meta);
        }

        Stand b=new Stand();
        b.slots[0]=new Stack(POTION,1,WATER);
        b.slots[1]=new Stack(SPLASH_POTION,1,WATER);
        b.slots[2]=new Stack(LINGERING_POTION,1,WATER);
        b.slots[3]=new Stack(NETHER_WART,2,0);
        b.slots[4]=new Stack(BLAZE_POWDER,2,0);
        b.bottleBits=b.bottles();
        int[] marks={0,1,2,400,401,402}; int cur=0;
        for (int mark : marks) {
            while (cur<mark) { b.tick(); cur++; }
            emitState(sb,b);
        }

        b=base(); b.tick(); b.slots[3]=new Stack(SUGAR,1,0); b.tick();
        emitState(sb,b);
        b=base(); b.tick(); b.slots[3]=empty(); b.tick();
        emitState(sb,b);

        b=new Stand(); b.slots[0]=new Stack(SPLASH_POTION,1,AWKWARD);
        b.slots[3]=new Stack(DRAGON_BREATH,2,0);
        b.slots[4]=new Stack(BLAZE_POWDER,1,0);
        for (int i=0;i<401;i++) b.tick(); emitState(sb,b);

        b=new Stand(); b.slots[0]=new Stack(SPLASH_POTION,1,AWKWARD);
        b.slots[3]=new Stack(DRAGON_BREATH,1,0);
        b.slots[4]=new Stack(BLAZE_POWDER,1,0);
        for (int i=0;i<401;i++) b.tick(); emitState(sb,b);
        System.out.print(sb);
    }
}
