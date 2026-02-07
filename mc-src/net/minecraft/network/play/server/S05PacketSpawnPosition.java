package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;

public class S05PacketSpawnPosition extends Packet
{
    private int field_149364_a;
    private int field_149362_b;
    private int field_149363_c;
    private static final String __OBFID = "CL_00001336";

    public S05PacketSpawnPosition() {}

    public S05PacketSpawnPosition(int p_i45229_1_, int p_i45229_2_, int p_i45229_3_)
    {
        this.field_149364_a = p_i45229_1_;
        this.field_149362_b = p_i45229_2_;
        this.field_149363_c = p_i45229_3_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149364_a = p_148837_1_.readInt();
        this.field_149362_b = p_148837_1_.readInt();
        this.field_149363_c = p_148837_1_.readInt();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeInt(this.field_149364_a);
        p_148840_1_.writeInt(this.field_149362_b);
        p_148840_1_.writeInt(this.field_149363_c);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleSpawnPosition(this);
    }

    /**
     * If true, the network manager will process the packet immediately when received, otherwise it will queue it for
     * processing. Currently true for: Disconnect, LoginSuccess, KeepAlive, ServerQuery/Info, Ping/Pong
     */
    public boolean hasPriority()
    {
        return false;
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("x=%d, y=%d, z=%d", new Object[] {Integer.valueOf(this.field_149364_a), Integer.valueOf(this.field_149362_b), Integer.valueOf(this.field_149363_c)});
    }

    @SideOnly(Side.CLIENT)
    public int func_149360_c()
    {
        return this.field_149364_a;
    }

    @SideOnly(Side.CLIENT)
    public int func_149359_d()
    {
        return this.field_149362_b;
    }

    @SideOnly(Side.CLIENT)
    public int func_149358_e()
    {
        return this.field_149363_c;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}