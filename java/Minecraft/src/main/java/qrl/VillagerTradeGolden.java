package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.Entity;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Real 1.11.2 initial ordinary-career recipe oracle. */
public final class VillagerTradeGolden {
    private static final class MemoryWorld extends World {
        final List<Entity> entities = new ArrayList<Entity>();
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "villager-trade-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return new BlockPos(0, 64, 0); }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public boolean spawnEntity(Entity entity) {
            entities.add(entity);
            return true;
        }
    }

    private static final class TradeVillager extends EntityVillager {
        TradeVillager(World world) { super(world, 0); }
        void setSeed(long seed) { this.rand.setSeed(seed); }
        Random random() { return this.rand; }
        void tickEconomy() { super.updateAITasks(); }
    }

    private static final long[][] CASES = {
        {0, 4096}, {0, 6144}, {0, 0}, {0, 256},
        {1, 4096}, {1, 0}, {2, 0}, {3, 0}, {3, 2}, {3, 3},
        {4, 4096}, {4, 0}, {5, 0}
    };

    private static int item(ItemStack stack) {
        return stack.isEmpty() ? 0 : Item.getIdFromItem(stack.getItem());
    }

    private static int count(ItemStack stack) {
        return stack.isEmpty() ? 0 : stack.getCount();
    }

    private static int meta(ItemStack stack) {
        return stack.isEmpty() ? 0 : stack.getMetadata();
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static String stack(ItemStack stack) {
        StringBuilder out = new StringBuilder();
        NBTTagList enchants = stack.isEmpty() ? null
            : stack.getItem() == Items.ENCHANTED_BOOK
                ? Items.ENCHANTED_BOOK.getEnchantments(stack)
                : stack.getEnchantmentTagList();
        int size = enchants == null ? 0 : enchants.tagCount();
        out.append(item(stack)).append(' ').append(count(stack)).append(' ')
            .append(meta(stack)).append(' ').append(size);
        for (int i = 0; i < 8; ++i) {
            if (i < size) {
                NBTTagCompound tag = enchants.getCompoundTagAt(i);
                out.append(' ').append(tag.getShort("id"))
                    .append(' ').append(tag.getShort("lvl"));
            } else {
                out.append(" 0 0");
            }
        }
        return out.toString();
    }

    private static void consumeMissingMapPrice(
            EntityVillager.ITradeList entry, Random random) throws Exception {
        Field field = entry.getClass().getDeclaredField("value");
        field.setAccessible(true);
        Object value = field.get(entry);
        value.getClass().getMethod("getPrice", Random.class)
            .invoke(value, random);
    }

    private static Field villagerField(String name) throws Exception {
        Field field = EntityVillager.class.getDeclaredField(name);
        field.setAccessible(true);
        return field;
    }

    private static void resetLifecycle() throws Exception {
        MemoryWorld world = new MemoryWorld();
        TradeVillager villager = new TradeVillager(world);
        villager.setSeed(4096L);
        villagerField("randomTickDivider").setInt(villager, 1000);
        MerchantRecipe recipe = villager.getRecipes(null).get(0);
        villager.useRecipe(recipe);
        for (int tick = 0; tick < 40; ++tick) villager.tickEconomy();
        MerchantRecipeList recipes = villager.getRecipes(null);
        System.out.printf("R %d %d %d %d %d %d %d %d %012x%n",
            villagerField("careerId").getInt(villager),
            villagerField("careerLevel").getInt(villager), recipes.size(),
            recipe.getToolUses(), recipe.getMaxTradeUses(),
            villagerField("timeUntilReset").getInt(villager),
            villagerField("needsInitilization").getBoolean(villager) ? 1 : 0,
            villagerField("isWillingToMate").getBoolean(villager) ? 1 : 0,
            rawSeed(villager.random()));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        EntityVillager.ITradeList[][][][] all =
            EntityVillager.GET_TRADES_DONT_USE();
        for (long[] fixture : CASES) {
            int profession = (int)fixture[0];
            long seed = fixture[1];
            Random random = new Random(seed);
            int career = random.nextInt(all[profession].length) + 1;
            MerchantRecipeList recipes = new MerchantRecipeList();
            EntityVillager.ITradeList[][] levels = all[profession][career - 1];
            if (levels.length > 0) {
                for (EntityVillager.ITradeList entry : levels[0])
                    entry.addMerchantRecipe(null, recipes, random);
            }
            System.out.printf("T %d %d %d %d%n",
                profession, seed, career, recipes.size());
            for (int i = 0; i < recipes.size(); ++i) {
                MerchantRecipe recipe = recipes.get(i);
                ItemStack a = recipe.getItemToBuy();
                ItemStack b = recipe.getSecondItemToBuy();
                ItemStack out = recipe.getItemToSell();
                System.out.printf("O %d %d %d %d %d %d %d %d %d %d %d%n",
                    profession, seed, i,
                    item(a), count(a), meta(a),
                    item(b), count(b), meta(b),
                    item(out), count(out));
            }
            if (!recipes.isEmpty()) {
                MerchantRecipe recipe = recipes.get(0);
                for (int use = 1; use <= 2; ++use) {
                    recipe.incrementToolUses();
                    float pitch = (random.nextFloat() - random.nextFloat())
                        * 0.2F + 1.0F;
                    int xp = 3 + random.nextInt(4);
                    boolean reset = recipe.getToolUses() == 1
                        || random.nextInt(5) == 0;
                    if (reset) xp += 5;
                    System.out.printf("U %d %d %d %d %d %d%n",
                        profession, seed, use,
                        Float.floatToRawIntBits(pitch), xp, reset ? 1 : 0);
                }
            }
            for (int level = 1; level < levels.length; ++level) {
                int before = recipes.size();
                for (EntityVillager.ITradeList entry : levels[level]) {
                    if (entry.getClass().getSimpleName()
                            .contains("TreasureMapForEmeralds"))
                        consumeMissingMapPrice(entry, random);
                    else
                        entry.addMerchantRecipe(null, recipes, random);
                }
                System.out.printf("L %d %d %d %d %d %d %012x%n",
                    profession, seed, career, level + 1,
                    recipes.size() - before, recipes.size(), rawSeed(random));
                for (int i = before; i < recipes.size(); ++i) {
                    MerchantRecipe recipe = recipes.get(i);
                    System.out.printf("LO %d %d %d %d %d %s %s %s%n",
                        profession, seed, career, level + 1, i,
                        stack(recipe.getItemToBuy()),
                        stack(recipe.getSecondItemToBuy()),
                        stack(recipe.getItemToSell()));
                }
            }
        }
        resetLifecycle();
    }
}
