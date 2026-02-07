package net.minecraft.world;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collection;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.block.BlockHopper;
import net.minecraft.block.BlockLiquid;
import net.minecraft.block.BlockSlab;
import net.minecraft.block.BlockSnow;
import net.minecraft.block.BlockStairs;
import net.minecraft.block.material.Material;
import net.minecraft.command.IEntitySelector;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.pathfinding.PathEntity;
import net.minecraft.pathfinding.PathFinder;
import net.minecraft.profiler.Profiler;
import net.minecraft.scoreboard.Scoreboard;
import net.minecraft.server.MinecraftServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.Direction;
import net.minecraft.util.Facing;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.ReportedException;
import net.minecraft.util.Vec3;
import net.minecraft.village.VillageCollection;
import net.minecraft.village.VillageSiege;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.biome.WorldChunkManager;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.ISaveHandler;
import net.minecraft.world.storage.MapStorage;
import net.minecraft.world.storage.WorldInfo;

import cpw.mods.fml.common.FMLLog;

import com.google.common.collect.ImmutableSetMultimap;

import net.minecraftforge.client.ForgeHooksClient;
import net.minecraftforge.common.ForgeChunkManager;
import net.minecraftforge.common.ForgeChunkManager.Ticket;
import net.minecraftforge.common.ForgeModContainer;
import net.minecraftforge.common.ForgeHooks;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.common.util.ForgeDirection;
import net.minecraftforge.common.WorldSpecificSaveHandler;
import net.minecraftforge.event.entity.EntityEvent;
import net.minecraftforge.event.entity.EntityJoinWorldEvent;
import net.minecraftforge.event.world.WorldEvent;
import net.minecraftforge.event.entity.PlaySoundAtEntityEvent;
import net.minecraft.entity.EnumCreatureType;

public abstract class World implements IBlockAccess
{
   /**
     * Used in the getEntitiesWithinAABB functions to expand the search area for entities.
     * Modders should change this variable to a higher value if it is less then the radius
     * of one of there entities.
     */
    public static double MAX_ENTITY_RADIUS = 2.0D;

    public final MapStorage perWorldStorage;

    /** boolean; if true updates scheduled by scheduleBlockUpdate happen immediately */
    public boolean scheduledUpdatesAreImmediate;
    /** A list of all Entities in all currently-loaded chunks */
    public List loadedEntityList = new ArrayList();
    protected List unloadedEntityList = new ArrayList();
    /** A list of the loaded tile entities in the world */
    public List loadedTileEntityList = new ArrayList();
    private List addedTileEntityList = new ArrayList();
    private List field_147483_b = new ArrayList();
    /** Array list of players in the world. */
    public List playerEntities = new ArrayList();
    /** a list of all the lightning entities */
    public List weatherEffects = new ArrayList();
    private long cloudColour = 16777215L;
    /** How much light is subtracted from full daylight */
    public int skylightSubtracted;
    /**
     * Contains the current Linear Congruential Generator seed for block updates. Used with an A value of 3 and a C
     * value of 0x3c6ef35f, producing a highly planar series of values ill-suited for choosing random blocks in a
     * 16x128x16 field.
     */
    protected int updateLCG = (new Random()).nextInt();
    /** magic number used to generate fast random numbers for 3d distribution within a chunk */
    protected final int DIST_HASH_MAGIC = 1013904223;
    public float prevRainingStrength;
    public float rainingStrength;
    public float prevThunderingStrength;
    public float thunderingStrength;
    /**
     * Set to 2 whenever a lightning bolt is generated in SSP. Decrements if > 0 in updateWeather(). Value appears to be
     * unused.
     */
    public int lastLightningBolt;
    /** Option > Difficulty setting (0 - 3) */
    public EnumDifficulty difficultySetting;
    /** RNG for World. */
    public Random rand = new Random();
    /** The WorldProvider instance that World uses. */
    public final WorldProvider provider;
    protected List worldAccesses = new ArrayList();
    /** Handles chunk operations and caching */
    protected IChunkProvider chunkProvider;
    protected final ISaveHandler saveHandler;
    /** holds information about a world (size on disk, time, spawn point, seed, ...) */
    protected WorldInfo worldInfo;
    /** Boolean that is set to true when trying to find a spawn point */
    public boolean findingSpawnPoint;
    public MapStorage mapStorage;
    public VillageCollection villageCollectionObj;
    protected final VillageSiege villageSiegeObj = new VillageSiege(this);
    public final Profiler theProfiler;
    private final Calendar theCalendar = Calendar.getInstance();
    protected Scoreboard worldScoreboard = new Scoreboard();
    /** This is set to true for client worlds, and false for server worlds. */
    public boolean isRemote;
    /** Positions to update */
    protected Set activeChunkSet = new HashSet();
    /** number of ticks until the next random ambients play */
    private int ambientTickCountdown;
    /** indicates if enemies are spawned or not */
    protected boolean spawnHostileMobs;
    /** A flag indicating whether we should spawn peaceful mobs. */
    protected boolean spawnPeacefulMobs;
    private ArrayList collidingBoundingBoxes;
    private boolean field_147481_N;
    /**
     * is a temporary list of blocks and light values used when updating light levels. Holds up to 32x32x32 blocks (the
     * maximum influence of a light source.) Every element is a packed bit value: 0000000000LLLLzzzzzzyyyyyyxxxxxx. The
     * 4-bit L is a light level used when darkening blocks. 6-bit numbers x, y and z represent the block's offset from
     * the original block, plus 32 (i.e. value of 31 would mean a -1 offset
     */
    int[] lightUpdateBlockList;
    private static final String __OBFID = "CL_00000140";
    public boolean restoringBlockSnapshots = false;
    public boolean captureBlockSnapshots = false;
    public ArrayList<net.minecraftforge.common.util.BlockSnapshot> capturedBlockSnapshots = new ArrayList<net.minecraftforge.common.util.BlockSnapshot>();

    /**
     * Gets the biome for a given set of x/z coordinates
     */
    public BiomeGenBase getBiomeGenForCoords(final int p_72807_1_, final int p_72807_2_)
    {
        return provider.getBiomeGenForCoords(p_72807_1_, p_72807_2_);
    }

    public BiomeGenBase getBiomeGenForCoordsBody(final int p_72807_1_, final int p_72807_2_)
    {
        if (this.blockExists(p_72807_1_, 0, p_72807_2_))
        {
            Chunk chunk = this.getChunkFromBlockCoords(p_72807_1_, p_72807_2_);

            try
            {
                return chunk.getBiomeGenForWorldCoords(p_72807_1_ & 15, p_72807_2_ & 15, this.provider.worldChunkMgr);
            }
            catch (Throwable throwable)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Getting biome");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Coordinates of biome request");
                crashreportcategory.addCrashSectionCallable("Location", new Callable()
                {
                    private static final String __OBFID = "CL_00000141";
                    public String call()
                    {
                        return CrashReportCategory.getLocationInfo(p_72807_1_, 0, p_72807_2_);
                    }
                });
                throw new ReportedException(crashreport);
            }
        }
        else
        {
            return this.provider.worldChunkMgr.getBiomeGenAt(p_72807_1_, p_72807_2_);
        }
    }

    public WorldChunkManager getWorldChunkManager()
    {
        return this.provider.worldChunkMgr;
    }

    @SideOnly(Side.CLIENT)
    public World(ISaveHandler p_i45368_1_, String p_i45368_2_, WorldProvider p_i45368_3_, WorldSettings p_i45368_4_, Profiler p_i45368_5_)
    {
        this.ambientTickCountdown = this.rand.nextInt(12000);
        this.spawnHostileMobs = true;
        this.spawnPeacefulMobs = true;
        this.collidingBoundingBoxes = new ArrayList();
        this.lightUpdateBlockList = new int[32768];
        this.saveHandler = p_i45368_1_;
        this.theProfiler = p_i45368_5_;
        this.worldInfo = new WorldInfo(p_i45368_4_, p_i45368_2_);
        this.provider = p_i45368_3_;
        perWorldStorage = new MapStorage((ISaveHandler)null);
    }

    // Broken up so that the WorldClient gets the chance to set the mapstorage object before the dimension initializes
    @SideOnly(Side.CLIENT)
    protected void finishSetup()
    {
        VillageCollection villagecollection = (VillageCollection)this.mapStorage.loadData(VillageCollection.class, "villages");

        if (villagecollection == null)
        {
            this.villageCollectionObj = new VillageCollection(this);
            this.mapStorage.setData("villages", this.villageCollectionObj);
        }
        else
        {
            this.villageCollectionObj = villagecollection;
            this.villageCollectionObj.func_82566_a(this);
        }

        // Guarantee the dimension ID was not reset by the provider
        int providerDim = this.provider.dimensionId;
        this.provider.registerWorld(this);
        this.provider.dimensionId = providerDim;
        this.chunkProvider = this.createChunkProvider();
        this.calculateInitialSkylight();
        this.calculateInitialWeather();
    }

    public World(ISaveHandler p_i45369_1_, String p_i45369_2_, WorldSettings p_i45369_3_, WorldProvider p_i45369_4_, Profiler p_i45369_5_)
    {
        this.ambientTickCountdown = this.rand.nextInt(12000);
        this.spawnHostileMobs = true;
        this.spawnPeacefulMobs = true;
        this.collidingBoundingBoxes = new ArrayList();
        this.lightUpdateBlockList = new int[32768];
        this.saveHandler = p_i45369_1_;
        this.theProfiler = p_i45369_5_;
        this.mapStorage = getMapStorage(p_i45369_1_);
        this.worldInfo = p_i45369_1_.loadWorldInfo();

        if (p_i45369_4_ != null)
        {
            this.provider = p_i45369_4_;
        }
        else if (this.worldInfo != null && this.worldInfo.getVanillaDimension() != 0)
        {
            this.provider = WorldProvider.getProviderForDimension(this.worldInfo.getVanillaDimension());
        }
        else
        {
            this.provider = WorldProvider.getProviderForDimension(0);
        }

        if (this.worldInfo == null)
        {
            this.worldInfo = new WorldInfo(p_i45369_3_, p_i45369_2_);
        }
        else
        {
            this.worldInfo.setWorldName(p_i45369_2_);
        }

        this.provider.registerWorld(this);
        this.chunkProvider = this.createChunkProvider();

        if (this instanceof WorldServer)
        {
            this.perWorldStorage = new MapStorage(new WorldSpecificSaveHandler((WorldServer)this, p_i45369_1_));
        }
        else
        {
            this.perWorldStorage = new MapStorage((ISaveHandler)null);
        }

        if (!this.worldInfo.isInitialized())
        {
            try
            {
                this.initialize(p_i45369_3_);
            }
            catch (Throwable throwable1)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable1, "Exception initializing level");

                try
                {
                    this.addWorldInfoToCrashReport(crashreport);
                }
                catch (Throwable throwable)
                {
                    ;
                }

                throw new ReportedException(crashreport);
            }

            this.worldInfo.setServerInitialized(true);
        }

        VillageCollection villagecollection = (VillageCollection)this.perWorldStorage.loadData(VillageCollection.class, "villages");

        if (villagecollection == null)
        {
            this.villageCollectionObj = new VillageCollection(this);
            this.perWorldStorage.setData("villages", this.villageCollectionObj);
        }
        else
        {
            this.villageCollectionObj = villagecollection;
            this.villageCollectionObj.func_82566_a(this);
        }

        this.calculateInitialSkylight();
        this.calculateInitialWeather();
    }

    private static MapStorage s_mapStorage;
    private static ISaveHandler s_savehandler;
    //Provides a solution for different worlds getting different copies of the same data, potentially rewriting the data or causing race conditions/stale data
    //Buildcraft has suffered from the issue this fixes.  If you load the same data from two different worlds they can get two different copies of the same object, thus the last saved gets final say.
    private MapStorage getMapStorage(ISaveHandler savehandler)
    {
        if (s_savehandler != savehandler || s_mapStorage == null)
        {
            s_mapStorage = new MapStorage(savehandler);
            s_savehandler = savehandler;
        }
        return s_mapStorage;
    }

    /**
     * Creates the chunk provider for this world. Called in the constructor. Retrieves provider from worldProvider?
     */
    protected abstract IChunkProvider createChunkProvider();

    protected void initialize(WorldSettings p_72963_1_)
    {
        this.worldInfo.setServerInitialized(true);
    }

    /**
     * Sets a new spawn location by finding an uncovered block at a random (x,z) location in the chunk.
     */
    @SideOnly(Side.CLIENT)
    public void setSpawnLocation()
    {
        this.setSpawnLocation(8, 64, 8);
    }

    public Block getTopBlock(int p_147474_1_, int p_147474_2_)
    {
        int k;

        for (k = 63; !this.isAirBlock(p_147474_1_, k + 1, p_147474_2_); ++k)
        {
            ;
        }

        return this.getBlock(p_147474_1_, k, p_147474_2_);
    }

    public Block getBlock(int p_147439_1_, int p_147439_2_, int p_147439_3_)
    {
        if (p_147439_1_ >= -30000000 && p_147439_3_ >= -30000000 && p_147439_1_ < 30000000 && p_147439_3_ < 30000000 && p_147439_2_ >= 0 && p_147439_2_ < 256)
        {
            Chunk chunk = null;

            try
            {
                chunk = this.getChunkFromChunkCoords(p_147439_1_ >> 4, p_147439_3_ >> 4);
                return chunk.getBlock(p_147439_1_ & 15, p_147439_2_, p_147439_3_ & 15);
            }
            catch (Throwable throwable)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Exception getting block type in world");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Requested block coordinates");
                crashreportcategory.addCrashSection("Found chunk", Boolean.valueOf(chunk == null));
                crashreportcategory.addCrashSection("Location", CrashReportCategory.getLocationInfo(p_147439_1_, p_147439_2_, p_147439_3_));
                throw new ReportedException(crashreport);
            }
        }
        else
        {
            return Blocks.air;
        }
    }

    /**
     * Returns true if the block at the specified coordinates is empty
     */
    public boolean isAirBlock(int p_147437_1_, int p_147437_2_, int p_147437_3_)
    {
        Block block = this.getBlock(p_147437_1_, p_147437_2_, p_147437_3_);
        return block.isAir(this, p_147437_1_, p_147437_2_, p_147437_3_);
    }

    /**
     * Returns whether a block exists at world coordinates x, y, z
     */
    public boolean blockExists(int p_72899_1_, int p_72899_2_, int p_72899_3_)
    {
        return p_72899_2_ >= 0 && p_72899_2_ < 256 ? this.chunkExists(p_72899_1_ >> 4, p_72899_3_ >> 4) : false;
    }

    /**
     * Checks if any of the chunks within distance (argument 4) blocks of the given block exist
     */
    public boolean doChunksNearChunkExist(int p_72873_1_, int p_72873_2_, int p_72873_3_, int p_72873_4_)
    {
        return this.checkChunksExist(p_72873_1_ - p_72873_4_, p_72873_2_ - p_72873_4_, p_72873_3_ - p_72873_4_, p_72873_1_ + p_72873_4_, p_72873_2_ + p_72873_4_, p_72873_3_ + p_72873_4_);
    }

    /**
     * Checks between a min and max all the chunks inbetween actually exist. Args: minX, minY, minZ, maxX, maxY, maxZ
     */
    public boolean checkChunksExist(int p_72904_1_, int p_72904_2_, int p_72904_3_, int p_72904_4_, int p_72904_5_, int p_72904_6_)
    {
        if (p_72904_5_ >= 0 && p_72904_2_ < 256)
        {
            p_72904_1_ >>= 4;
            p_72904_3_ >>= 4;
            p_72904_4_ >>= 4;
            p_72904_6_ >>= 4;

            for (int k1 = p_72904_1_; k1 <= p_72904_4_; ++k1)
            {
                for (int l1 = p_72904_3_; l1 <= p_72904_6_; ++l1)
                {
                    if (!this.chunkExists(k1, l1))
                    {
                        return false;
                    }
                }
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * Returns whether a chunk exists at chunk coordinates x, y
     */
    protected boolean chunkExists(int p_72916_1_, int p_72916_2_)
    {
        return this.chunkProvider.chunkExists(p_72916_1_, p_72916_2_);
    }

    /**
     * Returns a chunk looked up by block coordinates. Args: x, z
     */
    public Chunk getChunkFromBlockCoords(int p_72938_1_, int p_72938_2_)
    {
        return this.getChunkFromChunkCoords(p_72938_1_ >> 4, p_72938_2_ >> 4);
    }

    /**
     * Returns back a chunk looked up by chunk coordinates Args: x, y
     */
    public Chunk getChunkFromChunkCoords(int p_72964_1_, int p_72964_2_)
    {
        return this.chunkProvider.provideChunk(p_72964_1_, p_72964_2_);
    }

    /**
     * Sets the block ID and metadata at a given location. Args: X, Y, Z, new block ID, new metadata, flags. Flag 1 will
     * cause a block update. Flag 2 will send the change to clients (you almost always want this). Flag 4 prevents the
     * block from being re-rendered, if this is a client world. Flags can be added together.
     */
    public boolean setBlock(int p_147465_1_, int p_147465_2_, int p_147465_3_, Block p_147465_4_, int p_147465_5_, int p_147465_6_)
    {
        if (p_147465_1_ >= -30000000 && p_147465_3_ >= -30000000 && p_147465_1_ < 30000000 && p_147465_3_ < 30000000)
        {
            if (p_147465_2_ < 0)
            {
                return false;
            }
            else if (p_147465_2_ >= 256)
            {
                return false;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_147465_1_ >> 4, p_147465_3_ >> 4);
                Block block1 = null;
                net.minecraftforge.common.util.BlockSnapshot blockSnapshot = null;

                if ((p_147465_6_ & 1) != 0)
                {
                    block1 = chunk.getBlock(p_147465_1_ & 15, p_147465_2_, p_147465_3_ & 15);
                }

                if (this.captureBlockSnapshots && !this.isRemote)
                {
                    blockSnapshot = net.minecraftforge.common.util.BlockSnapshot.getBlockSnapshot(this, p_147465_1_, p_147465_2_, p_147465_3_, p_147465_6_);
                    this.capturedBlockSnapshots.add(blockSnapshot);
                }

                boolean flag = chunk.func_150807_a(p_147465_1_ & 15, p_147465_2_, p_147465_3_ & 15, p_147465_4_, p_147465_5_);

                if (!flag && blockSnapshot != null)
                {
                    this.capturedBlockSnapshots.remove(blockSnapshot);
                    blockSnapshot = null;
                }

                this.theProfiler.startSection("checkLight");
                this.func_147451_t(p_147465_1_, p_147465_2_, p_147465_3_);
                this.theProfiler.endSection();

                if (flag && blockSnapshot == null) // Don't notify clients or update physics while capturing blockstates
                {
                    // Modularize client and physic updates
                    this.markAndNotifyBlock(p_147465_1_, p_147465_2_, p_147465_3_, chunk, block1, p_147465_4_, p_147465_6_);
                }

                return flag;
            }
        }
        else
        {
            return false;
        }
    }

    // Split off from original setBlock(int p_147465_1_, int p_147465_2_, int p_147465_3_, Block p_147465_4_, int p_147465_5_, int p_147465_6_) method in order to directly send client and physic updates
    public void markAndNotifyBlock(int x, int y, int z, Chunk chunk, Block oldBlock, Block newBlock, int flag)
    {
        if ((flag & 2) != 0 && (chunk == null || chunk.func_150802_k()))
        {
            this.markBlockForUpdate(x, y, z);
        }

        if (!this.isRemote && (flag & 1) != 0)
        {
            this.notifyBlockChange(x, y, z, oldBlock);

            if (newBlock.hasComparatorInputOverride())
            {
                this.func_147453_f(x, y, z, newBlock);
            }
        }
    }

    /**
     * Returns the block metadata at coords x,y,z
     */
    public int getBlockMetadata(int p_72805_1_, int p_72805_2_, int p_72805_3_)
    {
        if (p_72805_1_ >= -30000000 && p_72805_3_ >= -30000000 && p_72805_1_ < 30000000 && p_72805_3_ < 30000000)
        {
            if (p_72805_2_ < 0)
            {
                return 0;
            }
            else if (p_72805_2_ >= 256)
            {
                return 0;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_72805_1_ >> 4, p_72805_3_ >> 4);
                p_72805_1_ &= 15;
                p_72805_3_ &= 15;
                return chunk.getBlockMetadata(p_72805_1_, p_72805_2_, p_72805_3_);
            }
        }
        else
        {
            return 0;
        }
    }

    /**
     * Sets the blocks metadata and if set will then notify blocks that this block changed, depending on the flag. Args:
     * x, y, z, metadata, flag. See setBlock for flag description
     */
    public boolean setBlockMetadataWithNotify(int p_72921_1_, int p_72921_2_, int p_72921_3_, int p_72921_4_, int p_72921_5_)
    {
        if (p_72921_1_ >= -30000000 && p_72921_3_ >= -30000000 && p_72921_1_ < 30000000 && p_72921_3_ < 30000000)
        {
            if (p_72921_2_ < 0)
            {
                return false;
            }
            else if (p_72921_2_ >= 256)
            {
                return false;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_72921_1_ >> 4, p_72921_3_ >> 4);
                int j1 = p_72921_1_ & 15;
                int k1 = p_72921_3_ & 15;
                boolean flag = chunk.setBlockMetadata(j1, p_72921_2_, k1, p_72921_4_);

                if (flag)
                {
                    Block block = chunk.getBlock(j1, p_72921_2_, k1);

                    if ((p_72921_5_ & 2) != 0 && (!this.isRemote || (p_72921_5_ & 4) == 0) && chunk.func_150802_k())
                    {
                        this.markBlockForUpdate(p_72921_1_, p_72921_2_, p_72921_3_);
                    }

                    if (!this.isRemote && (p_72921_5_ & 1) != 0)
                    {
                        this.notifyBlockChange(p_72921_1_, p_72921_2_, p_72921_3_, block);

                        if (block.hasComparatorInputOverride())
                        {
                            this.func_147453_f(p_72921_1_, p_72921_2_, p_72921_3_, block);
                        }
                    }
                }

                return flag;
            }
        }
        else
        {
            return false;
        }
    }

    /**
     * Sets a block to 0 and notifies relevant systems with the block change  Args: x, y, z
     */
    public boolean setBlockToAir(int p_147468_1_, int p_147468_2_, int p_147468_3_)
    {
        return this.setBlock(p_147468_1_, p_147468_2_, p_147468_3_, Blocks.air, 0, 3);
    }

    public boolean func_147480_a(int p_147480_1_, int p_147480_2_, int p_147480_3_, boolean p_147480_4_)
    {
        Block block = this.getBlock(p_147480_1_, p_147480_2_, p_147480_3_);

        if (block.getMaterial() == Material.air)
        {
            return false;
        }
        else
        {
            int l = this.getBlockMetadata(p_147480_1_, p_147480_2_, p_147480_3_);
            this.playAuxSFX(2001, p_147480_1_, p_147480_2_, p_147480_3_, Block.getIdFromBlock(block) + (l << 12));

            if (p_147480_4_)
            {
                block.dropBlockAsItem(this, p_147480_1_, p_147480_2_, p_147480_3_, l, 0);
            }

            return this.setBlock(p_147480_1_, p_147480_2_, p_147480_3_, Blocks.air, 0, 3);
        }
    }

    /**
     * Sets a block by a coordinate
     */
    public boolean setBlock(int p_147449_1_, int p_147449_2_, int p_147449_3_, Block p_147449_4_)
    {
        return this.setBlock(p_147449_1_, p_147449_2_, p_147449_3_, p_147449_4_, 0, 3);
    }

    public void markBlockForUpdate(int p_147471_1_, int p_147471_2_, int p_147471_3_)
    {
        for (int l = 0; l < this.worldAccesses.size(); ++l)
        {
            ((IWorldAccess)this.worldAccesses.get(l)).markBlockForUpdate(p_147471_1_, p_147471_2_, p_147471_3_);
        }
    }

    /**
     * The block type change and need to notify other systems  Args: x, y, z, blockID
     */
    public void notifyBlockChange(int p_147444_1_, int p_147444_2_, int p_147444_3_, Block p_147444_4_)
    {
        this.notifyBlocksOfNeighborChange(p_147444_1_, p_147444_2_, p_147444_3_, p_147444_4_);
    }

    /**
     * marks a vertical line of blocks as dirty
     */
    public void markBlocksDirtyVertical(int p_72975_1_, int p_72975_2_, int p_72975_3_, int p_72975_4_)
    {
        int i1;

        if (p_72975_3_ > p_72975_4_)
        {
            i1 = p_72975_4_;
            p_72975_4_ = p_72975_3_;
            p_72975_3_ = i1;
        }

        if (!this.provider.hasNoSky)
        {
            for (i1 = p_72975_3_; i1 <= p_72975_4_; ++i1)
            {
                this.updateLightByType(EnumSkyBlock.Sky, p_72975_1_, i1, p_72975_2_);
            }
        }

        this.markBlockRangeForRenderUpdate(p_72975_1_, p_72975_3_, p_72975_2_, p_72975_1_, p_72975_4_, p_72975_2_);
    }

    public void markBlockRangeForRenderUpdate(int p_147458_1_, int p_147458_2_, int p_147458_3_, int p_147458_4_, int p_147458_5_, int p_147458_6_)
    {
        for (int k1 = 0; k1 < this.worldAccesses.size(); ++k1)
        {
            ((IWorldAccess)this.worldAccesses.get(k1)).markBlockRangeForRenderUpdate(p_147458_1_, p_147458_2_, p_147458_3_, p_147458_4_, p_147458_5_, p_147458_6_);
        }
    }

    public void notifyBlocksOfNeighborChange(int p_147459_1_, int p_147459_2_, int p_147459_3_, Block p_147459_4_)
    {
        this.notifyBlockOfNeighborChange(p_147459_1_ - 1, p_147459_2_, p_147459_3_, p_147459_4_);
        this.notifyBlockOfNeighborChange(p_147459_1_ + 1, p_147459_2_, p_147459_3_, p_147459_4_);
        this.notifyBlockOfNeighborChange(p_147459_1_, p_147459_2_ - 1, p_147459_3_, p_147459_4_);
        this.notifyBlockOfNeighborChange(p_147459_1_, p_147459_2_ + 1, p_147459_3_, p_147459_4_);
        this.notifyBlockOfNeighborChange(p_147459_1_, p_147459_2_, p_147459_3_ - 1, p_147459_4_);
        this.notifyBlockOfNeighborChange(p_147459_1_, p_147459_2_, p_147459_3_ + 1, p_147459_4_);
    }

    public void notifyBlocksOfNeighborChange(int p_147441_1_, int p_147441_2_, int p_147441_3_, Block p_147441_4_, int p_147441_5_)
    {
        if (p_147441_5_ != 4)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_ - 1, p_147441_2_, p_147441_3_, p_147441_4_);
        }

        if (p_147441_5_ != 5)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_ + 1, p_147441_2_, p_147441_3_, p_147441_4_);
        }

        if (p_147441_5_ != 0)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_, p_147441_2_ - 1, p_147441_3_, p_147441_4_);
        }

        if (p_147441_5_ != 1)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_, p_147441_2_ + 1, p_147441_3_, p_147441_4_);
        }

        if (p_147441_5_ != 2)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_, p_147441_2_, p_147441_3_ - 1, p_147441_4_);
        }

        if (p_147441_5_ != 3)
        {
            this.notifyBlockOfNeighborChange(p_147441_1_, p_147441_2_, p_147441_3_ + 1, p_147441_4_);
        }
    }

    /**
     * Notifies a block that one of its neighbor change to the specified type Args: x, y, z, block
     */
    public void notifyBlockOfNeighborChange(int p_147460_1_, int p_147460_2_, int p_147460_3_, final Block p_147460_4_)
    {
        if (!this.isRemote)
        {
            Block block = this.getBlock(p_147460_1_, p_147460_2_, p_147460_3_);

            try
            {
                block.onNeighborBlockChange(this, p_147460_1_, p_147460_2_, p_147460_3_, p_147460_4_);
            }
            catch (Throwable throwable1)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable1, "Exception while updating neighbours");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Block being updated");
                int l;

                try
                {
                    l = this.getBlockMetadata(p_147460_1_, p_147460_2_, p_147460_3_);
                }
                catch (Throwable throwable)
                {
                    l = -1;
                }

                crashreportcategory.addCrashSectionCallable("Source block type", new Callable()
                {
                    private static final String __OBFID = "CL_00000142";
                    public String call()
                    {
                        try
                        {
                            return String.format("ID #%d (%s // %s)", new Object[] {Integer.valueOf(Block.getIdFromBlock(p_147460_4_)), p_147460_4_.getUnlocalizedName(), p_147460_4_.getClass().getCanonicalName()});
                        }
                        catch (Throwable throwable2)
                        {
                            return "ID #" + Block.getIdFromBlock(p_147460_4_);
                        }
                    }
                });
                CrashReportCategory.func_147153_a(crashreportcategory, p_147460_1_, p_147460_2_, p_147460_3_, block, l);
                throw new ReportedException(crashreport);
            }
        }
    }

    /**
     * Returns true if the given block will receive a scheduled tick in this tick. Args: X, Y, Z, Block
     */
    public boolean isBlockTickScheduledThisTick(int p_147477_1_, int p_147477_2_, int p_147477_3_, Block p_147477_4_)
    {
        return false;
    }

    /**
     * Checks if the specified block is able to see the sky
     */
    public boolean canBlockSeeTheSky(int p_72937_1_, int p_72937_2_, int p_72937_3_)
    {
        return this.getChunkFromChunkCoords(p_72937_1_ >> 4, p_72937_3_ >> 4).canBlockSeeTheSky(p_72937_1_ & 15, p_72937_2_, p_72937_3_ & 15);
    }

    /**
     * Does the same as getBlockLightValue_do but without checking if its not a normal block
     */
    public int getFullBlockLightValue(int p_72883_1_, int p_72883_2_, int p_72883_3_)
    {
        if (p_72883_2_ < 0)
        {
            return 0;
        }
        else
        {
            if (p_72883_2_ >= 256)
            {
                p_72883_2_ = 255;
            }

            return this.getChunkFromChunkCoords(p_72883_1_ >> 4, p_72883_3_ >> 4).getBlockLightValue(p_72883_1_ & 15, p_72883_2_, p_72883_3_ & 15, 0);
        }
    }

    /**
     * Gets the light value of a block location
     */
    public int getBlockLightValue(int p_72957_1_, int p_72957_2_, int p_72957_3_)
    {
        return this.getBlockLightValue_do(p_72957_1_, p_72957_2_, p_72957_3_, true);
    }

    /**
     * Gets the light value of a block location. This is the actual function that gets the value and has a bool flag
     * that indicates if its a half step block to get the maximum light value of a direct neighboring block (left,
     * right, forward, back, and up)
     */
    public int getBlockLightValue_do(int p_72849_1_, int p_72849_2_, int p_72849_3_, boolean p_72849_4_)
    {
        if (p_72849_1_ >= -30000000 && p_72849_3_ >= -30000000 && p_72849_1_ < 30000000 && p_72849_3_ < 30000000)
        {
            if (p_72849_4_ && this.getBlock(p_72849_1_, p_72849_2_, p_72849_3_).getUseNeighborBrightness())
            {
                int l1 = this.getBlockLightValue_do(p_72849_1_, p_72849_2_ + 1, p_72849_3_, false);
                int l = this.getBlockLightValue_do(p_72849_1_ + 1, p_72849_2_, p_72849_3_, false);
                int i1 = this.getBlockLightValue_do(p_72849_1_ - 1, p_72849_2_, p_72849_3_, false);
                int j1 = this.getBlockLightValue_do(p_72849_1_, p_72849_2_, p_72849_3_ + 1, false);
                int k1 = this.getBlockLightValue_do(p_72849_1_, p_72849_2_, p_72849_3_ - 1, false);

                if (l > l1)
                {
                    l1 = l;
                }

                if (i1 > l1)
                {
                    l1 = i1;
                }

                if (j1 > l1)
                {
                    l1 = j1;
                }

                if (k1 > l1)
                {
                    l1 = k1;
                }

                return l1;
            }
            else if (p_72849_2_ < 0)
            {
                return 0;
            }
            else
            {
                if (p_72849_2_ >= 256)
                {
                    p_72849_2_ = 255;
                }

                Chunk chunk = this.getChunkFromChunkCoords(p_72849_1_ >> 4, p_72849_3_ >> 4);
                p_72849_1_ &= 15;
                p_72849_3_ &= 15;
                return chunk.getBlockLightValue(p_72849_1_, p_72849_2_, p_72849_3_, this.skylightSubtracted);
            }
        }
        else
        {
            return 15;
        }
    }

    /**
     * Returns the y coordinate with a block in it at this x, z coordinate
     */
    public int getHeightValue(int p_72976_1_, int p_72976_2_)
    {
        if (p_72976_1_ >= -30000000 && p_72976_2_ >= -30000000 && p_72976_1_ < 30000000 && p_72976_2_ < 30000000)
        {
            if (!this.chunkExists(p_72976_1_ >> 4, p_72976_2_ >> 4))
            {
                return 0;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_72976_1_ >> 4, p_72976_2_ >> 4);
                return chunk.getHeightValue(p_72976_1_ & 15, p_72976_2_ & 15);
            }
        }
        else
        {
            return 64;
        }
    }

    /**
     * Gets the heightMapMinimum field of the given chunk, or 0 if the chunk is not loaded. Coords are in blocks. Args:
     * X, Z
     */
    public int getChunkHeightMapMinimum(int p_82734_1_, int p_82734_2_)
    {
        if (p_82734_1_ >= -30000000 && p_82734_2_ >= -30000000 && p_82734_1_ < 30000000 && p_82734_2_ < 30000000)
        {
            if (!this.chunkExists(p_82734_1_ >> 4, p_82734_2_ >> 4))
            {
                return 0;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_82734_1_ >> 4, p_82734_2_ >> 4);
                return chunk.heightMapMinimum;
            }
        }
        else
        {
            return 64;
        }
    }

    /**
     * Brightness for SkyBlock.Sky is clear white and (through color computing it is assumed) DEPENDENT ON DAYTIME.
     * Brightness for SkyBlock.Block is yellowish and independent.
     */
    @SideOnly(Side.CLIENT)
    public int getSkyBlockTypeBrightness(EnumSkyBlock p_72925_1_, int p_72925_2_, int p_72925_3_, int p_72925_4_)
    {
        if (this.provider.hasNoSky && p_72925_1_ == EnumSkyBlock.Sky)
        {
            return 0;
        }
        else
        {
            if (p_72925_3_ < 0)
            {
                p_72925_3_ = 0;
            }

            if (p_72925_3_ >= 256)
            {
                return p_72925_1_.defaultLightValue;
            }
            else if (p_72925_2_ >= -30000000 && p_72925_4_ >= -30000000 && p_72925_2_ < 30000000 && p_72925_4_ < 30000000)
            {
                int l = p_72925_2_ >> 4;
                int i1 = p_72925_4_ >> 4;

                if (!this.chunkExists(l, i1))
                {
                    return p_72925_1_.defaultLightValue;
                }
                else if (this.getBlock(p_72925_2_, p_72925_3_, p_72925_4_).getUseNeighborBrightness())
                {
                    int j2 = this.getSavedLightValue(p_72925_1_, p_72925_2_, p_72925_3_ + 1, p_72925_4_);
                    int j1 = this.getSavedLightValue(p_72925_1_, p_72925_2_ + 1, p_72925_3_, p_72925_4_);
                    int k1 = this.getSavedLightValue(p_72925_1_, p_72925_2_ - 1, p_72925_3_, p_72925_4_);
                    int l1 = this.getSavedLightValue(p_72925_1_, p_72925_2_, p_72925_3_, p_72925_4_ + 1);
                    int i2 = this.getSavedLightValue(p_72925_1_, p_72925_2_, p_72925_3_, p_72925_4_ - 1);

                    if (j1 > j2)
                    {
                        j2 = j1;
                    }

                    if (k1 > j2)
                    {
                        j2 = k1;
                    }

                    if (l1 > j2)
                    {
                        j2 = l1;
                    }

                    if (i2 > j2)
                    {
                        j2 = i2;
                    }

                    return j2;
                }
                else
                {
                    Chunk chunk = this.getChunkFromChunkCoords(l, i1);
                    return chunk.getSavedLightValue(p_72925_1_, p_72925_2_ & 15, p_72925_3_, p_72925_4_ & 15);
                }
            }
            else
            {
                return p_72925_1_.defaultLightValue;
            }
        }
    }

    /**
     * Returns saved light value without taking into account the time of day.  Either looks in the sky light map or
     * block light map based on the enumSkyBlock arg.
     */
    public int getSavedLightValue(EnumSkyBlock p_72972_1_, int p_72972_2_, int p_72972_3_, int p_72972_4_)
    {
        if (p_72972_3_ < 0)
        {
            p_72972_3_ = 0;
        }

        if (p_72972_3_ >= 256)
        {
            p_72972_3_ = 255;
        }

        if (p_72972_2_ >= -30000000 && p_72972_4_ >= -30000000 && p_72972_2_ < 30000000 && p_72972_4_ < 30000000)
        {
            int l = p_72972_2_ >> 4;
            int i1 = p_72972_4_ >> 4;

            if (!this.chunkExists(l, i1))
            {
                return p_72972_1_.defaultLightValue;
            }
            else
            {
                Chunk chunk = this.getChunkFromChunkCoords(l, i1);
                return chunk.getSavedLightValue(p_72972_1_, p_72972_2_ & 15, p_72972_3_, p_72972_4_ & 15);
            }
        }
        else
        {
            return p_72972_1_.defaultLightValue;
        }
    }

    /**
     * Sets the light value either into the sky map or block map depending on if enumSkyBlock is set to sky or block.
     * Args: enumSkyBlock, x, y, z, lightValue
     */
    public void setLightValue(EnumSkyBlock p_72915_1_, int p_72915_2_, int p_72915_3_, int p_72915_4_, int p_72915_5_)
    {
        if (p_72915_2_ >= -30000000 && p_72915_4_ >= -30000000 && p_72915_2_ < 30000000 && p_72915_4_ < 30000000)
        {
            if (p_72915_3_ >= 0)
            {
                if (p_72915_3_ < 256)
                {
                    if (this.chunkExists(p_72915_2_ >> 4, p_72915_4_ >> 4))
                    {
                        Chunk chunk = this.getChunkFromChunkCoords(p_72915_2_ >> 4, p_72915_4_ >> 4);
                        chunk.setLightValue(p_72915_1_, p_72915_2_ & 15, p_72915_3_, p_72915_4_ & 15, p_72915_5_);

                        for (int i1 = 0; i1 < this.worldAccesses.size(); ++i1)
                        {
                            ((IWorldAccess)this.worldAccesses.get(i1)).markBlockForRenderUpdate(p_72915_2_, p_72915_3_, p_72915_4_);
                        }
                    }
                }
            }
        }
    }

    public void func_147479_m(int p_147479_1_, int p_147479_2_, int p_147479_3_)
    {
        for (int l = 0; l < this.worldAccesses.size(); ++l)
        {
            ((IWorldAccess)this.worldAccesses.get(l)).markBlockForRenderUpdate(p_147479_1_, p_147479_2_, p_147479_3_);
        }
    }

    /**
     * Any Light rendered on a 1.8 Block goes through here
     */
    @SideOnly(Side.CLIENT)
    public int getLightBrightnessForSkyBlocks(int p_72802_1_, int p_72802_2_, int p_72802_3_, int p_72802_4_)
    {
        int i1 = this.getSkyBlockTypeBrightness(EnumSkyBlock.Sky, p_72802_1_, p_72802_2_, p_72802_3_);
        int j1 = this.getSkyBlockTypeBrightness(EnumSkyBlock.Block, p_72802_1_, p_72802_2_, p_72802_3_);

        if (j1 < p_72802_4_)
        {
            j1 = p_72802_4_;
        }

        return i1 << 20 | j1 << 4;
    }

    /**
     * Returns how bright the block is shown as which is the block's light value looked up in a lookup table (light
     * values aren't linear for brightness). Args: x, y, z
     */
    public float getLightBrightness(int p_72801_1_, int p_72801_2_, int p_72801_3_)
    {
        return this.provider.lightBrightnessTable[this.getBlockLightValue(p_72801_1_, p_72801_2_, p_72801_3_)];
    }

    /**
     * Checks whether its daytime by seeing if the light subtracted from the skylight is less than 4
     */
    public boolean isDaytime()
    {
        return provider.isDaytime();
    }

    /**
     * Performs a raycast against all blocks in the world except liquids.
     */
    public MovingObjectPosition rayTraceBlocks(Vec3 p_72933_1_, Vec3 p_72933_2_)
    {
        return this.func_147447_a(p_72933_1_, p_72933_2_, false, false, false);
    }

    /**
     * Performs a raycast against all blocks in the world, and optionally liquids.
     */
    public MovingObjectPosition rayTraceBlocks(Vec3 p_72901_1_, Vec3 p_72901_2_, boolean p_72901_3_)
    {
        return this.func_147447_a(p_72901_1_, p_72901_2_, p_72901_3_, false, false);
    }

    public MovingObjectPosition func_147447_a(Vec3 p_147447_1_, Vec3 p_147447_2_, boolean p_147447_3_, boolean p_147447_4_, boolean p_147447_5_)
    {
        if (!Double.isNaN(p_147447_1_.xCoord) && !Double.isNaN(p_147447_1_.yCoord) && !Double.isNaN(p_147447_1_.zCoord))
        {
            if (!Double.isNaN(p_147447_2_.xCoord) && !Double.isNaN(p_147447_2_.yCoord) && !Double.isNaN(p_147447_2_.zCoord))
            {
                int i = MathHelper.floor_double(p_147447_2_.xCoord);
                int j = MathHelper.floor_double(p_147447_2_.yCoord);
                int k = MathHelper.floor_double(p_147447_2_.zCoord);
                int l = MathHelper.floor_double(p_147447_1_.xCoord);
                int i1 = MathHelper.floor_double(p_147447_1_.yCoord);
                int j1 = MathHelper.floor_double(p_147447_1_.zCoord);
                Block block = this.getBlock(l, i1, j1);
                int k1 = this.getBlockMetadata(l, i1, j1);

                if ((!p_147447_4_ || block.getCollisionBoundingBoxFromPool(this, l, i1, j1) != null) && block.canCollideCheck(k1, p_147447_3_))
                {
                    MovingObjectPosition movingobjectposition = block.collisionRayTrace(this, l, i1, j1, p_147447_1_, p_147447_2_);

                    if (movingobjectposition != null)
                    {
                        return movingobjectposition;
                    }
                }

                MovingObjectPosition movingobjectposition2 = null;
                k1 = 200;

                while (k1-- >= 0)
                {
                    if (Double.isNaN(p_147447_1_.xCoord) || Double.isNaN(p_147447_1_.yCoord) || Double.isNaN(p_147447_1_.zCoord))
                    {
                        return null;
                    }

                    if (l == i && i1 == j && j1 == k)
                    {
                        return p_147447_5_ ? movingobjectposition2 : null;
                    }

                    boolean flag6 = true;
                    boolean flag3 = true;
                    boolean flag4 = true;
                    double d0 = 999.0D;
                    double d1 = 999.0D;
                    double d2 = 999.0D;

                    if (i > l)
                    {
                        d0 = (double)l + 1.0D;
                    }
                    else if (i < l)
                    {
                        d0 = (double)l + 0.0D;
                    }
                    else
                    {
                        flag6 = false;
                    }

                    if (j > i1)
                    {
                        d1 = (double)i1 + 1.0D;
                    }
                    else if (j < i1)
                    {
                        d1 = (double)i1 + 0.0D;
                    }
                    else
                    {
                        flag3 = false;
                    }

                    if (k > j1)
                    {
                        d2 = (double)j1 + 1.0D;
                    }
                    else if (k < j1)
                    {
                        d2 = (double)j1 + 0.0D;
                    }
                    else
                    {
                        flag4 = false;
                    }

                    double d3 = 999.0D;
                    double d4 = 999.0D;
                    double d5 = 999.0D;
                    double d6 = p_147447_2_.xCoord - p_147447_1_.xCoord;
                    double d7 = p_147447_2_.yCoord - p_147447_1_.yCoord;
                    double d8 = p_147447_2_.zCoord - p_147447_1_.zCoord;

                    if (flag6)
                    {
                        d3 = (d0 - p_147447_1_.xCoord) / d6;
                    }

                    if (flag3)
                    {
                        d4 = (d1 - p_147447_1_.yCoord) / d7;
                    }

                    if (flag4)
                    {
                        d5 = (d2 - p_147447_1_.zCoord) / d8;
                    }

                    boolean flag5 = false;
                    byte b0;

                    if (d3 < d4 && d3 < d5)
                    {
                        if (i > l)
                        {
                            b0 = 4;
                        }
                        else
                        {
                            b0 = 5;
                        }

                        p_147447_1_.xCoord = d0;
                        p_147447_1_.yCoord += d7 * d3;
                        p_147447_1_.zCoord += d8 * d3;
                    }
                    else if (d4 < d5)
                    {
                        if (j > i1)
                        {
                            b0 = 0;
                        }
                        else
                        {
                            b0 = 1;
                        }

                        p_147447_1_.xCoord += d6 * d4;
                        p_147447_1_.yCoord = d1;
                        p_147447_1_.zCoord += d8 * d4;
                    }
                    else
                    {
                        if (k > j1)
                        {
                            b0 = 2;
                        }
                        else
                        {
                            b0 = 3;
                        }

                        p_147447_1_.xCoord += d6 * d5;
                        p_147447_1_.yCoord += d7 * d5;
                        p_147447_1_.zCoord = d2;
                    }

                    Vec3 vec32 = Vec3.createVectorHelper(p_147447_1_.xCoord, p_147447_1_.yCoord, p_147447_1_.zCoord);
                    l = (int)(vec32.xCoord = (double)MathHelper.floor_double(p_147447_1_.xCoord));

                    if (b0 == 5)
                    {
                        --l;
                        ++vec32.xCoord;
                    }

                    i1 = (int)(vec32.yCoord = (double)MathHelper.floor_double(p_147447_1_.yCoord));

                    if (b0 == 1)
                    {
                        --i1;
                        ++vec32.yCoord;
                    }

                    j1 = (int)(vec32.zCoord = (double)MathHelper.floor_double(p_147447_1_.zCoord));

                    if (b0 == 3)
                    {
                        --j1;
                        ++vec32.zCoord;
                    }

                    Block block1 = this.getBlock(l, i1, j1);
                    int l1 = this.getBlockMetadata(l, i1, j1);

                    if (!p_147447_4_ || block1.getCollisionBoundingBoxFromPool(this, l, i1, j1) != null)
                    {
                        if (block1.canCollideCheck(l1, p_147447_3_))
                        {
                            MovingObjectPosition movingobjectposition1 = block1.collisionRayTrace(this, l, i1, j1, p_147447_1_, p_147447_2_);

                            if (movingobjectposition1 != null)
                            {
                                return movingobjectposition1;
                            }
                        }
                        else
                        {
                            movingobjectposition2 = new MovingObjectPosition(l, i1, j1, b0, p_147447_1_, false);
                        }
                    }
                }

                return p_147447_5_ ? movingobjectposition2 : null;
            }
            else
            {
                return null;
            }
        }
        else
        {
            return null;
        }
    }

    /**
     * Plays a sound at the entity's position. Args: entity, sound, volume (relative to 1.0), and frequency (or pitch,
     * also relative to 1.0).
     */
    public void playSoundAtEntity(Entity p_72956_1_, String p_72956_2_, float p_72956_3_, float p_72956_4_)
    {
        PlaySoundAtEntityEvent event = new PlaySoundAtEntityEvent(p_72956_1_, p_72956_2_, p_72956_3_, p_72956_4_);
        if (MinecraftForge.EVENT_BUS.post(event))
        {
            return;
        }
        p_72956_2_ = event.name;
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).playSound(p_72956_2_, p_72956_1_.posX, p_72956_1_.posY - (double)p_72956_1_.yOffset, p_72956_1_.posZ, p_72956_3_, p_72956_4_);
        }
    }

    /**
     * Plays sound to all near players except the player reference given
     */
    public void playSoundToNearExcept(EntityPlayer p_85173_1_, String p_85173_2_, float p_85173_3_, float p_85173_4_)
    {
        PlaySoundAtEntityEvent event = new PlaySoundAtEntityEvent(p_85173_1_, p_85173_2_, p_85173_3_, p_85173_4_);
        if (MinecraftForge.EVENT_BUS.post(event))
        {
            return;
        }
        p_85173_2_ = event.name;
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).playSoundToNearExcept(p_85173_1_, p_85173_2_, p_85173_1_.posX, p_85173_1_.posY - (double)p_85173_1_.yOffset, p_85173_1_.posZ, p_85173_3_, p_85173_4_);
        }
    }

    /**
     * Play a sound effect. Many many parameters for this function. Not sure what they do, but a classic call is :
     * (double)i + 0.5D, (double)j + 0.5D, (double)k + 0.5D, 'random.door_open', 1.0F, world.rand.nextFloat() * 0.1F +
     * 0.9F with i,j,k position of the block.
     */
    public void playSoundEffect(double p_72908_1_, double p_72908_3_, double p_72908_5_, String p_72908_7_, float p_72908_8_, float p_72908_9_)
    {
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).playSound(p_72908_7_, p_72908_1_, p_72908_3_, p_72908_5_, p_72908_8_, p_72908_9_);
        }
    }

    /**
     * par8 is loudness, all pars passed to minecraftInstance.sndManager.playSound
     */
    public void playSound(double p_72980_1_, double p_72980_3_, double p_72980_5_, String p_72980_7_, float p_72980_8_, float p_72980_9_, boolean p_72980_10_) {}

    /**
     * Plays a record at the specified coordinates of the specified name. Args: recordName, x, y, z
     */
    public void playRecord(String p_72934_1_, int p_72934_2_, int p_72934_3_, int p_72934_4_)
    {
        for (int l = 0; l < this.worldAccesses.size(); ++l)
        {
            ((IWorldAccess)this.worldAccesses.get(l)).playRecord(p_72934_1_, p_72934_2_, p_72934_3_, p_72934_4_);
        }
    }

    /**
     * Spawns a particle.  Args particleName, x, y, z, velX, velY, velZ
     */
    public void spawnParticle(String p_72869_1_, double p_72869_2_, double p_72869_4_, double p_72869_6_, double p_72869_8_, double p_72869_10_, double p_72869_12_)
    {
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).spawnParticle(p_72869_1_, p_72869_2_, p_72869_4_, p_72869_6_, p_72869_8_, p_72869_10_, p_72869_12_);
        }
    }

    /**
     * adds a lightning bolt to the list of lightning bolts in this world.
     */
    public boolean addWeatherEffect(Entity p_72942_1_)
    {
        this.weatherEffects.add(p_72942_1_);
        return true;
    }

    /**
     * Called to place all entities as part of a world
     */
    public boolean spawnEntityInWorld(Entity p_72838_1_)
    {
        // do not drop any items while restoring blocksnapshots. Prevents dupes
        if (!this.isRemote && (p_72838_1_ == null || (p_72838_1_ instanceof net.minecraft.entity.item.EntityItem && this.restoringBlockSnapshots))) return false;

        int i = MathHelper.floor_double(p_72838_1_.posX / 16.0D);
        int j = MathHelper.floor_double(p_72838_1_.posZ / 16.0D);
        boolean flag = p_72838_1_.forceSpawn;

        if (p_72838_1_ instanceof EntityPlayer)
        {
            flag = true;
        }

        if (!flag && !this.chunkExists(i, j))
        {
            return false;
        }
        else
        {
            if (p_72838_1_ instanceof EntityPlayer)
            {
                EntityPlayer entityplayer = (EntityPlayer)p_72838_1_;
                this.playerEntities.add(entityplayer);
                this.updateAllPlayersSleepingFlag();
            }
            if (MinecraftForge.EVENT_BUS.post(new EntityJoinWorldEvent(p_72838_1_, this)) && !flag) return false;

            this.getChunkFromChunkCoords(i, j).addEntity(p_72838_1_);
            this.loadedEntityList.add(p_72838_1_);
            this.onEntityAdded(p_72838_1_);
            return true;
        }
    }

    public void onEntityAdded(Entity p_72923_1_)
    {
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).onEntityCreate(p_72923_1_);
        }
    }

    public void onEntityRemoved(Entity p_72847_1_)
    {
        for (int i = 0; i < this.worldAccesses.size(); ++i)
        {
            ((IWorldAccess)this.worldAccesses.get(i)).onEntityDestroy(p_72847_1_);
        }
    }

    /**
     * Schedule the entity for removal during the next tick. Marks the entity dead in anticipation.
     */
    public void removeEntity(Entity p_72900_1_)
    {
        if (p_72900_1_.riddenByEntity != null)
        {
            p_72900_1_.riddenByEntity.mountEntity((Entity)null);
        }

        if (p_72900_1_.ridingEntity != null)
        {
            p_72900_1_.mountEntity((Entity)null);
        }

        p_72900_1_.setDead();

        if (p_72900_1_ instanceof EntityPlayer)
        {
            this.playerEntities.remove(p_72900_1_);
            this.updateAllPlayersSleepingFlag();
            this.onEntityRemoved(p_72900_1_);
        }
    }

    /**
     * Do NOT use this method to remove normal entities- use normal removeEntity
     */
    public void removePlayerEntityDangerously(Entity p_72973_1_)
    {
        p_72973_1_.setDead();

        if (p_72973_1_ instanceof EntityPlayer)
        {
            this.playerEntities.remove(p_72973_1_);
            this.updateAllPlayersSleepingFlag();
        }

        int i = p_72973_1_.chunkCoordX;
        int j = p_72973_1_.chunkCoordZ;

        if (p_72973_1_.addedToChunk && this.chunkExists(i, j))
        {
            this.getChunkFromChunkCoords(i, j).removeEntity(p_72973_1_);
        }

        this.loadedEntityList.remove(p_72973_1_);
        this.onEntityRemoved(p_72973_1_);
    }

    /**
     * Adds a IWorldAccess to the list of worldAccesses
     */
    public void addWorldAccess(IWorldAccess p_72954_1_)
    {
        this.worldAccesses.add(p_72954_1_);
    }

    /**
     * Returns a list of bounding boxes that collide with aabb excluding the passed in entity's collision. Args: entity,
     * aabb
     */
    public List getCollidingBoundingBoxes(Entity p_72945_1_, AxisAlignedBB p_72945_2_)
    {
        this.collidingBoundingBoxes.clear();
        int i = MathHelper.floor_double(p_72945_2_.minX);
        int j = MathHelper.floor_double(p_72945_2_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72945_2_.minY);
        int l = MathHelper.floor_double(p_72945_2_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72945_2_.minZ);
        int j1 = MathHelper.floor_double(p_72945_2_.maxZ + 1.0D);

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = i1; l1 < j1; ++l1)
            {
                if (this.blockExists(k1, 64, l1))
                {
                    for (int i2 = k - 1; i2 < l; ++i2)
                    {
                        Block block;

                        if (k1 >= -30000000 && k1 < 30000000 && l1 >= -30000000 && l1 < 30000000)
                        {
                            block = this.getBlock(k1, i2, l1);
                        }
                        else
                        {
                            block = Blocks.stone;
                        }

                        block.addCollisionBoxesToList(this, k1, i2, l1, p_72945_2_, this.collidingBoundingBoxes, p_72945_1_);
                    }
                }
            }
        }

        double d0 = 0.25D;
        List list = this.getEntitiesWithinAABBExcludingEntity(p_72945_1_, p_72945_2_.expand(d0, d0, d0));

        for (int j2 = 0; j2 < list.size(); ++j2)
        {
            AxisAlignedBB axisalignedbb1 = ((Entity)list.get(j2)).getBoundingBox();

            if (axisalignedbb1 != null && axisalignedbb1.intersectsWith(p_72945_2_))
            {
                this.collidingBoundingBoxes.add(axisalignedbb1);
            }

            axisalignedbb1 = p_72945_1_.getCollisionBox((Entity)list.get(j2));

            if (axisalignedbb1 != null && axisalignedbb1.intersectsWith(p_72945_2_))
            {
                this.collidingBoundingBoxes.add(axisalignedbb1);
            }
        }

        return this.collidingBoundingBoxes;
    }

    public List func_147461_a(AxisAlignedBB p_147461_1_)
    {
        this.collidingBoundingBoxes.clear();
        int i = MathHelper.floor_double(p_147461_1_.minX);
        int j = MathHelper.floor_double(p_147461_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_147461_1_.minY);
        int l = MathHelper.floor_double(p_147461_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_147461_1_.minZ);
        int j1 = MathHelper.floor_double(p_147461_1_.maxZ + 1.0D);

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = i1; l1 < j1; ++l1)
            {
                if (this.blockExists(k1, 64, l1))
                {
                    for (int i2 = k - 1; i2 < l; ++i2)
                    {
                        Block block;

                        if (k1 >= -30000000 && k1 < 30000000 && l1 >= -30000000 && l1 < 30000000)
                        {
                            block = this.getBlock(k1, i2, l1);
                        }
                        else
                        {
                            block = Blocks.bedrock;
                        }

                        block.addCollisionBoxesToList(this, k1, i2, l1, p_147461_1_, this.collidingBoundingBoxes, (Entity)null);
                    }
                }
            }
        }

        return this.collidingBoundingBoxes;
    }

    /**
     * Returns the amount of skylight subtracted for the current time
     */
    public int calculateSkylightSubtracted(float p_72967_1_)
    {
        float f2 = provider.getSunBrightnessFactor(p_72967_1_);
        f2 = 1.0F - f2;
        return (int)(f2 * 11.0F);
    }

    /**
     * The current sun brightness factor for this dimension.
     * 0.0f means no light at all, and 1.0f means maximum sunlight.
     * Highly recommended for sunlight detection like solar panel.
     *
     * @return The current brightness factor
     * */
    public float getSunBrightnessFactor(float p_72967_1_)
    {
        float f1 = this.getCelestialAngle(p_72967_1_);
        float f2 = 1.0F - (MathHelper.cos(f1 * (float)Math.PI * 2.0F) * 2.0F + 0.5F);

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        f2 = 1.0F - f2;
        f2 = (float)((double)f2 * (1.0D - (double)(this.getRainStrength(p_72967_1_) * 5.0F) / 16.0D));
        f2 = (float)((double)f2 * (1.0D - (double)(this.getWeightedThunderStrength(p_72967_1_) * 5.0F) / 16.0D));
        return f2;
    }

    /**
     * Removes a worldAccess from the worldAccesses object
     */
    public void removeWorldAccess(IWorldAccess p_72848_1_)
    {
        this.worldAccesses.remove(p_72848_1_);
    }

    /**
     * Returns the sun brightness - checks time of day, rain and thunder
     */
    @SideOnly(Side.CLIENT)
    public float getSunBrightness(float p_72971_1_)
    {
        return provider.getSunBrightness(p_72971_1_);
    }

    @SideOnly(Side.CLIENT)
    public float getSunBrightnessBody(float p_72971_1_)
    {
        float f1 = this.getCelestialAngle(p_72971_1_);
        float f2 = 1.0F - (MathHelper.cos(f1 * (float)Math.PI * 2.0F) * 2.0F + 0.2F);

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        f2 = 1.0F - f2;
        f2 = (float)((double)f2 * (1.0D - (double)(this.getRainStrength(p_72971_1_) * 5.0F) / 16.0D));
        f2 = (float)((double)f2 * (1.0D - (double)(this.getWeightedThunderStrength(p_72971_1_) * 5.0F) / 16.0D));
        return f2 * 0.8F + 0.2F;
    }

    /**
     * Calculates the color for the skybox
     */
    @SideOnly(Side.CLIENT)
    public Vec3 getSkyColor(Entity p_72833_1_, float p_72833_2_)
    {
        return provider.getSkyColor(p_72833_1_, p_72833_2_);
    }

    @SideOnly(Side.CLIENT)
    public Vec3 getSkyColorBody(Entity p_72833_1_, float p_72833_2_)
    {
        float f1 = this.getCelestialAngle(p_72833_2_);
        float f2 = MathHelper.cos(f1 * (float)Math.PI * 2.0F) * 2.0F + 0.5F;

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        int i = MathHelper.floor_double(p_72833_1_.posX);
        int j = MathHelper.floor_double(p_72833_1_.posY);
        int k = MathHelper.floor_double(p_72833_1_.posZ);
        int l = ForgeHooksClient.getSkyBlendColour(this, i, j, k);
        float f4 = (float)(l >> 16 & 255) / 255.0F;
        float f5 = (float)(l >> 8 & 255) / 255.0F;
        float f6 = (float)(l & 255) / 255.0F;
        f4 *= f2;
        f5 *= f2;
        f6 *= f2;
        float f7 = this.getRainStrength(p_72833_2_);
        float f8;
        float f9;

        if (f7 > 0.0F)
        {
            f8 = (f4 * 0.3F + f5 * 0.59F + f6 * 0.11F) * 0.6F;
            f9 = 1.0F - f7 * 0.75F;
            f4 = f4 * f9 + f8 * (1.0F - f9);
            f5 = f5 * f9 + f8 * (1.0F - f9);
            f6 = f6 * f9 + f8 * (1.0F - f9);
        }

        f8 = this.getWeightedThunderStrength(p_72833_2_);

        if (f8 > 0.0F)
        {
            f9 = (f4 * 0.3F + f5 * 0.59F + f6 * 0.11F) * 0.2F;
            float f10 = 1.0F - f8 * 0.75F;
            f4 = f4 * f10 + f9 * (1.0F - f10);
            f5 = f5 * f10 + f9 * (1.0F - f10);
            f6 = f6 * f10 + f9 * (1.0F - f10);
        }

        if (this.lastLightningBolt > 0)
        {
            f9 = (float)this.lastLightningBolt - p_72833_2_;

            if (f9 > 1.0F)
            {
                f9 = 1.0F;
            }

            f9 *= 0.45F;
            f4 = f4 * (1.0F - f9) + 0.8F * f9;
            f5 = f5 * (1.0F - f9) + 0.8F * f9;
            f6 = f6 * (1.0F - f9) + 1.0F * f9;
        }

        return Vec3.createVectorHelper((double)f4, (double)f5, (double)f6);
    }

    /**
     * calls calculateCelestialAngle
     */
    public float getCelestialAngle(float p_72826_1_)
    {
        return this.provider.calculateCelestialAngle(this.worldInfo.getWorldTime(), p_72826_1_);
    }

    @SideOnly(Side.CLIENT)
    public int getMoonPhase()
    {
        return this.provider.getMoonPhase(this.worldInfo.getWorldTime());
    }

    /**
     * gets the current fullness of the moon expressed as a float between 1.0 and 0.0, in steps of .25
     */
    public float getCurrentMoonPhaseFactor()
    {
        return provider.getCurrentMoonPhaseFactor();
    }

    public float getCurrentMoonPhaseFactorBody()
    {
        return WorldProvider.moonPhaseFactors[this.provider.getMoonPhase(this.worldInfo.getWorldTime())];
    }

    /**
     * Return getCelestialAngle()*2*PI
     */
    public float getCelestialAngleRadians(float p_72929_1_)
    {
        float f1 = this.getCelestialAngle(p_72929_1_);
        return f1 * (float)Math.PI * 2.0F;
    }

    @SideOnly(Side.CLIENT)
    public Vec3 getCloudColour(float p_72824_1_)
    {
        return provider.drawClouds(p_72824_1_);
    }

    @SideOnly(Side.CLIENT)
    public Vec3 drawCloudsBody(float p_72824_1_)
    {
        float f1 = this.getCelestialAngle(p_72824_1_);
        float f2 = MathHelper.cos(f1 * (float)Math.PI * 2.0F) * 2.0F + 0.5F;

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        float f3 = (float)(this.cloudColour >> 16 & 255L) / 255.0F;
        float f4 = (float)(this.cloudColour >> 8 & 255L) / 255.0F;
        float f5 = (float)(this.cloudColour & 255L) / 255.0F;
        float f6 = this.getRainStrength(p_72824_1_);
        float f7;
        float f8;

        if (f6 > 0.0F)
        {
            f7 = (f3 * 0.3F + f4 * 0.59F + f5 * 0.11F) * 0.6F;
            f8 = 1.0F - f6 * 0.95F;
            f3 = f3 * f8 + f7 * (1.0F - f8);
            f4 = f4 * f8 + f7 * (1.0F - f8);
            f5 = f5 * f8 + f7 * (1.0F - f8);
        }

        f3 *= f2 * 0.9F + 0.1F;
        f4 *= f2 * 0.9F + 0.1F;
        f5 *= f2 * 0.85F + 0.15F;
        f7 = this.getWeightedThunderStrength(p_72824_1_);

        if (f7 > 0.0F)
        {
            f8 = (f3 * 0.3F + f4 * 0.59F + f5 * 0.11F) * 0.2F;
            float f9 = 1.0F - f7 * 0.95F;
            f3 = f3 * f9 + f8 * (1.0F - f9);
            f4 = f4 * f9 + f8 * (1.0F - f9);
            f5 = f5 * f9 + f8 * (1.0F - f9);
        }

        return Vec3.createVectorHelper((double)f3, (double)f4, (double)f5);
    }

    /**
     * Returns vector(ish) with R/G/B for fog
     */
    @SideOnly(Side.CLIENT)
    public Vec3 getFogColor(float p_72948_1_)
    {
        float f1 = this.getCelestialAngle(p_72948_1_);
        return this.provider.getFogColor(f1, p_72948_1_);
    }

    /**
     * Gets the height to which rain/snow will fall. Calculates it if not already stored.
     */
    public int getPrecipitationHeight(int p_72874_1_, int p_72874_2_)
    {
        return this.getChunkFromBlockCoords(p_72874_1_, p_72874_2_).getPrecipitationHeight(p_72874_1_ & 15, p_72874_2_ & 15);
    }

    /**
     * Finds the highest block on the x, z coordinate that is solid and returns its y coord. Args x, z
     */
    public int getTopSolidOrLiquidBlock(int p_72825_1_, int p_72825_2_)
    {
        Chunk chunk = this.getChunkFromBlockCoords(p_72825_1_, p_72825_2_);
        int x = p_72825_1_;
        int z = p_72825_2_;
        int k = chunk.getTopFilledSegment() + 15;
        p_72825_1_ &= 15;

        for (p_72825_2_ &= 15; k > 0; --k)
        {
            Block block = chunk.getBlock(p_72825_1_, k, p_72825_2_);

            if (block.getMaterial().blocksMovement() && block.getMaterial() != Material.leaves && !block.isFoliage(this, x, k, z))
            {
                return k + 1;
            }
        }

        return -1;
    }

    /**
     * How bright are stars in the sky
     */
    @SideOnly(Side.CLIENT)
    public float getStarBrightness(float p_72880_1_)
    {
        return provider.getStarBrightness(p_72880_1_);
    }

    @SideOnly(Side.CLIENT)
    public float getStarBrightnessBody(float par1)
    {
        float f1 = this.getCelestialAngle(par1);
        float f2 = 1.0F - (MathHelper.cos(f1 * (float)Math.PI * 2.0F) * 2.0F + 0.25F);

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        return f2 * f2 * 0.5F;
    }

    /**
     * Schedules a tick to a block with a delay (Most commonly the tick rate)
     */
    public void scheduleBlockUpdate(int p_147464_1_, int p_147464_2_, int p_147464_3_, Block p_147464_4_, int p_147464_5_) {}

    public void scheduleBlockUpdateWithPriority(int p_147454_1_, int p_147454_2_, int p_147454_3_, Block p_147454_4_, int p_147454_5_, int p_147454_6_) {}

    public void func_147446_b(int p_147446_1_, int p_147446_2_, int p_147446_3_, Block p_147446_4_, int p_147446_5_, int p_147446_6_) {}

    /**
     * Updates (and cleans up) entities and tile entities
     */
    public void updateEntities()
    {
        this.theProfiler.startSection("entities");
        this.theProfiler.startSection("global");
        int i;
        Entity entity;
        CrashReport crashreport;
        CrashReportCategory crashreportcategory;

        for (i = 0; i < this.weatherEffects.size(); ++i)
        {
            entity = (Entity)this.weatherEffects.get(i);

            try
            {
                ++entity.ticksExisted;
                entity.onUpdate();
            }
            catch (Throwable throwable2)
            {
                crashreport = CrashReport.makeCrashReport(throwable2, "Ticking entity");
                crashreportcategory = crashreport.makeCategory("Entity being ticked");

                if (entity == null)
                {
                    crashreportcategory.addCrashSection("Entity", "~~NULL~~");
                }
                else
                {
                    entity.addEntityCrashInfo(crashreportcategory);
                }

                if (ForgeModContainer.removeErroringEntities)
                {
                    FMLLog.getLogger().log(org.apache.logging.log4j.Level.ERROR, crashreport.getCompleteReport());
                    removeEntity(entity);
                }
                else
                {
                    throw new ReportedException(crashreport);
                }
            }

            if (entity.isDead)
            {
                this.weatherEffects.remove(i--);
            }
        }

        this.theProfiler.endStartSection("remove");
        this.loadedEntityList.removeAll(this.unloadedEntityList);
        int j;
        int l;

        for (i = 0; i < this.unloadedEntityList.size(); ++i)
        {
            entity = (Entity)this.unloadedEntityList.get(i);
            j = entity.chunkCoordX;
            l = entity.chunkCoordZ;

            if (entity.addedToChunk && this.chunkExists(j, l))
            {
                this.getChunkFromChunkCoords(j, l).removeEntity(entity);
            }
        }

        for (i = 0; i < this.unloadedEntityList.size(); ++i)
        {
            this.onEntityRemoved((Entity)this.unloadedEntityList.get(i));
        }

        this.unloadedEntityList.clear();
        this.theProfiler.endStartSection("regular");

        for (i = 0; i < this.loadedEntityList.size(); ++i)
        {
            entity = (Entity)this.loadedEntityList.get(i);

            if (entity.ridingEntity != null)
            {
                if (!entity.ridingEntity.isDead && entity.ridingEntity.riddenByEntity == entity)
                {
                    continue;
                }

                entity.ridingEntity.riddenByEntity = null;
                entity.ridingEntity = null;
            }

            this.theProfiler.startSection("tick");

            if (!entity.isDead)
            {
                try
                {
                    this.updateEntity(entity);
                }
                catch (Throwable throwable1)
                {
                    crashreport = CrashReport.makeCrashReport(throwable1, "Ticking entity");
                    crashreportcategory = crashreport.makeCategory("Entity being ticked");
                    entity.addEntityCrashInfo(crashreportcategory);

                    if (ForgeModContainer.removeErroringEntities)
                    {
                        FMLLog.getLogger().log(org.apache.logging.log4j.Level.ERROR, crashreport.getCompleteReport());
                        removeEntity(entity);
                    }
                    else
                    {
                        throw new ReportedException(crashreport);
                    }
                }
            }

            this.theProfiler.endSection();
            this.theProfiler.startSection("remove");

            if (entity.isDead)
            {
                j = entity.chunkCoordX;
                l = entity.chunkCoordZ;

                if (entity.addedToChunk && this.chunkExists(j, l))
                {
                    this.getChunkFromChunkCoords(j, l).removeEntity(entity);
                }

                this.loadedEntityList.remove(i--);
                this.onEntityRemoved(entity);
            }

            this.theProfiler.endSection();
        }

        this.theProfiler.endStartSection("blockEntities");
        this.field_147481_N = true;
        Iterator iterator = this.loadedTileEntityList.iterator();

        while (iterator.hasNext())
        {
            TileEntity tileentity = (TileEntity)iterator.next();

            if (!tileentity.isInvalid() && tileentity.hasWorldObj() && this.blockExists(tileentity.xCoord, tileentity.yCoord, tileentity.zCoord))
            {
                try
                {
                    tileentity.updateEntity();
                }
                catch (Throwable throwable)
                {
                    crashreport = CrashReport.makeCrashReport(throwable, "Ticking block entity");
                    crashreportcategory = crashreport.makeCategory("Block entity being ticked");
                    tileentity.func_145828_a(crashreportcategory);
                    if (ForgeModContainer.removeErroringTileEntities)
                    {
                        FMLLog.getLogger().log(org.apache.logging.log4j.Level.ERROR, crashreport.getCompleteReport());
                        tileentity.invalidate();
                        setBlockToAir(tileentity.xCoord, tileentity.yCoord, tileentity.zCoord);
                    }
                    else
                    {
                        throw new ReportedException(crashreport);
                    }
                }
            }

            if (tileentity.isInvalid())
            {
                iterator.remove();

                if (this.chunkExists(tileentity.xCoord >> 4, tileentity.zCoord >> 4))
                {
                    Chunk chunk = this.getChunkFromChunkCoords(tileentity.xCoord >> 4, tileentity.zCoord >> 4);

                    if (chunk != null)
                    {
                        chunk.removeInvalidTileEntity(tileentity.xCoord & 15, tileentity.yCoord, tileentity.zCoord & 15);
                    }
                }
            }
        }

        if (!this.field_147483_b.isEmpty())
        {
            for (Object tile : field_147483_b)
            {
               ((TileEntity)tile).onChunkUnload();
            }
            this.loadedTileEntityList.removeAll(this.field_147483_b);
            this.field_147483_b.clear();
        }

        this.field_147481_N = false;

        this.theProfiler.endStartSection("pendingBlockEntities");

        if (!this.addedTileEntityList.isEmpty())
        {
            for (int k = 0; k < this.addedTileEntityList.size(); ++k)
            {
                TileEntity tileentity1 = (TileEntity)this.addedTileEntityList.get(k);

                if (!tileentity1.isInvalid())
                {
                    if (!this.loadedTileEntityList.contains(tileentity1))
                    {
                        this.loadedTileEntityList.add(tileentity1);
                    }
                }
                else
                {
                    if (this.chunkExists(tileentity1.xCoord >> 4, tileentity1.zCoord >> 4))
                    {
                        Chunk chunk1 = this.getChunkFromChunkCoords(tileentity1.xCoord >> 4, tileentity1.zCoord >> 4);

                        if (chunk1 != null)
                        {
                            chunk1.removeInvalidTileEntity(tileentity1.xCoord & 15, tileentity1.yCoord, tileentity1.zCoord & 15);
                        }
                    }
                }
            }

            this.addedTileEntityList.clear();
        }

        this.theProfiler.endSection();
        this.theProfiler.endSection();
    }

    public void func_147448_a(Collection p_147448_1_)
    {
        List dest = field_147481_N ? addedTileEntityList : loadedTileEntityList;
        for(TileEntity entity : (Collection<TileEntity>)p_147448_1_)
        {
            if(entity.canUpdate()) dest.add(entity);
        }
    }

    /**
     * Will update the entity in the world if the chunk the entity is in is currently loaded. Args: entity
     */
    public void updateEntity(Entity p_72870_1_)
    {
        this.updateEntityWithOptionalForce(p_72870_1_, true);
    }

    /**
     * Will update the entity in the world if the chunk the entity is in is currently loaded or its forced to update.
     * Args: entity, forceUpdate
     */
    public void updateEntityWithOptionalForce(Entity p_72866_1_, boolean p_72866_2_)
    {
        int i = MathHelper.floor_double(p_72866_1_.posX);
        int j = MathHelper.floor_double(p_72866_1_.posZ);
        boolean isForced = getPersistentChunks().containsKey(new ChunkCoordIntPair(i >> 4, j >> 4));
        byte b0 = isForced ? (byte)0 : 32;
        boolean canUpdate = !p_72866_2_ || this.checkChunksExist(i - b0, 0, j - b0, i + b0, 0, j + b0);

        if (!canUpdate)
        {
            EntityEvent.CanUpdate event = new EntityEvent.CanUpdate(p_72866_1_);
            MinecraftForge.EVENT_BUS.post(event);
            canUpdate = event.canUpdate;
        }

        if (canUpdate)
        {
            p_72866_1_.lastTickPosX = p_72866_1_.posX;
            p_72866_1_.lastTickPosY = p_72866_1_.posY;
            p_72866_1_.lastTickPosZ = p_72866_1_.posZ;
            p_72866_1_.prevRotationYaw = p_72866_1_.rotationYaw;
            p_72866_1_.prevRotationPitch = p_72866_1_.rotationPitch;

            if (p_72866_2_ && p_72866_1_.addedToChunk)
            {
                ++p_72866_1_.ticksExisted;

                if (p_72866_1_.ridingEntity != null)
                {
                    p_72866_1_.updateRidden();
                }
                else
                {
                    p_72866_1_.onUpdate();
                }
            }

            this.theProfiler.startSection("chunkCheck");

            if (Double.isNaN(p_72866_1_.posX) || Double.isInfinite(p_72866_1_.posX))
            {
                p_72866_1_.posX = p_72866_1_.lastTickPosX;
            }

            if (Double.isNaN(p_72866_1_.posY) || Double.isInfinite(p_72866_1_.posY))
            {
                p_72866_1_.posY = p_72866_1_.lastTickPosY;
            }

            if (Double.isNaN(p_72866_1_.posZ) || Double.isInfinite(p_72866_1_.posZ))
            {
                p_72866_1_.posZ = p_72866_1_.lastTickPosZ;
            }

            if (Double.isNaN((double)p_72866_1_.rotationPitch) || Double.isInfinite((double)p_72866_1_.rotationPitch))
            {
                p_72866_1_.rotationPitch = p_72866_1_.prevRotationPitch;
            }

            if (Double.isNaN((double)p_72866_1_.rotationYaw) || Double.isInfinite((double)p_72866_1_.rotationYaw))
            {
                p_72866_1_.rotationYaw = p_72866_1_.prevRotationYaw;
            }

            int k = MathHelper.floor_double(p_72866_1_.posX / 16.0D);
            int l = MathHelper.floor_double(p_72866_1_.posY / 16.0D);
            int i1 = MathHelper.floor_double(p_72866_1_.posZ / 16.0D);

            if (!p_72866_1_.addedToChunk || p_72866_1_.chunkCoordX != k || p_72866_1_.chunkCoordY != l || p_72866_1_.chunkCoordZ != i1)
            {
                if (p_72866_1_.addedToChunk && this.chunkExists(p_72866_1_.chunkCoordX, p_72866_1_.chunkCoordZ))
                {
                    this.getChunkFromChunkCoords(p_72866_1_.chunkCoordX, p_72866_1_.chunkCoordZ).removeEntityAtIndex(p_72866_1_, p_72866_1_.chunkCoordY);
                }

                if (this.chunkExists(k, i1))
                {
                    p_72866_1_.addedToChunk = true;
                    this.getChunkFromChunkCoords(k, i1).addEntity(p_72866_1_);
                }
                else
                {
                    p_72866_1_.addedToChunk = false;
                }
            }

            this.theProfiler.endSection();

            if (p_72866_2_ && p_72866_1_.addedToChunk && p_72866_1_.riddenByEntity != null)
            {
                if (!p_72866_1_.riddenByEntity.isDead && p_72866_1_.riddenByEntity.ridingEntity == p_72866_1_)
                {
                    this.updateEntity(p_72866_1_.riddenByEntity);
                }
                else
                {
                    p_72866_1_.riddenByEntity.ridingEntity = null;
                    p_72866_1_.riddenByEntity = null;
                }
            }
        }
    }

    /**
     * Returns true if there are no solid, live entities in the specified AxisAlignedBB
     */
    public boolean checkNoEntityCollision(AxisAlignedBB p_72855_1_)
    {
        return this.checkNoEntityCollision(p_72855_1_, (Entity)null);
    }

    /**
     * Returns true if there are no solid, live entities in the specified AxisAlignedBB, excluding the given entity
     */
    public boolean checkNoEntityCollision(AxisAlignedBB p_72917_1_, Entity p_72917_2_)
    {
        List list = this.getEntitiesWithinAABBExcludingEntity((Entity)null, p_72917_1_);

        for (int i = 0; i < list.size(); ++i)
        {
            Entity entity1 = (Entity)list.get(i);

            if (!entity1.isDead && entity1.preventEntitySpawning && entity1 != p_72917_2_)
            {
                return false;
            }
        }

        return true;
    }

    /**
     * Returns true if there are any blocks in the region constrained by an AxisAlignedBB
     */
    public boolean checkBlockCollision(AxisAlignedBB p_72829_1_)
    {
        int i = MathHelper.floor_double(p_72829_1_.minX);
        int j = MathHelper.floor_double(p_72829_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72829_1_.minY);
        int l = MathHelper.floor_double(p_72829_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72829_1_.minZ);
        int j1 = MathHelper.floor_double(p_72829_1_.maxZ + 1.0D);

        if (p_72829_1_.minX < 0.0D)
        {
            --i;
        }

        if (p_72829_1_.minY < 0.0D)
        {
            --k;
        }

        if (p_72829_1_.minZ < 0.0D)
        {
            --i1;
        }

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = k; l1 < l; ++l1)
            {
                for (int i2 = i1; i2 < j1; ++i2)
                {
                    Block block = this.getBlock(k1, l1, i2);

                    if (block.getMaterial() != Material.air)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    /**
     * Returns if any of the blocks within the aabb are liquids. Args: aabb
     */
    public boolean isAnyLiquid(AxisAlignedBB p_72953_1_)
    {
        int i = MathHelper.floor_double(p_72953_1_.minX);
        int j = MathHelper.floor_double(p_72953_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72953_1_.minY);
        int l = MathHelper.floor_double(p_72953_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72953_1_.minZ);
        int j1 = MathHelper.floor_double(p_72953_1_.maxZ + 1.0D);

        if (p_72953_1_.minX < 0.0D)
        {
            --i;
        }

        if (p_72953_1_.minY < 0.0D)
        {
            --k;
        }

        if (p_72953_1_.minZ < 0.0D)
        {
            --i1;
        }

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = k; l1 < l; ++l1)
            {
                for (int i2 = i1; i2 < j1; ++i2)
                {
                    Block block = this.getBlock(k1, l1, i2);

                    if (block.getMaterial().isLiquid())
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    public boolean func_147470_e(AxisAlignedBB p_147470_1_)
    {
        int i = MathHelper.floor_double(p_147470_1_.minX);
        int j = MathHelper.floor_double(p_147470_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_147470_1_.minY);
        int l = MathHelper.floor_double(p_147470_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_147470_1_.minZ);
        int j1 = MathHelper.floor_double(p_147470_1_.maxZ + 1.0D);

        if (this.checkChunksExist(i, k, i1, j, l, j1))
        {
            for (int k1 = i; k1 < j; ++k1)
            {
                for (int l1 = k; l1 < l; ++l1)
                {
                    for (int i2 = i1; i2 < j1; ++i2)
                    {
                        Block block = this.getBlock(k1, l1, i2);

                        if (block == Blocks.fire || block == Blocks.flowing_lava || block == Blocks.lava)
                        {
                            return true;
                        }
                        else
                        {
                            if (block.isBurning(this, k1, l1, i2)) return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    /**
     * handles the acceleration of an object whilst in water. Not sure if it is used elsewhere.
     */
    public boolean handleMaterialAcceleration(AxisAlignedBB p_72918_1_, Material p_72918_2_, Entity p_72918_3_)
    {
        int i = MathHelper.floor_double(p_72918_1_.minX);
        int j = MathHelper.floor_double(p_72918_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72918_1_.minY);
        int l = MathHelper.floor_double(p_72918_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72918_1_.minZ);
        int j1 = MathHelper.floor_double(p_72918_1_.maxZ + 1.0D);

        if (!this.checkChunksExist(i, k, i1, j, l, j1))
        {
            return false;
        }
        else
        {
            boolean flag = false;
            Vec3 vec3 = Vec3.createVectorHelper(0.0D, 0.0D, 0.0D);

            for (int k1 = i; k1 < j; ++k1)
            {
                for (int l1 = k; l1 < l; ++l1)
                {
                    for (int i2 = i1; i2 < j1; ++i2)
                    {
                        Block block = this.getBlock(k1, l1, i2);

                        if (block.getMaterial() == p_72918_2_)
                        {
                            double d0 = (double)((float)(l1 + 1) - BlockLiquid.getLiquidHeightPercent(this.getBlockMetadata(k1, l1, i2)));

                            if ((double)l >= d0)
                            {
                                flag = true;
                                block.velocityToAddToEntity(this, k1, l1, i2, p_72918_3_, vec3);
                            }
                        }
                    }
                }
            }

            if (vec3.lengthVector() > 0.0D && p_72918_3_.isPushedByWater())
            {
                vec3 = vec3.normalize();
                double d1 = 0.014D;
                p_72918_3_.motionX += vec3.xCoord * d1;
                p_72918_3_.motionY += vec3.yCoord * d1;
                p_72918_3_.motionZ += vec3.zCoord * d1;
            }

            return flag;
        }
    }

    /**
     * Returns true if the given bounding box contains the given material
     */
    public boolean isMaterialInBB(AxisAlignedBB p_72875_1_, Material p_72875_2_)
    {
        int i = MathHelper.floor_double(p_72875_1_.minX);
        int j = MathHelper.floor_double(p_72875_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72875_1_.minY);
        int l = MathHelper.floor_double(p_72875_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72875_1_.minZ);
        int j1 = MathHelper.floor_double(p_72875_1_.maxZ + 1.0D);

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = k; l1 < l; ++l1)
            {
                for (int i2 = i1; i2 < j1; ++i2)
                {
                    if (this.getBlock(k1, l1, i2).getMaterial() == p_72875_2_)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    /**
     * checks if the given AABB is in the material given. Used while swimming.
     */
    public boolean isAABBInMaterial(AxisAlignedBB p_72830_1_, Material p_72830_2_)
    {
        int i = MathHelper.floor_double(p_72830_1_.minX);
        int j = MathHelper.floor_double(p_72830_1_.maxX + 1.0D);
        int k = MathHelper.floor_double(p_72830_1_.minY);
        int l = MathHelper.floor_double(p_72830_1_.maxY + 1.0D);
        int i1 = MathHelper.floor_double(p_72830_1_.minZ);
        int j1 = MathHelper.floor_double(p_72830_1_.maxZ + 1.0D);

        for (int k1 = i; k1 < j; ++k1)
        {
            for (int l1 = k; l1 < l; ++l1)
            {
                for (int i2 = i1; i2 < j1; ++i2)
                {
                    Block block = this.getBlock(k1, l1, i2);

                    if (block.getMaterial() == p_72830_2_)
                    {
                        int j2 = this.getBlockMetadata(k1, l1, i2);
                        double d0 = (double)(l1 + 1);

                        if (j2 < 8)
                        {
                            d0 = (double)(l1 + 1) - (double)j2 / 8.0D;
                        }

                        if (d0 >= p_72830_1_.minY)
                        {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    /**
     * Creates an explosion. Args: entity, x, y, z, strength
     */
    public Explosion createExplosion(Entity p_72876_1_, double p_72876_2_, double p_72876_4_, double p_72876_6_, float p_72876_8_, boolean p_72876_9_)
    {
        return this.newExplosion(p_72876_1_, p_72876_2_, p_72876_4_, p_72876_6_, p_72876_8_, false, p_72876_9_);
    }

    /**
     * returns a new explosion. Does initiation (at time of writing Explosion is not finished)
     */
    public Explosion newExplosion(Entity p_72885_1_, double p_72885_2_, double p_72885_4_, double p_72885_6_, float p_72885_8_, boolean p_72885_9_, boolean p_72885_10_)
    {
        Explosion explosion = new Explosion(this, p_72885_1_, p_72885_2_, p_72885_4_, p_72885_6_, p_72885_8_);
        explosion.isFlaming = p_72885_9_;
        explosion.isSmoking = p_72885_10_;
        if (net.minecraftforge.event.ForgeEventFactory.onExplosionStart(this, explosion)) return explosion;
        explosion.doExplosionA();
        explosion.doExplosionB(true);
        return explosion;
    }

    /**
     * Gets the percentage of real blocks within within a bounding box, along a specified vector.
     */
    public float getBlockDensity(Vec3 p_72842_1_, AxisAlignedBB p_72842_2_)
    {
        double d0 = 1.0D / ((p_72842_2_.maxX - p_72842_2_.minX) * 2.0D + 1.0D);
        double d1 = 1.0D / ((p_72842_2_.maxY - p_72842_2_.minY) * 2.0D + 1.0D);
        double d2 = 1.0D / ((p_72842_2_.maxZ - p_72842_2_.minZ) * 2.0D + 1.0D);

        if (d0 >= 0.0D && d1 >= 0.0D && d2 >= 0.0D)
        {
            int i = 0;
            int j = 0;

            for (float f = 0.0F; f <= 1.0F; f = (float)((double)f + d0))
            {
                for (float f1 = 0.0F; f1 <= 1.0F; f1 = (float)((double)f1 + d1))
                {
                    for (float f2 = 0.0F; f2 <= 1.0F; f2 = (float)((double)f2 + d2))
                    {
                        double d3 = p_72842_2_.minX + (p_72842_2_.maxX - p_72842_2_.minX) * (double)f;
                        double d4 = p_72842_2_.minY + (p_72842_2_.maxY - p_72842_2_.minY) * (double)f1;
                        double d5 = p_72842_2_.minZ + (p_72842_2_.maxZ - p_72842_2_.minZ) * (double)f2;

                        if (this.rayTraceBlocks(Vec3.createVectorHelper(d3, d4, d5), p_72842_1_) == null)
                        {
                            ++i;
                        }

                        ++j;
                    }
                }
            }

            return (float)i / (float)j;
        }
        else
        {
            return 0.0F;
        }
    }

    /**
     * If the block in the given direction of the given coordinate is fire, extinguish it. Args: Player, X,Y,Z,
     * blockDirection
     */
    public boolean extinguishFire(EntityPlayer p_72886_1_, int p_72886_2_, int p_72886_3_, int p_72886_4_, int p_72886_5_)
    {
        if (p_72886_5_ == 0)
        {
            --p_72886_3_;
        }

        if (p_72886_5_ == 1)
        {
            ++p_72886_3_;
        }

        if (p_72886_5_ == 2)
        {
            --p_72886_4_;
        }

        if (p_72886_5_ == 3)
        {
            ++p_72886_4_;
        }

        if (p_72886_5_ == 4)
        {
            --p_72886_2_;
        }

        if (p_72886_5_ == 5)
        {
            ++p_72886_2_;
        }

        if (this.getBlock(p_72886_2_, p_72886_3_, p_72886_4_) == Blocks.fire)
        {
            this.playAuxSFXAtEntity(p_72886_1_, 1004, p_72886_2_, p_72886_3_, p_72886_4_, 0);
            this.setBlockToAir(p_72886_2_, p_72886_3_, p_72886_4_);
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * This string is 'All: (number of loaded entities)' Viewable by press ing F3
     */
    @SideOnly(Side.CLIENT)
    public String getDebugLoadedEntities()
    {
        return "All: " + this.loadedEntityList.size();
    }

    /**
     * Returns the name of the current chunk provider, by calling chunkprovider.makeString()
     */
    @SideOnly(Side.CLIENT)
    public String getProviderName()
    {
        return this.chunkProvider.makeString();
    }

    public TileEntity getTileEntity(int p_147438_1_, int p_147438_2_, int p_147438_3_)
    {
        if (p_147438_2_ >= 0 && p_147438_2_ < 256)
        {
            TileEntity tileentity = null;
            int l;
            TileEntity tileentity1;

            if (this.field_147481_N)
            {
                for (l = 0; l < this.addedTileEntityList.size(); ++l)
                {
                    tileentity1 = (TileEntity)this.addedTileEntityList.get(l);

                    if (!tileentity1.isInvalid() && tileentity1.xCoord == p_147438_1_ && tileentity1.yCoord == p_147438_2_ && tileentity1.zCoord == p_147438_3_)
                    {
                        tileentity = tileentity1;
                        break;
                    }
                }
            }

            if (tileentity == null)
            {
                Chunk chunk = this.getChunkFromChunkCoords(p_147438_1_ >> 4, p_147438_3_ >> 4);

                if (chunk != null)
                {
                    tileentity = chunk.func_150806_e(p_147438_1_ & 15, p_147438_2_, p_147438_3_ & 15);
                }
            }

            if (tileentity == null)
            {
                for (l = 0; l < this.addedTileEntityList.size(); ++l)
                {
                    tileentity1 = (TileEntity)this.addedTileEntityList.get(l);

                    if (!tileentity1.isInvalid() && tileentity1.xCoord == p_147438_1_ && tileentity1.yCoord == p_147438_2_ && tileentity1.zCoord == p_147438_3_)
                    {
                        tileentity = tileentity1;
                        break;
                    }
                }
            }

            return tileentity;
        }
        else
        {
            return null;
        }
    }

    public void setTileEntity(int p_147455_1_, int p_147455_2_, int p_147455_3_, TileEntity p_147455_4_)
    {
        if (p_147455_4_ == null || p_147455_4_.isInvalid())
        {
            return;
        }

        if (p_147455_4_.canUpdate())
        {
            if (this.field_147481_N)
            {
                Iterator iterator = this.addedTileEntityList.iterator();

                while (iterator.hasNext())
                {
                    TileEntity tileentity1 = (TileEntity)iterator.next();

                    if (tileentity1.xCoord == p_147455_1_ && tileentity1.yCoord == p_147455_2_ && tileentity1.zCoord == p_147455_3_)
                    {
                        tileentity1.invalidate();
                        iterator.remove();
                    }
                }

                this.addedTileEntityList.add(p_147455_4_);
            }
            else
            {
                this.loadedTileEntityList.add(p_147455_4_);
            }
        }
        Chunk chunk = this.getChunkFromChunkCoords(p_147455_1_ >> 4, p_147455_3_ >> 4);
        if (chunk != null)
        {
            chunk.func_150812_a(p_147455_1_ & 15, p_147455_2_, p_147455_3_ & 15, p_147455_4_);
        }
        //notify tile changes
        func_147453_f(p_147455_1_, p_147455_2_, p_147455_3_, getBlock(p_147455_1_, p_147455_2_, p_147455_3_));
    }

    public void removeTileEntity(int p_147475_1_, int p_147475_2_, int p_147475_3_)
    {
        Chunk chunk = getChunkFromChunkCoords(p_147475_1_ >> 4, p_147475_3_ >> 4);
        if (chunk != null) chunk.removeTileEntity(p_147475_1_ & 15, p_147475_2_, p_147475_3_ & 15);
        func_147453_f(p_147475_1_, p_147475_2_, p_147475_3_, getBlock(p_147475_1_, p_147475_2_, p_147475_3_));
    }

    public void func_147457_a(TileEntity p_147457_1_)
    {
        this.field_147483_b.add(p_147457_1_);
    }

    public boolean func_147469_q(int p_147469_1_, int p_147469_2_, int p_147469_3_)
    {
        AxisAlignedBB axisalignedbb = this.getBlock(p_147469_1_, p_147469_2_, p_147469_3_).getCollisionBoundingBoxFromPool(this, p_147469_1_, p_147469_2_, p_147469_3_);
        return axisalignedbb != null && axisalignedbb.getAverageEdgeLength() >= 1.0D;
    }

    /**
     * Returns true if the block at the given coordinate has a solid (buildable) top surface.
     */
    public static boolean doesBlockHaveSolidTopSurface(IBlockAccess p_147466_0_, int p_147466_1_, int p_147466_2_, int p_147466_3_)
    {
        Block block = p_147466_0_.getBlock(p_147466_1_, p_147466_2_, p_147466_3_);
        return block.isSideSolid(p_147466_0_, p_147466_1_, p_147466_2_, p_147466_3_, ForgeDirection.UP);
    }

    /**
     * Checks if the block is a solid, normal cube. If the chunk does not exist, or is not loaded, it returns the
     * boolean parameter
     */
    public boolean isBlockNormalCubeDefault(int p_147445_1_, int p_147445_2_, int p_147445_3_, boolean p_147445_4_)
    {
        if (p_147445_1_ >= -30000000 && p_147445_3_ >= -30000000 && p_147445_1_ < 30000000 && p_147445_3_ < 30000000)
        {
            Chunk chunk = this.chunkProvider.provideChunk(p_147445_1_ >> 4, p_147445_3_ >> 4);

            if (chunk != null && !chunk.isEmpty())
            {
                Block block = this.getBlock(p_147445_1_, p_147445_2_, p_147445_3_);
                return block.isNormalCube(this, p_147445_1_, p_147445_2_, p_147445_3_);
            }
            else
            {
                return p_147445_4_;
            }
        }
        else
        {
            return p_147445_4_;
        }
    }

    /**
     * Called on construction of the World class to setup the initial skylight values
     */
    public void calculateInitialSkylight()
    {
        int i = this.calculateSkylightSubtracted(1.0F);

        if (i != this.skylightSubtracted)
        {
            this.skylightSubtracted = i;
        }
    }

    /**
     * Set which types of mobs are allowed to spawn (peaceful vs hostile).
     */
    public void setAllowedSpawnTypes(boolean p_72891_1_, boolean p_72891_2_)
    {
        provider.setAllowedSpawnTypes(p_72891_1_, p_72891_2_);
    }

    /**
     * Runs a single tick for the world
     */
    public void tick()
    {
        this.updateWeather();
    }

    /**
     * Called from World constructor to set rainingStrength and thunderingStrength
     */
    private void calculateInitialWeather()
    {
        provider.calculateInitialWeather();
    }

    public void calculateInitialWeatherBody()
    {
        if (this.worldInfo.isRaining())
        {
            this.rainingStrength = 1.0F;

            if (this.worldInfo.isThundering())
            {
                this.thunderingStrength = 1.0F;
            }
        }
    }

    /**
     * Updates all weather states.
     */
    protected void updateWeather()
    {
        provider.updateWeather();
    }

    public void updateWeatherBody()
    {
        if (!this.provider.hasNoSky)
        {
            if (!this.isRemote)
            {
                int i = this.worldInfo.getThunderTime();

                if (i <= 0)
                {
                    if (this.worldInfo.isThundering())
                    {
                        this.worldInfo.setThunderTime(this.rand.nextInt(12000) + 3600);
                    }
                    else
                    {
                        this.worldInfo.setThunderTime(this.rand.nextInt(168000) + 12000);
                    }
                }
                else
                {
                    --i;
                    this.worldInfo.setThunderTime(i);

                    if (i <= 0)
                    {
                        this.worldInfo.setThundering(!this.worldInfo.isThundering());
                    }
                }

                this.prevThunderingStrength = this.thunderingStrength;

                if (this.worldInfo.isThundering())
                {
                    this.thunderingStrength = (float)((double)this.thunderingStrength + 0.01D);
                }
                else
                {
                    this.thunderingStrength = (float)((double)this.thunderingStrength - 0.01D);
                }

                this.thunderingStrength = MathHelper.clamp_float(this.thunderingStrength, 0.0F, 1.0F);
                int j = this.worldInfo.getRainTime();

                if (j <= 0)
                {
                    if (this.worldInfo.isRaining())
                    {
                        this.worldInfo.setRainTime(this.rand.nextInt(12000) + 12000);
                    }
                    else
                    {
                        this.worldInfo.setRainTime(this.rand.nextInt(168000) + 12000);
                    }
                }
                else
                {
                    --j;
                    this.worldInfo.setRainTime(j);

                    if (j <= 0)
                    {
                        this.worldInfo.setRaining(!this.worldInfo.isRaining());
                    }
                }

                this.prevRainingStrength = this.rainingStrength;

                if (this.worldInfo.isRaining())
                {
                    this.rainingStrength = (float)((double)this.rainingStrength + 0.01D);
                }
                else
                {
                    this.rainingStrength = (float)((double)this.rainingStrength - 0.01D);
                }

                this.rainingStrength = MathHelper.clamp_float(this.rainingStrength, 0.0F, 1.0F);
            }
        }
    }

    protected void setActivePlayerChunksAndCheckLight()
    {
        this.activeChunkSet.clear();
        this.theProfiler.startSection("buildList");
        this.activeChunkSet.addAll(getPersistentChunks().keySet());
        int i;
        EntityPlayer entityplayer;
        int j;
        int k;
        int l;

        for (i = 0; i < this.playerEntities.size(); ++i)
        {
            entityplayer = (EntityPlayer)this.playerEntities.get(i);
            j = MathHelper.floor_double(entityplayer.posX / 16.0D);
            k = MathHelper.floor_double(entityplayer.posZ / 16.0D);
            l = this.func_152379_p();

            for (int i1 = -l; i1 <= l; ++i1)
            {
                for (int j1 = -l; j1 <= l; ++j1)
                {
                    this.activeChunkSet.add(new ChunkCoordIntPair(i1 + j, j1 + k));
                }
            }
        }

        this.theProfiler.endSection();

        if (this.ambientTickCountdown > 0)
        {
            --this.ambientTickCountdown;
        }

        this.theProfiler.startSection("playerCheckLight");

        if (!this.playerEntities.isEmpty())
        {
            i = this.rand.nextInt(this.playerEntities.size());
            entityplayer = (EntityPlayer)this.playerEntities.get(i);
            j = MathHelper.floor_double(entityplayer.posX) + this.rand.nextInt(11) - 5;
            k = MathHelper.floor_double(entityplayer.posY) + this.rand.nextInt(11) - 5;
            l = MathHelper.floor_double(entityplayer.posZ) + this.rand.nextInt(11) - 5;
            this.func_147451_t(j, k, l);
        }

        this.theProfiler.endSection();
    }

    protected abstract int func_152379_p();

    protected void func_147467_a(int p_147467_1_, int p_147467_2_, Chunk p_147467_3_)
    {
        this.theProfiler.endStartSection("moodSound");

        if (this.ambientTickCountdown == 0 && !this.isRemote)
        {
            this.updateLCG = this.updateLCG * 3 + 1013904223;
            int k = this.updateLCG >> 2;
            int l = k & 15;
            int i1 = k >> 8 & 15;
            int j1 = k >> 16 & 255;
            Block block = p_147467_3_.getBlock(l, j1, i1);
            l += p_147467_1_;
            i1 += p_147467_2_;

            if (block.getMaterial() == Material.air && this.getFullBlockLightValue(l, j1, i1) <= this.rand.nextInt(8) && this.getSavedLightValue(EnumSkyBlock.Sky, l, j1, i1) <= 0)
            {
                EntityPlayer entityplayer = this.getClosestPlayer((double)l + 0.5D, (double)j1 + 0.5D, (double)i1 + 0.5D, 8.0D);

                if (entityplayer != null && entityplayer.getDistanceSq((double)l + 0.5D, (double)j1 + 0.5D, (double)i1 + 0.5D) > 4.0D)
                {
                    this.playSoundEffect((double)l + 0.5D, (double)j1 + 0.5D, (double)i1 + 0.5D, "ambient.cave.cave", 0.7F, 0.8F + this.rand.nextFloat() * 0.2F);
                    this.ambientTickCountdown = this.rand.nextInt(12000) + 6000;
                }
            }
        }

        this.theProfiler.endStartSection("checkLight");
        p_147467_3_.enqueueRelightChecks();
    }

    protected void func_147456_g()
    {
        this.setActivePlayerChunksAndCheckLight();
    }

    /**
     * checks to see if a given block is both water and is cold enough to freeze
     */
    public boolean isBlockFreezable(int p_72884_1_, int p_72884_2_, int p_72884_3_)
    {
        return this.canBlockFreeze(p_72884_1_, p_72884_2_, p_72884_3_, false);
    }

    /**
     * checks to see if a given block is both water and has at least one immediately adjacent non-water block
     */
    public boolean isBlockFreezableNaturally(int p_72850_1_, int p_72850_2_, int p_72850_3_)
    {
        return this.canBlockFreeze(p_72850_1_, p_72850_2_, p_72850_3_, true);
    }

    /**
     * checks to see if a given block is both water, and cold enough to freeze - if the par4 boolean is set, this will
     * only return true if there is a non-water block immediately adjacent to the specified block
     */
    public boolean canBlockFreeze(int p_72834_1_, int p_72834_2_, int p_72834_3_, boolean p_72834_4_)
    {
        return provider.canBlockFreeze(p_72834_1_, p_72834_2_, p_72834_3_, p_72834_4_);
    }

    public boolean canBlockFreezeBody(int p_72834_1_, int p_72834_2_, int p_72834_3_, boolean p_72834_4_)
    {
        BiomeGenBase biomegenbase = this.getBiomeGenForCoords(p_72834_1_, p_72834_3_);
        float f = biomegenbase.getFloatTemperature(p_72834_1_, p_72834_2_, p_72834_3_);

        if (f > 0.15F)
        {
            return false;
        }
        else
        {
            if (p_72834_2_ >= 0 && p_72834_2_ < 256 && this.getSavedLightValue(EnumSkyBlock.Block, p_72834_1_, p_72834_2_, p_72834_3_) < 10)
            {
                Block block = this.getBlock(p_72834_1_, p_72834_2_, p_72834_3_);

                if ((block == Blocks.water || block == Blocks.flowing_water) && this.getBlockMetadata(p_72834_1_, p_72834_2_, p_72834_3_) == 0)
                {
                    if (!p_72834_4_)
                    {
                        return true;
                    }

                    boolean flag1 = true;

                    if (flag1 && this.getBlock(p_72834_1_ - 1, p_72834_2_, p_72834_3_).getMaterial() != Material.water)
                    {
                        flag1 = false;
                    }

                    if (flag1 && this.getBlock(p_72834_1_ + 1, p_72834_2_, p_72834_3_).getMaterial() != Material.water)
                    {
                        flag1 = false;
                    }

                    if (flag1 && this.getBlock(p_72834_1_, p_72834_2_, p_72834_3_ - 1).getMaterial() != Material.water)
                    {
                        flag1 = false;
                    }

                    if (flag1 && this.getBlock(p_72834_1_, p_72834_2_, p_72834_3_ + 1).getMaterial() != Material.water)
                    {
                        flag1 = false;
                    }

                    if (!flag1)
                    {
                        return true;
                    }
                }
            }

            return false;
        }
    }

    public boolean func_147478_e(int p_147478_1_, int p_147478_2_, int p_147478_3_, boolean p_147478_4_)
    {
        return provider.canSnowAt(p_147478_1_, p_147478_2_, p_147478_3_, p_147478_4_);
    }

    public boolean canSnowAtBody(int p_147478_1_, int p_147478_2_, int p_147478_3_, boolean p_147478_4_)
    {
        BiomeGenBase biomegenbase = this.getBiomeGenForCoords(p_147478_1_, p_147478_3_);
        float f = biomegenbase.getFloatTemperature(p_147478_1_, p_147478_2_, p_147478_3_);

        if (f > 0.15F)
        {
            return false;
        }
        else if (!p_147478_4_)
        {
            return true;
        }
        else
        {
            if (p_147478_2_ >= 0 && p_147478_2_ < 256 && this.getSavedLightValue(EnumSkyBlock.Block, p_147478_1_, p_147478_2_, p_147478_3_) < 10)
            {
                Block block = this.getBlock(p_147478_1_, p_147478_2_, p_147478_3_);

                if (block.getMaterial() == Material.air && Blocks.snow_layer.canPlaceBlockAt(this, p_147478_1_, p_147478_2_, p_147478_3_))
                {
                    return true;
                }
            }

            return false;
        }
    }

    public boolean func_147451_t(int p_147451_1_, int p_147451_2_, int p_147451_3_)
    {
        boolean flag = false;

        if (!this.provider.hasNoSky)
        {
            flag |= this.updateLightByType(EnumSkyBlock.Sky, p_147451_1_, p_147451_2_, p_147451_3_);
        }

        flag |= this.updateLightByType(EnumSkyBlock.Block, p_147451_1_, p_147451_2_, p_147451_3_);
        return flag;
    }

    private int computeLightValue(int p_98179_1_, int p_98179_2_, int p_98179_3_, EnumSkyBlock p_98179_4_)
    {
        if (p_98179_4_ == EnumSkyBlock.Sky && this.canBlockSeeTheSky(p_98179_1_, p_98179_2_, p_98179_3_))
        {
            return 15;
        }
        else
        {
            Block block = this.getBlock(p_98179_1_, p_98179_2_, p_98179_3_);
            int blockLight = block.getLightValue(this, p_98179_1_, p_98179_2_, p_98179_3_);
            int l = p_98179_4_ == EnumSkyBlock.Sky ? 0 : blockLight;
            int i1 = block.getLightOpacity(this, p_98179_1_, p_98179_2_, p_98179_3_);

            if (i1 >= 15 && blockLight > 0)
            {
                i1 = 1;
            }

            if (i1 < 1)
            {
                i1 = 1;
            }

            if (i1 >= 15)
            {
                return 0;
            }
            else if (l >= 14)
            {
                return l;
            }
            else
            {
                for (int j1 = 0; j1 < 6; ++j1)
                {
                    int k1 = p_98179_1_ + Facing.offsetsXForSide[j1];
                    int l1 = p_98179_2_ + Facing.offsetsYForSide[j1];
                    int i2 = p_98179_3_ + Facing.offsetsZForSide[j1];
                    int j2 = this.getSavedLightValue(p_98179_4_, k1, l1, i2) - i1;

                    if (j2 > l)
                    {
                        l = j2;
                    }

                    if (l >= 14)
                    {
                        return l;
                    }
                }

                return l;
            }
        }
    }

    public boolean updateLightByType(EnumSkyBlock p_147463_1_, int p_147463_2_, int p_147463_3_, int p_147463_4_)
    {
        if (!this.doChunksNearChunkExist(p_147463_2_, p_147463_3_, p_147463_4_, 17))
        {
            return false;
        }
        else
        {
            int l = 0;
            int i1 = 0;
            this.theProfiler.startSection("getBrightness");
            int j1 = this.getSavedLightValue(p_147463_1_, p_147463_2_, p_147463_3_, p_147463_4_);
            int k1 = this.computeLightValue(p_147463_2_, p_147463_3_, p_147463_4_, p_147463_1_);
            int l1;
            int i2;
            int j2;
            int k2;
            int l2;
            int i3;
            int j3;
            int k3;
            int l3;

            if (k1 > j1)
            {
                this.lightUpdateBlockList[i1++] = 133152;
            }
            else if (k1 < j1)
            {
                this.lightUpdateBlockList[i1++] = 133152 | j1 << 18;

                while (l < i1)
                {
                    l1 = this.lightUpdateBlockList[l++];
                    i2 = (l1 & 63) - 32 + p_147463_2_;
                    j2 = (l1 >> 6 & 63) - 32 + p_147463_3_;
                    k2 = (l1 >> 12 & 63) - 32 + p_147463_4_;
                    l2 = l1 >> 18 & 15;
                    i3 = this.getSavedLightValue(p_147463_1_, i2, j2, k2);

                    if (i3 == l2)
                    {
                        this.setLightValue(p_147463_1_, i2, j2, k2, 0);

                        if (l2 > 0)
                        {
                            j3 = MathHelper.abs_int(i2 - p_147463_2_);
                            k3 = MathHelper.abs_int(j2 - p_147463_3_);
                            l3 = MathHelper.abs_int(k2 - p_147463_4_);

                            if (j3 + k3 + l3 < 17)
                            {
                                for (int i4 = 0; i4 < 6; ++i4)
                                {
                                    int j4 = i2 + Facing.offsetsXForSide[i4];
                                    int k4 = j2 + Facing.offsetsYForSide[i4];
                                    int l4 = k2 + Facing.offsetsZForSide[i4];
                                    int i5 = Math.max(1, this.getBlock(j4, k4, l4).getLightOpacity(this, j4, k4, l4));
                                    i3 = this.getSavedLightValue(p_147463_1_, j4, k4, l4);

                                    if (i3 == l2 - i5 && i1 < this.lightUpdateBlockList.length)
                                    {
                                        this.lightUpdateBlockList[i1++] = j4 - p_147463_2_ + 32 | k4 - p_147463_3_ + 32 << 6 | l4 - p_147463_4_ + 32 << 12 | l2 - i5 << 18;
                                    }
                                }
                            }
                        }
                    }
                }

                l = 0;
            }

            this.theProfiler.endSection();
            this.theProfiler.startSection("checkedPosition < toCheckCount");

            while (l < i1)
            {
                l1 = this.lightUpdateBlockList[l++];
                i2 = (l1 & 63) - 32 + p_147463_2_;
                j2 = (l1 >> 6 & 63) - 32 + p_147463_3_;
                k2 = (l1 >> 12 & 63) - 32 + p_147463_4_;
                l2 = this.getSavedLightValue(p_147463_1_, i2, j2, k2);
                i3 = this.computeLightValue(i2, j2, k2, p_147463_1_);

                if (i3 != l2)
                {
                    this.setLightValue(p_147463_1_, i2, j2, k2, i3);

                    if (i3 > l2)
                    {
                        j3 = Math.abs(i2 - p_147463_2_);
                        k3 = Math.abs(j2 - p_147463_3_);
                        l3 = Math.abs(k2 - p_147463_4_);
                        boolean flag = i1 < this.lightUpdateBlockList.length - 6;

                        if (j3 + k3 + l3 < 17 && flag)
                        {
                            if (this.getSavedLightValue(p_147463_1_, i2 - 1, j2, k2) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 - 1 - p_147463_2_ + 32 + (j2 - p_147463_3_ + 32 << 6) + (k2 - p_147463_4_ + 32 << 12);
                            }

                            if (this.getSavedLightValue(p_147463_1_, i2 + 1, j2, k2) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 + 1 - p_147463_2_ + 32 + (j2 - p_147463_3_ + 32 << 6) + (k2 - p_147463_4_ + 32 << 12);
                            }

                            if (this.getSavedLightValue(p_147463_1_, i2, j2 - 1, k2) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 - p_147463_2_ + 32 + (j2 - 1 - p_147463_3_ + 32 << 6) + (k2 - p_147463_4_ + 32 << 12);
                            }

                            if (this.getSavedLightValue(p_147463_1_, i2, j2 + 1, k2) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 - p_147463_2_ + 32 + (j2 + 1 - p_147463_3_ + 32 << 6) + (k2 - p_147463_4_ + 32 << 12);
                            }

                            if (this.getSavedLightValue(p_147463_1_, i2, j2, k2 - 1) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 - p_147463_2_ + 32 + (j2 - p_147463_3_ + 32 << 6) + (k2 - 1 - p_147463_4_ + 32 << 12);
                            }

                            if (this.getSavedLightValue(p_147463_1_, i2, j2, k2 + 1) < i3)
                            {
                                this.lightUpdateBlockList[i1++] = i2 - p_147463_2_ + 32 + (j2 - p_147463_3_ + 32 << 6) + (k2 + 1 - p_147463_4_ + 32 << 12);
                            }
                        }
                    }
                }
            }

            this.theProfiler.endSection();
            return true;
        }
    }

    /**
     * Runs through the list of updates to run and ticks them
     */
    public boolean tickUpdates(boolean p_72955_1_)
    {
        return false;
    }

    public List getPendingBlockUpdates(Chunk p_72920_1_, boolean p_72920_2_)
    {
        return null;
    }

    /**
     * Will get all entities within the specified AABB excluding the one passed into it. Args: entityToExclude, aabb
     */
    public List getEntitiesWithinAABBExcludingEntity(Entity p_72839_1_, AxisAlignedBB p_72839_2_)
    {
        return this.getEntitiesWithinAABBExcludingEntity(p_72839_1_, p_72839_2_, (IEntitySelector)null);
    }

    public List getEntitiesWithinAABBExcludingEntity(Entity p_94576_1_, AxisAlignedBB p_94576_2_, IEntitySelector p_94576_3_)
    {
        ArrayList arraylist = new ArrayList();
        int i = MathHelper.floor_double((p_94576_2_.minX - MAX_ENTITY_RADIUS) / 16.0D);
        int j = MathHelper.floor_double((p_94576_2_.maxX + MAX_ENTITY_RADIUS) / 16.0D);
        int k = MathHelper.floor_double((p_94576_2_.minZ - MAX_ENTITY_RADIUS) / 16.0D);
        int l = MathHelper.floor_double((p_94576_2_.maxZ + MAX_ENTITY_RADIUS) / 16.0D);

        for (int i1 = i; i1 <= j; ++i1)
        {
            for (int j1 = k; j1 <= l; ++j1)
            {
                if (this.chunkExists(i1, j1))
                {
                    this.getChunkFromChunkCoords(i1, j1).getEntitiesWithinAABBForEntity(p_94576_1_, p_94576_2_, arraylist, p_94576_3_);
                }
            }
        }

        return arraylist;
    }

    /**
     * Returns all entities of the specified class type which intersect with the AABB. Args: entityClass, aabb
     */
    public List getEntitiesWithinAABB(Class p_72872_1_, AxisAlignedBB p_72872_2_)
    {
        return this.selectEntitiesWithinAABB(p_72872_1_, p_72872_2_, (IEntitySelector)null);
    }

    public List selectEntitiesWithinAABB(Class p_82733_1_, AxisAlignedBB p_82733_2_, IEntitySelector p_82733_3_)
    {
        int i = MathHelper.floor_double((p_82733_2_.minX - MAX_ENTITY_RADIUS) / 16.0D);
        int j = MathHelper.floor_double((p_82733_2_.maxX + MAX_ENTITY_RADIUS) / 16.0D);
        int k = MathHelper.floor_double((p_82733_2_.minZ - MAX_ENTITY_RADIUS) / 16.0D);
        int l = MathHelper.floor_double((p_82733_2_.maxZ + MAX_ENTITY_RADIUS) / 16.0D);
        ArrayList arraylist = new ArrayList();

        for (int i1 = i; i1 <= j; ++i1)
        {
            for (int j1 = k; j1 <= l; ++j1)
            {
                if (this.chunkExists(i1, j1))
                {
                    this.getChunkFromChunkCoords(i1, j1).getEntitiesOfTypeWithinAAAB(p_82733_1_, p_82733_2_, arraylist, p_82733_3_);
                }
            }
        }

        return arraylist;
    }

    public Entity findNearestEntityWithinAABB(Class p_72857_1_, AxisAlignedBB p_72857_2_, Entity p_72857_3_)
    {
        List list = this.getEntitiesWithinAABB(p_72857_1_, p_72857_2_);
        Entity entity1 = null;
        double d0 = Double.MAX_VALUE;

        for (int i = 0; i < list.size(); ++i)
        {
            Entity entity2 = (Entity)list.get(i);

            if (entity2 != p_72857_3_)
            {
                double d1 = p_72857_3_.getDistanceSqToEntity(entity2);

                if (d1 <= d0)
                {
                    entity1 = entity2;
                    d0 = d1;
                }
            }
        }

        return entity1;
    }

    /**
     * Returns the Entity with the given ID, or null if it doesn't exist in this World.
     */
    public abstract Entity getEntityByID(int p_73045_1_);

    /**
     * Accessor for world Loaded Entity List
     */
    @SideOnly(Side.CLIENT)
    public List getLoadedEntityList()
    {
        return this.loadedEntityList;
    }

    /**
     * Args: X, Y, Z, tile entity Marks the chunk the tile entity is in as modified. This is essential as chunks that
     * are not marked as modified may be rolled back when exiting the game.
     */
    public void markTileEntityChunkModified(int p_147476_1_, int p_147476_2_, int p_147476_3_, TileEntity p_147476_4_)
    {
        if (this.blockExists(p_147476_1_, p_147476_2_, p_147476_3_))
        {
            this.getChunkFromBlockCoords(p_147476_1_, p_147476_3_).setChunkModified();
        }
    }

    /**
     * Counts how many entities of an entity class exist in the world. Args: entityClass
     */
    public int countEntities(Class p_72907_1_)
    {
        int i = 0;

        for (int j = 0; j < this.loadedEntityList.size(); ++j)
        {
            Entity entity = (Entity)this.loadedEntityList.get(j);

            if ((!(entity instanceof EntityLiving) || !((EntityLiving)entity).isNoDespawnRequired()) && p_72907_1_.isAssignableFrom(entity.getClass()))
            {
                ++i;
            }
        }

        return i;
    }

    /**
     * adds entities to the loaded entities list, and loads thier skins.
     */
    public void addLoadedEntities(List p_72868_1_)
    {
        for (int i = 0; i < p_72868_1_.size(); ++i)
        {
            Entity entity = (Entity)p_72868_1_.get(i);
            if (!MinecraftForge.EVENT_BUS.post(new EntityJoinWorldEvent(entity, this)))
            {
                loadedEntityList.add(entity);
                this.onEntityAdded(entity);
            }
        }
    }

    /**
     * Adds a list of entities to be unloaded on the next pass of World.updateEntities()
     */
    public void unloadEntities(List p_72828_1_)
    {
        this.unloadedEntityList.addAll(p_72828_1_);
    }

    /**
     * Returns true if the given Entity can be placed on the given side of the given block position.
     */
    public boolean canPlaceEntityOnSide(Block p_147472_1_, int p_147472_2_, int p_147472_3_, int p_147472_4_, boolean p_147472_5_, int p_147472_6_, Entity p_147472_7_, ItemStack p_147472_8_)
    {
        Block block1 = this.getBlock(p_147472_2_, p_147472_3_, p_147472_4_);
        AxisAlignedBB axisalignedbb = p_147472_5_ ? null : p_147472_1_.getCollisionBoundingBoxFromPool(this, p_147472_2_, p_147472_3_, p_147472_4_);
        return axisalignedbb != null && !this.checkNoEntityCollision(axisalignedbb, p_147472_7_) ? false : (block1.getMaterial() == Material.circuits && p_147472_1_ == Blocks.anvil ? true : block1.isReplaceable(this, p_147472_2_, p_147472_3_, p_147472_4_) && p_147472_1_.canReplace(this, p_147472_2_, p_147472_3_, p_147472_4_, p_147472_6_, p_147472_8_));
    }

    public PathEntity getPathEntityToEntity(Entity p_72865_1_, Entity p_72865_2_, float p_72865_3_, boolean p_72865_4_, boolean p_72865_5_, boolean p_72865_6_, boolean p_72865_7_)
    {
        this.theProfiler.startSection("pathfind");
        int i = MathHelper.floor_double(p_72865_1_.posX);
        int j = MathHelper.floor_double(p_72865_1_.posY + 1.0D);
        int k = MathHelper.floor_double(p_72865_1_.posZ);
        int l = (int)(p_72865_3_ + 16.0F);
        int i1 = i - l;
        int j1 = j - l;
        int k1 = k - l;
        int l1 = i + l;
        int i2 = j + l;
        int j2 = k + l;
        ChunkCache chunkcache = new ChunkCache(this, i1, j1, k1, l1, i2, j2, 0);
        PathEntity pathentity = (new PathFinder(chunkcache, p_72865_4_, p_72865_5_, p_72865_6_, p_72865_7_)).createEntityPathTo(p_72865_1_, p_72865_2_, p_72865_3_);
        this.theProfiler.endSection();
        return pathentity;
    }

    public PathEntity getEntityPathToXYZ(Entity p_72844_1_, int p_72844_2_, int p_72844_3_, int p_72844_4_, float p_72844_5_, boolean p_72844_6_, boolean p_72844_7_, boolean p_72844_8_, boolean p_72844_9_)
    {
        this.theProfiler.startSection("pathfind");
        int l = MathHelper.floor_double(p_72844_1_.posX);
        int i1 = MathHelper.floor_double(p_72844_1_.posY);
        int j1 = MathHelper.floor_double(p_72844_1_.posZ);
        int k1 = (int)(p_72844_5_ + 8.0F);
        int l1 = l - k1;
        int i2 = i1 - k1;
        int j2 = j1 - k1;
        int k2 = l + k1;
        int l2 = i1 + k1;
        int i3 = j1 + k1;
        ChunkCache chunkcache = new ChunkCache(this, l1, i2, j2, k2, l2, i3, 0);
        PathEntity pathentity = (new PathFinder(chunkcache, p_72844_6_, p_72844_7_, p_72844_8_, p_72844_9_)).createEntityPathTo(p_72844_1_, p_72844_2_, p_72844_3_, p_72844_4_, p_72844_5_);
        this.theProfiler.endSection();
        return pathentity;
    }

    /**
     * Is this block powering in the specified direction Args: x, y, z, direction
     */
    public int isBlockProvidingPowerTo(int p_72879_1_, int p_72879_2_, int p_72879_3_, int p_72879_4_)
    {
        return this.getBlock(p_72879_1_, p_72879_2_, p_72879_3_).isProvidingStrongPower(this, p_72879_1_, p_72879_2_, p_72879_3_, p_72879_4_);
    }

    /**
     * Returns the highest redstone signal strength powering the given block. Args: X, Y, Z.
     */
    public int getBlockPowerInput(int p_94577_1_, int p_94577_2_, int p_94577_3_)
    {
        byte b0 = 0;
        int l = Math.max(b0, this.isBlockProvidingPowerTo(p_94577_1_, p_94577_2_ - 1, p_94577_3_, 0));

        if (l >= 15)
        {
            return l;
        }
        else
        {
            l = Math.max(l, this.isBlockProvidingPowerTo(p_94577_1_, p_94577_2_ + 1, p_94577_3_, 1));

            if (l >= 15)
            {
                return l;
            }
            else
            {
                l = Math.max(l, this.isBlockProvidingPowerTo(p_94577_1_, p_94577_2_, p_94577_3_ - 1, 2));

                if (l >= 15)
                {
                    return l;
                }
                else
                {
                    l = Math.max(l, this.isBlockProvidingPowerTo(p_94577_1_, p_94577_2_, p_94577_3_ + 1, 3));

                    if (l >= 15)
                    {
                        return l;
                    }
                    else
                    {
                        l = Math.max(l, this.isBlockProvidingPowerTo(p_94577_1_ - 1, p_94577_2_, p_94577_3_, 4));

                        if (l >= 15)
                        {
                            return l;
                        }
                        else
                        {
                            l = Math.max(l, this.isBlockProvidingPowerTo(p_94577_1_ + 1, p_94577_2_, p_94577_3_, 5));
                            return l >= 15 ? l : l;
                        }
                    }
                }
            }
        }
    }

    /**
     * Returns the indirect signal strength being outputted by the given block in the *opposite* of the given direction.
     * Args: X, Y, Z, direction
     */
    public boolean getIndirectPowerOutput(int p_94574_1_, int p_94574_2_, int p_94574_3_, int p_94574_4_)
    {
        return this.getIndirectPowerLevelTo(p_94574_1_, p_94574_2_, p_94574_3_, p_94574_4_) > 0;
    }

    /**
     * Gets the power level from a certain block face.  Args: x, y, z, direction
     */
    public int getIndirectPowerLevelTo(int p_72878_1_, int p_72878_2_, int p_72878_3_, int p_72878_4_)
    {
        Block block = this.getBlock(p_72878_1_, p_72878_2_, p_72878_3_);
        return block.shouldCheckWeakPower(this, p_72878_1_, p_72878_2_, p_72878_3_, p_72878_4_) ? this.getBlockPowerInput(p_72878_1_, p_72878_2_, p_72878_3_) : block.isProvidingWeakPower(this, p_72878_1_, p_72878_2_, p_72878_3_, p_72878_4_);
    }

    /**
     * Used to see if one of the blocks next to you or your block is getting power from a neighboring block. Used by
     * items like TNT or Doors so they don't have redstone going straight into them.  Args: x, y, z
     */
    public boolean isBlockIndirectlyGettingPowered(int p_72864_1_, int p_72864_2_, int p_72864_3_)
    {
        return this.getIndirectPowerLevelTo(p_72864_1_, p_72864_2_ - 1, p_72864_3_, 0) > 0 ? true : (this.getIndirectPowerLevelTo(p_72864_1_, p_72864_2_ + 1, p_72864_3_, 1) > 0 ? true : (this.getIndirectPowerLevelTo(p_72864_1_, p_72864_2_, p_72864_3_ - 1, 2) > 0 ? true : (this.getIndirectPowerLevelTo(p_72864_1_, p_72864_2_, p_72864_3_ + 1, 3) > 0 ? true : (this.getIndirectPowerLevelTo(p_72864_1_ - 1, p_72864_2_, p_72864_3_, 4) > 0 ? true : this.getIndirectPowerLevelTo(p_72864_1_ + 1, p_72864_2_, p_72864_3_, 5) > 0))));
    }

    public int getStrongestIndirectPower(int p_94572_1_, int p_94572_2_, int p_94572_3_)
    {
        int l = 0;

        for (int i1 = 0; i1 < 6; ++i1)
        {
            int j1 = this.getIndirectPowerLevelTo(p_94572_1_ + Facing.offsetsXForSide[i1], p_94572_2_ + Facing.offsetsYForSide[i1], p_94572_3_ + Facing.offsetsZForSide[i1], i1);

            if (j1 >= 15)
            {
                return 15;
            }

            if (j1 > l)
            {
                l = j1;
            }
        }

        return l;
    }

    /**
     * Gets the closest player to the entity within the specified distance (if distance is less than 0 then ignored).
     * Args: entity, dist
     */
    public EntityPlayer getClosestPlayerToEntity(Entity p_72890_1_, double p_72890_2_)
    {
        return this.getClosestPlayer(p_72890_1_.posX, p_72890_1_.posY, p_72890_1_.posZ, p_72890_2_);
    }

    /**
     * Gets the closest player to the point within the specified distance (distance can be set to less than 0 to not
     * limit the distance). Args: x, y, z, dist
     */
    public EntityPlayer getClosestPlayer(double p_72977_1_, double p_72977_3_, double p_72977_5_, double p_72977_7_)
    {
        double d4 = -1.0D;
        EntityPlayer entityplayer = null;

        for (int i = 0; i < this.playerEntities.size(); ++i)
        {
            EntityPlayer entityplayer1 = (EntityPlayer)this.playerEntities.get(i);
            double d5 = entityplayer1.getDistanceSq(p_72977_1_, p_72977_3_, p_72977_5_);

            if ((p_72977_7_ < 0.0D || d5 < p_72977_7_ * p_72977_7_) && (d4 == -1.0D || d5 < d4))
            {
                d4 = d5;
                entityplayer = entityplayer1;
            }
        }

        return entityplayer;
    }

    /**
     * Returns the closest vulnerable player to this entity within the given radius, or null if none is found
     */
    public EntityPlayer getClosestVulnerablePlayerToEntity(Entity p_72856_1_, double p_72856_2_)
    {
        return this.getClosestVulnerablePlayer(p_72856_1_.posX, p_72856_1_.posY, p_72856_1_.posZ, p_72856_2_);
    }

    /**
     * Returns the closest vulnerable player within the given radius, or null if none is found.
     */
    public EntityPlayer getClosestVulnerablePlayer(double p_72846_1_, double p_72846_3_, double p_72846_5_, double p_72846_7_)
    {
        double d4 = -1.0D;
        EntityPlayer entityplayer = null;

        for (int i = 0; i < this.playerEntities.size(); ++i)
        {
            EntityPlayer entityplayer1 = (EntityPlayer)this.playerEntities.get(i);

            if (!entityplayer1.capabilities.disableDamage && entityplayer1.isEntityAlive())
            {
                double d5 = entityplayer1.getDistanceSq(p_72846_1_, p_72846_3_, p_72846_5_);
                double d6 = p_72846_7_;

                if (entityplayer1.isSneaking())
                {
                    d6 = p_72846_7_ * 0.800000011920929D;
                }

                if (entityplayer1.isInvisible())
                {
                    float f = entityplayer1.getArmorVisibility();

                    if (f < 0.1F)
                    {
                        f = 0.1F;
                    }

                    d6 *= (double)(0.7F * f);
                }

                if ((p_72846_7_ < 0.0D || d5 < d6 * d6) && (d4 == -1.0D || d5 < d4))
                {
                    d4 = d5;
                    entityplayer = entityplayer1;
                }
            }
        }

        return entityplayer;
    }

    /**
     * Find a player by name in this world.
     */
    public EntityPlayer getPlayerEntityByName(String p_72924_1_)
    {
        for (int i = 0; i < this.playerEntities.size(); ++i)
        {
            EntityPlayer entityplayer = (EntityPlayer)this.playerEntities.get(i);

            if (p_72924_1_.equals(entityplayer.getCommandSenderName()))
            {
                return entityplayer;
            }
        }

        return null;
    }

    public EntityPlayer func_152378_a(UUID p_152378_1_)
    {
        for (int i = 0; i < this.playerEntities.size(); ++i)
        {
            EntityPlayer entityplayer = (EntityPlayer)this.playerEntities.get(i);

            if (p_152378_1_.equals(entityplayer.getUniqueID()))
            {
                return entityplayer;
            }
        }

        return null;
    }

    /**
     * If on MP, sends a quitting packet.
     */
    @SideOnly(Side.CLIENT)
    public void sendQuittingDisconnectingPacket() {}

    /**
     * Checks whether the session lock file was modified by another process
     */
    public void checkSessionLock() throws MinecraftException
    {
        this.saveHandler.checkSessionLock();
    }

    @SideOnly(Side.CLIENT)
    public void func_82738_a(long p_82738_1_)
    {
        this.worldInfo.incrementTotalWorldTime(p_82738_1_);
    }

    /**
     * Retrieve the world seed from level.dat
     */
    public long getSeed()
    {
        return provider.getSeed();
    }

    public long getTotalWorldTime()
    {
        return this.worldInfo.getWorldTotalTime();
    }

    public long getWorldTime()
    {
        return provider.getWorldTime();
    }

    /**
     * Sets the world time.
     */
    public void setWorldTime(long p_72877_1_)
    {
        provider.setWorldTime(p_72877_1_);
    }

    /**
     * Returns the coordinates of the spawn point
     */
    public ChunkCoordinates getSpawnPoint()
    {
        return provider.getSpawnPoint();
    }

    public void setSpawnLocation(int p_72950_1_, int p_72950_2_, int p_72950_3_)
    {
        provider.setSpawnPoint(p_72950_1_, p_72950_2_, p_72950_3_);
    }

    /**
     * spwans an entity and loads surrounding chunks
     */
    @SideOnly(Side.CLIENT)
    public void joinEntityInSurroundings(Entity p_72897_1_)
    {
        int i = MathHelper.floor_double(p_72897_1_.posX / 16.0D);
        int j = MathHelper.floor_double(p_72897_1_.posZ / 16.0D);
        byte b0 = 2;

        for (int k = i - b0; k <= i + b0; ++k)
        {
            for (int l = j - b0; l <= j + b0; ++l)
            {
                this.getChunkFromChunkCoords(k, l);
            }
        }

        if (!this.loadedEntityList.contains(p_72897_1_))
        {
            if (!MinecraftForge.EVENT_BUS.post(new EntityJoinWorldEvent(p_72897_1_, this)))
            {
                this.loadedEntityList.add(p_72897_1_);
            }
        }
    }

    /**
     * Called when checking if a certain block can be mined or not. The 'spawn safe zone' check is located here.
     */
    public boolean canMineBlock(EntityPlayer p_72962_1_, int p_72962_2_, int p_72962_3_, int p_72962_4_)
    {
        return provider.canMineBlock(p_72962_1_, p_72962_2_, p_72962_3_, p_72962_4_);
    }

    public boolean canMineBlockBody(EntityPlayer par1EntityPlayer, int par2, int par3, int par4)
    {
        return true;
    }

    /**
     * sends a Packet 38 (Entity Status) to all tracked players of that entity
     */
    public void setEntityState(Entity p_72960_1_, byte p_72960_2_) {}

    /**
     * gets the IChunkProvider this world uses.
     */
    public IChunkProvider getChunkProvider()
    {
        return this.chunkProvider;
    }

    /**
     * Adds a block event with the given Args to the blockEventCache. During the next tick(), the block specified will
     * have its onBlockEvent handler called with the given parameters. Args: X,Y,Z, Block, EventID, EventParameter
     */
    public void addBlockEvent(int p_147452_1_, int p_147452_2_, int p_147452_3_, Block p_147452_4_, int p_147452_5_, int p_147452_6_)
    {
        p_147452_4_.onBlockEventReceived(this, p_147452_1_, p_147452_2_, p_147452_3_, p_147452_5_, p_147452_6_);
    }

    /**
     * Returns this world's current save handler
     */
    public ISaveHandler getSaveHandler()
    {
        return this.saveHandler;
    }

    /**
     * Gets the World's WorldInfo instance
     */
    public WorldInfo getWorldInfo()
    {
        return this.worldInfo;
    }

    /**
     * Gets the GameRules instance.
     */
    public GameRules getGameRules()
    {
        return this.worldInfo.getGameRulesInstance();
    }

    /**
     * Updates the flag that indicates whether or not all players in the world are sleeping.
     */
    public void updateAllPlayersSleepingFlag() {}

    public float getWeightedThunderStrength(float p_72819_1_)
    {
        return (this.prevThunderingStrength + (this.thunderingStrength - this.prevThunderingStrength) * p_72819_1_) * this.getRainStrength(p_72819_1_);
    }

    /**
     * Sets the strength of the thunder.
     */
    @SideOnly(Side.CLIENT)
    public void setThunderStrength(float p_147442_1_)
    {
        this.prevThunderingStrength = p_147442_1_;
        this.thunderingStrength = p_147442_1_;
    }

    /**
     * Not sure about this actually. Reverting this one myself.
     */
    public float getRainStrength(float p_72867_1_)
    {
        return this.prevRainingStrength + (this.rainingStrength - this.prevRainingStrength) * p_72867_1_;
    }

    /**
     * Sets the strength of the rain.
     */
    @SideOnly(Side.CLIENT)
    public void setRainStrength(float p_72894_1_)
    {
        this.prevRainingStrength = p_72894_1_;
        this.rainingStrength = p_72894_1_;
    }

    /**
     * Returns true if the current thunder strength (weighted with the rain strength) is greater than 0.9
     */
    public boolean isThundering()
    {
        return (double)this.getWeightedThunderStrength(1.0F) > 0.9D;
    }

    /**
     * Returns true if the current rain strength is greater than 0.2
     */
    public boolean isRaining()
    {
        return (double)this.getRainStrength(1.0F) > 0.2D;
    }

    public boolean canLightningStrikeAt(int p_72951_1_, int p_72951_2_, int p_72951_3_)
    {
        if (!this.isRaining())
        {
            return false;
        }
        else if (!this.canBlockSeeTheSky(p_72951_1_, p_72951_2_, p_72951_3_))
        {
            return false;
        }
        else if (this.getPrecipitationHeight(p_72951_1_, p_72951_3_) > p_72951_2_)
        {
            return false;
        }
        else
        {
            BiomeGenBase biomegenbase = this.getBiomeGenForCoords(p_72951_1_, p_72951_3_);
            return biomegenbase.getEnableSnow() ? false : (this.func_147478_e(p_72951_1_, p_72951_2_, p_72951_3_, false) ? false : biomegenbase.canSpawnLightningBolt());
        }
    }

    /**
     * Checks to see if the biome rainfall values for a given x,y,z coordinate set are extremely high
     */
    public boolean isBlockHighHumidity(int p_72958_1_, int p_72958_2_, int p_72958_3_)
    {
        return provider.isBlockHighHumidity(p_72958_1_, p_72958_2_, p_72958_3_);
    }

    /**
     * Assigns the given String id to the given MapDataBase using the MapStorage, removing any existing ones of the same
     * id.
     */
    public void setItemData(String p_72823_1_, WorldSavedData p_72823_2_)
    {
        this.mapStorage.setData(p_72823_1_, p_72823_2_);
    }

    /**
     * Loads an existing MapDataBase corresponding to the given String id from disk using the MapStorage, instantiating
     * the given Class, or returns null if none such file exists. args: Class to instantiate, String dataid
     */
    public WorldSavedData loadItemData(Class p_72943_1_, String p_72943_2_)
    {
        return this.mapStorage.loadData(p_72943_1_, p_72943_2_);
    }

    /**
     * Returns an unique new data id from the MapStorage for the given prefix and saves the idCounts map to the
     * 'idcounts' file.
     */
    public int getUniqueDataId(String p_72841_1_)
    {
        return this.mapStorage.getUniqueDataId(p_72841_1_);
    }

    public void playBroadcastSound(int p_82739_1_, int p_82739_2_, int p_82739_3_, int p_82739_4_, int p_82739_5_)
    {
        for (int j1 = 0; j1 < this.worldAccesses.size(); ++j1)
        {
            ((IWorldAccess)this.worldAccesses.get(j1)).broadcastSound(p_82739_1_, p_82739_2_, p_82739_3_, p_82739_4_, p_82739_5_);
        }
    }

    /**
     * See description for func_72706_a.
     */
    public void playAuxSFX(int p_72926_1_, int p_72926_2_, int p_72926_3_, int p_72926_4_, int p_72926_5_)
    {
        this.playAuxSFXAtEntity((EntityPlayer)null, p_72926_1_, p_72926_2_, p_72926_3_, p_72926_4_, p_72926_5_);
    }

    /**
     * See description for playAuxSFX.
     */
    public void playAuxSFXAtEntity(EntityPlayer p_72889_1_, int p_72889_2_, int p_72889_3_, int p_72889_4_, int p_72889_5_, int p_72889_6_)
    {
        try
        {
            for (int j1 = 0; j1 < this.worldAccesses.size(); ++j1)
            {
                ((IWorldAccess)this.worldAccesses.get(j1)).playAuxSFX(p_72889_1_, p_72889_2_, p_72889_3_, p_72889_4_, p_72889_5_, p_72889_6_);
            }
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Playing level event");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Level event being played");
            crashreportcategory.addCrashSection("Block coordinates", CrashReportCategory.getLocationInfo(p_72889_3_, p_72889_4_, p_72889_5_));
            crashreportcategory.addCrashSection("Event source", p_72889_1_);
            crashreportcategory.addCrashSection("Event type", Integer.valueOf(p_72889_2_));
            crashreportcategory.addCrashSection("Event data", Integer.valueOf(p_72889_6_));
            throw new ReportedException(crashreport);
        }
    }

    /**
     * Returns current world height.
     */
    public int getHeight()
    {
        return provider.getHeight();
    }

    /**
     * Returns current world height.
     */
    public int getActualHeight()
    {
        return provider.getActualHeight();
    }

    /**
     * puts the World Random seed to a specific state dependant on the inputs
     */
    public Random setRandomSeed(int p_72843_1_, int p_72843_2_, int p_72843_3_)
    {
        long l = (long)p_72843_1_ * 341873128712L + (long)p_72843_2_ * 132897987541L + this.getWorldInfo().getSeed() + (long)p_72843_3_;
        this.rand.setSeed(l);
        return this.rand;
    }

    /**
     * Returns the location of the closest structure of the specified type. If not found returns null.
     */
    public ChunkPosition findClosestStructure(String p_147440_1_, int p_147440_2_, int p_147440_3_, int p_147440_4_)
    {
        return this.getChunkProvider().func_147416_a(this, p_147440_1_, p_147440_2_, p_147440_3_, p_147440_4_);
    }

    /**
     * set by !chunk.getAreLevelsEmpty
     */
    @SideOnly(Side.CLIENT)
    public boolean extendedLevelsInChunkCache()
    {
        return false;
    }

    /**
     * Returns horizon height for use in rendering the sky.
     */
    @SideOnly(Side.CLIENT)
    public double getHorizon()
    {
        return provider.getHorizon();
    }

    /**
     * Adds some basic stats of the world to the given crash report.
     */
    public CrashReportCategory addWorldInfoToCrashReport(CrashReport p_72914_1_)
    {
        CrashReportCategory crashreportcategory = p_72914_1_.makeCategoryDepth("Affected level", 1);
        crashreportcategory.addCrashSection("Level name", this.worldInfo == null ? "????" : this.worldInfo.getWorldName());
        crashreportcategory.addCrashSectionCallable("All players", new Callable()
        {
            private static final String __OBFID = "CL_00000143";
            public String call()
            {
                return World.this.playerEntities.size() + " total; " + World.this.playerEntities.toString();
            }
        });
        crashreportcategory.addCrashSectionCallable("Chunk stats", new Callable()
        {
            private static final String __OBFID = "CL_00000144";
            public String call()
            {
                return World.this.chunkProvider.makeString();
            }
        });

        try
        {
            this.worldInfo.addToCrashReport(crashreportcategory);
        }
        catch (Throwable throwable)
        {
            crashreportcategory.addCrashSectionThrowable("Level Data Unobtainable", throwable);
        }

        return crashreportcategory;
    }

    /**
     * Starts (or continues) destroying a block with given ID at the given coordinates for the given partially destroyed
     * value
     */
    public void destroyBlockInWorldPartially(int p_147443_1_, int p_147443_2_, int p_147443_3_, int p_147443_4_, int p_147443_5_)
    {
        for (int j1 = 0; j1 < this.worldAccesses.size(); ++j1)
        {
            IWorldAccess iworldaccess = (IWorldAccess)this.worldAccesses.get(j1);
            iworldaccess.destroyBlockPartially(p_147443_1_, p_147443_2_, p_147443_3_, p_147443_4_, p_147443_5_);
        }
    }

    /**
     * returns a calendar object containing the current date
     */
    public Calendar getCurrentDate()
    {
        if (this.getTotalWorldTime() % 600L == 0L)
        {
            this.theCalendar.setTimeInMillis(MinecraftServer.getSystemTimeMillis());
        }

        return this.theCalendar;
    }

    @SideOnly(Side.CLIENT)
    public void makeFireworks(double p_92088_1_, double p_92088_3_, double p_92088_5_, double p_92088_7_, double p_92088_9_, double p_92088_11_, NBTTagCompound p_92088_13_) {}

    public Scoreboard getScoreboard()
    {
        return this.worldScoreboard;
    }

    public void func_147453_f(int p_147453_1_, int p_147453_2_, int p_147453_3_, Block p_147453_4_)
    {
        for (ForgeDirection dir : ForgeDirection.VALID_DIRECTIONS)
        {
            int i1 = p_147453_1_ + dir.offsetX;
            int y  = p_147453_2_ + dir.offsetY;
            int j1 = p_147453_3_ + dir.offsetZ;
            Block block1 = this.getBlock(i1, y, j1);

            block1.onNeighborChange(this, i1, y, j1, p_147453_1_, p_147453_2_, p_147453_3_);
            if (block1.isNormalCube(this, i1, y, j1))
            {
                i1 += dir.offsetX;
                y  += dir.offsetY;
                j1 += dir.offsetZ;
                Block block2 = this.getBlock(i1, y, j1);

                if (block2.getWeakChanges(this, i1, y, j1))
                {
                    block2.onNeighborChange(this, i1, y, j1, p_147453_1_, p_147453_2_, p_147453_3_);
                }
            }
        }
    }

    public float func_147462_b(double p_147462_1_, double p_147462_3_, double p_147462_5_)
    {
        return this.func_147473_B(MathHelper.floor_double(p_147462_1_), MathHelper.floor_double(p_147462_3_), MathHelper.floor_double(p_147462_5_));
    }

    public float func_147473_B(int p_147473_1_, int p_147473_2_, int p_147473_3_)
    {
        float f = 0.0F;
        boolean flag = this.difficultySetting == EnumDifficulty.HARD;

        if (this.blockExists(p_147473_1_, p_147473_2_, p_147473_3_))
        {
            float f1 = this.getCurrentMoonPhaseFactor();
            f += MathHelper.clamp_float((float)this.getChunkFromBlockCoords(p_147473_1_, p_147473_3_).inhabitedTime / 3600000.0F, 0.0F, 1.0F) * (flag ? 1.0F : 0.75F);
            f += f1 * 0.25F;
        }

        if (this.difficultySetting == EnumDifficulty.EASY || this.difficultySetting == EnumDifficulty.PEACEFUL)
        {
            f *= (float)this.difficultySetting.getDifficultyId() / 2.0F;
        }

        return MathHelper.clamp_float(f, 0.0F, flag ? 1.5F : 1.0F);
    }

    public void func_147450_X()
    {
        Iterator iterator = this.worldAccesses.iterator();

        while (iterator.hasNext())
        {
            IWorldAccess iworldaccess = (IWorldAccess)iterator.next();
            iworldaccess.onStaticEntitiesChanged();
        }
    }


    /* ======================================== FORGE START =====================================*/
    /**
     * Adds a single TileEntity to the world.
     * @param entity The TileEntity to be added.
     */
    public void addTileEntity(TileEntity entity)
    {
        List dest = field_147481_N ? addedTileEntityList : loadedTileEntityList;
        if(entity.canUpdate())
        {
            dest.add(entity);
        }
    }

    /**
     * Determine if the given block is considered solid on the
     * specified side.  Used by placement logic.
     *
     * @param x Block X Position
     * @param y Block Y Position
     * @param z Block Z Position
     * @param side The Side in question
     * @return True if the side is solid
     */
    public boolean isSideSolid(int x, int y, int z, ForgeDirection side)
    {
        return isSideSolid(x, y, z, side, false);
    }

    /**
     * Determine if the given block is considered solid on the
     * specified side.  Used by placement logic.
     *
     * @param x Block X Position
     * @param y Block Y Position
     * @param z Block Z Position
     * @param side The Side in question
     * @param _default The default to return if the block doesn't exist.
     * @return True if the side is solid
     */
    @Override
    public boolean isSideSolid(int x, int y, int z, ForgeDirection side, boolean _default)
    {
        if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000)
        {
            return _default;
        }

        Chunk chunk = this.chunkProvider.provideChunk(x >> 4, z >> 4);
        if (chunk == null || chunk.isEmpty())
        {
            return _default;
        }
        return getBlock(x, y, z).isSideSolid(this, x, y, z, side);
    }

    /**
     * Get the persistent chunks for this world
     *
     * @return
     */
    public ImmutableSetMultimap<ChunkCoordIntPair, Ticket> getPersistentChunks()
    {
        return ForgeChunkManager.getPersistentChunksFor(this);
    }

    /**
     * Readded as it was removed, very useful helper function
     *
     * @param x X position
     * @param y Y Position
     * @param z Z Position
     * @return The blocks light opacity
     */
    public int getBlockLightOpacity(int x, int y, int z)
    {
        if (x < -30000000 || z < -30000000 || x >= 30000000 || z >= 30000000)
        {
            return 0;
        }

        if (y < 0 || y >= 256)
        {
            return 0;
        }

        /**
         * Returns back a chunk looked up by chunk coordinates Args: x, y
         */
        return getChunkFromChunkCoords(x >> 4, z >> 4).func_150808_b(x & 15, y, z & 15);
    }

    /**
     * Returns a count of entities that classify themselves as the specified creature type.
     */
    public int countEntities(EnumCreatureType type, boolean forSpawnCount)
    {
        int count = 0;
        for (int x = 0; x < loadedEntityList.size(); x++)
        {
            if (((Entity)loadedEntityList.get(x)).isCreatureType(type, forSpawnCount))
            {
                count++;
            }
        }
        return count;
    }
}