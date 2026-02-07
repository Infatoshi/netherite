package net.minecraft.entity.ai;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;

public class EntitySenses
{
    EntityLiving entityObj;
    /** Cache of entities which we can see */
    List seenEntities = new ArrayList();
    /** Cache of entities which we cannot see */
    List unseenEntities = new ArrayList();
    private static final String __OBFID = "CL_00001628";

    public EntitySenses(EntityLiving p_i1672_1_)
    {
        this.entityObj = p_i1672_1_;
    }

    /**
     * Clears canSeeCachePositive and canSeeCacheNegative.
     */
    public void clearSensingCache()
    {
        this.seenEntities.clear();
        this.unseenEntities.clear();
    }

    /**
     * Checks, whether 'our' entity can see the entity given as argument (true) or not (false), caching the result.
     */
    public boolean canSee(Entity p_75522_1_)
    {
        if (this.seenEntities.contains(p_75522_1_))
        {
            return true;
        }
        else if (this.unseenEntities.contains(p_75522_1_))
        {
            return false;
        }
        else
        {
            this.entityObj.worldObj.theProfiler.startSection("canSee");
            boolean flag = this.entityObj.canEntityBeSeen(p_75522_1_);
            this.entityObj.worldObj.theProfiler.endSection();

            if (flag)
            {
                this.seenEntities.add(p_75522_1_);
            }
            else
            {
                this.unseenEntities.add(p_75522_1_);
            }

            return flag;
        }
    }
}