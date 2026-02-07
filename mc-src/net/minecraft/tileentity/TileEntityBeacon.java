package net.minecraft.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.inventory.IInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.S35PacketUpdateTileEntity;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.stats.AchievementList;
import net.minecraft.util.AxisAlignedBB;

public class TileEntityBeacon extends TileEntity implements IInventory
{
    /** List of effects that Beacon can apply */
    public static final Potion[][] effectsList = new Potion[][] {{Potion.moveSpeed, Potion.digSpeed}, {Potion.resistance, Potion.jump}, {Potion.damageBoost}, {Potion.regeneration}};
    @SideOnly(Side.CLIENT)
    private long field_146016_i;
    @SideOnly(Side.CLIENT)
    private float field_146014_j;
    private boolean field_146015_k;
    /** Level of this beacon's pyramid. */
    private int levels = -1;
    /** Primary potion effect given by this beacon. */
    private int primaryEffect;
    /** Secondary potion effect given by this beacon. */
    private int secondaryEffect;
    /** Item given to this beacon as payment. */
    private ItemStack payment;
    private String field_146008_p;
    private static final String __OBFID = "CL_00000339";

    public void updateEntity()
    {
        if (this.worldObj.getTotalWorldTime() % 80L == 0L)
        {
            this.func_146003_y();
            this.func_146000_x();
        }
    }

    private void func_146000_x()
    {
        if (this.field_146015_k && this.levels > 0 && !this.worldObj.isRemote && this.primaryEffect > 0)
        {
            double d0 = (double)(this.levels * 10 + 10);
            byte b0 = 0;

            if (this.levels >= 4 && this.primaryEffect == this.secondaryEffect)
            {
                b0 = 1;
            }

            AxisAlignedBB axisalignedbb = AxisAlignedBB.getBoundingBox((double)this.xCoord, (double)this.yCoord, (double)this.zCoord, (double)(this.xCoord + 1), (double)(this.yCoord + 1), (double)(this.zCoord + 1)).expand(d0, d0, d0);
            axisalignedbb.maxY = (double)this.worldObj.getHeight();
            List list = this.worldObj.getEntitiesWithinAABB(EntityPlayer.class, axisalignedbb);
            Iterator iterator = list.iterator();
            EntityPlayer entityplayer;

            while (iterator.hasNext())
            {
                entityplayer = (EntityPlayer)iterator.next();
                entityplayer.addPotionEffect(new PotionEffect(this.primaryEffect, 180, b0, true));
            }

            if (this.levels >= 4 && this.primaryEffect != this.secondaryEffect && this.secondaryEffect > 0)
            {
                iterator = list.iterator();

                while (iterator.hasNext())
                {
                    entityplayer = (EntityPlayer)iterator.next();
                    entityplayer.addPotionEffect(new PotionEffect(this.secondaryEffect, 180, 0, true));
                }
            }
        }
    }

    private void func_146003_y()
    {
        int i = this.levels;

        if (!this.worldObj.canBlockSeeTheSky(this.xCoord, this.yCoord + 1, this.zCoord))
        {
            this.field_146015_k = false;
            this.levels = 0;
        }
        else
        {
            this.field_146015_k = true;
            this.levels = 0;

            for (int j = 1; j <= 4; this.levels = j++)
            {
                int k = this.yCoord - j;

                if (k < 0)
                {
                    break;
                }

                boolean flag = true;

                for (int l = this.xCoord - j; l <= this.xCoord + j && flag; ++l)
                {
                    for (int i1 = this.zCoord - j; i1 <= this.zCoord + j; ++i1)
                    {
                        Block block = this.worldObj.getBlock(l, k, i1);

                        if (!block.isBeaconBase(this.worldObj, l, k, i1, xCoord, yCoord, zCoord))
                        {
                            flag = false;
                            break;
                        }
                    }
                }

                if (!flag)
                {
                    break;
                }
            }

            if (this.levels == 0)
            {
                this.field_146015_k = false;
            }
        }

        if (!this.worldObj.isRemote && this.levels == 4 && i < this.levels)
        {
            Iterator iterator = this.worldObj.getEntitiesWithinAABB(EntityPlayer.class, AxisAlignedBB.getBoundingBox((double)this.xCoord, (double)this.yCoord, (double)this.zCoord, (double)this.xCoord, (double)(this.yCoord - 4), (double)this.zCoord).expand(10.0D, 5.0D, 10.0D)).iterator();

            while (iterator.hasNext())
            {
                EntityPlayer entityplayer = (EntityPlayer)iterator.next();
                entityplayer.triggerAchievement(AchievementList.field_150965_K);
            }
        }
    }

    @SideOnly(Side.CLIENT)
    public float func_146002_i()
    {
        if (!this.field_146015_k)
        {
            return 0.0F;
        }
        else
        {
            int i = (int)(this.worldObj.getTotalWorldTime() - this.field_146016_i);
            this.field_146016_i = this.worldObj.getTotalWorldTime();

            if (i > 1)
            {
                this.field_146014_j -= (float)i / 40.0F;

                if (this.field_146014_j < 0.0F)
                {
                    this.field_146014_j = 0.0F;
                }
            }

            this.field_146014_j += 0.025F;

            if (this.field_146014_j > 1.0F)
            {
                this.field_146014_j = 1.0F;
            }

            return this.field_146014_j;
        }
    }

    /**
     * Return the primary potion effect given by this beacon.
     */
    public int getPrimaryEffect()
    {
        return this.primaryEffect;
    }

    /**
     * Return the secondary potion effect given by this beacon.
     */
    public int getSecondaryEffect()
    {
        return this.secondaryEffect;
    }

    /**
     * Return the levels of this beacon's pyramid.
     */
    public int getLevels()
    {
        return this.levels;
    }

    @SideOnly(Side.CLIENT)
    public void func_146005_c(int p_146005_1_)
    {
        this.levels = p_146005_1_;
    }

    public void setPrimaryEffect(int p_146001_1_)
    {
        this.primaryEffect = 0;

        for (int j = 0; j < this.levels && j < 3; ++j)
        {
            Potion[] apotion = effectsList[j];
            int k = apotion.length;

            for (int l = 0; l < k; ++l)
            {
                Potion potion = apotion[l];

                if (potion.id == p_146001_1_)
                {
                    this.primaryEffect = p_146001_1_;
                    return;
                }
            }
        }
    }

    public void setSecondaryEffect(int p_146004_1_)
    {
        this.secondaryEffect = 0;

        if (this.levels >= 4)
        {
            for (int j = 0; j < 4; ++j)
            {
                Potion[] apotion = effectsList[j];
                int k = apotion.length;

                for (int l = 0; l < k; ++l)
                {
                    Potion potion = apotion[l];

                    if (potion.id == p_146004_1_)
                    {
                        this.secondaryEffect = p_146004_1_;
                        return;
                    }
                }
            }
        }
    }

    /**
     * Overriden in a sign to provide the text.
     */
    public Packet getDescriptionPacket()
    {
        NBTTagCompound nbttagcompound = new NBTTagCompound();
        this.writeToNBT(nbttagcompound);
        return new S35PacketUpdateTileEntity(this.xCoord, this.yCoord, this.zCoord, 3, nbttagcompound);
    }

    @SideOnly(Side.CLIENT)
    public double getMaxRenderDistanceSquared()
    {
        return 65536.0D;
    }

    public void readFromNBT(NBTTagCompound p_145839_1_)
    {
        super.readFromNBT(p_145839_1_);
        this.primaryEffect = p_145839_1_.getInteger("Primary");
        this.secondaryEffect = p_145839_1_.getInteger("Secondary");
        this.levels = p_145839_1_.getInteger("Levels");
    }

    public void writeToNBT(NBTTagCompound p_145841_1_)
    {
        super.writeToNBT(p_145841_1_);
        p_145841_1_.setInteger("Primary", this.primaryEffect);
        p_145841_1_.setInteger("Secondary", this.secondaryEffect);
        p_145841_1_.setInteger("Levels", this.levels);
    }

    /**
     * Returns the number of slots in the inventory.
     */
    public int getSizeInventory()
    {
        return 1;
    }

    /**
     * Returns the stack in slot i
     */
    public ItemStack getStackInSlot(int p_70301_1_)
    {
        return p_70301_1_ == 0 ? this.payment : null;
    }

    /**
     * Removes from an inventory slot (first arg) up to a specified number (second arg) of items and returns them in a
     * new stack.
     */
    public ItemStack decrStackSize(int p_70298_1_, int p_70298_2_)
    {
        if (p_70298_1_ == 0 && this.payment != null)
        {
            if (p_70298_2_ >= this.payment.stackSize)
            {
                ItemStack itemstack = this.payment;
                this.payment = null;
                return itemstack;
            }
            else
            {
                this.payment.stackSize -= p_70298_2_;
                return new ItemStack(this.payment.getItem(), p_70298_2_, this.payment.getItemDamage());
            }
        }
        else
        {
            return null;
        }
    }

    /**
     * When some containers are closed they call this on each slot, then drop whatever it returns as an EntityItem -
     * like when you close a workbench GUI.
     */
    public ItemStack getStackInSlotOnClosing(int p_70304_1_)
    {
        if (p_70304_1_ == 0 && this.payment != null)
        {
            ItemStack itemstack = this.payment;
            this.payment = null;
            return itemstack;
        }
        else
        {
            return null;
        }
    }

    /**
     * Sets the given item stack to the specified slot in the inventory (can be crafting or armor sections).
     */
    public void setInventorySlotContents(int p_70299_1_, ItemStack p_70299_2_)
    {
        if (p_70299_1_ == 0)
        {
            this.payment = p_70299_2_;
        }
    }

    /**
     * Returns the name of the inventory
     */
    public String getInventoryName()
    {
        return this.hasCustomInventoryName() ? this.field_146008_p : "container.beacon";
    }

    /**
     * Returns if the inventory is named
     */
    public boolean hasCustomInventoryName()
    {
        return this.field_146008_p != null && this.field_146008_p.length() > 0;
    }

    public void func_145999_a(String p_145999_1_)
    {
        this.field_146008_p = p_145999_1_;
    }

    /**
     * Returns the maximum stack size for a inventory slot.
     */
    public int getInventoryStackLimit()
    {
        return 1;
    }

    /**
     * Do not make give this method the name canInteractWith because it clashes with Container
     */
    public boolean isUseableByPlayer(EntityPlayer p_70300_1_)
    {
        return this.worldObj.getTileEntity(this.xCoord, this.yCoord, this.zCoord) != this ? false : p_70300_1_.getDistanceSq((double)this.xCoord + 0.5D, (double)this.yCoord + 0.5D, (double)this.zCoord + 0.5D) <= 64.0D;
    }

    public void openInventory() {}

    public void closeInventory() {}

    /**
     * Returns true if automation is allowed to insert the given stack (ignoring stack size) into the given slot.
     */
    public boolean isItemValidForSlot(int p_94041_1_, ItemStack p_94041_2_)
    {
        return p_94041_2_.getItem() != null && p_94041_2_.getItem().isBeaconPayment(p_94041_2_);
    }
}