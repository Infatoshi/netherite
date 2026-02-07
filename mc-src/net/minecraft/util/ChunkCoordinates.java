package net.minecraft.util;

public class ChunkCoordinates implements Comparable
{
    public int posX;
    /** the y coordinate */
    public int posY;
    /** the z coordinate */
    public int posZ;
    private static final String __OBFID = "CL_00001555";

    public ChunkCoordinates() {}

    public ChunkCoordinates(int p_i1354_1_, int p_i1354_2_, int p_i1354_3_)
    {
        this.posX = p_i1354_1_;
        this.posY = p_i1354_2_;
        this.posZ = p_i1354_3_;
    }

    public ChunkCoordinates(ChunkCoordinates p_i1355_1_)
    {
        this.posX = p_i1355_1_.posX;
        this.posY = p_i1355_1_.posY;
        this.posZ = p_i1355_1_.posZ;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!(p_equals_1_ instanceof ChunkCoordinates))
        {
            return false;
        }
        else
        {
            ChunkCoordinates chunkcoordinates = (ChunkCoordinates)p_equals_1_;
            return this.posX == chunkcoordinates.posX && this.posY == chunkcoordinates.posY && this.posZ == chunkcoordinates.posZ;
        }
    }

    public int hashCode()
    {
        return this.posX + this.posZ << 8 + this.posY << 16;
    }

    public int compareTo(ChunkCoordinates p_compareTo_1_)
    {
        return this.posY == p_compareTo_1_.posY ? (this.posZ == p_compareTo_1_.posZ ? this.posX - p_compareTo_1_.posX : this.posZ - p_compareTo_1_.posZ) : this.posY - p_compareTo_1_.posY;
    }

    public void set(int p_71571_1_, int p_71571_2_, int p_71571_3_)
    {
        this.posX = p_71571_1_;
        this.posY = p_71571_2_;
        this.posZ = p_71571_3_;
    }

    /**
     * Returns the squared distance between this coordinates and the coordinates given as argument.
     */
    public float getDistanceSquared(int p_71569_1_, int p_71569_2_, int p_71569_3_)
    {
        float f = (float)(this.posX - p_71569_1_);
        float f1 = (float)(this.posY - p_71569_2_);
        float f2 = (float)(this.posZ - p_71569_3_);
        return f * f + f1 * f1 + f2 * f2;
    }

    /**
     * Return the squared distance between this coordinates and the ChunkCoordinates given as argument.
     */
    public float getDistanceSquaredToChunkCoordinates(ChunkCoordinates p_82371_1_)
    {
        return this.getDistanceSquared(p_82371_1_.posX, p_82371_1_.posY, p_82371_1_.posZ);
    }

    public String toString()
    {
        return "Pos{x=" + this.posX + ", y=" + this.posY + ", z=" + this.posZ + '}';
    }

    public int compareTo(Object p_compareTo_1_)
    {
        return this.compareTo((ChunkCoordinates)p_compareTo_1_);
    }
}