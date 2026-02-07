package net.minecraft.network;

import com.google.common.collect.BiMap;
import io.netty.buffer.ByteBuf;
import java.io.IOException;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public abstract class Packet
{
    private static final Logger logger = LogManager.getLogger();
    private static final String __OBFID = "CL_00001272";

    /**
     * Returns a packet instance, given the params: BiMap<int, (Packet) Class> and (int) id
     */
    public static Packet generatePacket(BiMap p_148839_0_, int p_148839_1_)
    {
        try
        {
            Class oclass = (Class)p_148839_0_.get(Integer.valueOf(p_148839_1_));
            return oclass == null ? null : (Packet)oclass.newInstance();
        }
        catch (Exception exception)
        {
            logger.error("Couldn\'t create packet " + p_148839_1_, exception);
            return null;
        }
    }

    /**
     * Will write a byte array to supplied ByteBuf as a separately defined structure by prefixing the byte array with
     * its length
     */
    public static void writeBlob(ByteBuf p_148838_0_, byte[] p_148838_1_)
    {
        p_148838_0_.writeShort(p_148838_1_.length);
        p_148838_0_.writeBytes(p_148838_1_);
    }

    /**
     * Will read a byte array from the supplied ByteBuf, the first short encountered will be interpreted as the size of
     * the byte array to read in
     */
    public static byte[] readBlob(ByteBuf p_148834_0_) throws IOException
    {
        short short1 = p_148834_0_.readShort();

        if (short1 < 0)
        {
            throw new IOException("Key was smaller than nothing!  Weird key!");
        }
        else
        {
            byte[] abyte = new byte[short1];
            p_148834_0_.readBytes(abyte);
            return abyte;
        }
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public abstract void readPacketData(PacketBuffer p_148837_1_) throws IOException;

    /**
     * Writes the raw packet data to the data stream.
     */
    public abstract void writePacketData(PacketBuffer p_148840_1_) throws IOException;

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public abstract void processPacket(INetHandler p_148833_1_);

    /**
     * If true, the network manager will process the packet immediately when received, otherwise it will queue it for
     * processing. Currently true for: Disconnect, LoginSuccess, KeepAlive, ServerQuery/Info, Ping/Pong
     */
    public boolean hasPriority()
    {
        return false;
    }

    public String toString()
    {
        return this.getClass().getSimpleName();
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return "";
    }
}