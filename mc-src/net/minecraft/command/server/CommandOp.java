package net.minecraft.command.server;

import com.mojang.authlib.GameProfile;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.CommandException;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.server.MinecraftServer;

public class CommandOp extends CommandBase
{
    private static final String __OBFID = "CL_00000694";

    public String getCommandName()
    {
        return "op";
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
        return "commands.op.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length == 1 && p_71515_2_[0].length() > 0)
        {
            MinecraftServer minecraftserver = MinecraftServer.getServer();
            GameProfile gameprofile = minecraftserver.func_152358_ax().func_152655_a(p_71515_2_[0]);

            if (gameprofile == null)
            {
                throw new CommandException("commands.op.failed", new Object[] {p_71515_2_[0]});
            }
            else
            {
                minecraftserver.getConfigurationManager().func_152605_a(gameprofile);
                func_152373_a(p_71515_1_, this, "commands.op.success", new Object[] {p_71515_2_[0]});
            }
        }
        else
        {
            throw new WrongUsageException("commands.op.usage", new Object[0]);
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        if (p_71516_2_.length == 1)
        {
            String s = p_71516_2_[p_71516_2_.length - 1];
            ArrayList arraylist = new ArrayList();
            GameProfile[] agameprofile = MinecraftServer.getServer().func_152357_F();
            int i = agameprofile.length;

            for (int j = 0; j < i; ++j)
            {
                GameProfile gameprofile = agameprofile[j];

                if (!MinecraftServer.getServer().getConfigurationManager().func_152596_g(gameprofile) && doesStringStartWith(s, gameprofile.getName()))
                {
                    arraylist.add(gameprofile.getName());
                }
            }

            return arraylist;
        }
        else
        {
            return null;
        }
    }
}