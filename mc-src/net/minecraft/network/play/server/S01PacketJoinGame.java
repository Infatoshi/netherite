package net.minecraft.network.play.server;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;

public class S01PacketJoinGame extends Packet
{
    private int field_149206_a;
    private boolean field_149204_b;
    private WorldSettings.GameType field_149205_c;
    private int field_149202_d;
    private EnumDifficulty field_149203_e;
    private int field_149200_f;
    private WorldType field_149201_g;
    private static final String __OBFID = "CL_00001310";

    public S01PacketJoinGame() {}

    public S01PacketJoinGame(int p_i45201_1_, WorldSettings.GameType p_i45201_2_, boolean p_i45201_3_, int p_i45201_4_, EnumDifficulty p_i45201_5_, int p_i45201_6_, WorldType p_i45201_7_)
    {
        this.field_149206_a = p_i45201_1_;
        this.field_149202_d = p_i45201_4_;
        this.field_149203_e = p_i45201_5_;
        this.field_149205_c = p_i45201_2_;
        this.field_149200_f = p_i45201_6_;
        this.field_149204_b = p_i45201_3_;
        this.field_149201_g = p_i45201_7_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149206_a = p_148837_1_.readInt();
        short short1 = p_148837_1_.readUnsignedByte();
        this.field_149204_b = (short1 & 8) == 8;
        int i = short1 & -9;
        this.field_149205_c = WorldSettings.GameType.getByID(i);
        this.field_149202_d = p_148837_1_.readByte();
        this.field_149203_e = EnumDifficulty.getDifficultyEnum(p_148837_1_.readUnsignedByte());
        this.field_149200_f = p_148837_1_.readUnsignedByte();
        this.field_149201_g = WorldType.parseWorldType(p_148837_1_.readStringFromBuffer(16));

        if (this.field_149201_g == null)
        {
            this.field_149201_g = WorldType.DEFAULT;
        }
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeInt(this.field_149206_a);
        int i = this.field_149205_c.getID();

        if (this.field_149204_b)
        {
            i |= 8;
        }

        p_148840_1_.writeByte(i);
        p_148840_1_.writeByte(this.field_149202_d);
        p_148840_1_.writeByte(this.field_149203_e.getDifficultyId());
        p_148840_1_.writeByte(this.field_149200_f);
        p_148840_1_.writeStringToBuffer(this.field_149201_g.getWorldTypeName());
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayClient p_148833_1_)
    {
        p_148833_1_.handleJoinGame(this);
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("eid=%d, gameType=%d, hardcore=%b, dimension=%d, difficulty=%s, maxplayers=%d", new Object[] {Integer.valueOf(this.field_149206_a), Integer.valueOf(this.field_149205_c.getID()), Boolean.valueOf(this.field_149204_b), Integer.valueOf(this.field_149202_d), this.field_149203_e, Integer.valueOf(this.field_149200_f)});
    }

    @SideOnly(Side.CLIENT)
    public int func_149197_c()
    {
        return this.field_149206_a;
    }

    @SideOnly(Side.CLIENT)
    public boolean func_149195_d()
    {
        return this.field_149204_b;
    }

    @SideOnly(Side.CLIENT)
    public WorldSettings.GameType func_149198_e()
    {
        return this.field_149205_c;
    }

    @SideOnly(Side.CLIENT)
    public int func_149194_f()
    {
        return this.field_149202_d;
    }

    @SideOnly(Side.CLIENT)
    public EnumDifficulty func_149192_g()
    {
        return this.field_149203_e;
    }

    @SideOnly(Side.CLIENT)
    public int func_149193_h()
    {
        return this.field_149200_f;
    }

    @SideOnly(Side.CLIENT)
    public WorldType func_149196_i()
    {
        return this.field_149201_g;
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayClient)p_148833_1_);
    }
}