package net.minecraft.network.status;

import net.minecraft.network.INetHandler;
import net.minecraft.network.status.client.C00PacketServerQuery;
import net.minecraft.network.status.client.C01PacketPing;

public interface INetHandlerStatusServer extends INetHandler
{
    void processPing(C01PacketPing p_147311_1_);

    void processServerQuery(C00PacketServerQuery p_147312_1_);
}