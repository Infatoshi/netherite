package net.minecraft.command;

import java.util.List;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.JsonToNBT;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTException;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.MinecraftServer;

public class CommandGive extends CommandBase
{
    private static final String __OBFID = "CL_00000502";

    public String getCommandName()
    {
        return "give";
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
        return "commands.give.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length < 2)
        {
            throw new WrongUsageException("commands.give.usage", new Object[0]);
        }
        else
        {
            EntityPlayerMP entityplayermp = getPlayer(p_71515_1_, p_71515_2_[0]);
            Item item = getItemByText(p_71515_1_, p_71515_2_[1]);
            int i = 1;
            int j = 0;

            if (p_71515_2_.length >= 3)
            {
                i = parseIntBounded(p_71515_1_, p_71515_2_[2], 1, 64);
            }

            if (p_71515_2_.length >= 4)
            {
                j = parseInt(p_71515_1_, p_71515_2_[3]);
            }

            ItemStack itemstack = new ItemStack(item, i, j);

            if (p_71515_2_.length >= 5)
            {
                String s = func_147178_a(p_71515_1_, p_71515_2_, 4).getUnformattedText();

                try
                {
                    NBTBase nbtbase = JsonToNBT.func_150315_a(s);

                    if (!(nbtbase instanceof NBTTagCompound))
                    {
                        func_152373_a(p_71515_1_, this, "commands.give.tagError", new Object[] {"Not a valid tag"});
                        return;
                    }

                    itemstack.setTagCompound((NBTTagCompound)nbtbase);
                }
                catch (NBTException nbtexception)
                {
                    func_152373_a(p_71515_1_, this, "commands.give.tagError", new Object[] {nbtexception.getMessage()});
                    return;
                }
            }

            EntityItem entityitem = entityplayermp.dropPlayerItemWithRandomChoice(itemstack, false);
            entityitem.delayBeforeCanPickup = 0;
            entityitem.func_145797_a(entityplayermp.getCommandSenderName());
            func_152373_a(p_71515_1_, this, "commands.give.success", new Object[] {itemstack.func_151000_E(), Integer.valueOf(i), entityplayermp.getCommandSenderName()});
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, this.getPlayers()) : (p_71516_2_.length == 2 ? getListOfStringsFromIterableMatchingLastWord(p_71516_2_, Item.itemRegistry.getKeys()) : null);
    }

    protected String[] getPlayers()
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