package net.minecraft.nbt;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import net.minecraft.util.MathHelper;

public class NBTTagFloat extends NBTBase.NBTPrimitive
{
    /** The float value for the tag. */
    private float data;
    private static final String __OBFID = "CL_00001220";

    NBTTagFloat() {}

    public NBTTagFloat(float p_i45131_1_)
    {
        this.data = p_i45131_1_;
    }

    /**
     * Write the actual data contents of the tag, implemented in NBT extension classes
     */
    void write(DataOutput p_74734_1_) throws IOException
    {
        p_74734_1_.writeFloat(this.data);
    }

    void func_152446_a(DataInput p_152446_1_, int p_152446_2_, NBTSizeTracker p_152446_3_) throws IOException
    {
        p_152446_3_.func_152450_a(32L);
        this.data = p_152446_1_.readFloat();
    }

    /**
     * Gets the type byte for the tag.
     */
    public byte getId()
    {
        return (byte)5;
    }

    public String toString()
    {
        return "" + this.data + "f";
    }

    /**
     * Creates a clone of the tag.
     */
    public NBTBase copy()
    {
        return new NBTTagFloat(this.data);
    }

    public boolean equals(Object p_equals_1_)
    {
        if (super.equals(p_equals_1_))
        {
            NBTTagFloat nbttagfloat = (NBTTagFloat)p_equals_1_;
            return this.data == nbttagfloat.data;
        }
        else
        {
            return false;
        }
    }

    public int hashCode()
    {
        return super.hashCode() ^ Float.floatToIntBits(this.data);
    }

    public long func_150291_c()
    {
        return (long)this.data;
    }

    public int func_150287_d()
    {
        return MathHelper.floor_float(this.data);
    }

    public short func_150289_e()
    {
        return (short)(MathHelper.floor_float(this.data) & 65535);
    }

    public byte func_150290_f()
    {
        return (byte)(MathHelper.floor_float(this.data) & 255);
    }

    public double func_150286_g()
    {
        return (double)this.data;
    }

    public float func_150288_h()
    {
        return this.data;
    }
}