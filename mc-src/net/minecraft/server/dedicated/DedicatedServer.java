package net.minecraft.server.dedicated;

import cpw.mods.fml.common.FMLCommonHandler;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.Proxy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;
import java.util.concurrent.Callable;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.ServerCommand;
import net.minecraft.crash.CrashReport;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.network.rcon.IServer;
import net.minecraft.network.rcon.RConThreadMain;
import net.minecraft.network.rcon.RConThreadQuery;
import net.minecraft.profiler.PlayerUsageSnooper;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.ServerEula;
import net.minecraft.server.gui.MinecraftServerGui;
import net.minecraft.server.management.PreYggdrasilConverter;
import net.minecraft.server.management.ServerConfigurationManager;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.CryptManager;
import net.minecraft.util.MathHelper;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.SERVER)
public class DedicatedServer extends MinecraftServer implements IServer
{
    private static final Logger field_155771_h = LogManager.getLogger();
    public final List pendingCommandList = Collections.synchronizedList(new ArrayList());
    private RConThreadQuery theRConThreadQuery;
    private RConThreadMain theRConThreadMain;
    private PropertyManager settings;
    private ServerEula field_154332_n;
    private boolean canSpawnStructures;
    private WorldSettings.GameType gameType;
    private boolean guiIsEnabled;
    public static boolean allowPlayerLogins = false;
    private static final String __OBFID = "CL_00001784";

    public DedicatedServer(File p_i1508_1_)
    {
        super(p_i1508_1_, Proxy.NO_PROXY);
        Thread thread = new Thread("Server Infinisleeper")
        {
            private static final String __OBFID = "CL_00001787";
            {
                this.setDaemon(true);
                this.start();
            }
            public void run()
            {
                while (true)
                {
                    try
                    {
                        while (true)
                        {
                            Thread.sleep(2147483647L);
                        }
                    }
                    catch (InterruptedException interruptedexception)
                    {
                        ;
                    }
                }
            }
        };
    }

    /**
     * Initialises the server and starts it.
     */
    protected boolean startServer() throws IOException
    {
        Thread thread = new Thread("Server console handler")
        {
            private static final String __OBFID = "CL_00001786";
            public void run()
            {
                BufferedReader bufferedreader = new BufferedReader(new InputStreamReader(System.in));
                String s4;

                try
                {
                    while (!DedicatedServer.this.isServerStopped() && DedicatedServer.this.isServerRunning() && (s4 = bufferedreader.readLine()) != null)
                    {
                        DedicatedServer.this.addPendingCommand(s4, DedicatedServer.this);
                    }
                }
                catch (IOException ioexception1)
                {
                    DedicatedServer.field_155771_h.error("Exception handling console input", ioexception1);
                }
            }
        };
        thread.setDaemon(true);
        thread.start();
        field_155771_h.info("Starting minecraft server version 1.7.10");

        if (Runtime.getRuntime().maxMemory() / 1024L / 1024L < 512L)
        {
            field_155771_h.warn("To start the server with more ram, launch it as \"java -Xmx1024M -Xms1024M -jar minecraft_server.jar\"");
        }

        FMLCommonHandler.instance().onServerStart(this);

        field_155771_h.info("Loading properties");
        this.settings = new PropertyManager(new File("server.properties"));
        this.field_154332_n = new ServerEula(new File("eula.txt"));

        if (!this.field_154332_n.func_154346_a())
        {
            field_155771_h.info("You need to agree to the EULA in order to run the server. Go to eula.txt for more info.");
            this.field_154332_n.func_154348_b();
            return false;
        }
        else
        {
            if (this.isSinglePlayer())
            {
                this.setHostname("127.0.0.1");
            }
            else
            {
                this.setOnlineMode(this.settings.getBooleanProperty("online-mode", true));
                this.setHostname(this.settings.getStringProperty("server-ip", ""));
            }

            this.setCanSpawnAnimals(this.settings.getBooleanProperty("spawn-animals", true));
            this.setCanSpawnNPCs(this.settings.getBooleanProperty("spawn-npcs", true));
            this.setAllowPvp(this.settings.getBooleanProperty("pvp", true));
            this.setAllowFlight(this.settings.getBooleanProperty("allow-flight", false));
            this.func_155759_m(this.settings.getStringProperty("resource-pack", ""));
            this.setMOTD(this.settings.getStringProperty("motd", "A Minecraft Server"));
            this.setForceGamemode(this.settings.getBooleanProperty("force-gamemode", false));
            this.func_143006_e(this.settings.getIntProperty("player-idle-timeout", 0));

            if (this.settings.getIntProperty("difficulty", 1) < 0)
            {
                this.settings.setProperty("difficulty", Integer.valueOf(0));
            }
            else if (this.settings.getIntProperty("difficulty", 1) > 3)
            {
                this.settings.setProperty("difficulty", Integer.valueOf(3));
            }

            this.canSpawnStructures = this.settings.getBooleanProperty("generate-structures", true);
            int i = this.settings.getIntProperty("gamemode", WorldSettings.GameType.SURVIVAL.getID());
            this.gameType = WorldSettings.getGameTypeById(i);
            field_155771_h.info("Default game type: " + this.gameType);
            InetAddress inetaddress = null;

            if (this.getServerHostname().length() > 0)
            {
                inetaddress = InetAddress.getByName(this.getServerHostname());
            }

            if (this.getServerPort() < 0)
            {
                this.setServerPort(this.settings.getIntProperty("server-port", 25565));
            }

            field_155771_h.info("Generating keypair");
            this.setKeyPair(CryptManager.createNewKeyPair());
            field_155771_h.info("Starting Minecraft server on " + (this.getServerHostname().length() == 0 ? "*" : this.getServerHostname()) + ":" + this.getServerPort());

            try
            {
                this.func_147137_ag().addLanEndpoint(inetaddress, this.getServerPort());
            }
            catch (IOException ioexception)
            {
                field_155771_h.warn("**** FAILED TO BIND TO PORT!");
                field_155771_h.warn("The exception was: {}", new Object[] {ioexception.toString()});
                field_155771_h.warn("Perhaps a server is already running on that port?");
                return false;
            }

            if (!this.isServerInOnlineMode())
            {
                field_155771_h.warn("**** SERVER IS RUNNING IN OFFLINE/INSECURE MODE!");
                field_155771_h.warn("The server will make no attempt to authenticate usernames. Beware.");
                field_155771_h.warn("While this makes the game possible to play without internet access, it also opens up the ability for hackers to connect with any username they choose.");
                field_155771_h.warn("To change this, set \"online-mode\" to \"true\" in the server.properties file.");
            }

            if (this.func_152368_aE())
            {
                this.func_152358_ax().func_152658_c();
            }

            if (!PreYggdrasilConverter.func_152714_a(this.settings))
            {
                return false;
            }
            else
            {
                FMLCommonHandler.instance().onServerStarted();
                this.func_152361_a(new DedicatedPlayerList(this));
                long j = System.nanoTime();

                if (this.getFolderName() == null)
                {
                    this.setFolderName(this.settings.getStringProperty("level-name", "world"));
                }

                String s = this.settings.getStringProperty("level-seed", "");
                String s1 = this.settings.getStringProperty("level-type", "DEFAULT");
                String s2 = this.settings.getStringProperty("generator-settings", "");
                long k = (new Random()).nextLong();

                if (s.length() > 0)
                {
                    try
                    {
                        long l = Long.parseLong(s);

                        if (l != 0L)
                        {
                            k = l;
                        }
                    }
                    catch (NumberFormatException numberformatexception)
                    {
                        k = (long)s.hashCode();
                    }
                }

                WorldType worldtype = WorldType.parseWorldType(s1);

                if (worldtype == null)
                {
                    worldtype = WorldType.DEFAULT;
                }

                this.func_147136_ar();
                this.isCommandBlockEnabled();
                this.getOpPermissionLevel();
                this.isSnooperEnabled();
                this.setBuildLimit(this.settings.getIntProperty("max-build-height", 256));
                this.setBuildLimit((this.getBuildLimit() + 8) / 16 * 16);
                this.setBuildLimit(MathHelper.clamp_int(this.getBuildLimit(), 64, 256));
                this.settings.setProperty("max-build-height", Integer.valueOf(this.getBuildLimit()));
                if (!FMLCommonHandler.instance().handleServerAboutToStart(this)) { return false; }
                field_155771_h.info("Preparing level \"" + this.getFolderName() + "\"");
                this.loadAllWorlds(this.getFolderName(), this.getFolderName(), k, worldtype, s2);
                long i1 = System.nanoTime() - j;
                String s3 = String.format("%.3fs", new Object[] {Double.valueOf((double)i1 / 1.0E9D)});
                field_155771_h.info("Done (" + s3 + ")! For help, type \"help\" or \"?\"");

                if (this.settings.getBooleanProperty("enable-query", false))
                {
                    field_155771_h.info("Starting GS4 status listener");
                    this.theRConThreadQuery = new RConThreadQuery(this);
                    this.theRConThreadQuery.startThread();
                }

                if (this.settings.getBooleanProperty("enable-rcon", false))
                {
                    field_155771_h.info("Starting remote control listener");
                    this.theRConThreadMain = new RConThreadMain(this);
                    this.theRConThreadMain.startThread();
                }

                return FMLCommonHandler.instance().handleServerStarting(this);
            }
        }
    }

    public boolean canStructuresSpawn()
    {
        return this.canSpawnStructures;
    }

    public WorldSettings.GameType getGameType()
    {
        return this.gameType;
    }

    public EnumDifficulty func_147135_j()
    {
        return EnumDifficulty.getDifficultyEnum(this.settings.getIntProperty("difficulty", 1));
    }

    /**
     * Defaults to false.
     */
    public boolean isHardcore()
    {
        return this.settings.getBooleanProperty("hardcore", false);
    }

    /**
     * Called on exit from the main run() loop.
     */
    protected void finalTick(CrashReport p_71228_1_) {}

    /**
     * Adds the server info, including from theWorldServer, to the crash report.
     */
    public CrashReport addServerInfoToCrashReport(CrashReport p_71230_1_)
    {
        p_71230_1_ = super.addServerInfoToCrashReport(p_71230_1_);
        p_71230_1_.getCategory().addCrashSectionCallable("Is Modded", new Callable()
        {
            private static final String __OBFID = "CL_00001785";
            public String call()
            {
                String s = DedicatedServer.this.getServerModName();
                return !s.equals("vanilla") ? "Definitely; Server brand changed to \'" + s + "\'" : "Unknown (can\'t tell)";
            }
        });
        p_71230_1_.getCategory().addCrashSectionCallable("Type", new Callable()
        {
            private static final String __OBFID = "CL_00001788";
            public String call()
            {
                return "Dedicated Server (map_server.txt)";
            }
        });
        return p_71230_1_;
    }

    /**
     * Directly calls System.exit(0), instantly killing the program.
     */
    protected void systemExitNow()
    {
        System.exit(0);
    }

    public void updateTimeLightAndEntities()
    {
        super.updateTimeLightAndEntities();
        this.executePendingCommands();
    }

    public boolean getAllowNether()
    {
        return this.settings.getBooleanProperty("allow-nether", true);
    }

    public boolean allowSpawnMonsters()
    {
        return this.settings.getBooleanProperty("spawn-monsters", true);
    }

    public void addServerStatsToSnooper(PlayerUsageSnooper p_70000_1_)
    {
        p_70000_1_.func_152768_a("whitelist_enabled", Boolean.valueOf(this.getConfigurationManager().isWhiteListEnabled()));
        p_70000_1_.func_152768_a("whitelist_count", Integer.valueOf(this.getConfigurationManager().func_152598_l().length));
        super.addServerStatsToSnooper(p_70000_1_);
    }

    /**
     * Returns whether snooping is enabled or not.
     */
    public boolean isSnooperEnabled()
    {
        return this.settings.getBooleanProperty("snooper-enabled", true);
    }

    public void addPendingCommand(String p_71331_1_, ICommandSender p_71331_2_)
    {
        this.pendingCommandList.add(new ServerCommand(p_71331_1_, p_71331_2_));
    }

    public void executePendingCommands()
    {
        while (!this.pendingCommandList.isEmpty())
        {
            ServerCommand servercommand = (ServerCommand)this.pendingCommandList.remove(0);
            this.getCommandManager().executeCommand(servercommand.sender, servercommand.command);
        }
    }

    public boolean isDedicatedServer()
    {
        return true;
    }

    public DedicatedPlayerList getConfigurationManager()
    {
        return (DedicatedPlayerList)super.getConfigurationManager();
    }

    /**
     * Gets an integer property. If it does not exist, set it to the specified value.
     */
    public int getIntProperty(String p_71327_1_, int p_71327_2_)
    {
        return this.settings.getIntProperty(p_71327_1_, p_71327_2_);
    }

    /**
     * Gets a string property. If it does not exist, set it to the specified value.
     */
    public String getStringProperty(String p_71330_1_, String p_71330_2_)
    {
        return this.settings.getStringProperty(p_71330_1_, p_71330_2_);
    }

    /**
     * Gets a boolean property. If it does not exist, set it to the specified value.
     */
    public boolean getBooleanProperty(String p_71332_1_, boolean p_71332_2_)
    {
        return this.settings.getBooleanProperty(p_71332_1_, p_71332_2_);
    }

    /**
     * Saves an Object with the given property name.
     */
    public void setProperty(String p_71328_1_, Object p_71328_2_)
    {
        this.settings.setProperty(p_71328_1_, p_71328_2_);
    }

    /**
     * Saves all of the server properties to the properties file.
     */
    public void saveProperties()
    {
        this.settings.saveProperties();
    }

    /**
     * Returns the filename where server properties are stored
     */
    public String getSettingsFilename()
    {
        File file1 = this.settings.getPropertiesFile();
        return file1 != null ? file1.getAbsolutePath() : "No settings file";
    }

    public void setGuiEnabled()
    {
        MinecraftServerGui.createServerGui(this);
        this.guiIsEnabled = true;
    }

    public boolean getGuiEnabled()
    {
        return this.guiIsEnabled;
    }

    /**
     * On dedicated does nothing. On integrated, sets commandsAllowedForAll, gameType and allows external connections.
     */
    public String shareToLAN(WorldSettings.GameType p_71206_1_, boolean p_71206_2_)
    {
        return "";
    }

    /**
     * Return whether command blocks are enabled.
     */
    public boolean isCommandBlockEnabled()
    {
        return this.settings.getBooleanProperty("enable-command-block", false);
    }

    /**
     * Return the spawn protection area's size.
     */
    public int getSpawnProtectionSize()
    {
        return this.settings.getIntProperty("spawn-protection", super.getSpawnProtectionSize());
    }

    /**
     * Returns true if a player does not have permission to edit the block at the given coordinates.
     */
    public boolean isBlockProtected(World p_96290_1_, int p_96290_2_, int p_96290_3_, int p_96290_4_, EntityPlayer p_96290_5_)
    {
        if (p_96290_1_.provider.dimensionId != 0)
        {
            return false;
        }
        else if (this.getConfigurationManager().func_152603_m().func_152690_d())
        {
            return false;
        }
        else if (this.getConfigurationManager().func_152596_g(p_96290_5_.getGameProfile()))
        {
            return false;
        }
        else if (this.getSpawnProtectionSize() <= 0)
        {
            return false;
        }
        else
        {
            ChunkCoordinates chunkcoordinates = p_96290_1_.getSpawnPoint();
            int l = MathHelper.abs_int(p_96290_2_ - chunkcoordinates.posX);
            int i1 = MathHelper.abs_int(p_96290_4_ - chunkcoordinates.posZ);
            int j1 = Math.max(l, i1);
            return j1 <= this.getSpawnProtectionSize();
        }
    }

    public int getOpPermissionLevel()
    {
        return this.settings.getIntProperty("op-permission-level", 4);
    }

    public void func_143006_e(int p_143006_1_)
    {
        super.func_143006_e(p_143006_1_);
        this.settings.setProperty("player-idle-timeout", Integer.valueOf(p_143006_1_));
        this.saveProperties();
    }

    public boolean func_152363_m()
    {
        return this.settings.getBooleanProperty("broadcast-rcon-to-ops", true);
    }

    public boolean func_147136_ar()
    {
        return this.settings.getBooleanProperty("announce-player-achievements", true);
    }

    protected boolean func_152368_aE() throws IOException
    {
        boolean flag = false;
        int i;

        for (i = 0; !flag && i <= 2; ++i)
        {
            if (i > 0)
            {
                field_155771_h.warn("Encountered a problem while converting the user banlist, retrying in a few seconds");
                this.func_152369_aG();
            }

            flag = PreYggdrasilConverter.func_152724_a(this);
        }

        boolean flag1 = false;

        for (i = 0; !flag1 && i <= 2; ++i)
        {
            if (i > 0)
            {
                field_155771_h.warn("Encountered a problem while converting the ip banlist, retrying in a few seconds");
                this.func_152369_aG();
            }

            flag1 = PreYggdrasilConverter.func_152722_b(this);
        }

        boolean flag2 = false;

        for (i = 0; !flag2 && i <= 2; ++i)
        {
            if (i > 0)
            {
                field_155771_h.warn("Encountered a problem while converting the op list, retrying in a few seconds");
                this.func_152369_aG();
            }

            flag2 = PreYggdrasilConverter.func_152718_c(this);
        }

        boolean flag3 = false;

        for (i = 0; !flag3 && i <= 2; ++i)
        {
            if (i > 0)
            {
                field_155771_h.warn("Encountered a problem while converting the whitelist, retrying in a few seconds");
                this.func_152369_aG();
            }

            flag3 = PreYggdrasilConverter.func_152710_d(this);
        }

        boolean flag4 = false;

        for (i = 0; !flag4 && i <= 2; ++i)
        {
            if (i > 0)
            {
                field_155771_h.warn("Encountered a problem while converting the player save files, retrying in a few seconds");
                this.func_152369_aG();
            }

            flag4 = PreYggdrasilConverter.func_152723_a(this, this.settings);
        }

        return flag || flag1 || flag2 || flag3 || flag4;
    }

    private void func_152369_aG()
    {
        try
        {
            Thread.sleep(5000L);
        }
        catch (InterruptedException interruptedexception)
        {
            ;
        }
    }
}