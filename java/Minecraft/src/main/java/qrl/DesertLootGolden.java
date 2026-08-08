package qrl;

import java.io.File;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.world.storage.loot.LootContext;
import net.minecraft.world.storage.loot.LootTable;
import net.minecraft.world.storage.loot.LootTableList;
import net.minecraft.world.storage.loot.LootTableManager;

/** Real 1.11.2 desert_pyramid fillInventory oracle. */
public final class DesertLootGolden {
    private static final long[] SEEDS = {
        0L, 42L, 12345L, -6024556974586992056L
    };

    private static void emit(int value) {
        System.out.printf("%08x%n", value);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        LootTableManager manager = new LootTableManager(new File("run/empty-loot-root"));
        LootTable table = manager.getLootTableFromLocation(
            LootTableList.CHESTS_DESERT_PYRAMID);
        LootContext context = new LootContext(0.0F, null, manager,
            null, null, null);
        for (long seed : SEEDS) {
            InventoryBasic inventory = new InventoryBasic("desert", false, 27);
            table.fillInventory(inventory, new Random(seed), context);
            int nonempty = 0;
            for (int slot = 0; slot < 27; ++slot) {
                ItemStack stack = inventory.getStackInSlot(slot);
                int item = stack.isEmpty() ? 0 : Item.getIdFromItem(stack.getItem());
                int count = stack.isEmpty() ? 0 : stack.getCount();
                int meta = stack.isEmpty() ? 0 : stack.getItemDamage();
                NBTTagList enchants = stack.isEmpty()
                    ? new NBTTagList() : stack.getEnchantmentTagList();
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
                emit(item); emit(count); emit(meta); emit(n);
                emit(id0); emit(level0); emit(id1); emit(level1);
                if (!stack.isEmpty()) ++nonempty;
            }
            emit(nonempty);
        }
    }
}
