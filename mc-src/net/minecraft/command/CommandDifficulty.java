package net.minecraft.command;

import java.util.List;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.world.EnumDifficulty;

public class CommandDifficulty extends CommandBase
{
    private static final String __OBFID = "CL_00000422";

    public String getCommandName()
    {
        return "difficulty";
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
        return "commands.difficulty.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length > 0)
        {
            EnumDifficulty enumdifficulty = this.func_147201_h(p_71515_1_, p_71515_2_[0]);
            MinecraftServer.getServer().func_147139_a(enumdifficulty);
            func_152373_a(p_71515_1_, this, "commands.difficulty.success", new Object[] {new ChatComponentTranslation(enumdifficulty.getDifficultyResourceKey(), new Object[0])});
        }
        else
        {
            throw new WrongUsageException("commands.difficulty.usage", new Object[0]);
        }
    }

    protected EnumDifficulty func_147201_h(ICommandSender p_147201_1_, String p_147201_2_)
    {
        return !p_147201_2_.equalsIgnoreCase("peaceful") && !p_147201_2_.equalsIgnoreCase("p") ? (!p_147201_2_.equalsIgnoreCase("easy") && !p_147201_2_.equalsIgnoreCase("e") ? (!p_147201_2_.equalsIgnoreCase("normal") && !p_147201_2_.equalsIgnoreCase("n") ? (!p_147201_2_.equalsIgnoreCase("hard") && !p_147201_2_.equalsIgnoreCase("h") ? EnumDifficulty.getDifficultyEnum(parseIntBounded(p_147201_1_, p_147201_2_, 0, 3)) : EnumDifficulty.HARD) : EnumDifficulty.NORMAL) : EnumDifficulty.EASY) : EnumDifficulty.PEACEFUL;
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, new String[] {"peaceful", "easy", "normal", "hard"}): null;
    }
}