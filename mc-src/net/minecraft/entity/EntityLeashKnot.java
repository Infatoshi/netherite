package net.minecraft.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.world.World;

public class EntityLeashKnot extends EntityHanging
{
    private static final String __OBFID = "CL_00001548";

    public EntityLeashKnot(World p_i1592_1_)
    {
        super(p_i1592_1_);
    }

    public EntityLeashKnot(World p_i1593_1_, int p_i1593_2_, int p_i1593_3_, int p_i1593_4_)
    {
        super(p_i1593_1_, p_i1593_2_, p_i1593_3_, p_i1593_4_, 0);
        this.setPosition((double)p_i1593_2_ + 0.5D, (double)p_i1593_3_ + 0.5D, (double)p_i1593_4_ + 0.5D);
    }

    protected void entityInit()
    {
        super.entityInit();
    }

    public void setDirection(int p_82328_1_) {}

    public int getWidthPixels()
    {
        return 9;
    }

    public int getHeightPixels()
    {
        return 9;
    }

    /**
     * Checks if the entity is in range to render by using the past in distance and comparing it to its average edge
     * length * 64 * renderDistanceWeight Args: distance
     */
    @SideOnly(Side.CLIENT)
    public boolean isInRangeToRenderDist(double p_70112_1_)
    {
        return p_70112_1_ < 1024.0D;
    }

    /**
     * Called when this entity is broken. Entity parameter may be null.
     */
    public void onBroken(Entity p_110128_1_) {}

    /**
     * Either write this entity to the NBT tag given and return true, or return false without doing anything. If this
     * returns false the entity is not saved on disk. Ridden entities return false here as they are saved with their
     * rider.
     */
    public boolean writeToNBTOptional(NBTTagCompound p_70039_1_)
    {
        return false;
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_) {}

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_) {}

    /**
     * First layer of player interaction
     */
    public boolean interactFirst(EntityPlayer p_130002_1_)
    {
        ItemStack itemstack = p_130002_1_.getHeldItem();
        boolean flag = false;
        double d0;
        List list;
        Iterator iterator;
        EntityLiving entityliving;

        if (itemstack != null && itemstack.getItem() == Items.lead && !this.worldObj.isRemote)
        {
            d0 = 7.0D;
            list = this.worldObj.getEntitiesWithinAABB(EntityLiving.class, AxisAlignedBB.getBoundingBox(this.posX - d0, this.posY - d0, this.posZ - d0, this.posX + d0, this.posY + d0, this.posZ + d0));

            if (list != null)
            {
                iterator = list.iterator();

                while (iterator.hasNext())
                {
                    entityliving = (EntityLiving)iterator.next();

                    if (entityliving.getLeashed() && entityliving.getLeashedToEntity() == p_130002_1_)
                    {
                        entityliving.setLeashedToEntity(this, true);
                        flag = true;
                    }
                }
            }
        }

        if (!this.worldObj.isRemote && !flag)
        {
            this.setDead();

            if (p_130002_1_.capabilities.isCreativeMode)
            {
                d0 = 7.0D;
                list = this.worldObj.getEntitiesWithinAABB(EntityLiving.class, AxisAlignedBB.getBoundingBox(this.posX - d0, this.posY - d0, this.posZ - d0, this.posX + d0, this.posY + d0, this.posZ + d0));

                if (list != null)
                {
                    iterator = list.iterator();

                    while (iterator.hasNext())
                    {
                        entityliving = (EntityLiving)iterator.next();

                        if (entityliving.getLeashed() && entityliving.getLeashedToEntity() == this)
                        {
                            entityliving.clearLeashed(true, false);
                        }
                    }
                }
            }
        }

        return true;
    }

    /**
     * checks to make sure painting can be placed there
     */
    public boolean onValidSurface()
    {
        return this.worldObj.getBlock(this.field_146063_b, this.field_146064_c, this.field_146062_d).getRenderType() == 11;
    }

    public static EntityLeashKnot func_110129_a(World p_110129_0_, int p_110129_1_, int p_110129_2_, int p_110129_3_)
    {
        EntityLeashKnot entityleashknot = new EntityLeashKnot(p_110129_0_, p_110129_1_, p_110129_2_, p_110129_3_);
        entityleashknot.forceSpawn = true;
        p_110129_0_.spawnEntityInWorld(entityleashknot);
        return entityleashknot;
    }

    public static EntityLeashKnot getKnotForBlock(World p_110130_0_, int p_110130_1_, int p_110130_2_, int p_110130_3_)
    {
        List list = p_110130_0_.getEntitiesWithinAABB(EntityLeashKnot.class, AxisAlignedBB.getBoundingBox((double)p_110130_1_ - 1.0D, (double)p_110130_2_ - 1.0D, (double)p_110130_3_ - 1.0D, (double)p_110130_1_ + 1.0D, (double)p_110130_2_ + 1.0D, (double)p_110130_3_ + 1.0D));

        if (list != null)
        {
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                EntityLeashKnot entityleashknot = (EntityLeashKnot)iterator.next();

                if (entityleashknot.field_146063_b == p_110130_1_ && entityleashknot.field_146064_c == p_110130_2_ && entityleashknot.field_146062_d == p_110130_3_)
                {
                    return entityleashknot;
                }
            }
        }

        return null;
    }
}