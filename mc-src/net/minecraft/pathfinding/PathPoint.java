package net.minecraft.pathfinding;

import net.minecraft.util.MathHelper;

public class PathPoint
{
    /** The x coordinate of this point */
    public final int xCoord;
    /** The y coordinate of this point */
    public final int yCoord;
    /** The z coordinate of this point */
    public final int zCoord;
    /** A hash of the coordinates used to identify this point */
    private final int hash;
    /** The index of this point in its assigned path */
    int index = -1;
    /** The distance along the path to this point */
    float totalPathDistance;
    /** The linear distance to the next point */
    float distanceToNext;
    /** The distance to the target */
    float distanceToTarget;
    /** The point preceding this in its assigned path */
    PathPoint previous;
    /** Indicates this is the origin */
    public boolean isFirst;
    private static final String __OBFID = "CL_00000574";

    public PathPoint(int p_i2135_1_, int p_i2135_2_, int p_i2135_3_)
    {
        this.xCoord = p_i2135_1_;
        this.yCoord = p_i2135_2_;
        this.zCoord = p_i2135_3_;
        this.hash = makeHash(p_i2135_1_, p_i2135_2_, p_i2135_3_);
    }

    public static int makeHash(int p_75830_0_, int p_75830_1_, int p_75830_2_)
    {
        return p_75830_1_ & 255 | (p_75830_0_ & 32767) << 8 | (p_75830_2_ & 32767) << 24 | (p_75830_0_ < 0 ? Integer.MIN_VALUE : 0) | (p_75830_2_ < 0 ? 32768 : 0);
    }

    /**
     * Returns the linear distance to another path point
     */
    public float distanceTo(PathPoint p_75829_1_)
    {
        float f = (float)(p_75829_1_.xCoord - this.xCoord);
        float f1 = (float)(p_75829_1_.yCoord - this.yCoord);
        float f2 = (float)(p_75829_1_.zCoord - this.zCoord);
        return MathHelper.sqrt_float(f * f + f1 * f1 + f2 * f2);
    }

    /**
     * Returns the squared distance to another path point
     */
    public float distanceToSquared(PathPoint p_75832_1_)
    {
        float f = (float)(p_75832_1_.xCoord - this.xCoord);
        float f1 = (float)(p_75832_1_.yCoord - this.yCoord);
        float f2 = (float)(p_75832_1_.zCoord - this.zCoord);
        return f * f + f1 * f1 + f2 * f2;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!(p_equals_1_ instanceof PathPoint))
        {
            return false;
        }
        else
        {
            PathPoint pathpoint = (PathPoint)p_equals_1_;
            return this.hash == pathpoint.hash && this.xCoord == pathpoint.xCoord && this.yCoord == pathpoint.yCoord && this.zCoord == pathpoint.zCoord;
        }
    }

    public int hashCode()
    {
        return this.hash;
    }

    /**
     * Returns true if this point has already been assigned to a path
     */
    public boolean isAssigned()
    {
        return this.index >= 0;
    }

    public String toString()
    {
        return this.xCoord + ", " + this.yCoord + ", " + this.zCoord;
    }
}