package net.minecraft.world.gen.structure;

import net.minecraft.nbt.NBTTagIntArray;

public class StructureBoundingBox
{
    /** The first x coordinate of a bounding box. */
    public int minX;
    /** The first y coordinate of a bounding box. */
    public int minY;
    /** The first z coordinate of a bounding box. */
    public int minZ;
    /** The second x coordinate of a bounding box. */
    public int maxX;
    /** The second y coordinate of a bounding box. */
    public int maxY;
    /** The second z coordinate of a bounding box. */
    public int maxZ;
    private static final String __OBFID = "CL_00000442";

    public StructureBoundingBox() {}

    public StructureBoundingBox(int[] p_i43000_1_)
    {
        if (p_i43000_1_.length == 6)
        {
            this.minX = p_i43000_1_[0];
            this.minY = p_i43000_1_[1];
            this.minZ = p_i43000_1_[2];
            this.maxX = p_i43000_1_[3];
            this.maxY = p_i43000_1_[4];
            this.maxZ = p_i43000_1_[5];
        }
    }

    /**
     * returns a new StructureBoundingBox with MAX values
     */
    public static StructureBoundingBox getNewBoundingBox()
    {
        return new StructureBoundingBox(Integer.MAX_VALUE, Integer.MAX_VALUE, Integer.MAX_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE);
    }

    /**
     * used to project a possible new component Bounding Box - to check if it would cut anything already spawned
     */
    public static StructureBoundingBox getComponentToAddBoundingBox(int p_78889_0_, int p_78889_1_, int p_78889_2_, int p_78889_3_, int p_78889_4_, int p_78889_5_, int p_78889_6_, int p_78889_7_, int p_78889_8_, int p_78889_9_)
    {
        switch (p_78889_9_)
        {
            case 0:
                return new StructureBoundingBox(p_78889_0_ + p_78889_3_, p_78889_1_ + p_78889_4_, p_78889_2_ + p_78889_5_, p_78889_0_ + p_78889_6_ - 1 + p_78889_3_, p_78889_1_ + p_78889_7_ - 1 + p_78889_4_, p_78889_2_ + p_78889_8_ - 1 + p_78889_5_);
            case 1:
                return new StructureBoundingBox(p_78889_0_ - p_78889_8_ + 1 + p_78889_5_, p_78889_1_ + p_78889_4_, p_78889_2_ + p_78889_3_, p_78889_0_ + p_78889_5_, p_78889_1_ + p_78889_7_ - 1 + p_78889_4_, p_78889_2_ + p_78889_6_ - 1 + p_78889_3_);
            case 2:
                return new StructureBoundingBox(p_78889_0_ + p_78889_3_, p_78889_1_ + p_78889_4_, p_78889_2_ - p_78889_8_ + 1 + p_78889_5_, p_78889_0_ + p_78889_6_ - 1 + p_78889_3_, p_78889_1_ + p_78889_7_ - 1 + p_78889_4_, p_78889_2_ + p_78889_5_);
            case 3:
                return new StructureBoundingBox(p_78889_0_ + p_78889_5_, p_78889_1_ + p_78889_4_, p_78889_2_ + p_78889_3_, p_78889_0_ + p_78889_8_ - 1 + p_78889_5_, p_78889_1_ + p_78889_7_ - 1 + p_78889_4_, p_78889_2_ + p_78889_6_ - 1 + p_78889_3_);
            default:
                return new StructureBoundingBox(p_78889_0_ + p_78889_3_, p_78889_1_ + p_78889_4_, p_78889_2_ + p_78889_5_, p_78889_0_ + p_78889_6_ - 1 + p_78889_3_, p_78889_1_ + p_78889_7_ - 1 + p_78889_4_, p_78889_2_ + p_78889_8_ - 1 + p_78889_5_);
        }
    }

    public StructureBoundingBox(StructureBoundingBox p_i2031_1_)
    {
        this.minX = p_i2031_1_.minX;
        this.minY = p_i2031_1_.minY;
        this.minZ = p_i2031_1_.minZ;
        this.maxX = p_i2031_1_.maxX;
        this.maxY = p_i2031_1_.maxY;
        this.maxZ = p_i2031_1_.maxZ;
    }

    public StructureBoundingBox(int p_i2032_1_, int p_i2032_2_, int p_i2032_3_, int p_i2032_4_, int p_i2032_5_, int p_i2032_6_)
    {
        this.minX = p_i2032_1_;
        this.minY = p_i2032_2_;
        this.minZ = p_i2032_3_;
        this.maxX = p_i2032_4_;
        this.maxY = p_i2032_5_;
        this.maxZ = p_i2032_6_;
    }

    public StructureBoundingBox(int p_i2033_1_, int p_i2033_2_, int p_i2033_3_, int p_i2033_4_)
    {
        this.minX = p_i2033_1_;
        this.minZ = p_i2033_2_;
        this.maxX = p_i2033_3_;
        this.maxZ = p_i2033_4_;
        this.minY = 1;
        this.maxY = 512;
    }

    /**
     * Returns whether the given bounding box intersects with this one. Args: structureboundingbox
     */
    public boolean intersectsWith(StructureBoundingBox p_78884_1_)
    {
        return this.maxX >= p_78884_1_.minX && this.minX <= p_78884_1_.maxX && this.maxZ >= p_78884_1_.minZ && this.minZ <= p_78884_1_.maxZ && this.maxY >= p_78884_1_.minY && this.minY <= p_78884_1_.maxY;
    }

    /**
     * Discover if a coordinate is inside the bounding box area.
     */
    public boolean intersectsWith(int p_78885_1_, int p_78885_2_, int p_78885_3_, int p_78885_4_)
    {
        return this.maxX >= p_78885_1_ && this.minX <= p_78885_3_ && this.maxZ >= p_78885_2_ && this.minZ <= p_78885_4_;
    }

    /**
     * Expands a bounding box's dimensions to include the supplied bounding box.
     */
    public void expandTo(StructureBoundingBox p_78888_1_)
    {
        this.minX = Math.min(this.minX, p_78888_1_.minX);
        this.minY = Math.min(this.minY, p_78888_1_.minY);
        this.minZ = Math.min(this.minZ, p_78888_1_.minZ);
        this.maxX = Math.max(this.maxX, p_78888_1_.maxX);
        this.maxY = Math.max(this.maxY, p_78888_1_.maxY);
        this.maxZ = Math.max(this.maxZ, p_78888_1_.maxZ);
    }

    /**
     * Offsets the current bounding box by the specified coordinates. Args: x, y, z
     */
    public void offset(int p_78886_1_, int p_78886_2_, int p_78886_3_)
    {
        this.minX += p_78886_1_;
        this.minY += p_78886_2_;
        this.minZ += p_78886_3_;
        this.maxX += p_78886_1_;
        this.maxY += p_78886_2_;
        this.maxZ += p_78886_3_;
    }

    /**
     * Returns true if block is inside bounding box
     */
    public boolean isVecInside(int p_78890_1_, int p_78890_2_, int p_78890_3_)
    {
        return p_78890_1_ >= this.minX && p_78890_1_ <= this.maxX && p_78890_3_ >= this.minZ && p_78890_3_ <= this.maxZ && p_78890_2_ >= this.minY && p_78890_2_ <= this.maxY;
    }

    /**
     * Returns width of a bounding box
     */
    public int getXSize()
    {
        return this.maxX - this.minX + 1;
    }

    /**
     * Returns height of a bounding box
     */
    public int getYSize()
    {
        return this.maxY - this.minY + 1;
    }

    /**
     * Returns length of a bounding box
     */
    public int getZSize()
    {
        return this.maxZ - this.minZ + 1;
    }

    public int getCenterX()
    {
        return this.minX + (this.maxX - this.minX + 1) / 2;
    }

    public int getCenterY()
    {
        return this.minY + (this.maxY - this.minY + 1) / 2;
    }

    public int getCenterZ()
    {
        return this.minZ + (this.maxZ - this.minZ + 1) / 2;
    }

    public String toString()
    {
        return "(" + this.minX + ", " + this.minY + ", " + this.minZ + "; " + this.maxX + ", " + this.maxY + ", " + this.maxZ + ")";
    }

    public NBTTagIntArray func_151535_h()
    {
        return new NBTTagIntArray(new int[] {this.minX, this.minY, this.minZ, this.maxX, this.maxY, this.maxZ});
    }
}