package net.minecraft.command.server;

import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.NetworkStatistics;
import net.minecraft.util.ChatComponentText;

public class CommandNetstat extends CommandBase
{
    private static final String __OBFID = "CL_00001904";

    public String getCommandName()
    {
        return "netstat";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 0;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.players.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_1_ instanceof EntityPlayer)
        {
            p_71515_1_.addChatMessage(new ChatComponentText("Command is not available for players"));
        }
        else
        {
            if (p_71515_2_.length > 0 && p_71515_2_[0].length() > 1)
            {
                if ("hottest-read".equals(p_71515_2_[0]))
                {
                    p_71515_1_.addChatMessage(new ChatComponentText(NetworkManager.field_152462_h.func_152477_e().toString()));
                }
                else if ("hottest-write".equals(p_71515_2_[0]))
                {
                    p_71515_1_.addChatMessage(new ChatComponentText(NetworkManager.field_152462_h.func_152475_g().toString()));
                }
                else if ("most-read".equals(p_71515_2_[0]))
                {
                    p_71515_1_.addChatMessage(new ChatComponentText(NetworkManager.field_152462_h.func_152467_f().toString()));
                }
                else if ("most-write".equals(p_71515_2_[0]))
                {
                    p_71515_1_.addChatMessage(new ChatComponentText(NetworkManager.field_152462_h.func_152470_h().toString()));
                }
                else
                {
                    NetworkStatistics.PacketStat packetstat;
                    int i;

                    if ("packet-read".equals(p_71515_2_[0]))
                    {
                        if (p_71515_2_.length > 1 && p_71515_2_[1].length() > 0)
                        {
                            try
                            {
                                i = Integer.parseInt(p_71515_2_[1].trim());
                                packetstat = NetworkManager.field_152462_h.func_152466_a(i);
                                this.func_152375_a(p_71515_1_, i, packetstat);
                            }
                            catch (Exception exception1)
                            {
                                p_71515_1_.addChatMessage(new ChatComponentText("Packet " + p_71515_2_[1] + " not found!"));
                            }
                        }
                        else
                        {
                            p_71515_1_.addChatMessage(new ChatComponentText("Packet id is missing"));
                        }
                    }
                    else if ("packet-write".equals(p_71515_2_[0]))
                    {
                        if (p_71515_2_.length > 1 && p_71515_2_[1].length() > 0)
                        {
                            try
                            {
                                i = Integer.parseInt(p_71515_2_[1].trim());
                                packetstat = NetworkManager.field_152462_h.func_152468_b(i);
                                this.func_152375_a(p_71515_1_, i, packetstat);
                            }
                            catch (Exception exception)
                            {
                                p_71515_1_.addChatMessage(new ChatComponentText("Packet " + p_71515_2_[1] + " not found!"));
                            }
                        }
                        else
                        {
                            p_71515_1_.addChatMessage(new ChatComponentText("Packet id is missing"));
                        }
                    }
                    else if ("read-count".equals(p_71515_2_[0]))
                    {
                        p_71515_1_.addChatMessage(new ChatComponentText("total-read-count" + String.valueOf(NetworkManager.field_152462_h.func_152472_c())));
                    }
                    else if ("write-count".equals(p_71515_2_[0]))
                    {
                        p_71515_1_.addChatMessage(new ChatComponentText("total-write-count" + String.valueOf(NetworkManager.field_152462_h.func_152473_d())));
                    }
                    else
                    {
                        p_71515_1_.addChatMessage(new ChatComponentText("Unrecognized: " + p_71515_2_[0]));
                    }
                }
            }
            else
            {
                String s = "reads: " + NetworkManager.field_152462_h.func_152465_a();
                s = s + ", writes: " + NetworkManager.field_152462_h.func_152471_b();
                p_71515_1_.addChatMessage(new ChatComponentText(s));
            }
        }
    }

    private void func_152375_a(ICommandSender p_152375_1_, int p_152375_2_, NetworkStatistics.PacketStat p_152375_3_)
    {
        if (p_152375_3_ != null)
        {
            p_152375_1_.addChatMessage(new ChatComponentText(p_152375_3_.toString()));
        }
        else
        {
            p_152375_1_.addChatMessage(new ChatComponentText("Packet " + p_152375_2_ + " not found!"));
        }
    }
}