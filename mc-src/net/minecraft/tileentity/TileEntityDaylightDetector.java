package net.minecraft.tileentity;

import net.minecraft.block.BlockDaylightDetector;

public class TileEntityDaylightDetector extends TileEntity
{
    private static final String __OBFID = "CL_00000350";

    public void updateEntity()
    {
        if (this.worldObj != null && !this.worldObj.isRemote && this.worldObj.getTotalWorldTime() % 20L == 0L)
        {
            this.blockType = this.getBlockType();

            if (this.blockType instanceof BlockDaylightDetector)
            {
                ((BlockDaylightDetector)this.blockType).func_149957_e(this.worldObj, this.xCoord, this.yCoord, this.zCoord);
            }
        }
    }
}