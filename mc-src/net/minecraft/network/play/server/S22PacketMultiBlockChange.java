package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import net.minecraft.block.Block;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.chunk.Chunk;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public class S22PacketMultiBlockChange extends Packet
{
    private static final Logger logger = LogManager.getLogger();
    private ChunkCoordIntPair field_148925_b;
    private byte[] field_148926_c;
    private int field_148924_d;
    private static final String __OBFID = "CL_00001290";

    public S22PacketMultiBlockChange() {}

    public S22PacketMultiBlockChange(int p_i45181_1_, short[] p_i45181_2_, Chunk p_i45181_3_)
    {
        this.field_148925_b = new ChunkCoordIntPair(p_i45181_3_.xPosition, p_i45181_3_.zPosition);
        this.field_148924_d = p_i45181_1_;
        int j = 4 * p_i45181_1_;

        try
        {
            ByteArrayOutputStream bytearrayoutputstream = new ByteArrayOutputStream(j);
            DataOutputStream dataoutputstream = new DataOutputStream(bytearrayoutputstream);

            for (int k = 0; k < p_i45181_1_; ++k)
            {
                int l = p_i45181_2_[k] >> 12 & 15;
                int i1 = p_i45181_2_[k] >> 8 & 15;
                int j1 = p_i45181_2_[k] & 255;
                dataoutputstream.writeShort(p_i45181_2_[k]);
                dataoutputstream.writeShort((short)((Block.getIdFromBlock(p_i45181_3_.getBlock(l, j1, i1)) & 4095) << 4 | p_i45181_3_.getBlockMetadata(l, j1, i1) & 15));
            }

            this.field_148926_c = bytearrayoutputstream.toByteArray();

            if (this.field_148926_c.length != j)
            {
                throw new RuntimeException("Expected length " + j + " doesn\'t match received length " + this.field_148926_c.length);
            }
        }
        catch (IOException ioexception)
        {
            logger.error("Couldn\'t create bulk block update packet", ioexception);
            this.field_148926_c = null;
        }
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_148925_b = new ChunkCoordIntPair(p_148837_1_.readInt(), p_148837_1_.readInt());
        this.field_148924_d = p_148837_1_.readShort() & 65535;
        int i = p_148837_1_.readInt();

        if (i > 0)
        {
            this.field_148926_c = new byte[i];
            p_148837_1_.readBytes(this.field_148926_c);
        }
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeInt(this.field_148925_b.chunkXPos);
        p_148840_1_.writeInt(this.field_148925_b.chunkZPos);
        p_148840_1_.writeShort((short)this.field_148924_d);

        if (this.field_148926_c != null)
        {
            p_148840_1_.writeInt(this.field_148926_c.length);
            p_148840_1_.writeBytes(this.field_148926_c);
        }
        else
        {
            p_148840_1_.writeInt(0);
        }
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleMultiBlockChange(this);
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("xc=%d, zc=%d, count=%d", new Object[] {Integer.valueOf(this.field_148925_b.chunkXPos), Integer.valueOf(this.field_148925_b.chunkZPos), Integer.valueOf(this.field_148924_d)});
    }

    @SideOnly(Side.CLIENT)
    public ChunkCoordIntPair func_148920_c()
    {
        return this.field_148925_b;
    }

    @SideOnly(Side.CLIENT)
    public byte[] func_148921_d()
    {
        return this.field_148926_c;
    }

    @SideOnly(Side.CLIENT)
    public int func_148922_e()
    {
        return this.field_148924_d;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}