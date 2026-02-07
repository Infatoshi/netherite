package net.minecraft.network.rcon;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;

@SideOnly(Side.SERVER)
public class RConThreadMain extends RConThreadBase
{
    /** Port RCon is running on */
    private int rconPort;
    /** Port the server is running on */
    private int serverPort;
    /** Hostname RCon is running on */
    private String hostname;
    /** The RCon ServerSocket. */
    private ServerSocket serverSocket;
    /** The RCon password */
    private String rconPassword;
    /** A map of client addresses to their running Threads */
    private Map clientThreads;
    private static final String __OBFID = "CL_00001805";

    public RConThreadMain(IServer p_i1538_1_)
    {
        super(p_i1538_1_, "RCON Listener");
        this.rconPort = p_i1538_1_.getIntProperty("rcon.port", 0);
        this.rconPassword = p_i1538_1_.getStringProperty("rcon.password", "");
        this.hostname = p_i1538_1_.getHostname();
        this.serverPort = p_i1538_1_.getPort();

        if (0 == this.rconPort)
        {
            this.rconPort = this.serverPort + 10;
            this.logInfo("Setting default rcon port to " + this.rconPort);
            p_i1538_1_.setProperty("rcon.port", Integer.valueOf(this.rconPort));

            if (0 == this.rconPassword.length())
            {
                p_i1538_1_.setProperty("rcon.password", "");
            }

            p_i1538_1_.saveProperties();
        }

        if (0 == this.hostname.length())
        {
            this.hostname = "0.0.0.0";
        }

        this.initClientThreadList();
        this.serverSocket = null;
    }

    private void initClientThreadList()
    {
        this.clientThreads = new HashMap();
    }

    /**
     * Cleans up the clientThreads map by removing client Threads that are not running
     */
    private void cleanClientThreadsMap()
    {
        Iterator iterator = this.clientThreads.entrySet().iterator();

        while (iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();

            if (!((RConThreadClient)entry.getValue()).isRunning())
            {
                iterator.remove();
            }
        }
    }

    public void run()
    {
        this.logInfo("RCON running on " + this.hostname + ":" + this.rconPort);

        try
        {
            while (this.running)
            {
                try
                {
                    Socket socket = this.serverSocket.accept();
                    socket.setSoTimeout(500);
                    RConThreadClient rconthreadclient = new RConThreadClient(this.server, socket);
                    rconthreadclient.startThread();
                    this.clientThreads.put(socket.getRemoteSocketAddress(), rconthreadclient);
                    this.cleanClientThreadsMap();
                }
                catch (SocketTimeoutException sockettimeoutexception)
                {
                    this.cleanClientThreadsMap();
                }
                catch (IOException ioexception)
                {
                    if (this.running)
                    {
                        this.logInfo("IO: " + ioexception.getMessage());
                    }
                }
            }
        }
        finally
        {
            this.closeServerSocket(this.serverSocket);
        }
    }

    /**
     * Creates a new Thread object from this class and starts running
     */
    public void startThread()
    {
        if (0 == this.rconPassword.length())
        {
            this.logWarning("No rcon password set in \'" + this.server.getSettingsFilename() + "\', rcon disabled!");
        }
        else if (0 < this.rconPort && 65535 >= this.rconPort)
        {
            if (!this.running)
            {
                try
                {
                    this.serverSocket = new ServerSocket(this.rconPort, 0, InetAddress.getByName(this.hostname));
                    this.serverSocket.setSoTimeout(500);
                    super.startThread();
                }
                catch (IOException ioexception)
                {
                    this.logWarning("Unable to initialise rcon on " + this.hostname + ":" + this.rconPort + " : " + ioexception.getMessage());
                }
            }
        }
        else
        {
            this.logWarning("Invalid rcon port " + this.rconPort + " found in \'" + this.server.getSettingsFilename() + "\', rcon disabled!");
        }
    }
}