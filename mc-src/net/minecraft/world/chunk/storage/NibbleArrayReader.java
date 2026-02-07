package net.minecraft.world.chunk.storage;

public class NibbleArrayReader
{
    public final byte[] data;
    private final int depthBits;
    private final int depthBitsPlusFour;
    private static final String __OBFID = "CL_00000376";

    public NibbleArrayReader(byte[] p_i1998_1_, int p_i1998_2_)
    {
        this.data = p_i1998_1_;
        this.depthBits = p_i1998_2_;
        this.depthBitsPlusFour = p_i1998_2_ + 4;
    }

    public int get(int p_76686_1_, int p_76686_2_, int p_76686_3_)
    {
        int l = p_76686_1_ << this.depthBitsPlusFour | p_76686_3_ << this.depthBits | p_76686_2_;
        int i1 = l >> 1;
        int j1 = l & 1;
        return j1 == 0 ? this.data[i1] & 15 : this.data[i1] >> 4 & 15;
    }
}