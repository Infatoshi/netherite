package net.minecraft.client.network;

import com.google.common.base.Charsets;
import com.mojang.authlib.GameProfile;
import cpw.mods.fml.common.network.handshake.NetworkDispatcher;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.util.concurrent.GenericFutureListener;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Map.Entry;
import net.minecraft.block.Block;
import net.minecraft.client.ClientBrandRetriever;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.entity.EntityOtherPlayerMP;
import net.minecraft.client.gui.GuiChat;
import net.minecraft.client.gui.GuiDisconnected;
import net.minecraft.client.gui.GuiDownloadTerrain;
import net.minecraft.client.gui.GuiMainMenu;
import net.minecraft.client.gui.GuiMerchant;
import net.minecraft.client.gui.GuiMultiplayer;
import net.minecraft.client.gui.GuiPlayerInfo;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiScreenDemo;
import net.minecraft.client.gui.GuiScreenRealmsProxy;
import net.minecraft.client.gui.GuiWinGame;
import net.minecraft.client.gui.GuiYesNo;
import net.minecraft.client.gui.GuiYesNoCallback;
import net.minecraft.client.gui.IProgressMeter;
import net.minecraft.client.gui.inventory.GuiContainerCreative;
import net.minecraft.client.multiplayer.PlayerControllerMP;
import net.minecraft.client.multiplayer.ServerData;
import net.minecraft.client.multiplayer.ServerList;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.particle.EntityCrit2FX;
import net.minecraft.client.particle.EntityPickupFX;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.settings.GameSettings;
import net.minecraft.client.stream.MetadataAchievement;
import net.minecraft.creativetab.CreativeTabs;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLeashKnot;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.NpcMerchant;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.BaseAttributeMap;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.ai.attributes.RangedAttribute;
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.item.EntityEnderPearl;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.entity.item.EntityFallingBlock;
import net.minecraft.entity.item.EntityFireworkRocket;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntityFishHook;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.entity.projectile.EntityWitherSkull;
import net.minecraft.inventory.AnimalChest;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.Item;
import net.minecraft.item.ItemMap;
import net.minecraft.item.ItemStack;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.INetHandlerPlayClient;
import net.minecraft.network.play.client.C00PacketKeepAlive;
import net.minecraft.network.play.client.C03PacketPlayer;
import net.minecraft.network.play.client.C0FPacketConfirmTransaction;
import net.minecraft.network.play.client.C17PacketCustomPayload;
import net.minecraft.network.play.server.S00PacketKeepAlive;
import net.minecraft.network.play.server.S01PacketJoinGame;
import net.minecraft.network.play.server.S02PacketChat;
import net.minecraft.network.play.server.S03PacketTimeUpdate;
import net.minecraft.network.play.server.S04PacketEntityEquipment;
import net.minecraft.network.play.server.S05PacketSpawnPosition;
import net.minecraft.network.play.server.S06PacketUpdateHealth;
import net.minecraft.network.play.server.S07PacketRespawn;
import net.minecraft.network.play.server.S08PacketPlayerPosLook;
import net.minecraft.network.play.server.S09PacketHeldItemChange;
import net.minecraft.network.play.server.S0APacketUseBed;
import net.minecraft.network.play.server.S0BPacketAnimation;
import net.minecraft.network.play.server.S0CPacketSpawnPlayer;
import net.minecraft.network.play.server.S0DPacketCollectItem;
import net.minecraft.network.play.server.S0EPacketSpawnObject;
import net.minecraft.network.play.server.S0FPacketSpawnMob;
import net.minecraft.network.play.server.S10PacketSpawnPainting;
import net.minecraft.network.play.server.S11PacketSpawnExperienceOrb;
import net.minecraft.network.play.server.S12PacketEntityVelocity;
import net.minecraft.network.play.server.S13PacketDestroyEntities;
import net.minecraft.network.play.server.S14PacketEntity;
import net.minecraft.network.play.server.S18PacketEntityTeleport;
import net.minecraft.network.play.server.S19PacketEntityHeadLook;
import net.minecraft.network.play.server.S19PacketEntityStatus;
import net.minecraft.network.play.server.S1BPacketEntityAttach;
import net.minecraft.network.play.server.S1CPacketEntityMetadata;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S1EPacketRemoveEntityEffect;
import net.minecraft.network.play.server.S1FPacketSetExperience;
import net.minecraft.network.play.server.S20PacketEntityProperties;
import net.minecraft.network.play.server.S21PacketChunkData;
import net.minecraft.network.play.server.S22PacketMultiBlockChange;
import net.minecraft.network.play.server.S23PacketBlockChange;
import net.minecraft.network.play.server.S24PacketBlockAction;
import net.minecraft.network.play.server.S25PacketBlockBreakAnim;
import net.minecraft.network.play.server.S26PacketMapChunkBulk;
import net.minecraft.network.play.server.S27PacketExplosion;
import net.minecraft.network.play.server.S28PacketEffect;
import net.minecraft.network.play.server.S29PacketSoundEffect;
import net.minecraft.network.play.server.S2APacketParticles;
import net.minecraft.network.play.server.S2BPacketChangeGameState;
import net.minecraft.network.play.server.S2CPacketSpawnGlobalEntity;
import net.minecraft.network.play.server.S2DPacketOpenWindow;
import net.minecraft.network.play.server.S2EPacketCloseWindow;
import net.minecraft.network.play.server.S2FPacketSetSlot;
import net.minecraft.network.play.server.S30PacketWindowItems;
import net.minecraft.network.play.server.S31PacketWindowProperty;
import net.minecraft.network.play.server.S32PacketConfirmTransaction;
import net.minecraft.network.play.server.S33PacketUpdateSign;
import net.minecraft.network.play.server.S34PacketMaps;
import net.minecraft.network.play.server.S35PacketUpdateTileEntity;
import net.minecraft.network.play.server.S36PacketSignEditorOpen;
import net.minecraft.network.play.server.S37PacketStatistics;
import net.minecraft.network.play.server.S38PacketPlayerListItem;
import net.minecraft.network.play.server.S39PacketPlayerAbilities;
import net.minecraft.network.play.server.S3APacketTabComplete;
import net.minecraft.network.play.server.S3BPacketScoreboardObjective;
import net.minecraft.network.play.server.S3CPacketUpdateScore;
import net.minecraft.network.play.server.S3DPacketDisplayScoreboard;
import net.minecraft.network.play.server.S3EPacketTeams;
import net.minecraft.network.play.server.S3FPacketCustomPayload;
import net.minecraft.network.play.server.S40PacketDisconnect;
import net.minecraft.potion.PotionEffect;
import net.minecraft.realms.DisconnectedOnlineScreen;
import net.minecraft.scoreboard.IScoreObjectiveCriteria;
import net.minecraft.scoreboard.Score;
import net.minecraft.scoreboard.ScoreObjective;
import net.minecraft.scoreboard.ScorePlayerTeam;
import net.minecraft.scoreboard.Scoreboard;
import net.minecraft.stats.Achievement;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatBase;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityBeacon;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityCommandBlock;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityDropper;
import net.minecraft.tileentity.TileEntityFlowerPot;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityHopper;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.tileentity.TileEntitySkull;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.MathHelper;
import net.minecraft.village.MerchantRecipeList;
import net.minecraft.world.Explosion;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.storage.ISaveHandler;
import net.minecraft.world.storage.MapData;
import net.minecraft.world.storage.MapStorage;
import net.minecraftforge.client.event.ClientChatReceivedEvent;
import net.minecraftforge.common.MinecraftForge;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class NetHandlerPlayClient implements INetHandlerPlayClient
{
    private static final Logger logger = LogManager.getLogger();
    /**
     * The NetworkManager instance used to communicate with the server (used only by handlePlayerPosLook to update
     * positioning and handleJoinGame to inform the server of the client distribution/mods)
     */
    private final NetworkManager netManager;
    /** Reference to the Minecraft instance, which many handler methods operate on */
    private Minecraft gameController;
    /** Reference to the current ClientWorld instance, which many handler methods operate on */
    private WorldClient clientWorldController;
    /**
     * True if the client has finished downloading terrain and may spawn. Set upon receipt of S08PacketPlayerPosLook,
     * reset upon respawning
     */
    private boolean doneLoadingTerrain;
    /** Origin of the central MapStorage serving as a public reference for WorldClient. Not used in this class */
    public MapStorage mapStorageOrigin = new MapStorage((ISaveHandler)null);
    /** A mapping from player names to their respective GuiPlayerInfo (specifies the clients response time to the server) */
    private Map playerInfoMap = new HashMap();
    /** An ArrayList of GuiPlayerInfo (includes all the players' GuiPlayerInfo on the current server) */
    public List playerInfoList = new ArrayList();
    public int currentServerMaxPlayers = 20;
    /**
     * Seems to be either null (integrated server) or an instance of either GuiMultiplayer (when connecting to a server)
     * or GuiScreenReamlsTOS (when connecting to MCO server)
     */
    private GuiScreen guiScreenServer;
    private boolean field_147308_k = false;
    /**
     * Just an ordinary random number generator, used to randomize audio pitch of item/orb pickup and randomize both
     * particlespawn offset and velocity
     */
    private Random avRandomizer = new Random();
    private static final String __OBFID = "CL_00000878";

    public NetHandlerPlayClient(Minecraft p_i45061_1_, GuiScreen p_i45061_2_, NetworkManager p_i45061_3_)
    {
        this.gameController = p_i45061_1_;
        this.guiScreenServer = p_i45061_2_;
        this.netManager = p_i45061_3_;
    }

    /**
     * Clears the WorldClient instance associated with this NetHandlerPlayClient
     */
    public void cleanup()
    {
        this.clientWorldController = null;
    }

    /**
     * For scheduled network tasks. Used in NetHandlerPlayServer to send keep-alive packets and in NetHandlerLoginServer
     * for a login-timeout
     */
    public void onNetworkTick() {}

    /**
     * Registers some server properties (gametype,hardcore-mode,terraintype,difficulty,player limit), creates a new
     * WorldClient and sets the player initial dimension
     */
    public void handleJoinGame(S01PacketJoinGame p_147282_1_)
    {
        this.gameController.playerController = new PlayerControllerMP(this.gameController, this);
        this.clientWorldController = new WorldClient(this, new WorldSettings(0L, p_147282_1_.func_149198_e(), false, p_147282_1_.func_149195_d(), p_147282_1_.func_149196_i()), NetworkDispatcher.get(getNetworkManager()).getOverrideDimension(p_147282_1_), p_147282_1_.func_149192_g(), this.gameController.mcProfiler);
        this.clientWorldController.isRemote = true;
        this.gameController.loadWorld(this.clientWorldController);
        this.gameController.thePlayer.dimension = p_147282_1_.func_149194_f();
        this.gameController.displayGuiScreen(new GuiDownloadTerrain(this));
        this.gameController.thePlayer.setEntityId(p_147282_1_.func_149197_c());
        this.currentServerMaxPlayers = p_147282_1_.func_149193_h();
        this.gameController.playerController.setGameType(p_147282_1_.func_149198_e());
        this.gameController.gameSettings.sendSettingsToServer();
        this.netManager.scheduleOutboundPacket(new C17PacketCustomPayload("MC|Brand", ClientBrandRetriever.getClientModName().getBytes(Charsets.UTF_8)), new GenericFutureListener[0]);
    }

    /**
     * Spawns an instance of the objecttype indicated by the packet and sets its position and momentum
     */
    public void handleSpawnObject(S0EPacketSpawnObject p_147235_1_)
    {
        double d0 = (double)p_147235_1_.func_148997_d() / 32.0D;
        double d1 = (double)p_147235_1_.func_148998_e() / 32.0D;
        double d2 = (double)p_147235_1_.func_148994_f() / 32.0D;
        Object object = null;

        if (p_147235_1_.func_148993_l() == 10)
        {
            object = EntityMinecart.createMinecart(this.clientWorldController, d0, d1, d2, p_147235_1_.func_149009_m());
        }
        else if (p_147235_1_.func_148993_l() == 90)
        {
            Entity entity = this.clientWorldController.getEntityByID(p_147235_1_.func_149009_m());

            if (entity instanceof EntityPlayer)
            {
                object = new EntityFishHook(this.clientWorldController, d0, d1, d2, (EntityPlayer)entity);
            }

            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 60)
        {
            object = new EntityArrow(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 61)
        {
            object = new EntitySnowball(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 71)
        {
            object = new EntityItemFrame(this.clientWorldController, (int)d0, (int)d1, (int)d2, p_147235_1_.func_149009_m());
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 77)
        {
            object = new EntityLeashKnot(this.clientWorldController, (int)d0, (int)d1, (int)d2);
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 65)
        {
            object = new EntityEnderPearl(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 72)
        {
            object = new EntityEnderEye(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 76)
        {
            object = new EntityFireworkRocket(this.clientWorldController, d0, d1, d2, (ItemStack)null);
        }
        else if (p_147235_1_.func_148993_l() == 63)
        {
            object = new EntityLargeFireball(this.clientWorldController, d0, d1, d2, (double)p_147235_1_.func_149010_g() / 8000.0D, (double)p_147235_1_.func_149004_h() / 8000.0D, (double)p_147235_1_.func_148999_i() / 8000.0D);
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 64)
        {
            object = new EntitySmallFireball(this.clientWorldController, d0, d1, d2, (double)p_147235_1_.func_149010_g() / 8000.0D, (double)p_147235_1_.func_149004_h() / 8000.0D, (double)p_147235_1_.func_148999_i() / 8000.0D);
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 66)
        {
            object = new EntityWitherSkull(this.clientWorldController, d0, d1, d2, (double)p_147235_1_.func_149010_g() / 8000.0D, (double)p_147235_1_.func_149004_h() / 8000.0D, (double)p_147235_1_.func_148999_i() / 8000.0D);
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 62)
        {
            object = new EntityEgg(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 73)
        {
            object = new EntityPotion(this.clientWorldController, d0, d1, d2, p_147235_1_.func_149009_m());
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 75)
        {
            object = new EntityExpBottle(this.clientWorldController, d0, d1, d2);
            p_147235_1_.func_149002_g(0);
        }
        else if (p_147235_1_.func_148993_l() == 1)
        {
            object = new EntityBoat(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 50)
        {
            object = new EntityTNTPrimed(this.clientWorldController, d0, d1, d2, (EntityLivingBase)null);
        }
        else if (p_147235_1_.func_148993_l() == 51)
        {
            object = new EntityEnderCrystal(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 2)
        {
            object = new EntityItem(this.clientWorldController, d0, d1, d2);
        }
        else if (p_147235_1_.func_148993_l() == 70)
        {
            object = new EntityFallingBlock(this.clientWorldController, d0, d1, d2, Block.getBlockById(p_147235_1_.func_149009_m() & 65535), p_147235_1_.func_149009_m() >> 16);
            p_147235_1_.func_149002_g(0);
        }

        if (object != null)
        {
            ((Entity)object).serverPosX = p_147235_1_.func_148997_d();
            ((Entity)object).serverPosY = p_147235_1_.func_148998_e();
            ((Entity)object).serverPosZ = p_147235_1_.func_148994_f();
            ((Entity)object).rotationPitch = (float)(p_147235_1_.func_149008_j() * 360) / 256.0F;
            ((Entity)object).rotationYaw = (float)(p_147235_1_.func_149006_k() * 360) / 256.0F;
            Entity[] aentity = ((Entity)object).getParts();

            if (aentity != null)
            {
                int i = p_147235_1_.func_149001_c() - ((Entity)object).getEntityId();

                for (int j = 0; j < aentity.length; ++j)
                {
                    aentity[j].setEntityId(aentity[j].getEntityId() + i);
                }
            }

            ((Entity)object).setEntityId(p_147235_1_.func_149001_c());
            this.clientWorldController.addEntityToWorld(p_147235_1_.func_149001_c(), (Entity)object);

            if (p_147235_1_.func_149009_m() > 0)
            {
                if (p_147235_1_.func_148993_l() == 60)
                {
                    Entity entity1 = this.clientWorldController.getEntityByID(p_147235_1_.func_149009_m());

                    if (entity1 instanceof EntityLivingBase)
                    {
                        EntityArrow entityarrow = (EntityArrow)object;
                        entityarrow.shootingEntity = entity1;
                    }
                }

                ((Entity)object).setVelocity((double)p_147235_1_.func_149010_g() / 8000.0D, (double)p_147235_1_.func_149004_h() / 8000.0D, (double)p_147235_1_.func_148999_i() / 8000.0D);
            }
        }
    }

    /**
     * Spawns an experience orb and sets its value (amount of XP)
     */
    public void handleSpawnExperienceOrb(S11PacketSpawnExperienceOrb p_147286_1_)
    {
        EntityXPOrb entityxporb = new EntityXPOrb(this.clientWorldController, (double)p_147286_1_.func_148984_d() / 32.0D, (double)p_147286_1_.func_148983_e() / 32.0D, (double)p_147286_1_.func_148982_f() / 32.0D, p_147286_1_.func_148986_g());
        // FORGE: BugFix MC-12013 Wrong XP orb clientside spawn position
        entityxporb.serverPosX = p_147286_1_.func_148984_d();
        entityxporb.serverPosY = p_147286_1_.func_148983_e();
        entityxporb.serverPosZ = p_147286_1_.func_148982_f();
        entityxporb.rotationYaw = 0.0F;
        entityxporb.rotationPitch = 0.0F;
        entityxporb.setEntityId(p_147286_1_.func_148985_c());
        this.clientWorldController.addEntityToWorld(p_147286_1_.func_148985_c(), entityxporb);
    }

    /**
     * Handles globally visible entities. Used in vanilla for lightning bolts
     */
    public void handleSpawnGlobalEntity(S2CPacketSpawnGlobalEntity p_147292_1_)
    {
        double d0 = (double)p_147292_1_.func_149051_d() / 32.0D;
        double d1 = (double)p_147292_1_.func_149050_e() / 32.0D;
        double d2 = (double)p_147292_1_.func_149049_f() / 32.0D;
        EntityLightningBolt entitylightningbolt = null;

        if (p_147292_1_.func_149053_g() == 1)
        {
            entitylightningbolt = new EntityLightningBolt(this.clientWorldController, d0, d1, d2);
        }

        if (entitylightningbolt != null)
        {
            entitylightningbolt.serverPosX = p_147292_1_.func_149051_d();
            entitylightningbolt.serverPosY = p_147292_1_.func_149050_e();
            entitylightningbolt.serverPosZ = p_147292_1_.func_149049_f();
            entitylightningbolt.rotationYaw = 0.0F;
            entitylightningbolt.rotationPitch = 0.0F;
            entitylightningbolt.setEntityId(p_147292_1_.func_149052_c());
            this.clientWorldController.addWeatherEffect(entitylightningbolt);
        }
    }

    /**
     * Handles the spawning of a painting object
     */
    public void handleSpawnPainting(S10PacketSpawnPainting p_147288_1_)
    {
        EntityPainting entitypainting = new EntityPainting(this.clientWorldController, p_147288_1_.func_148964_d(), p_147288_1_.func_148963_e(), p_147288_1_.func_148962_f(), p_147288_1_.func_148966_g(), p_147288_1_.func_148961_h());
        this.clientWorldController.addEntityToWorld(p_147288_1_.func_148965_c(), entitypainting);
    }

    /**
     * Sets the velocity of the specified entity to the specified value
     */
    public void handleEntityVelocity(S12PacketEntityVelocity p_147244_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147244_1_.func_149412_c());

        if (entity != null)
        {
            entity.setVelocity((double)p_147244_1_.func_149411_d() / 8000.0D, (double)p_147244_1_.func_149410_e() / 8000.0D, (double)p_147244_1_.func_149409_f() / 8000.0D);
        }
    }

    /**
     * Invoked when the server registers new proximate objects in your watchlist or when objects in your watchlist have
     * changed -> Registers any changes locally
     */
    public void handleEntityMetadata(S1CPacketEntityMetadata p_147284_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147284_1_.func_149375_d());

        if (entity != null && p_147284_1_.func_149376_c() != null)
        {
            entity.getDataWatcher().updateWatchedObjectsFromList(p_147284_1_.func_149376_c());
        }
    }

    /**
     * Handles the creation of a nearby player entity, sets the position and held item
     */
    public void handleSpawnPlayer(S0CPacketSpawnPlayer p_147237_1_)
    {
        double d0 = (double)p_147237_1_.func_148942_f() / 32.0D;
        double d1 = (double)p_147237_1_.func_148949_g() / 32.0D;
        double d2 = (double)p_147237_1_.func_148946_h() / 32.0D;
        float f = (float)(p_147237_1_.func_148941_i() * 360) / 256.0F;
        float f1 = (float)(p_147237_1_.func_148945_j() * 360) / 256.0F;
        GameProfile gameprofile = p_147237_1_.func_148948_e();
        EntityOtherPlayerMP entityotherplayermp = new EntityOtherPlayerMP(this.gameController.theWorld, p_147237_1_.func_148948_e());
        entityotherplayermp.prevPosX = entityotherplayermp.lastTickPosX = (double)(entityotherplayermp.serverPosX = p_147237_1_.func_148942_f());
        entityotherplayermp.prevPosY = entityotherplayermp.lastTickPosY = (double)(entityotherplayermp.serverPosY = p_147237_1_.func_148949_g());
        entityotherplayermp.prevPosZ = entityotherplayermp.lastTickPosZ = (double)(entityotherplayermp.serverPosZ = p_147237_1_.func_148946_h());
        int i = p_147237_1_.func_148947_k();

        if (i == 0)
        {
            entityotherplayermp.inventory.mainInventory[entityotherplayermp.inventory.currentItem] = null;
        }
        else
        {
            entityotherplayermp.inventory.mainInventory[entityotherplayermp.inventory.currentItem] = new ItemStack(Item.getItemById(i), 1, 0);
        }

        entityotherplayermp.setPositionAndRotation(d0, d1, d2, f, f1);
        this.clientWorldController.addEntityToWorld(p_147237_1_.func_148943_d(), entityotherplayermp);
        List list = p_147237_1_.func_148944_c();

        if (list != null)
        {
            entityotherplayermp.getDataWatcher().updateWatchedObjectsFromList(list);
        }
    }

    /**
     * Updates an entity's position and rotation as specified by the packet
     */
    public void handleEntityTeleport(S18PacketEntityTeleport p_147275_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147275_1_.func_149451_c());

        if (entity != null)
        {
            entity.serverPosX = p_147275_1_.func_149449_d();
            entity.serverPosY = p_147275_1_.func_149448_e();
            entity.serverPosZ = p_147275_1_.func_149446_f();
            double d0 = (double)entity.serverPosX / 32.0D;
            double d1 = (double)entity.serverPosY / 32.0D + 0.015625D;
            double d2 = (double)entity.serverPosZ / 32.0D;
            float f = (float)(p_147275_1_.func_149450_g() * 360) / 256.0F;
            float f1 = (float)(p_147275_1_.func_149447_h() * 360) / 256.0F;
            entity.setPositionAndRotation2(d0, d1, d2, f, f1, 3);
        }
    }

    /**
     * Updates which hotbar slot of the player is currently selected
     */
    public void handleHeldItemChange(S09PacketHeldItemChange p_147257_1_)
    {
        if (p_147257_1_.func_149385_c() >= 0 && p_147257_1_.func_149385_c() < InventoryPlayer.getHotbarSize())
        {
            this.gameController.thePlayer.inventory.currentItem = p_147257_1_.func_149385_c();
        }
    }

    /**
     * Updates the specified entity's position by the specified relative moment and absolute rotation. Note that
     * subclassing of the packet allows for the specification of a subset of this data (e.g. only rel. position, abs.
     * rotation or both).
     */
    public void handleEntityMovement(S14PacketEntity p_147259_1_)
    {
        Entity entity = p_147259_1_.func_149065_a(this.clientWorldController);

        if (entity != null)
        {
            entity.serverPosX += p_147259_1_.func_149062_c();
            entity.serverPosY += p_147259_1_.func_149061_d();
            entity.serverPosZ += p_147259_1_.func_149064_e();
            double d0 = (double)entity.serverPosX / 32.0D;
            double d1 = (double)entity.serverPosY / 32.0D;
            double d2 = (double)entity.serverPosZ / 32.0D;
            float f = p_147259_1_.func_149060_h() ? (float)(p_147259_1_.func_149066_f() * 360) / 256.0F : entity.rotationYaw;
            float f1 = p_147259_1_.func_149060_h() ? (float)(p_147259_1_.func_149063_g() * 360) / 256.0F : entity.rotationPitch;
            entity.setPositionAndRotation2(d0, d1, d2, f, f1, 3);
        }
    }

    /**
     * Updates the direction in which the specified entity is looking, normally this head rotation is independent of the
     * rotation of the entity itself
     */
    public void handleEntityHeadLook(S19PacketEntityHeadLook p_147267_1_)
    {
        Entity entity = p_147267_1_.func_149381_a(this.clientWorldController);

        if (entity != null)
        {
            float f = (float)(p_147267_1_.func_149380_c() * 360) / 256.0F;
            entity.setRotationYawHead(f);
        }
    }

    /**
     * Locally eliminates the entities. Invoked by the server when the items are in fact destroyed, or the player is no
     * longer registered as required to monitor them. The latter  happens when distance between the player and item
     * increases beyond a certain treshold (typically the viewing distance)
     */
    public void handleDestroyEntities(S13PacketDestroyEntities p_147238_1_)
    {
        for (int i = 0; i < p_147238_1_.func_149098_c().length; ++i)
        {
            this.clientWorldController.removeEntityFromWorld(p_147238_1_.func_149098_c()[i]);
        }
    }

    /**
     * Handles changes in player positioning and rotation such as when travelling to a new dimension, (re)spawning,
     * mounting horses etc. Seems to immediately reply to the server with the clients post-processing perspective on the
     * player positioning
     */
    public void handlePlayerPosLook(S08PacketPlayerPosLook p_147258_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;
        double d0 = p_147258_1_.func_148932_c();
        double d1 = p_147258_1_.func_148928_d();
        double d2 = p_147258_1_.func_148933_e();
        float f = p_147258_1_.func_148931_f();
        float f1 = p_147258_1_.func_148930_g();
        entityclientplayermp.ySize = 0.0F;
        entityclientplayermp.motionX = entityclientplayermp.motionY = entityclientplayermp.motionZ = 0.0D;
        entityclientplayermp.setPositionAndRotation(d0, d1, d2, f, f1);
        this.netManager.scheduleOutboundPacket(new C03PacketPlayer.C06PacketPlayerPosLook(entityclientplayermp.posX, entityclientplayermp.boundingBox.minY, entityclientplayermp.posY, entityclientplayermp.posZ, p_147258_1_.func_148931_f(), p_147258_1_.func_148930_g(), p_147258_1_.func_148929_h()), new GenericFutureListener[0]);

        if (!this.doneLoadingTerrain)
        {
            this.gameController.thePlayer.prevPosX = this.gameController.thePlayer.posX;
            this.gameController.thePlayer.prevPosY = this.gameController.thePlayer.posY;
            this.gameController.thePlayer.prevPosZ = this.gameController.thePlayer.posZ;
            this.doneLoadingTerrain = true;
            this.gameController.displayGuiScreen((GuiScreen)null);
        }
    }

    /**
     * Received from the servers PlayerManager if between 1 and 64 blocks in a chunk are changed. If only one block
     * requires an update, the server sends S23PacketBlockChange and if 64 or more blocks are changed, the server sends
     * S21PacketChunkData
     */
    public void handleMultiBlockChange(S22PacketMultiBlockChange p_147287_1_)
    {
        int i = p_147287_1_.func_148920_c().chunkXPos * 16;
        int j = p_147287_1_.func_148920_c().chunkZPos * 16;

        if (p_147287_1_.func_148921_d() != null)
        {
            DataInputStream datainputstream = new DataInputStream(new ByteArrayInputStream(p_147287_1_.func_148921_d()));

            try
            {
                for (int k = 0; k < p_147287_1_.func_148922_e(); ++k)
                {
                    short short1 = datainputstream.readShort();
                    short short2 = datainputstream.readShort();
                    int l = short2 >> 4 & 4095;
                    int i1 = short2 & 15;
                    int j1 = short1 >> 12 & 15;
                    int k1 = short1 >> 8 & 15;
                    int l1 = short1 & 255;
                    this.clientWorldController.func_147492_c(j1 + i, l1, k1 + j, Block.getBlockById(l), i1);
                }
            }
            catch (IOException ioexception)
            {
                ;
            }
        }
    }

    /**
     * Updates the specified chunk with the supplied data, marks it for re-rendering and lighting recalculation
     */
    public void handleChunkData(S21PacketChunkData p_147263_1_)
    {
        if (p_147263_1_.func_149274_i())
        {
            if (p_147263_1_.func_149276_g() == 0)
            {
                this.clientWorldController.doPreChunk(p_147263_1_.func_149273_e(), p_147263_1_.func_149271_f(), false);
                return;
            }

            this.clientWorldController.doPreChunk(p_147263_1_.func_149273_e(), p_147263_1_.func_149271_f(), true);
        }

        this.clientWorldController.invalidateBlockReceiveRegion(p_147263_1_.func_149273_e() << 4, 0, p_147263_1_.func_149271_f() << 4, (p_147263_1_.func_149273_e() << 4) + 15, 256, (p_147263_1_.func_149271_f() << 4) + 15);
        Chunk chunk = this.clientWorldController.getChunkFromChunkCoords(p_147263_1_.func_149273_e(), p_147263_1_.func_149271_f());
        chunk.fillChunk(p_147263_1_.func_149272_d(), p_147263_1_.func_149276_g(), p_147263_1_.func_149270_h(), p_147263_1_.func_149274_i());
        this.clientWorldController.markBlockRangeForRenderUpdate(p_147263_1_.func_149273_e() << 4, 0, p_147263_1_.func_149271_f() << 4, (p_147263_1_.func_149273_e() << 4) + 15, 256, (p_147263_1_.func_149271_f() << 4) + 15);

        if (!p_147263_1_.func_149274_i() || !(this.clientWorldController.provider instanceof WorldProviderSurface))
        {
            chunk.resetRelightChecks();
        }
    }

    /**
     * Updates the block and metadata and generates a blockupdate (and notify the clients)
     */
    public void handleBlockChange(S23PacketBlockChange p_147234_1_)
    {
        this.clientWorldController.func_147492_c(p_147234_1_.func_148879_d(), p_147234_1_.func_148878_e(), p_147234_1_.func_148877_f(), p_147234_1_.func_148880_c(), p_147234_1_.func_148881_g());
    }

    /**
     * Closes the network channel
     */
    public void handleDisconnect(S40PacketDisconnect p_147253_1_)
    {
        this.netManager.closeChannel(p_147253_1_.func_149165_c());
    }

    /**
     * Invoked when disconnecting, the parameter is a ChatComponent describing the reason for termination
     */
    public void onDisconnect(IChatComponent p_147231_1_)
    {
        this.gameController.loadWorld((WorldClient)null);

        if (this.guiScreenServer != null)
        {
            if (this.guiScreenServer instanceof GuiScreenRealmsProxy)
            {
                this.gameController.displayGuiScreen((new DisconnectedOnlineScreen(((GuiScreenRealmsProxy)this.guiScreenServer).func_154321_a(), "disconnect.lost", p_147231_1_)).getProxy());
            }
            else
            {
                this.gameController.displayGuiScreen(new GuiDisconnected(this.guiScreenServer, "disconnect.lost", p_147231_1_));
            }
        }
        else
        {
            this.gameController.displayGuiScreen(new GuiDisconnected(new GuiMultiplayer(new GuiMainMenu()), "disconnect.lost", p_147231_1_));
        }
    }

    public void addToSendQueue(Packet p_147297_1_)
    {
        this.netManager.scheduleOutboundPacket(p_147297_1_, new GenericFutureListener[0]);
    }

    public void handleCollectItem(S0DPacketCollectItem p_147246_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147246_1_.func_149354_c());
        Object object = (EntityLivingBase)this.clientWorldController.getEntityByID(p_147246_1_.func_149353_d());

        if (object == null)
        {
            object = this.gameController.thePlayer;
        }

        if (entity != null)
        {
            if (entity instanceof EntityXPOrb)
            {
                this.clientWorldController.playSoundAtEntity(entity, "random.orb", 0.2F, ((this.avRandomizer.nextFloat() - this.avRandomizer.nextFloat()) * 0.7F + 1.0F) * 2.0F);
            }
            else
            {
                this.clientWorldController.playSoundAtEntity(entity, "random.pop", 0.2F, ((this.avRandomizer.nextFloat() - this.avRandomizer.nextFloat()) * 0.7F + 1.0F) * 2.0F);
            }

            this.gameController.effectRenderer.addEffect(new EntityPickupFX(this.gameController.theWorld, entity, (Entity)object, -0.5F));
            this.clientWorldController.removeEntityFromWorld(p_147246_1_.func_149354_c());
        }
    }

    /**
     * Prints a chatmessage in the chat GUI
     */
    public void handleChat(S02PacketChat p_147251_1_)
    {
        ClientChatReceivedEvent event = new ClientChatReceivedEvent(p_147251_1_.func_148915_c());
        if (!MinecraftForge.EVENT_BUS.post(event) && event.message != null)
        {
            this.gameController.ingameGUI.getChatGUI().printChatMessage(event.message);
        }
    }

    /**
     * Renders a specified animation: Waking up a player, a living entity swinging its currently held item, being hurt
     * or receiving a critical hit by normal or magical means
     */
    public void handleAnimation(S0BPacketAnimation p_147279_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147279_1_.func_148978_c());

        if (entity != null)
        {
            if (p_147279_1_.func_148977_d() == 0)
            {
                EntityLivingBase entitylivingbase = (EntityLivingBase)entity;
                entitylivingbase.swingItem();
            }
            else if (p_147279_1_.func_148977_d() == 1)
            {
                entity.performHurtAnimation();
            }
            else if (p_147279_1_.func_148977_d() == 2)
            {
                EntityPlayer entityplayer = (EntityPlayer)entity;
                entityplayer.wakeUpPlayer(false, false, false);
            }
            else if (p_147279_1_.func_148977_d() == 4)
            {
                this.gameController.effectRenderer.addEffect(new EntityCrit2FX(this.gameController.theWorld, entity));
            }
            else if (p_147279_1_.func_148977_d() == 5)
            {
                EntityCrit2FX entitycrit2fx = new EntityCrit2FX(this.gameController.theWorld, entity, "magicCrit");
                this.gameController.effectRenderer.addEffect(entitycrit2fx);
            }
        }
    }

    /**
     * Retrieves the player identified by the packet, puts him to sleep if possible (and flags whether all players are
     * asleep)
     */
    public void handleUseBed(S0APacketUseBed p_147278_1_)
    {
        p_147278_1_.func_149091_a(this.clientWorldController).sleepInBedAt(p_147278_1_.func_149092_c(), p_147278_1_.func_149090_d(), p_147278_1_.func_149089_e());
    }

    /**
     * Spawns the mob entity at the specified location, with the specified rotation, momentum and type. Updates the
     * entities Datawatchers with the entity metadata specified in the packet
     */
    public void handleSpawnMob(S0FPacketSpawnMob p_147281_1_)
    {
        double d0 = (double)p_147281_1_.func_149023_f() / 32.0D;
        double d1 = (double)p_147281_1_.func_149034_g() / 32.0D;
        double d2 = (double)p_147281_1_.func_149029_h() / 32.0D;
        float f = (float)(p_147281_1_.func_149028_l() * 360) / 256.0F;
        float f1 = (float)(p_147281_1_.func_149030_m() * 360) / 256.0F;
        EntityLivingBase entitylivingbase = (EntityLivingBase)EntityList.createEntityByID(p_147281_1_.func_149025_e(), this.gameController.theWorld);
        if (entitylivingbase == null)
        {
            cpw.mods.fml.common.FMLLog.info("Server attempted to spawn an unknown entity using ID: {0} at ({1}, {2}, {3}) Skipping!", p_147281_1_.func_149025_e(), d0, d1, d2);
            return;
        }
        entitylivingbase.serverPosX = p_147281_1_.func_149023_f();
        entitylivingbase.serverPosY = p_147281_1_.func_149034_g();
        entitylivingbase.serverPosZ = p_147281_1_.func_149029_h();
        entitylivingbase.rotationYawHead = (float)(p_147281_1_.func_149032_n() * 360) / 256.0F;
        Entity[] aentity = entitylivingbase.getParts();

        if (aentity != null)
        {
            int i = p_147281_1_.func_149024_d() - entitylivingbase.getEntityId();

            for (int j = 0; j < aentity.length; ++j)
            {
                aentity[j].setEntityId(aentity[j].getEntityId() + i);
            }
        }

        entitylivingbase.setEntityId(p_147281_1_.func_149024_d());
        entitylivingbase.setPositionAndRotation(d0, d1, d2, f, f1);
        entitylivingbase.motionX = (double)((float)p_147281_1_.func_149026_i() / 8000.0F);
        entitylivingbase.motionY = (double)((float)p_147281_1_.func_149033_j() / 8000.0F);
        entitylivingbase.motionZ = (double)((float)p_147281_1_.func_149031_k() / 8000.0F);
        this.clientWorldController.addEntityToWorld(p_147281_1_.func_149024_d(), entitylivingbase);
        List list = p_147281_1_.func_149027_c();

        if (list != null)
        {
            entitylivingbase.getDataWatcher().updateWatchedObjectsFromList(list);
        }
    }

    public void handleTimeUpdate(S03PacketTimeUpdate p_147285_1_)
    {
        this.gameController.theWorld.func_82738_a(p_147285_1_.func_149366_c());
        this.gameController.theWorld.setWorldTime(p_147285_1_.func_149365_d());
    }

    public void handleSpawnPosition(S05PacketSpawnPosition p_147271_1_)
    {
        this.gameController.thePlayer.setSpawnChunk(new ChunkCoordinates(p_147271_1_.func_149360_c(), p_147271_1_.func_149359_d(), p_147271_1_.func_149358_e()), true);
        this.gameController.theWorld.getWorldInfo().setSpawnPosition(p_147271_1_.func_149360_c(), p_147271_1_.func_149359_d(), p_147271_1_.func_149358_e());
    }

    public void handleEntityAttach(S1BPacketEntityAttach p_147243_1_)
    {
        Object object = this.clientWorldController.getEntityByID(p_147243_1_.func_149403_d());
        Entity entity = this.clientWorldController.getEntityByID(p_147243_1_.func_149402_e());

        if (p_147243_1_.func_149404_c() == 0)
        {
            boolean flag = false;

            if (p_147243_1_.func_149403_d() == this.gameController.thePlayer.getEntityId())
            {
                object = this.gameController.thePlayer;

                if (entity instanceof EntityBoat)
                {
                    ((EntityBoat)entity).setIsBoatEmpty(false);
                }

                flag = ((Entity)object).ridingEntity == null && entity != null;
            }
            else if (entity instanceof EntityBoat)
            {
                ((EntityBoat)entity).setIsBoatEmpty(true);
            }

            if (object == null)
            {
                return;
            }

            ((Entity)object).mountEntity(entity);

            if (flag)
            {
                GameSettings gamesettings = this.gameController.gameSettings;
                this.gameController.ingameGUI.func_110326_a(I18n.format("mount.onboard", new Object[] {GameSettings.getKeyDisplayString(gamesettings.keyBindSneak.getKeyCode())}), false);
            }
        }
        else if (p_147243_1_.func_149404_c() == 1 && object != null && object instanceof EntityLiving)
        {
            if (entity != null)
            {
                ((EntityLiving)object).setLeashedToEntity(entity, false);
            }
            else
            {
                ((EntityLiving)object).clearLeashed(false, false);
            }
        }
    }

    /**
     * Invokes the entities' handleUpdateHealth method which is implemented in LivingBase (hurt/death),
     * MinecartMobSpawner (spawn delay), FireworkRocket & MinecartTNT (explosion), IronGolem (throwing,...), Witch
     * (spawn particles), Zombie (villager transformation), Animal (breeding mode particles), Horse (breeding/smoke
     * particles), Sheep (...), Tameable (...), Villager (particles for breeding mode, angry and happy), Wolf (...)
     */
    public void handleEntityStatus(S19PacketEntityStatus p_147236_1_)
    {
        Entity entity = p_147236_1_.func_149161_a(this.clientWorldController);

        if (entity != null)
        {
            entity.handleHealthUpdate(p_147236_1_.func_149160_c());
        }
    }

    /**
     * Recieves player health from the server and then proceeds to set it locally on the client.
     */
    public void handleUpdateHealth(S06PacketUpdateHealth p_147249_1_)
    {
        this.gameController.thePlayer.setPlayerSPHealth(p_147249_1_.func_149332_c());
        this.gameController.thePlayer.getFoodStats().setFoodLevel(p_147249_1_.func_149330_d());
        this.gameController.thePlayer.getFoodStats().setFoodSaturationLevel(p_147249_1_.func_149331_e());
    }

    public void handleSetExperience(S1FPacketSetExperience p_147295_1_)
    {
        this.gameController.thePlayer.setXPStats(p_147295_1_.func_149397_c(), p_147295_1_.func_149396_d(), p_147295_1_.func_149395_e());
    }

    /**
     * respawns the player
     */
    public void handleRespawn(S07PacketRespawn p_147280_1_)
    {
        if (p_147280_1_.func_149082_c() != this.gameController.thePlayer.dimension)
        {
            this.doneLoadingTerrain = false;
            Scoreboard scoreboard = this.clientWorldController.getScoreboard();
            this.clientWorldController = new WorldClient(this, new WorldSettings(0L, p_147280_1_.func_149083_e(), false, this.gameController.theWorld.getWorldInfo().isHardcoreModeEnabled(), p_147280_1_.func_149080_f()), p_147280_1_.func_149082_c(), p_147280_1_.func_149081_d(), this.gameController.mcProfiler);
            this.clientWorldController.setWorldScoreboard(scoreboard);
            this.clientWorldController.isRemote = true;
            this.gameController.loadWorld(this.clientWorldController);
            this.gameController.thePlayer.dimension = p_147280_1_.func_149082_c();
            this.gameController.displayGuiScreen(new GuiDownloadTerrain(this));
        }

        this.gameController.setDimensionAndSpawnPlayer(p_147280_1_.func_149082_c());
        this.gameController.playerController.setGameType(p_147280_1_.func_149083_e());
    }

    /**
     * Initiates a new explosion (sound, particles, drop spawn) for the affected blocks indicated by the packet.
     */
    public void handleExplosion(S27PacketExplosion p_147283_1_)
    {
        Explosion explosion = new Explosion(this.gameController.theWorld, (Entity)null, p_147283_1_.func_149148_f(), p_147283_1_.func_149143_g(), p_147283_1_.func_149145_h(), p_147283_1_.func_149146_i());
        explosion.affectedBlockPositions = p_147283_1_.func_149150_j();
        explosion.doExplosionB(true);
        this.gameController.thePlayer.motionX += (double)p_147283_1_.func_149149_c();
        this.gameController.thePlayer.motionY += (double)p_147283_1_.func_149144_d();
        this.gameController.thePlayer.motionZ += (double)p_147283_1_.func_149147_e();
    }

    /**
     * Displays a GUI by ID. In order starting from id 0: Chest, Workbench, Furnace, Dispenser, Enchanting table,
     * Brewing stand, Villager merchant, Beacon, Anvil, Hopper, Dropper, Horse
     */
    public void handleOpenWindow(S2DPacketOpenWindow p_147265_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;

        switch (p_147265_1_.func_148899_d())
        {
            case 0:
                entityclientplayermp.displayGUIChest(new InventoryBasic(p_147265_1_.func_148902_e(), p_147265_1_.func_148900_g(), p_147265_1_.func_148898_f()));
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 1:
                entityclientplayermp.displayGUIWorkbench(MathHelper.floor_double(entityclientplayermp.posX), MathHelper.floor_double(entityclientplayermp.posY), MathHelper.floor_double(entityclientplayermp.posZ));
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 2:
                TileEntityFurnace tileentityfurnace = new TileEntityFurnace();

                if (p_147265_1_.func_148900_g())
                {
                    tileentityfurnace.func_145951_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.func_146101_a(tileentityfurnace);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 3:
                TileEntityDispenser tileentitydispenser = new TileEntityDispenser();

                if (p_147265_1_.func_148900_g())
                {
                    tileentitydispenser.func_146018_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.func_146102_a(tileentitydispenser);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 4:
                entityclientplayermp.displayGUIEnchantment(MathHelper.floor_double(entityclientplayermp.posX), MathHelper.floor_double(entityclientplayermp.posY), MathHelper.floor_double(entityclientplayermp.posZ), p_147265_1_.func_148900_g() ? p_147265_1_.func_148902_e() : null);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 5:
                TileEntityBrewingStand tileentitybrewingstand = new TileEntityBrewingStand();

                if (p_147265_1_.func_148900_g())
                {
                    tileentitybrewingstand.func_145937_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.func_146098_a(tileentitybrewingstand);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 6:
                entityclientplayermp.displayGUIMerchant(new NpcMerchant(entityclientplayermp), p_147265_1_.func_148900_g() ? p_147265_1_.func_148902_e() : null);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 7:
                TileEntityBeacon tileentitybeacon = new TileEntityBeacon();
                entityclientplayermp.func_146104_a(tileentitybeacon);

                if (p_147265_1_.func_148900_g())
                {
                    tileentitybeacon.func_145999_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 8:
                entityclientplayermp.displayGUIAnvil(MathHelper.floor_double(entityclientplayermp.posX), MathHelper.floor_double(entityclientplayermp.posY), MathHelper.floor_double(entityclientplayermp.posZ));
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 9:
                TileEntityHopper tileentityhopper = new TileEntityHopper();

                if (p_147265_1_.func_148900_g())
                {
                    tileentityhopper.func_145886_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.func_146093_a(tileentityhopper);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 10:
                TileEntityDropper tileentitydropper = new TileEntityDropper();

                if (p_147265_1_.func_148900_g())
                {
                    tileentitydropper.func_146018_a(p_147265_1_.func_148902_e());
                }

                entityclientplayermp.func_146102_a(tileentitydropper);
                entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                break;
            case 11:
                Entity entity = this.clientWorldController.getEntityByID(p_147265_1_.func_148897_h());

                if (entity != null && entity instanceof EntityHorse)
                {
                    entityclientplayermp.displayGUIHorse((EntityHorse)entity, new AnimalChest(p_147265_1_.func_148902_e(), p_147265_1_.func_148900_g(), p_147265_1_.func_148898_f()));
                    entityclientplayermp.openContainer.windowId = p_147265_1_.func_148901_c();
                }
        }
    }

    /**
     * Handles pickin up an ItemStack or dropping one in your inventory or an open (non-creative) container
     */
    public void handleSetSlot(S2FPacketSetSlot p_147266_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;

        if (p_147266_1_.func_149175_c() == -1)
        {
            entityclientplayermp.inventory.setItemStack(p_147266_1_.func_149174_e());
        }
        else
        {
            boolean flag = false;

            if (this.gameController.currentScreen instanceof GuiContainerCreative)
            {
                GuiContainerCreative guicontainercreative = (GuiContainerCreative)this.gameController.currentScreen;
                flag = guicontainercreative.func_147056_g() != CreativeTabs.tabInventory.getTabIndex();
            }

            if (p_147266_1_.func_149175_c() == 0 && p_147266_1_.func_149173_d() >= 36 && p_147266_1_.func_149173_d() < 45)
            {
                ItemStack itemstack = entityclientplayermp.inventoryContainer.getSlot(p_147266_1_.func_149173_d()).getStack();

                if (p_147266_1_.func_149174_e() != null && (itemstack == null || itemstack.stackSize < p_147266_1_.func_149174_e().stackSize))
                {
                    p_147266_1_.func_149174_e().animationsToGo = 5;
                }

                entityclientplayermp.inventoryContainer.putStackInSlot(p_147266_1_.func_149173_d(), p_147266_1_.func_149174_e());
            }
            else if (p_147266_1_.func_149175_c() == entityclientplayermp.openContainer.windowId && (p_147266_1_.func_149175_c() != 0 || !flag))
            {
                entityclientplayermp.openContainer.putStackInSlot(p_147266_1_.func_149173_d(), p_147266_1_.func_149174_e());
            }
        }
    }

    /**
     * Verifies that the server and client are synchronized with respect to the inventory/container opened by the player
     * and confirms if it is the case.
     */
    public void handleConfirmTransaction(S32PacketConfirmTransaction p_147239_1_)
    {
        Container container = null;
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;

        if (p_147239_1_.func_148889_c() == 0)
        {
            container = entityclientplayermp.inventoryContainer;
        }
        else if (p_147239_1_.func_148889_c() == entityclientplayermp.openContainer.windowId)
        {
            container = entityclientplayermp.openContainer;
        }

        if (container != null && !p_147239_1_.func_148888_e())
        {
            this.addToSendQueue(new C0FPacketConfirmTransaction(p_147239_1_.func_148889_c(), p_147239_1_.func_148890_d(), true));
        }
    }

    /**
     * Handles the placement of a specified ItemStack in a specified container/inventory slot
     */
    public void handleWindowItems(S30PacketWindowItems p_147241_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;

        if (p_147241_1_.func_148911_c() == 0)
        {
            entityclientplayermp.inventoryContainer.putStacksInSlots(p_147241_1_.func_148910_d());
        }
        else if (p_147241_1_.func_148911_c() == entityclientplayermp.openContainer.windowId)
        {
            entityclientplayermp.openContainer.putStacksInSlots(p_147241_1_.func_148910_d());
        }
    }

    /**
     * Creates a sign in the specified location if it didn't exist and opens the GUI to edit its text
     */
    public void handleSignEditorOpen(S36PacketSignEditorOpen p_147268_1_)
    {
        Object object = this.clientWorldController.getTileEntity(p_147268_1_.func_149129_c(), p_147268_1_.func_149128_d(), p_147268_1_.func_149127_e());

        if (object == null)
        {
            object = new TileEntitySign();
            ((TileEntity)object).setWorldObj(this.clientWorldController);
            ((TileEntity)object).xCoord = p_147268_1_.func_149129_c();
            ((TileEntity)object).yCoord = p_147268_1_.func_149128_d();
            ((TileEntity)object).zCoord = p_147268_1_.func_149127_e();
        }

        this.gameController.thePlayer.func_146100_a((TileEntity)object);
    }

    /**
     * Updates a specified sign with the specified text lines
     */
    public void handleUpdateSign(S33PacketUpdateSign p_147248_1_)
    {
        boolean flag = false;

        if (this.gameController.theWorld.blockExists(p_147248_1_.func_149346_c(), p_147248_1_.func_149345_d(), p_147248_1_.func_149344_e()))
        {
            TileEntity tileentity = this.gameController.theWorld.getTileEntity(p_147248_1_.func_149346_c(), p_147248_1_.func_149345_d(), p_147248_1_.func_149344_e());

            if (tileentity instanceof TileEntitySign)
            {
                TileEntitySign tileentitysign = (TileEntitySign)tileentity;

                if (tileentitysign.func_145914_a())
                {
                    for (int i = 0; i < 4; ++i)
                    {
                        tileentitysign.signText[i] = p_147248_1_.func_149347_f()[i];
                    }

                    tileentitysign.markDirty();
                }

                flag = true;
            }
        }

        if (!flag && this.gameController.thePlayer != null)
        {
            this.gameController.thePlayer.addChatMessage(new ChatComponentText("Unable to locate sign at " + p_147248_1_.func_149346_c() + ", " + p_147248_1_.func_149345_d() + ", " + p_147248_1_.func_149344_e()));
        }
    }

    /**
     * Updates the NBTTagCompound metadata of instances of the following entitytypes: Mob spawners, command blocks,
     * beacons, skulls, flowerpot
     */
    public void handleUpdateTileEntity(S35PacketUpdateTileEntity p_147273_1_)
    {
        if (this.gameController.theWorld.blockExists(p_147273_1_.func_148856_c(), p_147273_1_.func_148855_d(), p_147273_1_.func_148854_e()))
        {
            TileEntity tileentity = this.gameController.theWorld.getTileEntity(p_147273_1_.func_148856_c(), p_147273_1_.func_148855_d(), p_147273_1_.func_148854_e());

            if (tileentity != null)
            {
                if (p_147273_1_.func_148853_f() == 1 && tileentity instanceof TileEntityMobSpawner)
                {
                    tileentity.readFromNBT(p_147273_1_.func_148857_g());
                }
                else if (p_147273_1_.func_148853_f() == 2 && tileentity instanceof TileEntityCommandBlock)
                {
                    tileentity.readFromNBT(p_147273_1_.func_148857_g());
                }
                else if (p_147273_1_.func_148853_f() == 3 && tileentity instanceof TileEntityBeacon)
                {
                    tileentity.readFromNBT(p_147273_1_.func_148857_g());
                }
                else if (p_147273_1_.func_148853_f() == 4 && tileentity instanceof TileEntitySkull)
                {
                    tileentity.readFromNBT(p_147273_1_.func_148857_g());
                }
                else if (p_147273_1_.func_148853_f() == 5 && tileentity instanceof TileEntityFlowerPot)
                {
                    tileentity.readFromNBT(p_147273_1_.func_148857_g());
                }
                else
                {
                    tileentity.onDataPacket(netManager, p_147273_1_);
                }
            }
        }
    }

    /**
     * Sets the progressbar of the opened window to the specified value
     */
    public void handleWindowProperty(S31PacketWindowProperty p_147245_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;

        if (entityclientplayermp.openContainer != null && entityclientplayermp.openContainer.windowId == p_147245_1_.func_149182_c())
        {
            entityclientplayermp.openContainer.updateProgressBar(p_147245_1_.func_149181_d(), p_147245_1_.func_149180_e());
        }
    }

    public void handleEntityEquipment(S04PacketEntityEquipment p_147242_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147242_1_.func_149389_d());

        if (entity != null)
        {
            entity.setCurrentItemOrArmor(p_147242_1_.func_149388_e(), p_147242_1_.func_149390_c());
        }
    }

    /**
     * Resets the ItemStack held in hand and closes the window that is opened
     */
    public void handleCloseWindow(S2EPacketCloseWindow p_147276_1_)
    {
        this.gameController.thePlayer.closeScreenNoPacket();
    }

    /**
     * Triggers Block.onBlockEventReceived, which is implemented in BlockPistonBase for extension/retraction, BlockNote
     * for setting the instrument (including audiovisual feedback) and in BlockContainer to set the number of players
     * accessing a (Ender)Chest
     */
    public void handleBlockAction(S24PacketBlockAction p_147261_1_)
    {
        this.gameController.theWorld.addBlockEvent(p_147261_1_.func_148867_d(), p_147261_1_.func_148866_e(), p_147261_1_.func_148865_f(), p_147261_1_.func_148868_c(), p_147261_1_.func_148869_g(), p_147261_1_.func_148864_h());
    }

    /**
     * Updates all registered IWorldAccess instances with destroyBlockInWorldPartially
     */
    public void handleBlockBreakAnim(S25PacketBlockBreakAnim p_147294_1_)
    {
        this.gameController.theWorld.destroyBlockInWorldPartially(p_147294_1_.func_148845_c(), p_147294_1_.func_148844_d(), p_147294_1_.func_148843_e(), p_147294_1_.func_148842_f(), p_147294_1_.func_148846_g());
    }

    public void handleMapChunkBulk(S26PacketMapChunkBulk p_147269_1_)
    {
        for (int i = 0; i < p_147269_1_.func_149254_d(); ++i)
        {
            int j = p_147269_1_.func_149255_a(i);
            int k = p_147269_1_.func_149253_b(i);
            this.clientWorldController.doPreChunk(j, k, true);
            this.clientWorldController.invalidateBlockReceiveRegion(j << 4, 0, k << 4, (j << 4) + 15, 256, (k << 4) + 15);
            Chunk chunk = this.clientWorldController.getChunkFromChunkCoords(j, k);
            chunk.fillChunk(p_147269_1_.func_149256_c(i), p_147269_1_.func_149252_e()[i], p_147269_1_.func_149257_f()[i], true);
            this.clientWorldController.markBlockRangeForRenderUpdate(j << 4, 0, k << 4, (j << 4) + 15, 256, (k << 4) + 15);

            if (!(this.clientWorldController.provider instanceof WorldProviderSurface))
            {
                chunk.resetRelightChecks();
            }
        }
    }

    public void handleChangeGameState(S2BPacketChangeGameState p_147252_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;
        int i = p_147252_1_.func_149138_c();
        float f = p_147252_1_.func_149137_d();
        int j = MathHelper.floor_float(f + 0.5F);

        if (i >= 0 && i < S2BPacketChangeGameState.field_149142_a.length && S2BPacketChangeGameState.field_149142_a[i] != null)
        {
            entityclientplayermp.addChatComponentMessage(new ChatComponentTranslation(S2BPacketChangeGameState.field_149142_a[i], new Object[0]));
        }

        if (i == 1)
        {
            this.clientWorldController.getWorldInfo().setRaining(true);
            this.clientWorldController.setRainStrength(0.0F);
        }
        else if (i == 2)
        {
            this.clientWorldController.getWorldInfo().setRaining(false);
            this.clientWorldController.setRainStrength(1.0F);
        }
        else if (i == 3)
        {
            this.gameController.playerController.setGameType(WorldSettings.GameType.getByID(j));
        }
        else if (i == 4)
        {
            this.gameController.displayGuiScreen(new GuiWinGame());
        }
        else if (i == 5)
        {
            GameSettings gamesettings = this.gameController.gameSettings;

            if (f == 0.0F)
            {
                this.gameController.displayGuiScreen(new GuiScreenDemo());
            }
            else if (f == 101.0F)
            {
                this.gameController.ingameGUI.getChatGUI().printChatMessage(new ChatComponentTranslation("demo.help.movement", new Object[] {GameSettings.getKeyDisplayString(gamesettings.keyBindForward.getKeyCode()), GameSettings.getKeyDisplayString(gamesettings.keyBindLeft.getKeyCode()), GameSettings.getKeyDisplayString(gamesettings.keyBindBack.getKeyCode()), GameSettings.getKeyDisplayString(gamesettings.keyBindRight.getKeyCode())}));
            }
            else if (f == 102.0F)
            {
                this.gameController.ingameGUI.getChatGUI().printChatMessage(new ChatComponentTranslation("demo.help.jump", new Object[] {GameSettings.getKeyDisplayString(gamesettings.keyBindJump.getKeyCode())}));
            }
            else if (f == 103.0F)
            {
                this.gameController.ingameGUI.getChatGUI().printChatMessage(new ChatComponentTranslation("demo.help.inventory", new Object[] {GameSettings.getKeyDisplayString(gamesettings.keyBindInventory.getKeyCode())}));
            }
        }
        else if (i == 6)
        {
            this.clientWorldController.playSound(entityclientplayermp.posX, entityclientplayermp.posY + (double)entityclientplayermp.getEyeHeight(), entityclientplayermp.posZ, "random.successful_hit", 0.18F, 0.45F, false);
        }
        else if (i == 7)
        {
            this.clientWorldController.setRainStrength(f);
        }
        else if (i == 8)
        {
            this.clientWorldController.setThunderStrength(f);
        }
    }

    /**
     * Updates the worlds MapStorage with the specified MapData for the specified map-identifier and invokes a
     * MapItemRenderer for it
     */
    public void handleMaps(S34PacketMaps p_147264_1_)
    {
        MapData mapdata = ItemMap.func_150912_a(p_147264_1_.func_149188_c(), this.gameController.theWorld);
        mapdata.updateMPMapData(p_147264_1_.func_149187_d());
        this.gameController.entityRenderer.getMapItemRenderer().func_148246_a(mapdata);
    }

    public void handleEffect(S28PacketEffect p_147277_1_)
    {
        if (p_147277_1_.func_149244_c())
        {
            this.gameController.theWorld.playBroadcastSound(p_147277_1_.func_149242_d(), p_147277_1_.func_149240_f(), p_147277_1_.func_149243_g(), p_147277_1_.func_149239_h(), p_147277_1_.func_149241_e());
        }
        else
        {
            this.gameController.theWorld.playAuxSFX(p_147277_1_.func_149242_d(), p_147277_1_.func_149240_f(), p_147277_1_.func_149243_g(), p_147277_1_.func_149239_h(), p_147277_1_.func_149241_e());
        }
    }

    /**
     * Updates the players statistics or achievements
     */
    public void handleStatistics(S37PacketStatistics p_147293_1_)
    {
        boolean flag = false;
        StatBase statbase;
        int i;

        for (Iterator iterator = p_147293_1_.func_148974_c().entrySet().iterator(); iterator.hasNext(); this.gameController.thePlayer.getStatFileWriter().func_150873_a(this.gameController.thePlayer, statbase, i))
        {
            Entry entry = (Entry)iterator.next();
            statbase = (StatBase)entry.getKey();
            i = ((Integer)entry.getValue()).intValue();

            if (statbase.isAchievement() && i > 0)
            {
                if (this.field_147308_k && this.gameController.thePlayer.getStatFileWriter().writeStat(statbase) == 0)
                {
                    Achievement achievement = (Achievement)statbase;
                    this.gameController.guiAchievement.func_146256_a(achievement);
                    this.gameController.func_152346_Z().func_152911_a(new MetadataAchievement(achievement), 0L);

                    if (statbase == AchievementList.openInventory)
                    {
                        this.gameController.gameSettings.showInventoryAchievementHint = false;
                        this.gameController.gameSettings.saveOptions();
                    }
                }

                flag = true;
            }
        }

        if (!this.field_147308_k && !flag && this.gameController.gameSettings.showInventoryAchievementHint)
        {
            this.gameController.guiAchievement.func_146255_b(AchievementList.openInventory);
        }

        this.field_147308_k = true;

        if (this.gameController.currentScreen instanceof IProgressMeter)
        {
            ((IProgressMeter)this.gameController.currentScreen).func_146509_g();
        }
    }

    public void handleEntityEffect(S1DPacketEntityEffect p_147260_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147260_1_.func_149426_d());

        if (entity instanceof EntityLivingBase)
        {
            PotionEffect potioneffect = new PotionEffect(p_147260_1_.func_149427_e(), p_147260_1_.func_149425_g(), p_147260_1_.func_149428_f());
            potioneffect.setPotionDurationMax(p_147260_1_.func_149429_c());
            ((EntityLivingBase)entity).addPotionEffect(potioneffect);
        }
    }

    /**
     * Handle a remove entity effect packet.
     */
    public void handleRemoveEntityEffect(S1EPacketRemoveEntityEffect p_147262_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147262_1_.func_149076_c());

        if (entity instanceof EntityLivingBase)
        {
            ((EntityLivingBase)entity).removePotionEffectClient(p_147262_1_.func_149075_d());
        }
    }

    public void handlePlayerListItem(S38PacketPlayerListItem p_147256_1_)
    {
        GuiPlayerInfo guiplayerinfo = (GuiPlayerInfo)this.playerInfoMap.get(p_147256_1_.func_149122_c());

        if (guiplayerinfo == null && p_147256_1_.func_149121_d())
        {
            guiplayerinfo = new GuiPlayerInfo(p_147256_1_.func_149122_c());
            this.playerInfoMap.put(p_147256_1_.func_149122_c(), guiplayerinfo);
            this.playerInfoList.add(guiplayerinfo);
        }

        if (guiplayerinfo != null && !p_147256_1_.func_149121_d())
        {
            this.playerInfoMap.remove(p_147256_1_.func_149122_c());
            this.playerInfoList.remove(guiplayerinfo);
        }

        if (guiplayerinfo != null && p_147256_1_.func_149121_d())
        {
            guiplayerinfo.responseTime = p_147256_1_.func_149120_e();
        }
    }

    public void handleKeepAlive(S00PacketKeepAlive p_147272_1_)
    {
        this.addToSendQueue(new C00PacketKeepAlive(p_147272_1_.func_149134_c()));
    }

    /**
     * Allows validation of the connection state transition. Parameters: from, to (connection state). Typically throws
     * IllegalStateException or UnsupportedOperationException if validation fails
     */
    public void onConnectionStateTransition(EnumConnectionState p_147232_1_, EnumConnectionState p_147232_2_)
    {
        throw new IllegalStateException("Unexpected protocol change!");
    }

    public void handlePlayerAbilities(S39PacketPlayerAbilities p_147270_1_)
    {
        EntityClientPlayerMP entityclientplayermp = this.gameController.thePlayer;
        entityclientplayermp.capabilities.isFlying = p_147270_1_.func_149106_d();
        entityclientplayermp.capabilities.isCreativeMode = p_147270_1_.func_149103_f();
        entityclientplayermp.capabilities.disableDamage = p_147270_1_.func_149112_c();
        entityclientplayermp.capabilities.allowFlying = p_147270_1_.func_149105_e();
        entityclientplayermp.capabilities.setFlySpeed(p_147270_1_.func_149101_g());
        entityclientplayermp.capabilities.setPlayerWalkSpeed(p_147270_1_.func_149107_h());
    }

    /**
     * Displays the available command-completion options the server knows of
     */
    public void handleTabComplete(S3APacketTabComplete p_147274_1_)
    {
        String[] astring = p_147274_1_.func_149630_c();

        if (this.gameController.currentScreen instanceof GuiChat)
        {
            GuiChat guichat = (GuiChat)this.gameController.currentScreen;
            guichat.func_146406_a(astring);
        }
    }

    public void handleSoundEffect(S29PacketSoundEffect p_147255_1_)
    {
        this.gameController.theWorld.playSound(p_147255_1_.func_149207_d(), p_147255_1_.func_149211_e(), p_147255_1_.func_149210_f(), p_147255_1_.func_149212_c(), p_147255_1_.func_149208_g(), p_147255_1_.func_149209_h(), false);
    }

    /**
     * Handles packets that have room for a channel specification. Vanilla implemented channels are "MC|TrList" to
     * acquire a MerchantRecipeList trades for a villager merchant, "MC|Brand" which sets the server brand? on the
     * player instance and finally "MC|RPack" which the server uses to communicate the identifier of the default server
     * resourcepack for the client to load.
     */
    public void handleCustomPayload(S3FPacketCustomPayload p_147240_1_)
    {
        if ("MC|TrList".equals(p_147240_1_.func_149169_c()))
        {
            ByteBuf bytebuf = Unpooled.wrappedBuffer(p_147240_1_.func_149168_d());

            try
            {
                int i = bytebuf.readInt();
                GuiScreen guiscreen = this.gameController.currentScreen;

                if (guiscreen != null && guiscreen instanceof GuiMerchant && i == this.gameController.thePlayer.openContainer.windowId)
                {
                    IMerchant imerchant = ((GuiMerchant)guiscreen).func_147035_g();
                    MerchantRecipeList merchantrecipelist = MerchantRecipeList.func_151390_b(new PacketBuffer(bytebuf));
                    imerchant.setRecipes(merchantrecipelist);
                }
            }
            catch (IOException ioexception)
            {
                logger.error("Couldn\'t load trade info", ioexception);
            }
            finally
            {
                bytebuf.release();
            }
        }
        else if ("MC|Brand".equals(p_147240_1_.func_149169_c()))
        {
            this.gameController.thePlayer.func_142020_c(new String(p_147240_1_.func_149168_d(), Charsets.UTF_8));
        }
        else if ("MC|RPack".equals(p_147240_1_.func_149169_c()))
        {
            final String s = new String(p_147240_1_.func_149168_d(), Charsets.UTF_8);

            if (this.gameController.func_147104_D() != null && this.gameController.func_147104_D().func_152586_b() == ServerData.ServerResourceMode.ENABLED)
            {
                this.gameController.getResourcePackRepository().func_148526_a(s);
            }
            else if (this.gameController.func_147104_D() == null || this.gameController.func_147104_D().func_152586_b() == ServerData.ServerResourceMode.PROMPT)
            {
                this.gameController.displayGuiScreen(new GuiYesNo(new GuiYesNoCallback()
                {
                    private static final String __OBFID = "CL_00000879";
                    public void confirmClicked(boolean p_73878_1_, int p_73878_2_)
                    {
                        NetHandlerPlayClient.this.gameController = Minecraft.getMinecraft();

                        if (NetHandlerPlayClient.this.gameController.func_147104_D() != null)
                        {
                            NetHandlerPlayClient.this.gameController.func_147104_D().func_152584_a(ServerData.ServerResourceMode.ENABLED);
                            ServerList.func_147414_b(NetHandlerPlayClient.this.gameController.func_147104_D());
                        }

                        if (p_73878_1_)
                        {
                            NetHandlerPlayClient.this.gameController.getResourcePackRepository().func_148526_a(s);
                        }

                        NetHandlerPlayClient.this.gameController.displayGuiScreen((GuiScreen)null);
                    }
                }, I18n.format("multiplayer.texturePrompt.line1", new Object[0]), I18n.format("multiplayer.texturePrompt.line2", new Object[0]), 0));
            }
        }
    }

    /**
     * May create a scoreboard objective, remove an objective from the scoreboard or update an objectives' displayname
     */
    public void handleScoreboardObjective(S3BPacketScoreboardObjective p_147291_1_)
    {
        Scoreboard scoreboard = this.clientWorldController.getScoreboard();
        ScoreObjective scoreobjective;

        if (p_147291_1_.func_149338_e() == 0)
        {
            scoreobjective = scoreboard.addScoreObjective(p_147291_1_.func_149339_c(), IScoreObjectiveCriteria.field_96641_b);
            scoreobjective.setDisplayName(p_147291_1_.func_149337_d());
        }
        else
        {
            scoreobjective = scoreboard.getObjective(p_147291_1_.func_149339_c());

            if (p_147291_1_.func_149338_e() == 1)
            {
                scoreboard.func_96519_k(scoreobjective);
            }
            else if (p_147291_1_.func_149338_e() == 2)
            {
                scoreobjective.setDisplayName(p_147291_1_.func_149337_d());
            }
        }
    }

    /**
     * Either updates the score with a specified value or removes the score for an objective
     */
    public void handleUpdateScore(S3CPacketUpdateScore p_147250_1_)
    {
        Scoreboard scoreboard = this.clientWorldController.getScoreboard();
        ScoreObjective scoreobjective = scoreboard.getObjective(p_147250_1_.func_149321_d());

        if (p_147250_1_.func_149322_f() == 0)
        {
            Score score = scoreboard.func_96529_a(p_147250_1_.func_149324_c(), scoreobjective);
            score.setScorePoints(p_147250_1_.func_149323_e());
        }
        else if (p_147250_1_.func_149322_f() == 1)
        {
            scoreboard.func_96515_c(p_147250_1_.func_149324_c());
        }
    }

    /**
     * Removes or sets the ScoreObjective to be displayed at a particular scoreboard position (list, sidebar, below
     * name)
     */
    public void handleDisplayScoreboard(S3DPacketDisplayScoreboard p_147254_1_)
    {
        Scoreboard scoreboard = this.clientWorldController.getScoreboard();

        if (p_147254_1_.func_149370_d().length() == 0)
        {
            scoreboard.func_96530_a(p_147254_1_.func_149371_c(), (ScoreObjective)null);
        }
        else
        {
            ScoreObjective scoreobjective = scoreboard.getObjective(p_147254_1_.func_149370_d());
            scoreboard.func_96530_a(p_147254_1_.func_149371_c(), scoreobjective);
        }
    }

    /**
     * Updates a team managed by the scoreboard: Create/Remove the team registration, Register/Remove the player-team-
     * memberships, Set team displayname/prefix/suffix and/or whether friendly fire is enabled
     */
    public void handleTeams(S3EPacketTeams p_147247_1_)
    {
        Scoreboard scoreboard = this.clientWorldController.getScoreboard();
        ScorePlayerTeam scoreplayerteam;

        if (p_147247_1_.func_149307_h() == 0)
        {
            scoreplayerteam = scoreboard.createTeam(p_147247_1_.func_149312_c());
        }
        else
        {
            scoreplayerteam = scoreboard.getTeam(p_147247_1_.func_149312_c());
        }

        if (p_147247_1_.func_149307_h() == 0 || p_147247_1_.func_149307_h() == 2)
        {
            scoreplayerteam.setTeamName(p_147247_1_.func_149306_d());
            scoreplayerteam.setNamePrefix(p_147247_1_.func_149311_e());
            scoreplayerteam.setNameSuffix(p_147247_1_.func_149309_f());
            scoreplayerteam.func_98298_a(p_147247_1_.func_149308_i());
        }

        Iterator iterator;
        String s;

        if (p_147247_1_.func_149307_h() == 0 || p_147247_1_.func_149307_h() == 3)
        {
            iterator = p_147247_1_.func_149310_g().iterator();

            while (iterator.hasNext())
            {
                s = (String)iterator.next();
                scoreboard.func_151392_a(s, p_147247_1_.func_149312_c());
            }
        }

        if (p_147247_1_.func_149307_h() == 4)
        {
            iterator = p_147247_1_.func_149310_g().iterator();

            while (iterator.hasNext())
            {
                s = (String)iterator.next();
                scoreboard.removePlayerFromTeam(s, scoreplayerteam);
            }
        }

        if (p_147247_1_.func_149307_h() == 1)
        {
            scoreboard.removeTeam(scoreplayerteam);
        }
    }

    /**
     * Spawns a specified number of particles at the specified location with a randomized displacement according to
     * specified bounds
     */
    public void handleParticles(S2APacketParticles p_147289_1_)
    {
        if (p_147289_1_.func_149222_k() == 0)
        {
            double d0 = (double)(p_147289_1_.func_149227_j() * p_147289_1_.func_149221_g());
            double d2 = (double)(p_147289_1_.func_149227_j() * p_147289_1_.func_149224_h());
            double d4 = (double)(p_147289_1_.func_149227_j() * p_147289_1_.func_149223_i());
            this.clientWorldController.spawnParticle(p_147289_1_.func_149228_c(), p_147289_1_.func_149220_d(), p_147289_1_.func_149226_e(), p_147289_1_.func_149225_f(), d0, d2, d4);
        }
        else
        {
            for (int i = 0; i < p_147289_1_.func_149222_k(); ++i)
            {
                double d1 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149221_g();
                double d3 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149224_h();
                double d5 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149223_i();
                double d6 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149227_j();
                double d7 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149227_j();
                double d8 = this.avRandomizer.nextGaussian() * (double)p_147289_1_.func_149227_j();
                this.clientWorldController.spawnParticle(p_147289_1_.func_149228_c(), p_147289_1_.func_149220_d() + d1, p_147289_1_.func_149226_e() + d3, p_147289_1_.func_149225_f() + d5, d6, d7, d8);
            }
        }
    }

    /**
     * Updates en entity's attributes and their respective modifiers, which are used for speed bonusses (player
     * sprinting, animals fleeing, baby speed), weapon/tool attackDamage, hostiles followRange randomization, zombie
     * maxHealth and knockback resistance as well as reinforcement spawning chance.
     */
    public void handleEntityProperties(S20PacketEntityProperties p_147290_1_)
    {
        Entity entity = this.clientWorldController.getEntityByID(p_147290_1_.func_149442_c());

        if (entity != null)
        {
            if (!(entity instanceof EntityLivingBase))
            {
                throw new IllegalStateException("Server tried to update attributes of a non-living entity (actually: " + entity + ")");
            }
            else
            {
                BaseAttributeMap baseattributemap = ((EntityLivingBase)entity).getAttributeMap();
                Iterator iterator = p_147290_1_.func_149441_d().iterator();

                while (iterator.hasNext())
                {
                    S20PacketEntityProperties.Snapshot snapshot = (S20PacketEntityProperties.Snapshot)iterator.next();
                    IAttributeInstance iattributeinstance = baseattributemap.getAttributeInstanceByName(snapshot.func_151409_a());

                    if (iattributeinstance == null)
                    {
                        iattributeinstance = baseattributemap.registerAttribute(new RangedAttribute(snapshot.func_151409_a(), 0.0D, 2.2250738585072014E-308D, Double.MAX_VALUE));
                    }

                    iattributeinstance.setBaseValue(snapshot.func_151410_b());
                    iattributeinstance.removeAllModifiers();
                    Iterator iterator1 = snapshot.func_151408_c().iterator();

                    while (iterator1.hasNext())
                    {
                        AttributeModifier attributemodifier = (AttributeModifier)iterator1.next();
                        iattributeinstance.applyModifier(attributemodifier);
                    }
                }
            }
        }
    }

    /**
     * Returns this the NetworkManager instance registered with this NetworkHandlerPlayClient
     */
    public NetworkManager getNetworkManager()
    {
        return this.netManager;
    }
}