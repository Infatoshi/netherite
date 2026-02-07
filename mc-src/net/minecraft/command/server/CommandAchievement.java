package net.minecraft.command.server;

import com.google.common.collect.Lists;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.CommandException;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.MinecraftServer;
import net.minecraft.stats.Achievement;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatList;

public class CommandAchievement extends CommandBase
{
    private static final String __OBFID = "CL_00000113";

    public String getCommandName()
    {
        return "achievement";
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
        return "commands.achievement.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length >= 2)
        {
            StatBase statbase = StatList.func_151177_a(p_71515_2_[1]);

            if (statbase == null && !p_71515_2_[1].equals("*"))
            {
                throw new CommandException("commands.achievement.unknownAchievement", new Object[] {p_71515_2_[1]});
            }

            EntityPlayerMP entityplayermp;

            if (p_71515_2_.length >= 3)
            {
                entityplayermp = getPlayer(p_71515_1_, p_71515_2_[2]);
            }
            else
            {
                entityplayermp = getCommandSenderAsPlayer(p_71515_1_);
            }

            if (p_71515_2_[0].equalsIgnoreCase("give"))
            {
                if (statbase == null)
                {
                    Iterator iterator = AchievementList.achievementList.iterator();

                    while (iterator.hasNext())
                    {
                        Achievement achievement = (Achievement)iterator.next();
                        entityplayermp.triggerAchievement(achievement);
                    }

                    func_152373_a(p_71515_1_, this, "commands.achievement.give.success.all", new Object[] {entityplayermp.getCommandSenderName()});
                }
                else
                {
                    if (statbase instanceof Achievement)
                    {
                        Achievement achievement2 = (Achievement)statbase;
                        ArrayList arraylist;

                        for (arraylist = Lists.newArrayList(); achievement2.parentAchievement != null && !entityplayermp.func_147099_x().hasAchievementUnlocked(achievement2.parentAchievement); achievement2 = achievement2.parentAchievement)
                        {
                            arraylist.add(achievement2.parentAchievement);
                        }

                        Iterator iterator1 = Lists.reverse(arraylist).iterator();

                        while (iterator1.hasNext())
                        {
                            Achievement achievement1 = (Achievement)iterator1.next();
                            entityplayermp.triggerAchievement(achievement1);
                        }
                    }

                    entityplayermp.triggerAchievement(statbase);
                    func_152373_a(p_71515_1_, this, "commands.achievement.give.success.one", new Object[] {entityplayermp.getCommandSenderName(), statbase.func_150955_j()});
                }

                return;
            }
        }

        throw new WrongUsageException("commands.achievement.usage", new Object[0]);
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        if (p_71516_2_.length == 1)
        {
            /**
             * Returns a List of strings (chosen from the given strings) which the last word in the given string array
             * is a beginning-match for. (Tab completion).
             */
            return getListOfStringsMatchingLastWord(p_71516_2_, new String[] {"give"});
        }
        else if (p_71516_2_.length != 2)
        {
            return p_71516_2_.length == 3 ? getListOfStringsMatchingLastWord(p_71516_2_, MinecraftServer.getServer().getAllUsernames()) : null;
        }
        else
        {
            ArrayList arraylist = Lists.newArrayList();
            Iterator iterator = StatList.allStats.iterator();

            while (iterator.hasNext())
            {
                StatBase statbase = (StatBase)iterator.next();
                arraylist.add(statbase.statId);
            }

            /**
             * Returns a List of strings (chosen from the given string iterable) which the last word in the given string
             * array is a beginning-match for. (Tab completion).
             */
            return getListOfStringsFromIterableMatchingLastWord(p_71516_2_, arraylist);
        }
    }

    /**
     * Return whether the specified command parameter index is a username parameter.
     */
    public boolean isUsernameIndex(String[] p_82358_1_, int p_82358_2_)
    {
        return p_82358_2_ == 2;
    }
}