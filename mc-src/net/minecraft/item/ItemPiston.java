package net.minecraft.item;

import net.minecraft.block.Block;

public class ItemPiston extends ItemBlock
{
    private static final String __OBFID = "CL_00000054";

    public ItemPiston(Block p_i45348_1_)
    {
        super(p_i45348_1_);
    }

    /**
     * Returns the metadata of the block which this Item (ItemBlock) can place
     */
    public int getMetadata(int p_77647_1_)
    {
        return 7;
    }
}