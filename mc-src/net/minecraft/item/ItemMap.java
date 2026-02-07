package net.minecraft.item;

import com.google.common.collect.HashMultiset;
import com.google.common.collect.Iterables;
import com.google.common.collect.Multisets;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.block.material.MapColor;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.S34PacketMaps;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.storage.MapData;

public class ItemMap extends ItemMapBase
{
    private static final String __OBFID = "CL_00000047";

    protected ItemMap()
    {
        this.setHasSubtypes(true);
    }

    @SideOnly(Side.CLIENT)
    public static MapData func_150912_a(int p_150912_0_, World p_150912_1_)
    {
        String s = "map_" + p_150912_0_;
        MapData mapdata = (MapData)p_150912_1_.loadItemData(MapData.class, s);

        if (mapdata == null)
        {
            mapdata = new MapData(s);
            p_150912_1_.setItemData(s, mapdata);
        }

        return mapdata;
    }

    public MapData getMapData(ItemStack p_77873_1_, World p_77873_2_)
    {
        String s = "map_" + p_77873_1_.getItemDamage();
        MapData mapdata = (MapData)p_77873_2_.loadItemData(MapData.class, s);

        if (mapdata == null && !p_77873_2_.isRemote)
        {
            p_77873_1_.setItemDamage(p_77873_2_.getUniqueDataId("map"));
            s = "map_" + p_77873_1_.getItemDamage();
            mapdata = new MapData(s);
            mapdata.scale = 3;
            int i = 128 * (1 << mapdata.scale);
            mapdata.xCenter = Math.round((float)p_77873_2_.getWorldInfo().getSpawnX() / (float)i) * i;
            mapdata.zCenter = Math.round((float)(p_77873_2_.getWorldInfo().getSpawnZ() / i)) * i;
            mapdata.dimension = p_77873_2_.provider.dimensionId;
            mapdata.markDirty();
            p_77873_2_.setItemData(s, mapdata);
        }

        return mapdata;
    }

    public void updateMapData(World p_77872_1_, Entity p_77872_2_, MapData p_77872_3_)
    {
        if (p_77872_1_.provider.dimensionId == p_77872_3_.dimension && p_77872_2_ instanceof EntityPlayer)
        {
            int i = 1 << p_77872_3_.scale;
            int j = p_77872_3_.xCenter;
            int k = p_77872_3_.zCenter;
            int l = MathHelper.floor_double(p_77872_2_.posX - (double)j) / i + 64;
            int i1 = MathHelper.floor_double(p_77872_2_.posZ - (double)k) / i + 64;
            int j1 = 128 / i;

            if (p_77872_1_.provider.hasNoSky)
            {
                j1 /= 2;
            }

            MapData.MapInfo mapinfo = p_77872_3_.func_82568_a((EntityPlayer)p_77872_2_);
            ++mapinfo.field_82569_d;

            for (int k1 = l - j1 + 1; k1 < l + j1; ++k1)
            {
                if ((k1 & 15) == (mapinfo.field_82569_d & 15))
                {
                    int l1 = 255;
                    int i2 = 0;
                    double d0 = 0.0D;

                    for (int j2 = i1 - j1 - 1; j2 < i1 + j1; ++j2)
                    {
                        if (k1 >= 0 && j2 >= -1 && k1 < 128 && j2 < 128)
                        {
                            int k2 = k1 - l;
                            int l2 = j2 - i1;
                            boolean flag = k2 * k2 + l2 * l2 > (j1 - 2) * (j1 - 2);
                            int i3 = (j / i + k1 - 64) * i;
                            int j3 = (k / i + j2 - 64) * i;
                            HashMultiset hashmultiset = HashMultiset.create();
                            Chunk chunk = p_77872_1_.getChunkFromBlockCoords(i3, j3);

                            if (!chunk.isEmpty())
                            {
                                int k3 = i3 & 15;
                                int l3 = j3 & 15;
                                int i4 = 0;
                                double d1 = 0.0D;
                                int j4;

                                if (p_77872_1_.provider.hasNoSky)
                                {
                                    j4 = i3 + j3 * 231871;
                                    j4 = j4 * j4 * 31287121 + j4 * 11;

                                    if ((j4 >> 20 & 1) == 0)
                                    {
                                        hashmultiset.add(Blocks.dirt.getMapColor(0), 10);
                                    }
                                    else
                                    {
                                        hashmultiset.add(Blocks.stone.getMapColor(0), 100);
                                    }

                                    d1 = 100.0D;
                                }
                                else
                                {
                                    for (j4 = 0; j4 < i; ++j4)
                                    {
                                        for (int k4 = 0; k4 < i; ++k4)
                                        {
                                            int l4 = chunk.getHeightValue(j4 + k3, k4 + l3) + 1;
                                            Block block = Blocks.air;
                                            int i5 = 0;

                                            if (l4 > 1)
                                            {
                                                do
                                                {
                                                    --l4;
                                                    block = chunk.getBlock(j4 + k3, l4, k4 + l3);
                                                    i5 = chunk.getBlockMetadata(j4 + k3, l4, k4 + l3);
                                                }
                                                while (block.getMapColor(i5) == MapColor.airColor && l4 > 0);

                                                if (l4 > 0 && block.getMaterial().isLiquid())
                                                {
                                                    int j5 = l4 - 1;
                                                    Block block1;

                                                    do
                                                    {
                                                        block1 = chunk.getBlock(j4 + k3, j5--, k4 + l3);
                                                        ++i4;
                                                    }
                                                    while (j5 > 0 && block1.getMaterial().isLiquid());
                                                }
                                            }

                                            d1 += (double)l4 / (double)(i * i);
                                            hashmultiset.add(block.getMapColor(i5));
                                        }
                                    }
                                }

                                i4 /= i * i;
                                double d2 = (d1 - d0) * 4.0D / (double)(i + 4) + ((double)(k1 + j2 & 1) - 0.5D) * 0.4D;
                                byte b0 = 1;

                                if (d2 > 0.6D)
                                {
                                    b0 = 2;
                                }

                                if (d2 < -0.6D)
                                {
                                    b0 = 0;
                                }

                                MapColor mapcolor = (MapColor)Iterables.getFirst(Multisets.copyHighestCountFirst(hashmultiset), MapColor.airColor);

                                if (mapcolor == MapColor.waterColor)
                                {
                                    d2 = (double)i4 * 0.1D + (double)(k1 + j2 & 1) * 0.2D;
                                    b0 = 1;

                                    if (d2 < 0.5D)
                                    {
                                        b0 = 2;
                                    }

                                    if (d2 > 0.9D)
                                    {
                                        b0 = 0;
                                    }
                                }

                                d0 = d1;

                                if (j2 >= 0 && k2 * k2 + l2 * l2 < j1 * j1 && (!flag || (k1 + j2 & 1) != 0))
                                {
                                    byte b1 = p_77872_3_.colors[k1 + j2 * 128];
                                    byte b2 = (byte)(mapcolor.colorIndex * 4 + b0);

                                    if (b1 != b2)
                                    {
                                        if (l1 > j2)
                                        {
                                            l1 = j2;
                                        }

                                        if (i2 < j2)
                                        {
                                            i2 = j2;
                                        }

                                        p_77872_3_.colors[k1 + j2 * 128] = b2;
                                    }
                                }
                            }
                        }
                    }

                    if (l1 <= i2)
                    {
                        p_77872_3_.setColumnDirty(k1, l1, i2);
                    }
                }
            }
        }
    }

    /**
     * Called each tick as long the item is on a player inventory. Uses by maps to check if is on a player hand and
     * update it's contents.
     */
    public void onUpdate(ItemStack p_77663_1_, World p_77663_2_, Entity p_77663_3_, int p_77663_4_, boolean p_77663_5_)
    {
        if (!p_77663_2_.isRemote)
        {
            MapData mapdata = this.getMapData(p_77663_1_, p_77663_2_);

            if (p_77663_3_ instanceof EntityPlayer)
            {
                EntityPlayer entityplayer = (EntityPlayer)p_77663_3_;
                mapdata.updateVisiblePlayers(entityplayer, p_77663_1_);
            }

            if (p_77663_5_)
            {
                this.updateMapData(p_77663_2_, p_77663_3_, mapdata);
            }
        }
    }

    public Packet func_150911_c(ItemStack p_150911_1_, World p_150911_2_, EntityPlayer p_150911_3_)
    {
        byte[] abyte = this.getMapData(p_150911_1_, p_150911_2_).getUpdatePacketData(p_150911_1_, p_150911_2_, p_150911_3_);
        return abyte == null ? null : new S34PacketMaps(p_150911_1_.getItemDamage(), abyte);
    }

    /**
     * Called when item is crafted/smelted. Used only by maps so far.
     */
    public void onCreated(ItemStack p_77622_1_, World p_77622_2_, EntityPlayer p_77622_3_)
    {
        if (p_77622_1_.hasTagCompound() && p_77622_1_.getTagCompound().getBoolean("map_is_scaling"))
        {
            MapData mapdata = Items.filled_map.getMapData(p_77622_1_, p_77622_2_);
            p_77622_1_.setItemDamage(p_77622_2_.getUniqueDataId("map"));
            MapData mapdata1 = new MapData("map_" + p_77622_1_.getItemDamage());
            mapdata1.scale = (byte)(mapdata.scale + 1);

            if (mapdata1.scale > 4)
            {
                mapdata1.scale = 4;
            }

            mapdata1.xCenter = mapdata.xCenter;
            mapdata1.zCenter = mapdata.zCenter;
            mapdata1.dimension = mapdata.dimension;
            mapdata1.markDirty();
            p_77622_2_.setItemData("map_" + p_77622_1_.getItemDamage(), mapdata1);
        }
    }

    /**
     * allows items to add custom lines of information to the mouseover description
     */
    @SideOnly(Side.CLIENT)
    public void addInformation(ItemStack p_77624_1_, EntityPlayer p_77624_2_, List p_77624_3_, boolean p_77624_4_)
    {
        MapData mapdata = this.getMapData(p_77624_1_, p_77624_2_.worldObj);

        if (p_77624_4_)
        {
            if (mapdata == null)
            {
                p_77624_3_.add("Unknown map");
            }
            else
            {
                p_77624_3_.add("Scaling at 1:" + (1 << mapdata.scale));
                p_77624_3_.add("(Level " + mapdata.scale + "/" + 4 + ")");
            }
        }
    }
}