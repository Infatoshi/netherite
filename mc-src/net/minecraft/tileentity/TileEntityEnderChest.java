package net.minecraft.tileentity;

import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;

public class TileEntityEnderChest extends TileEntity
{
    public float field_145972_a;
    public float field_145975_i;
    public int field_145973_j;
    private int field_145974_k;
    private static final String __OBFID = "CL_00000355";

    public void updateEntity()
    {
        super.updateEntity();

        if (++this.field_145974_k % 20 * 4 == 0)
        {
            this.worldObj.addBlockEvent(this.xCoord, this.yCoord, this.zCoord, Blocks.ender_chest, 1, this.field_145973_j);
        }

        this.field_145975_i = this.field_145972_a;
        float f = 0.1F;
        double d1;

        if (this.field_145973_j > 0 && this.field_145972_a == 0.0F)
        {
            double d0 = (double)this.xCoord + 0.5D;
            d1 = (double)this.zCoord + 0.5D;
            this.worldObj.playSoundEffect(d0, (double)this.yCoord + 0.5D, d1, "random.chestopen", 0.5F, this.worldObj.rand.nextFloat() * 0.1F + 0.9F);
        }

        if (this.field_145973_j == 0 && this.field_145972_a > 0.0F || this.field_145973_j > 0 && this.field_145972_a < 1.0F)
        {
            float f2 = this.field_145972_a;

            if (this.field_145973_j > 0)
            {
                this.field_145972_a += f;
            }
            else
            {
                this.field_145972_a -= f;
            }

            if (this.field_145972_a > 1.0F)
            {
                this.field_145972_a = 1.0F;
            }

            float f1 = 0.5F;

            if (this.field_145972_a < f1 && f2 >= f1)
            {
                d1 = (double)this.xCoord + 0.5D;
                double d2 = (double)this.zCoord + 0.5D;
                this.worldObj.playSoundEffect(d1, (double)this.yCoord + 0.5D, d2, "random.chestclosed", 0.5F, this.worldObj.rand.nextFloat() * 0.1F + 0.9F);
            }

            if (this.field_145972_a < 0.0F)
            {
                this.field_145972_a = 0.0F;
            }
        }
    }

    /**
     * Called when a client event is received with the event number and argument, see World.sendClientEvent
     */
    public boolean receiveClientEvent(int p_145842_1_, int p_145842_2_)
    {
        if (p_145842_1_ == 1)
        {
            this.field_145973_j = p_145842_2_;
            return true;
        }
        else
        {
            return super.receiveClientEvent(p_145842_1_, p_145842_2_);
        }
    }

    /**
     * invalidates a tile entity
     */
    public void invalidate()
    {
        this.updateContainingBlockInfo();
        super.invalidate();
    }

    public void func_145969_a()
    {
        ++this.field_145973_j;
        this.worldObj.addBlockEvent(this.xCoord, this.yCoord, this.zCoord, Blocks.ender_chest, 1, this.field_145973_j);
    }

    public void func_145970_b()
    {
        --this.field_145973_j;
        this.worldObj.addBlockEvent(this.xCoord, this.yCoord, this.zCoord, Blocks.ender_chest, 1, this.field_145973_j);
    }

    public boolean func_145971_a(EntityPlayer p_145971_1_)
    {
        return this.worldObj.getTileEntity(this.xCoord, this.yCoord, this.zCoord) != this ? false : p_145971_1_.getDistanceSq((double)this.xCoord + 0.5D, (double)this.yCoord + 0.5D, (double)this.zCoord + 0.5D) <= 64.0D;
    }
}