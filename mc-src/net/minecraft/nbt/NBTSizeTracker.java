package net.minecraft.nbt;

public class NBTSizeTracker
{
    public static final NBTSizeTracker field_152451_a = new NBTSizeTracker(0L)
    {
        private static final String __OBFID = "CL_00001902";
        public void func_152450_a(long p_152450_1_) {}
    };
    private final long field_152452_b;
    private long field_152453_c;
    private static final String __OBFID = "CL_00001903";

    public NBTSizeTracker(long p_i1203_1_)
    {
        this.field_152452_b = p_i1203_1_;
    }

    public void func_152450_a(long p_152450_1_)
    {
        this.field_152453_c += p_152450_1_ / 8L;

        if (this.field_152453_c > this.field_152452_b)
        {
            throw new RuntimeException("Tried to read NBT tag that was too big; tried to allocate: " + this.field_152453_c + "bytes where max allowed: " + this.field_152452_b);
        }
    }

    /*
     * UTF8 is not a simple encoding system, each character can be either
     * 1, 2, or 3 bytes. Depending on where it's numerical value falls.
     * We have to count up each character individually to see the true
     * length of the data.
     *
     * Basic concept is that it uses the MSB of each byte as a 'read more' signal.
     * So it has to shift each 7-bit segment.
     *
     * This will accurately count the correct byte length to encode this string, plus the 2 bytes for it's length prefix.
     */
    public static void readUTF(NBTSizeTracker tracker, String data)
    {
        tracker.func_152450_a(16); //Header length
        if (data == null)
            return;

        int len = data.length();
        int utflen = 0;

        for (int i = 0; i < len; i++)
        {
            int c = data.charAt(i);
            if ((c >= 0x0001) && (c <= 0x007F)) utflen += 1;
            else if (c > 0x07FF)                utflen += 3;
            else                                utflen += 2;
        }
        tracker.func_152450_a(8 * utflen);
    }
}