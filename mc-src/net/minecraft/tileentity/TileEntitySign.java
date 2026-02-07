package net.minecraft.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.S33PacketUpdateSign;

public class TileEntitySign extends TileEntity
{
    /** An array of four strings storing the lines of text on the sign. */
    public String[] signText = new String[] {"", "", "", ""};
    /**
     * The index of the line currently being edited. Only used on client side, but defined on both. Note this is only
     * really used when the > < are going to be visible.
     */
    public int lineBeingEdited = -1;
    private boolean field_145916_j = true;
    private EntityPlayer field_145917_k;
    private static final String __OBFID = "CL_00000363";

    public void writeToNBT(NBTTagCompound p_145841_1_)
    {
        super.writeToNBT(p_145841_1_);
        p_145841_1_.setString("Text1", this.signText[0]);
        p_145841_1_.setString("Text2", this.signText[1]);
        p_145841_1_.setString("Text3", this.signText[2]);
        p_145841_1_.setString("Text4", this.signText[3]);
    }

    public void readFromNBT(NBTTagCompound p_145839_1_)
    {
        this.field_145916_j = false;
        super.readFromNBT(p_145839_1_);

        for (int i = 0; i < 4; ++i)
        {
            this.signText[i] = p_145839_1_.getString("Text" + (i + 1));

            if (this.signText[i].length() > 15)
            {
                this.signText[i] = this.signText[i].substring(0, 15);
            }
        }
    }

    /**
     * Overriden in a sign to provide the text.
     */
    public Packet getDescriptionPacket()
    {
        String[] astring = new String[4];
        System.arraycopy(this.signText, 0, astring, 0, 4);
        return new S33PacketUpdateSign(this.xCoord, this.yCoord, this.zCoord, astring);
    }

    public boolean func_145914_a()
    {
        return this.field_145916_j;
    }

    /**
     * Sets the sign's isEditable flag to the specified parameter.
     */
    @SideOnly(Side.CLIENT)
    public void setEditable(boolean p_145913_1_)
    {
        this.field_145916_j = p_145913_1_;

        if (!p_145913_1_)
        {
            this.field_145917_k = null;
        }
    }

    public void func_145912_a(EntityPlayer p_145912_1_)
    {
        this.field_145917_k = p_145912_1_;
    }

    public EntityPlayer func_145911_b()
    {
        return this.field_145917_k;
    }
}