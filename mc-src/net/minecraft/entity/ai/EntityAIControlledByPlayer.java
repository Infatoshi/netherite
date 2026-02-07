package net.minecraft.entity.ai;

import net.minecraft.block.Block;
import net.minecraft.block.BlockSlab;
import net.minecraft.block.material.Material;
import net.minecraft.entity.EntityCreature;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.pathfinding.PathFinder;
import net.minecraft.pathfinding.PathPoint;
import net.minecraft.util.MathHelper;

public class EntityAIControlledByPlayer extends EntityAIBase
{
    private final EntityLiving thisEntity;
    private final float maxSpeed;
    private float currentSpeed;
    /** Whether the entity's speed is boosted. */
    private boolean speedBoosted;
    /** Counter for speed boosting, upon reaching maxSpeedBoostTime the speed boost will be disabled */
    private int speedBoostTime;
    /** Maximum time the entity's speed should be boosted for. */
    private int maxSpeedBoostTime;
    private static final String __OBFID = "CL_00001580";

    public EntityAIControlledByPlayer(EntityLiving p_i1620_1_, float p_i1620_2_)
    {
        this.thisEntity = p_i1620_1_;
        this.maxSpeed = p_i1620_2_;
        this.setMutexBits(7);
    }

    /**
     * Execute a one shot task or start executing a continuous task
     */
    public void startExecuting()
    {
        this.currentSpeed = 0.0F;
    }

    /**
     * Resets the task
     */
    public void resetTask()
    {
        this.speedBoosted = false;
        this.currentSpeed = 0.0F;
    }

    /**
     * Returns whether the EntityAIBase should begin execution.
     */
    public boolean shouldExecute()
    {
        return this.thisEntity.isEntityAlive() && this.thisEntity.riddenByEntity != null && this.thisEntity.riddenByEntity instanceof EntityPlayer && (this.speedBoosted || this.thisEntity.canBeSteered());
    }

    /**
     * Updates the task
     */
    public void updateTask()
    {
        EntityPlayer entityplayer = (EntityPlayer)this.thisEntity.riddenByEntity;
        EntityCreature entitycreature = (EntityCreature)this.thisEntity;
        float f = MathHelper.wrapAngleTo180_float(entityplayer.rotationYaw - this.thisEntity.rotationYaw) * 0.5F;

        if (f > 5.0F)
        {
            f = 5.0F;
        }

        if (f < -5.0F)
        {
            f = -5.0F;
        }

        this.thisEntity.rotationYaw = MathHelper.wrapAngleTo180_float(this.thisEntity.rotationYaw + f);

        if (this.currentSpeed < this.maxSpeed)
        {
            this.currentSpeed += (this.maxSpeed - this.currentSpeed) * 0.01F;
        }

        if (this.currentSpeed > this.maxSpeed)
        {
            this.currentSpeed = this.maxSpeed;
        }

        int i = MathHelper.floor_double(this.thisEntity.posX);
        int j = MathHelper.floor_double(this.thisEntity.posY);
        int k = MathHelper.floor_double(this.thisEntity.posZ);
        float f1 = this.currentSpeed;

        if (this.speedBoosted)
        {
            if (this.speedBoostTime++ > this.maxSpeedBoostTime)
            {
                this.speedBoosted = false;
            }

            f1 += f1 * 1.15F * MathHelper.sin((float)this.speedBoostTime / (float)this.maxSpeedBoostTime * (float)Math.PI);
        }

        float f2 = 0.91F;

        if (this.thisEntity.onGround)
        {
            f2 = this.thisEntity.worldObj.getBlock(MathHelper.floor_float((float)i), MathHelper.floor_float((float)j) - 1, MathHelper.floor_float((float)k)).slipperiness * 0.91F;
        }

        float f3 = 0.16277136F / (f2 * f2 * f2);
        float f4 = MathHelper.sin(entitycreature.rotationYaw * (float)Math.PI / 180.0F);
        float f5 = MathHelper.cos(entitycreature.rotationYaw * (float)Math.PI / 180.0F);
        float f6 = entitycreature.getAIMoveSpeed() * f3;
        float f7 = Math.max(f1, 1.0F);
        f7 = f6 / f7;
        float f8 = f1 * f7;
        float f9 = -(f8 * f4);
        float f10 = f8 * f5;

        if (MathHelper.abs(f9) > MathHelper.abs(f10))
        {
            if (f9 < 0.0F)
            {
                f9 -= this.thisEntity.width / 2.0F;
            }

            if (f9 > 0.0F)
            {
                f9 += this.thisEntity.width / 2.0F;
            }

            f10 = 0.0F;
        }
        else
        {
            f9 = 0.0F;

            if (f10 < 0.0F)
            {
                f10 -= this.thisEntity.width / 2.0F;
            }

            if (f10 > 0.0F)
            {
                f10 += this.thisEntity.width / 2.0F;
            }
        }

        int l = MathHelper.floor_double(this.thisEntity.posX + (double)f9);
        int i1 = MathHelper.floor_double(this.thisEntity.posZ + (double)f10);
        PathPoint pathpoint = new PathPoint(MathHelper.floor_float(this.thisEntity.width + 1.0F), MathHelper.floor_float(this.thisEntity.height + entityplayer.height + 1.0F), MathHelper.floor_float(this.thisEntity.width + 1.0F));

        if (i != l || k != i1)
        {
            Block block = this.thisEntity.worldObj.getBlock(i, j, k);
            boolean flag = !this.func_151498_a(block) && (block.getMaterial() != Material.air || !this.func_151498_a(this.thisEntity.worldObj.getBlock(i, j - 1, k)));

            if (flag && PathFinder.func_82565_a(this.thisEntity, l, j, i1, pathpoint, false, false, true) == 0 && PathFinder.func_82565_a(this.thisEntity, i, j + 1, k, pathpoint, false, false, true) == 1 && PathFinder.func_82565_a(this.thisEntity, l, j + 1, i1, pathpoint, false, false, true) == 1)
            {
                entitycreature.getJumpHelper().setJumping();
            }
        }

        if (!entityplayer.capabilities.isCreativeMode && this.currentSpeed >= this.maxSpeed * 0.5F && this.thisEntity.getRNG().nextFloat() < 0.006F && !this.speedBoosted)
        {
            ItemStack itemstack = entityplayer.getHeldItem();

            if (itemstack != null && itemstack.getItem() == Items.carrot_on_a_stick)
            {
                itemstack.damageItem(1, entityplayer);

                if (itemstack.stackSize == 0)
                {
                    ItemStack itemstack1 = new ItemStack(Items.fishing_rod);
                    itemstack1.setTagCompound(itemstack.stackTagCompound);
                    entityplayer.inventory.mainInventory[entityplayer.inventory.currentItem] = itemstack1;
                }
            }
        }

        this.thisEntity.moveEntityWithHeading(0.0F, f1);
    }

    private boolean func_151498_a(Block p_151498_1_)
    {
        return p_151498_1_.getRenderType() == 10 || p_151498_1_ instanceof BlockSlab;
    }

    /**
     * Return whether the entity's speed is boosted.
     */
    public boolean isSpeedBoosted()
    {
        return this.speedBoosted;
    }

    /**
     * Boost the entity's movement speed.
     */
    public void boostSpeed()
    {
        this.speedBoosted = true;
        this.speedBoostTime = 0;
        this.maxSpeedBoostTime = this.thisEntity.getRNG().nextInt(841) + 140;
    }

    /**
     * Return whether the entity is being controlled by a player.
     */
    public boolean isControlledByPlayer()
    {
        return !this.isSpeedBoosted() && this.currentSpeed > this.maxSpeed * 0.3F;
    }
}