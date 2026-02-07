package net.minecraft.command;

import java.util.List;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.world.WorldSettings;

public class CommandGameMode extends CommandBase
{
    private static final String __OBFID = "CL_00000448";

    public String getCommandName()
    {
        return "gamemode";
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
        return "commands.gamemode.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length > 0)
        {
            WorldSettings.GameType gametype = this.getGameModeFromCommand(p_71515_1_, p_71515_2_[0]);
            EntityPlayerMP entityplayermp = p_71515_2_.length >= 2 ? getPlayer(p_71515_1_, p_71515_2_[1]) : getCommandSenderAsPlayer(p_71515_1_);
            entityplayermp.setGameType(gametype);
            entityplayermp.fallDistance = 0.0F;
            ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation("gameMode." + gametype.getName(), new Object[0]);

            if (entityplayermp != p_71515_1_)
            {
                func_152374_a(p_71515_1_, this, 1, "commands.gamemode.success.other", new Object[] {entityplayermp.getCommandSenderName(), chatcomponenttranslation});
            }
            else
            {
                func_152374_a(p_71515_1_, this, 1, "commands.gamemode.success.self", new Object[] {chatcomponenttranslation});
            }
        }
        else
        {
            throw new WrongUsageException("commands.gamemode.usage", new Object[0]);
        }
    }

    /**
     * Gets the Game Mode specified in the command.
     */
    protected WorldSettings.GameType getGameModeFromCommand(ICommandSender p_71539_1_, String p_71539_2_)
    {
        return !p_71539_2_.equalsIgnoreCase(WorldSettings.GameType.SURVIVAL.getName()) && !p_71539_2_.equalsIgnoreCase("s") ? (!p_71539_2_.equalsIgnoreCase(WorldSettings.GameType.CREATIVE.getName()) && !p_71539_2_.equalsIgnoreCase("c") ? (!p_71539_2_.equalsIgnoreCase(WorldSettings.GameType.ADVENTURE.getName()) && !p_71539_2_.equalsIgnoreCase("a") ? WorldSettings.getGameTypeById(parseIntBounded(p_71539_1_, p_71539_2_, 0, WorldSettings.GameType.values().length - 2)) : WorldSettings.GameType.ADVENTURE) : WorldSettings.GameType.CREATIVE) : WorldSettings.GameType.SURVIVAL;
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, new String[] {"survival", "creative", "adventure"}): (p_71516_2_.length == 2 ? getListOfStringsMatchingLastWord(p_71516_2_, this.getListOfPlayerUsernames()) : null);
    }

    /**
     * Returns String array containing all player usernames in the server.
     */
    protected String[] getListOfPlayerUsernames()
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