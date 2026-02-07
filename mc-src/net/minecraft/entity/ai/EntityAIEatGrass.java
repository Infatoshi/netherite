package net.minecraft.entity.ai;

import net.minecraft.block.Block;
import net.minecraft.entity.EntityLiving;
import net.minecraft.init.Blocks;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public class EntityAIEatGrass extends EntityAIBase
{
    private EntityLiving field_151500_b;
    private World field_151501_c;
    int field_151502_a;
    private static final String __OBFID = "CL_00001582";

    public EntityAIEatGrass(EntityLiving p_i45314_1_)
    {
        this.field_151500_b = p_i45314_1_;
        this.field_151501_c = p_i45314_1_.worldObj;
        this.setMutexBits(7);
    }

    /**
     * Returns whether the EntityAIBase should begin execution.
     */
    public boolean shouldExecute()
    {
        if (this.field_151500_b.getRNG().nextInt(this.field_151500_b.isChild() ? 50 : 1000) != 0)
        {
            return false;
        }
        else
        {
            int i = MathHelper.floor_double(this.field_151500_b.posX);
            int j = MathHelper.floor_double(this.field_151500_b.posY);
            int k = MathHelper.floor_double(this.field_151500_b.posZ);
            return this.field_151501_c.getBlock(i, j, k) == Blocks.tallgrass && this.field_151501_c.getBlockMetadata(i, j, k) == 1 ? true : this.field_151501_c.getBlock(i, j - 1, k) == Blocks.grass;
        }
    }

    /**
     * Execute a one shot task or start executing a continuous task
     */
    public void startExecuting()
    {
        this.field_151502_a = 40;
        this.field_151501_c.setEntityState(this.field_151500_b, (byte)10);
        this.field_151500_b.getNavigator().clearPathEntity();
    }

    /**
     * Resets the task
     */
    public void resetTask()
    {
        this.field_151502_a = 0;
    }

    /**
     * Returns whether an in-progress EntityAIBase should continue executing
     */
    public boolean continueExecuting()
    {
        return this.field_151502_a > 0;
    }

    public int func_151499_f()
    {
        return this.field_151502_a;
    }

    /**
     * Updates the task
     */
    public void updateTask()
    {
        this.field_151502_a = Math.max(0, this.field_151502_a - 1);

        if (this.field_151502_a == 4)
        {
            int i = MathHelper.floor_double(this.field_151500_b.posX);
            int j = MathHelper.floor_double(this.field_151500_b.posY);
            int k = MathHelper.floor_double(this.field_151500_b.posZ);

            if (this.field_151501_c.getBlock(i, j, k) == Blocks.tallgrass)
            {
                if (this.field_151501_c.getGameRules().getGameRuleBooleanValue("mobGriefing"))
                {
                    this.field_151501_c.func_147480_a(i, j, k, false);
                }

                this.field_151500_b.eatGrassBonus();
            }
            else if (this.field_151501_c.getBlock(i, j - 1, k) == Blocks.grass)
            {
                if (this.field_151501_c.getGameRules().getGameRuleBooleanValue("mobGriefing"))
                {
                    this.field_151501_c.playAuxSFX(2001, i, j - 1, k, Block.getIdFromBlock(Blocks.grass));
                    this.field_151501_c.setBlock(i, j - 1, k, Blocks.dirt, 0, 2);
                }

                this.field_151500_b.eatGrassBonus();
            }
        }
    }
}