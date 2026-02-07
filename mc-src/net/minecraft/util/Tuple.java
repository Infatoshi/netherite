package net.minecraft.util;

public class Tuple
{
    /** First Object in the Tuple */
    private Object first;
    /** Second Object in the Tuple */
    private Object second;
    private static final String __OBFID = "CL_00001502";

    public Tuple(Object p_i1555_1_, Object p_i1555_2_)
    {
        this.first = p_i1555_1_;
        this.second = p_i1555_2_;
    }

    /**
     * Get the first Object in the Tuple
     */
    public Object getFirst()
    {
        return this.first;
    }

    /**
     * Get the second Object in the Tuple
     */
    public Object getSecond()
    {
        return this.second;
    }
}