package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S25PacketBlockBreakAnim extends Packet
{
    private int field_148852_a;
    private int field_148850_b;
    private int field_148851_c;
    private int field_148848_d;
    private int field_148849_e;
    private static final String __OBFID = "CL_00001284";

    public S25PacketBlockBreakAnim() {}

    public S25PacketBlockBreakAnim(int p_i45174_1_, int p_i45174_2_, int p_i45174_3_, int p_i45174_4_, int p_i45174_5_)
    {
        this.field_148852_a = p_i45174_1_;
        this.field_148850_b = p_i45174_2_;
        this.field_148851_c = p_i45174_3_;
        this.field_148848_d = p_i45174_4_;
        this.field_148849_e = p_i45174_5_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148852_a = p_148837_1_.readVarIntFromBuffer();
        this.field_148850_b = p_148837_1_.readInt();
        this.field_148851_c = p_148837_1_.readInt();
        this.field_148848_d = p_148837_1_.readInt();
        this.field_148849_e = p_148837_1_.readUnsignedByte();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeVarIntToBuffer(this.field_148852_a);
        p_148840_1_.writeInt(this.field_148850_b);
        p_148840_1_.writeInt(this.field_148851_c);
        p_148840_1_.writeInt(this.field_148848_d);
        p_148840_1_.writeByte(this.field_148849_e);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleBlockBreakAnim(this);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }

    @SideOnly(Side.CLIENT)
    public int func_148845_c()
    {
        return this.field_148852_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_148844_d()
    {
        return this.field_148850_b;
    }

    @SideOnly(Side.CLIENT)
    public int func_148843_e()
    {
        return this.field_148851_c;
    }

    @SideOnly(Side.CLIENT)
    public int func_148842_f()
    {
        return this.field_148848_d;
    }

    @SideOnly(Side.CLIENT)
    public int func_148846_g()
    {
        return this.field_148849_e;
    }
}