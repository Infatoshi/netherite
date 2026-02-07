package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S08PacketPlayerPosLook extends Packet
{
    private double field_148940_a;
    private double field_148938_b;
    private double field_148939_c;
    private float field_148936_d;
    private float field_148937_e;
    private boolean field_148935_f;
    private static final String __OBFID = "CL_00001273";

    public S08PacketPlayerPosLook() {}

    public S08PacketPlayerPosLook(double p_i45164_1_, double p_i45164_3_, double p_i45164_5_, float p_i45164_7_, float p_i45164_8_, boolean p_i45164_9_)
    {
        this.field_148940_a = p_i45164_1_;
        this.field_148938_b = p_i45164_3_;
        this.field_148939_c = p_i45164_5_;
        this.field_148936_d = p_i45164_7_;
        this.field_148937_e = p_i45164_8_;
        this.field_148935_f = p_i45164_9_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148940_a = p_148837_1_.readDouble();
        this.field_148938_b = p_148837_1_.readDouble();
        this.field_148939_c = p_148837_1_.readDouble();
        this.field_148936_d = p_148837_1_.readFloat();
        this.field_148937_e = p_148837_1_.readFloat();
        this.field_148935_f = p_148837_1_.readBoolean();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeDouble(this.field_148940_a);
        p_148840_1_.writeDouble(this.field_148938_b);
        p_148840_1_.writeDouble(this.field_148939_c);
        p_148840_1_.writeFloat(this.field_148936_d);
        p_148840_1_.writeFloat(this.field_148937_e);
        p_148840_1_.writeBoolean(this.field_148935_f);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handlePlayerPosLook(this);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }

    @SideOnly(Side.CLIENT)
    public double func_148932_c()
    {
        return this.field_148940_a;
    }

    @SideOnly(Side.CLIENT)
    public double func_148928_d()
    {
        return this.field_148938_b;
    }

    @SideOnly(Side.CLIENT)
    public double func_148933_e()
    {
        return this.field_148939_c;
    }

    @SideOnly(Side.CLIENT)
    public float func_148931_f()
    {
        return this.field_148936_d;
    }

    @SideOnly(Side.CLIENT)
    public float func_148930_g()
    {
        return this.field_148937_e;
    }

    @SideOnly(Side.CLIENT)
    public boolean func_148929_h()
    {
        return this.field_148935_f;
    }
}