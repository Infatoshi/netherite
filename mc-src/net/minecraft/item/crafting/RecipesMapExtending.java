package net.minecraft.item.crafting;

import net.minecraft.init.Items;
import net.minecraft.inventory.InventoryCrafting;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.world.World;
import net.minecraft.world.storage.MapData;

public class RecipesMapExtending extends ShapedRecipes
{
    private static final String __OBFID = "CL_00000088";

    public RecipesMapExtending()
    {
        super(3, 3, new ItemStack[] {new ItemStack(Items.paper), new ItemStack(Items.paper), new ItemStack(Items.paper), new ItemStack(Items.paper), new ItemStack(Items.filled_map, 0, 32767), new ItemStack(Items.paper), new ItemStack(Items.paper), new ItemStack(Items.paper), new ItemStack(Items.paper)}, new ItemStack(Items.map, 0, 0));
    }

    /**
     * Used to check if a recipe matches current crafting inventory
     */
    public boolean matches(InventoryCrafting p_77569_1_, World p_77569_2_)
    {
        if (!super.matches(p_77569_1_, p_77569_2_))
        {
            return false;
        }
        else
        {
            ItemStack itemstack = null;

            for (int i = 0; i < p_77569_1_.getSizeInventory() && itemstack == null; ++i)
            {
                ItemStack itemstack1 = p_77569_1_.getStackInSlot(i);

                if (itemstack1 != null && itemstack1.getItem() == Items.filled_map)
                {
                    itemstack = itemstack1;
                }
            }

            if (itemstack == null)
            {
                return false;
            }
            else
            {
                MapData mapdata = Items.filled_map.getMapData(itemstack, p_77569_2_);
                return mapdata == null ? false : mapdata.scale < 4;
            }
        }
    }

    /**
     * Returns an Item that is the result of this recipe
     */
    public ItemStack getCraftingResult(InventoryCrafting p_77572_1_)
    {
        ItemStack itemstack = null;

        for (int i = 0; i < p_77572_1_.getSizeInventory() && itemstack == null; ++i)
        {
            ItemStack itemstack1 = p_77572_1_.getStackInSlot(i);

            if (itemstack1 != null && itemstack1.getItem() == Items.filled_map)
            {
                itemstack = itemstack1;
            }
        }

        itemstack = itemstack.copy();
        itemstack.stackSize = 1;

        if (itemstack.getTagCompound() == null)
        {
            itemstack.setTagCompound(new NBTTagCompound());
        }

        itemstack.getTagCompound().setBoolean("map_is_scaling", true);
        return itemstack;
    }
}