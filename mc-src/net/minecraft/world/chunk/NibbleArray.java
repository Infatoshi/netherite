package net.minecraft.world.chunk;

public class NibbleArray
{
    /**
     * Byte array of data stored in this holder. Possibly a light map or some chunk data. Data is accessed in 4-bit
     * pieces.
     */
    public final byte[] data;
    /** Log base 2 of the chunk height (128); applied as a shift on Z coordinate */
    private final int depthBits;
    /** Log base 2 of the chunk height (128) * width (16); applied as a shift on X coordinate */
    private final int depthBitsPlusFour;
    private static final String __OBFID = "CL_00000371";

    public NibbleArray(int p_i1992_1_, int p_i1992_2_)
    {
        this.data = new byte[p_i1992_1_ >> 1];
        this.depthBits = p_i1992_2_;
        this.depthBitsPlusFour = p_i1992_2_ + 4;
    }

    public NibbleArray(byte[] p_i1993_1_, int p_i1993_2_)
    {
        this.data = p_i1993_1_;
        this.depthBits = p_i1993_2_;
        this.depthBitsPlusFour = p_i1993_2_ + 4;
    }

    /**
     * Returns the nibble of data corresponding to the passed in x, y, z. y is at most 6 bits, z is at most 4.
     */
    public int get(int p_76582_1_, int p_76582_2_, int p_76582_3_)
    {
        int l = p_76582_2_ << this.depthBitsPlusFour | p_76582_3_ << this.depthBits | p_76582_1_;
        int i1 = l >> 1;
        int j1 = l & 1;
        return j1 == 0 ? this.data[i1] & 15 : this.data[i1] >> 4 & 15;
    }

    /**
     * Arguments are x, y, z, val. Sets the nibble of data at x << 11 | z << 7 | y to val.
     */
    public void set(int p_76581_1_, int p_76581_2_, int p_76581_3_, int p_76581_4_)
    {
        int i1 = p_76581_2_ << this.depthBitsPlusFour | p_76581_3_ << this.depthBits | p_76581_1_;
        int j1 = i1 >> 1;
        int k1 = i1 & 1;

        if (k1 == 0)
        {
            this.data[j1] = (byte)(this.data[j1] & 240 | p_76581_4_ & 15);
        }
        else
        {
            this.data[j1] = (byte)(this.data[j1] & 15 | (p_76581_4_ & 15) << 4);
        }
    }
}