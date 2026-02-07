package net.minecraft.entity;

import java.util.Iterator;
import java.util.List;
import net.minecraft.block.material.Material;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.DamageSource;
import net.minecraft.util.Direction;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public abstract class EntityHanging extends Entity
{
    private int tickCounter1;
    public int hangingDirection;
    public int field_146063_b;
    public int field_146064_c;
    public int field_146062_d;
    private static final String __OBFID = "CL_00001546";

    public EntityHanging(World p_i1588_1_)
    {
        super(p_i1588_1_);
        this.yOffset = 0.0F;
        this.setSize(0.5F, 0.5F);
    }

    public EntityHanging(World p_i1589_1_, int p_i1589_2_, int p_i1589_3_, int p_i1589_4_, int p_i1589_5_)
    {
        this(p_i1589_1_);
        this.field_146063_b = p_i1589_2_;
        this.field_146064_c = p_i1589_3_;
        this.field_146062_d = p_i1589_4_;
    }

    protected void entityInit() {}

    public void setDirection(int p_82328_1_)
    {
        this.hangingDirection = p_82328_1_;
        this.prevRotationYaw = this.rotationYaw = (float)(p_82328_1_ * 90);
        float f = (float)this.getWidthPixels();
        float f1 = (float)this.getHeightPixels();
        float f2 = (float)this.getWidthPixels();

        if (p_82328_1_ != 2 && p_82328_1_ != 0)
        {
            f = 0.5F;
        }
        else
        {
            f2 = 0.5F;
            this.rotationYaw = this.prevRotationYaw = (float)(Direction.rotateOpposite[p_82328_1_] * 90);
        }

        f /= 32.0F;
        f1 /= 32.0F;
        f2 /= 32.0F;
        float f3 = (float)this.field_146063_b + 0.5F;
        float f4 = (float)this.field_146064_c + 0.5F;
        float f5 = (float)this.field_146062_d + 0.5F;
        float f6 = 0.5625F;

        if (p_82328_1_ == 2)
        {
            f5 -= f6;
        }

        if (p_82328_1_ == 1)
        {
            f3 -= f6;
        }

        if (p_82328_1_ == 0)
        {
            f5 += f6;
        }

        if (p_82328_1_ == 3)
        {
            f3 += f6;
        }

        if (p_82328_1_ == 2)
        {
            f3 -= this.func_70517_b(this.getWidthPixels());
        }

        if (p_82328_1_ == 1)
        {
            f5 += this.func_70517_b(this.getWidthPixels());
        }

        if (p_82328_1_ == 0)
        {
            f3 += this.func_70517_b(this.getWidthPixels());
        }

        if (p_82328_1_ == 3)
        {
            f5 -= this.func_70517_b(this.getWidthPixels());
        }

        f4 += this.func_70517_b(this.getHeightPixels());
        this.setPosition((double)f3, (double)f4, (double)f5);
        float f7 = -0.03125F;
        this.boundingBox.setBounds((double)(f3 - f - f7), (double)(f4 - f1 - f7), (double)(f5 - f2 - f7), (double)(f3 + f + f7), (double)(f4 + f1 + f7), (double)(f5 + f2 + f7));
    }

    private float func_70517_b(int p_70517_1_)
    {
        return p_70517_1_ == 32 ? 0.5F : (p_70517_1_ == 64 ? 0.5F : 0.0F);
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        this.prevPosX = this.posX;
        this.prevPosY = this.posY;
        this.prevPosZ = this.posZ;

        if (this.tickCounter1++ == 100 && !this.worldObj.isRemote)
        {
            this.tickCounter1 = 0;

            if (!this.isDead && !this.onValidSurface())
            {
                this.setDead();
                this.onBroken((Entity)null);
            }
        }
    }

    /**
     * checks to make sure painting can be placed there
     */
    public boolean onValidSurface()
    {
        if (!this.worldObj.getCollidingBoundingBoxes(this, this.boundingBox).isEmpty())
        {
            return false;
        }
        else
        {
            int i = Math.max(1, this.getWidthPixels() / 16);
            int j = Math.max(1, this.getHeightPixels() / 16);
            int k = this.field_146063_b;
            int l = this.field_146064_c;
            int i1 = this.field_146062_d;

            if (this.hangingDirection == 2)
            {
                k = MathHelper.floor_double(this.posX - (double)((float)this.getWidthPixels() / 32.0F));
            }

            if (this.hangingDirection == 1)
            {
                i1 = MathHelper.floor_double(this.posZ - (double)((float)this.getWidthPixels() / 32.0F));
            }

            if (this.hangingDirection == 0)
            {
                k = MathHelper.floor_double(this.posX - (double)((float)this.getWidthPixels() / 32.0F));
            }

            if (this.hangingDirection == 3)
            {
                i1 = MathHelper.floor_double(this.posZ - (double)((float)this.getWidthPixels() / 32.0F));
            }

            l = MathHelper.floor_double(this.posY - (double)((float)this.getHeightPixels() / 32.0F));

            for (int j1 = 0; j1 < i; ++j1)
            {
                for (int k1 = 0; k1 < j; ++k1)
                {
                    Material material;

                    if (this.hangingDirection != 2 && this.hangingDirection != 0)
                    {
                        material = this.worldObj.getBlock(this.field_146063_b, l + k1, i1 + j1).getMaterial();
                    }
                    else
                    {
                        material = this.worldObj.getBlock(k + j1, l + k1, this.field_146062_d).getMaterial();
                    }

                    if (!material.isSolid())
                    {
                        return false;
                    }
                }
            }

            List list = this.worldObj.getEntitiesWithinAABBExcludingEntity(this, this.boundingBox);
            Iterator iterator = list.iterator();
            Entity entity;

            do
            {
                if (!iterator.hasNext())
                {
                    return true;
                }

                entity = (Entity)iterator.next();
            }
            while (!(entity instanceof EntityHanging));

            return false;
        }
    }

    /**
     * Returns true if other Entities should be prevented from moving through this Entity.
     */
    public boolean canBeCollidedWith()
    {
        return true;
    }

    /**
     * Called when a player attacks an entity. If this returns true the attack will not happen.
     */
    public boolean hitByEntity(Entity p_85031_1_)
    {
        return p_85031_1_ instanceof EntityPlayer ? this.attackEntityFrom(DamageSource.causePlayerDamage((EntityPlayer)p_85031_1_), 0.0F) : false;
    }

    public void func_145781_i(int p_145781_1_)
    {
        this.worldObj.func_147450_X();
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        if (this.isEntityInvulnerable())
        {
            return false;
        }
        else
        {
            if (!this.isDead && !this.worldObj.isRemote)
            {
                this.setDead();
                this.setBeenAttacked();
                this.onBroken(p_70097_1_.getEntity());
            }

            return true;
        }
    }

    /**
     * Tries to moves the entity by the passed in displacement. Args: x, y, z
     */
    public void moveEntity(double p_70091_1_, double p_70091_3_, double p_70091_5_)
    {
        if (!this.worldObj.isRemote && !this.isDead && p_70091_1_ * p_70091_1_ + p_70091_3_ * p_70091_3_ + p_70091_5_ * p_70091_5_ > 0.0D)
        {
            this.setDead();
            this.onBroken((Entity)null);
        }
    }

    /**
     * Adds to the current velocity of the entity. Args: x, y, z
     */
    public void addVelocity(double p_70024_1_, double p_70024_3_, double p_70024_5_)
    {
        if (!this.worldObj.isRemote && !this.isDead && p_70024_1_ * p_70024_1_ + p_70024_3_ * p_70024_3_ + p_70024_5_ * p_70024_5_ > 0.0D)
        {
            this.setDead();
            this.onBroken((Entity)null);
        }
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        p_70014_1_.setByte("Direction", (byte)this.hangingDirection);
        p_70014_1_.setInteger("TileX", this.field_146063_b);
        p_70014_1_.setInteger("TileY", this.field_146064_c);
        p_70014_1_.setInteger("TileZ", this.field_146062_d);

        switch (this.hangingDirection)
        {
            case 0:
                p_70014_1_.setByte("Dir", (byte)2);
                break;
            case 1:
                p_70014_1_.setByte("Dir", (byte)1);
                break;
            case 2:
                p_70014_1_.setByte("Dir", (byte)0);
                break;
            case 3:
                p_70014_1_.setByte("Dir", (byte)3);
        }
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        if (p_70037_1_.hasKey("Direction", 99))
        {
            this.hangingDirection = p_70037_1_.getByte("Direction");
        }
        else
        {
            switch (p_70037_1_.getByte("Dir"))
            {
                case 0:
                    this.hangingDirection = 2;
                    break;
                case 1:
                    this.hangingDirection = 1;
                    break;
                case 2:
                    this.hangingDirection = 0;
                    break;
                case 3:
                    this.hangingDirection = 3;
            }
        }

        this.field_146063_b = p_70037_1_.getInteger("TileX");
        this.field_146064_c = p_70037_1_.getInteger("TileY");
        this.field_146062_d = p_70037_1_.getInteger("TileZ");
        this.setDirection(this.hangingDirection);
    }

    public abstract int getWidthPixels();

    public abstract int getHeightPixels();

    /**
     * Called when this entity is broken. Entity parameter may be null.
     */
    public abstract void onBroken(Entity p_110128_1_);

    protected boolean shouldSetPosAfterLoading()
    {
        return false;
    }
}