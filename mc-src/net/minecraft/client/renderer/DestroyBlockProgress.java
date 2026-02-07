package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public class DestroyBlockProgress
{
    /**
     * entity ID of the player associated with this partially destroyed Block. Used to identify the Blocks in the client
     * Renderer, max 1 per player on a server
     */
    private final int miningPlayerEntId;
    private final int partialBlockX;
    private final int partialBlockY;
    private final int partialBlockZ;
    /** damage ranges from 1 to 10. -1 causes the client to delete the partial block renderer. */
    private int partialBlockProgress;
    /** keeps track of how many ticks this PartiallyDestroyedBlock already exists */
    private int createdAtCloudUpdateTick;
    private static final String __OBFID = "CL_00001427";

    public DestroyBlockProgress(int p_i1511_1_, int p_i1511_2_, int p_i1511_3_, int p_i1511_4_)
    {
        this.miningPlayerEntId = p_i1511_1_;
        this.partialBlockX = p_i1511_2_;
        this.partialBlockY = p_i1511_3_;
        this.partialBlockZ = p_i1511_4_;
    }

    public int getPartialBlockX()
    {
        return this.partialBlockX;
    }

    public int getPartialBlockY()
    {
        return this.partialBlockY;
    }

    public int getPartialBlockZ()
    {
        return this.partialBlockZ;
    }

    /**
     * inserts damage value into this partially destroyed Block. -1 causes client renderer to delete it, otherwise
     * ranges from 1 to 10
     */
    public void setPartialBlockDamage(int p_73107_1_)
    {
        if (p_73107_1_ > 10)
        {
            p_73107_1_ = 10;
        }

        this.partialBlockProgress = p_73107_1_;
    }

    public int getPartialBlockDamage()
    {
        return this.partialBlockProgress;
    }

    /**
     * saves the current Cloud update tick into the PartiallyDestroyedBlock
     */
    public void setCloudUpdateTick(int p_82744_1_)
    {
        this.createdAtCloudUpdateTick = p_82744_1_;
    }

    /**
     * retrieves the 'date' at which the PartiallyDestroyedBlock was created
     */
    public int getCreationCloudUpdateTick()
    {
        return this.createdAtCloudUpdateTick;
    }
}