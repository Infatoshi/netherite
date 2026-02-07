package net.minecraft.network.play.client;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.network.INetHandler;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayServer;
import net.minecraft.world.EnumDifficulty;

public class C15PacketClientSettings extends Packet
{
    private String field_149530_a;
    private int field_149528_b;
    private EntityPlayer.EnumChatVisibility field_149529_c;
    private boolean field_149526_d;
    private EnumDifficulty field_149527_e;
    private boolean field_149525_f;
    private static final String __OBFID = "CL_00001350";

    public C15PacketClientSettings() {}

    @SideOnly(Side.CLIENT)
    public C15PacketClientSettings(String p_i45243_1_, int p_i45243_2_, EntityPlayer.EnumChatVisibility p_i45243_3_, boolean p_i45243_4_, EnumDifficulty p_i45243_5_, boolean p_i45243_6_)
    {
        this.field_149530_a = p_i45243_1_;
        this.field_149528_b = p_i45243_2_;
        this.field_149529_c = p_i45243_3_;
        this.field_149526_d = p_i45243_4_;
        this.field_149527_e = p_i45243_5_;
        this.field_149525_f = p_i45243_6_;
    }

    /**
     * Reads the raw packet data from the data stream.
     */
    public void readPacketData(PacketBuffer p_148837_1_) throws IOException
    {
        this.field_149530_a = p_148837_1_.readStringFromBuffer(7);
        this.field_149528_b = p_148837_1_.readByte();
        this.field_149529_c = EntityPlayer.EnumChatVisibility.getEnumChatVisibility(p_148837_1_.readByte());
        this.field_149526_d = p_148837_1_.readBoolean();
        this.field_149527_e = EnumDifficulty.getDifficultyEnum(p_148837_1_.readByte());
        this.field_149525_f = p_148837_1_.readBoolean();
    }

    /**
     * Writes the raw packet data to the data stream.
     */
    public void writePacketData(PacketBuffer p_148840_1_) throws IOException
    {
        p_148840_1_.writeStringToBuffer(this.field_149530_a);
        p_148840_1_.writeByte(this.field_149528_b);
        p_148840_1_.writeByte(this.field_149529_c.getChatVisibility());
        p_148840_1_.writeBoolean(this.field_149526_d);
        p_148840_1_.writeByte(this.field_149527_e.getDifficultyId());
        p_148840_1_.writeBoolean(this.field_149525_f);
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandlerPlayServer p_148833_1_)
    {
        p_148833_1_.processClientSettings(this);
    }

    public String func_149524_c()
    {
        return this.field_149530_a;
    }

    public int func_149521_d()
    {
        return this.field_149528_b;
    }

    public EntityPlayer.EnumChatVisibility func_149523_e()
    {
        return this.field_149529_c;
    }

    public boolean func_149520_f()
    {
        return this.field_149526_d;
    }

    public EnumDifficulty func_149518_g()
    {
        return this.field_149527_e;
    }

    public boolean func_149519_h()
    {
        return this.field_149525_f;
    }

    /**
     * Returns a string formatted as comma separated [field]=[value] values. Used by Minecraft for logging purposes.
     */
    public String serialize()
    {
        return String.format("lang=\'%s\', view=%d, chat=%s, col=%b, difficulty=%s, cape=%b", new Object[] {this.field_149530_a, Integer.valueOf(this.field_149528_b), this.field_149529_c, Boolean.valueOf(this.field_149526_d), this.field_149527_e, Boolean.valueOf(this.field_149525_f)});
    }

    /**
     * Passes this Packet on to the NetHandler for processing.
     */
    public void processPacket(INetHandler p_148833_1_)
    {
        this.processPacket((INetHandlerPlayServer)p_148833_1_);
    }
}