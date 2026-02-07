package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.util.concurrent.GenericFutureListener;
import java.io.IOException;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.ServerStatusResponse;
import net.minecraft.network.handshake.client.C00Handshake;
import net.minecraft.network.status.INetHandlerStatusClient;
import net.minecraft.network.status.client.C00PacketServerQuery;
import net.minecraft.network.status.client.C01PacketPing;
import net.minecraft.network.status.server.S00PacketServerInfo;
import net.minecraft.network.status.server.S01PacketPong;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.IChatComponent;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class RealmsServerStatusPinger
{
    private static final Logger LOGGER = LogManager.getLogger();
    private final List connections = Collections.synchronizedList(new ArrayList());
    private static final String __OBFID = "CL_00001854";

    public void pingServer(final String p_pingServer_1_, final ServerPing p_pingServer_2_) throws IOException
    {
        if (p_pingServer_1_ != null && !p_pingServer_1_.startsWith("0.0.0.0") && !p_pingServer_1_.isEmpty())
        {
            RealmsServerAddress realmsserveraddress = RealmsServerAddress.parseString(p_pingServer_1_);
            final NetworkManager networkmanager = NetworkManager.provideLanClient(InetAddress.getByName(realmsserveraddress.getHost()), realmsserveraddress.getPort());
            this.connections.add(networkmanager);
            networkmanager.setNetHandler(new INetHandlerStatusClient()
            {
                private boolean field_154345_e = false;
                private static final String __OBFID = "CL_00001807";
                public void handleServerInfo(S00PacketServerInfo p_147397_1_)
                {
                    ServerStatusResponse serverstatusresponse = p_147397_1_.func_149294_c();

                    if (serverstatusresponse.func_151318_b() != null)
                    {
                        p_pingServer_2_.nrOfPlayers = String.valueOf(serverstatusresponse.func_151318_b().func_151333_b());
                    }

                    networkmanager.scheduleOutboundPacket(new C01PacketPing(Realms.currentTimeMillis()), new GenericFutureListener[0]);
                    this.field_154345_e = true;
                }
                public void handlePong(S01PacketPong p_147398_1_)
                {
                    networkmanager.closeChannel(new ChatComponentText("Finished"));
                }
                /**
                 * Invoked when disconnecting, the parameter is a ChatComponent describing the reason for termination
                 */
                public void onDisconnect(IChatComponent p_147231_1_)
                {
                    if (!this.field_154345_e)
                    {
                        RealmsServerStatusPinger.LOGGER.error("Can\'t ping " + p_pingServer_1_ + ": " + p_147231_1_.getUnformattedText());
                    }
                }
                /**
                 * Allows validation of the connection state transition. Parameters: from, to (connection state).
                 * Typically throws IllegalStateException or UnsupportedOperationException if validation fails
                 */
                public void onConnectionStateTransition(EnumConnectionState p_147232_1_, EnumConnectionState p_147232_2_)
                {
                    if (p_147232_2_ != EnumConnectionState.STATUS)
                    {
                        throw new UnsupportedOperationException("Unexpected change in protocol to " + p_147232_2_);
                    }
                }
                /**
                 * For scheduled network tasks. Used in NetHandlerPlayServer to send keep-alive packets and in
                 * NetHandlerLoginServer for a login-timeout
                 */
                public void onNetworkTick() {}
            });

            try
            {
                networkmanager.scheduleOutboundPacket(new C00Handshake(RealmsSharedConstants.NETWORK_PROTOCOL_VERSION, realmsserveraddress.getHost(), realmsserveraddress.getPort(), EnumConnectionState.STATUS), new GenericFutureListener[0]);
                networkmanager.scheduleOutboundPacket(new C00PacketServerQuery(), new GenericFutureListener[0]);
            }
            catch (Throwable throwable)
            {
                LOGGER.error(throwable);
            }
        }
    }

    public void tick()
    {
        List list = this.connections;

        synchronized (this.connections)
        {
            Iterator iterator = this.connections.iterator();

            while (iterator.hasNext())
            {
                NetworkManager networkmanager = (NetworkManager)iterator.next();

                if (networkmanager.isChannelOpen())
                {
                    networkmanager.processReceivedPackets();
                }
                else
                {
                    iterator.remove();

                    if (networkmanager.getExitMessage() != null)
                    {
                        networkmanager.getNetHandler().onDisconnect(networkmanager.getExitMessage());
                    }
                }
            }
        }
    }

    public void removeAll()
    {
        List list = this.connections;

        synchronized (this.connections)
        {
            Iterator iterator = this.connections.iterator();

            while (iterator.hasNext())
            {
                NetworkManager networkmanager = (NetworkManager)iterator.next();

                if (networkmanager.isChannelOpen())
                {
                    iterator.remove();
                    networkmanager.closeChannel(new ChatComponentText("Cancelled"));
                }
            }
        }
    }
}