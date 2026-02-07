package net.minecraft.network.play.client;

import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayServer;

public class C16PacketClientStatus extends Packet
{
    private C16PacketClientStatus.EnumState field_149437_a;
    private static final String __OBFID = "CL_00001348";

    public C16PacketClientStatus() {}

    public C16PacketClientStatus(C16PacketClientStatus.EnumState p_i45242_1_)
    {
        this.field_149437_a = p_i45242_1_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149437_a = C16PacketClientStatus.EnumState.field_151404_e[p_148837_1_.readByte() % C16PacketClientStatus.EnumState.field_151404_e.length];
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeByte(this.field_149437_a.field_151403_d);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayServer p_148833_1_)
    {
        p_148833_1_.processClientStatus(this);
    }

    public C16PacketClientStatus.EnumState func_149435_c()
    {
        return this.field_149437_a;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayServer)p_148833_1_);
    }

    public static enum EnumState
    {
        PERFORM_RESPAWN(0),
        REQUEST_STATS(1),
        OPEN_INVENTORY_ACHIEVEMENT(2);
        private final int field_151403_d;
        private static final C16PacketClientStatus.EnumState[] field_151404_e = new C16PacketClientStatus.EnumState[values().length];

        private static final String __OBFID = "CL_00001349";

        private EnumState(int p_i45241_3_)
        {
            this.field_151403_d = p_i45241_3_;
        }

        static
        {
            C16PacketClientStatus.EnumState[] var0 = values();
            int var1 = var0.length;

            for (int var2 = 0; var2 < var1; ++var2)
            {
                C16PacketClientStatus.EnumState var3 = var0[var2];
                field_151404_e[var3.field_151403_d] = var3;
            }
        }
    }
}