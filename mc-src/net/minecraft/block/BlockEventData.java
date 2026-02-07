package net.minecraft.block;

public class BlockEventData
{
    private int coordX;
    private int coordY;
    private int coordZ;
    private Block field_151344_d;
    /** Different for each blockID */
    private int eventID;
    /** Different for each blockID, eventID */
    private int eventParameter;
    private static final String __OBFID = "CL_00000131";

    public BlockEventData(int p_i45362_1_, int p_i45362_2_, int p_i45362_3_, Block p_i45362_4_, int p_i45362_5_, int p_i45362_6_)
    {
        this.coordX = p_i45362_1_;
        this.coordY = p_i45362_2_;
        this.coordZ = p_i45362_3_;
        this.eventID = p_i45362_5_;
        this.eventParameter = p_i45362_6_;
        this.field_151344_d = p_i45362_4_;
    }

    public int func_151340_a()
    {
        return this.coordX;
    }

    public int func_151342_b()
    {
        return this.coordY;
    }

    public int func_151341_c()
    {
        return this.coordZ;
    }

    /**
     * Get the Event ID (different for each BlockID)
     */
    public int getEventID()
    {
        return this.eventID;
    }

    /**
     * Get the Event Parameter (different for each Block,EventID)
     */
    public int getEventParameter()
    {
        return this.eventParameter;
    }

    public Block getBlock()
    {
        return this.field_151344_d;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!(p_equals_1_ instanceof BlockEventData))
        {
            return false;
        }
        else
        {
            BlockEventData blockeventdata = (BlockEventData)p_equals_1_;
            return this.coordX == blockeventdata.coordX && this.coordY == blockeventdata.coordY && this.coordZ == blockeventdata.coordZ && this.eventID == blockeventdata.eventID && this.eventParameter == blockeventdata.eventParameter && this.field_151344_d == blockeventdata.field_151344_d;
        }
    }

    public String toString()
    {
        return "TE(" + this.coordX + "," + this.coordY + "," + this.coordZ + ")," + this.eventID + "," + this.eventParameter + "," + this.field_151344_d;
    }
}