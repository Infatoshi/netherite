package net.minecraft.entity.item;

import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.item.Item;
import net.minecraft.util.DamageSource;
import net.minecraft.world.World;

public class EntityMinecartChest extends EntityMinecartContainer
{
    private static final String __OBFID = "CL_00001671";

    public EntityMinecartChest(World p_i1714_1_)
    {
        super(p_i1714_1_);
    }

    public EntityMinecartChest(World p_i1715_1_, double p_i1715_2_, double p_i1715_4_, double p_i1715_6_)
    {
        super(p_i1715_1_, p_i1715_2_, p_i1715_4_, p_i1715_6_);
    }

    public void killMinecart(DamageSource p_94095_1_)
    {
        super.killMinecart(p_94095_1_);
        this.func_145778_a(Item.getItemFromBlock(Blocks.chest), 1, 0.0F);
    }

    /**
     * Returns the number of slots in the inventory.
     */
    public int getSizeInventory()
    {
        return 27;
    }

    public int getMinecartType()
    {
        return 1;
    }

    public Block func_145817_o()
    {
        return Blocks.chest;
    }

    public int getDefaultDisplayTileOffset()
    {
        return 8;
    }
}