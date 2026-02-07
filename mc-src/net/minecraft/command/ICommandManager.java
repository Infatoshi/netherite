package net.minecraft.command;

import java.util.List;
import java.util.Map;

public interface ICommandManager
{
    int executeCommand(ICommandSender p_71556_1_, String p_71556_2_);

    /**
     * Performs a "begins with" string match on each token in par2. Only returns commands that par1 can use.
     */
    List getPossibleCommands(ICommandSender p_71558_1_, String p_71558_2_);

    /**
     * returns all commands that the commandSender can use
     */
    List getPossibleCommands(ICommandSender p_71557_1_);

    /**
     * returns a map of string to commads. All commands are returned, not just ones which someone has permission to use.
     */
    Map getCommands();
}