package net.minecraft.command;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.event.ClickEvent;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.MathHelper;

public class CommandHelp extends CommandBase
{
    private static final String __OBFID = "CL_00000529";

    public String getCommandName()
    {
        return "help";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 0;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.help.usage";
    }

    public List getCommandAliases()
    {
        return Arrays.asList(new String[] {"?"});
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        List list = this.getSortedPossibleCommands(p_71515_1_);
        byte b0 = 7;
        int i = (list.size() - 1) / b0;
        boolean flag = false;
        int k;

        try
        {
            k = p_71515_2_.length == 0 ? 0 : parseIntBounded(p_71515_1_, p_71515_2_[0], 1, i + 1) - 1;
        }
        catch (NumberInvalidException numberinvalidexception)
        {
            Map map = this.getCommands();
            ICommand icommand = (ICommand)map.get(p_71515_2_[0]);

            if (icommand != null)
            {
                throw new WrongUsageException(icommand.getCommandUsage(p_71515_1_), new Object[0]);
            }

            if (MathHelper.parseIntWithDefault(p_71515_2_[0], -1) != -1)
            {
                throw numberinvalidexception;
            }

            throw new CommandNotFoundException();
        }

        int j = Math.min((k + 1) * b0, list.size());
        ChatComponentTranslation chatcomponenttranslation1 = new ChatComponentTranslation("commands.help.header", new Object[] {Integer.valueOf(k + 1), Integer.valueOf(i + 1)});
        chatcomponenttranslation1.getChatStyle().setColor(EnumChatFormatting.DARK_GREEN);
        p_71515_1_.addChatMessage(chatcomponenttranslation1);

        for (int l = k * b0; l < j; ++l)
        {
            ICommand icommand1 = (ICommand)list.get(l);
            ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation(icommand1.getCommandUsage(p_71515_1_), new Object[0]);
            chatcomponenttranslation.getChatStyle().setChatClickEvent(new ClickEvent(ClickEvent.Action.SUGGEST_COMMAND, "/" + icommand1.getCommandName() + " "));
            p_71515_1_.addChatMessage(chatcomponenttranslation);
        }

        if (k == 0 && p_71515_1_ instanceof EntityPlayer)
        {
            ChatComponentTranslation chatcomponenttranslation2 = new ChatComponentTranslation("commands.help.footer", new Object[0]);
            chatcomponenttranslation2.getChatStyle().setColor(EnumChatFormatting.GREEN);
            p_71515_1_.addChatMessage(chatcomponenttranslation2);
        }
    }

    /**
     * Returns a sorted list of all possible commands for the given ICommandSender.
     */
    protected List getSortedPossibleCommands(ICommandSender p_71534_1_)
    {
        List list = MinecraftServer.getServer().getCommandManager().getPossibleCommands(p_71534_1_);
        Collections.sort(list);
        return list;
    }

    protected Map getCommands()
    {
        return MinecraftServer.getServer().getCommandManager().getCommands();
    }
}