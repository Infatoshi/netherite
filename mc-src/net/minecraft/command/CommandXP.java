package net.minecraft.command;

import java.util.List;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.MinecraftServer;

public class CommandXP extends CommandBase
{
    private static final String __OBFID = "CL_00000398";

    public String getCommandName()
    {
        return "xp";
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
        return "commands.xp.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length <= 0)
        {
            throw new WrongUsageException("commands.xp.usage", new Object[0]);
        }
        else
        {
            String s = p_71515_2_[0];
            boolean flag = s.endsWith("l") || s.endsWith("L");

            if (flag && s.length() > 1)
            {
                s = s.substring(0, s.length() - 1);
            }

            int i = parseInt(p_71515_1_, s);
            boolean flag1 = i < 0;

            if (flag1)
            {
                i *= -1;
            }

            EntityPlayerMP entityplayermp;

            if (p_71515_2_.length > 1)
            {
                entityplayermp = getPlayer(p_71515_1_, p_71515_2_[1]);
            }
            else
            {
                entityplayermp = getCommandSenderAsPlayer(p_71515_1_);
            }

            if (flag)
            {
                if (flag1)
                {
                    entityplayermp.addExperienceLevel(-i);
                    func_152373_a(p_71515_1_, this, "commands.xp.success.negative.levels", new Object[] {Integer.valueOf(i), entityplayermp.getCommandSenderName()});
                }
                else
                {
                    entityplayermp.addExperienceLevel(i);
                    func_152373_a(p_71515_1_, this, "commands.xp.success.levels", new Object[] {Integer.valueOf(i), entityplayermp.getCommandSenderName()});
                }
            }
            else
            {
                if (flag1)
                {
                    throw new WrongUsageException("commands.xp.failure.widthdrawXp", new Object[0]);
                }

                entityplayermp.addExperience(i);
                func_152373_a(p_71515_1_, this, "commands.xp.success", new Object[] {Integer.valueOf(i), entityplayermp.getCommandSenderName()});
            }
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 2 ? getListOfStringsMatchingLastWord(p_71516_2_, this.getAllUsernames()) : null;
    }

    protected String[] getAllUsernames()
    {
        return MinecraftServer.getServer().getAllUsernames();
    }

    /**
     * Return whether the specified command parameter index is a username parameter.
     */
    public boolean isUsernameIndex(String[] p_82358_1_, int p_82358_2_)
    {
        return p_82358_2_ == 1;
    }
}