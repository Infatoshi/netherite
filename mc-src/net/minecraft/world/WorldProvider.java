package net.minecraft.world;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Vec3;
import net.minecraft.world.WorldSettings.GameType;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.biome.WorldChunkManager;
import net.minecraft.world.biome.WorldChunkManagerHell;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.ChunkProviderFlat;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.FlatGeneratorInfo;
import net.minecraft.world.storage.WorldInfo;
import net.minecraftforge.client.IRenderHandler;
import net.minecraftforge.common.DimensionManager;

public abstract class WorldProvider
{
    public static final float[] moonPhaseFactors = new float[] {1.0F, 0.75F, 0.5F, 0.25F, 0.0F, 0.25F, 0.5F, 0.75F};
    /** world object being used */
    public World worldObj;
    public WorldType terrainType;
    public String field_82913_c;
    /** World chunk manager being used to generate chunks */
    public WorldChunkManager worldChunkMgr;
    /** States whether the Hell world provider is used(true) or if the normal world provider is used(false) */
    public boolean isHellWorld;
    /** A boolean that tells if a world does not have a sky. Used in calculating weather and skylight */
    public boolean hasNoSky;
    /** Light to brightness conversion table */
    public float[] lightBrightnessTable = new float[16];
    /** The id for the dimension (ex. -1: Nether, 0: Overworld, 1: The End) */
    public int dimensionId;
    /** Array for sunrise/sunset colors (RGBA) */
    private float[] colorsSunriseSunset = new float[4];
    private static final String __OBFID = "CL_00000386";

    /**
     * associate an existing world with a World provider, and setup its lightbrightness table
     */
    public final void registerWorld(World p_76558_1_)
    {
        this.worldObj = p_76558_1_;
        this.terrainType = p_76558_1_.getWorldInfo().getTerrainType();
        this.field_82913_c = p_76558_1_.getWorldInfo().getGeneratorOptions();
        this.registerWorldChunkManager();
        this.generateLightBrightnessTable();
    }

    /**
     * Creates the light to brightness table
     */
    protected void generateLightBrightnessTable()
    {
        float f = 0.0F;

        for (int i = 0; i <= 15; ++i)
        {
            float f1 = 1.0F - (float)i / 15.0F;
            this.lightBrightnessTable[i] = (1.0F - f1) / (f1 * 3.0F + 1.0F) * (1.0F - f) + f;
        }
    }

    /**
     * creates a new world chunk manager for WorldProvider
     */
    protected void registerWorldChunkManager()
    {
        this.worldChunkMgr = terrainType.getChunkManager(worldObj);
    }

    /**
     * Returns a new chunk provider which generates chunks for this world
     */
    public IChunkProvider createChunkGenerator()
    {
        return terrainType.getChunkGenerator(worldObj, field_82913_c);
    }

    /**
     * Will check if the x, z position specified is alright to be set as the map spawn point
     */
    public boolean canCoordinateBeSpawn(int p_76566_1_, int p_76566_2_)
    {
        return this.worldObj.getTopBlock(p_76566_1_, p_76566_2_) == Blocks.grass;
    }

    /**
     * Calculates the angle of sun and moon in the sky relative to a specified time (usually worldTime)
     */
    public float calculateCelestialAngle(long p_76563_1_, float p_76563_3_)
    {
        int j = (int)(p_76563_1_ % 24000L);
        float f1 = ((float)j + p_76563_3_) / 24000.0F - 0.25F;

        if (f1 < 0.0F)
        {
            ++f1;
        }

        if (f1 > 1.0F)
        {
            --f1;
        }

        float f2 = f1;
        f1 = 1.0F - (float)((Math.cos((double)f1 * Math.PI) + 1.0D) / 2.0D);
        f1 = f2 + (f1 - f2) / 3.0F;
        return f1;
    }

    public int getMoonPhase(long p_76559_1_)
    {
        return (int)(p_76559_1_ / 24000L % 8L + 8L) % 8;
    }

    /**
     * Returns 'true' if in the "main surface world", but 'false' if in the Nether or End dimensions.
     */
    public boolean isSurfaceWorld()
    {
        return true;
    }

    /**
     * Returns array with sunrise/sunset colors
     */
    @SideOnly(Side.CLIENT)
    public float[] calcSunriseSunsetColors(float p_76560_1_, float p_76560_2_)
    {
        float f2 = 0.4F;
        float f3 = MathHelper.cos(p_76560_1_ * (float)Math.PI * 2.0F) - 0.0F;
        float f4 = -0.0F;

        if (f3 >= f4 - f2 && f3 <= f4 + f2)
        {
            float f5 = (f3 - f4) / f2 * 0.5F + 0.5F;
            float f6 = 1.0F - (1.0F - MathHelper.sin(f5 * (float)Math.PI)) * 0.99F;
            f6 *= f6;
            this.colorsSunriseSunset[0] = f5 * 0.3F + 0.7F;
            this.colorsSunriseSunset[1] = f5 * f5 * 0.7F + 0.2F;
            this.colorsSunriseSunset[2] = f5 * f5 * 0.0F + 0.2F;
            this.colorsSunriseSunset[3] = f6;
            return this.colorsSunriseSunset;
        }
        else
        {
            return null;
        }
    }

    /**
     * Return Vec3D with biome specific fog color
     */
    @SideOnly(Side.CLIENT)
    public Vec3 getFogColor(float p_76562_1_, float p_76562_2_)
    {
        float f2 = MathHelper.cos(p_76562_1_ * (float)Math.PI * 2.0F) * 2.0F + 0.5F;

        if (f2 < 0.0F)
        {
            f2 = 0.0F;
        }

        if (f2 > 1.0F)
        {
            f2 = 1.0F;
        }

        float f3 = 0.7529412F;
        float f4 = 0.84705883F;
        float f5 = 1.0F;
        f3 *= f2 * 0.94F + 0.06F;
        f4 *= f2 * 0.94F + 0.06F;
        f5 *= f2 * 0.91F + 0.09F;
        return Vec3.createVectorHelper((double)f3, (double)f4, (double)f5);
    }

    /**
     * True if the player can respawn in this dimension (true = overworld, false = nether).
     */
    public boolean canRespawnHere()
    {
        return true;
    }

    public static WorldProvider getProviderForDimension(int p_76570_0_)
    {
        return DimensionManager.createProviderFor(p_76570_0_);
    }

    /**
     * the y level at which clouds are rendered.
     */
    @SideOnly(Side.CLIENT)
    public float getCloudHeight()
    {
        return this.terrainType.getCloudHeight();
    }

    @SideOnly(Side.CLIENT)
    public boolean isSkyColored()
    {
        return true;
    }

    /**
     * Gets the hard-coded portal location to use when entering this dimension.
     */
    public ChunkCoordinates getEntrancePortalLocation()
    {
        return null;
    }

    public int getAverageGroundLevel()
    {
        return this.terrainType.getMinimumSpawnHeight(this.worldObj);
    }

    /**
     * returns true if this dimension is supposed to display void particles and pull in the far plane based on the
     * user's Y offset.
     */
    @SideOnly(Side.CLIENT)
    public boolean getWorldHasVoidParticles()
    {
        return this.terrainType.hasVoidParticles(this.hasNoSky);
    }

    /**
     * Returns a double value representing the Y value relative to the top of the map at which void fog is at its
     * maximum. The default factor of 0.03125 relative to 256, for example, means the void fog will be at its maximum at
     * (256*0.03125), or 8.
     */
    @SideOnly(Side.CLIENT)
    public double getVoidFogYFactor()
    {
        return this.terrainType.voidFadeMagnitude();
    }

    /**
     * Returns true if the given X,Z coordinate should show environmental fog.
     */
    @SideOnly(Side.CLIENT)
    public boolean doesXZShowFog(int p_76568_1_, int p_76568_2_)
    {
        return false;
    }

    /**
     * Returns the dimension's name, e.g. "The End", "Nether", or "Overworld".
     */
    public abstract String getDimensionName();

    /*======================================= Forge Start =========================================*/
    private IRenderHandler skyRenderer = null;
    private IRenderHandler cloudRenderer = null;
    private IRenderHandler weatherRenderer = null;

    /**
     * Sets the providers current dimension ID, used in default getSaveFolder()
     * Added to allow default providers to be registered for multiple dimensions.
     *
     * @param dim Dimension ID
     */
    public void setDimension(int dim)
    {
        this.dimensionId = dim;
    }

    /**
     * Returns the sub-folder of the world folder that this WorldProvider saves to.
     * EXA: DIM1, DIM-1
     * @return The sub-folder name to save this world's chunks to.
     */
    public String getSaveFolder()
    {
        return (dimensionId == 0 ? null : "DIM" + dimensionId);
    }

    /**
     * A message to display to the user when they transfer to this dimension.
     *
     * @return The message to be displayed
     */
    public String getWelcomeMessage()
    {
        if (this instanceof WorldProviderEnd)
        {
            return "Entering the End";
        }
        else if (this instanceof WorldProviderHell)
        {
            return "Entering the Nether";
        }
        return null;
    }

    /**
     * A Message to display to the user when they transfer out of this dismension.
     *
     * @return The message to be displayed
     */
    public String getDepartMessage()
    {
        if (this instanceof WorldProviderEnd)
        {
            return "Leaving the End";
        }
        else if (this instanceof WorldProviderHell)
        {
            return "Leaving the Nether";
        }
        return null;
    }

    /**
     * The dimensions movement factor. Relative to normal overworld.
     * It is applied to the players position when they transfer dimensions.
     * Exa: Nether movement is 8.0
     * @return The movement factor
     */
    public double getMovementFactor()
    {
        if (this instanceof WorldProviderHell)
        {
            return 8.0;
        }
        return 1.0;
    }

    @SideOnly(Side.CLIENT)
    public IRenderHandler getSkyRenderer()
    {
        return this.skyRenderer;
    }

    @SideOnly(Side.CLIENT)
    public void setSkyRenderer(IRenderHandler skyRenderer)
    {
        this.skyRenderer = skyRenderer;
    }

    @SideOnly(Side.CLIENT)
    public IRenderHandler getCloudRenderer()
    {
        return cloudRenderer;
    }

    @SideOnly(Side.CLIENT)
    public void setCloudRenderer(IRenderHandler renderer)
    {
        cloudRenderer = renderer;
    }

    @SideOnly(Side.CLIENT)
    public IRenderHandler getWeatherRenderer()
    {
        return weatherRenderer;
    }

    @SideOnly(Side.CLIENT)
    public void setWeatherRenderer(IRenderHandler renderer)
    {
        weatherRenderer = renderer;
    }

    public ChunkCoordinates getRandomizedSpawnPoint()
    {
        ChunkCoordinates chunkcoordinates = new ChunkCoordinates(this.worldObj.getSpawnPoint());

        boolean isAdventure = worldObj.getWorldInfo().getGameType() == GameType.ADVENTURE;
        int spawnFuzz = terrainType.getSpawnFuzz();
        int spawnFuzzHalf = spawnFuzz / 2;

        if (!hasNoSky && !isAdventure && net.minecraftforge.common.ForgeModContainer.defaultHasSpawnFuzz)
        {
            chunkcoordinates.posX += this.worldObj.rand.nextInt(spawnFuzz) - spawnFuzzHalf;
            chunkcoordinates.posZ += this.worldObj.rand.nextInt(spawnFuzz) - spawnFuzzHalf;
            chunkcoordinates.posY = this.worldObj.getTopSolidOrLiquidBlock(chunkcoordinates.posX, chunkcoordinates.posZ);
        }

        return chunkcoordinates;
    }

    /**
     * Determine if the cusor on the map should 'spin' when rendered, like it does for the player in the nether.
     *
     * @param entity The entity holding the map, playername, or frame-ENTITYID
     * @param x X Position
     * @param y Y Position
     * @param z Z Postion
     * @return True to 'spin' the cursor
     */
    public boolean shouldMapSpin(String entity, double x, double y, double z)
    {
        return dimensionId < 0;
    }

    /**
     * Determines the dimension the player will be respawned in, typically this brings them back to the overworld.
     *
     * @param player The player that is respawning
     * @return The dimension to respawn the player in
     */
    public int getRespawnDimension(EntityPlayerMP player)
    {
        return 0;
    }

    /*======================================= Start Moved From World =========================================*/

    public BiomeGenBase getBiomeGenForCoords(int x, int z)
    {
        return worldObj.getBiomeGenForCoordsBody(x, z);
    }

    public boolean isDaytime()
    {
        return worldObj.skylightSubtracted < 4;
    }

    /**
     * The current sun brightness factor for this dimension.
     * 0.0f means no light at all, and 1.0f means maximum sunlight.
     * This will be used for the "calculateSkylightSubtracted"
     * which is for Sky light value calculation.
     *
     * @return The current brightness factor
     * */
    public float getSunBrightnessFactor(float par1)
    {
        return worldObj.getSunBrightnessFactor(par1);
    }

    /**
     * Calculates the current moon phase factor.
     * This factor is effective for slimes.
     * (This method do not affect the moon rendering)
     * */
    public float getCurrentMoonPhaseFactor()
    {
        return worldObj.getCurrentMoonPhaseFactorBody();
    }

    @SideOnly(Side.CLIENT)
    public Vec3 getSkyColor(Entity cameraEntity, float partialTicks)
    {
        return worldObj.getSkyColorBody(cameraEntity, partialTicks);
    }

    @SideOnly(Side.CLIENT)
    public Vec3 drawClouds(float partialTicks)
    {
        return worldObj.drawCloudsBody(partialTicks);
    }

    /**
     * Gets the Sun Brightness for rendering sky.
     * */
    @SideOnly(Side.CLIENT)
    public float getSunBrightness(float par1)
    {
        return worldObj.getSunBrightnessBody(par1);
    }

    /**
     * Gets the Star Brightness for rendering sky.
     * */
    @SideOnly(Side.CLIENT)
    public float getStarBrightness(float par1)
    {
        return worldObj.getStarBrightnessBody(par1);
    }

    public void setAllowedSpawnTypes(boolean allowHostile, boolean allowPeaceful)
    {
        worldObj.spawnHostileMobs = allowHostile;
        worldObj.spawnPeacefulMobs = allowPeaceful;
    }

    public void calculateInitialWeather()
    {
        worldObj.calculateInitialWeatherBody();
    }

    public void updateWeather()
    {
        worldObj.updateWeatherBody();
    }

    public boolean canBlockFreeze(int x, int y, int z, boolean byWater)
    {
        return worldObj.canBlockFreezeBody(x, y, z, byWater);
    }

    public boolean canSnowAt(int x, int y, int z, boolean checkLight)
    {
        return worldObj.canSnowAtBody(x, y, z, checkLight);
    }

    public void setWorldTime(long time)
    {
        worldObj.worldInfo.setWorldTime(time);
    }

    public long getSeed()
    {
        return worldObj.worldInfo.getSeed();
    }

    public long getWorldTime()
    {
        return worldObj.worldInfo.getWorldTime();
    }

    public ChunkCoordinates getSpawnPoint()
    {
        WorldInfo info = worldObj.worldInfo;
        return new ChunkCoordinates(info.getSpawnX(), info.getSpawnY(), info.getSpawnZ());
    }

    public void setSpawnPoint(int x, int y, int z)
    {
        worldObj.worldInfo.setSpawnPosition(x, y, z);
    }

    public boolean canMineBlock(EntityPlayer player, int x, int y, int z)
    {
        return worldObj.canMineBlockBody(player, x, y, z);
    }

    public boolean isBlockHighHumidity(int x, int y, int z)
    {
        return worldObj.getBiomeGenForCoords(x, z).isHighHumidity();
    }

    public int getHeight()
    {
        return 256;
    }

    public int getActualHeight()
    {
        return hasNoSky ? 128 : 256;
    }

    public double getHorizon()
    {
        return worldObj.worldInfo.getTerrainType().getHorizon(worldObj);
    }

    public void resetRainAndThunder()
    {
        worldObj.worldInfo.setRainTime(0);
        worldObj.worldInfo.setRaining(false);
        worldObj.worldInfo.setThunderTime(0);
        worldObj.worldInfo.setThundering(false);
    }

    public boolean canDoLightning(Chunk chunk)
    {
        return true;
    }

    public boolean canDoRainSnowIce(Chunk chunk)
    {
        return true;
    }
}