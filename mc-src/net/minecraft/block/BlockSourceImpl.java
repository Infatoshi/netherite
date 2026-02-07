package net.minecraft.block;

import net.minecraft.dispenser.IBlockSource;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.World;

public class BlockSourceImpl implements IBlockSource
{
    private final World worldObj;
    private final int xPos;
    private final int yPos;
    private final int zPos;
    private static final String __OBFID = "CL_00001194";

    public BlockSourceImpl(World p_i1365_1_, int p_i1365_2_, int p_i1365_3_, int p_i1365_4_)
    {
        this.worldObj = p_i1365_1_;
        this.xPos = p_i1365_2_;
        this.yPos = p_i1365_3_;
        this.zPos = p_i1365_4_;
    }

    public World getWorld()
    {
        return this.worldObj;
    }

    public double getX()
    {
        return (double)this.xPos + 0.5D;
    }

    public double getY()
    {
        return (double)this.yPos + 0.5D;
    }

    public double getZ()
    {
        return (double)this.zPos + 0.5D;
    }

    public int getXInt()
    {
        return this.xPos;
    }

    public int getYInt()
    {
        return this.yPos;
    }

    public int getZInt()
    {
        return this.zPos;
    }

    public int getBlockMetadata()
    {
        return this.worldObj.getBlockMetadata(this.xPos, this.yPos, this.zPos);
    }

    public TileEntity getBlockTileEntity()
    {
        return this.worldObj.getTileEntity(this.xPos, this.yPos, this.zPos);
    }
}