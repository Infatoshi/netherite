package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.entity.Entity;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S0BPacketAnimation extends Packet
{
    private int field_148981_a;
    private int field_148980_b;
    private static final String __OBFID = "CL_00001282";

    public S0BPacketAnimation() {}

    public S0BPacketAnimation(Entity p_i45172_1_, int p_i45172_2_)
    {
        this.field_148981_a = p_i45172_1_.getEntityId();
        this.field_148980_b = p_i45172_2_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148981_a = p_148837_1_.readVarIntFromBuffer();
        this.field_148980_b = p_148837_1_.readUnsignedByte();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeVarIntToBuffer(this.field_148981_a);
        p_148840_1_.writeByte(this.field_148980_b);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleAnimation(this);
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("id=%d, type=%d", new Object[] {Integer.valueOf(this.field_148981_a), Integer.valueOf(this.field_148980_b)});
    }

    @SideOnly(Side.CLIENT)
    public int func_148978_c()
    {
        return this.field_148981_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_148977_d()
    {
        return this.field_148980_b;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}