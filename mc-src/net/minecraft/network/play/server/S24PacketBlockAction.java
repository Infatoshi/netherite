package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.block.Block;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S24PacketBlockAction extends Packet
{
    private int field_148876_a;
    private int field_148874_b;
    private int field_148875_c;
    private int field_148872_d;
    private int field_148873_e;
    private Block field_148871_f;
    private static final String __OBFID = "CL_00001286";

    public S24PacketBlockAction() {}

    public S24PacketBlockAction(int p_i45176_1_, int p_i45176_2_, int p_i45176_3_, Block p_i45176_4_, int p_i45176_5_, int p_i45176_6_)
    {
        this.field_148876_a = p_i45176_1_;
        this.field_148874_b = p_i45176_2_;
        this.field_148875_c = p_i45176_3_;
        this.field_148872_d = p_i45176_5_;
        this.field_148873_e = p_i45176_6_;
        this.field_148871_f = p_i45176_4_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148876_a = p_148837_1_.readInt();
        this.field_148874_b = p_148837_1_.readShort();
        this.field_148875_c = p_148837_1_.readInt();
        this.field_148872_d = p_148837_1_.readUnsignedByte();
        this.field_148873_e = p_148837_1_.readUnsignedByte();
        this.field_148871_f = Block.getBlockById(p_148837_1_.readVarIntFromBuffer() & 4095);
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeInt(this.field_148876_a);
        p_148840_1_.writeShort(this.field_148874_b);
        p_148840_1_.writeInt(this.field_148875_c);
        p_148840_1_.writeByte(this.field_148872_d);
        p_148840_1_.writeByte(this.field_148873_e);
        p_148840_1_.writeVarIntToBuffer(Block.getIdFromBlock(this.field_148871_f) & 4095);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleBlockAction(this);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }

    @SideOnly(Side.CLIENT)
    public Block func_148868_c()
    {
        return this.field_148871_f;
    }

    @SideOnly(Side.CLIENT)
    public int func_148867_d()
    {
        return this.field_148876_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_148866_e()
    {
        return this.field_148874_b;
    }

    @SideOnly(Side.CLIENT)
    public int func_148865_f()
    {
        return this.field_148875_c;
    }

    @SideOnly(Side.CLIENT)
    public int func_148869_g()
    {
        return this.field_148872_d;
    }

    @SideOnly(Side.CLIENT)
    public int func_148864_h()
    {
        return this.field_148873_e;
    }
}