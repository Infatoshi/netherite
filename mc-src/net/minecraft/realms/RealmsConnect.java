package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.util.concurrent.GenericFutureListener;
import java.net.InetAddress;
import java.net.UnknownHostException;
import net.minecraft.client.Minecraft;
import net.minecraft.client.network.NetHandlerLoginClient;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.handshake.client.C00Handshake;
import net.minecraft.network.login.client.C00PacketLoginStart;
import net.minecraft.util.ChatComponentTranslation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class RealmsConnect
{
    private static final Logger LOGGER = LogManager.getLogger();
    private final RealmsScreen onlineScreen;
    private volatile boolean aborted = false;
    private NetworkManager connection;
    private static final String __OBFID = "CL_00001844";

    public RealmsConnect(RealmsScreen p_i1079_1_)
    {
        this.onlineScreen = p_i1079_1_;
    }

    public void connect(final String p_connect_1_, final int p_connect_2_)
    {
        (new Thread("Realms-connect-task")
        {
            private static final String __OBFID = "CL_00001808";
            public void run()
            {
                InetAddress inetaddress = null;

                try
                {
                    cpw.mods.fml.client.FMLClientHandler.instance().connectToRealmsServer(p_connect_1_, p_connect_2_);
                    inetaddress = InetAddress.getByName(p_connect_1_);

                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.this.connection = NetworkManager.provideLanClient(inetaddress, p_connect_2_);

                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.this.connection.setNetHandler(new NetHandlerLoginClient(RealmsConnect.this.connection, Minecraft.getMinecraft(), RealmsConnect.this.onlineScreen.getProxy()));

                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.this.connection.scheduleOutboundPacket(new C00Handshake(5, p_connect_1_, p_connect_2_, EnumConnectionState.LOGIN), new GenericFutureListener[0]);

                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.this.connection.scheduleOutboundPacket(new C00PacketLoginStart(Minecraft.getMinecraft().getSession().func_148256_e()), new GenericFutureListener[0]);
                }
                catch (UnknownHostException unknownhostexception)
                {
                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.LOGGER.error("Couldn\'t connect to world", unknownhostexception);
                    Realms.setScreen(new DisconnectedOnlineScreen(RealmsConnect.this.onlineScreen, "connect.failed", new ChatComponentTranslation("disconnect.genericReason", new Object[] {"Unknown host \'" + p_connect_1_ + "\'"})));
                }
                catch (Exception exception)
                {
                    if (RealmsConnect.this.aborted)
                    {
                        return;
                    }

                    RealmsConnect.LOGGER.error("Couldn\'t connect to world", exception);
                    String s = exception.toString();

                    if (inetaddress != null)
                    {
                        String s1 = inetaddress.toString() + ":" + p_connect_2_;
                        s = s.replaceAll(s1, "");
                    }

                    Realms.setScreen(new DisconnectedOnlineScreen(RealmsConnect.this.onlineScreen, "connect.failed", new ChatComponentTranslation("disconnect.genericReason", new Object[] {s})));
                }
            }
        }).start();
    }

    public void abort()
    {
        this.aborted = true;
    }

    public void tick()
    {
        if (this.connection != null)
        {
            if (this.connection.isChannelOpen())
            {
                this.connection.processReceivedPackets();
            }
            else if (this.connection.getExitMessage() != null)
            {
                this.connection.getNetHandler().onDisconnect(this.connection.getExitMessage());
            }
        }
    }
}