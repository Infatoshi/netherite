package net.minecraft.client.multiplayer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.IChatComponent;

@SideOnly(Side.CLIENT)
public class ServerData
{
    public String serverName;
    public String serverIP;
    /**
     * the string indicating number of players on and capacity of the server that is shown on the server browser (i.e.
     * "5/20" meaning 5 slots used out of 20 slots total)
     */
    public String populationInfo;
    /**
     * (better variable name would be 'hostname') server name as displayed in the server browser's second line (grey
     * text)
     */
    public String serverMOTD;
    /** last server ping that showed up in the server browser */
    public long pingToServer;
    public int field_82821_f;
    /** Game version for this server. */
    public String gameVersion;
    public boolean field_78841_f;
    public String field_147412_i;
    private ServerData.ServerResourceMode field_152587_j;
    private String field_147411_m;
    private boolean field_152588_l;
    private static final String __OBFID = "CL_00000890";

    public ServerData(String p_i1193_1_, String p_i1193_2_)
    {
        this.field_82821_f = 5;
        this.gameVersion = "1.7.10";
        this.field_152587_j = ServerData.ServerResourceMode.PROMPT;
        this.serverName = p_i1193_1_;
        this.serverIP = p_i1193_2_;
    }

    public ServerData(String p_i1055_1_, String p_i1055_2_, boolean p_i1055_3_)
    {
        this(p_i1055_1_, p_i1055_2_);
        this.field_152588_l = p_i1055_3_;
    }

    /**
     * Returns an NBTTagCompound with the server's name, IP and maybe acceptTextures.
     */
    public NBTTagCompound getNBTCompound()
    {
        NBTTagCompound nbttagcompound = new NBTTagCompound();
        nbttagcompound.setString("name", this.serverName);
        nbttagcompound.setString("ip", this.serverIP);

        if (this.field_147411_m != null)
        {
            nbttagcompound.setString("icon", this.field_147411_m);
        }

        if (this.field_152587_j == ServerData.ServerResourceMode.ENABLED)
        {
            nbttagcompound.setBoolean("acceptTextures", true);
        }
        else if (this.field_152587_j == ServerData.ServerResourceMode.DISABLED)
        {
            nbttagcompound.setBoolean("acceptTextures", false);
        }

        return nbttagcompound;
    }

    public ServerData.ServerResourceMode func_152586_b()
    {
        return this.field_152587_j;
    }

    public void func_152584_a(ServerData.ServerResourceMode p_152584_1_)
    {
        this.field_152587_j = p_152584_1_;
    }

    /**
     * Takes an NBTTagCompound with 'name' and 'ip' keys, returns a ServerData instance.
     */
    public static ServerData getServerDataFromNBTCompound(NBTTagCompound p_78837_0_)
    {
        ServerData serverdata = new ServerData(p_78837_0_.getString("name"), p_78837_0_.getString("ip"));

        if (p_78837_0_.hasKey("icon", 8))
        {
            serverdata.func_147407_a(p_78837_0_.getString("icon"));
        }

        if (p_78837_0_.hasKey("acceptTextures", 1))
        {
            if (p_78837_0_.getBoolean("acceptTextures"))
            {
                serverdata.func_152584_a(ServerData.ServerResourceMode.ENABLED);
            }
            else
            {
                serverdata.func_152584_a(ServerData.ServerResourceMode.DISABLED);
            }
        }
        else
        {
            serverdata.func_152584_a(ServerData.ServerResourceMode.PROMPT);
        }

        return serverdata;
    }

    /**
     * Returns the base-64 encoded representation of the server's icon, or null if not available
     */
    public String getBase64EncodedIconData()
    {
        return this.field_147411_m;
    }

    public void func_147407_a(String p_147407_1_)
    {
        this.field_147411_m = p_147407_1_;
    }

    public void func_152583_a(ServerData p_152583_1_)
    {
        this.serverIP = p_152583_1_.serverIP;
        this.serverName = p_152583_1_.serverName;
        this.func_152584_a(p_152583_1_.func_152586_b());
        this.field_147411_m = p_152583_1_.field_147411_m;
    }

    public boolean func_152585_d()
    {
        return this.field_152588_l;
    }

    @SideOnly(Side.CLIENT)
    public static enum ServerResourceMode
    {
        ENABLED("enabled"),
        DISABLED("disabled"),
        PROMPT("prompt");
        private final IChatComponent field_152594_d;

        private static final String __OBFID = "CL_00001833";

        private ServerResourceMode(String p_i1053_3_)
        {
            this.field_152594_d = new ChatComponentTranslation("addServer.resourcePack." + p_i1053_3_, new Object[0]);
        }

        public IChatComponent func_152589_a()
        {
            return this.field_152594_d;
        }
    }
}