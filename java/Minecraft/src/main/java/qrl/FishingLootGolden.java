package qrl;

import java.io.File;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.world.storage.loot.LootContext;
import net.minecraft.world.storage.loot.LootTable;
import net.minecraft.world.storage.loot.LootTableList;
import net.minecraft.world.storage.loot.LootTableManager;

/** Real 1.11.2 gameplay/fishing nested-table and function oracle. */
public final class FishingLootGolden {
    private static void emit(int value) {
        System.out.printf("%08x%n", value);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        LootTableManager manager = new LootTableManager(new File("run/empty-loot-root"));
        LootTable table = manager.getLootTableFromLocation(LootTableList.GAMEPLAY_FISHING);
        Random random = new Random(0x0fedcba98765L ^ 0x5deece66dL);
        for (int i = 0; i < 48; ++i) {
            LootContext context = new LootContext((float)(i & 3), null, manager,
                null, null, null);
            List<ItemStack> generated = table.generateLootForPools(random, context);
            if (generated.size() != 1) throw new AssertionError(generated.size());
            ItemStack stack = generated.get(0);
            int item = Item.getIdFromItem(stack.getItem());
            NBTTagList enchants = stack.getEnchantmentTagList();
            if (item == 403 && stack.hasTagCompound())
                enchants = stack.getTagCompound().getTagList("StoredEnchantments", 10);
            int n = enchants == null ? 0 : enchants.tagCount();
            int id0 = 0, level0 = 0, id1 = 0, level1 = 0;
            if (n > 0) {
                NBTTagCompound e = enchants.getCompoundTagAt(0);
                id0 = e.getShort("id"); level0 = e.getShort("lvl");
            }
            if (n > 1) {
                NBTTagCompound e = enchants.getCompoundTagAt(1);
                id1 = e.getShort("id"); level1 = e.getShort("lvl");
            }
            /* Compact C potion metadata stores the otherwise-NBT water type. */
            int meta = item == 373 ? 1 : stack.getItemDamage();
            emit(item); emit(stack.getCount()); emit(meta);
            emit(n); emit(id0); emit(level0); emit(id1); emit(level1);
        }
    }
}
