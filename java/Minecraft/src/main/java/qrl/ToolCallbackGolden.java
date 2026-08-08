package qrl;

import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemHoe;
import net.minecraft.item.ItemPickaxe;
import net.minecraft.item.ItemShears;
import net.minecraft.item.ItemSpade;
import net.minecraft.item.ItemStack;
import net.minecraft.item.ItemSword;
import net.minecraft.item.ItemTool;

/** Exhaustive initialized-registry oracle for the finite tool callbacks. */
public final class ToolCallbackGolden {
    private ToolCallbackGolden() {}

    private static int canHarvest(Item item, IBlockState state) {
        if (item instanceof ItemPickaxe)
            return ((ItemPickaxe)item).canHarvestBlock(state) ? 1 : 0;
        if (item instanceof ItemSpade)
            return ((ItemSpade)item).canHarvestBlock(state) ? 1 : 0;
        if (item instanceof ItemSword)
            return ((ItemSword)item).canHarvestBlock(state) ? 1 : 0;
        if (item instanceof ItemShears)
            return ((ItemShears)item).canHarvestBlock(state) ? 1 : 0;
        return -1;
    }

    public static void main(String[] args) {
        Bootstrap.register();
        for (Item item : Item.REGISTRY) {
            if (!(item instanceof ItemTool) && !(item instanceof ItemSword)
                    && !(item instanceof ItemHoe)
                    && !(item instanceof ItemShears))
                continue;
            int itemId = Item.getIdFromItem(item);
            if (item instanceof ItemTool) {
                ItemStack stack = new ItemStack(item);
                System.out.printf("H %d %d %d %d%n", itemId,
                    item.getHarvestLevel(stack, "pickaxe", null, null),
                    item.getHarvestLevel(stack, "axe", null, null),
                    item.getHarvestLevel(stack, "shovel", null, null));
            }
            for (Block block : Block.REGISTRY) {
                int blockId = Block.getIdFromBlock(block);
                IBlockState state = block.getDefaultState();
                int value = canHarvest(item, state);
                if (value >= 0)
                    System.out.printf("C %d %d %d%n", itemId, blockId, value);
            }
            int hitWear = item instanceof ItemTool ? 2
                : item instanceof ItemSword || item instanceof ItemHoe ? 1 : 0;
            int destroyWear = item instanceof ItemSword ? 2
                : item instanceof ItemTool || item instanceof ItemShears ? 1 : 0;
            System.out.printf("D %d %d %d%n", itemId, hitWear, destroyWear);
        }
    }
}
