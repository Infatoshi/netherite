package qrl;

import java.io.File;
import java.lang.reflect.Field;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.world.storage.loot.LootContext;
import net.minecraft.world.storage.loot.LootEntry;
import net.minecraft.world.storage.loot.LootEntryItem;
import net.minecraft.world.storage.loot.LootPool;
import net.minecraft.world.storage.loot.LootTable;
import net.minecraft.world.storage.loot.LootTableList;
import net.minecraft.world.storage.loot.LootTableManager;
import net.minecraft.world.storage.loot.RandomValueRange;
import net.minecraft.world.storage.loot.conditions.LootCondition;
import net.minecraft.world.storage.loot.functions.LootFunction;

/** Real 1.11.2 quality*Luck and bonus_rolls*Luck oracle. */
public final class LuckLootGolden {
    private static void emit(int value) {
        System.out.printf("%08x%n", value);
    }

    @SuppressWarnings("unchecked")
    private static int assertBuiltinLuckCensus(
            LootTableManager manager) throws Exception {
        Field poolsField = LootTable.class.getDeclaredField("pools");
        Field entriesField = LootPool.class.getDeclaredField("lootEntries");
        Field bonusField = LootPool.class.getDeclaredField("bonusRolls");
        Field qualityField = LootEntry.class.getDeclaredField("quality");
        poolsField.setAccessible(true);
        entriesField.setAccessible(true);
        bonusField.setAccessible(true);
        qualityField.setAccessible(true);
        int checked = 0;
        for (net.minecraft.util.ResourceLocation location :
                LootTableList.getAll()) {
            if (location.equals(LootTableList.GAMEPLAY_FISHING)) continue;
            LootTable builtin = manager.getLootTableFromLocation(location);
            for (LootPool builtinPool :
                    (List<LootPool>)poolsField.get(builtin)) {
                RandomValueRange bonus =
                    (RandomValueRange)bonusField.get(builtinPool);
                if (bonus.getMin() != 0.0F || bonus.getMax() != 0.0F)
                    throw new AssertionError(
                        location + " has nonzero bonus_rolls");
                for (LootEntry entry :
                        (List<LootEntry>)entriesField.get(builtinPool))
                    if (qualityField.getInt(entry) != 0)
                        throw new AssertionError(
                            location + " has nonzero quality");
            }
            ++checked;
        }
        return checked;
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        LootEntry[] entries = {
            new LootEntryItem(Items.DIAMOND, 1, 2,
                new LootFunction[0], new LootCondition[0], "diamond"),
            new LootEntryItem(Items.STICK, 10, -1,
                new LootFunction[0], new LootCondition[0], "stick")
        };
        LootPool pool = new LootPool(entries, new LootCondition[0],
            new RandomValueRange(1.0F),
            new RandomValueRange(0.25F, 1.25F), "luck");
        LootTable table = new LootTable(new LootPool[] {pool});
        LootTableManager manager = new LootTableManager(
            new File("run/empty-loot-root"));
        emit(assertBuiltinLuckCensus(manager));
        float[] lucks = {-2.0F, -0.5F, 0.0F, 0.5F,
                         1.0F, 2.0F, 3.5F, 4.0F};
        for (int index = 0; index < 64; ++index) {
            Random random = new Random(0x4c55434bL + index * 10007L);
            LootContext context = new LootContext(
                lucks[index & 7], null, manager, null, null, null);
            List<ItemStack> result =
                table.generateLootForPools(random, context);
            emit(index);
            emit(result.size());
            for (int item = 0; item < 8; ++item)
                emit(item < result.size()
                    ? Item.getIdFromItem(result.get(item).getItem()) : 0);
            InventoryBasic inventory =
                new InventoryBasic("luck", false, 27);
            table.fillInventory(inventory,
                new Random(0x4c55434bL + index * 10007L), context);
            for (int slot = 0; slot < inventory.getSizeInventory(); ++slot) {
                ItemStack stack = inventory.getStackInSlot(slot);
                emit(stack.isEmpty()
                    ? 0 : Item.getIdFromItem(stack.getItem()));
                emit(stack.isEmpty() ? 0 : stack.getCount());
            }
        }
    }
}
