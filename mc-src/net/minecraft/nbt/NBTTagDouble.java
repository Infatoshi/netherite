package net.minecraft.nbt;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import net.minecraft.util.MathHelper;

public class NBTTagDouble extends NBTBase.NBTPrimitive
{
    /** The double value for the tag. */
    private double data;
    private static final String __OBFID = "CL_00001218";

    NBTTagDouble() {}

    public NBTTagDouble(double p_i45130_1_)
    {
        this.data = p_i45130_1_;
    }

    /**
     * Write the actual data contents of the tag, implemented in NBT extension classes
     */
    void write(DataOutput p_74734_1_) throws IOException
    {
        p_74734_1_.writeDouble(this.data);
    }

    void func_152446_a(DataInput p_152446_1_, int p_152446_2_, NBTSizeTracker p_152446_3_) throws IOException
    {
        p_152446_3_.func_152450_a(64L);
        this.data = p_152446_1_.readDouble();
    }

    /**
     * Gets the type byte for the tag.
     */
    public byte getId()
    {
        return (byte)6;
    }

    public String toString()
    {
        return "" + this.data + "d";
    }

    /**
     * Creates a clone of the tag.
     */
    public NBTBase copy()
    {
        return new NBTTagDouble(this.data);
    }

    public boolean equals(Object p_equals_1_)
    {
        if (super.equals(p_equals_1_))
        {
            NBTTagDouble nbttagdouble = (NBTTagDouble)p_equals_1_;
            return this.data == nbttagdouble.data;
        }
        else
        {
            return false;
        }
    }

    public int hashCode()
    {
        long i = Double.doubleToLongBits(this.data);
        return super.hashCode() ^ (int)(i ^ i >>> 32);
    }

    public long func_150291_c()
    {
        return (long)Math.floor(this.data);
    }

    public int func_150287_d()
    {
        return MathHelper.floor_double(this.data);
    }

    public short func_150289_e()
    {
        return (short)(MathHelper.floor_double(this.data) & 65535);
    }

    public byte func_150290_f()
    {
        return (byte)(MathHelper.floor_double(this.data) & 255);
    }

    public double func_150286_g()
    {
        return this.data;
    }

    public float func_150288_h()
    {
        return (float)this.data;
    }
}