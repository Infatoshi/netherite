package net.minecraft.server.management;

import com.google.common.base.Charsets;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.mojang.authlib.GameProfile;

import cpw.mods.fml.common.FMLCommonHandler;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.net.SocketAddress;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.Map.Entry;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.S01PacketJoinGame;
import net.minecraft.network.play.server.S02PacketChat;
import net.minecraft.network.play.server.S03PacketTimeUpdate;
import net.minecraft.network.play.server.S05PacketSpawnPosition;
import net.minecraft.network.play.server.S07PacketRespawn;
import net.minecraft.network.play.server.S09PacketHeldItemChange;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S1FPacketSetExperience;
import net.minecraft.network.play.server.S2BPacketChangeGameState;
import net.minecraft.network.play.server.S38PacketPlayerListItem;
import net.minecraft.network.play.server.S39PacketPlayerAbilities;
import net.minecraft.network.play.server.S3EPacketTeams;
import net.minecraft.network.play.server.S3FPacketCustomPayload;
import net.minecraft.potion.PotionEffect;
import net.minecraft.scoreboard.Score;
import net.minecraft.scoreboard.ScoreObjective;
import net.minecraft.scoreboard.ScorePlayerTeam;
import net.minecraft.scoreboard.Scoreboard;
import net.minecraft.scoreboard.ServerScoreboard;
import net.minecraft.scoreboard.Team;
import net.minecraft.server.MinecraftServer;
import net.minecraft.stats.StatList;
import net.minecraft.stats.StatisticsFile;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.MathHelper;
import net.minecraft.world.Teleporter;
import net.minecraft.world.World;
import net.minecraft.world.WorldProvider;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.demo.DemoWorldManager;
import net.minecraft.world.storage.IPlayerFileData;
import net.minecraft.world.storage.SaveHandler;
import net.minecraftforge.common.chunkio.ChunkIOExecutor;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

public abstract class ServerConfigurationManager
{
    public static final File field_152613_a = new File("banned-players.json");
    public static final File field_152614_b = new File("banned-ips.json");
    public static final File field_152615_c = new File("ops.json");
    public static final File field_152616_d = new File("whitelist.json");
    private static final Logger logger = LogManager.getLogger();
    private static final SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd \'at\' HH:mm:ss z");
    /** Reference to the MinecraftServer object. */
    private final MinecraftServer mcServer;
    /** A list of player entities that exist on this server. */
    public final List playerEntityList = new ArrayList();
    private final UserListBans bannedPlayers;
    private final BanList bannedIPs;
    /** A set containing the OPs. */
    private final UserListOps ops;
    /** The Set of all whitelisted players. */
    private final UserListWhitelist whiteListedPlayers;
    private final Map field_148547_k;
    /** Reference to the PlayerNBTManager object. */
    private IPlayerFileData playerNBTManagerObj;
    /** Server setting to only allow OPs and whitelisted players to join the server. */
    private boolean whiteListEnforced;
    /** The maximum number of players that can be connected at a time. */
    protected int maxPlayers;
    private int viewDistance;
    private WorldSettings.GameType gameType;
    /** True if all players are allowed to use commands (cheats). */
    private boolean commandsAllowedForAll;
    /** index into playerEntities of player to ping, updated every tick; currently hardcoded to max at 200 players */
    private int playerPingIndex;
    private static final String __OBFID = "CL_00001423";

    public ServerConfigurationManager(MinecraftServer p_i1500_1_)
    {
        this.bannedPlayers = new UserListBans(field_152613_a);
        this.bannedIPs = new BanList(field_152614_b);
        this.ops = new UserListOps(field_152615_c);
        this.whiteListedPlayers = new UserListWhitelist(field_152616_d);
        this.field_148547_k = Maps.newHashMap();
        this.mcServer = p_i1500_1_;
        this.bannedPlayers.func_152686_a(false);
        this.bannedIPs.func_152686_a(false);
        this.maxPlayers = 8;
    }

    public void initializeConnectionToPlayer(NetworkManager p_72355_1_, EntityPlayerMP p_72355_2_, NetHandlerPlayServer nethandlerplayserver)
    {
        GameProfile gameprofile = p_72355_2_.getGameProfile();
        PlayerProfileCache playerprofilecache = this.mcServer.func_152358_ax();
        GameProfile gameprofile1 = playerprofilecache.func_152652_a(gameprofile.getId());
        String s = gameprofile1 == null ? gameprofile.getName() : gameprofile1.getName();
        playerprofilecache.func_152649_a(gameprofile);
        NBTTagCompound nbttagcompound = this.readPlayerDataFromFile(p_72355_2_);

        World playerWorld = this.mcServer.worldServerForDimension(p_72355_2_.dimension);
        if (playerWorld==null)
        {
            p_72355_2_.dimension=0;
            playerWorld=this.mcServer.worldServerForDimension(0);
            ChunkCoordinates spawnPoint = playerWorld.provider.getRandomizedSpawnPoint();
            p_72355_2_.setPosition(spawnPoint.posX, spawnPoint.posY, spawnPoint.posZ);
        }

        p_72355_2_.setWorld(playerWorld);
        p_72355_2_.theItemInWorldManager.setWorld((WorldServer)p_72355_2_.worldObj);
        String s1 = "local";

        if (p_72355_1_.getSocketAddress() != null)
        {
            s1 = p_72355_1_.getSocketAddress().toString();
        }

        logger.info(p_72355_2_.getCommandSenderName() + "[" + s1 + "] logged in with entity id " + p_72355_2_.getEntityId() + " at (" + p_72355_2_.posX + ", " + p_72355_2_.posY + ", " + p_72355_2_.posZ + ")");
        WorldServer worldserver = this.mcServer.worldServerForDimension(p_72355_2_.dimension);
        ChunkCoordinates chunkcoordinates = worldserver.getSpawnPoint();
        this.func_72381_a(p_72355_2_, (EntityPlayerMP)null, worldserver);
        p_72355_2_.playerNetServerHandler = nethandlerplayserver;
        nethandlerplayserver.sendPacket(new S01PacketJoinGame(p_72355_2_.getEntityId(), p_72355_2_.theItemInWorldManager.getGameType(), worldserver.getWorldInfo().isHardcoreModeEnabled(), worldserver.provider.dimensionId, worldserver.difficultySetting, this.getMaxPlayers(), worldserver.getWorldInfo().getTerrainType()));
        nethandlerplayserver.sendPacket(new S3FPacketCustomPayload("MC|Brand", this.getServerInstance().getServerModName().getBytes(Charsets.UTF_8)));
        nethandlerplayserver.sendPacket(new S05PacketSpawnPosition(chunkcoordinates.posX, chunkcoordinates.posY, chunkcoordinates.posZ));
        nethandlerplayserver.sendPacket(new S39PacketPlayerAbilities(p_72355_2_.capabilities));
        nethandlerplayserver.sendPacket(new S09PacketHeldItemChange(p_72355_2_.inventory.currentItem));
        p_72355_2_.func_147099_x().func_150877_d();
        p_72355_2_.func_147099_x().func_150884_b(p_72355_2_);
        this.func_96456_a((ServerScoreboard)worldserver.getScoreboard(), p_72355_2_);
        this.mcServer.func_147132_au();
        ChatComponentTranslation chatcomponenttranslation;

        if (!p_72355_2_.getCommandSenderName().equalsIgnoreCase(s))
        {
            chatcomponenttranslation = new ChatComponentTranslation("multiplayer.player.joined.renamed", new Object[] {p_72355_2_.func_145748_c_(), s});
        }
        else
        {
            chatcomponenttranslation = new ChatComponentTranslation("multiplayer.player.joined", new Object[] {p_72355_2_.func_145748_c_()});
        }

        chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.YELLOW);
        this.sendChatMsg(chatcomponenttranslation);
        this.playerLoggedIn(p_72355_2_);
        nethandlerplayserver.setPlayerLocation(p_72355_2_.posX, p_72355_2_.posY, p_72355_2_.posZ, p_72355_2_.rotationYaw, p_72355_2_.rotationPitch);
        this.updateTimeAndWeatherForPlayer(p_72355_2_, worldserver);

        if (this.mcServer.getTexturePack().length() > 0)
        {
            p_72355_2_.requestTexturePackLoad(this.mcServer.getTexturePack());
        }

        Iterator iterator = p_72355_2_.getActivePotionEffects().iterator();

        while (iterator.hasNext())
        {
            PotionEffect potioneffect = (PotionEffect)iterator.next();
            nethandlerplayserver.sendPacket(new S1DPacketEntityEffect(p_72355_2_.getEntityId(), potioneffect));
        }

        p_72355_2_.addSelfToInternalCraftingInventory();

        FMLCommonHandler.instance().firePlayerLoggedIn(p_72355_2_);
        if (nbttagcompound != null && nbttagcompound.hasKey("Riding", 10))
        {
            Entity entity = EntityList.createEntityFromNBT(nbttagcompound.getCompoundTag("Riding"), worldserver);

            if (entity != null)
            {
                entity.forceSpawn = true;
                worldserver.spawnEntityInWorld(entity);
                p_72355_2_.mountEntity(entity);
                entity.forceSpawn = false;
            }
        }
    }

    protected void func_96456_a(ServerScoreboard p_96456_1_, EntityPlayerMP p_96456_2_)
    {
        HashSet hashset = new HashSet();
        Iterator iterator = p_96456_1_.getTeams().iterator();

        while (iterator.hasNext())
        {
            ScorePlayerTeam scoreplayerteam = (ScorePlayerTeam)iterator.next();
            p_96456_2_.playerNetServerHandler.sendPacket(new S3EPacketTeams(scoreplayerteam, 0));
        }

        for (int i = 0; i < 3; ++i)
        {
            ScoreObjective scoreobjective = p_96456_1_.func_96539_a(i);

            if (scoreobjective != null && !hashset.contains(scoreobjective))
            {
                List list = p_96456_1_.func_96550_d(scoreobjective);
                Iterator iterator1 = list.iterator();

                while (iterator1.hasNext())
                {
                    Packet packet = (Packet)iterator1.next();
                    p_96456_2_.playerNetServerHandler.sendPacket(packet);
                }

                hashset.add(scoreobjective);
            }
        }
    }

    /**
     * Sets the NBT manager to the one for the WorldServer given.
     */
    public void setPlayerManager(WorldServer[] p_72364_1_)
    {
        this.playerNBTManagerObj = p_72364_1_[0].getSaveHandler().getSaveHandler();
    }

    public void func_72375_a(EntityPlayerMP p_72375_1_, WorldServer p_72375_2_)
    {
        WorldServer worldserver1 = p_72375_1_.getServerForPlayer();

        if (p_72375_2_ != null)
        {
            p_72375_2_.getPlayerManager().removePlayer(p_72375_1_);
        }

        worldserver1.getPlayerManager().addPlayer(p_72375_1_);
        worldserver1.theChunkProviderServer.loadChunk((int)p_72375_1_.posX >> 4, (int)p_72375_1_.posZ >> 4);
    }

    public int getEntityViewDistance()
    {
        return PlayerManager.getFurthestViewableBlock(this.getViewDistance());
    }

    /**
     * called during player login. reads the player information from disk.
     */
    public NBTTagCompound readPlayerDataFromFile(EntityPlayerMP p_72380_1_)
    {
        NBTTagCompound nbttagcompound = this.mcServer.worldServers[0].getWorldInfo().getPlayerNBTTagCompound();
        NBTTagCompound nbttagcompound1;

        if (p_72380_1_.getCommandSenderName().equals(this.mcServer.getServerOwner()) && nbttagcompound != null)
        {
            p_72380_1_.readFromNBT(nbttagcompound);
            nbttagcompound1 = nbttagcompound;
            logger.debug("loading single player");
            net.minecraftforge.event.ForgeEventFactory.firePlayerLoadingEvent(p_72380_1_, this.playerNBTManagerObj, p_72380_1_.getUniqueID().toString());
        }
        else
        {
            nbttagcompound1 = this.playerNBTManagerObj.readPlayerData(p_72380_1_);
        }

        return nbttagcompound1;
    }

    public NBTTagCompound getPlayerNBT(EntityPlayerMP player)
    {
        // Hacky method to allow loading the NBT for a player prior to login
        NBTTagCompound nbttagcompound = this.mcServer.worldServers[0].getWorldInfo().getPlayerNBTTagCompound();
        if (player.getCommandSenderName().equals(this.mcServer.getServerOwner()) && nbttagcompound != null)
        {
            return nbttagcompound;
        }
        else
        {
            return ((SaveHandler)this.playerNBTManagerObj).getPlayerNBT(player);
        }
    }
    /**
     * also stores the NBTTags if this is an intergratedPlayerList
     */
    protected void writePlayerData(EntityPlayerMP p_72391_1_)
    {
        if (p_72391_1_.playerNetServerHandler == null) return;

        this.playerNBTManagerObj.writePlayerData(p_72391_1_);
        StatisticsFile statisticsfile = (StatisticsFile)this.field_148547_k.get(p_72391_1_.getUniqueID());

        if (statisticsfile != null)
        {
            statisticsfile.func_150883_b();
        }
    }

    /**
     * Called when a player successfully logs in. Reads player data from disk and inserts the player into the world.
     */
    public void playerLoggedIn(EntityPlayerMP p_72377_1_)
    {
        this.sendPacketToAllPlayers(new S38PacketPlayerListItem(p_72377_1_.getCommandSenderName(), true, 1000));
        this.playerEntityList.add(p_72377_1_);
        WorldServer worldserver = this.mcServer.worldServerForDimension(p_72377_1_.dimension);
        ChunkIOExecutor.adjustPoolSize(this.getCurrentPlayerCount());
        worldserver.spawnEntityInWorld(p_72377_1_);
        this.func_72375_a(p_72377_1_, (WorldServer)null);

        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            EntityPlayerMP entityplayermp1 = (EntityPlayerMP)this.playerEntityList.get(i);
            p_72377_1_.playerNetServerHandler.sendPacket(new S38PacketPlayerListItem(entityplayermp1.getCommandSenderName(), true, entityplayermp1.ping));
        }
    }

    /**
     * Update which chunks the player needs info on.
     */
    public void updatePlayerPertinentChunks(EntityPlayerMP p_72358_1_)
    {
        p_72358_1_.getServerForPlayer().getPlayerManager().updatePlayerPertinentChunks(p_72358_1_);
    }

    /**
     * Called when a player disconnects from the game. Writes player data to disk and removes them from the world.
     */
    public void playerLoggedOut(EntityPlayerMP p_72367_1_)
    {
        FMLCommonHandler.instance().firePlayerLoggedOut(p_72367_1_);
        p_72367_1_.triggerAchievement(StatList.leaveGameStat);
        this.writePlayerData(p_72367_1_);
        WorldServer worldserver = p_72367_1_.getServerForPlayer();

        if (p_72367_1_.ridingEntity != null)
        {
            worldserver.removePlayerEntityDangerously(p_72367_1_.ridingEntity);
            logger.debug("removing player mount");
        }

        worldserver.removeEntity(p_72367_1_);
        worldserver.getPlayerManager().removePlayer(p_72367_1_);
        this.playerEntityList.remove(p_72367_1_);
        this.field_148547_k.remove(p_72367_1_.getUniqueID());
        net.minecraftforge.common.chunkio.ChunkIOExecutor.adjustPoolSize(this.getCurrentPlayerCount());
        this.sendPacketToAllPlayers(new S38PacketPlayerListItem(p_72367_1_.getCommandSenderName(), false, 9999));
    }

    /**
     * checks ban-lists, then white-lists, then space for the server. Returns null on success, or an error message
     */
    public String allowUserToConnect(SocketAddress p_148542_1_, GameProfile p_148542_2_)
    {
        String s;

        if (this.bannedPlayers.func_152702_a(p_148542_2_))
        {
            UserListBansEntry userlistbansentry = (UserListBansEntry)this.bannedPlayers.func_152683_b(p_148542_2_);
            s = "You are banned from this server!\nReason: " + userlistbansentry.getBanReason();

            if (userlistbansentry.getBanEndDate() != null)
            {
                s = s + "\nYour ban will be removed on " + dateFormat.format(userlistbansentry.getBanEndDate());
            }

            return s;
        }
        else if (!this.func_152607_e(p_148542_2_))
        {
            return "You are not white-listed on this server!";
        }
        else if (this.bannedIPs.func_152708_a(p_148542_1_))
        {
            IPBanEntry ipbanentry = this.bannedIPs.func_152709_b(p_148542_1_);
            s = "Your IP address is banned from this server!\nReason: " + ipbanentry.getBanReason();

            if (ipbanentry.getBanEndDate() != null)
            {
                s = s + "\nYour ban will be removed on " + dateFormat.format(ipbanentry.getBanEndDate());
            }

            return s;
        }
        else
        {
            return this.playerEntityList.size() >= this.maxPlayers ? "The server is full!" : null;
        }
    }

    /**
     * also checks for multiple logins across servers
     */
    public EntityPlayerMP createPlayerForUser(GameProfile p_148545_1_)
    {
        UUID uuid = EntityPlayer.func_146094_a(p_148545_1_);
        ArrayList arraylist = Lists.newArrayList();
        EntityPlayerMP entityplayermp;

        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            entityplayermp = (EntityPlayerMP)this.playerEntityList.get(i);

            if (entityplayermp.getUniqueID().equals(uuid))
            {
                arraylist.add(entityplayermp);
            }
        }

        Iterator iterator = arraylist.iterator();

        while (iterator.hasNext())
        {
            entityplayermp = (EntityPlayerMP)iterator.next();
            entityplayermp.playerNetServerHandler.kickPlayerFromServer("You logged in from another location");
        }

        Object object;

        if (this.mcServer.isDemo())
        {
            object = new DemoWorldManager(this.mcServer.worldServerForDimension(0));
        }
        else
        {
            object = new ItemInWorldManager(this.mcServer.worldServerForDimension(0));
        }

        return new EntityPlayerMP(this.mcServer, this.mcServer.worldServerForDimension(0), p_148545_1_, (ItemInWorldManager)object);
    }

    /**
     * creates and returns a respawned player based on the provided PlayerEntity. Args are the PlayerEntityMP to
     * respawn, an INT for the dimension to respawn into (usually 0), and a boolean value that is true if the player
     * beat the game rather than dying
     */
    public EntityPlayerMP respawnPlayer(EntityPlayerMP p_72368_1_, int p_72368_2_, boolean p_72368_3_)
    {
        World world = mcServer.worldServerForDimension(p_72368_2_);
        if (world == null)
        {
            p_72368_2_ = 0;
        }
        else if (!world.provider.canRespawnHere())
        {
            p_72368_2_ = world.provider.getRespawnDimension(p_72368_1_);
        }

        p_72368_1_.getServerForPlayer().getEntityTracker().removePlayerFromTrackers(p_72368_1_);
        p_72368_1_.getServerForPlayer().getEntityTracker().removeEntityFromAllTrackingPlayers(p_72368_1_);
        p_72368_1_.getServerForPlayer().getPlayerManager().removePlayer(p_72368_1_);
        this.playerEntityList.remove(p_72368_1_);
        this.mcServer.worldServerForDimension(p_72368_1_.dimension).removePlayerEntityDangerously(p_72368_1_);
        ChunkCoordinates chunkcoordinates = p_72368_1_.getBedLocation(p_72368_2_);
        boolean flag1 = p_72368_1_.isSpawnForced(p_72368_2_);
        p_72368_1_.dimension = p_72368_2_;
        Object object;

        if (this.mcServer.isDemo())
        {
            object = new DemoWorldManager(this.mcServer.worldServerForDimension(p_72368_1_.dimension));
        }
        else
        {
            object = new ItemInWorldManager(this.mcServer.worldServerForDimension(p_72368_1_.dimension));
        }

        EntityPlayerMP entityplayermp1 = new EntityPlayerMP(this.mcServer, this.mcServer.worldServerForDimension(p_72368_1_.dimension), p_72368_1_.getGameProfile(), (ItemInWorldManager)object);
        entityplayermp1.playerNetServerHandler = p_72368_1_.playerNetServerHandler;
        entityplayermp1.clonePlayer(p_72368_1_, p_72368_3_);
        entityplayermp1.dimension = p_72368_2_;
        entityplayermp1.setEntityId(p_72368_1_.getEntityId());
        WorldServer worldserver = this.mcServer.worldServerForDimension(p_72368_1_.dimension);
        this.func_72381_a(entityplayermp1, p_72368_1_, worldserver);
        ChunkCoordinates chunkcoordinates1;

        if (chunkcoordinates != null)
        {
            chunkcoordinates1 = EntityPlayer.verifyRespawnCoordinates(this.mcServer.worldServerForDimension(p_72368_1_.dimension), chunkcoordinates, flag1);

            if (chunkcoordinates1 != null)
            {
                entityplayermp1.setLocationAndAngles((double)((float)chunkcoordinates1.posX + 0.5F), (double)((float)chunkcoordinates1.posY + 0.1F), (double)((float)chunkcoordinates1.posZ + 0.5F), 0.0F, 0.0F);
                entityplayermp1.setSpawnChunk(chunkcoordinates, flag1);
            }
            else
            {
                entityplayermp1.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(0, 0.0F));
            }
        }

        worldserver.theChunkProviderServer.loadChunk((int)entityplayermp1.posX >> 4, (int)entityplayermp1.posZ >> 4);

        while (!worldserver.getCollidingBoundingBoxes(entityplayermp1, entityplayermp1.boundingBox).isEmpty())
        {
            entityplayermp1.setPosition(entityplayermp1.posX, entityplayermp1.posY + 1.0D, entityplayermp1.posZ);
        }

        entityplayermp1.playerNetServerHandler.sendPacket(new S07PacketRespawn(entityplayermp1.dimension, entityplayermp1.worldObj.difficultySetting, entityplayermp1.worldObj.getWorldInfo().getTerrainType(), entityplayermp1.theItemInWorldManager.getGameType()));
        chunkcoordinates1 = worldserver.getSpawnPoint();
        entityplayermp1.playerNetServerHandler.setPlayerLocation(entityplayermp1.posX, entityplayermp1.posY, entityplayermp1.posZ, entityplayermp1.rotationYaw, entityplayermp1.rotationPitch);
        entityplayermp1.playerNetServerHandler.sendPacket(new S05PacketSpawnPosition(chunkcoordinates1.posX, chunkcoordinates1.posY, chunkcoordinates1.posZ));
        entityplayermp1.playerNetServerHandler.sendPacket(new S1FPacketSetExperience(entityplayermp1.experience, entityplayermp1.experienceTotal, entityplayermp1.experienceLevel));
        this.updateTimeAndWeatherForPlayer(entityplayermp1, worldserver);
        worldserver.getPlayerManager().addPlayer(entityplayermp1);
        worldserver.spawnEntityInWorld(entityplayermp1);
        this.playerEntityList.add(entityplayermp1);
        entityplayermp1.addSelfToInternalCraftingInventory();
        entityplayermp1.setHealth(entityplayermp1.getHealth());
        FMLCommonHandler.instance().firePlayerRespawnEvent(entityplayermp1);
        return entityplayermp1;
    }

    public void transferPlayerToDimension(EntityPlayerMP p_72356_1_, int p_72356_2_)
    {
        transferPlayerToDimension(p_72356_1_, p_72356_2_, mcServer.worldServerForDimension(p_72356_2_).getDefaultTeleporter());
    }

    public void transferPlayerToDimension(EntityPlayerMP p_72356_1_, int p_72356_2_, Teleporter teleporter)
    {
        int j = p_72356_1_.dimension;
        WorldServer worldserver = this.mcServer.worldServerForDimension(p_72356_1_.dimension);
        p_72356_1_.dimension = p_72356_2_;
        WorldServer worldserver1 = this.mcServer.worldServerForDimension(p_72356_1_.dimension);
        p_72356_1_.playerNetServerHandler.sendPacket(new S07PacketRespawn(p_72356_1_.dimension, worldserver1.difficultySetting, worldserver1.getWorldInfo().getTerrainType(), p_72356_1_.theItemInWorldManager.getGameType())); // Forge: Use new dimensions information
        worldserver.removePlayerEntityDangerously(p_72356_1_);
        p_72356_1_.isDead = false;
        this.transferEntityToWorld(p_72356_1_, j, worldserver, worldserver1, teleporter);
        this.func_72375_a(p_72356_1_, worldserver);
        p_72356_1_.playerNetServerHandler.setPlayerLocation(p_72356_1_.posX, p_72356_1_.posY, p_72356_1_.posZ, p_72356_1_.rotationYaw, p_72356_1_.rotationPitch);
        p_72356_1_.theItemInWorldManager.setWorld(worldserver1);
        this.updateTimeAndWeatherForPlayer(p_72356_1_, worldserver1);
        this.syncPlayerInventory(p_72356_1_);
        Iterator iterator = p_72356_1_.getActivePotionEffects().iterator();

        while (iterator.hasNext())
        {
            PotionEffect potioneffect = (PotionEffect)iterator.next();
            p_72356_1_.playerNetServerHandler.sendPacket(new S1DPacketEntityEffect(p_72356_1_.getEntityId(), potioneffect));
        }
        FMLCommonHandler.instance().firePlayerChangedDimensionEvent(p_72356_1_, j, p_72356_2_);
    }

    /**
     * Transfers an entity from a world to another world.
     */
    public void transferEntityToWorld(Entity p_82448_1_, int p_82448_2_, WorldServer p_82448_3_, WorldServer p_82448_4_)
    {
        transferEntityToWorld(p_82448_1_, p_82448_2_, p_82448_3_, p_82448_4_, p_82448_4_.getDefaultTeleporter());
    }

    public void transferEntityToWorld(Entity p_82448_1_, int p_82448_2_, WorldServer p_82448_3_, WorldServer p_82448_4_, Teleporter teleporter)
    {
        WorldProvider pOld = p_82448_3_.provider;
        WorldProvider pNew = p_82448_4_.provider;
        double moveFactor = pOld.getMovementFactor() / pNew.getMovementFactor();
        double d0 = p_82448_1_.posX * moveFactor;
        double d1 = p_82448_1_.posZ * moveFactor;
        double d3 = p_82448_1_.posX;
        double d4 = p_82448_1_.posY;
        double d5 = p_82448_1_.posZ;
        float f = p_82448_1_.rotationYaw;
        p_82448_3_.theProfiler.startSection("moving");

        /*
        if (par1Entity.dimension == -1)
        {
            d0 /= d2;
            d1 /= d2;
            par1Entity.setLocationAndAngles(d0, par1Entity.posY, d1, par1Entity.rotationYaw, par1Entity.rotationPitch);

            if (par1Entity.isEntityAlive())
            {
                par3WorldServer.updateEntityWithOptionalForce(par1Entity, false);
            }
        }
        else if (par1Entity.dimension == 0)
        {
            d0 *= d2;
            d1 *= d2;
            par1Entity.setLocationAndAngles(d0, par1Entity.posY, d1, par1Entity.rotationYaw, par1Entity.rotationPitch);

            if (par1Entity.isEntityAlive())
            {
                par3WorldServer.updateEntityWithOptionalForce(par1Entity, false);
            }
        }
        */
        if (p_82448_1_.dimension == 1)
        {
            ChunkCoordinates chunkcoordinates;

            if (p_82448_2_ == 1)
            {
                chunkcoordinates = p_82448_4_.getSpawnPoint();
            }
            else
            {
                chunkcoordinates = p_82448_4_.getEntrancePortalLocation();
            }

            d0 = (double)chunkcoordinates.posX;
            p_82448_1_.posY = (double)chunkcoordinates.posY;
            d1 = (double)chunkcoordinates.posZ;
            p_82448_1_.setLocationAndAngles(d0, p_82448_1_.posY, d1, 90.0F, 0.0F);

            if (p_82448_1_.isEntityAlive())
            {
                p_82448_3_.updateEntityWithOptionalForce(p_82448_1_, false);
            }
        }

        p_82448_3_.theProfiler.endSection();

        if (p_82448_2_ != 1)
        {
            p_82448_3_.theProfiler.startSection("placing");
            d0 = (double)MathHelper.clamp_int((int)d0, -29999872, 29999872);
            d1 = (double)MathHelper.clamp_int((int)d1, -29999872, 29999872);

            if (p_82448_1_.isEntityAlive())
            {
                p_82448_1_.setLocationAndAngles(d0, p_82448_1_.posY, d1, p_82448_1_.rotationYaw, p_82448_1_.rotationPitch);
                teleporter.placeInPortal(p_82448_1_, d3, d4, d5, f);
                p_82448_4_.spawnEntityInWorld(p_82448_1_);
                p_82448_4_.updateEntityWithOptionalForce(p_82448_1_, false);
            }

            p_82448_3_.theProfiler.endSection();
        }

        p_82448_1_.setWorld(p_82448_4_);
    }

    /**
     * sends 1 player per tick, but only sends a player once every 600 ticks
     */
    public void sendPlayerInfoToAllPlayers()
    {
        if (++this.playerPingIndex > 600)
        {
            this.playerPingIndex = 0;
        }

        if (this.playerPingIndex < this.playerEntityList.size())
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)this.playerEntityList.get(this.playerPingIndex);
            this.sendPacketToAllPlayers(new S38PacketPlayerListItem(entityplayermp.getCommandSenderName(), true, entityplayermp.ping));
        }
    }

    public void sendPacketToAllPlayers(Packet p_148540_1_)
    {
        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            ((EntityPlayerMP)this.playerEntityList.get(i)).playerNetServerHandler.sendPacket(p_148540_1_);
        }
    }

    /**
     * Sends a packet to all players in the specified Dimension
     */
    public void sendPacketToAllPlayersInDimension(Packet p_148537_1_, int p_148537_2_)
    {
        for (int j = 0; j < this.playerEntityList.size(); ++j)
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)this.playerEntityList.get(j);

            if (entityplayermp.dimension == p_148537_2_)
            {
                entityplayermp.playerNetServerHandler.sendPacket(p_148537_1_);
            }
        }
    }

    public String func_152609_b(boolean p_152609_1_)
    {
        String s = "";
        ArrayList arraylist = Lists.newArrayList(this.playerEntityList);

        for (int i = 0; i < arraylist.size(); ++i)
        {
            if (i > 0)
            {
                s = s + ", ";
            }

            s = s + ((EntityPlayerMP)arraylist.get(i)).getCommandSenderName();

            if (p_152609_1_)
            {
                s = s + " (" + ((EntityPlayerMP)arraylist.get(i)).getUniqueID().toString() + ")";
            }
        }

        return s;
    }

    /**
     * Returns an array of the usernames of all the connected players.
     */
    public String[] getAllUsernames()
    {
        String[] astring = new String[this.playerEntityList.size()];

        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            astring[i] = ((EntityPlayerMP)this.playerEntityList.get(i)).getCommandSenderName();
        }

        return astring;
    }

    public GameProfile[] func_152600_g()
    {
        GameProfile[] agameprofile = new GameProfile[this.playerEntityList.size()];

        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            agameprofile[i] = ((EntityPlayerMP)this.playerEntityList.get(i)).getGameProfile();
        }

        return agameprofile;
    }

    public UserListBans func_152608_h()
    {
        return this.bannedPlayers;
    }

    public BanList getBannedIPs()
    {
        return this.bannedIPs;
    }

    public void func_152605_a(GameProfile p_152605_1_)
    {
        this.ops.func_152687_a(new UserListOpsEntry(p_152605_1_, this.mcServer.getOpPermissionLevel()));
    }

    public void func_152610_b(GameProfile p_152610_1_)
    {
        this.ops.func_152684_c(p_152610_1_);
    }

    public boolean func_152607_e(GameProfile p_152607_1_)
    {
        return !this.whiteListEnforced || this.ops.func_152692_d(p_152607_1_) || this.whiteListedPlayers.func_152692_d(p_152607_1_);
    }

    public boolean func_152596_g(GameProfile p_152596_1_)
    {
        return this.ops.func_152692_d(p_152596_1_) || this.mcServer.isSinglePlayer() && this.mcServer.worldServers[0].getWorldInfo().areCommandsAllowed() && this.mcServer.getServerOwner().equalsIgnoreCase(p_152596_1_.getName()) || this.commandsAllowedForAll;
    }

    public EntityPlayerMP func_152612_a(String p_152612_1_)
    {
        Iterator iterator = this.playerEntityList.iterator();
        EntityPlayerMP entityplayermp;

        do
        {
            if (!iterator.hasNext())
            {
                return null;
            }

            entityplayermp = (EntityPlayerMP)iterator.next();
        }
        while (!entityplayermp.getCommandSenderName().equalsIgnoreCase(p_152612_1_));

        return entityplayermp;
    }

    /**
     * Find all players in a specified range and narrowing down by other parameters
     */
    public List findPlayers(ChunkCoordinates p_82449_1_, int p_82449_2_, int p_82449_3_, int p_82449_4_, int p_82449_5_, int p_82449_6_, int p_82449_7_, Map p_82449_8_, String p_82449_9_, String p_82449_10_, World p_82449_11_)
    {
        if (this.playerEntityList.isEmpty())
        {
            return Collections.emptyList();
        }
        else
        {
            Object object = new ArrayList();
            boolean flag = p_82449_4_ < 0;
            boolean flag1 = p_82449_9_ != null && p_82449_9_.startsWith("!");
            boolean flag2 = p_82449_10_ != null && p_82449_10_.startsWith("!");
            int k1 = p_82449_2_ * p_82449_2_;
            int l1 = p_82449_3_ * p_82449_3_;
            p_82449_4_ = MathHelper.abs_int(p_82449_4_);

            if (flag1)
            {
                p_82449_9_ = p_82449_9_.substring(1);
            }

            if (flag2)
            {
                p_82449_10_ = p_82449_10_.substring(1);
            }

            for (int i2 = 0; i2 < this.playerEntityList.size(); ++i2)
            {
                EntityPlayerMP entityplayermp = (EntityPlayerMP)this.playerEntityList.get(i2);

                if ((p_82449_11_ == null || entityplayermp.worldObj == p_82449_11_) && (p_82449_9_ == null || flag1 != p_82449_9_.equalsIgnoreCase(entityplayermp.getCommandSenderName())))
                {
                    if (p_82449_10_ != null)
                    {
                        Team team = entityplayermp.getTeam();
                        String s2 = team == null ? "" : team.getRegisteredName();

                        if (flag2 == p_82449_10_.equalsIgnoreCase(s2))
                        {
                            continue;
                        }
                    }

                    if (p_82449_1_ != null && (p_82449_2_ > 0 || p_82449_3_ > 0))
                    {
                        float f = p_82449_1_.getDistanceSquaredToChunkCoordinates(entityplayermp.getPlayerCoordinates());

                        if (p_82449_2_ > 0 && f < (float)k1 || p_82449_3_ > 0 && f > (float)l1)
                        {
                            continue;
                        }
                    }

                    if (this.func_96457_a(entityplayermp, p_82449_8_) && (p_82449_5_ == WorldSettings.GameType.NOT_SET.getID() || p_82449_5_ == entityplayermp.theItemInWorldManager.getGameType().getID()) && (p_82449_6_ <= 0 || entityplayermp.experienceLevel >= p_82449_6_) && entityplayermp.experienceLevel <= p_82449_7_)
                    {
                        ((List)object).add(entityplayermp);
                    }
                }
            }

            if (p_82449_1_ != null)
            {
                Collections.sort((List)object, new PlayerPositionComparator(p_82449_1_));
            }

            if (flag)
            {
                Collections.reverse((List)object);
            }

            if (p_82449_4_ > 0)
            {
                object = ((List)object).subList(0, Math.min(p_82449_4_, ((List)object).size()));
            }

            return (List)object;
        }
    }

    private boolean func_96457_a(EntityPlayer p_96457_1_, Map p_96457_2_)
    {
        if (p_96457_2_ != null && p_96457_2_.size() != 0)
        {
            Iterator iterator = p_96457_2_.entrySet().iterator();
            Entry entry;
            boolean flag;
            int i;

            do
            {
                if (!iterator.hasNext())
                {
                    return true;
                }

                entry = (Entry)iterator.next();
                String s = (String)entry.getKey();
                flag = false;

                if (s.endsWith("_min") && s.length() > 4)
                {
                    flag = true;
                    s = s.substring(0, s.length() - 4);
                }

                Scoreboard scoreboard = p_96457_1_.getWorldScoreboard();
                ScoreObjective scoreobjective = scoreboard.getObjective(s);

                if (scoreobjective == null)
                {
                    return false;
                }

                Score score = p_96457_1_.getWorldScoreboard().func_96529_a(p_96457_1_.getCommandSenderName(), scoreobjective);
                i = score.getScorePoints();

                if (i < ((Integer)entry.getValue()).intValue() && flag)
                {
                    return false;
                }
            }
            while (i <= ((Integer)entry.getValue()).intValue() || flag);

            return false;
        }
        else
        {
            return true;
        }
    }

    /**
     * params: x,y,z,r,dimension. The packet is sent to all players within r radius of x,y,z (r^2>x^2+y^2+z^2)
     */
    public void sendToAllNear(double p_148541_1_, double p_148541_3_, double p_148541_5_, double p_148541_7_, int p_148541_9_, Packet p_148541_10_)
    {
        this.sendToAllNearExcept((EntityPlayer)null, p_148541_1_, p_148541_3_, p_148541_5_, p_148541_7_, p_148541_9_, p_148541_10_);
    }

    /**
     * params: srcPlayer,x,y,z,r,dimension. The packet is not sent to the srcPlayer, but all other players within the
     * search radius
     */
    public void sendToAllNearExcept(EntityPlayer p_148543_1_, double p_148543_2_, double p_148543_4_, double p_148543_6_, double p_148543_8_, int p_148543_10_, Packet p_148543_11_)
    {
        for (int j = 0; j < this.playerEntityList.size(); ++j)
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)this.playerEntityList.get(j);

            if (entityplayermp != p_148543_1_ && entityplayermp.dimension == p_148543_10_)
            {
                double d4 = p_148543_2_ - entityplayermp.posX;
                double d5 = p_148543_4_ - entityplayermp.posY;
                double d6 = p_148543_6_ - entityplayermp.posZ;

                if (d4 * d4 + d5 * d5 + d6 * d6 < p_148543_8_ * p_148543_8_)
                {
                    entityplayermp.playerNetServerHandler.sendPacket(p_148543_11_);
                }
            }
        }
    }

    /**
     * Saves all of the players' current states.
     */
    public void saveAllPlayerData()
    {
        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            this.writePlayerData((EntityPlayerMP)this.playerEntityList.get(i));
        }
    }

    public void func_152601_d(GameProfile p_152601_1_)
    {
        this.whiteListedPlayers.func_152687_a(new UserListWhitelistEntry(p_152601_1_));
    }

    public void func_152597_c(GameProfile p_152597_1_)
    {
        this.whiteListedPlayers.func_152684_c(p_152597_1_);
    }

    public UserListWhitelist func_152599_k()
    {
        return this.whiteListedPlayers;
    }

    public String[] func_152598_l()
    {
        return this.whiteListedPlayers.func_152685_a();
    }

    public UserListOps func_152603_m()
    {
        return this.ops;
    }

    public String[] func_152606_n()
    {
        return this.ops.func_152685_a();
    }

    /**
     * Either does nothing, or calls readWhiteList.
     */
    public void loadWhiteList() {}

    /**
     * Updates the time and weather for the given player to those of the given world
     */
    public void updateTimeAndWeatherForPlayer(EntityPlayerMP p_72354_1_, WorldServer p_72354_2_)
    {
        p_72354_1_.playerNetServerHandler.sendPacket(new S03PacketTimeUpdate(p_72354_2_.getTotalWorldTime(), p_72354_2_.getWorldTime(), p_72354_2_.getGameRules().getGameRuleBooleanValue("doDaylightCycle")));

        if (p_72354_2_.isRaining())
        {
            p_72354_1_.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(1, 0.0F));
            p_72354_1_.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(7, p_72354_2_.getRainStrength(1.0F)));
            p_72354_1_.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(8, p_72354_2_.getWeightedThunderStrength(1.0F)));
        }
    }

    /**
     * sends the players inventory to himself
     */
    public void syncPlayerInventory(EntityPlayerMP p_72385_1_)
    {
        p_72385_1_.sendContainerToPlayer(p_72385_1_.inventoryContainer);
        p_72385_1_.setPlayerHealthUpdated();
        p_72385_1_.playerNetServerHandler.sendPacket(new S09PacketHeldItemChange(p_72385_1_.inventory.currentItem));
    }

    /**
     * Returns the number of players currently on the server.
     */
    public int getCurrentPlayerCount()
    {
        return this.playerEntityList.size();
    }

    /**
     * Returns the maximum number of players allowed on the server.
     */
    public int getMaxPlayers()
    {
        return this.maxPlayers;
    }

    /**
     * Returns an array of usernames for which player.dat exists for.
     */
    public String[] getAvailablePlayerDat()
    {
        return this.mcServer.worldServers[0].getSaveHandler().getSaveHandler().getAvailablePlayerDat();
    }

    public void setWhiteListEnabled(boolean p_72371_1_)
    {
        this.whiteListEnforced = p_72371_1_;
    }

    public List getPlayerList(String p_72382_1_)
    {
        ArrayList arraylist = new ArrayList();
        Iterator iterator = this.playerEntityList.iterator();

        while (iterator.hasNext())
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)iterator.next();

            if (entityplayermp.getPlayerIP().equals(p_72382_1_))
            {
                arraylist.add(entityplayermp);
            }
        }

        return arraylist;
    }

    /**
     * Gets the View Distance.
     */
    public int getViewDistance()
    {
        return this.viewDistance;
    }

    public MinecraftServer getServerInstance()
    {
        return this.mcServer;
    }

    /**
     * On integrated servers, returns the host's player data to be written to level.dat.
     */
    public NBTTagCompound getHostPlayerData()
    {
        return null;
    }

    @SideOnly(Side.CLIENT)
    public void func_152604_a(WorldSettings.GameType p_152604_1_)
    {
        this.gameType = p_152604_1_;
    }

    private void func_72381_a(EntityPlayerMP p_72381_1_, EntityPlayerMP p_72381_2_, World p_72381_3_)
    {
        if (p_72381_2_ != null)
        {
            p_72381_1_.theItemInWorldManager.setGameType(p_72381_2_.theItemInWorldManager.getGameType());
        }
        else if (this.gameType != null)
        {
            p_72381_1_.theItemInWorldManager.setGameType(this.gameType);
        }

        p_72381_1_.theItemInWorldManager.initializeGameType(p_72381_3_.getWorldInfo().getGameType());
    }

    /**
     * Sets whether all players are allowed to use commands (cheats) on the server.
     */
    @SideOnly(Side.CLIENT)
    public void setCommandsAllowedForAll(boolean p_72387_1_)
    {
        this.commandsAllowedForAll = p_72387_1_;
    }

    /**
     * Kicks everyone with "Server closed" as reason.
     */
    public void removeAllPlayers()
    {
        for (int i = 0; i < this.playerEntityList.size(); ++i)
        {
            ((EntityPlayerMP)this.playerEntityList.get(i)).playerNetServerHandler.kickPlayerFromServer("Server closed");
        }
    }

    public void sendChatMsgImpl(IChatComponent p_148544_1_, boolean p_148544_2_)
    {
        this.mcServer.addChatMessage(p_148544_1_);
        this.sendPacketToAllPlayers(new S02PacketChat(p_148544_1_, p_148544_2_));
    }

    /**
     * Sends the given string to every player as chat message.
     */
    public void sendChatMsg(IChatComponent p_148539_1_)
    {
        this.sendChatMsgImpl(p_148539_1_, true);
    }

    public StatisticsFile func_152602_a(EntityPlayer p_152602_1_)
    {
        UUID uuid = p_152602_1_.getUniqueID();
        StatisticsFile statisticsfile = uuid == null ? null : (StatisticsFile)this.field_148547_k.get(uuid);

        if (statisticsfile == null)
        {
            File file1 = new File(this.mcServer.worldServerForDimension(0).getSaveHandler().getWorldDirectory(), "stats");
            File file2 = new File(file1, uuid.toString() + ".json");

            if (!file2.exists())
            {
                File file3 = new File(file1, p_152602_1_.getCommandSenderName() + ".json");

                if (file3.exists() && file3.isFile())
                {
                    file3.renameTo(file2);
                }
            }

            statisticsfile = new StatisticsFile(this.mcServer, file2);
            statisticsfile.func_150882_a();
            this.field_148547_k.put(uuid, statisticsfile);
        }

        return statisticsfile;
    }

    public void func_152611_a(int p_152611_1_)
    {
        this.viewDistance = p_152611_1_;

        if (this.mcServer.worldServers != null)
        {
            WorldServer[] aworldserver = this.mcServer.worldServers;
            int j = aworldserver.length;

            for (int k = 0; k < j; ++k)
            {
                WorldServer worldserver = aworldserver[k];

                if (worldserver != null)
                {
                    worldserver.getPlayerManager().func_152622_a(p_152611_1_);
                }
            }
        }
    }

    @SideOnly(Side.SERVER)
    public boolean isWhiteListEnabled()
    {
        return this.whiteListEnforced;
    }
}