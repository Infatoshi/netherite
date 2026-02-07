package net.minecraft.world.storage;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.*;
import net.minecraft.world.World;
import net.minecraft.world.WorldSavedData;

public class MapData extends WorldSavedData
{
    public int xCenter;
    public int zCenter;
    public int dimension;
    public byte scale;
    /** colours */
    public byte[] colors = new byte[16384];
    /** Holds a reference to the MapInfo of the players who own a copy of the map */
    public List playersArrayList = new ArrayList();
    /** Holds a reference to the players who own a copy of the map and a reference to their MapInfo */
    private Map playersHashMap = new HashMap();
    public Map playersVisibleOnMap = new LinkedHashMap();
    private static final String __OBFID = "CL_00000577";

    public MapData(String p_i2140_1_)
    {
        super(p_i2140_1_);
    }

    /**
     * reads in data from the NBTTagCompound into this MapDataBase
     */
    public void readFromNBT(NBTTagCompound p_76184_1_)
    {
        NBTBase dimension = p_76184_1_.getTag("dimension");

        if (dimension instanceof NBTTagByte)
        {
            this.dimension = ((NBTTagByte)dimension).func_150290_f();
        }
        else
        {
            this.dimension = ((NBTTagInt)dimension).func_150287_d();
        }

        this.xCenter = p_76184_1_.getInteger("xCenter");
        this.zCenter = p_76184_1_.getInteger("zCenter");
        this.scale = p_76184_1_.getByte("scale");

        if (this.scale < 0)
        {
            this.scale = 0;
        }

        if (this.scale > 4)
        {
            this.scale = 4;
        }

        short short1 = p_76184_1_.getShort("width");
        short short2 = p_76184_1_.getShort("height");

        if (short1 == 128 && short2 == 128)
        {
            this.colors = p_76184_1_.getByteArray("colors");
        }
        else
        {
            byte[] abyte = p_76184_1_.getByteArray("colors");
            this.colors = new byte[16384];
            int i = (128 - short1) / 2;
            int j = (128 - short2) / 2;

            for (int k = 0; k < short2; ++k)
            {
                int l = k + j;

                if (l >= 0 || l < 128)
                {
                    for (int i1 = 0; i1 < short1; ++i1)
                    {
                        int j1 = i1 + i;

                        if (j1 >= 0 || j1 < 128)
                        {
                            this.colors[j1 + l * 128] = abyte[i1 + k * short1];
                        }
                    }
                }
            }
        }
    }

    /**
     * write data to NBTTagCompound from this MapDataBase, similar to Entities and TileEntities
     */
    public void writeToNBT(NBTTagCompound p_76187_1_)
    {
        p_76187_1_.setInteger("dimension", this.dimension);
        p_76187_1_.setInteger("xCenter", this.xCenter);
        p_76187_1_.setInteger("zCenter", this.zCenter);
        p_76187_1_.setByte("scale", this.scale);
        p_76187_1_.setShort("width", (short)128);
        p_76187_1_.setShort("height", (short)128);
        p_76187_1_.setByteArray("colors", this.colors);
    }

    /**
     * Adds the player passed to the list of visible players and checks to see which players are visible
     */
    public void updateVisiblePlayers(EntityPlayer p_76191_1_, ItemStack p_76191_2_)
    {
        if (!this.playersHashMap.containsKey(p_76191_1_))
        {
            MapData.MapInfo mapinfo = new MapData.MapInfo(p_76191_1_);
            this.playersHashMap.put(p_76191_1_, mapinfo);
            this.playersArrayList.add(mapinfo);
        }

        if (!p_76191_1_.inventory.hasItemStack(p_76191_2_))
        {
            this.playersVisibleOnMap.remove(p_76191_1_.getCommandSenderName());
        }

        for (int i = 0; i < this.playersArrayList.size(); ++i)
        {
            MapData.MapInfo mapinfo1 = (MapData.MapInfo)this.playersArrayList.get(i);

            if (!mapinfo1.entityplayerObj.isDead && (mapinfo1.entityplayerObj.inventory.hasItemStack(p_76191_2_) || p_76191_2_.isOnItemFrame()))
            {
                if (!p_76191_2_.isOnItemFrame() && mapinfo1.entityplayerObj.dimension == this.dimension)
                {
                    this.func_82567_a(0, mapinfo1.entityplayerObj.worldObj, mapinfo1.entityplayerObj.getCommandSenderName(), mapinfo1.entityplayerObj.posX, mapinfo1.entityplayerObj.posZ, (double)mapinfo1.entityplayerObj.rotationYaw);
                }
            }
            else
            {
                this.playersHashMap.remove(mapinfo1.entityplayerObj);
                this.playersArrayList.remove(mapinfo1);
            }
        }

        if (p_76191_2_.isOnItemFrame())
        {
            this.func_82567_a(1, p_76191_1_.worldObj, "frame-" + p_76191_2_.getItemFrame().getEntityId(), (double)p_76191_2_.getItemFrame().field_146063_b, (double)p_76191_2_.getItemFrame().field_146062_d, (double)(p_76191_2_.getItemFrame().hangingDirection * 90));
        }
    }

    private void func_82567_a(int p_82567_1_, World p_82567_2_, String p_82567_3_, double p_82567_4_, double p_82567_6_, double p_82567_8_)
    {
        int j = 1 << this.scale;
        float f = (float)(p_82567_4_ - (double)this.xCenter) / (float)j;
        float f1 = (float)(p_82567_6_ - (double)this.zCenter) / (float)j;
        byte b0 = (byte)((int)((double)(f * 2.0F) + 0.5D));
        byte b1 = (byte)((int)((double)(f1 * 2.0F) + 0.5D));
        byte b3 = 63;
        byte b2;

        if (f >= (float)(-b3) && f1 >= (float)(-b3) && f <= (float)b3 && f1 <= (float)b3)
        {
            p_82567_8_ += p_82567_8_ < 0.0D ? -8.0D : 8.0D;
            b2 = (byte)((int)(p_82567_8_ * 16.0D / 360.0D));

            if (p_82567_2_.provider.shouldMapSpin(p_82567_3_, p_82567_4_, p_82567_6_, p_82567_8_))
            {
                int k = (int)(p_82567_2_.getWorldInfo().getWorldTime() / 10L);
                b2 = (byte)(k * k * 34187121 + k * 121 >> 15 & 15);
            }
        }
        else
        {
            if (Math.abs(f) >= 320.0F || Math.abs(f1) >= 320.0F)
            {
                this.playersVisibleOnMap.remove(p_82567_3_);
                return;
            }

            p_82567_1_ = 6;
            b2 = 0;

            if (f <= (float)(-b3))
            {
                b0 = (byte)((int)((double)(b3 * 2) + 2.5D));
            }

            if (f1 <= (float)(-b3))
            {
                b1 = (byte)((int)((double)(b3 * 2) + 2.5D));
            }

            if (f >= (float)b3)
            {
                b0 = (byte)(b3 * 2 + 1);
            }

            if (f1 >= (float)b3)
            {
                b1 = (byte)(b3 * 2 + 1);
            }
        }

        this.playersVisibleOnMap.put(p_82567_3_, new MapData.MapCoord((byte)p_82567_1_, b0, b1, b2));
    }

    /**
     * Get byte array of packet data to send to players on map for updating map data
     */
    public byte[] getUpdatePacketData(ItemStack p_76193_1_, World p_76193_2_, EntityPlayer p_76193_3_)
    {
        MapData.MapInfo mapinfo = (MapData.MapInfo)this.playersHashMap.get(p_76193_3_);
        return mapinfo == null ? null : mapinfo.getPlayersOnMap(p_76193_1_);
    }

    /**
     * Marks a vertical range of pixels as being modified so they will be resent to clients. Parameters: X, lowest Y,
     * highest Y
     */
    public void setColumnDirty(int p_76194_1_, int p_76194_2_, int p_76194_3_)
    {
        super.markDirty();

        for (int l = 0; l < this.playersArrayList.size(); ++l)
        {
            MapData.MapInfo mapinfo = (MapData.MapInfo)this.playersArrayList.get(l);

            if (mapinfo.field_76209_b[p_76194_1_] < 0 || mapinfo.field_76209_b[p_76194_1_] > p_76194_2_)
            {
                mapinfo.field_76209_b[p_76194_1_] = p_76194_2_;
            }

            if (mapinfo.field_76210_c[p_76194_1_] < 0 || mapinfo.field_76210_c[p_76194_1_] < p_76194_3_)
            {
                mapinfo.field_76210_c[p_76194_1_] = p_76194_3_;
            }
        }
    }

    /**
     * Updates the client's map with information from other players in MP
     */
    @SideOnly(Side.CLIENT)
    public void updateMPMapData(byte[] p_76192_1_)
    {
        int i;

        if (p_76192_1_[0] == 0)
        {
            i = p_76192_1_[1] & 255;
            int j = p_76192_1_[2] & 255;

            for (int k = 0; k < p_76192_1_.length - 3; ++k)
            {
                this.colors[(k + j) * 128 + i] = p_76192_1_[k + 3];
            }

            this.markDirty();
        }
        else if (p_76192_1_[0] == 1)
        {
            this.playersVisibleOnMap.clear();

            for (i = 0; i < (p_76192_1_.length - 1) / 3; ++i)
            {
                byte b2 = (byte)(p_76192_1_[i * 3 + 1] >> 4);
                byte b3 = p_76192_1_[i * 3 + 2];
                byte b0 = p_76192_1_[i * 3 + 3];
                byte b1 = (byte)(p_76192_1_[i * 3 + 1] & 15);
                this.playersVisibleOnMap.put("icon-" + i, new MapData.MapCoord(b2, b3, b0, b1));
            }
        }
        else if (p_76192_1_[0] == 2)
        {
            this.scale = p_76192_1_[1];
        }
    }

    public MapData.MapInfo func_82568_a(EntityPlayer p_82568_1_)
    {
        MapData.MapInfo mapinfo = (MapData.MapInfo)this.playersHashMap.get(p_82568_1_);

        if (mapinfo == null)
        {
            mapinfo = new MapData.MapInfo(p_82568_1_);
            this.playersHashMap.put(p_82568_1_, mapinfo);
            this.playersArrayList.add(mapinfo);
        }

        return mapinfo;
    }

    public class MapCoord
    {
        public byte iconSize;
        public byte centerX;
        public byte centerZ;
        public byte iconRotation;
        private static final String __OBFID = "CL_00000579";

        public MapCoord(byte p_i2139_2_, byte p_i2139_3_, byte p_i2139_4_, byte p_i2139_5_)
        {
            this.iconSize = p_i2139_2_;
            this.centerX = p_i2139_3_;
            this.centerZ = p_i2139_4_;
            this.iconRotation = p_i2139_5_;
        }
    }

    public class MapInfo
    {
        /** Reference for EntityPlayer object in MapInfo */
        public final EntityPlayer entityplayerObj;
        public int[] field_76209_b = new int[128];
        public int[] field_76210_c = new int[128];
        /** updated by x = mod(x*11,128) +1  x-1 is used to index field_76209_b and field_76210_c */
        private int currentRandomNumber;
        private int ticksUntilPlayerLocationMapUpdate;
        /** a cache of the result from getPlayersOnMap so that it is not resent when nothing changes */
        private byte[] lastPlayerLocationOnMap;
        public int field_82569_d;
        private boolean field_82570_i;
        private static final String __OBFID = "CL_00000578";

        public MapInfo(EntityPlayer p_i2138_2_)
        {
            this.entityplayerObj = p_i2138_2_;

            for (int i = 0; i < this.field_76209_b.length; ++i)
            {
                this.field_76209_b[i] = 0;
                this.field_76210_c[i] = 127;
            }
        }

        /**
         * returns a 1+players*3 array, of x,y, and color . the name of this function may be partially wrong, as there
         * is a second branch to the code here
         */
        public byte[] getPlayersOnMap(ItemStack p_76204_1_)
        {
            byte[] abyte;

            if (!this.field_82570_i)
            {
                abyte = new byte[] {(byte)2, MapData.this.scale};
                this.field_82570_i = true;
                return abyte;
            }
            else
            {
                int i;
                int i1;

                if (--this.ticksUntilPlayerLocationMapUpdate < 0)
                {
                    this.ticksUntilPlayerLocationMapUpdate = 4;
                    abyte = new byte[MapData.this.playersVisibleOnMap.size() * 3 + 1];
                    abyte[0] = 1;
                    i = 0;

                    for (Iterator iterator = MapData.this.playersVisibleOnMap.values().iterator(); iterator.hasNext(); ++i)
                    {
                        MapData.MapCoord mapcoord = (MapData.MapCoord)iterator.next();
                        abyte[i * 3 + 1] = (byte)(mapcoord.iconSize << 4 | mapcoord.iconRotation & 15);
                        abyte[i * 3 + 2] = mapcoord.centerX;
                        abyte[i * 3 + 3] = mapcoord.centerZ;
                    }

                    boolean flag = !p_76204_1_.isOnItemFrame();

                    if (this.lastPlayerLocationOnMap != null && this.lastPlayerLocationOnMap.length == abyte.length)
                    {
                        for (i1 = 0; i1 < abyte.length; ++i1)
                        {
                            if (abyte[i1] != this.lastPlayerLocationOnMap[i1])
                            {
                                flag = false;
                                break;
                            }
                        }
                    }
                    else
                    {
                        flag = false;
                    }

                    if (!flag)
                    {
                        this.lastPlayerLocationOnMap = abyte;
                        return abyte;
                    }
                }

                for (int k = 0; k < 1; ++k)
                {
                    i = this.currentRandomNumber++ * 11 % 128;

                    if (this.field_76209_b[i] >= 0)
                    {
                        int l = this.field_76210_c[i] - this.field_76209_b[i] + 1;
                        i1 = this.field_76209_b[i];
                        byte[] abyte1 = new byte[l + 3];
                        abyte1[0] = 0;
                        abyte1[1] = (byte)i;
                        abyte1[2] = (byte)i1;

                        for (int j = 0; j < abyte1.length - 3; ++j)
                        {
                            abyte1[j + 3] = MapData.this.colors[(j + i1) * 128 + i];
                        }

                        this.field_76210_c[i] = -1;
                        this.field_76209_b[i] = -1;
                        return abyte1;
                    }
                }

                return null;
            }
        }
    }
}