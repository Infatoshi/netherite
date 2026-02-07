package net.minecraft.client.network;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.MulticastSocket;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ThreadLanServerPing;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class LanServerDetector
{
    private static final AtomicInteger field_148551_a = new AtomicInteger(0);
    private static final Logger logger = LogManager.getLogger();
    private static final String __OBFID = "CL_00001133";

    @SideOnly(Side.CLIENT)
    public static class LanServer
        {
            private String lanServerMotd;
            private String lanServerIpPort;
            /** Last time this LanServer was seen. */
            private long timeLastSeen;
            private static final String __OBFID = "CL_00001134";

            public LanServer(String p_i1319_1_, String p_i1319_2_)
            {
                this.lanServerMotd = p_i1319_1_;
                this.lanServerIpPort = p_i1319_2_;
                this.timeLastSeen = Minecraft.getSystemTime();
            }

            public String getServerMotd()
            {
                return this.lanServerMotd;
            }

            public String getServerIpPort()
            {
                return this.lanServerIpPort;
            }

            /**
             * Updates the time this LanServer was last seen.
             */
            public void updateLastSeen()
            {
                this.timeLastSeen = Minecraft.getSystemTime();
            }
        }

    @SideOnly(Side.CLIENT)
    public static class LanServerList
        {
            private ArrayList listOfLanServers = new ArrayList();
            boolean wasUpdated;
            private static final String __OBFID = "CL_00001136";

            public synchronized boolean getWasUpdated()
            {
                return this.wasUpdated;
            }

            public synchronized void setWasNotUpdated()
            {
                this.wasUpdated = false;
            }

            public synchronized List getLanServers()
            {
                return Collections.unmodifiableList(this.listOfLanServers);
            }

            public synchronized void func_77551_a(String p_77551_1_, InetAddress p_77551_2_)
            {
                String s1 = ThreadLanServerPing.getMotdFromPingResponse(p_77551_1_);
                String s2 = ThreadLanServerPing.getAdFromPingResponse(p_77551_1_);

                if (s2 != null)
                {
                    s2 = p_77551_2_.getHostAddress() + ":" + s2;
                    boolean flag = false;
                    Iterator iterator = this.listOfLanServers.iterator();

                    while (iterator.hasNext())
                    {
                        LanServerDetector.LanServer lanserver = (LanServerDetector.LanServer)iterator.next();

                        if (lanserver.getServerIpPort().equals(s2))
                        {
                            lanserver.updateLastSeen();
                            flag = true;
                            break;
                        }
                    }

                    if (!flag)
                    {
                        this.listOfLanServers.add(new LanServerDetector.LanServer(s1, s2));
                        this.wasUpdated = true;
                    }
                }
            }
        }

    @SideOnly(Side.CLIENT)
    public static class ThreadLanServerFind extends Thread
        {
            /** The LanServerList */
            private final LanServerDetector.LanServerList localServerList;
            /** InetAddress for 224.0.2.60 */
            private final InetAddress broadcastAddress;
            /** The socket we're using to receive packets on. */
            private final MulticastSocket socket;
            private static final String __OBFID = "CL_00001135";

            public ThreadLanServerFind(LanServerDetector.LanServerList p_i1320_1_) throws IOException
            {
                super("LanServerDetector #" + LanServerDetector.field_148551_a.incrementAndGet());
                this.localServerList = p_i1320_1_;
                this.setDaemon(true);
                this.socket = new MulticastSocket(4445);
                this.broadcastAddress = InetAddress.getByName("224.0.2.60");
                this.socket.setSoTimeout(5000);
                this.socket.joinGroup(this.broadcastAddress);
            }

            public void run()
            {
                byte[] abyte = new byte[1024];

                while (!this.isInterrupted())
                {
                    DatagramPacket datagrampacket = new DatagramPacket(abyte, abyte.length);

                    try
                    {
                        this.socket.receive(datagrampacket);
                    }
                    catch (SocketTimeoutException sockettimeoutexception)
                    {
                        continue;
                    }
                    catch (IOException ioexception1)
                    {
                        LanServerDetector.logger.error("Couldn\'t ping server", ioexception1);
                        break;
                    }

                    String s = new String(datagrampacket.getData(), datagrampacket.getOffset(), datagrampacket.getLength());
                    LanServerDetector.logger.debug(datagrampacket.getAddress() + ": " + s);
                    this.localServerList.func_77551_a(s, datagrampacket.getAddress());
                }

                try
                {
                    this.socket.leaveGroup(this.broadcastAddress);
                }
                catch (IOException ioexception)
                {
                    ;
                }

                this.socket.close();
            }
        }
}