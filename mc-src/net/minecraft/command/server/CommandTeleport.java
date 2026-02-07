package net.minecraft.command.server;

import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.PlayerNotFoundException;
import net.minecraft.command.WrongUsageException;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.MinecraftServer;

public class CommandTeleport extends CommandBase
{
    private static final String __OBFID = "CL_00001180";

    public String getCommandName()
    {
        return "tp";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 2;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.tp.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length < 1)
        {
            throw new WrongUsageException("commands.tp.usage", new Object[0]);
        }
        else
        {
            EntityPlayerMP entityplayermp;

            if (p_71515_2_.length != 2 && p_71515_2_.length != 4)
            {
                entityplayermp = getCommandSenderAsPlayer(p_71515_1_);
            }
            else
            {
                entityplayermp = getPlayer(p_71515_1_, p_71515_2_[0]);

                if (entityplayermp == null)
                {
                    throw new PlayerNotFoundException();
                }
            }

            if (p_71515_2_.length != 3 && p_71515_2_.length != 4)
            {
                if (p_71515_2_.length == 1 || p_71515_2_.length == 2)
                {
                    EntityPlayerMP entityplayermp1 = getPlayer(p_71515_1_, p_71515_2_[p_71515_2_.length - 1]);

                    if (entityplayermp1 == null)
                    {
                        throw new PlayerNotFoundException();
                    }

                    if (entityplayermp1.worldObj != entityplayermp.worldObj)
                    {
                        func_152373_a(p_71515_1_, this, "commands.tp.notSameDimension", new Object[0]);
                        return;
                    }

                    entityplayermp.mountEntity((Entity)null);
                    entityplayermp.playerNetServerHandler.setPlayerLocation(entityplayermp1.posX, entityplayermp1.posY, entityplayermp1.posZ, entityplayermp1.rotationYaw, entityplayermp1.rotationPitch);
                    func_152373_a(p_71515_1_, this, "commands.tp.success", new Object[] {entityplayermp.getCommandSenderName(), entityplayermp1.getCommandSenderName()});
                }
            }
            else if (entityplayermp.worldObj != null)
            {
                int i = p_71515_2_.length - 3;
                double d0 = func_110666_a(p_71515_1_, entityplayermp.posX, p_71515_2_[i++]);
                double d1 = func_110665_a(p_71515_1_, entityplayermp.posY, p_71515_2_[i++], 0, 0);
                double d2 = func_110666_a(p_71515_1_, entityplayermp.posZ, p_71515_2_[i++]);
                entityplayermp.mountEntity((Entity)null);
                entityplayermp.setPositionAndUpdate(d0, d1, d2);
                func_152373_a(p_71515_1_, this, "commands.tp.success.coordinates", new Object[] {entityplayermp.getCommandSenderName(), Double.valueOf(d0), Double.valueOf(d1), Double.valueOf(d2)});
            }
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length != 1 && p_71516_2_.length != 2 ? null : getListOfStringsMatchingLastWord(p_71516_2_, MinecraftServer.getServer().getAllUsernames());
    }

    /**
     * Return whether the specified command parameter index is a username parameter.
     */
    public boolean isUsernameIndex(String[] p_82358_1_, int p_82358_2_)
    {
        return p_82358_2_ == 0;
    }
}