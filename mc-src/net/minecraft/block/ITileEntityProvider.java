package net.minecraft.block;

import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.World;

public interface ITileEntityProvider
{
    /**
     * Returns a new instance of a block's tile entity class. Called on placing the block.
     */
    TileEntity createNewTileEntity(World p_149915_1_, int p_149915_2_);
}