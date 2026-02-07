package net.minecraft.command.server;

import com.mojang.authlib.GameProfile;
import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.CommandException;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.server.MinecraftServer;

public class CommandDeOp extends CommandBase
{
    private static final String __OBFID = "CL_00000244";

    public String getCommandName()
    {
        return "deop";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 3;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.deop.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length == 1 && p_71515_2_[0].length() > 0)
        {
            MinecraftServer minecraftserver = MinecraftServer.getServer();
            GameProfile gameprofile = minecraftserver.getConfigurationManager().func_152603_m().func_152700_a(p_71515_2_[0]);

            if (gameprofile == null)
            {
                throw new CommandException("commands.deop.failed", new Object[] {p_71515_2_[0]});
            }
            else
            {
                minecraftserver.getConfigurationManager().func_152610_b(gameprofile);
                func_152373_a(p_71515_1_, this, "commands.deop.success", new Object[] {p_71515_2_[0]});
            }
        }
        else
        {
            throw new WrongUsageException("commands.deop.usage", new Object[0]);
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, MinecraftServer.getServer().getConfigurationManager().func_152606_n()) : null;
    }
}