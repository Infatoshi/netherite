package net.minecraft.entity.ai;

import net.minecraft.entity.EntityCreature;

public class EntityAIRestrictSun extends EntityAIBase
{
    private EntityCreature theEntity;
    private static final String __OBFID = "CL_00001611";

    public EntityAIRestrictSun(EntityCreature p_i1652_1_)
    {
        this.theEntity = p_i1652_1_;
    }

    /**
     * Returns whether the EntityAIBase should begin execution.
     */
    public boolean shouldExecute()
    {
        return this.theEntity.worldObj.isDaytime();
    }

    /**
     * Execute a one shot task or start executing a continuous task
     */
    public void startExecuting()
    {
        this.theEntity.getNavigator().setAvoidSun(true);
    }

    /**
     * Resets the task
     */
    public void resetTask()
    {
        this.theEntity.getNavigator().setAvoidSun(false);
    }
}