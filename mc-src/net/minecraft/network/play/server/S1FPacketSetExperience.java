package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S1FPacketSetExperience extends Packet
{
    private float field_149401_a;
    private int field_149399_b;
    private int field_149400_c;
    private static final String __OBFID = "CL_00001331";

    public S1FPacketSetExperience() {}

    public S1FPacketSetExperience(float p_i45222_1_, int p_i45222_2_, int p_i45222_3_)
    {
        this.field_149401_a = p_i45222_1_;
        this.field_149399_b = p_i45222_2_;
        this.field_149400_c = p_i45222_3_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149401_a = p_148837_1_.readFloat();
        this.field_149400_c = p_148837_1_.readShort();
        this.field_149399_b = p_148837_1_.readShort();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeFloat(this.field_149401_a);
        p_148840_1_.writeShort(this.field_149400_c);
        p_148840_1_.writeShort(this.field_149399_b);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleSetExperience(this);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }

    @SideOnly(Side.CLIENT)
    public float func_149397_c()
    {
        return this.field_149401_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_149396_d()
    {
        return this.field_149399_b;
    }

    @SideOnly(Side.CLIENT)
    public int func_149395_e()
    {
        return this.field_149400_c;
    }
}