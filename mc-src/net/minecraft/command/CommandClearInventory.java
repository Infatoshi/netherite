package net.minecraft.command;

import java.util.List;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.server.MinecraftServer;

public class CommandClearInventory extends CommandBase
{
    private static final String __OBFID = "CL_00000218";

    public String getCommandName()
    {
        return "clear";
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.clear.usage";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 2;
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        EntityPlayerMP entityplayermp = p_71515_2_.length == 0 ? getCommandSenderAsPlayer(p_71515_1_) : getPlayer(p_71515_1_, p_71515_2_[0]);
        Item item = p_71515_2_.length >= 2 ? getItemByText(p_71515_1_, p_71515_2_[1]) : null;
        int i = p_71515_2_.length >= 3 ? parseIntWithMin(p_71515_1_, p_71515_2_[2], 0) : -1;

        if (p_71515_2_.length >= 2 && item == null)
        {
            throw new CommandException("commands.clear.failure", new Object[] {entityplayermp.getCommandSenderName()});
        }
        else
        {
            int j = entityplayermp.inventory.clearInventory(item, i);
            entityplayermp.inventoryContainer.detectAndSendChanges();

            if (!entityplayermp.capabilities.isCreativeMode)
            {
                entityplayermp.updateHeldItem();
            }

            if (j == 0)
            {
                throw new CommandException("commands.clear.failure", new Object[] {entityplayermp.getCommandSenderName()});
            }
            else
            {
                func_152373_a(p_71515_1_, this, "commands.clear.success", new Object[] {entityplayermp.getCommandSenderName(), Integer.valueOf(j)});
            }
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, this.func_147209_d()) : (p_71516_2_.length == 2 ? getListOfStringsFromIterableMatchingLastWord(p_71516_2_, Item.itemRegistry.getKeys()) : null);
    }

    protected String[] func_147209_d()
    {
        return MinecraftServer.getServer().getAllUsernames();
    }

    /**
     * Return whether the specified command parameter index is a username parameter.
     */
    public boolean isUsernameIndex(String[] p_82358_1_, int p_82358_2_)
    {
        return p_82358_2_ == 0;
    }
}