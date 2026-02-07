package net.minecraft.dispenser;

public class PositionImpl implements IPosition
{
    protected final double x;
    protected final double y;
    protected final double z;
    private static final String __OBFID = "CL_00001208";

    public PositionImpl(double p_i1368_1_, double p_i1368_3_, double p_i1368_5_)
    {
        this.x = p_i1368_1_;
        this.y = p_i1368_3_;
        this.z = p_i1368_5_;
    }

    public double getX()
    {
        return this.x;
    }

    public double getY()
    {
        return this.y;
    }

    public double getZ()
    {
        return this.z;
    }
}