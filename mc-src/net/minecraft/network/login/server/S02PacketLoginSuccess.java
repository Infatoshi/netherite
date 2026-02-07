package net.minecraft.network.login.server;

import com.mojang.authlib.GameProfile;
import java.io.IOException;
import java.util.UUID;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.login.INetHandlerLoginClient;

public class S02PacketLoginSuccess extends Packet
{
    private GameProfile field_149602_a;
    private static final String __OBFID = "CL_00001375";

    public S02PacketLoginSuccess() {}

    public S02PacketLoginSuccess(GameProfile p_i45267_1_)
    {
        this.field_149602_a = p_i45267_1_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        String s = p_148837_1_.readStringFromBuffer(36);
        String s1 = p_148837_1_.readStringFromBuffer(16);
        UUID uuid = UUID.fromString(s);
        this.field_149602_a = new GameProfile(uuid, s1);
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        UUID uuid = this.field_149602_a.getId();
        p_148840_1_.writeStringToBuffer(uuid == null ? "" : uuid.toString());
        p_148840_1_.writeStringToBuffer(this.field_149602_a.getName());
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerLoginClient p_148833_1_)
    {
        p_148833_1_.handleLoginSuccess(this);
    }

    /**
     * If true, the network manager will process the packet immediately when received, otherwise it will queue it for
     * processing. Currently true for: Disconnect, LoginSuccess, KeepAlive, ServerQuery/Info, Ping/Pong
     */
    public boolean hasPriority()
    {
        return true;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerLoginClient)p_148833_1_);
    }
}