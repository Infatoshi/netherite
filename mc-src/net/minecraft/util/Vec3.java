package net.minecraft.util;

public class Vec3
{
    /** X coordinate of Vec3D */
    public double xCoord;
    /** Y coordinate of Vec3D */
    public double yCoord;
    /** Z coordinate of Vec3D */
    public double zCoord;
    private static final String __OBFID = "CL_00000612";

    /**
     * Static method for creating a new Vec3D given the three x,y,z values. This is only called from the other static
     * method which creates and places it in the list.
     */
    public static Vec3 createVectorHelper(double p_72443_0_, double p_72443_2_, double p_72443_4_)
    {
        return new Vec3(p_72443_0_, p_72443_2_, p_72443_4_);
    }

    protected Vec3(double p_i1108_1_, double p_i1108_3_, double p_i1108_5_)
    {
        if (p_i1108_1_ == -0.0D)
        {
            p_i1108_1_ = 0.0D;
        }

        if (p_i1108_3_ == -0.0D)
        {
            p_i1108_3_ = 0.0D;
        }

        if (p_i1108_5_ == -0.0D)
        {
            p_i1108_5_ = 0.0D;
        }

        this.xCoord = p_i1108_1_;
        this.yCoord = p_i1108_3_;
        this.zCoord = p_i1108_5_;
    }

    /**
     * Sets the x,y,z components of the vector as specified.
     */
    protected Vec3 setComponents(double p_72439_1_, double p_72439_3_, double p_72439_5_)
    {
        this.xCoord = p_72439_1_;
        this.yCoord = p_72439_3_;
        this.zCoord = p_72439_5_;
        return this;
    }

    /**
     * Returns a new vector with the result of the specified vector minus this.
     */
    public Vec3 subtract(Vec3 p_72444_1_)
    {
        /**
         * Static method for creating a new Vec3D given the three x,y,z values. This is only called from the other
         * static method which creates and places it in the list.
         */
        return createVectorHelper(p_72444_1_.xCoord - this.xCoord, p_72444_1_.yCoord - this.yCoord, p_72444_1_.zCoord - this.zCoord);
    }

    /**
     * Normalizes the vector to a length of 1 (except if it is the zero vector)
     */
    public Vec3 normalize()
    {
        double d0 = (double)MathHelper.sqrt_double(this.xCoord * this.xCoord + this.yCoord * this.yCoord + this.zCoord * this.zCoord);
        return d0 < 1.0E-4D ? createVectorHelper(0.0D, 0.0D, 0.0D) : createVectorHelper(this.xCoord / d0, this.yCoord / d0, this.zCoord / d0);
    }

    public double dotProduct(Vec3 p_72430_1_)
    {
        return this.xCoord * p_72430_1_.xCoord + this.yCoord * p_72430_1_.yCoord + this.zCoord * p_72430_1_.zCoord;
    }

    /**
     * Returns a new vector with the result of this vector x the specified vector.
     */
    public Vec3 crossProduct(Vec3 p_72431_1_)
    {
        /**
         * Static method for creating a new Vec3D given the three x,y,z values. This is only called from the other
         * static method which creates and places it in the list.
         */
        return createVectorHelper(this.yCoord * p_72431_1_.zCoord - this.zCoord * p_72431_1_.yCoord, this.zCoord * p_72431_1_.xCoord - this.xCoord * p_72431_1_.zCoord, this.xCoord * p_72431_1_.yCoord - this.yCoord * p_72431_1_.xCoord);
    }

    /**
     * Adds the specified x,y,z vector components to this vector and returns the resulting vector. Does not change this
     * vector.
     */
    public Vec3 addVector(double p_72441_1_, double p_72441_3_, double p_72441_5_)
    {
        /**
         * Static method for creating a new Vec3D given the three x,y,z values. This is only called from the other
         * static method which creates and places it in the list.
         */
        return createVectorHelper(this.xCoord + p_72441_1_, this.yCoord + p_72441_3_, this.zCoord + p_72441_5_);
    }

    /**
     * Euclidean distance between this and the specified vector, returned as double.
     */
    public double distanceTo(Vec3 p_72438_1_)
    {
        double d0 = p_72438_1_.xCoord - this.xCoord;
        double d1 = p_72438_1_.yCoord - this.yCoord;
        double d2 = p_72438_1_.zCoord - this.zCoord;
        return (double)MathHelper.sqrt_double(d0 * d0 + d1 * d1 + d2 * d2);
    }

    /**
     * The square of the Euclidean distance between this and the specified vector.
     */
    public double squareDistanceTo(Vec3 p_72436_1_)
    {
        double d0 = p_72436_1_.xCoord - this.xCoord;
        double d1 = p_72436_1_.yCoord - this.yCoord;
        double d2 = p_72436_1_.zCoord - this.zCoord;
        return d0 * d0 + d1 * d1 + d2 * d2;
    }

    /**
     * The square of the Euclidean distance between this and the vector of x,y,z components passed in.
     */
    public double squareDistanceTo(double p_72445_1_, double p_72445_3_, double p_72445_5_)
    {
        double d3 = p_72445_1_ - this.xCoord;
        double d4 = p_72445_3_ - this.yCoord;
        double d5 = p_72445_5_ - this.zCoord;
        return d3 * d3 + d4 * d4 + d5 * d5;
    }

    /**
     * Returns the length of the vector.
     */
    public double lengthVector()
    {
        return (double)MathHelper.sqrt_double(this.xCoord * this.xCoord + this.yCoord * this.yCoord + this.zCoord * this.zCoord);
    }

    /**
     * Returns a new vector with x value equal to the second parameter, along the line between this vector and the
     * passed in vector, or null if not possible.
     */
    public Vec3 getIntermediateWithXValue(Vec3 p_72429_1_, double p_72429_2_)
    {
        double d1 = p_72429_1_.xCoord - this.xCoord;
        double d2 = p_72429_1_.yCoord - this.yCoord;
        double d3 = p_72429_1_.zCoord - this.zCoord;

        if (d1 * d1 < 1.0000000116860974E-7D)
        {
            return null;
        }
        else
        {
            double d4 = (p_72429_2_ - this.xCoord) / d1;
            return d4 >= 0.0D && d4 <= 1.0D ? createVectorHelper(this.xCoord + d1 * d4, this.yCoord + d2 * d4, this.zCoord + d3 * d4) : null;
        }
    }

    /**
     * Returns a new vector with y value equal to the second parameter, along the line between this vector and the
     * passed in vector, or null if not possible.
     */
    public Vec3 getIntermediateWithYValue(Vec3 p_72435_1_, double p_72435_2_)
    {
        double d1 = p_72435_1_.xCoord - this.xCoord;
        double d2 = p_72435_1_.yCoord - this.yCoord;
        double d3 = p_72435_1_.zCoord - this.zCoord;

        if (d2 * d2 < 1.0000000116860974E-7D)
        {
            return null;
        }
        else
        {
            double d4 = (p_72435_2_ - this.yCoord) / d2;
            return d4 >= 0.0D && d4 <= 1.0D ? createVectorHelper(this.xCoord + d1 * d4, this.yCoord + d2 * d4, this.zCoord + d3 * d4) : null;
        }
    }

    /**
     * Returns a new vector with z value equal to the second parameter, along the line between this vector and the
     * passed in vector, or null if not possible.
     */
    public Vec3 getIntermediateWithZValue(Vec3 p_72434_1_, double p_72434_2_)
    {
        double d1 = p_72434_1_.xCoord - this.xCoord;
        double d2 = p_72434_1_.yCoord - this.yCoord;
        double d3 = p_72434_1_.zCoord - this.zCoord;

        if (d3 * d3 < 1.0000000116860974E-7D)
        {
            return null;
        }
        else
        {
            double d4 = (p_72434_2_ - this.zCoord) / d3;
            return d4 >= 0.0D && d4 <= 1.0D ? createVectorHelper(this.xCoord + d1 * d4, this.yCoord + d2 * d4, this.zCoord + d3 * d4) : null;
        }
    }

    public String toString()
    {
        return "(" + this.xCoord + ", " + this.yCoord + ", " + this.zCoord + ")";
    }

    /**
     * Rotates the vector around the x axis by the specified angle.
     */
    public void rotateAroundX(float p_72440_1_)
    {
        float f1 = MathHelper.cos(p_72440_1_);
        float f2 = MathHelper.sin(p_72440_1_);
        double d0 = this.xCoord;
        double d1 = this.yCoord * (double)f1 + this.zCoord * (double)f2;
        double d2 = this.zCoord * (double)f1 - this.yCoord * (double)f2;
        this.setComponents(d0, d1, d2);
    }

    /**
     * Rotates the vector around the y axis by the specified angle.
     */
    public void rotateAroundY(float p_72442_1_)
    {
        float f1 = MathHelper.cos(p_72442_1_);
        float f2 = MathHelper.sin(p_72442_1_);
        double d0 = this.xCoord * (double)f1 + this.zCoord * (double)f2;
        double d1 = this.yCoord;
        double d2 = this.zCoord * (double)f1 - this.xCoord * (double)f2;
        this.setComponents(d0, d1, d2);
    }

    /**
     * Rotates the vector around the z axis by the specified angle.
     */
    public void rotateAroundZ(float p_72446_1_)
    {
        float f1 = MathHelper.cos(p_72446_1_);
        float f2 = MathHelper.sin(p_72446_1_);
        double d0 = this.xCoord * (double)f1 + this.yCoord * (double)f2;
        double d1 = this.yCoord * (double)f1 - this.xCoord * (double)f2;
        double d2 = this.zCoord;
        this.setComponents(d0, d1, d2);
    }
}