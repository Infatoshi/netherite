package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.entity.Entity;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S12PacketEntityVelocity extends Packet
{
    private int field_149417_a;
    private int field_149415_b;
    private int field_149416_c;
    private int field_149414_d;
    private static final String __OBFID = "CL_00001328";

    public S12PacketEntityVelocity() {}

    public S12PacketEntityVelocity(Entity p_i45219_1_)
    {
        this(p_i45219_1_.getEntityId(), p_i45219_1_.motionX, p_i45219_1_.motionY, p_i45219_1_.motionZ);
    }

    public S12PacketEntityVelocity(int p_i45220_1_, double p_i45220_2_, double p_i45220_4_, double p_i45220_6_)
    {
        this.field_149417_a = p_i45220_1_;
        double d3 = 3.9D;

        if (p_i45220_2_ < -d3)
        {
            p_i45220_2_ = -d3;
        }

        if (p_i45220_4_ < -d3)
        {
            p_i45220_4_ = -d3;
        }

        if (p_i45220_6_ < -d3)
        {
            p_i45220_6_ = -d3;
        }

        if (p_i45220_2_ > d3)
        {
            p_i45220_2_ = d3;
        }

        if (p_i45220_4_ > d3)
        {
            p_i45220_4_ = d3;
        }

        if (p_i45220_6_ > d3)
        {
            p_i45220_6_ = d3;
        }

        this.field_149415_b = (int)(p_i45220_2_ * 8000.0D);
        this.field_149416_c = (int)(p_i45220_4_ * 8000.0D);
        this.field_149414_d = (int)(p_i45220_6_ * 8000.0D);
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149417_a = p_148837_1_.readInt();
        this.field_149415_b = p_148837_1_.readShort();
        this.field_149416_c = p_148837_1_.readShort();
        this.field_149414_d = p_148837_1_.readShort();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeInt(this.field_149417_a);
        p_148840_1_.writeShort(this.field_149415_b);
        p_148840_1_.writeShort(this.field_149416_c);
        p_148840_1_.writeShort(this.field_149414_d);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleEntityVelocity(this);
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("id=%d, x=%.2f, y=%.2f, z=%.2f", new Object[] {Integer.valueOf(this.field_149417_a), Float.valueOf((float)this.field_149415_b / 8000.0F), Float.valueOf((float)this.field_149416_c / 8000.0F), Float.valueOf((float)this.field_149414_d / 8000.0F)});
    }

    @SideOnly(Side.CLIENT)
    public int func_149412_c()
    {
        return this.field_149417_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_149411_d()
    {
        return this.field_149415_b;
    }

    @SideOnly(Side.CLIENT)
    public int func_149410_e()
    {
        return this.field_149416_c;
    }

    @SideOnly(Side.CLIENT)
    public int func_149409_f()
    {
        return this.field_149414_d;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}