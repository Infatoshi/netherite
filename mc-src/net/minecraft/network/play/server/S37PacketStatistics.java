package net.minecraft.network.play.server;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatList;

public class S37PacketStatistics extends Packet
{
    private Map field_148976_a;
    private static final String __OBFID = "CL_00001283";

    public S37PacketStatistics() {}

    public S37PacketStatistics(Map p_i45173_1_)
    {
        this.field_148976_a = p_i45173_1_;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleStatistics(this);
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        int i = p_148837_1_.readVarIntFromBuffer();
        this.field_148976_a = Maps.newHashMap();

        for (int j = 0; j < i; ++j)
        {
            StatBase statbase = StatList.func_151177_a(p_148837_1_.readStringFromBuffer(32767));
            int k = p_148837_1_.readVarIntFromBuffer();

            if (statbase != null)
            {
                this.field_148976_a.put(statbase, Integer.valueOf(k));
            }
        }
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeVarIntToBuffer(this.field_148976_a.size());
        Iterator iterator = this.field_148976_a.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            p_148840_1_.writeStringToBuffer(((StatBase)entry.getKey()).statId);
            p_148840_1_.writeVarIntToBuffer(((Integer)entry.getValue()).intValue());
        }
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("count=%d", new Object[] {Integer.valueOf(this.field_148976_a.size())});
    }

    @SideOnly(Side.CLIENT)
    public Map func_148974_c()
    {
        return this.field_148976_a;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}