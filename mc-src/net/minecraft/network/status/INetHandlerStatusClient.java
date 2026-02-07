package net.minecraft.network.status;

import net.minecraft.network.INetHandler;
import net.minecraft.network.status.server.S00PacketServerInfo;
import net.minecraft.network.status.server.S01PacketPong;

public interface INetHandlerStatusClient extends INetHandler
{
    void handleServerInfo(S00PacketServerInfo p_147397_1_);

    void handlePong(S01PacketPong p_147398_1_);
}