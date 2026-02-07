package net.minecraft.nbt;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public class NBTTagList extends NBTBase
{
    /** The array list containing the tags encapsulated in this list. */
    private List tagList = new ArrayList();
    /** The type byte for the tags in the list - they must all be of the same type. */
    private byte tagType = 0;
    private static final String __OBFID = "CL_00001224";

    /**
     * Write the actual data contents of the tag, implemented in NBT extension classes
     */
    void write(DataOutput p_74734_1_) throws IOException
    {
        if (!this.tagList.isEmpty())
        {
            this.tagType = ((NBTBase)this.tagList.get(0)).getId();
        }
        else
        {
            this.tagType = 0;
        }

        p_74734_1_.writeByte(this.tagType);
        p_74734_1_.writeInt(this.tagList.size());

        for (int i = 0; i < this.tagList.size(); ++i)
        {
            ((NBTBase)this.tagList.get(i)).write(p_74734_1_);
        }
    }

    void func_152446_a(DataInput p_152446_1_, int p_152446_2_, NBTSizeTracker p_152446_3_) throws IOException
    {
        if (p_152446_2_ > 512)
        {
            throw new RuntimeException("Tried to read NBT tag with too high complexity, depth > 512");
        }
        else
        {
            p_152446_3_.func_152450_a(8L);
            this.tagType = p_152446_1_.readByte();
            p_152446_3_.func_152450_a(32); //Forge: Count the length as well
            int j = p_152446_1_.readInt();
            this.tagList = new ArrayList();

            for (int k = 0; k < j; ++k)
            {
                p_152446_3_.func_152450_a(32); //Forge: 4 extra bytes for the object allocation.
                NBTBase nbtbase = NBTBase.func_150284_a(this.tagType);
                nbtbase.func_152446_a(p_152446_1_, p_152446_2_ + 1, p_152446_3_);
                this.tagList.add(nbtbase);
            }
        }
    }

    /**
     * Gets the type byte for the tag.
     */
    public byte getId()
    {
        return (byte)9;
    }

    public String toString()
    {
        String s = "[";
        int i = 0;

        for (Iterator iterator = this.tagList.iterator(); iterator.hasNext(); ++i)
        {
            NBTBase nbtbase = (NBTBase)iterator.next();
            s = s + "" + i + ':' + nbtbase + ',';
        }

        return s + "]";
    }

    /**
     * Adds the provided tag to the end of the list. There is no check to verify this tag is of the same type as any
     * previous tag.
     */
    public void appendTag(NBTBase p_74742_1_)
    {
        if (this.tagType == 0)
        {
            this.tagType = p_74742_1_.getId();
        }
        else if (this.tagType != p_74742_1_.getId())
        {
            System.err.println("WARNING: Adding mismatching tag types to tag list");
            return;
        }

        this.tagList.add(p_74742_1_);
    }

    public void func_150304_a(int p_150304_1_, NBTBase p_150304_2_)
    {
        if (p_150304_1_ >= 0 && p_150304_1_ < this.tagList.size())
        {
            if (this.tagType == 0)
            {
                this.tagType = p_150304_2_.getId();
            }
            else if (this.tagType != p_150304_2_.getId())
            {
                System.err.println("WARNING: Adding mismatching tag types to tag list");
                return;
            }

            this.tagList.set(p_150304_1_, p_150304_2_);
        }
        else
        {
            System.err.println("WARNING: index out of bounds to set tag in tag list");
        }
    }

    /**
     * Removes a tag at the given index.
     */
    public NBTBase removeTag(int p_74744_1_)
    {
        return (NBTBase)this.tagList.remove(p_74744_1_);
    }

    /**
     * Retrieves the NBTTagCompound at the specified index in the list
     */
    public NBTTagCompound getCompoundTagAt(int p_150305_1_)
    {
        if (p_150305_1_ >= 0 && p_150305_1_ < this.tagList.size())
        {
            NBTBase nbtbase = (NBTBase)this.tagList.get(p_150305_1_);
            return nbtbase.getId() == 10 ? (NBTTagCompound)nbtbase : new NBTTagCompound();
        }
        else
        {
            return new NBTTagCompound();
        }
    }

    public int[] func_150306_c(int p_150306_1_)
    {
        if (p_150306_1_ >= 0 && p_150306_1_ < this.tagList.size())
        {
            NBTBase nbtbase = (NBTBase)this.tagList.get(p_150306_1_);
            return nbtbase.getId() == 11 ? ((NBTTagIntArray)nbtbase).func_150302_c() : new int[0];
        }
        else
        {
            return new int[0];
        }
    }

    public double func_150309_d(int p_150309_1_)
    {
        if (p_150309_1_ >= 0 && p_150309_1_ < this.tagList.size())
        {
            NBTBase nbtbase = (NBTBase)this.tagList.get(p_150309_1_);
            return nbtbase.getId() == 6 ? ((NBTTagDouble)nbtbase).func_150286_g() : 0.0D;
        }
        else
        {
            return 0.0D;
        }
    }

    public float func_150308_e(int p_150308_1_)
    {
        if (p_150308_1_ >= 0 && p_150308_1_ < this.tagList.size())
        {
            NBTBase nbtbase = (NBTBase)this.tagList.get(p_150308_1_);
            return nbtbase.getId() == 5 ? ((NBTTagFloat)nbtbase).func_150288_h() : 0.0F;
        }
        else
        {
            return 0.0F;
        }
    }

    /**
     * Retrieves the tag String value at the specified index in the list
     */
    public String getStringTagAt(int p_150307_1_)
    {
        if (p_150307_1_ >= 0 && p_150307_1_ < this.tagList.size())
        {
            NBTBase nbtbase = (NBTBase)this.tagList.get(p_150307_1_);
            return nbtbase.getId() == 8 ? nbtbase.func_150285_a_() : nbtbase.toString();
        }
        else
        {
            return "";
        }
    }

    /**
     * Returns the number of tags in the list.
     */
    public int tagCount()
    {
        return this.tagList.size();
    }

    /**
     * Creates a clone of the tag.
     */
    public NBTBase copy()
    {
        NBTTagList nbttaglist = new NBTTagList();
        nbttaglist.tagType = this.tagType;
        Iterator iterator = this.tagList.iterator();

        while (iterator.hasNext())
        {
            NBTBase nbtbase = (NBTBase)iterator.next();
            NBTBase nbtbase1 = nbtbase.copy();
            nbttaglist.tagList.add(nbtbase1);
        }

        return nbttaglist;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (super.equals(p_equals_1_))
        {
            NBTTagList nbttaglist = (NBTTagList)p_equals_1_;

            if (this.tagType == nbttaglist.tagType)
            {
                return this.tagList.equals(nbttaglist.tagList);
            }
        }

        return false;
    }

    public int hashCode()
    {
        return super.hashCode() ^ this.tagList.hashCode();
    }

    public int func_150303_d()
    {
        return this.tagType;
    }
}