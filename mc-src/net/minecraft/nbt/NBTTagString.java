package net.minecraft.nbt;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;

public class NBTTagString extends NBTBase
{
    /** The string value for the tag (cannot be empty). */
    private String data;
    private static final String __OBFID = "CL_00001228";

    public NBTTagString()
    {
        this.data = "";
    }

    public NBTTagString(String p_i1389_1_)
    {
        this.data = p_i1389_1_;

        if (p_i1389_1_ == null)
        {
            throw new IllegalArgumentException("Empty string not allowed");
        }
    }

    /**
     * Write the actual data contents of the tag, implemented in NBT extension classes
     */
    void write(DataOutput p_74734_1_) throws IOException
    {
        p_74734_1_.writeUTF(this.data);
    }

    void func_152446_a(DataInput p_152446_1_, int p_152446_2_, NBTSizeTracker p_152446_3_) throws IOException
    {
        this.data = p_152446_1_.readUTF();
        NBTSizeTracker.readUTF(p_152446_3_, data); // Forge: Correctly read String length including header.
    }

    /**
     * Gets the type byte for the tag.
     */
    public byte getId()
    {
        return (byte)8;
    }

    public String toString()
    {
        return "\"" + this.data + "\"";
    }

    /**
     * Creates a clone of the tag.
     */
    public NBTBase copy()
    {
        return new NBTTagString(this.data);
    }

    public boolean equals(Object p_equals_1_)
    {
        if (!super.equals(p_equals_1_))
        {
            return false;
        }
        else
        {
            NBTTagString nbttagstring = (NBTTagString)p_equals_1_;
            return this.data == null && nbttagstring.data == null || this.data != null && this.data.equals(nbttagstring.data);
        }
    }

    public int hashCode()
    {
        return super.hashCode() ^ this.data.hashCode();
    }

    public String func_150285_a_()
    {
        return this.data;
    }
}