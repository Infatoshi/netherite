package net.minecraft.entity.ai;

import net.minecraft.entity.EntityLiving;

public class EntityAIWatchClosest2 extends EntityAIWatchClosest
{
    private static final String __OBFID = "CL_00001590";

    public EntityAIWatchClosest2(EntityLiving p_i1629_1_, Class p_i1629_2_, float p_i1629_3_, float p_i1629_4_)
    {
        super(p_i1629_1_, p_i1629_2_, p_i1629_3_, p_i1629_4_);
        this.setMutexBits(3);
    }
}