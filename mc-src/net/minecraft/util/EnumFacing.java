package net.minecraft.util;

public enum EnumFacing
{
    DOWN(0, 1, 0, -1, 0),
    UP(1, 0, 0, 1, 0),
    NORTH(2, 3, 0, 0, -1),
    SOUTH(3, 2, 0, 0, 1),
    EAST(4, 5, -1, 0, 0),
    WEST(5, 4, 1, 0, 0);
    /** Face order for D-U-N-S-E-W. */
    private final int order_a;
    /** Face order for U-D-S-N-W-E. */
    private final int order_b;
    private final int frontOffsetX;
    private final int frontOffsetY;
    private final int frontOffsetZ;
    /** List of all values in EnumFacing. Order is D-U-N-S-E-W. */
    private static final EnumFacing[] faceList = new EnumFacing[6];

    private static final String __OBFID = "CL_00001201";

    private EnumFacing(int p_i1367_3_, int p_i1367_4_, int p_i1367_5_, int p_i1367_6_, int p_i1367_7_)
    {
        this.order_a = p_i1367_3_;
        this.order_b = p_i1367_4_;
        this.frontOffsetX = p_i1367_5_;
        this.frontOffsetY = p_i1367_6_;
        this.frontOffsetZ = p_i1367_7_;
    }

    /**
     * Returns a offset that addresses the block in front of this facing.
     */
    public int getFrontOffsetX()
    {
        return this.frontOffsetX;
    }

    public int getFrontOffsetY()
    {
        return this.frontOffsetY;
    }

    /**
     * Returns a offset that addresses the block in front of this facing.
     */
    public int getFrontOffsetZ()
    {
        return this.frontOffsetZ;
    }

    /**
     * Returns the facing that represents the block in front of it.
     */
    public static EnumFacing getFront(int p_82600_0_)
    {
        return faceList[p_82600_0_ % faceList.length];
    }

    static
    {
        EnumFacing[] var0 = values();
        int var1 = var0.length;

        for (int var2 = 0; var2 < var1; ++var2)
        {
            EnumFacing var3 = var0[var2];
            faceList[var3.order_a] = var3;
        }
    }
}