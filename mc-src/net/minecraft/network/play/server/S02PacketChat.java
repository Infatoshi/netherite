package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;
import net.minecraft.util.IChatComponent;

public class S02PacketChat extends Packet
{
    private IChatComponent field_148919_a;
    private boolean field_148918_b;
    private static final String __OBFID = "CL_00001289";

    public S02PacketChat()
    {
        this.field_148918_b = true;
    }

    public S02PacketChat(IChatComponent p_i45179_1_)
    {
        this(p_i45179_1_, true);
    }

    public S02PacketChat(IChatComponent p_i45180_1_, boolean p_i45180_2_)
    {
        this.field_148918_b = true;
        this.field_148919_a = p_i45180_1_;
        this.field_148918_b = p_i45180_2_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148919_a = IChatComponent.Serializer.func_150699_a(p_148837_1_.readStringFromBuffer(32767));
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeStringToBuffer(IChatComponent.Serializer.func_150696_a(this.field_148919_a));
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleChat(this);
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("message=\'%s\'", new Object[] {this.field_148919_a});
    }

    @SideOnly(Side.CLIENT)
    public IChatComponent func_148915_c()
    {
        return this.field_148919_a;
    }

    public boolean func_148916_d()
    {
        return this.field_148918_b;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}