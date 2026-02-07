package net.minecraft.client;

import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.collect.Multimap;
import com.google.common.collect.Queues;
import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.common.util.concurrent.ListenableFutureTask;
import com.mojang.authlib.minecraft.MinecraftSessionService;
import com.mojang.authlib.yggdrasil.YggdrasilAuthenticationService;
import cpw.mods.fml.client.FMLClientHandler;
import cpw.mods.fml.common.FMLCommonHandler;
import cpw.mods.fml.common.StartupQuery;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.util.concurrent.GenericFutureListener;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.Proxy;
import java.net.SocketAddress;
import java.nio.ByteBuffer;
import java.text.DecimalFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Queue;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.Executors;
import java.util.concurrent.FutureTask;
import javax.imageio.ImageIO;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.audio.MusicTicker;
import net.minecraft.client.audio.SoundHandler;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.gui.FontRenderer;
import net.minecraft.client.gui.GuiChat;
import net.minecraft.client.gui.GuiControls;
import net.minecraft.client.gui.GuiGameOver;
import net.minecraft.client.gui.GuiIngame;
import net.minecraft.client.gui.GuiIngameMenu;
import net.minecraft.client.gui.GuiMainMenu;
import net.minecraft.client.gui.GuiMemoryErrorScreen;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiSleepMP;
import net.minecraft.client.gui.GuiWinGame;
import net.minecraft.client.gui.GuiYesNo;
import net.minecraft.client.gui.GuiYesNoCallback;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.gui.achievement.GuiAchievement;
import net.minecraft.client.gui.inventory.GuiInventory;
import net.minecraft.client.gui.stream.GuiStreamUnavailable;
import net.minecraft.client.multiplayer.GuiConnecting;
import net.minecraft.client.multiplayer.PlayerControllerMP;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.network.NetHandlerLoginClient;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.client.particle.EffectRenderer;
import net.minecraft.client.renderer.EntityRenderer;
import net.minecraft.client.renderer.GLAllocation;
import net.minecraft.client.renderer.ItemRenderer;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.RenderGlobal;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.WorldRenderer;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.client.resources.DefaultResourcePack;
import net.minecraft.client.resources.FoliageColorReloadListener;
import net.minecraft.client.resources.GrassColorReloadListener;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.resources.IReloadableResourceManager;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.client.resources.LanguageManager;
import net.minecraft.client.resources.ResourceIndex;
import net.minecraft.client.resources.ResourcePackRepository;
import net.minecraft.client.resources.SimpleReloadableResourceManager;
import net.minecraft.client.resources.SkinManager;
import net.minecraft.client.resources.data.AnimationMetadataSection;
import net.minecraft.client.resources.data.AnimationMetadataSectionSerializer;
import net.minecraft.client.resources.data.FontMetadataSection;
import net.minecraft.client.resources.data.FontMetadataSectionSerializer;
import net.minecraft.client.resources.data.IMetadataSerializer;
import net.minecraft.client.resources.data.LanguageMetadataSection;
import net.minecraft.client.resources.data.LanguageMetadataSectionSerializer;
import net.minecraft.client.resources.data.PackMetadataSection;
import net.minecraft.client.resources.data.PackMetadataSectionSerializer;
import net.minecraft.client.resources.data.TextureMetadataSection;
import net.minecraft.client.resources.data.TextureMetadataSectionSerializer;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.settings.KeyBinding;
import net.minecraft.client.shader.Framebuffer;
import net.minecraft.client.stream.IStream;
import net.minecraft.client.stream.NullStream;
import net.minecraft.client.stream.TwitchStream;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLeashKnot;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.boss.BossStatus;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemStack;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.handshake.client.C00Handshake;
import net.minecraft.network.login.client.C00PacketLoginStart;
import net.minecraft.network.play.client.C16PacketClientStatus;
import net.minecraft.profiler.IPlayerUsage;
import net.minecraft.profiler.PlayerUsageSnooper;
import net.minecraft.profiler.Profiler;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.IStatStringFormat;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MinecraftError;
import net.minecraft.util.MouseHelper;
import net.minecraft.util.MovementInputFromOptions;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.ScreenShotHelper;
import net.minecraft.util.Session;
import net.minecraft.util.Timer;
import net.minecraft.util.Util;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.WorldProviderEnd;
import net.minecraft.world.WorldProviderHell;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.chunk.storage.AnvilSaveConverter;
import net.minecraft.world.storage.ISaveFormat;
import net.minecraft.world.storage.ISaveHandler;
import net.minecraft.world.storage.WorldInfo;
import org.apache.commons.lang3.Validate;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.LWJGLException;
import org.lwjgl.Sys;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.ContextCapabilities;
import org.lwjgl.opengl.Display;
import org.lwjgl.opengl.DisplayMode;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GLContext;
import org.lwjgl.opengl.OpenGLException;
import org.lwjgl.opengl.PixelFormat;
import org.lwjgl.util.glu.GLU;

import com.google.common.collect.MapDifference;

@SideOnly(Side.CLIENT)
public class Minecraft implements IPlayerUsage
{
    private static final Logger logger = LogManager.getLogger();
    private static final ResourceLocation locationMojangPng = new ResourceLocation("textures/gui/title/mojang.png");
    public static final boolean isRunningOnMac = Util.getOSType() == Util.EnumOS.OSX;
    /** A 10MiB preallocation to ensure the heap is reasonably sized. */
    public static byte[] memoryReserve = new byte[10485760];
    private static final List macDisplayModes = Lists.newArrayList(new DisplayMode[] {new DisplayMode(2560, 1600), new DisplayMode(2880, 1800)});
    private final File fileResourcepacks;
    private final Multimap field_152356_J;
    private ServerData currentServerData;
    /** The RenderEngine instance used by Minecraft */
    public TextureManager renderEngine;
    /** Set to 'this' in Minecraft constructor; used by some settings get methods */
    private static Minecraft theMinecraft;
    public PlayerControllerMP playerController;
    private boolean fullscreen;
    private boolean hasCrashed;
    /** Instance of CrashReport. */
    private CrashReport crashReporter;
    public int displayWidth;
    public int displayHeight;
    private Timer timer = new Timer(20.0F);
    /** Instance of PlayerUsageSnooper. */
    private PlayerUsageSnooper usageSnooper = new PlayerUsageSnooper("client", this, MinecraftServer.getSystemTimeMillis());
    public WorldClient theWorld;
    public RenderGlobal renderGlobal;
    public EntityClientPlayerMP thePlayer;
    /**
     * The Entity from which the renderer determines the render viewpoint. Currently is always the parent Minecraft
     * class's 'thePlayer' instance. Modification of its location, rotation, or other settings at render time will
     * modify the camera likewise, with the caveat of triggering chunk rebuilds as it moves, making it unsuitable for
     * changing the viewpoint mid-render.
     */
    public EntityLivingBase renderViewEntity;
    public Entity pointedEntity;
    public EffectRenderer effectRenderer;
    private final Session session;
    private boolean isGamePaused;
    /** The font renderer used for displaying and measuring text. */
    public FontRenderer fontRenderer;
    public FontRenderer standardGalacticFontRenderer;
    /** The GuiScreen that's being displayed at the moment. */
    public GuiScreen currentScreen;
    public LoadingScreenRenderer loadingScreen;
    public EntityRenderer entityRenderer;
    /** Mouse left click counter */
    private int leftClickCounter;
    /** Display width */
    private int tempDisplayWidth;
    /** Display height */
    private int tempDisplayHeight;
    /** Instance of IntegratedServer. */
    private IntegratedServer theIntegratedServer;
    /** Gui achievement */
    public GuiAchievement guiAchievement;
    public GuiIngame ingameGUI;
    /** Skip render world */
    public boolean skipRenderWorld;
    /** The ray trace hit that the mouse is over. */
    public MovingObjectPosition objectMouseOver;
    /** The game settings that currently hold effect. */
    public GameSettings gameSettings;
    /** Mouse helper instance. */
    public MouseHelper mouseHelper;
    public final File mcDataDir;
    private final File fileAssets;
    private final String launchedVersion;
    private final Proxy proxy;
    private ISaveFormat saveLoader;
    /**
     * This is set to fpsCounter every debug screen update, and is shown on the debug screen. It's also sent as part of
     * the usage snooping.
     */
    private static int debugFPS;
    /** When you place a block, it's set to 6, decremented once per tick, when it's 0, you can place another block. */
    private int rightClickDelayTimer;
    /** Checked in Minecraft's while(running) loop, if true it's set to false and the textures refreshed. */
    private boolean refreshTexturePacksScheduled;
    private String serverName;
    private int serverPort;
    /** Does the actual gameplay have focus. If so then mouse and keys will effect the player instead of menus. */
    public boolean inGameHasFocus;
    long systemTime = getSystemTime();
    /** Join player counter */
    private int joinPlayerCounter;
    private final boolean jvm64bit;
    private final boolean isDemo;
    private NetworkManager myNetworkManager;
    private boolean integratedServerIsRunning;
    /** The profiler instance */
    public final Profiler mcProfiler = new Profiler();
    private long field_83002_am = -1L;
    private IReloadableResourceManager mcResourceManager;
    private final IMetadataSerializer metadataSerializer_ = new IMetadataSerializer();
    private List defaultResourcePacks = Lists.newArrayList();
    public DefaultResourcePack mcDefaultResourcePack;
    private ResourcePackRepository mcResourcePackRepository;
    private LanguageManager mcLanguageManager;
    private IStream field_152353_at;
    private Framebuffer framebufferMc;
    private TextureMap textureMapBlocks;
    private SoundHandler mcSoundHandler;
    private MusicTicker mcMusicTicker;
    private ResourceLocation field_152354_ay;
    private final MinecraftSessionService field_152355_az;
    private SkinManager field_152350_aA;
    private final Queue field_152351_aB = Queues.newArrayDeque();
    private final Thread field_152352_aC = Thread.currentThread();
    /** Set to true to keep the game loop running. Set to false by shutdown() to allow the game loop to exit cleanly. */
    volatile boolean running = true;
    /** String that shows the debug information */
    public String debug = "";
    /** Approximate time (in ms) of last update to debug string */
    long debugUpdateTime = getSystemTime();
    /** holds the current fps */
    int fpsCounter;
    long prevFrameTime = -1L;
    /** Profiler currently displayed in the debug screen pie chart */
    private String debugProfilerName = "root";
    private static final String __OBFID = "CL_00000631";

    public Minecraft(Session p_i1103_1_, int p_i1103_2_, int p_i1103_3_, boolean p_i1103_4_, boolean p_i1103_5_, File p_i1103_6_, File p_i1103_7_, File p_i1103_8_, Proxy p_i1103_9_, String p_i1103_10_, Multimap p_i1103_11_, String p_i1103_12_)
    {
        theMinecraft = this;
        this.mcDataDir = p_i1103_6_;
        this.fileAssets = p_i1103_7_;
        this.fileResourcepacks = p_i1103_8_;
        this.launchedVersion = p_i1103_10_;
        this.field_152356_J = p_i1103_11_;
        this.mcDefaultResourcePack = new DefaultResourcePack((new ResourceIndex(p_i1103_7_, p_i1103_12_)).func_152782_a());
        this.addDefaultResourcePack();
        this.proxy = p_i1103_9_ == null ? Proxy.NO_PROXY : p_i1103_9_;
        this.field_152355_az = (new YggdrasilAuthenticationService(p_i1103_9_, UUID.randomUUID().toString())).createMinecraftSessionService();
        this.startTimerHackThread();
        this.session = p_i1103_1_;
        logger.info("Setting user: " + p_i1103_1_.getUsername());
        this.isDemo = p_i1103_5_;
        this.displayWidth = p_i1103_2_;
        this.displayHeight = p_i1103_3_;
        this.tempDisplayWidth = p_i1103_2_;
        this.tempDisplayHeight = p_i1103_3_;
        this.fullscreen = p_i1103_4_;
        this.jvm64bit = isJvm64bit();
        ImageIO.setUseCache(false);
        Bootstrap.func_151354_b();
    }

    private static boolean isJvm64bit()
    {
        String[] astring = new String[] {"sun.arch.data.model", "com.ibm.vm.bitmode", "os.arch"};
        String[] astring1 = astring;
        int i = astring.length;

        for (int j = 0; j < i; ++j)
        {
            String s = astring1[j];
            String s1 = System.getProperty(s);

            if (s1 != null && s1.contains("64"))
            {
                return true;
            }
        }

        return false;
    }

    public Framebuffer getFramebuffer()
    {
        return this.framebufferMc;
    }

    private void startTimerHackThread()
    {
        Thread thread = new Thread("Timer hack thread")
        {
            private static final String __OBFID = "CL_00000632";
            public void run()
            {
                while (Minecraft.this.running)
                {
                    try
                    {
                        Thread.sleep(2147483647L);
                    }
                    catch (InterruptedException interruptedexception)
                    {
                        ;
                    }
                }
            }
        };
        thread.setDaemon(true);
        thread.start();
    }

    public void crashed(CrashReport p_71404_1_)
    {
        this.hasCrashed = true;
        this.crashReporter = p_71404_1_;
    }

    /**
     * Wrapper around displayCrashReportInternal
     */
    public void displayCrashReport(CrashReport p_71377_1_)
    {
        File file1 = new File(getMinecraft().mcDataDir, "crash-reports");
        File file2 = new File(file1, "crash-" + (new SimpleDateFormat("yyyy-MM-dd_HH.mm.ss")).format(new Date()) + "-client.txt");
        System.out.println(p_71377_1_.getCompleteReport());

        int retVal;
        if (p_71377_1_.getFile() != null)
        {
            System.out.println("#@!@# Game crashed! Crash report saved to: #@!@# " + p_71377_1_.getFile());
            retVal = -1;
        }
        else if (p_71377_1_.saveToFile(file2))
        {
            System.out.println("#@!@# Game crashed! Crash report saved to: #@!@# " + file2.getAbsolutePath());
            retVal = -1;
        }
        else
        {
            System.out.println("#@?@# Game crashed! Crash report could not be saved. #@?@#");
            retVal = -2;
        }
        FMLCommonHandler.instance().handleExit(retVal);
    }

    public void setServer(String p_71367_1_, int p_71367_2_)
    {
        this.serverName = p_71367_1_;
        this.serverPort = p_71367_2_;
    }

    /**
     * Starts the game: initializes the canvas, the title, the settings, etcetera.
     */
    private void startGame() throws LWJGLException
    {
        this.gameSettings = new GameSettings(this, this.mcDataDir);

        if (this.gameSettings.overrideHeight > 0 && this.gameSettings.overrideWidth > 0)
        {
            this.displayWidth = this.gameSettings.overrideWidth;
            this.displayHeight = this.gameSettings.overrideHeight;
        }

        if (this.fullscreen)
        {
            Display.setFullscreen(true);
            this.displayWidth = Display.getDisplayMode().getWidth();
            this.displayHeight = Display.getDisplayMode().getHeight();

            if (this.displayWidth <= 0)
            {
                this.displayWidth = 1;
            }

            if (this.displayHeight <= 0)
            {
                this.displayHeight = 1;
            }
        }
        else
        {
            Display.setDisplayMode(new DisplayMode(this.displayWidth, this.displayHeight));
        }

        Display.setResizable(true);
        Display.setTitle("Minecraft 1.7.10");
        logger.info("LWJGL Version: " + Sys.getVersion());
        Util.EnumOS enumos = Util.getOSType();

        if (enumos != Util.EnumOS.OSX)
        {
            try
            {
                InputStream inputstream = this.mcDefaultResourcePack.func_152780_c(new ResourceLocation("icons/icon_16x16.png"));
                InputStream inputstream1 = this.mcDefaultResourcePack.func_152780_c(new ResourceLocation("icons/icon_32x32.png"));

                if (inputstream != null && inputstream1 != null)
                {
                    Display.setIcon(new ByteBuffer[] {this.func_152340_a(inputstream), this.func_152340_a(inputstream1)});
                }
            }
            catch (IOException ioexception)
            {
                logger.error("Couldn\'t set icon", ioexception);
            }
        }

        try
        {
            net.minecraftforge.client.ForgeHooksClient.createDisplay();
        }
        catch (LWJGLException lwjglexception)
        {
            logger.error("Couldn\'t set pixel format", lwjglexception);

            try
            {
                Thread.sleep(1000L);
            }
            catch (InterruptedException interruptedexception)
            {
                ;
            }

            if (this.fullscreen)
            {
                this.updateDisplayMode();
            }

            Display.create();
        }

        OpenGlHelper.initializeTextures();

        try
        {
            this.field_152353_at = new TwitchStream(this, (String)Iterables.getFirst(this.field_152356_J.get("twitch_access_token"), (Object)null));
        }
        catch (Throwable throwable)
        {
            this.field_152353_at = new NullStream(throwable);
            logger.error("Couldn\'t initialize twitch stream");
        }

        this.framebufferMc = new Framebuffer(this.displayWidth, this.displayHeight, true);
        this.framebufferMc.setFramebufferColor(0.0F, 0.0F, 0.0F, 0.0F);
        this.guiAchievement = new GuiAchievement(this);
        this.metadataSerializer_.registerMetadataSectionType(new TextureMetadataSectionSerializer(), TextureMetadataSection.class);
        this.metadataSerializer_.registerMetadataSectionType(new FontMetadataSectionSerializer(), FontMetadataSection.class);
        this.metadataSerializer_.registerMetadataSectionType(new AnimationMetadataSectionSerializer(), AnimationMetadataSection.class);
        this.metadataSerializer_.registerMetadataSectionType(new PackMetadataSectionSerializer(), PackMetadataSection.class);
        this.metadataSerializer_.registerMetadataSectionType(new LanguageMetadataSectionSerializer(), LanguageMetadataSection.class);
        this.saveLoader = new AnvilSaveConverter(new File(this.mcDataDir, "saves"));
        this.mcResourcePackRepository = new ResourcePackRepository(this.fileResourcepacks, new File(this.mcDataDir, "server-resource-packs"), this.mcDefaultResourcePack, this.metadataSerializer_, this.gameSettings);
        this.mcResourceManager = new SimpleReloadableResourceManager(this.metadataSerializer_);
        this.mcLanguageManager = new LanguageManager(this.metadataSerializer_, this.gameSettings.language);
        this.mcResourceManager.registerReloadListener(this.mcLanguageManager);
        FMLClientHandler.instance().beginMinecraftLoading(this, this.defaultResourcePacks, this.mcResourceManager);
        this.renderEngine = new TextureManager(this.mcResourceManager);
        this.mcResourceManager.registerReloadListener(this.renderEngine);
        this.field_152350_aA = new SkinManager(this.renderEngine, new File(this.fileAssets, "skins"), this.field_152355_az);
        cpw.mods.fml.client.SplashProgress.drawVanillaScreen();
        this.mcSoundHandler = new SoundHandler(this.mcResourceManager, this.gameSettings);
        this.mcResourceManager.registerReloadListener(this.mcSoundHandler);
        this.mcMusicTicker = new MusicTicker(this);
        this.fontRenderer = new FontRenderer(this.gameSettings, new ResourceLocation("textures/font/ascii.png"), this.renderEngine, false);

        if (this.gameSettings.language != null)
        {
            this.fontRenderer.setUnicodeFlag(this.func_152349_b());
            this.fontRenderer.setBidiFlag(this.mcLanguageManager.isCurrentLanguageBidirectional());
        }

        this.standardGalacticFontRenderer = new FontRenderer(this.gameSettings, new ResourceLocation("textures/font/ascii_sga.png"), this.renderEngine, false);
        this.mcResourceManager.registerReloadListener(this.fontRenderer);
        this.mcResourceManager.registerReloadListener(this.standardGalacticFontRenderer);
        this.mcResourceManager.registerReloadListener(new GrassColorReloadListener());
        this.mcResourceManager.registerReloadListener(new FoliageColorReloadListener());
        cpw.mods.fml.common.ProgressManager.ProgressBar bar= cpw.mods.fml.common.ProgressManager.push("Rendering Setup", 9, true);
        bar.step("Loading Render Manager");
        RenderManager.instance.itemRenderer = new ItemRenderer(this);
        bar.step("Loading Entity Renderer");
        this.entityRenderer = new EntityRenderer(this, this.mcResourceManager);
        this.mcResourceManager.registerReloadListener(this.entityRenderer);
        AchievementList.openInventory.setStatStringFormatter(new IStatStringFormat()
        {
            private static final String __OBFID = "CL_00000639";
            /**
             * Formats the strings based on 'IStatStringFormat' interface.
             */
            public String formatString(String p_74535_1_)
            {
                try
                {
                    return String.format(p_74535_1_, new Object[] {GameSettings.getKeyDisplayString(Minecraft.this.gameSettings.keyBindInventory.getKeyCode())});
                }
                catch (Exception exception)
                {
                    return "Error: " + exception.getLocalizedMessage();
                }
            }
        });
        bar.step("Loading GL properties");
        this.mouseHelper = new MouseHelper();
        this.checkGLError("Pre startup");
        GL11.glEnable(GL11.GL_TEXTURE_2D);
        GL11.glShadeModel(GL11.GL_SMOOTH);
        GL11.glClearDepth(1.0D);
        GL11.glEnable(GL11.GL_DEPTH_TEST);
        GL11.glDepthFunc(GL11.GL_LEQUAL);
        GL11.glEnable(GL11.GL_ALPHA_TEST);
        GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
        GL11.glCullFace(GL11.GL_BACK);
        GL11.glMatrixMode(GL11.GL_PROJECTION);
        GL11.glLoadIdentity();
        GL11.glMatrixMode(GL11.GL_MODELVIEW);
        this.checkGLError("Startup");
        bar.step("Render Global instance");
        this.renderGlobal = new RenderGlobal(this);
        bar.step("Building Blocks Texture");
        this.textureMapBlocks = new TextureMap(0, "textures/blocks", true);
        bar.step("Anisotropy and Mipmaps");
        this.textureMapBlocks.setAnisotropicFiltering(this.gameSettings.anisotropicFiltering);
        this.textureMapBlocks.setMipmapLevels(this.gameSettings.mipmapLevels);
        bar.step("Loading Blocks Texture");
        this.renderEngine.loadTextureMap(TextureMap.locationBlocksTexture, this.textureMapBlocks);
        bar.step("Loading Items Texture");
        this.renderEngine.loadTextureMap(TextureMap.locationItemsTexture, new TextureMap(1, "textures/items", true));
        bar.step("Viewport");
        GL11.glViewport(0, 0, this.displayWidth, this.displayHeight);
        this.effectRenderer = new EffectRenderer(this.theWorld, this.renderEngine);
        cpw.mods.fml.common.ProgressManager.pop(bar);
        FMLClientHandler.instance().finishMinecraftLoading();
        this.checkGLError("Post startup");
        this.ingameGUI = new net.minecraftforge.client.GuiIngameForge(this);

        if (this.serverName != null)
        {
            FMLClientHandler.instance().connectToServerAtStartup(this.serverName, this.serverPort);
        }
        else
        {
            this.displayGuiScreen(new GuiMainMenu());
        }

        cpw.mods.fml.client.SplashProgress.clearVanillaResources(renderEngine, field_152354_ay);
        this.field_152354_ay = null;
        this.loadingScreen = new LoadingScreenRenderer(this);

        FMLClientHandler.instance().onInitializationComplete();
        if (this.gameSettings.fullScreen && !this.fullscreen)
        {
            this.toggleFullscreen();
        }

        try
        {
            Display.setVSyncEnabled(this.gameSettings.enableVsync);
        }
        catch (OpenGLException openglexception)
        {
            this.gameSettings.enableVsync = false;
            this.gameSettings.saveOptions();
        }
    }

    public boolean func_152349_b()
    {
        return this.mcLanguageManager.isCurrentLocaleUnicode() || this.gameSettings.forceUnicodeFont;
    }

    public void refreshResources()
    {
        ArrayList arraylist = Lists.newArrayList(this.defaultResourcePacks);
        Iterator iterator = this.mcResourcePackRepository.getRepositoryEntries().iterator();

        while (iterator.hasNext())
        {
            ResourcePackRepository.Entry entry = (ResourcePackRepository.Entry)iterator.next();
            arraylist.add(entry.getResourcePack());
        }

        if (this.mcResourcePackRepository.func_148530_e() != null)
        {
            arraylist.add(this.mcResourcePackRepository.func_148530_e());
        }

        try
        {
            this.mcResourceManager.reloadResources(arraylist);
        }
        catch (RuntimeException runtimeexception)
        {
            logger.info("Caught error stitching, removing all assigned resourcepacks", runtimeexception);
            arraylist.clear();
            arraylist.addAll(this.defaultResourcePacks);
            this.mcResourcePackRepository.func_148527_a(Collections.emptyList());
            this.mcResourceManager.reloadResources(arraylist);
            this.gameSettings.resourcePacks.clear();
            this.gameSettings.saveOptions();
        }

        this.mcLanguageManager.parseLanguageMetadata(arraylist);

        if (this.renderGlobal != null)
        {
            this.renderGlobal.loadRenderers();
        }
    }

    private void addDefaultResourcePack()
    {
        this.defaultResourcePacks.add(this.mcDefaultResourcePack);
    }

    private ByteBuffer func_152340_a(InputStream p_152340_1_) throws IOException
    {
        BufferedImage bufferedimage = ImageIO.read(p_152340_1_);
        int[] aint = bufferedimage.getRGB(0, 0, bufferedimage.getWidth(), bufferedimage.getHeight(), (int[])null, 0, bufferedimage.getWidth());
        ByteBuffer bytebuffer = ByteBuffer.allocate(4 * aint.length);
        int[] aint1 = aint;
        int i = aint.length;

        for (int j = 0; j < i; ++j)
        {
            int k = aint1[j];
            bytebuffer.putInt(k << 8 | k >> 24 & 255);
        }

        bytebuffer.flip();
        return bytebuffer;
    }

    private void updateDisplayMode() throws LWJGLException
    {
        HashSet hashset = new HashSet();
        Collections.addAll(hashset, Display.getAvailableDisplayModes());
        DisplayMode displaymode = Display.getDesktopDisplayMode();

        if (!hashset.contains(displaymode) && Util.getOSType() == Util.EnumOS.OSX)
        {
            Iterator iterator = macDisplayModes.iterator();

            while (iterator.hasNext())
            {
                DisplayMode displaymode1 = (DisplayMode)iterator.next();
                boolean flag = true;
                Iterator iterator1 = hashset.iterator();
                DisplayMode displaymode2;

                while (iterator1.hasNext())
                {
                    displaymode2 = (DisplayMode)iterator1.next();

                    if (displaymode2.getBitsPerPixel() == 32 && displaymode2.getWidth() == displaymode1.getWidth() && displaymode2.getHeight() == displaymode1.getHeight())
                    {
                        flag = false;
                        break;
                    }
                }

                if (!flag)
                {
                    iterator1 = hashset.iterator();

                    while (iterator1.hasNext())
                    {
                        displaymode2 = (DisplayMode)iterator1.next();

                        if (displaymode2.getBitsPerPixel() == 32 && displaymode2.getWidth() == displaymode1.getWidth() / 2 && displaymode2.getHeight() == displaymode1.getHeight() / 2)
                        {
                            displaymode = displaymode2;
                            break;
                        }
                    }
                }
            }
        }

        Display.setDisplayMode(displaymode);
        this.displayWidth = displaymode.getWidth();
        this.displayHeight = displaymode.getHeight();
    }

    /**
     * Displays a new screen.
     */
    public void loadScreen() throws LWJGLException
    {
        ScaledResolution scaledresolution = new ScaledResolution(this, this.displayWidth, this.displayHeight);
        int i = scaledresolution.getScaleFactor();
        Framebuffer framebuffer = new Framebuffer(scaledresolution.getScaledWidth() * i, scaledresolution.getScaledHeight() * i, true);
        framebuffer.bindFramebuffer(false);
        GL11.glMatrixMode(GL11.GL_PROJECTION);
        GL11.glLoadIdentity();
        GL11.glOrtho(0.0D, (double)scaledresolution.getScaledWidth(), (double)scaledresolution.getScaledHeight(), 0.0D, 1000.0D, 3000.0D);
        GL11.glMatrixMode(GL11.GL_MODELVIEW);
        GL11.glLoadIdentity();
        GL11.glTranslatef(0.0F, 0.0F, -2000.0F);
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glDisable(GL11.GL_FOG);
        GL11.glDisable(GL11.GL_DEPTH_TEST);
        GL11.glEnable(GL11.GL_TEXTURE_2D);

        try
        {
            this.field_152354_ay = this.renderEngine.getDynamicTextureLocation("logo", new DynamicTexture(ImageIO.read(this.mcDefaultResourcePack.getInputStream(locationMojangPng))));
            this.renderEngine.bindTexture(this.field_152354_ay);
        }
        catch (IOException ioexception)
        {
            logger.error("Unable to load logo: " + locationMojangPng, ioexception);
        }

        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();
        tessellator.setColorOpaque_I(16777215);
        tessellator.addVertexWithUV(0.0D, (double)this.displayHeight, 0.0D, 0.0D, 0.0D);
        tessellator.addVertexWithUV((double)this.displayWidth, (double)this.displayHeight, 0.0D, 0.0D, 0.0D);
        tessellator.addVertexWithUV((double)this.displayWidth, 0.0D, 0.0D, 0.0D, 0.0D);
        tessellator.addVertexWithUV(0.0D, 0.0D, 0.0D, 0.0D, 0.0D);
        tessellator.draw();
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        tessellator.setColorOpaque_I(16777215);
        short short1 = 256;
        short short2 = 256;
        this.scaledTessellator((scaledresolution.getScaledWidth() - short1) / 2, (scaledresolution.getScaledHeight() - short2) / 2, 0, 0, short1, short2);
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glDisable(GL11.GL_FOG);
        framebuffer.unbindFramebuffer();
        framebuffer.framebufferRender(scaledresolution.getScaledWidth() * i, scaledresolution.getScaledHeight() * i);
        GL11.glEnable(GL11.GL_ALPHA_TEST);
        GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
        GL11.glFlush();
        this.func_147120_f();
    }

    /**
     * Loads Tessellator with a scaled resolution
     */
    public void scaledTessellator(int p_71392_1_, int p_71392_2_, int p_71392_3_, int p_71392_4_, int p_71392_5_, int p_71392_6_)
    {
        float f = 0.00390625F;
        float f1 = 0.00390625F;
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();
        tessellator.addVertexWithUV((double)(p_71392_1_ + 0), (double)(p_71392_2_ + p_71392_6_), 0.0D, (double)((float)(p_71392_3_ + 0) * f), (double)((float)(p_71392_4_ + p_71392_6_) * f1));
        tessellator.addVertexWithUV((double)(p_71392_1_ + p_71392_5_), (double)(p_71392_2_ + p_71392_6_), 0.0D, (double)((float)(p_71392_3_ + p_71392_5_) * f), (double)((float)(p_71392_4_ + p_71392_6_) * f1));
        tessellator.addVertexWithUV((double)(p_71392_1_ + p_71392_5_), (double)(p_71392_2_ + 0), 0.0D, (double)((float)(p_71392_3_ + p_71392_5_) * f), (double)((float)(p_71392_4_ + 0) * f1));
        tessellator.addVertexWithUV((double)(p_71392_1_ + 0), (double)(p_71392_2_ + 0), 0.0D, (double)((float)(p_71392_3_ + 0) * f), (double)((float)(p_71392_4_ + 0) * f1));
        tessellator.draw();
    }

    /**
     * Returns the save loader that is currently being used
     */
    public ISaveFormat getSaveLoader()
    {
        return this.saveLoader;
    }

    /**
     * Sets the argument GuiScreen as the main (topmost visible) screen.
     */
    public void displayGuiScreen(GuiScreen p_147108_1_)
    {
        if (p_147108_1_ == null && this.theWorld == null)
        {
            p_147108_1_ = new GuiMainMenu();
        }
        else if (p_147108_1_ == null && this.thePlayer.getHealth() <= 0.0F)
        {
            p_147108_1_ = new GuiGameOver();
        }

        GuiScreen old = this.currentScreen;
        net.minecraftforge.client.event.GuiOpenEvent event = new net.minecraftforge.client.event.GuiOpenEvent(p_147108_1_);

        if (net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(event)) return;

        p_147108_1_ = event.gui;
        if (old != null && p_147108_1_ != old)
        {
            old.onGuiClosed();
        }
        
        if (p_147108_1_ instanceof GuiMainMenu)
        {
            this.gameSettings.showDebugInfo = false;
            this.ingameGUI.getChatGUI().clearChatMessages();
        }

        this.currentScreen = (GuiScreen)p_147108_1_;

        if (p_147108_1_ != null)
        {
            this.setIngameNotInFocus();
            ScaledResolution scaledresolution = new ScaledResolution(this, this.displayWidth, this.displayHeight);
            int i = scaledresolution.getScaledWidth();
            int j = scaledresolution.getScaledHeight();
            ((GuiScreen)p_147108_1_).setWorldAndResolution(this, i, j);
            this.skipRenderWorld = false;
        }
        else
        {
            this.mcSoundHandler.resumeSounds();
            this.setIngameFocus();
        }
    }

    /**
     * Checks for an OpenGL error. If there is one, prints the error ID and error string.
     */
    private void checkGLError(String p_71361_1_)
    {
        int i = GL11.glGetError();

        if (i != 0)
        {
            String s1 = GLU.gluErrorString(i);
            logger.error("########## GL ERROR ##########");
            logger.error("@ " + p_71361_1_);
            logger.error(i + ": " + s1);
        }
    }

    /**
     * Shuts down the minecraft applet by stopping the resource downloads, and clearing up GL stuff; called when the
     * application (or web page) is exited.
     */
    public void shutdownMinecraftApplet()
    {
        try
        {
            this.field_152353_at.func_152923_i();
            logger.info("Stopping!");

            try
            {
                this.loadWorld((WorldClient)null);
            }
            catch (Throwable throwable1)
            {
                ;
            }

            try
            {
                GLAllocation.deleteTexturesAndDisplayLists();
            }
            catch (Throwable throwable)
            {
                ;
            }

            this.mcSoundHandler.unloadSounds();
        }
        finally
        {
            Display.destroy();

            if (!this.hasCrashed)
            {
                System.exit(0);
            }
        }

        System.gc();
    }

    public void run()
    {
        this.running = true;
        CrashReport crashreport;

        try
        {
            this.startGame();
        }
        catch (Throwable throwable)
        {
            crashreport = CrashReport.makeCrashReport(throwable, "Initializing game");
            crashreport.makeCategory("Initialization");
            this.displayCrashReport(this.addGraphicsAndWorldToCrashReport(crashreport));
            return;
        }

        while (true)
        {
            try
            {
                while (this.running)
                {
                    if (!this.hasCrashed || this.crashReporter == null)
                    {
                        try
                        {
                            this.runGameLoop();
                        }
                        catch (OutOfMemoryError outofmemoryerror)
                        {
                            this.freeMemory();
                            this.displayGuiScreen(new GuiMemoryErrorScreen());
                            System.gc();
                        }

                        continue;
                    }

                    this.displayCrashReport(this.crashReporter);
                    return;
                }
            }
            catch (MinecraftError minecrafterror)
            {
                ;
            }
            catch (ReportedException reportedexception)
            {
                this.addGraphicsAndWorldToCrashReport(reportedexception.getCrashReport());
                this.freeMemory();
                logger.fatal("Reported exception thrown!", reportedexception);
                this.displayCrashReport(reportedexception.getCrashReport());
            }
            catch (Throwable throwable1)
            {
                crashreport = this.addGraphicsAndWorldToCrashReport(new CrashReport("Unexpected error", throwable1));
                this.freeMemory();
                logger.fatal("Unreported exception thrown!", throwable1);
                this.displayCrashReport(crashreport);
            }
            finally
            {
                this.shutdownMinecraftApplet();
            }

            return;
        }
    }

    /**
     * Called repeatedly from run()
     */
    private void runGameLoop()
    {
        this.mcProfiler.startSection("root");

        if (Display.isCreated() && Display.isCloseRequested())
        {
            this.shutdown();
        }

        if (this.isGamePaused && this.theWorld != null)
        {
            float f = this.timer.renderPartialTicks;
            this.timer.updateTimer();
            this.timer.renderPartialTicks = f;
        }
        else
        {
            this.timer.updateTimer();
        }

        if ((this.theWorld == null || this.currentScreen == null) && this.refreshTexturePacksScheduled)
        {
            this.refreshTexturePacksScheduled = false;
            this.refreshResources();
        }

        long j = System.nanoTime();
        this.mcProfiler.startSection("tick");

        for (int i = 0; i < this.timer.elapsedTicks; ++i)
        {
            this.runTick();
        }

        this.mcProfiler.endStartSection("preRenderErrors");
        long k = System.nanoTime() - j;
        this.checkGLError("Pre render");
        RenderBlocks.fancyGrass = this.gameSettings.fancyGraphics;
        this.mcProfiler.endStartSection("sound");
        this.mcSoundHandler.setListener(this.thePlayer, this.timer.renderPartialTicks);
        this.mcProfiler.endSection();
        this.mcProfiler.startSection("render");
        GL11.glPushMatrix();
        GL11.glClear(GL11.GL_COLOR_BUFFER_BIT | GL11.GL_DEPTH_BUFFER_BIT);
        this.framebufferMc.bindFramebuffer(true);
        this.mcProfiler.startSection("display");
        GL11.glEnable(GL11.GL_TEXTURE_2D);

        if (this.thePlayer != null && this.thePlayer.isEntityInsideOpaqueBlock())
        {
            this.gameSettings.thirdPersonView = 0;
        }

        this.mcProfiler.endSection();

        if (!this.skipRenderWorld)
        {
            FMLCommonHandler.instance().onRenderTickStart(this.timer.renderPartialTicks);
            this.mcProfiler.endStartSection("gameRenderer");
            this.entityRenderer.updateCameraAndRender(this.timer.renderPartialTicks);
            this.mcProfiler.endSection();
            FMLCommonHandler.instance().onRenderTickEnd(this.timer.renderPartialTicks);
        }

        GL11.glFlush();
        this.mcProfiler.endSection();

        if (!Display.isActive() && this.fullscreen)
        {
            this.toggleFullscreen();
        }

        if (this.gameSettings.showDebugInfo && this.gameSettings.showDebugProfilerChart)
        {
            if (!this.mcProfiler.profilingEnabled)
            {
                this.mcProfiler.clearProfiling();
            }

            this.mcProfiler.profilingEnabled = true;
            this.displayDebugInfo(k);
        }
        else
        {
            this.mcProfiler.profilingEnabled = false;
            this.prevFrameTime = System.nanoTime();
        }

        this.guiAchievement.func_146254_a();
        this.framebufferMc.unbindFramebuffer();
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.framebufferMc.framebufferRender(this.displayWidth, this.displayHeight);
        GL11.glPopMatrix();
        GL11.glPushMatrix();
        this.entityRenderer.func_152430_c(this.timer.renderPartialTicks);
        GL11.glPopMatrix();
        this.mcProfiler.startSection("root");
        this.func_147120_f();
        Thread.yield();
        this.mcProfiler.startSection("stream");
        this.mcProfiler.startSection("update");
        this.field_152353_at.func_152935_j();
        this.mcProfiler.endStartSection("submit");
        this.field_152353_at.func_152922_k();
        this.mcProfiler.endSection();
        this.mcProfiler.endSection();
        this.checkGLError("Post render");
        ++this.fpsCounter;
        this.isGamePaused = this.isSingleplayer() && this.currentScreen != null && this.currentScreen.doesGuiPauseGame() && !this.theIntegratedServer.getPublic();

        while (getSystemTime() >= this.debugUpdateTime + 1000L)
        {
            debugFPS = this.fpsCounter;
            this.debug = debugFPS + " fps, " + WorldRenderer.chunksUpdated + " chunk updates";
            WorldRenderer.chunksUpdated = 0;
            this.debugUpdateTime += 1000L;
            this.fpsCounter = 0;
            this.usageSnooper.addMemoryStatsToSnooper();

            if (!this.usageSnooper.isSnooperRunning())
            {
                this.usageSnooper.startSnooper();
            }
        }

        this.mcProfiler.endSection();

        if (this.isFramerateLimitBelowMax())
        {
            Display.sync(this.getLimitFramerate());
        }
    }

    public void func_147120_f()
    {
        Display.update();

        if (!this.fullscreen && Display.wasResized())
        {
            int i = this.displayWidth;
            int j = this.displayHeight;
            this.displayWidth = Display.getWidth();
            this.displayHeight = Display.getHeight();

            if (this.displayWidth != i || this.displayHeight != j)
            {
                if (this.displayWidth <= 0)
                {
                    this.displayWidth = 1;
                }

                if (this.displayHeight <= 0)
                {
                    this.displayHeight = 1;
                }

                this.resize(this.displayWidth, this.displayHeight);
            }
        }
    }

    public int getLimitFramerate()
    {
        return this.theWorld == null && this.currentScreen != null ? 30 : this.gameSettings.limitFramerate;
    }

    public boolean isFramerateLimitBelowMax()
    {
        return (float)this.getLimitFramerate() < GameSettings.Options.FRAMERATE_LIMIT.getValueMax();
    }

    public void freeMemory()
    {
        try
        {
            memoryReserve = new byte[0];
            this.renderGlobal.deleteAllDisplayLists();
        }
        catch (Throwable throwable2)
        {
            ;
        }

        try
        {
            System.gc();
        }
        catch (Throwable throwable1)
        {
            ;
        }

        try
        {
            System.gc();
            this.loadWorld((WorldClient)null);
        }
        catch (Throwable throwable)
        {
            ;
        }

        System.gc();
    }

    /**
     * Update debugProfilerName in response to number keys in debug screen
     */
    private void updateDebugProfilerName(int p_71383_1_)
    {
        List list = this.mcProfiler.getProfilingData(this.debugProfilerName);

        if (list != null && !list.isEmpty())
        {
            Profiler.Result result = (Profiler.Result)list.remove(0);

            if (p_71383_1_ == 0)
            {
                if (result.field_76331_c.length() > 0)
                {
                    int j = this.debugProfilerName.lastIndexOf(".");

                    if (j >= 0)
                    {
                        this.debugProfilerName = this.debugProfilerName.substring(0, j);
                    }
                }
            }
            else
            {
                --p_71383_1_;

                if (p_71383_1_ < list.size() && !((Profiler.Result)list.get(p_71383_1_)).field_76331_c.equals("unspecified"))
                {
                    if (this.debugProfilerName.length() > 0)
                    {
                        this.debugProfilerName = this.debugProfilerName + ".";
                    }

                    this.debugProfilerName = this.debugProfilerName + ((Profiler.Result)list.get(p_71383_1_)).field_76331_c;
                }
            }
        }
    }

    private void displayDebugInfo(long p_71366_1_)
    {
        if (this.mcProfiler.profilingEnabled)
        {
            List list = this.mcProfiler.getProfilingData(this.debugProfilerName);
            Profiler.Result result = (Profiler.Result)list.remove(0);
            GL11.glClear(GL11.GL_DEPTH_BUFFER_BIT);
            GL11.glMatrixMode(GL11.GL_PROJECTION);
            GL11.glEnable(GL11.GL_COLOR_MATERIAL);
            GL11.glLoadIdentity();
            GL11.glOrtho(0.0D, (double)this.displayWidth, (double)this.displayHeight, 0.0D, 1000.0D, 3000.0D);
            GL11.glMatrixMode(GL11.GL_MODELVIEW);
            GL11.glLoadIdentity();
            GL11.glTranslatef(0.0F, 0.0F, -2000.0F);
            GL11.glLineWidth(1.0F);
            GL11.glDisable(GL11.GL_TEXTURE_2D);
            Tessellator tessellator = Tessellator.instance;
            short short1 = 160;
            int j = this.displayWidth - short1 - 10;
            int k = this.displayHeight - short1 * 2;
            GL11.glEnable(GL11.GL_BLEND);
            tessellator.startDrawingQuads();
            tessellator.setColorRGBA_I(0, 200);
            tessellator.addVertex((double)((float)j - (float)short1 * 1.1F), (double)((float)k - (float)short1 * 0.6F - 16.0F), 0.0D);
            tessellator.addVertex((double)((float)j - (float)short1 * 1.1F), (double)(k + short1 * 2), 0.0D);
            tessellator.addVertex((double)((float)j + (float)short1 * 1.1F), (double)(k + short1 * 2), 0.0D);
            tessellator.addVertex((double)((float)j + (float)short1 * 1.1F), (double)((float)k - (float)short1 * 0.6F - 16.0F), 0.0D);
            tessellator.draw();
            GL11.glDisable(GL11.GL_BLEND);
            double d0 = 0.0D;
            int i1;

            for (int l = 0; l < list.size(); ++l)
            {
                Profiler.Result result1 = (Profiler.Result)list.get(l);
                i1 = MathHelper.floor_double(result1.field_76332_a / 4.0D) + 1;
                tessellator.startDrawing(6);
                tessellator.setColorOpaque_I(result1.func_76329_a());
                tessellator.addVertex((double)j, (double)k, 0.0D);
                int j1;
                float f;
                float f1;
                float f2;

                for (j1 = i1; j1 >= 0; --j1)
                {
                    f = (float)((d0 + result1.field_76332_a * (double)j1 / (double)i1) * Math.PI * 2.0D / 100.0D);
                    f1 = MathHelper.sin(f) * (float)short1;
                    f2 = MathHelper.cos(f) * (float)short1 * 0.5F;
                    tessellator.addVertex((double)((float)j + f1), (double)((float)k - f2), 0.0D);
                }

                tessellator.draw();
                tessellator.startDrawing(5);
                tessellator.setColorOpaque_I((result1.func_76329_a() & 16711422) >> 1);

                for (j1 = i1; j1 >= 0; --j1)
                {
                    f = (float)((d0 + result1.field_76332_a * (double)j1 / (double)i1) * Math.PI * 2.0D / 100.0D);
                    f1 = MathHelper.sin(f) * (float)short1;
                    f2 = MathHelper.cos(f) * (float)short1 * 0.5F;
                    tessellator.addVertex((double)((float)j + f1), (double)((float)k - f2), 0.0D);
                    tessellator.addVertex((double)((float)j + f1), (double)((float)k - f2 + 10.0F), 0.0D);
                }

                tessellator.draw();
                d0 += result1.field_76332_a;
            }

            DecimalFormat decimalformat = new DecimalFormat("##0.00");
            GL11.glEnable(GL11.GL_TEXTURE_2D);
            String s = "";

            if (!result.field_76331_c.equals("unspecified"))
            {
                s = s + "[0] ";
            }

            if (result.field_76331_c.length() == 0)
            {
                s = s + "ROOT ";
            }
            else
            {
                s = s + result.field_76331_c + " ";
            }

            i1 = 16777215;
            this.fontRenderer.drawStringWithShadow(s, j - short1, k - short1 / 2 - 16, i1);
            this.fontRenderer.drawStringWithShadow(s = decimalformat.format(result.field_76330_b) + "%", j + short1 - this.fontRenderer.getStringWidth(s), k - short1 / 2 - 16, i1);

            for (int k1 = 0; k1 < list.size(); ++k1)
            {
                Profiler.Result result2 = (Profiler.Result)list.get(k1);
                String s1 = "";

                if (result2.field_76331_c.equals("unspecified"))
                {
                    s1 = s1 + "[?] ";
                }
                else
                {
                    s1 = s1 + "[" + (k1 + 1) + "] ";
                }

                s1 = s1 + result2.field_76331_c;
                this.fontRenderer.drawStringWithShadow(s1, j - short1, k + short1 / 2 + k1 * 8 + 20, result2.func_76329_a());
                this.fontRenderer.drawStringWithShadow(s1 = decimalformat.format(result2.field_76332_a) + "%", j + short1 - 50 - this.fontRenderer.getStringWidth(s1), k + short1 / 2 + k1 * 8 + 20, result2.func_76329_a());
                this.fontRenderer.drawStringWithShadow(s1 = decimalformat.format(result2.field_76330_b) + "%", j + short1 - this.fontRenderer.getStringWidth(s1), k + short1 / 2 + k1 * 8 + 20, result2.func_76329_a());
            }
        }
    }

    /**
     * Called when the window is closing. Sets 'running' to false which allows the game loop to exit cleanly.
     */
    public void shutdown()
    {
        this.running = false;
    }

    /**
     * Will set the focus to ingame if the Minecraft window is the active with focus. Also clears any GUI screen
     * currently displayed
     */
    public void setIngameFocus()
    {
        if (Display.isActive())
        {
            if (!this.inGameHasFocus)
            {
                this.inGameHasFocus = true;
                this.mouseHelper.grabMouseCursor();
                this.displayGuiScreen((GuiScreen)null);
                this.leftClickCounter = 10000;
            }
        }
    }

    /**
     * Resets the player keystate, disables the ingame focus, and ungrabs the mouse cursor.
     */
    public void setIngameNotInFocus()
    {
        if (this.inGameHasFocus)
        {
            KeyBinding.unPressAllKeys();
            this.inGameHasFocus = false;
            this.mouseHelper.ungrabMouseCursor();
        }
    }

    /**
     * Displays the ingame menu
     */
    public void displayInGameMenu()
    {
        if (this.currentScreen == null)
        {
            this.displayGuiScreen(new GuiIngameMenu());

            if (this.isSingleplayer() && !this.theIntegratedServer.getPublic())
            {
                this.mcSoundHandler.pauseSounds();
            }
        }
    }

    private void func_147115_a(boolean p_147115_1_)
    {
        if (!p_147115_1_)
        {
            this.leftClickCounter = 0;
        }

        if (this.leftClickCounter <= 0)
        {
            if (p_147115_1_ && this.objectMouseOver != null && this.objectMouseOver.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK)
            {
                int i = this.objectMouseOver.blockX;
                int j = this.objectMouseOver.blockY;
                int k = this.objectMouseOver.blockZ;

                if (this.theWorld.getBlock(i, j, k).getMaterial() != Material.air)
                {
                    this.playerController.onPlayerDamageBlock(i, j, k, this.objectMouseOver.sideHit);

                    if (this.thePlayer.isCurrentToolAdventureModeExempt(i, j, k))
                    {
                        this.effectRenderer.addBlockHitEffects(i, j, k, this.objectMouseOver);
                        this.thePlayer.swingItem();
                    }
                }
            }
            else
            {
                this.playerController.resetBlockRemoving();
            }
        }
    }

    private void func_147116_af()
    {
        if (this.leftClickCounter <= 0)
        {
            this.thePlayer.swingItem();

            if (this.objectMouseOver == null)
            {
                logger.error("Null returned as \'hitResult\', this shouldn\'t happen!");

                if (this.playerController.isNotCreative())
                {
                    this.leftClickCounter = 10;
                }
            }
            else
            {
                switch (Minecraft.SwitchMovingObjectType.field_152390_a[this.objectMouseOver.typeOfHit.ordinal()])
                {
                    case 1:
                        this.playerController.attackEntity(this.thePlayer, this.objectMouseOver.entityHit);
                        break;
                    case 2:
                        int i = this.objectMouseOver.blockX;
                        int j = this.objectMouseOver.blockY;
                        int k = this.objectMouseOver.blockZ;

                        if (this.theWorld.getBlock(i, j, k).getMaterial() == Material.air)
                        {
                            if (this.playerController.isNotCreative())
                            {
                                this.leftClickCounter = 10;
                            }
                        }
                        else
                        {
                            this.playerController.clickBlock(i, j, k, this.objectMouseOver.sideHit);
                        }
                }
            }
        }
    }

    private void func_147121_ag()
    {
        this.rightClickDelayTimer = 4;
        boolean flag = true;
        ItemStack itemstack = this.thePlayer.inventory.getCurrentItem();

        if (this.objectMouseOver == null)
        {
            logger.warn("Null returned as \'hitResult\', this shouldn\'t happen!");
        }
        else
        {
            switch (Minecraft.SwitchMovingObjectType.field_152390_a[this.objectMouseOver.typeOfHit.ordinal()])
            {
                case 1:
                    if (this.playerController.interactWithEntitySendPacket(this.thePlayer, this.objectMouseOver.entityHit))
                    {
                        flag = false;
                    }

                    break;
                case 2:
                    int i = this.objectMouseOver.blockX;
                    int j = this.objectMouseOver.blockY;
                    int k = this.objectMouseOver.blockZ;

                    if (!this.theWorld.getBlock(i, j, k).isAir(theWorld, i, j, k))
                    {
                        int l = itemstack != null ? itemstack.stackSize : 0;

                        boolean result = !net.minecraftforge.event.ForgeEventFactory.onPlayerInteract(thePlayer, net.minecraftforge.event.entity.player.PlayerInteractEvent.Action.RIGHT_CLICK_BLOCK, i, j, k, this.objectMouseOver.sideHit, this.theWorld).isCanceled();
                        if (result && this.playerController.onPlayerRightClick(this.thePlayer, this.theWorld, itemstack, i, j, k, this.objectMouseOver.sideHit, this.objectMouseOver.hitVec))
                        {
                            flag = false;
                            this.thePlayer.swingItem();
                        }

                        if (itemstack == null)
                        {
                            return;
                        }

                        if (itemstack.stackSize == 0)
                        {
                            this.thePlayer.inventory.mainInventory[this.thePlayer.inventory.currentItem] = null;
                        }
                        else if (itemstack.stackSize != l || this.playerController.isInCreativeMode())
                        {
                            this.entityRenderer.itemRenderer.resetEquippedProgress();
                        }
                    }
            }
        }

        if (flag)
        {
            ItemStack itemstack1 = this.thePlayer.inventory.getCurrentItem();

            boolean result = !net.minecraftforge.event.ForgeEventFactory.onPlayerInteract(thePlayer, net.minecraftforge.event.entity.player.PlayerInteractEvent.Action.RIGHT_CLICK_AIR, 0, 0, 0, -1, this.theWorld).isCanceled();
            if (result && itemstack1 != null && this.playerController.sendUseItem(this.thePlayer, this.theWorld, itemstack1))
            {
                this.entityRenderer.itemRenderer.resetEquippedProgress2();
            }
        }
    }

    /**
     * Toggles fullscreen mode.
     */
    public void toggleFullscreen()
    {
        try
        {
            this.fullscreen = !this.fullscreen;

            if (this.fullscreen)
            {
                this.updateDisplayMode();
                this.displayWidth = Display.getDisplayMode().getWidth();
                this.displayHeight = Display.getDisplayMode().getHeight();

                if (this.displayWidth <= 0)
                {
                    this.displayWidth = 1;
                }

                if (this.displayHeight <= 0)
                {
                    this.displayHeight = 1;
                }
            }
            else
            {
                Display.setDisplayMode(new DisplayMode(this.tempDisplayWidth, this.tempDisplayHeight));
                this.displayWidth = this.tempDisplayWidth;
                this.displayHeight = this.tempDisplayHeight;

                if (this.displayWidth <= 0)
                {
                    this.displayWidth = 1;
                }

                if (this.displayHeight <= 0)
                {
                    this.displayHeight = 1;
                }
            }

            if (this.currentScreen != null)
            {
                this.resize(this.displayWidth, this.displayHeight);
            }
            else
            {
                this.updateFramebufferSize();
            }

            Display.setFullscreen(this.fullscreen);
            Display.setVSyncEnabled(this.gameSettings.enableVsync);
            this.func_147120_f();
        }
        catch (Exception exception)
        {
            logger.error("Couldn\'t toggle fullscreen", exception);
        }
    }

    /**
     * Called to resize the current screen.
     */
    public void resize(int p_71370_1_, int p_71370_2_)
    {
        this.displayWidth = p_71370_1_ <= 0 ? 1 : p_71370_1_;
        this.displayHeight = p_71370_2_ <= 0 ? 1 : p_71370_2_;

        if (this.currentScreen != null)
        {
            ScaledResolution scaledresolution = new ScaledResolution(this, p_71370_1_, p_71370_2_);
            int k = scaledresolution.getScaledWidth();
            int l = scaledresolution.getScaledHeight();
            this.currentScreen.setWorldAndResolution(this, k, l);
        }

        this.loadingScreen = new LoadingScreenRenderer(this);
        this.updateFramebufferSize();
    }

    private void updateFramebufferSize()
    {
        this.framebufferMc.createBindFramebuffer(this.displayWidth, this.displayHeight);

        if (this.entityRenderer != null)
        {
            this.entityRenderer.updateShaderGroupSize(this.displayWidth, this.displayHeight);
        }
    }

    /**
     * Runs the current tick.
     */
    public void runTick()
    {
        this.mcProfiler.startSection("scheduledExecutables");
        Queue queue = this.field_152351_aB;

        synchronized (this.field_152351_aB)
        {
            while (!this.field_152351_aB.isEmpty())
            {
                ((FutureTask)this.field_152351_aB.poll()).run();
            }
        }

        this.mcProfiler.endSection();

        if (this.rightClickDelayTimer > 0)
        {
            --this.rightClickDelayTimer;
        }

        FMLCommonHandler.instance().onPreClientTick();

        this.mcProfiler.startSection("gui");

        if (!this.isGamePaused)
        {
            this.ingameGUI.updateTick();
        }

        this.mcProfiler.endStartSection("pick");
        this.entityRenderer.getMouseOver(1.0F);
        this.mcProfiler.endStartSection("gameMode");

        if (!this.isGamePaused && this.theWorld != null)
        {
            this.playerController.updateController();
        }

        this.mcProfiler.endStartSection("textures");

        if (!this.isGamePaused)
        {
            this.renderEngine.tick();
        }

        if (this.currentScreen == null && this.thePlayer != null)
        {
            if (this.thePlayer.getHealth() <= 0.0F)
            {
                this.displayGuiScreen((GuiScreen)null);
            }
            else if (this.thePlayer.isPlayerSleeping() && this.theWorld != null)
            {
                this.displayGuiScreen(new GuiSleepMP());
            }
        }
        else if (this.currentScreen != null && this.currentScreen instanceof GuiSleepMP && !this.thePlayer.isPlayerSleeping())
        {
            this.displayGuiScreen((GuiScreen)null);
        }

        if (this.currentScreen != null)
        {
            this.leftClickCounter = 10000;
        }

        CrashReport crashreport;
        CrashReportCategory crashreportcategory;

        if (this.currentScreen != null)
        {
            try
            {
                this.currentScreen.handleInput();
            }
            catch (Throwable throwable1)
            {
                crashreport = CrashReport.makeCrashReport(throwable1, "Updating screen events");
                crashreportcategory = crashreport.makeCategory("Affected screen");
                crashreportcategory.addCrashSectionCallable("Screen name", new Callable()
                {
                    private static final String __OBFID = "CL_00000640";
                    public String call()
                    {
                        return Minecraft.this.currentScreen.getClass().getCanonicalName();
                    }
                });
                throw new ReportedException(crashreport);
            }

            if (this.currentScreen != null)
            {
                try
                {
                    this.currentScreen.updateScreen();
                }
                catch (Throwable throwable)
                {
                    crashreport = CrashReport.makeCrashReport(throwable, "Ticking screen");
                    crashreportcategory = crashreport.makeCategory("Affected screen");
                    crashreportcategory.addCrashSectionCallable("Screen name", new Callable()
                    {
                        private static final String __OBFID = "CL_00000642";
                        public String call()
                        {
                            return Minecraft.this.currentScreen.getClass().getCanonicalName();
                        }
                    });
                    throw new ReportedException(crashreport);
                }
            }
        }

        if (this.currentScreen == null || this.currentScreen.allowUserInput)
        {
            this.mcProfiler.endStartSection("mouse");
            int j;

            while (Mouse.next())
            {
                if (net.minecraftforge.client.ForgeHooksClient.postMouseEvent()) continue;

                j = Mouse.getEventButton();
                KeyBinding.setKeyBindState(j - 100, Mouse.getEventButtonState());

                if (Mouse.getEventButtonState())
                {
                    KeyBinding.onTick(j - 100);
                }

                long k = getSystemTime() - this.systemTime;

                if (k <= 200L)
                {
                    int i = Mouse.getEventDWheel();

                    if (i != 0)
                    {
                        this.thePlayer.inventory.changeCurrentItem(i);

                        if (this.gameSettings.noclip)
                        {
                            if (i > 0)
                            {
                                i = 1;
                            }

                            if (i < 0)
                            {
                                i = -1;
                            }

                            this.gameSettings.noclipRate += (float)i * 0.25F;
                        }
                    }

                    if (this.currentScreen == null)
                    {
                        if (!this.inGameHasFocus && Mouse.getEventButtonState())
                        {
                            this.setIngameFocus();
                        }
                    }
                    else if (this.currentScreen != null)
                    {
                        this.currentScreen.handleMouseInput();
                    }
                }
                FMLCommonHandler.instance().fireMouseInput();
            }

            if (this.leftClickCounter > 0)
            {
                --this.leftClickCounter;
            }

            this.mcProfiler.endStartSection("keyboard");
            boolean flag;

            while (Keyboard.next())
            {
                KeyBinding.setKeyBindState(Keyboard.getEventKey(), Keyboard.getEventKeyState());

                if (Keyboard.getEventKeyState())
                {
                    KeyBinding.onTick(Keyboard.getEventKey());
                }

                if (this.field_83002_am > 0L)
                {
                    if (getSystemTime() - this.field_83002_am >= 6000L)
                    {
                        throw new ReportedException(new CrashReport("Manually triggered debug crash", new Throwable()));
                    }

                    if (!Keyboard.isKeyDown(46) || !Keyboard.isKeyDown(61))
                    {
                        this.field_83002_am = -1L;
                    }
                }
                else if (Keyboard.isKeyDown(46) && Keyboard.isKeyDown(61))
                {
                    this.field_83002_am = getSystemTime();
                }

                this.func_152348_aa();

                if (Keyboard.getEventKeyState())
                {
                    if (Keyboard.getEventKey() == 62 && this.entityRenderer != null)
                    {
                        this.entityRenderer.deactivateShader();
                    }

                    if (this.currentScreen != null)
                    {
                        this.currentScreen.handleKeyboardInput();
                    }
                    else
                    {
                        if (Keyboard.getEventKey() == 1)
                        {
                            this.displayInGameMenu();
                        }

                        if (Keyboard.getEventKey() == 31 && Keyboard.isKeyDown(61))
                        {
                            this.refreshResources();
                        }

                        if (Keyboard.getEventKey() == 20 && Keyboard.isKeyDown(61))
                        {
                            this.refreshResources();
                        }

                        if (Keyboard.getEventKey() == 33 && Keyboard.isKeyDown(61))
                        {
                            flag = Keyboard.isKeyDown(42) | Keyboard.isKeyDown(54);
                            this.gameSettings.setOptionValue(GameSettings.Options.RENDER_DISTANCE, flag ? -1 : 1);
                        }

                        if (Keyboard.getEventKey() == 30 && Keyboard.isKeyDown(61))
                        {
                            this.renderGlobal.loadRenderers();
                        }

                        if (Keyboard.getEventKey() == 35 && Keyboard.isKeyDown(61))
                        {
                            this.gameSettings.advancedItemTooltips = !this.gameSettings.advancedItemTooltips;
                            this.gameSettings.saveOptions();
                        }

                        if (Keyboard.getEventKey() == 48 && Keyboard.isKeyDown(61))
                        {
                            RenderManager.debugBoundingBox = !RenderManager.debugBoundingBox;
                        }

                        if (Keyboard.getEventKey() == 25 && Keyboard.isKeyDown(61))
                        {
                            this.gameSettings.pauseOnLostFocus = !this.gameSettings.pauseOnLostFocus;
                            this.gameSettings.saveOptions();
                        }

                        if (Keyboard.getEventKey() == 59)
                        {
                            this.gameSettings.hideGUI = !this.gameSettings.hideGUI;
                        }

                        if (Keyboard.getEventKey() == 61)
                        {
                            this.gameSettings.showDebugInfo = !this.gameSettings.showDebugInfo;
                            this.gameSettings.showDebugProfilerChart = GuiScreen.isShiftKeyDown();
                        }

                        if (this.gameSettings.keyBindTogglePerspective.isPressed())
                        {
                            ++this.gameSettings.thirdPersonView;

                            if (this.gameSettings.thirdPersonView > 2)
                            {
                                this.gameSettings.thirdPersonView = 0;
                            }
                        }

                        if (this.gameSettings.keyBindSmoothCamera.isPressed())
                        {
                            this.gameSettings.smoothCamera = !this.gameSettings.smoothCamera;
                        }
                    }

                    if (this.gameSettings.showDebugInfo && this.gameSettings.showDebugProfilerChart)
                    {
                        if (Keyboard.getEventKey() == 11)
                        {
                            this.updateDebugProfilerName(0);
                        }

                        for (j = 0; j < 9; ++j)
                        {
                            if (Keyboard.getEventKey() == 2 + j)
                            {
                                this.updateDebugProfilerName(j + 1);
                            }
                        }
                    }
                }
                FMLCommonHandler.instance().fireKeyInput();
            }

            for (j = 0; j < 9; ++j)
            {
                if (this.gameSettings.keyBindsHotbar[j].isPressed())
                {
                    this.thePlayer.inventory.currentItem = j;
                }
            }

            flag = this.gameSettings.chatVisibility != EntityPlayer.EnumChatVisibility.HIDDEN;

            while (this.gameSettings.keyBindInventory.isPressed())
            {
                if (this.playerController.func_110738_j())
                {
                    this.thePlayer.func_110322_i();
                }
                else
                {
                    this.getNetHandler().addToSendQueue(new C16PacketClientStatus(C16PacketClientStatus.EnumState.OPEN_INVENTORY_ACHIEVEMENT));
                    this.displayGuiScreen(new GuiInventory(this.thePlayer));
                }
            }

            while (this.gameSettings.keyBindDrop.isPressed())
            {
                this.thePlayer.dropOneItem(GuiScreen.isCtrlKeyDown());
            }

            while (this.gameSettings.keyBindChat.isPressed() && flag)
            {
                this.displayGuiScreen(new GuiChat());
            }

            if (this.currentScreen == null && this.gameSettings.keyBindCommand.isPressed() && flag)
            {
                this.displayGuiScreen(new GuiChat("/"));
            }

            if (this.thePlayer.isUsingItem())
            {
                if (!this.gameSettings.keyBindUseItem.getIsKeyPressed())
                {
                    this.playerController.onStoppedUsingItem(this.thePlayer);
                }

                label391:

                while (true)
                {
                    if (!this.gameSettings.keyBindAttack.isPressed())
                    {
                        while (this.gameSettings.keyBindUseItem.isPressed())
                        {
                            ;
                        }

                        while (true)
                        {
                            if (this.gameSettings.keyBindPickBlock.isPressed())
                            {
                                continue;
                            }

                            break label391;
                        }
                    }
                }
            }
            else
            {
                while (this.gameSettings.keyBindAttack.isPressed())
                {
                    this.func_147116_af();
                }

                while (this.gameSettings.keyBindUseItem.isPressed())
                {
                    this.func_147121_ag();
                }

                while (this.gameSettings.keyBindPickBlock.isPressed())
                {
                    this.func_147112_ai();
                }
            }

            if (this.gameSettings.keyBindUseItem.getIsKeyPressed() && this.rightClickDelayTimer == 0 && !this.thePlayer.isUsingItem())
            {
                this.func_147121_ag();
            }

            this.func_147115_a(this.currentScreen == null && this.gameSettings.keyBindAttack.getIsKeyPressed() && this.inGameHasFocus);
        }

        if (this.theWorld != null)
        {
            if (this.thePlayer != null)
            {
                ++this.joinPlayerCounter;

                if (this.joinPlayerCounter == 30)
                {
                    this.joinPlayerCounter = 0;
                    this.theWorld.joinEntityInSurroundings(this.thePlayer);
                }
            }

            this.mcProfiler.endStartSection("gameRenderer");

            if (!this.isGamePaused)
            {
                this.entityRenderer.updateRenderer();
            }

            this.mcProfiler.endStartSection("levelRenderer");

            if (!this.isGamePaused)
            {
                this.renderGlobal.updateClouds();
            }

            this.mcProfiler.endStartSection("level");

            if (!this.isGamePaused)
            {
                if (this.theWorld.lastLightningBolt > 0)
                {
                    --this.theWorld.lastLightningBolt;
                }

                this.theWorld.updateEntities();
            }
        }

        if (!this.isGamePaused)
        {
            this.mcMusicTicker.update();
            this.mcSoundHandler.update();
        }

        if (this.theWorld != null)
        {
            if (!this.isGamePaused)
            {
                this.theWorld.setAllowedSpawnTypes(this.theWorld.difficultySetting != EnumDifficulty.PEACEFUL, true);

                try
                {
                    this.theWorld.tick();
                }
                catch (Throwable throwable2)
                {
                    crashreport = CrashReport.makeCrashReport(throwable2, "Exception in world tick");

                    if (this.theWorld == null)
                    {
                        crashreportcategory = crashreport.makeCategory("Affected level");
                        crashreportcategory.addCrashSection("Problem", "Level is null!");
                    }
                    else
                    {
                        this.theWorld.addWorldInfoToCrashReport(crashreport);
                    }

                    throw new ReportedException(crashreport);
                }
            }

            this.mcProfiler.endStartSection("animateTick");

            if (!this.isGamePaused && this.theWorld != null)
            {
                this.theWorld.doVoidFogParticles(MathHelper.floor_double(this.thePlayer.posX), MathHelper.floor_double(this.thePlayer.posY), MathHelper.floor_double(this.thePlayer.posZ));
            }

            this.mcProfiler.endStartSection("particles");

            if (!this.isGamePaused)
            {
                this.effectRenderer.updateEffects();
            }
        }
        else if (this.myNetworkManager != null)
        {
            this.mcProfiler.endStartSection("pendingConnection");
            this.myNetworkManager.processReceivedPackets();
        }

        FMLCommonHandler.instance().onPostClientTick();

        this.mcProfiler.endSection();
        this.systemTime = getSystemTime();
    }

    /**
     * Arguments: World foldername,  World ingame name, WorldSettings
     */
    public void launchIntegratedServer(String p_71371_1_, String p_71371_2_, WorldSettings p_71371_3_)
    {
        FMLClientHandler.instance().startIntegratedServer(p_71371_1_, p_71371_2_, p_71371_3_);
        this.loadWorld((WorldClient)null);
        System.gc();
        ISaveHandler isavehandler = this.saveLoader.getSaveLoader(p_71371_1_, false);
        WorldInfo worldinfo = isavehandler.loadWorldInfo();

        if (worldinfo == null && p_71371_3_ != null)
        {
            worldinfo = new WorldInfo(p_71371_3_, p_71371_1_);
            isavehandler.saveWorldInfo(worldinfo);
        }

        if (p_71371_3_ == null)
        {
            p_71371_3_ = new WorldSettings(worldinfo);
        }

        try
        {
            this.theIntegratedServer = new IntegratedServer(this, p_71371_1_, p_71371_2_, p_71371_3_);
            this.theIntegratedServer.startServerThread();
            this.integratedServerIsRunning = true;
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Starting integrated server");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Starting integrated server");
            crashreportcategory.addCrashSection("Level ID", p_71371_1_);
            crashreportcategory.addCrashSection("Level Name", p_71371_2_);
            throw new ReportedException(crashreport);
        }

        this.loadingScreen.displayProgressMessage(I18n.format("menu.loadingLevel", new Object[0]));

        while (!this.theIntegratedServer.serverIsInRunLoop())
        {
            if (!StartupQuery.check())
            {
                loadWorld(null);
                displayGuiScreen(null);
                return;
            }
            String s2 = this.theIntegratedServer.getUserMessage();

            if (s2 != null)
            {
                this.loadingScreen.resetProgresAndWorkingMessage(I18n.format(s2, new Object[0]));
            }
            else
            {
                this.loadingScreen.resetProgresAndWorkingMessage("");
            }

            try
            {
                Thread.sleep(200L);
            }
            catch (InterruptedException interruptedexception)
            {
                ;
            }
        }

        this.displayGuiScreen((GuiScreen)null);
        SocketAddress socketaddress = this.theIntegratedServer.func_147137_ag().addLocalEndpoint();
        NetworkManager networkmanager = NetworkManager.provideLocalClient(socketaddress);
        networkmanager.setNetHandler(new NetHandlerLoginClient(networkmanager, this, (GuiScreen)null));
        networkmanager.scheduleOutboundPacket(new C00Handshake(5, socketaddress.toString(), 0, EnumConnectionState.LOGIN), new GenericFutureListener[0]);
        networkmanager.scheduleOutboundPacket(new C00PacketLoginStart(this.getSession().func_148256_e()), new GenericFutureListener[0]);
        this.myNetworkManager = networkmanager;
    }

    /**
     * unloads the current world first
     */
    public void loadWorld(WorldClient p_71403_1_)
    {
        this.loadWorld(p_71403_1_, "");
    }

    /**
     * par2Str is displayed on the loading screen to the user unloads the current world first
     */
    public void loadWorld(WorldClient p_71353_1_, String p_71353_2_)
    {
        if (theWorld != null)
        {
            net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(new net.minecraftforge.event.world.WorldEvent.Unload(theWorld));
        }

        if (p_71353_1_ == null)
        {
            NetHandlerPlayClient nethandlerplayclient = this.getNetHandler();

            if (nethandlerplayclient != null)
            {
                nethandlerplayclient.cleanup();
            }

            if (this.theIntegratedServer != null)
            {
                this.theIntegratedServer.initiateShutdown();
                if (loadingScreen != null)
                {
                    this.loadingScreen.resetProgresAndWorkingMessage(I18n.format("forge.client.shutdown.internal"));
                }
                while (!theIntegratedServer.isServerStopped())
                {
                    try
                    {
                        Thread.sleep(10);
                    }
                    catch (InterruptedException ie) {}
                }
            }

            this.theIntegratedServer = null;
            this.guiAchievement.func_146257_b();
            this.entityRenderer.getMapItemRenderer().func_148249_a();
        }

        this.renderViewEntity = null;
        this.myNetworkManager = null;

        if (this.loadingScreen != null)
        {
            this.loadingScreen.resetProgressAndMessage(p_71353_2_);
            this.loadingScreen.resetProgresAndWorkingMessage("");
        }

        if (p_71353_1_ == null && this.theWorld != null)
        {
            if (this.mcResourcePackRepository.func_148530_e() != null)
            {
                this.scheduleResourcesRefresh();
            }

            this.mcResourcePackRepository.func_148529_f();
            this.setServerData((ServerData)null);
            this.integratedServerIsRunning = false;
            FMLClientHandler.instance().handleClientWorldClosing(this.theWorld);
        }

        this.mcSoundHandler.stopSounds();
        this.theWorld = p_71353_1_;

        if (p_71353_1_ != null)
        {
            if (this.renderGlobal != null)
            {
                this.renderGlobal.setWorldAndLoadRenderers(p_71353_1_);
            }

            if (this.effectRenderer != null)
            {
                this.effectRenderer.clearEffects(p_71353_1_);
            }

            if (this.thePlayer == null)
            {
                this.thePlayer = this.playerController.func_147493_a(p_71353_1_, new StatFileWriter());
                this.playerController.flipPlayer(this.thePlayer);
            }

            this.thePlayer.preparePlayerToSpawn();
            p_71353_1_.spawnEntityInWorld(this.thePlayer);
            this.thePlayer.movementInput = new MovementInputFromOptions(this.gameSettings);
            this.playerController.setPlayerCapabilities(this.thePlayer);
            this.renderViewEntity = this.thePlayer;
        }
        else
        {
            this.saveLoader.flushCache();
            this.thePlayer = null;
        }

        System.gc();
        this.systemTime = 0L;
    }

    /**
     * A String of renderGlobal.getDebugInfoRenders
     */
    public String debugInfoRenders()
    {
        return this.renderGlobal.getDebugInfoRenders();
    }

    /**
     * Gets the information in the F3 menu about how many entities are infront/around you
     */
    public String getEntityDebug()
    {
        return this.renderGlobal.getDebugInfoEntities();
    }

    /**
     * Gets the name of the world's current chunk provider
     */
    public String getWorldProviderName()
    {
        return this.theWorld.getProviderName();
    }

    /**
     * A String of how many entities are in the world
     */
    public String debugInfoEntities()
    {
        return "P: " + this.effectRenderer.getStatistics() + ". T: " + this.theWorld.getDebugLoadedEntities();
    }

    public void setDimensionAndSpawnPlayer(int p_71354_1_)
    {
        this.theWorld.setSpawnLocation();
        this.theWorld.removeAllEntities();
        int j = 0;
        String s = null;

        if (this.thePlayer != null)
        {
            j = this.thePlayer.getEntityId();
            this.theWorld.removeEntity(this.thePlayer);
            s = this.thePlayer.func_142021_k();
        }

        this.renderViewEntity = null;
        this.thePlayer = this.playerController.func_147493_a(this.theWorld, this.thePlayer == null ? new StatFileWriter() : this.thePlayer.getStatFileWriter());
        this.thePlayer.dimension = p_71354_1_;
        this.renderViewEntity = this.thePlayer;
        this.thePlayer.preparePlayerToSpawn();
        this.thePlayer.func_142020_c(s);
        this.theWorld.spawnEntityInWorld(this.thePlayer);
        this.playerController.flipPlayer(this.thePlayer);
        this.thePlayer.movementInput = new MovementInputFromOptions(this.gameSettings);
        this.thePlayer.setEntityId(j);
        this.playerController.setPlayerCapabilities(this.thePlayer);

        if (this.currentScreen instanceof GuiGameOver)
        {
            this.displayGuiScreen((GuiScreen)null);
        }
    }

    /**
     * Gets whether this is a demo or not.
     */
    public final boolean isDemo()
    {
        return this.isDemo;
    }

    public NetHandlerPlayClient getNetHandler()
    {
        return this.thePlayer != null ? this.thePlayer.sendQueue : null;
    }

    public static boolean isGuiEnabled()
    {
        /** Set to 'this' in Minecraft constructor; used by some settings get methods */
        return theMinecraft == null || !theMinecraft.gameSettings.hideGUI;
    }

    public static boolean isFancyGraphicsEnabled()
    {
        return theMinecraft != null && theMinecraft.gameSettings.fancyGraphics;
    }

    /**
     * Returns if ambient occlusion is enabled
     */
    public static boolean isAmbientOcclusionEnabled()
    {
        return theMinecraft != null && theMinecraft.gameSettings.ambientOcclusion != 0;
    }

    private void func_147112_ai()
    {
        if (this.objectMouseOver != null)
        {
            boolean flag = this.thePlayer.capabilities.isCreativeMode;
            int j;

            if (!net.minecraftforge.common.ForgeHooks.onPickBlock(this.objectMouseOver, this.thePlayer, this.theWorld)) return;
            // We delete this code wholly instead of commenting it out, to make sure we detect changes in it between MC versions
            if (flag)
            {
                j = this.thePlayer.inventoryContainer.inventorySlots.size() - 9 + this.thePlayer.inventory.currentItem;
                this.playerController.sendSlotPacket(this.thePlayer.inventory.getStackInSlot(this.thePlayer.inventory.currentItem), j);
            }
        }
    }

    /**
     * adds core server Info (GL version , Texture pack, isModded, type), and the worldInfo to the crash report
     */
    public CrashReport addGraphicsAndWorldToCrashReport(CrashReport p_71396_1_)
    {
        p_71396_1_.getCategory().addCrashSectionCallable("Launched Version", new Callable()
        {
            private static final String __OBFID = "CL_00000643";
            public String call()
            {
                return Minecraft.this.launchedVersion;
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("LWJGL", new Callable()
        {
            private static final String __OBFID = "CL_00000644";
            public String call()
            {
                return Sys.getVersion();
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("OpenGL", new Callable()
        {
            private static final String __OBFID = "CL_00000645";
            public String call()
            {
                return GL11.glGetString(GL11.GL_RENDERER) + " GL version " + GL11.glGetString(GL11.GL_VERSION) + ", " + GL11.glGetString(GL11.GL_VENDOR);
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("GL Caps", new Callable()
        {
            private static final String __OBFID = "CL_00000646";
            public String call()
            {
                return OpenGlHelper.func_153172_c();
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Is Modded", new Callable()
        {
            private static final String __OBFID = "CL_00000647";
            public String call()
            {
                String s = ClientBrandRetriever.getClientModName();
                return !s.equals("vanilla") ? "Definitely; Client brand changed to \'" + s + "\'" : (Minecraft.class.getSigners() == null ? "Very likely; Jar signature invalidated" : "Probably not. Jar signature remains and client brand is untouched.");
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Type", new Callable()
        {
            private static final String __OBFID = "CL_00000633";
            public String call()
            {
                return "Client (map_client.txt)";
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Resource Packs", new Callable()
        {
            private static final String __OBFID = "CL_00000634";
            public String call()
            {
                return Minecraft.this.gameSettings.resourcePacks.toString();
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Current Language", new Callable()
        {
            private static final String __OBFID = "CL_00000635";
            public String call()
            {
                return Minecraft.this.mcLanguageManager.getCurrentLanguage().toString();
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Profiler Position", new Callable()
        {
            private static final String __OBFID = "CL_00000636";
            public String call()
            {
                return Minecraft.this.mcProfiler.profilingEnabled ? Minecraft.this.mcProfiler.getNameOfLastSection() : "N/A (disabled)";
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Vec3 Pool Size", new Callable()
        {
            private static final String __OBFID = "CL_00000637";
            public String call()
            {
                byte b0 = 0;
                int i = 56 * b0;
                int j = i / 1024 / 1024;
                byte b1 = 0;
                int k = 56 * b1;
                int l = k / 1024 / 1024;
                return b0 + " (" + i + " bytes; " + j + " MB) allocated, " + b1 + " (" + k + " bytes; " + l + " MB) used";
            }
        });
        p_71396_1_.getCategory().addCrashSectionCallable("Anisotropic Filtering", new Callable()
        {
            private static final String __OBFID = "CL_00001853";
            public String func_152388_a()
            {
                return Minecraft.this.gameSettings.anisotropicFiltering == 1 ? "Off (1)" : "On (" + Minecraft.this.gameSettings.anisotropicFiltering + ")";
            }
            public Object call()
            {
                return this.func_152388_a();
            }
        });

        if (this.theWorld != null)
        {
            this.theWorld.addWorldInfoToCrashReport(p_71396_1_);
        }

        return p_71396_1_;
    }

    /**
     * Return the singleton Minecraft instance for the game
     */
    public static Minecraft getMinecraft()
    {
        /** Set to 'this' in Minecraft constructor; used by some settings get methods */
        return theMinecraft;
    }

    public void scheduleResourcesRefresh()
    {
        this.refreshTexturePacksScheduled = true;
    }

    public void addServerStatsToSnooper(PlayerUsageSnooper p_70000_1_)
    {
        p_70000_1_.func_152768_a("fps", Integer.valueOf(debugFPS));
        p_70000_1_.func_152768_a("vsync_enabled", Boolean.valueOf(this.gameSettings.enableVsync));
        p_70000_1_.func_152768_a("display_frequency", Integer.valueOf(Display.getDisplayMode().getFrequency()));
        p_70000_1_.func_152768_a("display_type", this.fullscreen ? "fullscreen" : "windowed");
        p_70000_1_.func_152768_a("run_time", Long.valueOf((MinecraftServer.getSystemTimeMillis() - p_70000_1_.getMinecraftStartTimeMillis()) / 60L * 1000L));
        p_70000_1_.func_152768_a("resource_packs", Integer.valueOf(this.mcResourcePackRepository.getRepositoryEntries().size()));
        int i = 0;
        Iterator iterator = this.mcResourcePackRepository.getRepositoryEntries().iterator();

        while (iterator.hasNext())
        {
            ResourcePackRepository.Entry entry = (ResourcePackRepository.Entry)iterator.next();
            p_70000_1_.func_152768_a("resource_pack[" + i++ + "]", entry.getResourcePackName());
        }

        if (this.theIntegratedServer != null && this.theIntegratedServer.getPlayerUsageSnooper() != null)
        {
            p_70000_1_.func_152768_a("snooper_partner", this.theIntegratedServer.getPlayerUsageSnooper().getUniqueID());
        }
    }

    public void addServerTypeToSnooper(PlayerUsageSnooper p_70001_1_)
    {
        p_70001_1_.func_152767_b("opengl_version", GL11.glGetString(GL11.GL_VERSION));
        p_70001_1_.func_152767_b("opengl_vendor", GL11.glGetString(GL11.GL_VENDOR));
        p_70001_1_.func_152767_b("client_brand", ClientBrandRetriever.getClientModName());
        p_70001_1_.func_152767_b("launched_version", this.launchedVersion);
        ContextCapabilities contextcapabilities = GLContext.getCapabilities();
        p_70001_1_.func_152767_b("gl_caps[ARB_arrays_of_arrays]", Boolean.valueOf(contextcapabilities.GL_ARB_arrays_of_arrays));
        p_70001_1_.func_152767_b("gl_caps[ARB_base_instance]", Boolean.valueOf(contextcapabilities.GL_ARB_base_instance));
        p_70001_1_.func_152767_b("gl_caps[ARB_blend_func_extended]", Boolean.valueOf(contextcapabilities.GL_ARB_blend_func_extended));
        p_70001_1_.func_152767_b("gl_caps[ARB_clear_buffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_clear_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_color_buffer_float]", Boolean.valueOf(contextcapabilities.GL_ARB_color_buffer_float));
        p_70001_1_.func_152767_b("gl_caps[ARB_compatibility]", Boolean.valueOf(contextcapabilities.GL_ARB_compatibility));
        p_70001_1_.func_152767_b("gl_caps[ARB_compressed_texture_pixel_storage]", Boolean.valueOf(contextcapabilities.GL_ARB_compressed_texture_pixel_storage));
        p_70001_1_.func_152767_b("gl_caps[ARB_compute_shader]", Boolean.valueOf(contextcapabilities.GL_ARB_compute_shader));
        p_70001_1_.func_152767_b("gl_caps[ARB_copy_buffer]", Boolean.valueOf(contextcapabilities.GL_ARB_copy_buffer));
        p_70001_1_.func_152767_b("gl_caps[ARB_copy_image]", Boolean.valueOf(contextcapabilities.GL_ARB_copy_image));
        p_70001_1_.func_152767_b("gl_caps[ARB_depth_buffer_float]", Boolean.valueOf(contextcapabilities.GL_ARB_depth_buffer_float));
        p_70001_1_.func_152767_b("gl_caps[ARB_compute_shader]", Boolean.valueOf(contextcapabilities.GL_ARB_compute_shader));
        p_70001_1_.func_152767_b("gl_caps[ARB_copy_buffer]", Boolean.valueOf(contextcapabilities.GL_ARB_copy_buffer));
        p_70001_1_.func_152767_b("gl_caps[ARB_copy_image]", Boolean.valueOf(contextcapabilities.GL_ARB_copy_image));
        p_70001_1_.func_152767_b("gl_caps[ARB_depth_buffer_float]", Boolean.valueOf(contextcapabilities.GL_ARB_depth_buffer_float));
        p_70001_1_.func_152767_b("gl_caps[ARB_depth_clamp]", Boolean.valueOf(contextcapabilities.GL_ARB_depth_clamp));
        p_70001_1_.func_152767_b("gl_caps[ARB_depth_texture]", Boolean.valueOf(contextcapabilities.GL_ARB_depth_texture));
        p_70001_1_.func_152767_b("gl_caps[ARB_draw_buffers]", Boolean.valueOf(contextcapabilities.GL_ARB_draw_buffers));
        p_70001_1_.func_152767_b("gl_caps[ARB_draw_buffers_blend]", Boolean.valueOf(contextcapabilities.GL_ARB_draw_buffers_blend));
        p_70001_1_.func_152767_b("gl_caps[ARB_draw_elements_base_vertex]", Boolean.valueOf(contextcapabilities.GL_ARB_draw_elements_base_vertex));
        p_70001_1_.func_152767_b("gl_caps[ARB_draw_indirect]", Boolean.valueOf(contextcapabilities.GL_ARB_draw_indirect));
        p_70001_1_.func_152767_b("gl_caps[ARB_draw_instanced]", Boolean.valueOf(contextcapabilities.GL_ARB_draw_instanced));
        p_70001_1_.func_152767_b("gl_caps[ARB_explicit_attrib_location]", Boolean.valueOf(contextcapabilities.GL_ARB_explicit_attrib_location));
        p_70001_1_.func_152767_b("gl_caps[ARB_explicit_uniform_location]", Boolean.valueOf(contextcapabilities.GL_ARB_explicit_uniform_location));
        p_70001_1_.func_152767_b("gl_caps[ARB_fragment_layer_viewport]", Boolean.valueOf(contextcapabilities.GL_ARB_fragment_layer_viewport));
        p_70001_1_.func_152767_b("gl_caps[ARB_fragment_program]", Boolean.valueOf(contextcapabilities.GL_ARB_fragment_program));
        p_70001_1_.func_152767_b("gl_caps[ARB_fragment_shader]", Boolean.valueOf(contextcapabilities.GL_ARB_fragment_shader));
        p_70001_1_.func_152767_b("gl_caps[ARB_fragment_program_shadow]", Boolean.valueOf(contextcapabilities.GL_ARB_fragment_program_shadow));
        p_70001_1_.func_152767_b("gl_caps[ARB_framebuffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_framebuffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_framebuffer_sRGB]", Boolean.valueOf(contextcapabilities.GL_ARB_framebuffer_sRGB));
        p_70001_1_.func_152767_b("gl_caps[ARB_geometry_shader4]", Boolean.valueOf(contextcapabilities.GL_ARB_geometry_shader4));
        p_70001_1_.func_152767_b("gl_caps[ARB_gpu_shader5]", Boolean.valueOf(contextcapabilities.GL_ARB_gpu_shader5));
        p_70001_1_.func_152767_b("gl_caps[ARB_half_float_pixel]", Boolean.valueOf(contextcapabilities.GL_ARB_half_float_pixel));
        p_70001_1_.func_152767_b("gl_caps[ARB_half_float_vertex]", Boolean.valueOf(contextcapabilities.GL_ARB_half_float_vertex));
        p_70001_1_.func_152767_b("gl_caps[ARB_instanced_arrays]", Boolean.valueOf(contextcapabilities.GL_ARB_instanced_arrays));
        p_70001_1_.func_152767_b("gl_caps[ARB_map_buffer_alignment]", Boolean.valueOf(contextcapabilities.GL_ARB_map_buffer_alignment));
        p_70001_1_.func_152767_b("gl_caps[ARB_map_buffer_range]", Boolean.valueOf(contextcapabilities.GL_ARB_map_buffer_range));
        p_70001_1_.func_152767_b("gl_caps[ARB_multisample]", Boolean.valueOf(contextcapabilities.GL_ARB_multisample));
        p_70001_1_.func_152767_b("gl_caps[ARB_multitexture]", Boolean.valueOf(contextcapabilities.GL_ARB_multitexture));
        p_70001_1_.func_152767_b("gl_caps[ARB_occlusion_query2]", Boolean.valueOf(contextcapabilities.GL_ARB_occlusion_query2));
        p_70001_1_.func_152767_b("gl_caps[ARB_pixel_buffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_pixel_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_seamless_cube_map]", Boolean.valueOf(contextcapabilities.GL_ARB_seamless_cube_map));
        p_70001_1_.func_152767_b("gl_caps[ARB_shader_objects]", Boolean.valueOf(contextcapabilities.GL_ARB_shader_objects));
        p_70001_1_.func_152767_b("gl_caps[ARB_shader_stencil_export]", Boolean.valueOf(contextcapabilities.GL_ARB_shader_stencil_export));
        p_70001_1_.func_152767_b("gl_caps[ARB_shader_texture_lod]", Boolean.valueOf(contextcapabilities.GL_ARB_shader_texture_lod));
        p_70001_1_.func_152767_b("gl_caps[ARB_shadow]", Boolean.valueOf(contextcapabilities.GL_ARB_shadow));
        p_70001_1_.func_152767_b("gl_caps[ARB_shadow_ambient]", Boolean.valueOf(contextcapabilities.GL_ARB_shadow_ambient));
        p_70001_1_.func_152767_b("gl_caps[ARB_stencil_texturing]", Boolean.valueOf(contextcapabilities.GL_ARB_stencil_texturing));
        p_70001_1_.func_152767_b("gl_caps[ARB_sync]", Boolean.valueOf(contextcapabilities.GL_ARB_sync));
        p_70001_1_.func_152767_b("gl_caps[ARB_tessellation_shader]", Boolean.valueOf(contextcapabilities.GL_ARB_tessellation_shader));
        p_70001_1_.func_152767_b("gl_caps[ARB_texture_border_clamp]", Boolean.valueOf(contextcapabilities.GL_ARB_texture_border_clamp));
        p_70001_1_.func_152767_b("gl_caps[ARB_texture_buffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_texture_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_texture_cube_map]", Boolean.valueOf(contextcapabilities.GL_ARB_texture_cube_map));
        p_70001_1_.func_152767_b("gl_caps[ARB_texture_cube_map_array]", Boolean.valueOf(contextcapabilities.GL_ARB_texture_cube_map_array));
        p_70001_1_.func_152767_b("gl_caps[ARB_texture_non_power_of_two]", Boolean.valueOf(contextcapabilities.GL_ARB_texture_non_power_of_two));
        p_70001_1_.func_152767_b("gl_caps[ARB_uniform_buffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_uniform_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_vertex_blend]", Boolean.valueOf(contextcapabilities.GL_ARB_vertex_blend));
        p_70001_1_.func_152767_b("gl_caps[ARB_vertex_buffer_object]", Boolean.valueOf(contextcapabilities.GL_ARB_vertex_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[ARB_vertex_program]", Boolean.valueOf(contextcapabilities.GL_ARB_vertex_program));
        p_70001_1_.func_152767_b("gl_caps[ARB_vertex_shader]", Boolean.valueOf(contextcapabilities.GL_ARB_vertex_shader));
        p_70001_1_.func_152767_b("gl_caps[EXT_bindable_uniform]", Boolean.valueOf(contextcapabilities.GL_EXT_bindable_uniform));
        p_70001_1_.func_152767_b("gl_caps[EXT_blend_equation_separate]", Boolean.valueOf(contextcapabilities.GL_EXT_blend_equation_separate));
        p_70001_1_.func_152767_b("gl_caps[EXT_blend_func_separate]", Boolean.valueOf(contextcapabilities.GL_EXT_blend_func_separate));
        p_70001_1_.func_152767_b("gl_caps[EXT_blend_minmax]", Boolean.valueOf(contextcapabilities.GL_EXT_blend_minmax));
        p_70001_1_.func_152767_b("gl_caps[EXT_blend_subtract]", Boolean.valueOf(contextcapabilities.GL_EXT_blend_subtract));
        p_70001_1_.func_152767_b("gl_caps[EXT_draw_instanced]", Boolean.valueOf(contextcapabilities.GL_EXT_draw_instanced));
        p_70001_1_.func_152767_b("gl_caps[EXT_framebuffer_multisample]", Boolean.valueOf(contextcapabilities.GL_EXT_framebuffer_multisample));
        p_70001_1_.func_152767_b("gl_caps[EXT_framebuffer_object]", Boolean.valueOf(contextcapabilities.GL_EXT_framebuffer_object));
        p_70001_1_.func_152767_b("gl_caps[EXT_framebuffer_sRGB]", Boolean.valueOf(contextcapabilities.GL_EXT_framebuffer_sRGB));
        p_70001_1_.func_152767_b("gl_caps[EXT_geometry_shader4]", Boolean.valueOf(contextcapabilities.GL_EXT_geometry_shader4));
        p_70001_1_.func_152767_b("gl_caps[EXT_gpu_program_parameters]", Boolean.valueOf(contextcapabilities.GL_EXT_gpu_program_parameters));
        p_70001_1_.func_152767_b("gl_caps[EXT_gpu_shader4]", Boolean.valueOf(contextcapabilities.GL_EXT_gpu_shader4));
        p_70001_1_.func_152767_b("gl_caps[EXT_multi_draw_arrays]", Boolean.valueOf(contextcapabilities.GL_EXT_multi_draw_arrays));
        p_70001_1_.func_152767_b("gl_caps[EXT_packed_depth_stencil]", Boolean.valueOf(contextcapabilities.GL_EXT_packed_depth_stencil));
        p_70001_1_.func_152767_b("gl_caps[EXT_paletted_texture]", Boolean.valueOf(contextcapabilities.GL_EXT_paletted_texture));
        p_70001_1_.func_152767_b("gl_caps[EXT_rescale_normal]", Boolean.valueOf(contextcapabilities.GL_EXT_rescale_normal));
        p_70001_1_.func_152767_b("gl_caps[EXT_separate_shader_objects]", Boolean.valueOf(contextcapabilities.GL_EXT_separate_shader_objects));
        p_70001_1_.func_152767_b("gl_caps[EXT_shader_image_load_store]", Boolean.valueOf(contextcapabilities.GL_EXT_shader_image_load_store));
        p_70001_1_.func_152767_b("gl_caps[EXT_shadow_funcs]", Boolean.valueOf(contextcapabilities.GL_EXT_shadow_funcs));
        p_70001_1_.func_152767_b("gl_caps[EXT_shared_texture_palette]", Boolean.valueOf(contextcapabilities.GL_EXT_shared_texture_palette));
        p_70001_1_.func_152767_b("gl_caps[EXT_stencil_clear_tag]", Boolean.valueOf(contextcapabilities.GL_EXT_stencil_clear_tag));
        p_70001_1_.func_152767_b("gl_caps[EXT_stencil_two_side]", Boolean.valueOf(contextcapabilities.GL_EXT_stencil_two_side));
        p_70001_1_.func_152767_b("gl_caps[EXT_stencil_wrap]", Boolean.valueOf(contextcapabilities.GL_EXT_stencil_wrap));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_3d]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_3d));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_array]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_array));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_buffer_object]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_buffer_object));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_filter_anisotropic]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_filter_anisotropic));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_integer]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_integer));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_lod_bias]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_lod_bias));
        p_70001_1_.func_152767_b("gl_caps[EXT_texture_sRGB]", Boolean.valueOf(contextcapabilities.GL_EXT_texture_sRGB));
        p_70001_1_.func_152767_b("gl_caps[EXT_vertex_shader]", Boolean.valueOf(contextcapabilities.GL_EXT_vertex_shader));
        p_70001_1_.func_152767_b("gl_caps[EXT_vertex_weighting]", Boolean.valueOf(contextcapabilities.GL_EXT_vertex_weighting));
        p_70001_1_.func_152767_b("gl_caps[gl_max_vertex_uniforms]", Integer.valueOf(GL11.glGetInteger(GL20.GL_MAX_VERTEX_UNIFORM_COMPONENTS)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_caps[gl_max_fragment_uniforms]", Integer.valueOf(GL11.glGetInteger(GL20.GL_MAX_FRAGMENT_UNIFORM_COMPONENTS)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_caps[gl_max_vertex_attribs]", Integer.valueOf(GL11.glGetInteger(GL20.GL_MAX_VERTEX_ATTRIBS)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_caps[gl_max_vertex_texture_image_units]", Integer.valueOf(GL11.glGetInteger(GL20.GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_caps[gl_max_texture_image_units]", Integer.valueOf(GL11.glGetInteger(GL20.GL_MAX_TEXTURE_IMAGE_UNITS)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_caps[gl_max_texture_image_units]", Integer.valueOf(GL11.glGetInteger(35071)));
        GL11.glGetError();
        p_70001_1_.func_152767_b("gl_max_texture_size", Integer.valueOf(getGLMaximumTextureSize()));
    }

    //Forge: Adds a optimization to the getGLMaximumTextureSize, only calculate it once.
    private static int max_texture_size = -1;
    /**
     * Used in the usage snooper.
     */
    public static int getGLMaximumTextureSize()
    {
        if (max_texture_size != -1)
        {
            return max_texture_size;
        }

        for (int i = 16384; i > 0; i >>= 1)
        {
            GL11.glTexImage2D(GL11.GL_PROXY_TEXTURE_2D, 0, GL11.GL_RGBA, i, i, 0, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, (ByteBuffer)null);
            int j = GL11.glGetTexLevelParameteri(GL11.GL_PROXY_TEXTURE_2D, 0, GL11.GL_TEXTURE_WIDTH);

            if (j != 0)
            {
                max_texture_size = i;
                return i;
            }
        }

        return -1;
    }

    /**
     * Returns whether snooping is enabled or not.
     */
    public boolean isSnooperEnabled()
    {
        return this.gameSettings.snooperEnabled;
    }

    /**
     * Set the current ServerData instance.
     */
    public void setServerData(ServerData p_71351_1_)
    {
        this.currentServerData = p_71351_1_;
    }

    public ServerData func_147104_D()
    {
        return this.currentServerData;
    }

    public boolean isIntegratedServerRunning()
    {
        return this.integratedServerIsRunning;
    }

    /**
     * Returns true if there is only one player playing, and the current server is the integrated one.
     */
    public boolean isSingleplayer()
    {
        return this.integratedServerIsRunning && this.theIntegratedServer != null;
    }

    /**
     * Returns the currently running integrated server
     */
    public IntegratedServer getIntegratedServer()
    {
        return this.theIntegratedServer;
    }

    public static void stopIntegratedServer()
    {
        if (theMinecraft != null)
        {
            IntegratedServer integratedserver = theMinecraft.getIntegratedServer();

            if (integratedserver != null)
            {
                integratedserver.stopServer();
            }
        }
    }

    /**
     * Returns the PlayerUsageSnooper instance.
     */
    public PlayerUsageSnooper getPlayerUsageSnooper()
    {
        return this.usageSnooper;
    }

    /**
     * Gets the system time in milliseconds.
     */
    public static long getSystemTime()
    {
        return Sys.getTime() * 1000L / Sys.getTimerResolution();
    }

    /**
     * Returns whether we're in full screen or not.
     */
    public boolean isFullScreen()
    {
        return this.fullscreen;
    }

    public Session getSession()
    {
        return this.session;
    }

    public Multimap func_152341_N()
    {
        return this.field_152356_J;
    }

    public Proxy getProxy()
    {
        return this.proxy;
    }

    public TextureManager getTextureManager()
    {
        return this.renderEngine;
    }

    public IResourceManager getResourceManager()
    {
        return this.mcResourceManager;
    }

    public ResourcePackRepository getResourcePackRepository()
    {
        return this.mcResourcePackRepository;
    }

    public LanguageManager getLanguageManager()
    {
        return this.mcLanguageManager;
    }

    public TextureMap getTextureMapBlocks()
    {
        return this.textureMapBlocks;
    }

    public boolean isJava64bit()
    {
        return this.jvm64bit;
    }

    public boolean isGamePaused()
    {
        return this.isGamePaused;
    }

    public SoundHandler getSoundHandler()
    {
        return this.mcSoundHandler;
    }

    public MusicTicker.MusicType func_147109_W()
    {
        return this.currentScreen instanceof GuiWinGame ? MusicTicker.MusicType.CREDITS : (this.thePlayer != null ? (this.thePlayer.worldObj.provider instanceof WorldProviderHell ? MusicTicker.MusicType.NETHER : (this.thePlayer.worldObj.provider instanceof WorldProviderEnd ? (BossStatus.bossName != null && BossStatus.statusBarTime > 0 ? MusicTicker.MusicType.END_BOSS : MusicTicker.MusicType.END) : (this.thePlayer.capabilities.isCreativeMode && this.thePlayer.capabilities.allowFlying ? MusicTicker.MusicType.CREATIVE : MusicTicker.MusicType.GAME))) : MusicTicker.MusicType.MENU);
    }

    public IStream func_152346_Z()
    {
        return this.field_152353_at;
    }

    public void func_152348_aa()
    {
        int i = Keyboard.getEventKey();

        if (i != 0 && !Keyboard.isRepeatEvent())
        {
            if (!(this.currentScreen instanceof GuiControls) || ((GuiControls)this.currentScreen).field_152177_g <= getSystemTime() - 20L)
            {
                if (Keyboard.getEventKeyState())
                {
                    if (i == this.gameSettings.field_152396_an.getKeyCode())
                    {
                        if (this.func_152346_Z().func_152934_n())
                        {
                            this.func_152346_Z().func_152914_u();
                        }
                        else if (this.func_152346_Z().func_152924_m())
                        {
                            this.displayGuiScreen(new GuiYesNo(new GuiYesNoCallback()
                            {
                                private static final String __OBFID = "CL_00001852";
                                public void confirmClicked(boolean p_73878_1_, int p_73878_2_)
                                {
                                    if (p_73878_1_)
                                    {
                                        Minecraft.this.func_152346_Z().func_152930_t();
                                    }

                                    Minecraft.this.displayGuiScreen((GuiScreen)null);
                                }
                            }, I18n.format("stream.confirm_start", new Object[0]), "", 0));
                        }
                        else if (this.func_152346_Z().func_152928_D() && this.func_152346_Z().func_152936_l())
                        {
                            if (this.theWorld != null)
                            {
                                this.ingameGUI.getChatGUI().printChatMessage(new ChatComponentText("Not ready to start streaming yet!"));
                            }
                        }
                        else
                        {
                            GuiStreamUnavailable.func_152321_a(this.currentScreen);
                        }
                    }
                    else if (i == this.gameSettings.field_152397_ao.getKeyCode())
                    {
                        if (this.func_152346_Z().func_152934_n())
                        {
                            if (this.func_152346_Z().func_152919_o())
                            {
                                this.func_152346_Z().func_152933_r();
                            }
                            else
                            {
                                this.func_152346_Z().func_152916_q();
                            }
                        }
                    }
                    else if (i == this.gameSettings.field_152398_ap.getKeyCode())
                    {
                        if (this.func_152346_Z().func_152934_n())
                        {
                            this.func_152346_Z().func_152931_p();
                        }
                    }
                    else if (i == this.gameSettings.field_152399_aq.getKeyCode())
                    {
                        this.field_152353_at.func_152910_a(true);
                    }
                    else if (i == this.gameSettings.field_152395_am.getKeyCode())
                    {
                        this.toggleFullscreen();
                    }
                    else if (i == this.gameSettings.keyBindScreenshot.getKeyCode())
                    {
                        this.ingameGUI.getChatGUI().printChatMessage(ScreenShotHelper.saveScreenshot(this.mcDataDir, this.displayWidth, this.displayHeight, this.framebufferMc));
                    }
                }
                else if (i == this.gameSettings.field_152399_aq.getKeyCode())
                {
                    this.field_152353_at.func_152910_a(false);
                }
            }
        }
    }

    public ListenableFuture func_152343_a(Callable p_152343_1_)
    {
        Validate.notNull(p_152343_1_);

        if (!this.func_152345_ab())
        {
            ListenableFutureTask listenablefuturetask = ListenableFutureTask.create(p_152343_1_);
            Queue queue = this.field_152351_aB;

            synchronized (this.field_152351_aB)
            {
                this.field_152351_aB.add(listenablefuturetask);
                return listenablefuturetask;
            }
        }
        else
        {
            try
            {
                return Futures.immediateFuture(p_152343_1_.call());
            }
            catch (Exception exception)
            {
                return Futures.immediateFailedCheckedFuture(exception);
            }
        }
    }

    public ListenableFuture func_152344_a(Runnable p_152344_1_)
    {
        Validate.notNull(p_152344_1_);
        return this.func_152343_a(Executors.callable(p_152344_1_));
    }

    public boolean func_152345_ab()
    {
        return Thread.currentThread() == this.field_152352_aC;
    }

    public MinecraftSessionService func_152347_ac()
    {
        return this.field_152355_az;
    }

    public SkinManager func_152342_ad()
    {
        return this.field_152350_aA;
    }

    @SideOnly(Side.CLIENT)

    static final class SwitchMovingObjectType
        {
            static final int[] field_152390_a = new int[MovingObjectPosition.MovingObjectType.values().length];
            private static final String __OBFID = "CL_00000638";

            static
            {
                try
                {
                    field_152390_a[MovingObjectPosition.MovingObjectType.ENTITY.ordinal()] = 1;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_152390_a[MovingObjectPosition.MovingObjectType.BLOCK.ordinal()] = 2;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}