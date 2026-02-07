package net.minecraft.world.chunk.storage;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.nbt.CompressedStreamTools;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.MinecraftException;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.NibbleArray;
import net.minecraft.world.storage.IThreadedFileIO;
import net.minecraft.world.storage.ThreadedFileIOBase;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.world.ChunkDataEvent;

import org.apache.logging.log4j.Level;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import cpw.mods.fml.common.FMLLog;

public class AnvilChunkLoader implements IChunkLoader, IThreadedFileIO
{
    private static final Logger logger = LogManager.getLogger();
    private List chunksToRemove = new ArrayList();
    private Set pendingAnvilChunksCoordinates = new HashSet();
    private Object syncLockObject = new Object();
    /** Save directory for chunks using the Anvil format */
    public final File chunkSaveLocation;
    private static final String __OBFID = "CL_00000384";

    public AnvilChunkLoader(File p_i2003_1_)
    {
        this.chunkSaveLocation = p_i2003_1_;
    }

    public boolean chunkExists(World world, int i, int j)
    {
        ChunkCoordIntPair chunkcoordintpair = new ChunkCoordIntPair(i, j);

        synchronized (this.syncLockObject)
        {
            if (this.pendingAnvilChunksCoordinates.contains(chunkcoordintpair))
            {
                Iterator iter = this.chunksToRemove.iterator();
                while (iter.hasNext())
                {
                    PendingChunk pendingChunk = (PendingChunk)iter.next();
                    if (pendingChunk.chunkCoordinate.equals(chunkcoordintpair))
                    {
                        return true;
                    }
                }
            }
        }

        return RegionFileCache.createOrLoadRegionFile(this.chunkSaveLocation, i, j).chunkExists(i & 31, j & 31);
    }

    /**
     * Loads the specified(XZ) chunk into the specified world.
     */
    public Chunk loadChunk(World p_75815_1_, int p_75815_2_, int p_75815_3_) throws IOException
    {
        Object[] data = this.loadChunk__Async(p_75815_1_, p_75815_2_, p_75815_3_);

        if (data != null)
        {
            Chunk chunk = (Chunk) data[0];
            NBTTagCompound nbttagcompound = (NBTTagCompound) data[1];
            this.loadEntities(p_75815_1_, nbttagcompound.getCompoundTag("Level"), chunk);
            return chunk;
        }

        return null;
    }

    public Object[] loadChunk__Async(World p_75815_1_, int p_75815_2_, int p_75815_3_) throws IOException
    {
        NBTTagCompound nbttagcompound = null;
        ChunkCoordIntPair chunkcoordintpair = new ChunkCoordIntPair(p_75815_2_, p_75815_3_);
        Object object = this.syncLockObject;

        synchronized (this.syncLockObject)
        {
            if (this.pendingAnvilChunksCoordinates.contains(chunkcoordintpair))
            {
                Iterator iter = this.chunksToRemove.iterator();
                while (iter.hasNext())
                {
                    PendingChunk pendingChunk = (PendingChunk)iter.next();
                    if (pendingChunk.chunkCoordinate.equals(chunkcoordintpair))
                    {
                        nbttagcompound = pendingChunk.nbtTags;
                        break;
                    }
                }
            }
        }

        if (nbttagcompound == null)
        {
            DataInputStream datainputstream = RegionFileCache.getChunkInputStream(this.chunkSaveLocation, p_75815_2_, p_75815_3_);

            if (datainputstream == null)
            {
                return null;
            }

            nbttagcompound = CompressedStreamTools.read(datainputstream);
        }

        return this.checkedReadChunkFromNBT__Async(p_75815_1_, p_75815_2_, p_75815_3_, nbttagcompound);
    }

    /**
     * Wraps readChunkFromNBT. Checks the coordinates and several NBT tags.
     */
    protected Chunk checkedReadChunkFromNBT(World p_75822_1_, int p_75822_2_, int p_75822_3_, NBTTagCompound p_75822_4_)
    {
        Object[] data = this.checkedReadChunkFromNBT__Async(p_75822_1_, p_75822_2_, p_75822_3_, p_75822_4_);

        if (data != null)
        {
            Chunk chunk = (Chunk) data[0];
            return chunk;
        }

        return null;
    }

    protected Object[] checkedReadChunkFromNBT__Async(World p_75822_1_, int p_75822_2_, int p_75822_3_, NBTTagCompound p_75822_4_)
    {
        if (!p_75822_4_.hasKey("Level", 10))
        {
            logger.error("Chunk file at " + p_75822_2_ + "," + p_75822_3_ + " is missing level data, skipping");
            return null;
        }
        else if (!p_75822_4_.getCompoundTag("Level").hasKey("Sections", 9))
        {
            logger.error("Chunk file at " + p_75822_2_ + "," + p_75822_3_ + " is missing block data, skipping");
            return null;
        }
        else
        {
            Chunk chunk = this.readChunkFromNBT(p_75822_1_, p_75822_4_.getCompoundTag("Level"));

            if (!chunk.isAtLocation(p_75822_2_, p_75822_3_))
            {
                logger.error("Chunk file at " + p_75822_2_ + "," + p_75822_3_ + " is in the wrong location; relocating. (Expected " + p_75822_2_ + ", " + p_75822_3_ + ", got " + chunk.xPosition + ", " + chunk.zPosition + ")");
                p_75822_4_.setInteger("xPos", p_75822_2_);
                p_75822_4_.setInteger("zPos", p_75822_3_);
                // Have to move tile entities since we don't load them at this stage
                NBTTagList tileEntities = p_75822_4_.getCompoundTag("Level").getTagList("TileEntities", 10);

                if (tileEntities != null)
                {
                    for (int te = 0; te < tileEntities.tagCount(); te++)
                    {
                        NBTTagCompound tileEntity = (NBTTagCompound) tileEntities.getCompoundTagAt(te);
                        int x = tileEntity.getInteger("x") - chunk.xPosition * 16;
                        int z = tileEntity.getInteger("z") - chunk.zPosition * 16;
                        tileEntity.setInteger("x", p_75822_2_ * 16 + x);
                        tileEntity.setInteger("z", p_75822_3_ * 16 + z);
                    }
                }

                chunk = this.readChunkFromNBT(p_75822_1_, p_75822_4_.getCompoundTag("Level"));
            }

            Object[] data = new Object[2];
            data[0] = chunk;
            data[1] = p_75822_4_;
            // event is fired in ChunkIOProvider.callStage2 since it must be fired after TE's load.
            // MinecraftForge.EVENT_BUS.post(new ChunkDataEvent.Load(chunk, par4NBTTagCompound));
            return data;
        }
    }

    public void saveChunk(World p_75816_1_, Chunk p_75816_2_) throws MinecraftException, IOException
    {
        p_75816_1_.checkSessionLock();

        try
        {
            NBTTagCompound nbttagcompound = new NBTTagCompound();
            NBTTagCompound nbttagcompound1 = new NBTTagCompound();
            nbttagcompound.setTag("Level", nbttagcompound1);
            this.writeChunkToNBT(p_75816_2_, p_75816_1_, nbttagcompound1);
            MinecraftForge.EVENT_BUS.post(new ChunkDataEvent.Save(p_75816_2_, nbttagcompound));
            this.addChunkToPending(p_75816_2_.getChunkCoordIntPair(), nbttagcompound);
        }
        catch (Exception exception)
        {
            exception.printStackTrace();
        }
    }

    protected void addChunkToPending(ChunkCoordIntPair p_75824_1_, NBTTagCompound p_75824_2_)
    {
        Object object = this.syncLockObject;

        synchronized (this.syncLockObject)
        {
            if (this.pendingAnvilChunksCoordinates.contains(p_75824_1_))
            {
                for (int i = 0; i < this.chunksToRemove.size(); ++i)
                {
                    if (((AnvilChunkLoader.PendingChunk)this.chunksToRemove.get(i)).chunkCoordinate.equals(p_75824_1_))
                    {
                        this.chunksToRemove.set(i, new AnvilChunkLoader.PendingChunk(p_75824_1_, p_75824_2_));
                        return;
                    }
                }
            }

            this.chunksToRemove.add(new AnvilChunkLoader.PendingChunk(p_75824_1_, p_75824_2_));
            this.pendingAnvilChunksCoordinates.add(p_75824_1_);
            ThreadedFileIOBase.threadedIOInstance.queueIO(this);
        }
    }

    /**
     * Returns a boolean stating if the write was unsuccessful.
     */
    public boolean writeNextIO()
    {
        AnvilChunkLoader.PendingChunk pendingchunk = null;
        Object object = this.syncLockObject;

        synchronized (this.syncLockObject)
        {
            if (this.chunksToRemove.isEmpty())
            {
                return false;
            }

            pendingchunk = (AnvilChunkLoader.PendingChunk)this.chunksToRemove.remove(0);
            this.pendingAnvilChunksCoordinates.remove(pendingchunk.chunkCoordinate);
        }

        if (pendingchunk != null)
        {
            try
            {
                this.writeChunkNBTTags(pendingchunk);
            }
            catch (Exception exception)
            {
                exception.printStackTrace();
            }
        }

        return true;
    }

    private void writeChunkNBTTags(AnvilChunkLoader.PendingChunk p_75821_1_) throws IOException
    {
        DataOutputStream dataoutputstream = RegionFileCache.getChunkOutputStream(this.chunkSaveLocation, p_75821_1_.chunkCoordinate.chunkXPos, p_75821_1_.chunkCoordinate.chunkZPos);
        CompressedStreamTools.write(p_75821_1_.nbtTags, dataoutputstream);
        dataoutputstream.close();
    }

    /**
     * Save extra data associated with this Chunk not normally saved during autosave, only during chunk unload.
     * Currently unused.
     */
    public void saveExtraChunkData(World p_75819_1_, Chunk p_75819_2_) {}

    /**
     * Called every World.tick()
     */
    public void chunkTick() {}

    /**
     * Save extra data not associated with any Chunk.  Not saved during autosave, only during world unload.  Currently
     * unused.
     */
    public void saveExtraData()
    {
        while (this.writeNextIO())
        {
            ;
        }
    }

    /**
     * Writes the Chunk passed as an argument to the NBTTagCompound also passed, using the World argument to retrieve
     * the Chunk's last update time.
     */
    private void writeChunkToNBT(Chunk p_75820_1_, World p_75820_2_, NBTTagCompound p_75820_3_)
    {
        p_75820_3_.setByte("V", (byte)1);
        p_75820_3_.setInteger("xPos", p_75820_1_.xPosition);
        p_75820_3_.setInteger("zPos", p_75820_1_.zPosition);
        p_75820_3_.setLong("LastUpdate", p_75820_2_.getTotalWorldTime());
        p_75820_3_.setIntArray("HeightMap", p_75820_1_.heightMap);
        p_75820_3_.setBoolean("TerrainPopulated", p_75820_1_.isTerrainPopulated);
        p_75820_3_.setBoolean("LightPopulated", p_75820_1_.isLightPopulated);
        p_75820_3_.setLong("InhabitedTime", p_75820_1_.inhabitedTime);
        ExtendedBlockStorage[] aextendedblockstorage = p_75820_1_.getBlockStorageArray();
        NBTTagList nbttaglist = new NBTTagList();
        boolean flag = !p_75820_2_.provider.hasNoSky;
        ExtendedBlockStorage[] aextendedblockstorage1 = aextendedblockstorage;
        int i = aextendedblockstorage.length;
        NBTTagCompound nbttagcompound1;

        for (int j = 0; j < i; ++j)
        {
            ExtendedBlockStorage extendedblockstorage = aextendedblockstorage1[j];

            if (extendedblockstorage != null)
            {
                nbttagcompound1 = new NBTTagCompound();
                nbttagcompound1.setByte("Y", (byte)(extendedblockstorage.getYLocation() >> 4 & 255));
                nbttagcompound1.setByteArray("Blocks", extendedblockstorage.getBlockLSBArray());

                if (extendedblockstorage.getBlockMSBArray() != null)
                {
                    nbttagcompound1.setByteArray("Add", extendedblockstorage.getBlockMSBArray().data);
                }

                nbttagcompound1.setByteArray("Data", extendedblockstorage.getMetadataArray().data);
                nbttagcompound1.setByteArray("BlockLight", extendedblockstorage.getBlocklightArray().data);

                if (flag)
                {
                    nbttagcompound1.setByteArray("SkyLight", extendedblockstorage.getSkylightArray().data);
                }
                else
                {
                    nbttagcompound1.setByteArray("SkyLight", new byte[extendedblockstorage.getBlocklightArray().data.length]);
                }

                nbttaglist.appendTag(nbttagcompound1);
            }
        }

        p_75820_3_.setTag("Sections", nbttaglist);
        p_75820_3_.setByteArray("Biomes", p_75820_1_.getBiomeArray());
        p_75820_1_.hasEntities = false;
        NBTTagList nbttaglist2 = new NBTTagList();
        Iterator iterator1;

        for (i = 0; i < p_75820_1_.entityLists.length; ++i)
        {
            iterator1 = p_75820_1_.entityLists[i].iterator();

            while (iterator1.hasNext())
            {
                Entity entity = (Entity)iterator1.next();
                nbttagcompound1 = new NBTTagCompound();

                try
                {
                    if (entity.writeToNBTOptional(nbttagcompound1))
                    {
                        p_75820_1_.hasEntities = true;
                        nbttaglist2.appendTag(nbttagcompound1);
                    }
                }
                catch (Exception e)
                {
                    FMLLog.log(Level.ERROR, e,
                            "An Entity type %s has thrown an exception trying to write state. It will not persist. Report this to the mod author",
                            entity.getClass().getName());
                }
            }
        }

        p_75820_3_.setTag("Entities", nbttaglist2);
        NBTTagList nbttaglist3 = new NBTTagList();
        iterator1 = p_75820_1_.chunkTileEntityMap.values().iterator();

        while (iterator1.hasNext())
        {
            TileEntity tileentity = (TileEntity)iterator1.next();
            nbttagcompound1 = new NBTTagCompound();
            try {
            tileentity.writeToNBT(nbttagcompound1);
            nbttaglist3.appendTag(nbttagcompound1);
            }
            catch (Exception e)
            {
                FMLLog.log(Level.ERROR, e,
                        "A TileEntity type %s has throw an exception trying to write state. It will not persist. Report this to the mod author",
                        tileentity.getClass().getName());
            }
        }

        p_75820_3_.setTag("TileEntities", nbttaglist3);
        List list = p_75820_2_.getPendingBlockUpdates(p_75820_1_, false);

        if (list != null)
        {
            long k = p_75820_2_.getTotalWorldTime();
            NBTTagList nbttaglist1 = new NBTTagList();
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                NextTickListEntry nextticklistentry = (NextTickListEntry)iterator.next();
                NBTTagCompound nbttagcompound2 = new NBTTagCompound();
                nbttagcompound2.setInteger("i", Block.getIdFromBlock(nextticklistentry.func_151351_a()));
                nbttagcompound2.setInteger("x", nextticklistentry.xCoord);
                nbttagcompound2.setInteger("y", nextticklistentry.yCoord);
                nbttagcompound2.setInteger("z", nextticklistentry.zCoord);
                nbttagcompound2.setInteger("t", (int)(nextticklistentry.scheduledTime - k));
                nbttagcompound2.setInteger("p", nextticklistentry.priority);
                nbttaglist1.appendTag(nbttagcompound2);
            }

            p_75820_3_.setTag("TileTicks", nbttaglist1);
        }
    }

    /**
     * Reads the data stored in the passed NBTTagCompound and creates a Chunk with that data in the passed World.
     * Returns the created Chunk.
     */
    private Chunk readChunkFromNBT(World p_75823_1_, NBTTagCompound p_75823_2_)
    {
        int i = p_75823_2_.getInteger("xPos");
        int j = p_75823_2_.getInteger("zPos");
        Chunk chunk = new Chunk(p_75823_1_, i, j);
        chunk.heightMap = p_75823_2_.getIntArray("HeightMap");
        chunk.isTerrainPopulated = p_75823_2_.getBoolean("TerrainPopulated");
        chunk.isLightPopulated = p_75823_2_.getBoolean("LightPopulated");
        chunk.inhabitedTime = p_75823_2_.getLong("InhabitedTime");
        NBTTagList nbttaglist = p_75823_2_.getTagList("Sections", 10);
        byte b0 = 16;
        ExtendedBlockStorage[] aextendedblockstorage = new ExtendedBlockStorage[b0];
        boolean flag = !p_75823_1_.provider.hasNoSky;

        for (int k = 0; k < nbttaglist.tagCount(); ++k)
        {
            NBTTagCompound nbttagcompound1 = nbttaglist.getCompoundTagAt(k);
            byte b1 = nbttagcompound1.getByte("Y");
            ExtendedBlockStorage extendedblockstorage = new ExtendedBlockStorage(b1 << 4, flag);
            extendedblockstorage.setBlockLSBArray(nbttagcompound1.getByteArray("Blocks"));

            if (nbttagcompound1.hasKey("Add", 7))
            {
                extendedblockstorage.setBlockMSBArray(new NibbleArray(nbttagcompound1.getByteArray("Add"), 4));
            }

            extendedblockstorage.setBlockMetadataArray(new NibbleArray(nbttagcompound1.getByteArray("Data"), 4));
            extendedblockstorage.setBlocklightArray(new NibbleArray(nbttagcompound1.getByteArray("BlockLight"), 4));

            if (flag)
            {
                extendedblockstorage.setSkylightArray(new NibbleArray(nbttagcompound1.getByteArray("SkyLight"), 4));
            }

            extendedblockstorage.removeInvalidBlocks();
            aextendedblockstorage[b1] = extendedblockstorage;
        }

        chunk.setStorageArrays(aextendedblockstorage);

        if (p_75823_2_.hasKey("Biomes", 7))
        {
            chunk.setBiomeArray(p_75823_2_.getByteArray("Biomes"));
        }

        // End this method here and split off entity loading to another method
        return chunk;
    }

    public void loadEntities(World p_75823_1_, NBTTagCompound p_75823_2_, Chunk chunk)
    {
        NBTTagList nbttaglist1 = p_75823_2_.getTagList("Entities", 10);

        if (nbttaglist1 != null)
        {
            for (int l = 0; l < nbttaglist1.tagCount(); ++l)
            {
                NBTTagCompound nbttagcompound3 = nbttaglist1.getCompoundTagAt(l);
                Entity entity2 = EntityList.createEntityFromNBT(nbttagcompound3, p_75823_1_);
                chunk.hasEntities = true;

                if (entity2 != null)
                {
                    chunk.addEntity(entity2);
                    Entity entity = entity2;

                    for (NBTTagCompound nbttagcompound2 = nbttagcompound3; nbttagcompound2.hasKey("Riding", 10); nbttagcompound2 = nbttagcompound2.getCompoundTag("Riding"))
                    {
                        Entity entity1 = EntityList.createEntityFromNBT(nbttagcompound2.getCompoundTag("Riding"), p_75823_1_);

                        if (entity1 != null)
                        {
                            chunk.addEntity(entity1);
                            entity.mountEntity(entity1);
                        }

                        entity = entity1;
                    }
                }
            }
        }

        NBTTagList nbttaglist2 = p_75823_2_.getTagList("TileEntities", 10);

        if (nbttaglist2 != null)
        {
            for (int i1 = 0; i1 < nbttaglist2.tagCount(); ++i1)
            {
                NBTTagCompound nbttagcompound4 = nbttaglist2.getCompoundTagAt(i1);
                TileEntity tileentity = TileEntity.createAndLoadEntity(nbttagcompound4);

                if (tileentity != null)
                {
                    chunk.addTileEntity(tileentity);
                }
            }
        }

        if (p_75823_2_.hasKey("TileTicks", 9))
        {
            NBTTagList nbttaglist3 = p_75823_2_.getTagList("TileTicks", 10);

            if (nbttaglist3 != null)
            {
                for (int j1 = 0; j1 < nbttaglist3.tagCount(); ++j1)
                {
                    NBTTagCompound nbttagcompound5 = nbttaglist3.getCompoundTagAt(j1);
                    p_75823_1_.func_147446_b(nbttagcompound5.getInteger("x"), nbttagcompound5.getInteger("y"), nbttagcompound5.getInteger("z"), Block.getBlockById(nbttagcompound5.getInteger("i")), nbttagcompound5.getInteger("t"), nbttagcompound5.getInteger("p"));
                }
            }
        }

        // return chunk;
    }

    static class PendingChunk
        {
            public final ChunkCoordIntPair chunkCoordinate;
            public final NBTTagCompound nbtTags;
            private static final String __OBFID = "CL_00000385";

            public PendingChunk(ChunkCoordIntPair p_i2002_1_, NBTTagCompound p_i2002_2_)
            {
                this.chunkCoordinate = p_i2002_1_;
                this.nbtTags = p_i2002_2_;
            }
        }
}