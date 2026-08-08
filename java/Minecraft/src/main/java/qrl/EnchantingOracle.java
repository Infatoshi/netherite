package qrl;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.lang.reflect.Field;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.inventory.ContainerEnchantment;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.WorldServer;

/** Direct execution of the real 1.11.2 ContainerEnchantment. */
final class EnchantingOracle {
    private EnchantingOracle() {}

    private static final BlockPos TABLE = new BlockPos(1536, 100, 1536);
    private static final int[][] SHELVES = {
        {-2, -2}, {-2, -1}, {-2, 0}, {-2, 1}, {-2, 2},
        {2, -2}, {2, -1}, {2, 0}, {2, 1}, {2, 2},
        {-1, -2}, {0, -2}, {1, -2}, {-1, 2}, {1, 2}
    };

    private static Field xpSeedField() throws Exception {
        Field field = EntityPlayer.class.getDeclaredField("xpSeed");
        field.setAccessible(true);
        return field;
    }

    private static long cursor(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get();
    }

    private static void setCursor(Random random, long seed48) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        ((AtomicLong)field.get(random)).set(seed48);
    }

    private static void prepare(WorldServer world, int power) {
        for (int y = 0; y <= 1; ++y)
            for (int z = -2; z <= 2; ++z)
                for (int x = -2; x <= 2; ++x)
                    world.setBlockToAir(TABLE.add(x, y, z));
        world.setBlockState(TABLE, Blocks.ENCHANTING_TABLE.getDefaultState(), 2);
        for (int i = 0; i < power; ++i)
            world.setBlockState(TABLE.add(SHELVES[i][0], 0, SHELVES[i][1]),
                                Blocks.BOOKSHELF.getDefaultState(), 2);
    }

    private static JsonArray enchantments(ItemStack stack) {
        JsonArray out = new JsonArray();
        if (stack.isEmpty() || !stack.hasTagCompound()) return out;
        NBTTagCompound tag = stack.getTagCompound();
        String key = stack.getItem() == Items.ENCHANTED_BOOK
            ? "StoredEnchantments" : "ench";
        NBTTagList list = tag.getTagList(key, 10);
        for (int i = 0; i < list.tagCount(); ++i) {
            NBTTagCompound enchantment = list.getCompoundTagAt(i);
            JsonArray pair = new JsonArray();
            pair.add(new JsonPrimitive(enchantment.getShort("id")));
            pair.add(new JsonPrimitive(enchantment.getShort("lvl")));
            out.add(pair);
        }
        return out;
    }

    static JsonObject run(WorldServer world, EntityPlayerMP player,
                          int itemId, int xpSeed, int power, int button,
                          int level, int lapis, long playerSeed) {
        JsonObject out = new JsonObject();
        int oldLevel = player.experienceLevel;
        int oldTotal = player.experienceTotal;
        float oldExperience = player.experience;
        Random playerRandom = player.getRNG();
        long oldPlayerCursor = 0L;
        long oldWorldCursor = 0L;
        int oldXpSeed = 0;
        try {
            Field xpField = xpSeedField();
            oldXpSeed = xpField.getInt(player);
            oldPlayerCursor = cursor(playerRandom);
            oldWorldCursor = cursor(world.rand);
            prepare(world, power);
            player.experienceLevel = level;
            player.experienceTotal = 0;
            player.experience = 0.0F;
            playerRandom.setSeed(playerSeed);
            xpField.setInt(player, xpSeed);

            ContainerEnchantment table = new ContainerEnchantment(
                player.inventory, world, TABLE);
            Item item = Item.getItemById(itemId);
            if (item == null) throw new IllegalArgumentException("unknown item");
            table.tableInventory.setInventorySlotContents(
                0, new ItemStack(item, 1, 0));
            table.tableInventory.setInventorySlotContents(
                1, new ItemStack(Items.DYE, lapis, 4));

            JsonArray offers = new JsonArray();
            for (int i = 0; i < 3; ++i) {
                JsonArray row = new JsonArray();
                row.add(new JsonPrimitive(table.enchantLevels[i]));
                row.add(new JsonPrimitive(table.enchantClue[i]));
                row.add(new JsonPrimitive(table.worldClue[i]));
                offers.add(row);
            }
            boolean applied = table.enchantItem(player, button);
            ItemStack result = table.tableInventory.getStackInSlot(0);
            ItemStack lapisResult = table.tableInventory.getStackInSlot(1);
            out.addProperty("ok", true);
            out.add("offers", offers);
            out.addProperty("applied", applied);
            out.addProperty("item", result.isEmpty() ? 0
                : Item.getIdFromItem(result.getItem()));
            out.addProperty("count", result.isEmpty() ? 0 : result.getCount());
            out.addProperty("meta", result.isEmpty() ? 0 : result.getItemDamage());
            out.add("enchants", enchantments(result));
            out.addProperty("lapis", lapisResult.isEmpty() ? 0
                : lapisResult.getCount());
            out.addProperty("level", player.experienceLevel);
            out.addProperty("xp_seed", xpField.getInt(player));
            out.addProperty("player_seed48", cursor(playerRandom));
        } catch (Throwable t) {
            out.addProperty("ok", false);
            out.addProperty("error", t.toString());
        } finally {
            try {
                player.experienceLevel = oldLevel;
                player.experienceTotal = oldTotal;
                player.experience = oldExperience;
                xpSeedField().setInt(player, oldXpSeed);
                setCursor(playerRandom, oldPlayerCursor);
                setCursor(world.rand, oldWorldCursor);
            } catch (Throwable ignored) {}
        }
        return out;
    }
}
