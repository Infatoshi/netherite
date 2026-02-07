package net.minecraft.world.gen.structure;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Map.Entry;
import net.minecraft.util.MathHelper;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraftforge.common.BiomeManager;

public class MapGenStronghold extends MapGenStructure
{
    public final List field_151546_e;
    /** is spawned false and set true once the defined BiomeGenBases were compared with the present ones */
    private boolean ranBiomeCheck;
    private ChunkCoordIntPair[] structureCoords;
    private double field_82671_h;
    private int field_82672_i;
    private static final String __OBFID = "CL_00000481";

    public MapGenStronghold()
    {
        this.structureCoords = new ChunkCoordIntPair[3];
        this.field_82671_h = 32.0D;
        this.field_82672_i = 3;
        this.field_151546_e = new ArrayList();
        BiomeGenBase[] abiomegenbase = BiomeGenBase.getBiomeGenArray();
        int i = abiomegenbase.length;

        for (int j = 0; j < i; ++j)
        {
            BiomeGenBase biomegenbase = abiomegenbase[j];

            if (biomegenbase != null && biomegenbase.rootHeight > 0.0F && !BiomeManager.strongHoldBiomesBlackList.contains(biomegenbase))
            {
                this.field_151546_e.add(biomegenbase);
            }
        }
        for (BiomeGenBase biome : BiomeManager.strongHoldBiomes)
        {
            if (!this.field_151546_e.contains(biome))
            {
                this.field_151546_e.add(biome);
            }
        }
    }

    public MapGenStronghold(Map p_i2068_1_)
    {
        this();
        Iterator iterator = p_i2068_1_.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();

            if (((String)entry.getKey()).equals("distance"))
            {
                this.field_82671_h = MathHelper.parseDoubleWithDefaultAndMax((String)entry.getValue(), this.field_82671_h, 1.0D);
            }
            else if (((String)entry.getKey()).equals("count"))
            {
                this.structureCoords = new ChunkCoordIntPair[MathHelper.parseIntWithDefaultAndMax((String)entry.getValue(), this.structureCoords.length, 1)];
            }
            else if (((String)entry.getKey()).equals("spread"))
            {
                this.field_82672_i = MathHelper.parseIntWithDefaultAndMax((String)entry.getValue(), this.field_82672_i, 1);
            }
        }
    }

    public String func_143025_a()
    {
        return "Stronghold";
    }

    protected boolean canSpawnStructureAtCoords(int p_75047_1_, int p_75047_2_)
    {
        if (!this.ranBiomeCheck)
        {
            Random random = new Random();
            random.setSeed(this.worldObj.getSeed());
            double d0 = random.nextDouble() * Math.PI * 2.0D;
            int l = 1;

            for (int i1 = 0; i1 < this.structureCoords.length; ++i1)
            {
                double d1 = (1.25D * (double)l + random.nextDouble()) * this.field_82671_h * (double)l;
                int j1 = (int)Math.round(Math.cos(d0) * d1);
                int k1 = (int)Math.round(Math.sin(d0) * d1);
                ChunkPosition chunkposition = this.worldObj.getWorldChunkManager().findBiomePosition((j1 << 4) + 8, (k1 << 4) + 8, 112, this.field_151546_e, random);

                if (chunkposition != null)
                {
                    j1 = chunkposition.chunkPosX >> 4;
                    k1 = chunkposition.chunkPosZ >> 4;
                }

                this.structureCoords[i1] = new ChunkCoordIntPair(j1, k1);
                d0 += (Math.PI * 2D) * (double)l / (double)this.field_82672_i;

                if (i1 == this.field_82672_i)
                {
                    l += 2 + random.nextInt(5);
                    this.field_82672_i += 1 + random.nextInt(2);
                }
            }

            this.ranBiomeCheck = true;
        }

        ChunkCoordIntPair[] achunkcoordintpair = this.structureCoords;
        int l1 = achunkcoordintpair.length;

        for (int k = 0; k < l1; ++k)
        {
            ChunkCoordIntPair chunkcoordintpair = achunkcoordintpair[k];

            if (p_75047_1_ == chunkcoordintpair.chunkXPos && p_75047_2_ == chunkcoordintpair.chunkZPos)
            {
                return true;
            }
        }

        return false;
    }

    /**
     * Returns a list of other locations at which the structure generation has been run, or null if not relevant to this
     * structure generator.
     */
    protected List getCoordList()
    {
        ArrayList arraylist = new ArrayList();
        ChunkCoordIntPair[] achunkcoordintpair = this.structureCoords;
        int i = achunkcoordintpair.length;

        for (int j = 0; j < i; ++j)
        {
            ChunkCoordIntPair chunkcoordintpair = achunkcoordintpair[j];

            if (chunkcoordintpair != null)
            {
                arraylist.add(chunkcoordintpair.func_151349_a(64));
            }
        }

        return arraylist;
    }

    protected StructureStart getStructureStart(int p_75049_1_, int p_75049_2_)
    {
        MapGenStronghold.Start start;

        for (start = new MapGenStronghold.Start(this.worldObj, this.rand, p_75049_1_, p_75049_2_); start.getComponents().isEmpty() || ((StructureStrongholdPieces.Stairs2)start.getComponents().get(0)).strongholdPortalRoom == null; start = new MapGenStronghold.Start(this.worldObj, this.rand, p_75049_1_, p_75049_2_))
        {
            ;
        }

        return start;
    }

    public static class Start extends StructureStart
        {
            private static final String __OBFID = "CL_00000482";

            public Start() {}

            public Start(World p_i2067_1_, Random p_i2067_2_, int p_i2067_3_, int p_i2067_4_)
            {
                super(p_i2067_3_, p_i2067_4_);
                StructureStrongholdPieces.prepareStructurePieces();
                StructureStrongholdPieces.Stairs2 stairs2 = new StructureStrongholdPieces.Stairs2(0, p_i2067_2_, (p_i2067_3_ << 4) + 2, (p_i2067_4_ << 4) + 2);
                this.components.add(stairs2);
                stairs2.buildComponent(stairs2, this.components, p_i2067_2_);
                List list = stairs2.field_75026_c;

                while (!list.isEmpty())
                {
                    int k = p_i2067_2_.nextInt(list.size());
                    StructureComponent structurecomponent = (StructureComponent)list.remove(k);
                    structurecomponent.buildComponent(stairs2, this.components, p_i2067_2_);
                }

                this.updateBoundingBox();
                this.markAvailableHeight(p_i2067_1_, p_i2067_2_, 10);
            }
        }
}