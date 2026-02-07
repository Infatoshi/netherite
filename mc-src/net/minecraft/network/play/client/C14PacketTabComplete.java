package net.minecraft.network.play.client;

import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayServer;
import org.apache.commons.lang3.StringUtils;

public class C14PacketTabComplete extends Packet
{
    private String field_149420_a;
    private static final String __OBFID = "CL_00001346";

    public C14PacketTabComplete() {}

    public C14PacketTabComplete(String p_i45239_1_)
    {
        this.field_149420_a = p_i45239_1_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149420_a = p_148837_1_.readStringFromBuffer(32767);
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeStringToBuffer(StringUtils.substring(this.field_149420_a, 0, 32767));
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayServer p_148833_1_)
    {
        p_148833_1_.processTabComplete(this);
    }

    public String func_149419_c()
    {
        return this.field_149420_a;
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("message=\'%s\'", new Object[] {this.field_149420_a});
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayServer)p_148833_1_);
    }
}