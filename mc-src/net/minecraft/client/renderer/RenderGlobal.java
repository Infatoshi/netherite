package net.minecraft.client.renderer;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.audio.ISound;
import net.minecraft.client.audio.PositionedSoundRecord;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.particle.EntityAuraFX;
import net.minecraft.client.particle.EntityBlockDustFX;
import net.minecraft.client.particle.EntityBreakingFX;
import net.minecraft.client.particle.EntityBubbleFX;
import net.minecraft.client.particle.EntityCloudFX;
import net.minecraft.client.particle.EntityCritFX;
import net.minecraft.client.particle.EntityDiggingFX;
import net.minecraft.client.particle.EntityDropParticleFX;
import net.minecraft.client.particle.EntityEnchantmentTableParticleFX;
import net.minecraft.client.particle.EntityExplodeFX;
import net.minecraft.client.particle.EntityFX;
import net.minecraft.client.particle.EntityFireworkSparkFX;
import net.minecraft.client.particle.EntityFishWakeFX;
import net.minecraft.client.particle.EntityFlameFX;
import net.minecraft.client.particle.EntityFootStepFX;
import net.minecraft.client.particle.EntityHeartFX;
import net.minecraft.client.particle.EntityHugeExplodeFX;
import net.minecraft.client.particle.EntityLargeExplodeFX;
import net.minecraft.client.particle.EntityLavaFX;
import net.minecraft.client.particle.EntityNoteFX;
import net.minecraft.client.particle.EntityPortalFX;
import net.minecraft.client.particle.EntityReddustFX;
import net.minecraft.client.particle.EntitySmokeFX;
import net.minecraft.client.particle.EntitySnowShovelFX;
import net.minecraft.client.particle.EntitySpellParticleFX;
import net.minecraft.client.particle.EntitySplashFX;
import net.minecraft.client.particle.EntitySuspendFX;
import net.minecraft.client.renderer.culling.ICamera;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.client.renderer.texture.IIconRegister;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.client.renderer.tileentity.TileEntityRendererDispatcher;
import net.minecraft.client.util.RenderDistanceSorter;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemDye;
import net.minecraft.item.ItemRecord;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.Vec3;
import net.minecraft.world.IWorldAccess;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.opengl.ARBOcclusionQuery;
import org.lwjgl.opengl.GL11;

import net.minecraftforge.client.IRenderHandler;
import net.minecraftforge.client.MinecraftForgeClient;

@SideOnly(Side.CLIENT)
public class RenderGlobal implements IWorldAccess
{
    private static final Logger logger = LogManager.getLogger();
    private static final ResourceLocation locationMoonPhasesPng = new ResourceLocation("textures/environment/moon_phases.png");
    private static final ResourceLocation locationSunPng = new ResourceLocation("textures/environment/sun.png");
    private static final ResourceLocation locationCloudsPng = new ResourceLocation("textures/environment/clouds.png");
    private static final ResourceLocation locationEndSkyPng = new ResourceLocation("textures/environment/end_sky.png");
    public List tileEntities = new ArrayList();
    private WorldClient theWorld;
    /** The RenderEngine instance used by RenderGlobal */
    private final TextureManager renderEngine;
    private List worldRenderersToUpdate = new ArrayList();
    private WorldRenderer[] sortedWorldRenderers;
    private WorldRenderer[] worldRenderers;
    private int renderChunksWide;
    private int renderChunksTall;
    private int renderChunksDeep;
    /** OpenGL render lists base */
    private int glRenderListBase;
    /** A reference to the Minecraft object. */
    private Minecraft mc;
    private RenderBlocks renderBlocksRg;
    /** OpenGL occlusion query base */
    private IntBuffer glOcclusionQueryBase;
    /** Is occlusion testing enabled */
    private boolean occlusionEnabled;
    /** counts the cloud render updates. Used with mod to stagger some updates */
    private int cloudTickCounter;
    /** The star GL Call list */
    private int starGLCallList;
    /** OpenGL sky list */
    private int glSkyList;
    /** OpenGL sky list 2 */
    private int glSkyList2;
    /** Minimum block X */
    private int minBlockX;
    /** Minimum block Y */
    private int minBlockY;
    /** Minimum block Z */
    private int minBlockZ;
    /** Maximum block X */
    private int maxBlockX;
    /** Maximum block Y */
    private int maxBlockY;
    /** Maximum block Z */
    private int maxBlockZ;
    /**
     * Stores blocks currently being broken. Key is entity ID of the thing doing the breaking. Value is a
     * DestroyBlockProgress
     */
    private final Map damagedBlocks = new HashMap();
    /** Currently playing sounds.  Type:  HashMap<ChunkCoordinates, ISound> */
    private final Map mapSoundPositions = Maps.newHashMap();
    private IIcon[] destroyBlockIcons;
    private boolean displayListEntitiesDirty;
    private int displayListEntities;
    private int renderDistanceChunks = -1;
    /** Render entities startup counter (init value=2) */
    private int renderEntitiesStartupCounter = 2;
    /** Count entities total */
    private int countEntitiesTotal;
    /** Count entities rendered */
    private int countEntitiesRendered;
    /** Count entities hidden */
    private int countEntitiesHidden;
    /** Occlusion query result */
    IntBuffer occlusionResult = GLAllocation.createDirectIntBuffer(64);
    /** How many renderers are loaded this frame that try to be rendered */
    private int renderersLoaded;
    /** How many renderers are being clipped by the frustrum this frame */
    private int renderersBeingClipped;
    /** How many renderers are being occluded this frame */
    private int renderersBeingOccluded;
    /** How many renderers are actually being rendered this frame */
    private int renderersBeingRendered;
    /** How many renderers are skipping rendering due to not having a render pass this frame */
    private int renderersSkippingRenderPass;
    /** Dummy render int */
    private int dummyRenderInt;
    /** World renderers check index */
    private int worldRenderersCheckIndex;
    /** List of OpenGL lists for the current render pass */
    private List glRenderLists = new ArrayList();
    /** All render lists (fixed length 4) */
    private RenderList[] allRenderLists = new RenderList[] {new RenderList(), new RenderList(), new RenderList(), new RenderList()};
    /**
     * Previous x position when the renderers were sorted. (Once the distance moves more than 4 units they will be
     * resorted)
     */
    double prevSortX = -9999.0D;
    /**
     * Previous y position when the renderers were sorted. (Once the distance moves more than 4 units they will be
     * resorted)
     */
    double prevSortY = -9999.0D;
    /**
     * Previous Z position when the renderers were sorted. (Once the distance moves more than 4 units they will be
     * resorted)
     */
    double prevSortZ = -9999.0D;
    double prevRenderSortX = -9999.0D;
    double prevRenderSortY = -9999.0D;
    double prevRenderSortZ = -9999.0D;
    int prevChunkSortX = -999;
    int prevChunkSortY = -999;
    int prevChunkSortZ = -999;
    /** The offset used to determine if a renderer is one of the sixteenth that are being updated this frame */
    int frustumCheckOffset;
    private static final String __OBFID = "CL_00000954";

    public RenderGlobal(Minecraft p_i1249_1_)
    {
        this.mc = p_i1249_1_;
        this.renderEngine = p_i1249_1_.getTextureManager();
        byte b0 = 34;
        byte b1 = 16;
        this.glRenderListBase = GLAllocation.generateDisplayLists(b0 * b0 * b1 * 3);
        this.displayListEntitiesDirty = false;
        this.displayListEntities = GLAllocation.generateDisplayLists(1);
        this.occlusionEnabled = OpenGlCapsChecker.checkARBOcclusion();

        if (this.occlusionEnabled)
        {
            this.occlusionResult.clear();
            this.glOcclusionQueryBase = GLAllocation.createDirectIntBuffer(b0 * b0 * b1);
            this.glOcclusionQueryBase.clear();
            this.glOcclusionQueryBase.position(0);
            this.glOcclusionQueryBase.limit(b0 * b0 * b1);
            ARBOcclusionQuery.glGenQueriesARB(this.glOcclusionQueryBase);
        }

        this.starGLCallList = GLAllocation.generateDisplayLists(3);
        GL11.glPushMatrix();
        GL11.glNewList(this.starGLCallList, GL11.GL_COMPILE);
        this.renderStars();
        GL11.glEndList();
        GL11.glPopMatrix();
        Tessellator tessellator = Tessellator.instance;
        this.glSkyList = this.starGLCallList + 1;
        GL11.glNewList(this.glSkyList, GL11.GL_COMPILE);
        byte b2 = 64;
        int i = 256 / b2 + 2;
        float f = 16.0F;
        int j;
        int k;

        for (j = -b2 * i; j <= b2 * i; j += b2)
        {
            for (k = -b2 * i; k <= b2 * i; k += b2)
            {
                tessellator.startDrawingQuads();
                tessellator.addVertex((double)(j + 0), (double)f, (double)(k + 0));
                tessellator.addVertex((double)(j + b2), (double)f, (double)(k + 0));
                tessellator.addVertex((double)(j + b2), (double)f, (double)(k + b2));
                tessellator.addVertex((double)(j + 0), (double)f, (double)(k + b2));
                tessellator.draw();
            }
        }

        GL11.glEndList();
        this.glSkyList2 = this.starGLCallList + 2;
        GL11.glNewList(this.glSkyList2, GL11.GL_COMPILE);
        f = -16.0F;
        tessellator.startDrawingQuads();

        for (j = -b2 * i; j <= b2 * i; j += b2)
        {
            for (k = -b2 * i; k <= b2 * i; k += b2)
            {
                tessellator.addVertex((double)(j + b2), (double)f, (double)(k + 0));
                tessellator.addVertex((double)(j + 0), (double)f, (double)(k + 0));
                tessellator.addVertex((double)(j + 0), (double)f, (double)(k + b2));
                tessellator.addVertex((double)(j + b2), (double)f, (double)(k + b2));
            }
        }

        tessellator.draw();
        GL11.glEndList();
    }

    private void renderStars()
    {
        Random random = new Random(10842L);
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();

        for (int i = 0; i < 1500; ++i)
        {
            double d0 = (double)(random.nextFloat() * 2.0F - 1.0F);
            double d1 = (double)(random.nextFloat() * 2.0F - 1.0F);
            double d2 = (double)(random.nextFloat() * 2.0F - 1.0F);
            double d3 = (double)(0.15F + random.nextFloat() * 0.1F);
            double d4 = d0 * d0 + d1 * d1 + d2 * d2;

            if (d4 < 1.0D && d4 > 0.01D)
            {
                d4 = 1.0D / Math.sqrt(d4);
                d0 *= d4;
                d1 *= d4;
                d2 *= d4;
                double d5 = d0 * 100.0D;
                double d6 = d1 * 100.0D;
                double d7 = d2 * 100.0D;
                double d8 = Math.atan2(d0, d2);
                double d9 = Math.sin(d8);
                double d10 = Math.cos(d8);
                double d11 = Math.atan2(Math.sqrt(d0 * d0 + d2 * d2), d1);
                double d12 = Math.sin(d11);
                double d13 = Math.cos(d11);
                double d14 = random.nextDouble() * Math.PI * 2.0D;
                double d15 = Math.sin(d14);
                double d16 = Math.cos(d14);

                for (int j = 0; j < 4; ++j)
                {
                    double d17 = 0.0D;
                    double d18 = (double)((j & 2) - 1) * d3;
                    double d19 = (double)((j + 1 & 2) - 1) * d3;
                    double d20 = d18 * d16 - d19 * d15;
                    double d21 = d19 * d16 + d18 * d15;
                    double d22 = d20 * d12 + d17 * d13;
                    double d23 = d17 * d12 - d20 * d13;
                    double d24 = d23 * d9 - d21 * d10;
                    double d25 = d21 * d9 + d23 * d10;
                    tessellator.addVertex(d5 + d24, d6 + d22, d7 + d25);
                }
            }
        }

        tessellator.draw();
    }

    /**
     * set null to clear
     */
    public void setWorldAndLoadRenderers(WorldClient p_72732_1_)
    {
        if (this.theWorld != null)
        {
            this.theWorld.removeWorldAccess(this);
        }

        this.prevSortX = -9999.0D;
        this.prevSortY = -9999.0D;
        this.prevSortZ = -9999.0D;
        this.prevRenderSortX = -9999.0D;
        this.prevRenderSortY = -9999.0D;
        this.prevRenderSortZ = -9999.0D;
        this.prevChunkSortX = -9999;
        this.prevChunkSortY = -9999;
        this.prevChunkSortZ = -9999;
        RenderManager.instance.set(p_72732_1_);
        this.theWorld = p_72732_1_;
        this.renderBlocksRg = new RenderBlocks(p_72732_1_);

        if (p_72732_1_ != null)
        {
            p_72732_1_.addWorldAccess(this);
            this.loadRenderers();
        }
    }

    /**
     * Loads all the renderers and sets up the basic settings usage
     */
    public void loadRenderers()
    {
        if (this.theWorld != null)
        {
            Blocks.leaves.setGraphicsLevel(this.mc.gameSettings.fancyGraphics);
            Blocks.leaves2.setGraphicsLevel(this.mc.gameSettings.fancyGraphics);
            this.renderDistanceChunks = this.mc.gameSettings.renderDistanceChunks;
            int i;

            if (this.worldRenderers != null)
            {
                for (i = 0; i < this.worldRenderers.length; ++i)
                {
                    this.worldRenderers[i].stopRendering();
                }
            }

            i = this.renderDistanceChunks * 2 + 1;
            this.renderChunksWide = i;
            this.renderChunksTall = 16;
            this.renderChunksDeep = i;
            this.worldRenderers = new WorldRenderer[this.renderChunksWide * this.renderChunksTall * this.renderChunksDeep];
            this.sortedWorldRenderers = new WorldRenderer[this.renderChunksWide * this.renderChunksTall * this.renderChunksDeep];
            int j = 0;
            int k = 0;
            this.minBlockX = 0;
            this.minBlockY = 0;
            this.minBlockZ = 0;
            this.maxBlockX = this.renderChunksWide;
            this.maxBlockY = this.renderChunksTall;
            this.maxBlockZ = this.renderChunksDeep;
            int l;

            for (l = 0; l < this.worldRenderersToUpdate.size(); ++l)
            {
                ((WorldRenderer)this.worldRenderersToUpdate.get(l)).needsUpdate = false;
            }

            this.worldRenderersToUpdate.clear();
            this.tileEntities.clear();
            this.onStaticEntitiesChanged();

            for (l = 0; l < this.renderChunksWide; ++l)
            {
                for (int i1 = 0; i1 < this.renderChunksTall; ++i1)
                {
                    for (int j1 = 0; j1 < this.renderChunksDeep; ++j1)
                    {
                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l] = new WorldRenderer(this.theWorld, this.tileEntities, l * 16, i1 * 16, j1 * 16, this.glRenderListBase + j);

                        if (this.occlusionEnabled)
                        {
                            this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].glOcclusionQuery = this.glOcclusionQueryBase.get(k);
                        }

                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].isWaitingOnOcclusionQuery = false;
                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].isVisible = true;
                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].isInFrustum = true;
                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].chunkIndex = k++;
                        this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l].markDirty();
                        this.sortedWorldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l] = this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l];
                        this.worldRenderersToUpdate.add(this.worldRenderers[(j1 * this.renderChunksTall + i1) * this.renderChunksWide + l]);
                        j += 3;
                    }
                }
            }

            if (this.theWorld != null)
            {
                EntityLivingBase entitylivingbase = this.mc.renderViewEntity;

                if (entitylivingbase != null)
                {
                    this.markRenderersForNewPosition(MathHelper.floor_double(entitylivingbase.posX), MathHelper.floor_double(entitylivingbase.posY), MathHelper.floor_double(entitylivingbase.posZ));
                    Arrays.sort(this.sortedWorldRenderers, new EntitySorter(entitylivingbase));
                }
            }

            this.renderEntitiesStartupCounter = 2;
        }
    }

    /**
     * Renders all entities within range and within the frustrum. Args: pos, frustrum, partialTickTime
     */
    public void renderEntities(EntityLivingBase p_147589_1_, ICamera p_147589_2_, float p_147589_3_)
    {
        int pass = MinecraftForgeClient.getRenderPass();
        if (this.renderEntitiesStartupCounter > 0)
        {
            if (pass > 0) return;
            --this.renderEntitiesStartupCounter;
        }
        else
        {
            double d0 = p_147589_1_.prevPosX + (p_147589_1_.posX - p_147589_1_.prevPosX) * (double)p_147589_3_;
            double d1 = p_147589_1_.prevPosY + (p_147589_1_.posY - p_147589_1_.prevPosY) * (double)p_147589_3_;
            double d2 = p_147589_1_.prevPosZ + (p_147589_1_.posZ - p_147589_1_.prevPosZ) * (double)p_147589_3_;
            this.theWorld.theProfiler.startSection("prepare");
            TileEntityRendererDispatcher.instance.cacheActiveRenderInfo(this.theWorld, this.mc.getTextureManager(), this.mc.fontRenderer, this.mc.renderViewEntity, p_147589_3_);
            RenderManager.instance.cacheActiveRenderInfo(this.theWorld, this.mc.getTextureManager(), this.mc.fontRenderer, this.mc.renderViewEntity, this.mc.pointedEntity, this.mc.gameSettings, p_147589_3_);
            if (pass == 0) // no indentation to shrink patch
            {
            this.countEntitiesTotal = 0;
            this.countEntitiesRendered = 0;
            this.countEntitiesHidden = 0;
            }
            EntityLivingBase entitylivingbase1 = this.mc.renderViewEntity;
            double d3 = entitylivingbase1.lastTickPosX + (entitylivingbase1.posX - entitylivingbase1.lastTickPosX) * (double)p_147589_3_;
            double d4 = entitylivingbase1.lastTickPosY + (entitylivingbase1.posY - entitylivingbase1.lastTickPosY) * (double)p_147589_3_;
            double d5 = entitylivingbase1.lastTickPosZ + (entitylivingbase1.posZ - entitylivingbase1.lastTickPosZ) * (double)p_147589_3_;
            TileEntityRendererDispatcher.staticPlayerX = d3;
            TileEntityRendererDispatcher.staticPlayerY = d4;
            TileEntityRendererDispatcher.staticPlayerZ = d5;
            this.theWorld.theProfiler.endStartSection("staticentities");

            if (this.displayListEntitiesDirty)
            {
                RenderManager.renderPosX = 0.0D;
                RenderManager.renderPosY = 0.0D;
                RenderManager.renderPosZ = 0.0D;
                this.rebuildDisplayListEntities();
            }

            GL11.glMatrixMode(GL11.GL_MODELVIEW);
            GL11.glPushMatrix();
            GL11.glTranslated(-d3, -d4, -d5);
            GL11.glCallList(this.displayListEntities);
            GL11.glPopMatrix();
            RenderManager.renderPosX = d3;
            RenderManager.renderPosY = d4;
            RenderManager.renderPosZ = d5;
            this.mc.entityRenderer.enableLightmap((double)p_147589_3_);
            this.theWorld.theProfiler.endStartSection("global");
            List list = this.theWorld.getLoadedEntityList();
            if (pass == 0) // no indentation for smaller patch size
            {
            this.countEntitiesTotal = list.size();
            }
            int i;
            Entity entity;

            for (i = 0; i < this.theWorld.weatherEffects.size(); ++i)
            {
                entity = (Entity)this.theWorld.weatherEffects.get(i);
                if (!entity.shouldRenderInPass(pass)) continue;
                ++this.countEntitiesRendered;

                if (entity.isInRangeToRender3d(d0, d1, d2))
                {
                    RenderManager.instance.renderEntitySimple(entity, p_147589_3_);
                }
            }

            this.theWorld.theProfiler.endStartSection("entities");

            for (i = 0; i < list.size(); ++i)
            {
                entity = (Entity)list.get(i);
                if (!entity.shouldRenderInPass(pass)) continue;
                boolean flag = entity.isInRangeToRender3d(d0, d1, d2) && (entity.ignoreFrustumCheck || p_147589_2_.isBoundingBoxInFrustum(entity.boundingBox) || entity.riddenByEntity == this.mc.thePlayer);

                if (!flag && entity instanceof EntityLiving)
                {
                    EntityLiving entityliving = (EntityLiving)entity;

                    if (entityliving.getLeashed() && entityliving.getLeashedToEntity() != null)
                    {
                        Entity entity1 = entityliving.getLeashedToEntity();
                        flag = p_147589_2_.isBoundingBoxInFrustum(entity1.boundingBox);
                    }
                }

                if (flag && (entity != this.mc.renderViewEntity || this.mc.gameSettings.thirdPersonView != 0 || this.mc.renderViewEntity.isPlayerSleeping()) && this.theWorld.blockExists(MathHelper.floor_double(entity.posX), 0, MathHelper.floor_double(entity.posZ)))
                {
                    ++this.countEntitiesRendered;
                    RenderManager.instance.renderEntitySimple(entity, p_147589_3_);
                }
            }

            this.theWorld.theProfiler.endStartSection("blockentities");
            RenderHelper.enableStandardItemLighting();

            for (i = 0; i < this.tileEntities.size(); ++i)
            {
                TileEntity tile = (TileEntity)this.tileEntities.get(i);
                if (tile.shouldRenderInPass(pass) && p_147589_2_.isBoundingBoxInFrustum(tile.getRenderBoundingBox()))
                {
                    TileEntityRendererDispatcher.instance.renderTileEntity(tile, p_147589_3_);
                }
            }

            this.mc.entityRenderer.disableLightmap((double)p_147589_3_);
            this.theWorld.theProfiler.endSection();
        }
    }

    /**
     * Gets the render info for use on the Debug screen
     */
    public String getDebugInfoRenders()
    {
        return "C: " + this.renderersBeingRendered + "/" + this.renderersLoaded + ". F: " + this.renderersBeingClipped + ", O: " + this.renderersBeingOccluded + ", E: " + this.renderersSkippingRenderPass;
    }

    /**
     * Gets the entities info for use on the Debug screen
     */
    public String getDebugInfoEntities()
    {
        return "E: " + this.countEntitiesRendered + "/" + this.countEntitiesTotal + ". B: " + this.countEntitiesHidden + ", I: " + (this.countEntitiesTotal - this.countEntitiesHidden - this.countEntitiesRendered);
    }

    public void onStaticEntitiesChanged()
    {
        this.displayListEntitiesDirty = true;
    }

    public void rebuildDisplayListEntities()
    {
        this.theWorld.theProfiler.startSection("staticentityrebuild");
        GL11.glPushMatrix();
        GL11.glNewList(this.displayListEntities, GL11.GL_COMPILE);
        List list = this.theWorld.getLoadedEntityList();
        this.displayListEntitiesDirty = false;

        for (int i = 0; i < list.size(); ++i)
        {
            Entity entity = (Entity)list.get(i);

            if (RenderManager.instance.getEntityRenderObject(entity).isStaticEntity())
            {
                this.displayListEntitiesDirty = this.displayListEntitiesDirty || !RenderManager.instance.renderEntityStatic(entity, 0.0F, true);
            }
        }

        GL11.glEndList();
        GL11.glPopMatrix();
        this.theWorld.theProfiler.endSection();
    }

    /**
     * Goes through all the renderers setting new positions on them and those that have their position changed are
     * adding to be updated
     */
    private void markRenderersForNewPosition(int p_72722_1_, int p_72722_2_, int p_72722_3_)
    {
        p_72722_1_ -= 8;
        p_72722_2_ -= 8;
        p_72722_3_ -= 8;
        this.minBlockX = Integer.MAX_VALUE;
        this.minBlockY = Integer.MAX_VALUE;
        this.minBlockZ = Integer.MAX_VALUE;
        this.maxBlockX = Integer.MIN_VALUE;
        this.maxBlockY = Integer.MIN_VALUE;
        this.maxBlockZ = Integer.MIN_VALUE;
        int l = this.renderChunksWide * 16;
        int i1 = l / 2;

        for (int j1 = 0; j1 < this.renderChunksWide; ++j1)
        {
            int k1 = j1 * 16;
            int l1 = k1 + i1 - p_72722_1_;

            if (l1 < 0)
            {
                l1 -= l - 1;
            }

            l1 /= l;
            k1 -= l1 * l;

            if (k1 < this.minBlockX)
            {
                this.minBlockX = k1;
            }

            if (k1 > this.maxBlockX)
            {
                this.maxBlockX = k1;
            }

            for (int i2 = 0; i2 < this.renderChunksDeep; ++i2)
            {
                int j2 = i2 * 16;
                int k2 = j2 + i1 - p_72722_3_;

                if (k2 < 0)
                {
                    k2 -= l - 1;
                }

                k2 /= l;
                j2 -= k2 * l;

                if (j2 < this.minBlockZ)
                {
                    this.minBlockZ = j2;
                }

                if (j2 > this.maxBlockZ)
                {
                    this.maxBlockZ = j2;
                }

                for (int l2 = 0; l2 < this.renderChunksTall; ++l2)
                {
                    int i3 = l2 * 16;

                    if (i3 < this.minBlockY)
                    {
                        this.minBlockY = i3;
                    }

                    if (i3 > this.maxBlockY)
                    {
                        this.maxBlockY = i3;
                    }

                    WorldRenderer worldrenderer = this.worldRenderers[(i2 * this.renderChunksTall + l2) * this.renderChunksWide + j1];
                    boolean flag = worldrenderer.needsUpdate;
                    worldrenderer.setPosition(k1, i3, j2);

                    if (!flag && worldrenderer.needsUpdate)
                    {
                        this.worldRenderersToUpdate.add(worldrenderer);
                    }
                }
            }
        }
    }

    /**
     * Sorts all renderers based on the passed in entity. Args: entityLiving, renderPass, partialTickTime
     */
    public int sortAndRender(EntityLivingBase p_72719_1_, int p_72719_2_, double p_72719_3_)
    {
        this.theWorld.theProfiler.startSection("sortchunks");

        for (int j = 0; j < 10; ++j)
        {
            this.worldRenderersCheckIndex = (this.worldRenderersCheckIndex + 1) % this.worldRenderers.length;
            WorldRenderer worldrenderer = this.worldRenderers[this.worldRenderersCheckIndex];

            if (worldrenderer.needsUpdate && !this.worldRenderersToUpdate.contains(worldrenderer))
            {
                this.worldRenderersToUpdate.add(worldrenderer);
            }
        }

        if (this.mc.gameSettings.renderDistanceChunks != this.renderDistanceChunks)
        {
            this.loadRenderers();
        }

        if (p_72719_2_ == 0)
        {
            this.renderersLoaded = 0;
            this.dummyRenderInt = 0;
            this.renderersBeingClipped = 0;
            this.renderersBeingOccluded = 0;
            this.renderersBeingRendered = 0;
            this.renderersSkippingRenderPass = 0;
        }

        double d9 = p_72719_1_.lastTickPosX + (p_72719_1_.posX - p_72719_1_.lastTickPosX) * p_72719_3_;
        double d1 = p_72719_1_.lastTickPosY + (p_72719_1_.posY - p_72719_1_.lastTickPosY) * p_72719_3_;
        double d2 = p_72719_1_.lastTickPosZ + (p_72719_1_.posZ - p_72719_1_.lastTickPosZ) * p_72719_3_;
        double d3 = p_72719_1_.posX - this.prevSortX;
        double d4 = p_72719_1_.posY - this.prevSortY;
        double d5 = p_72719_1_.posZ - this.prevSortZ;

        if (this.prevChunkSortX != p_72719_1_.chunkCoordX || this.prevChunkSortY != p_72719_1_.chunkCoordY || this.prevChunkSortZ != p_72719_1_.chunkCoordZ || d3 * d3 + d4 * d4 + d5 * d5 > 16.0D)
        {
            this.prevSortX = p_72719_1_.posX;
            this.prevSortY = p_72719_1_.posY;
            this.prevSortZ = p_72719_1_.posZ;
            this.prevChunkSortX = p_72719_1_.chunkCoordX;
            this.prevChunkSortY = p_72719_1_.chunkCoordY;
            this.prevChunkSortZ = p_72719_1_.chunkCoordZ;
            this.markRenderersForNewPosition(MathHelper.floor_double(p_72719_1_.posX), MathHelper.floor_double(p_72719_1_.posY), MathHelper.floor_double(p_72719_1_.posZ));
            Arrays.sort(this.sortedWorldRenderers, new EntitySorter(p_72719_1_));
        }

        double d6 = p_72719_1_.posX - this.prevRenderSortX;
        double d7 = p_72719_1_.posY - this.prevRenderSortY;
        double d8 = p_72719_1_.posZ - this.prevRenderSortZ;
        int k;

        if (d6 * d6 + d7 * d7 + d8 * d8 > 1.0D)
        {
            this.prevRenderSortX = p_72719_1_.posX;
            this.prevRenderSortY = p_72719_1_.posY;
            this.prevRenderSortZ = p_72719_1_.posZ;

            for (k = 0; k < 27; ++k)
            {
                this.sortedWorldRenderers[k].updateRendererSort(p_72719_1_);
            }
        }

        RenderHelper.disableStandardItemLighting();
        byte b1 = 0;

        if (this.occlusionEnabled && this.mc.gameSettings.advancedOpengl && !this.mc.gameSettings.anaglyph && p_72719_2_ == 0)
        {
            byte b0 = 0;
            int l = 16;
            this.checkOcclusionQueryResult(b0, l);

            for (int i1 = b0; i1 < l; ++i1)
            {
                this.sortedWorldRenderers[i1].isVisible = true;
            }

            this.theWorld.theProfiler.endStartSection("render");
            k = b1 + this.renderSortedRenderers(b0, l, p_72719_2_, p_72719_3_);

            do
            {
                this.theWorld.theProfiler.endStartSection("occ");
                int l1 = l;
                l *= 2;

                if (l > this.sortedWorldRenderers.length)
                {
                    l = this.sortedWorldRenderers.length;
                }

                GL11.glDisable(GL11.GL_TEXTURE_2D);
                GL11.glDisable(GL11.GL_LIGHTING);
                GL11.glDisable(GL11.GL_ALPHA_TEST);
                GL11.glDisable(GL11.GL_FOG);
                GL11.glColorMask(false, false, false, false);
                GL11.glDepthMask(false);
                this.theWorld.theProfiler.startSection("check");
                this.checkOcclusionQueryResult(l1, l);
                this.theWorld.theProfiler.endSection();
                GL11.glPushMatrix();
                float f9 = 0.0F;
                float f = 0.0F;
                float f1 = 0.0F;

                for (int j1 = l1; j1 < l; ++j1)
                {
                    if (this.sortedWorldRenderers[j1].skipAllRenderPasses())
                    {
                        this.sortedWorldRenderers[j1].isInFrustum = false;
                    }
                    else
                    {
                        if (!this.sortedWorldRenderers[j1].isInFrustum)
                        {
                            this.sortedWorldRenderers[j1].isVisible = true;
                        }

                        if (this.sortedWorldRenderers[j1].isInFrustum && !this.sortedWorldRenderers[j1].isWaitingOnOcclusionQuery)
                        {
                            float f2 = MathHelper.sqrt_float(this.sortedWorldRenderers[j1].distanceToEntitySquared(p_72719_1_));
                            int k1 = (int)(1.0F + f2 / 128.0F);

                            if (this.cloudTickCounter % k1 == j1 % k1)
                            {
                                WorldRenderer worldrenderer1 = this.sortedWorldRenderers[j1];
                                float f3 = (float)((double)worldrenderer1.posXMinus - d9);
                                float f4 = (float)((double)worldrenderer1.posYMinus - d1);
                                float f5 = (float)((double)worldrenderer1.posZMinus - d2);
                                float f6 = f3 - f9;
                                float f7 = f4 - f;
                                float f8 = f5 - f1;

                                if (f6 != 0.0F || f7 != 0.0F || f8 != 0.0F)
                                {
                                    GL11.glTranslatef(f6, f7, f8);
                                    f9 += f6;
                                    f += f7;
                                    f1 += f8;
                                }

                                this.theWorld.theProfiler.startSection("bb");
                                ARBOcclusionQuery.glBeginQueryARB(ARBOcclusionQuery.GL_SAMPLES_PASSED_ARB, this.sortedWorldRenderers[j1].glOcclusionQuery);
                                this.sortedWorldRenderers[j1].callOcclusionQueryList();
                                ARBOcclusionQuery.glEndQueryARB(ARBOcclusionQuery.GL_SAMPLES_PASSED_ARB);
                                this.theWorld.theProfiler.endSection();
                                this.sortedWorldRenderers[j1].isWaitingOnOcclusionQuery = true;
                            }
                        }
                    }
                }

                GL11.glPopMatrix();

                if (this.mc.gameSettings.anaglyph)
                {
                    if (EntityRenderer.anaglyphField == 0)
                    {
                        GL11.glColorMask(false, true, true, true);
                    }
                    else
                    {
                        GL11.glColorMask(true, false, false, true);
                    }
                }
                else
                {
                    GL11.glColorMask(true, true, true, true);
                }

                GL11.glDepthMask(true);
                GL11.glEnable(GL11.GL_TEXTURE_2D);
                GL11.glEnable(GL11.GL_ALPHA_TEST);
                GL11.glEnable(GL11.GL_FOG);
                this.theWorld.theProfiler.endStartSection("render");
                k += this.renderSortedRenderers(l1, l, p_72719_2_, p_72719_3_);
            }
            while (l < this.sortedWorldRenderers.length);
        }
        else
        {
            this.theWorld.theProfiler.endStartSection("render");
            k = b1 + this.renderSortedRenderers(0, this.sortedWorldRenderers.length, p_72719_2_, p_72719_3_);
        }

        this.theWorld.theProfiler.endSection();
        return k;
    }

    private void checkOcclusionQueryResult(int p_72720_1_, int p_72720_2_)
    {
        for (int k = p_72720_1_; k < p_72720_2_; ++k)
        {
            if (this.sortedWorldRenderers[k].isWaitingOnOcclusionQuery)
            {
                this.occlusionResult.clear();
                ARBOcclusionQuery.glGetQueryObjectuARB(this.sortedWorldRenderers[k].glOcclusionQuery, ARBOcclusionQuery.GL_QUERY_RESULT_AVAILABLE_ARB, this.occlusionResult);

                if (this.occlusionResult.get(0) != 0)
                {
                    this.sortedWorldRenderers[k].isWaitingOnOcclusionQuery = false;
                    this.occlusionResult.clear();
                    ARBOcclusionQuery.glGetQueryObjectuARB(this.sortedWorldRenderers[k].glOcclusionQuery, ARBOcclusionQuery.GL_QUERY_RESULT_ARB, this.occlusionResult);
                    this.sortedWorldRenderers[k].isVisible = this.occlusionResult.get(0) != 0;
                }
            }
        }
    }

    /**
     * Renders the sorted renders for the specified render pass. Args: startRenderer, numRenderers, renderPass,
     * partialTickTime
     */
    private int renderSortedRenderers(int p_72724_1_, int p_72724_2_, int p_72724_3_, double p_72724_4_)
    {
        this.glRenderLists.clear();
        int l = 0;
        int i1 = p_72724_1_;
        int j1 = p_72724_2_;
        byte b0 = 1;

        if (p_72724_3_ == 1)
        {
            i1 = this.sortedWorldRenderers.length - 1 - p_72724_1_;
            j1 = this.sortedWorldRenderers.length - 1 - p_72724_2_;
            b0 = -1;
        }

        for (int k1 = i1; k1 != j1; k1 += b0)
        {
            if (p_72724_3_ == 0)
            {
                ++this.renderersLoaded;

                if (this.sortedWorldRenderers[k1].skipRenderPass[p_72724_3_])
                {
                    ++this.renderersSkippingRenderPass;
                }
                else if (!this.sortedWorldRenderers[k1].isInFrustum)
                {
                    ++this.renderersBeingClipped;
                }
                else if (this.occlusionEnabled && !this.sortedWorldRenderers[k1].isVisible)
                {
                    ++this.renderersBeingOccluded;
                }
                else
                {
                    ++this.renderersBeingRendered;
                }
            }

            if (!this.sortedWorldRenderers[k1].skipRenderPass[p_72724_3_] && this.sortedWorldRenderers[k1].isInFrustum && (!this.occlusionEnabled || this.sortedWorldRenderers[k1].isVisible))
            {
                int l1 = this.sortedWorldRenderers[k1].getGLCallListForPass(p_72724_3_);

                if (l1 >= 0)
                {
                    this.glRenderLists.add(this.sortedWorldRenderers[k1]);
                    ++l;
                }
            }
        }

        EntityLivingBase entitylivingbase = this.mc.renderViewEntity;
        double d3 = entitylivingbase.lastTickPosX + (entitylivingbase.posX - entitylivingbase.lastTickPosX) * p_72724_4_;
        double d1 = entitylivingbase.lastTickPosY + (entitylivingbase.posY - entitylivingbase.lastTickPosY) * p_72724_4_;
        double d2 = entitylivingbase.lastTickPosZ + (entitylivingbase.posZ - entitylivingbase.lastTickPosZ) * p_72724_4_;
        int i2 = 0;
        int j2;

        for (j2 = 0; j2 < this.allRenderLists.length; ++j2)
        {
            this.allRenderLists[j2].resetList();
        }

        int k2;
        int l2;

        for (j2 = 0; j2 < this.glRenderLists.size(); ++j2)
        {
            WorldRenderer worldrenderer = (WorldRenderer)this.glRenderLists.get(j2);
            k2 = -1;

            for (l2 = 0; l2 < i2; ++l2)
            {
                if (this.allRenderLists[l2].rendersChunk(worldrenderer.posXMinus, worldrenderer.posYMinus, worldrenderer.posZMinus))
                {
                    k2 = l2;
                }
            }

            if (k2 < 0)
            {
                k2 = i2++;
                this.allRenderLists[k2].setupRenderList(worldrenderer.posXMinus, worldrenderer.posYMinus, worldrenderer.posZMinus, d3, d1, d2);
            }

            this.allRenderLists[k2].addGLRenderList(worldrenderer.getGLCallListForPass(p_72724_3_));
        }

        j2 = MathHelper.floor_double(d3);
        int i3 = MathHelper.floor_double(d2);
        k2 = j2 - (j2 & 1023);
        l2 = i3 - (i3 & 1023);
        Arrays.sort(this.allRenderLists, new RenderDistanceSorter(k2, l2));
        this.renderAllRenderLists(p_72724_3_, p_72724_4_);
        return l;
    }

    /**
     * Render all render lists
     */
    public void renderAllRenderLists(int p_72733_1_, double p_72733_2_)
    {
        this.mc.entityRenderer.enableLightmap(p_72733_2_);

        for (int j = 0; j < this.allRenderLists.length; ++j)
        {
            this.allRenderLists[j].callLists();
        }

        this.mc.entityRenderer.disableLightmap(p_72733_2_);
    }

    public void updateClouds()
    {
        ++this.cloudTickCounter;

        if (this.cloudTickCounter % 20 == 0)
        {
            Iterator iterator = this.damagedBlocks.values().iterator();

            while (iterator.hasNext())
            {
                DestroyBlockProgress destroyblockprogress = (DestroyBlockProgress)iterator.next();
                int i = destroyblockprogress.getCreationCloudUpdateTick();

                if (this.cloudTickCounter - i > 400)
                {
                    iterator.remove();
                }
            }
        }
    }

    /**
     * Renders the sky with the partial tick time. Args: partialTickTime
     */
    public void renderSky(float p_72714_1_)
    {
        IRenderHandler skyProvider = null;
        if ((skyProvider = this.mc.theWorld.provider.getSkyRenderer()) != null)
        {
            skyProvider.render(p_72714_1_, this.theWorld, mc);
            return;
        }
        if (this.mc.theWorld.provider.dimensionId == 1)
        {
            GL11.glDisable(GL11.GL_FOG);
            GL11.glDisable(GL11.GL_ALPHA_TEST);
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            RenderHelper.disableStandardItemLighting();
            GL11.glDepthMask(false);
            this.renderEngine.bindTexture(locationEndSkyPng);
            Tessellator tessellator = Tessellator.instance;

            for (int i = 0; i < 6; ++i)
            {
                GL11.glPushMatrix();

                if (i == 1)
                {
                    GL11.glRotatef(90.0F, 1.0F, 0.0F, 0.0F);
                }

                if (i == 2)
                {
                    GL11.glRotatef(-90.0F, 1.0F, 0.0F, 0.0F);
                }

                if (i == 3)
                {
                    GL11.glRotatef(180.0F, 1.0F, 0.0F, 0.0F);
                }

                if (i == 4)
                {
                    GL11.glRotatef(90.0F, 0.0F, 0.0F, 1.0F);
                }

                if (i == 5)
                {
                    GL11.glRotatef(-90.0F, 0.0F, 0.0F, 1.0F);
                }

                tessellator.startDrawingQuads();
                tessellator.setColorOpaque_I(2631720);
                tessellator.addVertexWithUV(-100.0D, -100.0D, -100.0D, 0.0D, 0.0D);
                tessellator.addVertexWithUV(-100.0D, -100.0D, 100.0D, 0.0D, 16.0D);
                tessellator.addVertexWithUV(100.0D, -100.0D, 100.0D, 16.0D, 16.0D);
                tessellator.addVertexWithUV(100.0D, -100.0D, -100.0D, 16.0D, 0.0D);
                tessellator.draw();
                GL11.glPopMatrix();
            }

            GL11.glDepthMask(true);
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            GL11.glEnable(GL11.GL_ALPHA_TEST);
        }
        else if (this.mc.theWorld.provider.isSurfaceWorld())
        {
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            Vec3 vec3 = this.theWorld.getSkyColor(this.mc.renderViewEntity, p_72714_1_);
            float f1 = (float)vec3.xCoord;
            float f2 = (float)vec3.yCoord;
            float f3 = (float)vec3.zCoord;
            float f6;

            if (this.mc.gameSettings.anaglyph)
            {
                float f4 = (f1 * 30.0F + f2 * 59.0F + f3 * 11.0F) / 100.0F;
                float f5 = (f1 * 30.0F + f2 * 70.0F) / 100.0F;
                f6 = (f1 * 30.0F + f3 * 70.0F) / 100.0F;
                f1 = f4;
                f2 = f5;
                f3 = f6;
            }

            GL11.glColor3f(f1, f2, f3);
            Tessellator tessellator1 = Tessellator.instance;
            GL11.glDepthMask(false);
            GL11.glEnable(GL11.GL_FOG);
            GL11.glColor3f(f1, f2, f3);
            GL11.glCallList(this.glSkyList);
            GL11.glDisable(GL11.GL_FOG);
            GL11.glDisable(GL11.GL_ALPHA_TEST);
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            RenderHelper.disableStandardItemLighting();
            float[] afloat = this.theWorld.provider.calcSunriseSunsetColors(this.theWorld.getCelestialAngle(p_72714_1_), p_72714_1_);
            float f7;
            float f8;
            float f9;
            float f10;

            if (afloat != null)
            {
                GL11.glDisable(GL11.GL_TEXTURE_2D);
                GL11.glShadeModel(GL11.GL_SMOOTH);
                GL11.glPushMatrix();
                GL11.glRotatef(90.0F, 1.0F, 0.0F, 0.0F);
                GL11.glRotatef(MathHelper.sin(this.theWorld.getCelestialAngleRadians(p_72714_1_)) < 0.0F ? 180.0F : 0.0F, 0.0F, 0.0F, 1.0F);
                GL11.glRotatef(90.0F, 0.0F, 0.0F, 1.0F);
                f6 = afloat[0];
                f7 = afloat[1];
                f8 = afloat[2];
                float f11;

                if (this.mc.gameSettings.anaglyph)
                {
                    f9 = (f6 * 30.0F + f7 * 59.0F + f8 * 11.0F) / 100.0F;
                    f10 = (f6 * 30.0F + f7 * 70.0F) / 100.0F;
                    f11 = (f6 * 30.0F + f8 * 70.0F) / 100.0F;
                    f6 = f9;
                    f7 = f10;
                    f8 = f11;
                }

                tessellator1.startDrawing(6);
                tessellator1.setColorRGBA_F(f6, f7, f8, afloat[3]);
                tessellator1.addVertex(0.0D, 100.0D, 0.0D);
                byte b0 = 16;
                tessellator1.setColorRGBA_F(afloat[0], afloat[1], afloat[2], 0.0F);

                for (int j = 0; j <= b0; ++j)
                {
                    f11 = (float)j * (float)Math.PI * 2.0F / (float)b0;
                    float f12 = MathHelper.sin(f11);
                    float f13 = MathHelper.cos(f11);
                    tessellator1.addVertex((double)(f12 * 120.0F), (double)(f13 * 120.0F), (double)(-f13 * 40.0F * afloat[3]));
                }

                tessellator1.draw();
                GL11.glPopMatrix();
                GL11.glShadeModel(GL11.GL_FLAT);
            }

            GL11.glEnable(GL11.GL_TEXTURE_2D);
            OpenGlHelper.glBlendFunc(770, 1, 1, 0);
            GL11.glPushMatrix();
            f6 = 1.0F - this.theWorld.getRainStrength(p_72714_1_);
            f7 = 0.0F;
            f8 = 0.0F;
            f9 = 0.0F;
            GL11.glColor4f(1.0F, 1.0F, 1.0F, f6);
            GL11.glTranslatef(f7, f8, f9);
            GL11.glRotatef(-90.0F, 0.0F, 1.0F, 0.0F);
            GL11.glRotatef(this.theWorld.getCelestialAngle(p_72714_1_) * 360.0F, 1.0F, 0.0F, 0.0F);
            f10 = 30.0F;
            this.renderEngine.bindTexture(locationSunPng);
            tessellator1.startDrawingQuads();
            tessellator1.addVertexWithUV((double)(-f10), 100.0D, (double)(-f10), 0.0D, 0.0D);
            tessellator1.addVertexWithUV((double)f10, 100.0D, (double)(-f10), 1.0D, 0.0D);
            tessellator1.addVertexWithUV((double)f10, 100.0D, (double)f10, 1.0D, 1.0D);
            tessellator1.addVertexWithUV((double)(-f10), 100.0D, (double)f10, 0.0D, 1.0D);
            tessellator1.draw();
            f10 = 20.0F;
            this.renderEngine.bindTexture(locationMoonPhasesPng);
            int k = this.theWorld.getMoonPhase();
            int l = k % 4;
            int i1 = k / 4 % 2;
            float f14 = (float)(l + 0) / 4.0F;
            float f15 = (float)(i1 + 0) / 2.0F;
            float f16 = (float)(l + 1) / 4.0F;
            float f17 = (float)(i1 + 1) / 2.0F;
            tessellator1.startDrawingQuads();
            tessellator1.addVertexWithUV((double)(-f10), -100.0D, (double)f10, (double)f16, (double)f17);
            tessellator1.addVertexWithUV((double)f10, -100.0D, (double)f10, (double)f14, (double)f17);
            tessellator1.addVertexWithUV((double)f10, -100.0D, (double)(-f10), (double)f14, (double)f15);
            tessellator1.addVertexWithUV((double)(-f10), -100.0D, (double)(-f10), (double)f16, (double)f15);
            tessellator1.draw();
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            float f18 = this.theWorld.getStarBrightness(p_72714_1_) * f6;

            if (f18 > 0.0F)
            {
                GL11.glColor4f(f18, f18, f18, f18);
                GL11.glCallList(this.starGLCallList);
            }

            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            GL11.glDisable(GL11.GL_BLEND);
            GL11.glEnable(GL11.GL_ALPHA_TEST);
            GL11.glEnable(GL11.GL_FOG);
            GL11.glPopMatrix();
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            GL11.glColor3f(0.0F, 0.0F, 0.0F);
            double d0 = this.mc.thePlayer.getPosition(p_72714_1_).yCoord - this.theWorld.getHorizon();

            if (d0 < 0.0D)
            {
                GL11.glPushMatrix();
                GL11.glTranslatef(0.0F, 12.0F, 0.0F);
                GL11.glCallList(this.glSkyList2);
                GL11.glPopMatrix();
                f8 = 1.0F;
                f9 = -((float)(d0 + 65.0D));
                f10 = -f8;
                tessellator1.startDrawingQuads();
                tessellator1.setColorRGBA_I(0, 255);
                tessellator1.addVertex((double)(-f8), (double)f9, (double)f8);
                tessellator1.addVertex((double)f8, (double)f9, (double)f8);
                tessellator1.addVertex((double)f8, (double)f10, (double)f8);
                tessellator1.addVertex((double)(-f8), (double)f10, (double)f8);
                tessellator1.addVertex((double)(-f8), (double)f10, (double)(-f8));
                tessellator1.addVertex((double)f8, (double)f10, (double)(-f8));
                tessellator1.addVertex((double)f8, (double)f9, (double)(-f8));
                tessellator1.addVertex((double)(-f8), (double)f9, (double)(-f8));
                tessellator1.addVertex((double)f8, (double)f10, (double)(-f8));
                tessellator1.addVertex((double)f8, (double)f10, (double)f8);
                tessellator1.addVertex((double)f8, (double)f9, (double)f8);
                tessellator1.addVertex((double)f8, (double)f9, (double)(-f8));
                tessellator1.addVertex((double)(-f8), (double)f9, (double)(-f8));
                tessellator1.addVertex((double)(-f8), (double)f9, (double)f8);
                tessellator1.addVertex((double)(-f8), (double)f10, (double)f8);
                tessellator1.addVertex((double)(-f8), (double)f10, (double)(-f8));
                tessellator1.addVertex((double)(-f8), (double)f10, (double)(-f8));
                tessellator1.addVertex((double)(-f8), (double)f10, (double)f8);
                tessellator1.addVertex((double)f8, (double)f10, (double)f8);
                tessellator1.addVertex((double)f8, (double)f10, (double)(-f8));
                tessellator1.draw();
            }

            if (this.theWorld.provider.isSkyColored())
            {
                GL11.glColor3f(f1 * 0.2F + 0.04F, f2 * 0.2F + 0.04F, f3 * 0.6F + 0.1F);
            }
            else
            {
                GL11.glColor3f(f1, f2, f3);
            }

            GL11.glPushMatrix();
            GL11.glTranslatef(0.0F, -((float)(d0 - 16.0D)), 0.0F);
            GL11.glCallList(this.glSkyList2);
            GL11.glPopMatrix();
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            GL11.glDepthMask(true);
        }
    }

    public void renderClouds(float p_72718_1_)
    {
        IRenderHandler renderer = null;
        if ((renderer = theWorld.provider.getCloudRenderer()) != null)
        {
            renderer.render(p_72718_1_, theWorld, mc);
            return;
        }
        if (this.mc.theWorld.provider.isSurfaceWorld())
        {
            if (this.mc.gameSettings.fancyGraphics)
            {
                this.renderCloudsFancy(p_72718_1_);
            }
            else
            {
                GL11.glDisable(GL11.GL_CULL_FACE);
                float f1 = (float)(this.mc.renderViewEntity.lastTickPosY + (this.mc.renderViewEntity.posY - this.mc.renderViewEntity.lastTickPosY) * (double)p_72718_1_);
                byte b0 = 32;
                int i = 256 / b0;
                Tessellator tessellator = Tessellator.instance;
                this.renderEngine.bindTexture(locationCloudsPng);
                GL11.glEnable(GL11.GL_BLEND);
                OpenGlHelper.glBlendFunc(770, 771, 1, 0);
                Vec3 vec3 = this.theWorld.getCloudColour(p_72718_1_);
                float f2 = (float)vec3.xCoord;
                float f3 = (float)vec3.yCoord;
                float f4 = (float)vec3.zCoord;
                float f5;

                if (this.mc.gameSettings.anaglyph)
                {
                    f5 = (f2 * 30.0F + f3 * 59.0F + f4 * 11.0F) / 100.0F;
                    float f6 = (f2 * 30.0F + f3 * 70.0F) / 100.0F;
                    float f7 = (f2 * 30.0F + f4 * 70.0F) / 100.0F;
                    f2 = f5;
                    f3 = f6;
                    f4 = f7;
                }

                f5 = 4.8828125E-4F;
                double d2 = (double)((float)this.cloudTickCounter + p_72718_1_);
                double d0 = this.mc.renderViewEntity.prevPosX + (this.mc.renderViewEntity.posX - this.mc.renderViewEntity.prevPosX) * (double)p_72718_1_ + d2 * 0.029999999329447746D;
                double d1 = this.mc.renderViewEntity.prevPosZ + (this.mc.renderViewEntity.posZ - this.mc.renderViewEntity.prevPosZ) * (double)p_72718_1_;
                int j = MathHelper.floor_double(d0 / 2048.0D);
                int k = MathHelper.floor_double(d1 / 2048.0D);
                d0 -= (double)(j * 2048);
                d1 -= (double)(k * 2048);
                float f8 = this.theWorld.provider.getCloudHeight() - f1 + 0.33F;
                float f9 = (float)(d0 * (double)f5);
                float f10 = (float)(d1 * (double)f5);
                tessellator.startDrawingQuads();
                tessellator.setColorRGBA_F(f2, f3, f4, 0.8F);

                for (int l = -b0 * i; l < b0 * i; l += b0)
                {
                    for (int i1 = -b0 * i; i1 < b0 * i; i1 += b0)
                    {
                        tessellator.addVertexWithUV((double)(l + 0), (double)f8, (double)(i1 + b0), (double)((float)(l + 0) * f5 + f9), (double)((float)(i1 + b0) * f5 + f10));
                        tessellator.addVertexWithUV((double)(l + b0), (double)f8, (double)(i1 + b0), (double)((float)(l + b0) * f5 + f9), (double)((float)(i1 + b0) * f5 + f10));
                        tessellator.addVertexWithUV((double)(l + b0), (double)f8, (double)(i1 + 0), (double)((float)(l + b0) * f5 + f9), (double)((float)(i1 + 0) * f5 + f10));
                        tessellator.addVertexWithUV((double)(l + 0), (double)f8, (double)(i1 + 0), (double)((float)(l + 0) * f5 + f9), (double)((float)(i1 + 0) * f5 + f10));
                    }
                }

                tessellator.draw();
                GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
                GL11.glDisable(GL11.GL_BLEND);
                GL11.glEnable(GL11.GL_CULL_FACE);
            }
        }
    }

    /**
     * Checks if the given position is to be rendered with cloud fog
     */
    public boolean hasCloudFog(double p_72721_1_, double p_72721_3_, double p_72721_5_, float p_72721_7_)
    {
        return false;
    }

    /**
     * Renders the 3d fancy clouds
     */
    public void renderCloudsFancy(float p_72736_1_)
    {
        GL11.glDisable(GL11.GL_CULL_FACE);
        float f1 = (float)(this.mc.renderViewEntity.lastTickPosY + (this.mc.renderViewEntity.posY - this.mc.renderViewEntity.lastTickPosY) * (double)p_72736_1_);
        Tessellator tessellator = Tessellator.instance;
        float f2 = 12.0F;
        float f3 = 4.0F;
        double d0 = (double)((float)this.cloudTickCounter + p_72736_1_);
        double d1 = (this.mc.renderViewEntity.prevPosX + (this.mc.renderViewEntity.posX - this.mc.renderViewEntity.prevPosX) * (double)p_72736_1_ + d0 * 0.029999999329447746D) / (double)f2;
        double d2 = (this.mc.renderViewEntity.prevPosZ + (this.mc.renderViewEntity.posZ - this.mc.renderViewEntity.prevPosZ) * (double)p_72736_1_) / (double)f2 + 0.33000001311302185D;
        float f4 = this.theWorld.provider.getCloudHeight() - f1 + 0.33F;
        int i = MathHelper.floor_double(d1 / 2048.0D);
        int j = MathHelper.floor_double(d2 / 2048.0D);
        d1 -= (double)(i * 2048);
        d2 -= (double)(j * 2048);
        this.renderEngine.bindTexture(locationCloudsPng);
        GL11.glEnable(GL11.GL_BLEND);
        OpenGlHelper.glBlendFunc(770, 771, 1, 0);
        Vec3 vec3 = this.theWorld.getCloudColour(p_72736_1_);
        float f5 = (float)vec3.xCoord;
        float f6 = (float)vec3.yCoord;
        float f7 = (float)vec3.zCoord;
        float f8;
        float f9;
        float f10;

        if (this.mc.gameSettings.anaglyph)
        {
            f8 = (f5 * 30.0F + f6 * 59.0F + f7 * 11.0F) / 100.0F;
            f9 = (f5 * 30.0F + f6 * 70.0F) / 100.0F;
            f10 = (f5 * 30.0F + f7 * 70.0F) / 100.0F;
            f5 = f8;
            f6 = f9;
            f7 = f10;
        }

        f8 = (float)(d1 * 0.0D);
        f9 = (float)(d2 * 0.0D);
        f10 = 0.00390625F;
        f8 = (float)MathHelper.floor_double(d1) * f10;
        f9 = (float)MathHelper.floor_double(d2) * f10;
        float f11 = (float)(d1 - (double)MathHelper.floor_double(d1));
        float f12 = (float)(d2 - (double)MathHelper.floor_double(d2));
        byte b0 = 8;
        byte b1 = 4;
        float f13 = 9.765625E-4F;
        GL11.glScalef(f2, 1.0F, f2);

        for (int k = 0; k < 2; ++k)
        {
            if (k == 0)
            {
                GL11.glColorMask(false, false, false, false);
            }
            else if (this.mc.gameSettings.anaglyph)
            {
                if (EntityRenderer.anaglyphField == 0)
                {
                    GL11.glColorMask(false, true, true, true);
                }
                else
                {
                    GL11.glColorMask(true, false, false, true);
                }
            }
            else
            {
                GL11.glColorMask(true, true, true, true);
            }

            for (int l = -b1 + 1; l <= b1; ++l)
            {
                for (int i1 = -b1 + 1; i1 <= b1; ++i1)
                {
                    tessellator.startDrawingQuads();
                    float f14 = (float)(l * b0);
                    float f15 = (float)(i1 * b0);
                    float f16 = f14 - f11;
                    float f17 = f15 - f12;

                    if (f4 > -f3 - 1.0F)
                    {
                        tessellator.setColorRGBA_F(f5 * 0.7F, f6 * 0.7F, f7 * 0.7F, 0.8F);
                        tessellator.setNormal(0.0F, -1.0F, 0.0F);
                        tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + (float)b0), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + 0.0F), (double)(f17 + (float)b0), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + 0.0F), (double)(f17 + 0.0F), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + 0.0F), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                    }

                    if (f4 <= f3 + 1.0F)
                    {
                        tessellator.setColorRGBA_F(f5, f6, f7, 0.8F);
                        tessellator.setNormal(0.0F, 1.0F, 0.0F);
                        tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + f3 - f13), (double)(f17 + (float)b0), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + f3 - f13), (double)(f17 + (float)b0), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + f3 - f13), (double)(f17 + 0.0F), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                        tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + f3 - f13), (double)(f17 + 0.0F), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                    }

                    tessellator.setColorRGBA_F(f5 * 0.9F, f6 * 0.9F, f7 * 0.9F, 0.8F);
                    int j1;

                    if (l > -1)
                    {
                        tessellator.setNormal(-1.0F, 0.0F, 0.0F);

                        for (j1 = 0; j1 < b0; ++j1)
                        {
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + (float)b0), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 0.0F), (double)(f4 + f3), (double)(f17 + (float)b0), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 0.0F), (double)(f4 + f3), (double)(f17 + 0.0F), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + 0.0F), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                        }
                    }

                    if (l <= 1)
                    {
                        tessellator.setNormal(1.0F, 0.0F, 0.0F);

                        for (j1 = 0; j1 < b0; ++j1)
                        {
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 1.0F - f13), (double)(f4 + 0.0F), (double)(f17 + (float)b0), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 1.0F - f13), (double)(f4 + f3), (double)(f17 + (float)b0), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + (float)b0) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 1.0F - f13), (double)(f4 + f3), (double)(f17 + 0.0F), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)j1 + 1.0F - f13), (double)(f4 + 0.0F), (double)(f17 + 0.0F), (double)((f14 + (float)j1 + 0.5F) * f10 + f8), (double)((f15 + 0.0F) * f10 + f9));
                        }
                    }

                    tessellator.setColorRGBA_F(f5 * 0.8F, f6 * 0.8F, f7 * 0.8F, 0.8F);

                    if (i1 > -1)
                    {
                        tessellator.setNormal(0.0F, 0.0F, -1.0F);

                        for (j1 = 0; j1 < b0; ++j1)
                        {
                            tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + f3), (double)(f17 + (float)j1 + 0.0F), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + f3), (double)(f17 + (float)j1 + 0.0F), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + 0.0F), (double)(f17 + (float)j1 + 0.0F), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + (float)j1 + 0.0F), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                        }
                    }

                    if (i1 <= 1)
                    {
                        tessellator.setNormal(0.0F, 0.0F, 1.0F);

                        for (j1 = 0; j1 < b0; ++j1)
                        {
                            tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + f3), (double)(f17 + (float)j1 + 1.0F - f13), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + f3), (double)(f17 + (float)j1 + 1.0F - f13), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + (float)b0), (double)(f4 + 0.0F), (double)(f17 + (float)j1 + 1.0F - f13), (double)((f14 + (float)b0) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                            tessellator.addVertexWithUV((double)(f16 + 0.0F), (double)(f4 + 0.0F), (double)(f17 + (float)j1 + 1.0F - f13), (double)((f14 + 0.0F) * f10 + f8), (double)((f15 + (float)j1 + 0.5F) * f10 + f9));
                        }
                    }

                    tessellator.draw();
                }
            }
        }

        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        GL11.glDisable(GL11.GL_BLEND);
        GL11.glEnable(GL11.GL_CULL_FACE);
    }

    /**
     * Updates some of the renderers sorted by distance from the player
     */
    public boolean updateRenderers(EntityLivingBase p_72716_1_, boolean p_72716_2_)
    {
        byte b0 = 2;
        RenderSorter rendersorter = new RenderSorter(p_72716_1_);
        WorldRenderer[] aworldrenderer = new WorldRenderer[b0];
        ArrayList arraylist = null;
        int i = this.worldRenderersToUpdate.size();
        int j = 0;
        this.theWorld.theProfiler.startSection("nearChunksSearch");
        int k;
        WorldRenderer worldrenderer;
        int l;
        int i1;
        label136:

        for (k = 0; k < i; ++k)
        {
            worldrenderer = (WorldRenderer)this.worldRenderersToUpdate.get(k);

            if (worldrenderer != null)
            {
                if (!p_72716_2_)
                {
                    if (worldrenderer.distanceToEntitySquared(p_72716_1_) > 272.0F)
                    {
                        for (l = 0; l < b0 && (aworldrenderer[l] == null || rendersorter.compare(aworldrenderer[l], worldrenderer) <= 0); ++l)
                        {
                            ;
                        }

                        --l;

                        if (l > 0)
                        {
                            i1 = l;

                            while (true)
                            {
                                --i1;

                                if (i1 == 0)
                                {
                                    aworldrenderer[l] = worldrenderer;
                                    continue label136;
                                }

                                aworldrenderer[i1 - 1] = aworldrenderer[i1];
                            }
                        }

                        continue;
                    }
                }
                else if (!worldrenderer.isInFrustum)
                {
                    continue;
                }

                if (arraylist == null)
                {
                    arraylist = new ArrayList();
                }

                ++j;
                arraylist.add(worldrenderer);
                this.worldRenderersToUpdate.set(k, (Object)null);
            }
        }

        this.theWorld.theProfiler.endSection();
        this.theWorld.theProfiler.startSection("sort");

        if (arraylist != null)
        {
            if (arraylist.size() > 1)
            {
                Collections.sort(arraylist, rendersorter);
            }

            for (k = arraylist.size() - 1; k >= 0; --k)
            {
                worldrenderer = (WorldRenderer)arraylist.get(k);
                worldrenderer.updateRenderer(p_72716_1_);
                worldrenderer.needsUpdate = false;
            }
        }

        this.theWorld.theProfiler.endSection();
        k = 0;
        this.theWorld.theProfiler.startSection("rebuild");
        int k1;

        for (k1 = b0 - 1; k1 >= 0; --k1)
        {
            WorldRenderer worldrenderer2 = aworldrenderer[k1];

            if (worldrenderer2 != null)
            {
                if (!worldrenderer2.isInFrustum && k1 != b0 - 1)
                {
                    aworldrenderer[k1] = null;
                    aworldrenderer[0] = null;
                    break;
                }

                aworldrenderer[k1].updateRenderer(p_72716_1_);
                aworldrenderer[k1].needsUpdate = false;
                ++k;
            }
        }

        this.theWorld.theProfiler.endSection();
        this.theWorld.theProfiler.startSection("cleanup");
        k1 = 0;
        l = 0;

        for (i1 = this.worldRenderersToUpdate.size(); k1 != i1; ++k1)
        {
            WorldRenderer worldrenderer1 = (WorldRenderer)this.worldRenderersToUpdate.get(k1);

            if (worldrenderer1 != null)
            {
                boolean flag1 = false;

                for (int j1 = 0; j1 < b0 && !flag1; ++j1)
                {
                    if (worldrenderer1 == aworldrenderer[j1])
                    {
                        flag1 = true;
                    }
                }

                if (!flag1)
                {
                    if (l != k1)
                    {
                        this.worldRenderersToUpdate.set(l, worldrenderer1);
                    }

                    ++l;
                }
            }
        }

        this.theWorld.theProfiler.endSection();
        this.theWorld.theProfiler.startSection("trim");

        while (true)
        {
            --k1;

            if (k1 < l)
            {
                this.theWorld.theProfiler.endSection();
                return i == j + k;
            }

            this.worldRenderersToUpdate.remove(k1);
        }
    }

    public void drawBlockDamageTexture(Tessellator p_72717_1_, EntityPlayer p_72717_2_, float p_72717_3_)
    {
        drawBlockDamageTexture(p_72717_1_, (EntityLivingBase)p_72717_2_, p_72717_3_);
    }

    public void drawBlockDamageTexture(Tessellator p_72717_1_, EntityLivingBase p_72717_2_, float p_72717_3_)
    {
        double d0 = p_72717_2_.lastTickPosX + (p_72717_2_.posX - p_72717_2_.lastTickPosX) * (double)p_72717_3_;
        double d1 = p_72717_2_.lastTickPosY + (p_72717_2_.posY - p_72717_2_.lastTickPosY) * (double)p_72717_3_;
        double d2 = p_72717_2_.lastTickPosZ + (p_72717_2_.posZ - p_72717_2_.lastTickPosZ) * (double)p_72717_3_;

        if (!this.damagedBlocks.isEmpty())
        {
            OpenGlHelper.glBlendFunc(774, 768, 1, 0);
            this.renderEngine.bindTexture(TextureMap.locationBlocksTexture);
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 0.5F);
            GL11.glPushMatrix();
            GL11.glPolygonOffset(-3.0F, -3.0F);
            GL11.glEnable(GL11.GL_POLYGON_OFFSET_FILL);
            GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
            GL11.glEnable(GL11.GL_ALPHA_TEST);
            p_72717_1_.startDrawingQuads();
            p_72717_1_.setTranslation(-d0, -d1, -d2);
            p_72717_1_.disableColor();
            Iterator iterator = this.damagedBlocks.values().iterator();

            while (iterator.hasNext())
            {
                DestroyBlockProgress destroyblockprogress = (DestroyBlockProgress)iterator.next();
                double d3 = (double)destroyblockprogress.getPartialBlockX() - d0;
                double d4 = (double)destroyblockprogress.getPartialBlockY() - d1;
                double d5 = (double)destroyblockprogress.getPartialBlockZ() - d2;

                if (d3 * d3 + d4 * d4 + d5 * d5 > 1024.0D)
                {
                    iterator.remove();
                }
                else
                {
                    Block block = this.theWorld.getBlock(destroyblockprogress.getPartialBlockX(), destroyblockprogress.getPartialBlockY(), destroyblockprogress.getPartialBlockZ());

                    if (block.getMaterial() != Material.air)
                    {
                        this.renderBlocksRg.renderBlockUsingTexture(block, destroyblockprogress.getPartialBlockX(), destroyblockprogress.getPartialBlockY(), destroyblockprogress.getPartialBlockZ(), this.destroyBlockIcons[destroyblockprogress.getPartialBlockDamage()]);
                    }
                }
            }

            p_72717_1_.draw();
            p_72717_1_.setTranslation(0.0D, 0.0D, 0.0D);
            GL11.glDisable(GL11.GL_ALPHA_TEST);
            GL11.glPolygonOffset(0.0F, 0.0F);
            GL11.glDisable(GL11.GL_POLYGON_OFFSET_FILL);
            GL11.glEnable(GL11.GL_ALPHA_TEST);
            GL11.glDepthMask(true);
            GL11.glPopMatrix();
        }
    }

    /**
     * Draws the selection box for the player. Args: entityPlayer, rayTraceHit, i, itemStack, partialTickTime
     */
    public void drawSelectionBox(EntityPlayer p_72731_1_, MovingObjectPosition p_72731_2_, int p_72731_3_, float p_72731_4_)
    {
        if (p_72731_3_ == 0 && p_72731_2_.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK)
        {
            GL11.glEnable(GL11.GL_BLEND);
            OpenGlHelper.glBlendFunc(770, 771, 1, 0);
            GL11.glColor4f(0.0F, 0.0F, 0.0F, 0.4F);
            GL11.glLineWidth(2.0F);
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            GL11.glDepthMask(false);
            float f1 = 0.002F;
            Block block = this.theWorld.getBlock(p_72731_2_.blockX, p_72731_2_.blockY, p_72731_2_.blockZ);

            if (block.getMaterial() != Material.air)
            {
                block.setBlockBoundsBasedOnState(this.theWorld, p_72731_2_.blockX, p_72731_2_.blockY, p_72731_2_.blockZ);
                double d0 = p_72731_1_.lastTickPosX + (p_72731_1_.posX - p_72731_1_.lastTickPosX) * (double)p_72731_4_;
                double d1 = p_72731_1_.lastTickPosY + (p_72731_1_.posY - p_72731_1_.lastTickPosY) * (double)p_72731_4_;
                double d2 = p_72731_1_.lastTickPosZ + (p_72731_1_.posZ - p_72731_1_.lastTickPosZ) * (double)p_72731_4_;
                drawOutlinedBoundingBox(block.getSelectedBoundingBoxFromPool(this.theWorld, p_72731_2_.blockX, p_72731_2_.blockY, p_72731_2_.blockZ).expand((double)f1, (double)f1, (double)f1).getOffsetBoundingBox(-d0, -d1, -d2), -1);
            }

            GL11.glDepthMask(true);
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            GL11.glDisable(GL11.GL_BLEND);
        }
    }

    /**
     * Draws lines for the edges of the bounding box.
     */
    public static void drawOutlinedBoundingBox(AxisAlignedBB p_147590_0_, int p_147590_1_)
    {
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawing(3);

        if (p_147590_1_ != -1)
        {
            tessellator.setColorOpaque_I(p_147590_1_);
        }

        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.minY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.minY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.minY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.minY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.minY, p_147590_0_.minZ);
        tessellator.draw();
        tessellator.startDrawing(3);

        if (p_147590_1_ != -1)
        {
            tessellator.setColorOpaque_I(p_147590_1_);
        }

        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.maxY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.maxY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.maxY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.maxY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.maxY, p_147590_0_.minZ);
        tessellator.draw();
        tessellator.startDrawing(1);

        if (p_147590_1_ != -1)
        {
            tessellator.setColorOpaque_I(p_147590_1_);
        }

        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.minY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.maxY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.minY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.maxY, p_147590_0_.minZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.minY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.maxX, p_147590_0_.maxY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.minY, p_147590_0_.maxZ);
        tessellator.addVertex(p_147590_0_.minX, p_147590_0_.maxY, p_147590_0_.maxZ);
        tessellator.draw();
    }

    /**
     * Marks the blocks in the given range for update
     */
    public void markBlocksForUpdate(int p_72725_1_, int p_72725_2_, int p_72725_3_, int p_72725_4_, int p_72725_5_, int p_72725_6_)
    {
        int k1 = MathHelper.bucketInt(p_72725_1_, 16);
        int l1 = MathHelper.bucketInt(p_72725_2_, 16);
        int i2 = MathHelper.bucketInt(p_72725_3_, 16);
        int j2 = MathHelper.bucketInt(p_72725_4_, 16);
        int k2 = MathHelper.bucketInt(p_72725_5_, 16);
        int l2 = MathHelper.bucketInt(p_72725_6_, 16);

        for (int i3 = k1; i3 <= j2; ++i3)
        {
            int j3 = i3 % this.renderChunksWide;

            if (j3 < 0)
            {
                j3 += this.renderChunksWide;
            }

            for (int k3 = l1; k3 <= k2; ++k3)
            {
                int l3 = k3 % this.renderChunksTall;

                if (l3 < 0)
                {
                    l3 += this.renderChunksTall;
                }

                for (int i4 = i2; i4 <= l2; ++i4)
                {
                    int j4 = i4 % this.renderChunksDeep;

                    if (j4 < 0)
                    {
                        j4 += this.renderChunksDeep;
                    }

                    int k4 = (j4 * this.renderChunksTall + l3) * this.renderChunksWide + j3;
                    WorldRenderer worldrenderer = this.worldRenderers[k4];

                    if (worldrenderer != null && !worldrenderer.needsUpdate)
                    {
                        this.worldRenderersToUpdate.add(worldrenderer);
                        worldrenderer.markDirty();
                    }
                }
            }
        }
    }

    /**
     * On the client, re-renders the block. On the server, sends the block to the client (which will re-render it),
     * including the tile entity description packet if applicable. Args: x, y, z
     */
    public void markBlockForUpdate(int p_147586_1_, int p_147586_2_, int p_147586_3_)
    {
        this.markBlocksForUpdate(p_147586_1_ - 1, p_147586_2_ - 1, p_147586_3_ - 1, p_147586_1_ + 1, p_147586_2_ + 1, p_147586_3_ + 1);
    }

    /**
     * On the client, re-renders this block. On the server, does nothing. Used for lighting updates.
     */
    public void markBlockForRenderUpdate(int p_147588_1_, int p_147588_2_, int p_147588_3_)
    {
        this.markBlocksForUpdate(p_147588_1_ - 1, p_147588_2_ - 1, p_147588_3_ - 1, p_147588_1_ + 1, p_147588_2_ + 1, p_147588_3_ + 1);
    }

    /**
     * On the client, re-renders all blocks in this range, inclusive. On the server, does nothing. Args: min x, min y,
     * min z, max x, max y, max z
     */
    public void markBlockRangeForRenderUpdate(int p_147585_1_, int p_147585_2_, int p_147585_3_, int p_147585_4_, int p_147585_5_, int p_147585_6_)
    {
        this.markBlocksForUpdate(p_147585_1_ - 1, p_147585_2_ - 1, p_147585_3_ - 1, p_147585_4_ + 1, p_147585_5_ + 1, p_147585_6_ + 1);
    }

    /**
     * Checks all renderers that previously weren't in the frustum and 1/16th of those that previously were in the
     * frustum for frustum clipping Args: frustum, partialTickTime
     */
    public void clipRenderersByFrustum(ICamera p_72729_1_, float p_72729_2_)
    {
        for (int i = 0; i < this.worldRenderers.length; ++i)
        {
            if (!this.worldRenderers[i].skipAllRenderPasses() && (!this.worldRenderers[i].isInFrustum || (i + this.frustumCheckOffset & 15) == 0))
            {
                this.worldRenderers[i].updateInFrustum(p_72729_1_);
            }
        }

        ++this.frustumCheckOffset;
    }

    /**
     * Plays the specified record. Arg: recordName, x, y, z
     */
    public void playRecord(String p_72702_1_, int p_72702_2_, int p_72702_3_, int p_72702_4_)
    {
        ChunkCoordinates chunkcoordinates = new ChunkCoordinates(p_72702_2_, p_72702_3_, p_72702_4_);
        ISound isound = (ISound)this.mapSoundPositions.get(chunkcoordinates);

        if (isound != null)
        {
            this.mc.getSoundHandler().stopSound(isound);
            this.mapSoundPositions.remove(chunkcoordinates);
        }

        if (p_72702_1_ != null)
        {
            ItemRecord itemrecord = ItemRecord.getRecord(p_72702_1_);

            ResourceLocation resource = null;
            if (itemrecord != null)
            {
                this.mc.ingameGUI.setRecordPlayingMessage(itemrecord.getRecordNameLocal());
                resource = itemrecord.getRecordResource(p_72702_1_);
            }

            if (resource == null) resource = new ResourceLocation(p_72702_1_);
            PositionedSoundRecord positionedsoundrecord = PositionedSoundRecord.func_147675_a(resource, (float)p_72702_2_, (float)p_72702_3_, (float)p_72702_4_);
            this.mapSoundPositions.put(chunkcoordinates, positionedsoundrecord);
            this.mc.getSoundHandler().playSound(positionedsoundrecord);
        }
    }

    /**
     * Plays the specified sound. Arg: soundName, x, y, z, volume, pitch
     */
    public void playSound(String p_72704_1_, double p_72704_2_, double p_72704_4_, double p_72704_6_, float p_72704_8_, float p_72704_9_) {}

    /**
     * Plays sound to all near players except the player reference given
     */
    public void playSoundToNearExcept(EntityPlayer p_85102_1_, String p_85102_2_, double p_85102_3_, double p_85102_5_, double p_85102_7_, float p_85102_9_, float p_85102_10_) {}

    /**
     * Spawns a particle. Arg: particleType, x, y, z, velX, velY, velZ
     */
    public void spawnParticle(String p_72708_1_, final double p_72708_2_, final double p_72708_4_, final double p_72708_6_, double p_72708_8_, double p_72708_10_, double p_72708_12_)
    {
        try
        {
            this.doSpawnParticle(p_72708_1_, p_72708_2_, p_72708_4_, p_72708_6_, p_72708_8_, p_72708_10_, p_72708_12_);
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Exception while adding particle");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Particle being added");
            crashreportcategory.addCrashSection("Name", p_72708_1_);
            crashreportcategory.addCrashSectionCallable("Position", new Callable()
            {
                private static final String __OBFID = "CL_00000955";
                public String call()
                {
                    return CrashReportCategory.func_85074_a(p_72708_2_, p_72708_4_, p_72708_6_);
                }
            });
            throw new ReportedException(crashreport);
        }
    }

    /**
     * Spawns a particle. Arg: particleType, x, y, z, velX, velY, velZ
     */
    public EntityFX doSpawnParticle(String p_72726_1_, double p_72726_2_, double p_72726_4_, double p_72726_6_, double p_72726_8_, double p_72726_10_, double p_72726_12_)
    {
        if (this.mc != null && this.mc.renderViewEntity != null && this.mc.effectRenderer != null)
        {
            int i = this.mc.gameSettings.particleSetting;

            if (i == 1 && this.theWorld.rand.nextInt(3) == 0)
            {
                i = 2;
            }

            double d6 = this.mc.renderViewEntity.posX - p_72726_2_;
            double d7 = this.mc.renderViewEntity.posY - p_72726_4_;
            double d8 = this.mc.renderViewEntity.posZ - p_72726_6_;
            EntityFX entityfx = null;

            if (p_72726_1_.equals("hugeexplosion"))
            {
                this.mc.effectRenderer.addEffect(entityfx = new EntityHugeExplodeFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_));
            }
            else if (p_72726_1_.equals("largeexplode"))
            {
                this.mc.effectRenderer.addEffect(entityfx = new EntityLargeExplodeFX(this.renderEngine, this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_));
            }
            else if (p_72726_1_.equals("fireworksSpark"))
            {
                this.mc.effectRenderer.addEffect(entityfx = new EntityFireworkSparkFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, this.mc.effectRenderer));
            }

            if (entityfx != null)
            {
                return (EntityFX)entityfx;
            }
            else
            {
                double d9 = 16.0D;

                if (d6 * d6 + d7 * d7 + d8 * d8 > d9 * d9)
                {
                    return null;
                }
                else if (i > 1)
                {
                    return null;
                }
                else
                {
                    if (p_72726_1_.equals("bubble"))
                    {
                        entityfx = new EntityBubbleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("suspended"))
                    {
                        entityfx = new EntitySuspendFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("depthsuspend"))
                    {
                        entityfx = new EntityAuraFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("townaura"))
                    {
                        entityfx = new EntityAuraFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("crit"))
                    {
                        entityfx = new EntityCritFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("magicCrit"))
                    {
                        entityfx = new EntityCritFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                        ((EntityFX)entityfx).setRBGColorF(((EntityFX)entityfx).getRedColorF() * 0.3F, ((EntityFX)entityfx).getGreenColorF() * 0.8F, ((EntityFX)entityfx).getBlueColorF());
                        ((EntityFX)entityfx).nextTextureIndexX();
                    }
                    else if (p_72726_1_.equals("smoke"))
                    {
                        entityfx = new EntitySmokeFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("mobSpell"))
                    {
                        entityfx = new EntitySpellParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, 0.0D, 0.0D, 0.0D);
                        ((EntityFX)entityfx).setRBGColorF((float)p_72726_8_, (float)p_72726_10_, (float)p_72726_12_);
                    }
                    else if (p_72726_1_.equals("mobSpellAmbient"))
                    {
                        entityfx = new EntitySpellParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, 0.0D, 0.0D, 0.0D);
                        ((EntityFX)entityfx).setAlphaF(0.15F);
                        ((EntityFX)entityfx).setRBGColorF((float)p_72726_8_, (float)p_72726_10_, (float)p_72726_12_);
                    }
                    else if (p_72726_1_.equals("spell"))
                    {
                        entityfx = new EntitySpellParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("instantSpell"))
                    {
                        entityfx = new EntitySpellParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                        ((EntitySpellParticleFX)entityfx).setBaseSpellTextureIndex(144);
                    }
                    else if (p_72726_1_.equals("witchMagic"))
                    {
                        entityfx = new EntitySpellParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                        ((EntitySpellParticleFX)entityfx).setBaseSpellTextureIndex(144);
                        float f = this.theWorld.rand.nextFloat() * 0.5F + 0.35F;
                        ((EntityFX)entityfx).setRBGColorF(1.0F * f, 0.0F * f, 1.0F * f);
                    }
                    else if (p_72726_1_.equals("note"))
                    {
                        entityfx = new EntityNoteFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("portal"))
                    {
                        entityfx = new EntityPortalFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("enchantmenttable"))
                    {
                        entityfx = new EntityEnchantmentTableParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("explode"))
                    {
                        entityfx = new EntityExplodeFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("flame"))
                    {
                        entityfx = new EntityFlameFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("lava"))
                    {
                        entityfx = new EntityLavaFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_);
                    }
                    else if (p_72726_1_.equals("footstep"))
                    {
                        entityfx = new EntityFootStepFX(this.renderEngine, this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_);
                    }
                    else if (p_72726_1_.equals("splash"))
                    {
                        entityfx = new EntitySplashFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("wake"))
                    {
                        entityfx = new EntityFishWakeFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("largesmoke"))
                    {
                        entityfx = new EntitySmokeFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, 2.5F);
                    }
                    else if (p_72726_1_.equals("cloud"))
                    {
                        entityfx = new EntityCloudFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("reddust"))
                    {
                        entityfx = new EntityReddustFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, (float)p_72726_8_, (float)p_72726_10_, (float)p_72726_12_);
                    }
                    else if (p_72726_1_.equals("snowballpoof"))
                    {
                        entityfx = new EntityBreakingFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, Items.snowball);
                    }
                    else if (p_72726_1_.equals("dripWater"))
                    {
                        entityfx = new EntityDropParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, Material.water);
                    }
                    else if (p_72726_1_.equals("dripLava"))
                    {
                        entityfx = new EntityDropParticleFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, Material.lava);
                    }
                    else if (p_72726_1_.equals("snowshovel"))
                    {
                        entityfx = new EntitySnowShovelFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("slime"))
                    {
                        entityfx = new EntityBreakingFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, Items.slime_ball);
                    }
                    else if (p_72726_1_.equals("heart"))
                    {
                        entityfx = new EntityHeartFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                    }
                    else if (p_72726_1_.equals("angryVillager"))
                    {
                        entityfx = new EntityHeartFX(this.theWorld, p_72726_2_, p_72726_4_ + 0.5D, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                        ((EntityFX)entityfx).setParticleTextureIndex(81);
                        ((EntityFX)entityfx).setRBGColorF(1.0F, 1.0F, 1.0F);
                    }
                    else if (p_72726_1_.equals("happyVillager"))
                    {
                        entityfx = new EntityAuraFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_);
                        ((EntityFX)entityfx).setParticleTextureIndex(82);
                        ((EntityFX)entityfx).setRBGColorF(1.0F, 1.0F, 1.0F);
                    }
                    else
                    {
                        int k;
                        String[] astring;

                        if (p_72726_1_.startsWith("iconcrack_"))
                        {
                            astring = p_72726_1_.split("_", 3);
                            int j = Integer.parseInt(astring[1]);

                            if (astring.length > 2)
                            {
                                k = Integer.parseInt(astring[2]);
                                entityfx = new EntityBreakingFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, Item.getItemById(j), k);
                            }
                            else
                            {
                                entityfx = new EntityBreakingFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, Item.getItemById(j), 0);
                            }
                        }
                        else
                        {
                            Block block;

                            if (p_72726_1_.startsWith("blockcrack_"))
                            {
                                astring = p_72726_1_.split("_", 3);
                                block = Block.getBlockById(Integer.parseInt(astring[1]));
                                k = Integer.parseInt(astring[2]);
                                entityfx = (new EntityDiggingFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, block, k)).applyRenderColor(k);
                            }
                            else if (p_72726_1_.startsWith("blockdust_"))
                            {
                                astring = p_72726_1_.split("_", 3);
                                block = Block.getBlockById(Integer.parseInt(astring[1]));
                                k = Integer.parseInt(astring[2]);
                                entityfx = (new EntityBlockDustFX(this.theWorld, p_72726_2_, p_72726_4_, p_72726_6_, p_72726_8_, p_72726_10_, p_72726_12_, block, k)).applyRenderColor(k);
                            }
                        }
                    }

                    if (entityfx != null)
                    {
                        this.mc.effectRenderer.addEffect((EntityFX)entityfx);
                    }

                    return (EntityFX)entityfx;
                }
            }
        }
        else
        {
            return null;
        }
    }

    /**
     * Called on all IWorldAccesses when an entity is created or loaded. On client worlds, starts downloading any
     * necessary textures. On server worlds, adds the entity to the entity tracker.
     */
    public void onEntityCreate(Entity p_72703_1_) {}

    /**
     * Called on all IWorldAccesses when an entity is unloaded or destroyed. On client worlds, releases any downloaded
     * textures. On server worlds, removes the entity from the entity tracker.
     */
    public void onEntityDestroy(Entity p_72709_1_) {}

    /**
     * Deletes all display lists
     */
    public void deleteAllDisplayLists()
    {
        GLAllocation.deleteDisplayLists(this.glRenderListBase);
    }

    public void broadcastSound(int p_82746_1_, int p_82746_2_, int p_82746_3_, int p_82746_4_, int p_82746_5_)
    {
        Random random = this.theWorld.rand;

        switch (p_82746_1_)
        {
            case 1013:
            case 1018:
                if (this.mc.renderViewEntity != null)
                {
                    double d0 = (double)p_82746_2_ - this.mc.renderViewEntity.posX;
                    double d1 = (double)p_82746_3_ - this.mc.renderViewEntity.posY;
                    double d2 = (double)p_82746_4_ - this.mc.renderViewEntity.posZ;
                    double d3 = Math.sqrt(d0 * d0 + d1 * d1 + d2 * d2);
                    double d4 = this.mc.renderViewEntity.posX;
                    double d5 = this.mc.renderViewEntity.posY;
                    double d6 = this.mc.renderViewEntity.posZ;

                    if (d3 > 0.0D)
                    {
                        d4 += d0 / d3 * 2.0D;
                        d5 += d1 / d3 * 2.0D;
                        d6 += d2 / d3 * 2.0D;
                    }

                    if (p_82746_1_ == 1013)
                    {
                        this.theWorld.playSound(d4, d5, d6, "mob.wither.spawn", 1.0F, 1.0F, false);
                    }
                    else if (p_82746_1_ == 1018)
                    {
                        this.theWorld.playSound(d4, d5, d6, "mob.enderdragon.end", 5.0F, 1.0F, false);
                    }
                }
            default:
        }
    }

    /**
     * Plays a pre-canned sound effect along with potentially auxiliary data-driven one-shot behaviour (particles, etc).
     */
    public void playAuxSFX(EntityPlayer p_72706_1_, int p_72706_2_, int p_72706_3_, int p_72706_4_, int p_72706_5_, int p_72706_6_)
    {
        Random random = this.theWorld.rand;
        Block block = null;
        double d0;
        double d1;
        double d2;
        String s;
        int k1;
        double d4;
        double d5;
        double d6;
        double d7;
        int l2;
        double d13;

        switch (p_72706_2_)
        {
            case 1000:
                this.theWorld.playSound((double)p_72706_3_, (double)p_72706_4_, (double)p_72706_5_, "random.click", 1.0F, 1.0F, false);
                break;
            case 1001:
                this.theWorld.playSound((double)p_72706_3_, (double)p_72706_4_, (double)p_72706_5_, "random.click", 1.0F, 1.2F, false);
                break;
            case 1002:
                this.theWorld.playSound((double)p_72706_3_, (double)p_72706_4_, (double)p_72706_5_, "random.bow", 1.0F, 1.2F, false);
                break;
            case 1003:
                if (Math.random() < 0.5D)
                {
                    this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "random.door_open", 1.0F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                }
                else
                {
                    this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "random.door_close", 1.0F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                }

                break;
            case 1004:
                this.theWorld.playSound((double)((float)p_72706_3_ + 0.5F), (double)((float)p_72706_4_ + 0.5F), (double)((float)p_72706_5_ + 0.5F), "random.fizz", 0.5F, 2.6F + (random.nextFloat() - random.nextFloat()) * 0.8F, false);
                break;
            case 1005:
                if (Item.getItemById(p_72706_6_) instanceof ItemRecord)
                {
                    this.theWorld.playRecord("records." + ((ItemRecord)Item.getItemById(p_72706_6_)).recordName, p_72706_3_, p_72706_4_, p_72706_5_);
                }
                else
                {
                    this.theWorld.playRecord((String)null, p_72706_3_, p_72706_4_, p_72706_5_);
                }

                break;
            case 1007:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.ghast.charge", 10.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1008:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.ghast.fireball", 10.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1009:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.ghast.fireball", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1010:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.zombie.wood", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1011:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.zombie.metal", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1012:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.zombie.woodbreak", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1014:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.wither.shoot", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1015:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.bat.takeoff", 0.05F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1016:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.zombie.infect", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1017:
                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "mob.zombie.unfect", 2.0F, (random.nextFloat() - random.nextFloat()) * 0.2F + 1.0F, false);
                break;
            case 1020:
                this.theWorld.playSound((double)((float)p_72706_3_ + 0.5F), (double)((float)p_72706_4_ + 0.5F), (double)((float)p_72706_5_ + 0.5F), "random.anvil_break", 1.0F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                break;
            case 1021:
                this.theWorld.playSound((double)((float)p_72706_3_ + 0.5F), (double)((float)p_72706_4_ + 0.5F), (double)((float)p_72706_5_ + 0.5F), "random.anvil_use", 1.0F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                break;
            case 1022:
                this.theWorld.playSound((double)((float)p_72706_3_ + 0.5F), (double)((float)p_72706_4_ + 0.5F), (double)((float)p_72706_5_ + 0.5F), "random.anvil_land", 0.3F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                break;
            case 2000:
                int j2 = p_72706_6_ % 3 - 1;
                int j1 = p_72706_6_ / 3 % 3 - 1;
                d1 = (double)p_72706_3_ + (double)j2 * 0.6D + 0.5D;
                d2 = (double)p_72706_4_ + 0.5D;
                double d9 = (double)p_72706_5_ + (double)j1 * 0.6D + 0.5D;

                for (int k2 = 0; k2 < 10; ++k2)
                {
                    double d11 = random.nextDouble() * 0.2D + 0.01D;
                    double d12 = d1 + (double)j2 * 0.01D + (random.nextDouble() - 0.5D) * (double)j1 * 0.5D;
                    d4 = d2 + (random.nextDouble() - 0.5D) * 0.5D;
                    d13 = d9 + (double)j1 * 0.01D + (random.nextDouble() - 0.5D) * (double)j2 * 0.5D;
                    d5 = (double)j2 * d11 + random.nextGaussian() * 0.01D;
                    d6 = -0.03D + random.nextGaussian() * 0.01D;
                    d7 = (double)j1 * d11 + random.nextGaussian() * 0.01D;
                    this.spawnParticle("smoke", d12, d4, d13, d5, d6, d7);
                }

                return;
            case 2001:
                block = Block.getBlockById(p_72706_6_ & 4095);

                if (block.getMaterial() != Material.air)
                {
                    this.mc.getSoundHandler().playSound(new PositionedSoundRecord(new ResourceLocation(block.stepSound.getBreakSound()), (block.stepSound.getVolume() + 1.0F) / 2.0F, block.stepSound.getPitch() * 0.8F, (float)p_72706_3_ + 0.5F, (float)p_72706_4_ + 0.5F, (float)p_72706_5_ + 0.5F));
                }

                this.mc.effectRenderer.addBlockDestroyEffects(p_72706_3_, p_72706_4_, p_72706_5_, block, p_72706_6_ >> 12 & 255);
                break;
            case 2002:
                d0 = (double)p_72706_3_;
                d1 = (double)p_72706_4_;
                d2 = (double)p_72706_5_;
                s = "iconcrack_" + Item.getIdFromItem(Items.potionitem) + "_" + p_72706_6_;

                for (k1 = 0; k1 < 8; ++k1)
                {
                    this.spawnParticle(s, d0, d1, d2, random.nextGaussian() * 0.15D, random.nextDouble() * 0.2D, random.nextGaussian() * 0.15D);
                }

                k1 = Items.potionitem.getColorFromDamage(p_72706_6_);
                float f = (float)(k1 >> 16 & 255) / 255.0F;
                float f1 = (float)(k1 >> 8 & 255) / 255.0F;
                float f2 = (float)(k1 >> 0 & 255) / 255.0F;
                String s1 = "spell";

                if (Items.potionitem.isEffectInstant(p_72706_6_))
                {
                    s1 = "instantSpell";
                }

                for (l2 = 0; l2 < 100; ++l2)
                {
                    d4 = random.nextDouble() * 4.0D;
                    d13 = random.nextDouble() * Math.PI * 2.0D;
                    d5 = Math.cos(d13) * d4;
                    d6 = 0.01D + random.nextDouble() * 0.5D;
                    d7 = Math.sin(d13) * d4;
                    EntityFX entityfx = this.doSpawnParticle(s1, d0 + d5 * 0.1D, d1 + 0.3D, d2 + d7 * 0.1D, d5, d6, d7);

                    if (entityfx != null)
                    {
                        float f4 = 0.75F + random.nextFloat() * 0.25F;
                        entityfx.setRBGColorF(f * f4, f1 * f4, f2 * f4);
                        entityfx.multiplyVelocity((float)d4);
                    }
                }

                this.theWorld.playSound((double)p_72706_3_ + 0.5D, (double)p_72706_4_ + 0.5D, (double)p_72706_5_ + 0.5D, "game.potion.smash", 1.0F, this.theWorld.rand.nextFloat() * 0.1F + 0.9F, false);
                break;
            case 2003:
                d0 = (double)p_72706_3_ + 0.5D;
                d1 = (double)p_72706_4_;
                d2 = (double)p_72706_5_ + 0.5D;
                s = "iconcrack_" + Item.getIdFromItem(Items.ender_eye);

                for (k1 = 0; k1 < 8; ++k1)
                {
                    this.spawnParticle(s, d0, d1, d2, random.nextGaussian() * 0.15D, random.nextDouble() * 0.2D, random.nextGaussian() * 0.15D);
                }

                for (double d10 = 0.0D; d10 < (Math.PI * 2D); d10 += 0.15707963267948966D)
                {
                    this.spawnParticle("portal", d0 + Math.cos(d10) * 5.0D, d1 - 0.4D, d2 + Math.sin(d10) * 5.0D, Math.cos(d10) * -5.0D, 0.0D, Math.sin(d10) * -5.0D);
                    this.spawnParticle("portal", d0 + Math.cos(d10) * 5.0D, d1 - 0.4D, d2 + Math.sin(d10) * 5.0D, Math.cos(d10) * -7.0D, 0.0D, Math.sin(d10) * -7.0D);
                }

                return;
            case 2004:
                for (l2 = 0; l2 < 20; ++l2)
                {
                    d4 = (double)p_72706_3_ + 0.5D + ((double)this.theWorld.rand.nextFloat() - 0.5D) * 2.0D;
                    d13 = (double)p_72706_4_ + 0.5D + ((double)this.theWorld.rand.nextFloat() - 0.5D) * 2.0D;
                    d5 = (double)p_72706_5_ + 0.5D + ((double)this.theWorld.rand.nextFloat() - 0.5D) * 2.0D;
                    this.theWorld.spawnParticle("smoke", d4, d13, d5, 0.0D, 0.0D, 0.0D);
                    this.theWorld.spawnParticle("flame", d4, d13, d5, 0.0D, 0.0D, 0.0D);
                }

                return;
            case 2005:
                ItemDye.func_150918_a(this.theWorld, p_72706_3_, p_72706_4_, p_72706_5_, p_72706_6_);
                break;
            case 2006:
                block = this.theWorld.getBlock(p_72706_3_, p_72706_4_, p_72706_5_);

                if (block.getMaterial() != Material.air)
                {
                    double d3 = (double)Math.min(0.2F + (float)p_72706_6_ / 15.0F, 10.0F);

                    if (d3 > 2.5D)
                    {
                        d3 = 2.5D;
                    }

                    int l1 = (int)(150.0D * d3);

                    for (int i2 = 0; i2 < l1; ++i2)
                    {
                        float f3 = MathHelper.randomFloatClamp(random, 0.0F, ((float)Math.PI * 2F));
                        d5 = (double)MathHelper.randomFloatClamp(random, 0.75F, 1.0F);
                        d6 = 0.20000000298023224D + d3 / 100.0D;
                        d7 = (double)(MathHelper.cos(f3) * 0.2F) * d5 * d5 * (d3 + 0.2D);
                        double d8 = (double)(MathHelper.sin(f3) * 0.2F) * d5 * d5 * (d3 + 0.2D);
                        this.theWorld.spawnParticle("blockdust_" + Block.getIdFromBlock(block) + "_" + this.theWorld.getBlockMetadata(p_72706_3_, p_72706_4_, p_72706_5_), (double)((float)p_72706_3_ + 0.5F), (double)((float)p_72706_4_ + 1.0F), (double)((float)p_72706_5_ + 0.5F), d7, d6, d8);
                    }
                }
        }
    }

    /**
     * Starts (or continues) destroying a block with given ID at the given coordinates for the given partially destroyed
     * value
     */
    public void destroyBlockPartially(int p_147587_1_, int p_147587_2_, int p_147587_3_, int p_147587_4_, int p_147587_5_)
    {
        if (p_147587_5_ >= 0 && p_147587_5_ < 10)
        {
            DestroyBlockProgress destroyblockprogress = (DestroyBlockProgress)this.damagedBlocks.get(Integer.valueOf(p_147587_1_));

            if (destroyblockprogress == null || destroyblockprogress.getPartialBlockX() != p_147587_2_ || destroyblockprogress.getPartialBlockY() != p_147587_3_ || destroyblockprogress.getPartialBlockZ() != p_147587_4_)
            {
                destroyblockprogress = new DestroyBlockProgress(p_147587_1_, p_147587_2_, p_147587_3_, p_147587_4_);
                this.damagedBlocks.put(Integer.valueOf(p_147587_1_), destroyblockprogress);
            }

            destroyblockprogress.setPartialBlockDamage(p_147587_5_);
            destroyblockprogress.setCloudUpdateTick(this.cloudTickCounter);
        }
        else
        {
            this.damagedBlocks.remove(Integer.valueOf(p_147587_1_));
        }
    }

    public void registerDestroyBlockIcons(IIconRegister p_94140_1_)
    {
        this.destroyBlockIcons = new IIcon[10];

        for (int i = 0; i < this.destroyBlockIcons.length; ++i)
        {
            this.destroyBlockIcons[i] = p_94140_1_.registerIcon("destroy_stage_" + i);
        }
    }
}