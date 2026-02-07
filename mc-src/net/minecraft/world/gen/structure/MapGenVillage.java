package net.minecraft.world.gen.structure;

import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Map.Entry;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;
import net.minecraft.world.biome.BiomeGenBase;

public class MapGenVillage extends MapGenStructure
{
    /** A list of all the biomes villages can spawn in. */
    public static List villageSpawnBiomes = Arrays.asList(new BiomeGenBase[] {BiomeGenBase.plains, BiomeGenBase.desert, BiomeGenBase.savanna});
    /** World terrain type, 0 for normal, 1 for flat map */
    private int terrainType;
    private int field_82665_g;
    private int field_82666_h;
    private static final String __OBFID = "CL_00000514";

    public MapGenVillage()
    {
        this.field_82665_g = 32;
        this.field_82666_h = 8;
    }

    public MapGenVillage(Map p_i2093_1_)
    {
        this();
        Iterator iterator = p_i2093_1_.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();

            if (((String)entry.getKey()).equals("size"))
            {
                this.terrainType = MathHelper.parseIntWithDefaultAndMax((String)entry.getValue(), this.terrainType, 0);
            }
            else if (((String)entry.getKey()).equals("distance"))
            {
                this.field_82665_g = MathHelper.parseIntWithDefaultAndMax((String)entry.getValue(), this.field_82665_g, this.field_82666_h + 1);
            }
        }
    }

    public String func_143025_a()
    {
        return "Village";
    }

    protected boolean canSpawnStructureAtCoords(int p_75047_1_, int p_75047_2_)
    {
        int k = p_75047_1_;
        int l = p_75047_2_;

        if (p_75047_1_ < 0)
        {
            p_75047_1_ -= this.field_82665_g - 1;
        }

        if (p_75047_2_ < 0)
        {
            p_75047_2_ -= this.field_82665_g - 1;
        }

        int i1 = p_75047_1_ / this.field_82665_g;
        int j1 = p_75047_2_ / this.field_82665_g;
        Random random = this.worldObj.setRandomSeed(i1, j1, 10387312);
        i1 *= this.field_82665_g;
        j1 *= this.field_82665_g;
        i1 += random.nextInt(this.field_82665_g - this.field_82666_h);
        j1 += random.nextInt(this.field_82665_g - this.field_82666_h);

        if (k == i1 && l == j1)
        {
            boolean flag = this.worldObj.getWorldChunkManager().areBiomesViable(k * 16 + 8, l * 16 + 8, 0, villageSpawnBiomes);

            if (flag)
            {
                return true;
            }
        }

        return false;
    }

    protected StructureStart getStructureStart(int p_75049_1_, int p_75049_2_)
    {
        return new MapGenVillage.Start(this.worldObj, this.rand, p_75049_1_, p_75049_2_, this.terrainType);
    }

    public static class Start extends StructureStart
        {
            /** well ... thats what it does */
            private boolean hasMoreThanTwoComponents;
            private static final String __OBFID = "CL_00000515";

            public Start() {}

            public Start(World p_i2092_1_, Random p_i2092_2_, int p_i2092_3_, int p_i2092_4_, int p_i2092_5_)
            {
                super(p_i2092_3_, p_i2092_4_);
                List list = StructureVillagePieces.getStructureVillageWeightedPieceList(p_i2092_2_, p_i2092_5_);
                StructureVillagePieces.Start start = new StructureVillagePieces.Start(p_i2092_1_.getWorldChunkManager(), 0, p_i2092_2_, (p_i2092_3_ << 4) + 2, (p_i2092_4_ << 4) + 2, list, p_i2092_5_);
                this.components.add(start);
                start.buildComponent(start, this.components, p_i2092_2_);
                List list1 = start.field_74930_j;
                List list2 = start.field_74932_i;
                int l;

                while (!list1.isEmpty() || !list2.isEmpty())
                {
                    StructureComponent structurecomponent;

                    if (list1.isEmpty())
                    {
                        l = p_i2092_2_.nextInt(list2.size());
                        structurecomponent = (StructureComponent)list2.remove(l);
                        structurecomponent.buildComponent(start, this.components, p_i2092_2_);
                    }
                    else
                    {
                        l = p_i2092_2_.nextInt(list1.size());
                        structurecomponent = (StructureComponent)list1.remove(l);
                        structurecomponent.buildComponent(start, this.components, p_i2092_2_);
                    }
                }

                this.updateBoundingBox();
                l = 0;
                Iterator iterator = this.components.iterator();

                while (iterator.hasNext())
                {
                    StructureComponent structurecomponent1 = (StructureComponent)iterator.next();

                    if (!(structurecomponent1 instanceof StructureVillagePieces.Road))
                    {
                        ++l;
                    }
                }

                this.hasMoreThanTwoComponents = l > 2;
            }

            /**
             * currently only defined for Villages, returns true if Village has more than 2 non-road components
             */
            public boolean isSizeableStructure()
            {
                return this.hasMoreThanTwoComponents;
            }

            public void func_143022_a(NBTTagCompound p_143022_1_)
            {
                super.func_143022_a(p_143022_1_);
                p_143022_1_.setBoolean("Valid", this.hasMoreThanTwoComponents);
            }

            public void func_143017_b(NBTTagCompound p_143017_1_)
            {
                super.func_143017_b(p_143017_1_);
                this.hasMoreThanTwoComponents = p_143017_1_.getBoolean("Valid");
            }
        }
}