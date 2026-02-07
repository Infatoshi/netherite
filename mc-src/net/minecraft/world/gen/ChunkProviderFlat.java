package net.minecraft.world.gen;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.init.Blocks;
import net.minecraft.util.IProgressUpdate;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
import net.minecraft.world.gen.feature.WorldGenDungeons;
import net.minecraft.world.gen.feature.WorldGenLakes;
import net.minecraft.world.gen.structure.MapGenMineshaft;
import net.minecraft.world.gen.structure.MapGenScatteredFeature;
import net.minecraft.world.gen.structure.MapGenStronghold;
import net.minecraft.world.gen.structure.MapGenStructure;
import net.minecraft.world.gen.structure.MapGenVillage;

public class ChunkProviderFlat implements IChunkProvider
{
    private World worldObj;
    private Random random;
    private final Block[] cachedBlockIDs = new Block[256];
    private final byte[] cachedBlockMetadata = new byte[256];
    private final FlatGeneratorInfo flatWorldGenInfo;
    private final List structureGenerators = new ArrayList();
    private final boolean hasDecoration;
    private final boolean hasDungeons;
    private WorldGenLakes waterLakeGenerator;
    private WorldGenLakes lavaLakeGenerator;
    private static final String __OBFID = "CL_00000391";

    public ChunkProviderFlat(World p_i2004_1_, long p_i2004_2_, boolean p_i2004_4_, String p_i2004_5_)
    {
        this.worldObj = p_i2004_1_;
        this.random = new Random(p_i2004_2_);
        this.flatWorldGenInfo = FlatGeneratorInfo.createFlatGeneratorFromString(p_i2004_5_);

        if (p_i2004_4_)
        {
            Map map = this.flatWorldGenInfo.getWorldFeatures();

            if (map.containsKey("village"))
            {
                Map map1 = (Map)map.get("village");

                if (!map1.containsKey("size"))
                {
                    map1.put("size", "1");
                }

                this.structureGenerators.add(new MapGenVillage(map1));
            }

            if (map.containsKey("biome_1"))
            {
                this.structureGenerators.add(new MapGenScatteredFeature((Map)map.get("biome_1")));
            }

            if (map.containsKey("mineshaft"))
            {
                this.structureGenerators.add(new MapGenMineshaft((Map)map.get("mineshaft")));
            }

            if (map.containsKey("stronghold"))
            {
                this.structureGenerators.add(new MapGenStronghold((Map)map.get("stronghold")));
            }
        }

        this.hasDecoration = this.flatWorldGenInfo.getWorldFeatures().containsKey("decoration");

        if (this.flatWorldGenInfo.getWorldFeatures().containsKey("lake"))
        {
            this.waterLakeGenerator = new WorldGenLakes(Blocks.water);
        }

        if (this.flatWorldGenInfo.getWorldFeatures().containsKey("lava_lake"))
        {
            this.lavaLakeGenerator = new WorldGenLakes(Blocks.lava);
        }

        this.hasDungeons = this.flatWorldGenInfo.getWorldFeatures().containsKey("dungeon");
        Iterator iterator = this.flatWorldGenInfo.getFlatLayers().iterator();

        while (iterator.hasNext())
        {
            FlatLayerInfo flatlayerinfo = (FlatLayerInfo)iterator.next();

            for (int j = flatlayerinfo.getMinY(); j < flatlayerinfo.getMinY() + flatlayerinfo.getLayerCount(); ++j)
            {
                this.cachedBlockIDs[j] = flatlayerinfo.func_151536_b();
                this.cachedBlockMetadata[j] = (byte)flatlayerinfo.getFillBlockMeta();
            }
        }
    }

    /**
     * loads or generates the chunk at the chunk location specified
     */
    public Chunk loadChunk(int p_73158_1_, int p_73158_2_)
    {
        return this.provideChunk(p_73158_1_, p_73158_2_);
    }

    /**
     * Will return back a chunk, if it doesn't exist and its not a MP client it will generates all the blocks for the
     * specified chunk from the map seed and chunk seed
     */
    public Chunk provideChunk(int p_73154_1_, int p_73154_2_)
    {
        Chunk chunk = new Chunk(this.worldObj, p_73154_1_, p_73154_2_);
        int l;

        for (int k = 0; k < this.cachedBlockIDs.length; ++k)
        {
            Block block = this.cachedBlockIDs[k];

            if (block != null)
            {
                l = k >> 4;
                ExtendedBlockStorage extendedblockstorage = chunk.getBlockStorageArray()[l];

                if (extendedblockstorage == null)
                {
                    extendedblockstorage = new ExtendedBlockStorage(k, !this.worldObj.provider.hasNoSky);
                    chunk.getBlockStorageArray()[l] = extendedblockstorage;
                }

                for (int i1 = 0; i1 < 16; ++i1)
                {
                    for (int j1 = 0; j1 < 16; ++j1)
                    {
                        extendedblockstorage.func_150818_a(i1, k & 15, j1, block);
                        extendedblockstorage.setExtBlockMetadata(i1, k & 15, j1, this.cachedBlockMetadata[k]);
                    }
                }
            }
        }

        chunk.generateSkylightMap();
        BiomeGenBase[] abiomegenbase = this.worldObj.getWorldChunkManager().loadBlockGeneratorData((BiomeGenBase[])null, p_73154_1_ * 16, p_73154_2_ * 16, 16, 16);
        byte[] abyte = chunk.getBiomeArray();

        for (l = 0; l < abyte.length; ++l)
        {
            abyte[l] = (byte)abiomegenbase[l].biomeID;
        }

        Iterator iterator = this.structureGenerators.iterator();

        while (iterator.hasNext())
        {
            MapGenBase mapgenbase = (MapGenBase)iterator.next();
            mapgenbase.func_151539_a(this, this.worldObj, p_73154_1_, p_73154_2_, (Block[])null);
        }

        chunk.generateSkylightMap();
        return chunk;
    }

    /**
     * Checks to see if a chunk exists at x, y
     */
    public boolean chunkExists(int p_73149_1_, int p_73149_2_)
    {
        return true;
    }

    /**
     * Populates chunk with ores etc etc
     */
    public void populate(IChunkProvider p_73153_1_, int p_73153_2_, int p_73153_3_)
    {
        int k = p_73153_2_ * 16;
        int l = p_73153_3_ * 16;
        BiomeGenBase biomegenbase = this.worldObj.getBiomeGenForCoords(k + 16, l + 16);
        boolean flag = false;
        this.random.setSeed(this.worldObj.getSeed());
        long i1 = this.random.nextLong() / 2L * 2L + 1L;
        long j1 = this.random.nextLong() / 2L * 2L + 1L;
        this.random.setSeed((long)p_73153_2_ * i1 + (long)p_73153_3_ * j1 ^ this.worldObj.getSeed());
        Iterator iterator = this.structureGenerators.iterator();

        while (iterator.hasNext())
        {
            MapGenStructure mapgenstructure = (MapGenStructure)iterator.next();
            boolean flag1 = mapgenstructure.generateStructuresInChunk(this.worldObj, this.random, p_73153_2_, p_73153_3_);

            if (mapgenstructure instanceof MapGenVillage)
            {
                flag |= flag1;
            }
        }

        int l1;
        int i2;
        int j2;

        if (this.waterLakeGenerator != null && !flag && this.random.nextInt(4) == 0)
        {
            l1 = k + this.random.nextInt(16) + 8;
            i2 = this.random.nextInt(256);
            j2 = l + this.random.nextInt(16) + 8;
            this.waterLakeGenerator.generate(this.worldObj, this.random, l1, i2, j2);
        }

        if (this.lavaLakeGenerator != null && !flag && this.random.nextInt(8) == 0)
        {
            l1 = k + this.random.nextInt(16) + 8;
            i2 = this.random.nextInt(this.random.nextInt(248) + 8);
            j2 = l + this.random.nextInt(16) + 8;

            if (i2 < 63 || this.random.nextInt(10) == 0)
            {
                this.lavaLakeGenerator.generate(this.worldObj, this.random, l1, i2, j2);
            }
        }

        if (this.hasDungeons)
        {
            for (l1 = 0; l1 < 8; ++l1)
            {
                i2 = k + this.random.nextInt(16) + 8;
                j2 = this.random.nextInt(256);
                int k1 = l + this.random.nextInt(16) + 8;
                (new WorldGenDungeons()).generate(this.worldObj, this.random, i2, j2, k1);
            }
        }

        if (this.hasDecoration)
        {
            biomegenbase.decorate(this.worldObj, this.random, k, l);
        }
    }

    /**
     * Two modes of operation: if passed true, save all Chunks in one go.  If passed false, save up to two chunks.
     * Return true if all chunks have been saved.
     */
    public boolean saveChunks(boolean p_73151_1_, IProgressUpdate p_73151_2_)
    {
        return true;
    }

    /**
     * Save extra data not associated with any Chunk.  Not saved during autosave, only during world unload.  Currently
     * unimplemented.
     */
    public void saveExtraData() {}

    /**
     * Unloads chunks that are marked to be unloaded. This is not guaranteed to unload every such chunk.
     */
    public boolean unloadQueuedChunks()
    {
        return false;
    }

    /**
     * Returns if the IChunkProvider supports saving.
     */
    public boolean canSave()
    {
        return true;
    }

    /**
     * Converts the instance data to a readable string.
     */
    public String makeString()
    {
        return "FlatLevelSource";
    }

    /**
     * Returns a list of creatures of the specified type that can spawn at the given location.
     */
    public List getPossibleCreatures(EnumCreatureType p_73155_1_, int p_73155_2_, int p_73155_3_, int p_73155_4_)
    {
        BiomeGenBase biomegenbase = this.worldObj.getBiomeGenForCoords(p_73155_2_, p_73155_4_);
        return biomegenbase.getSpawnableList(p_73155_1_);
    }

    public ChunkPosition func_147416_a(World p_147416_1_, String p_147416_2_, int p_147416_3_, int p_147416_4_, int p_147416_5_)
    {
        if ("Stronghold".equals(p_147416_2_))
        {
            Iterator iterator = this.structureGenerators.iterator();

            while (iterator.hasNext())
            {
                MapGenStructure mapgenstructure = (MapGenStructure)iterator.next();

                if (mapgenstructure instanceof MapGenStronghold)
                {
                    return mapgenstructure.func_151545_a(p_147416_1_, p_147416_3_, p_147416_4_, p_147416_5_);
                }
            }
        }

        return null;
    }

    public int getLoadedChunkCount()
    {
        return 0;
    }

    public void recreateStructures(int p_82695_1_, int p_82695_2_)
    {
        Iterator iterator = this.structureGenerators.iterator();

        while (iterator.hasNext())
        {
            MapGenStructure mapgenstructure = (MapGenStructure)iterator.next();
            mapgenstructure.func_151539_a(this, this.worldObj, p_82695_1_, p_82695_2_, (Block[])null);
        }
    }
}