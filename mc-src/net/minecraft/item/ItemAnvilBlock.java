package net.minecraft.item;

import net.minecraft.block.Block;
import net.minecraft.block.BlockAnvil;

public class ItemAnvilBlock extends ItemMultiTexture
{
    private static final String __OBFID = "CL_00001764";

    public ItemAnvilBlock(Block p_i1826_1_)
    {
        super(p_i1826_1_, p_i1826_1_, BlockAnvil.anvilDamageNames);
    }

    /**
     * Returns the metadata of the block which this Item (ItemBlock) can place
     */
    public int getMetadata(int p_77647_1_)
    {
        return p_77647_1_ << 2;
    }
}