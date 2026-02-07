package net.minecraft.command;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.Map.Entry;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.CommandEvent;

public class CommandHandler implements ICommandManager
{
    private static final Logger logger = LogManager.getLogger();
    /** Map of Strings to the ICommand objects they represent */
    private final Map commandMap = new HashMap();
    /** The set of ICommand objects currently loaded. */
    private final Set commandSet = new HashSet();
    private static final String __OBFID = "CL_00001765";

    public int executeCommand(ICommandSender p_71556_1_, String p_71556_2_)
    {
        p_71556_2_ = p_71556_2_.trim();

        if (p_71556_2_.startsWith("/"))
        {
            p_71556_2_ = p_71556_2_.substring(1);
        }

        String[] astring = p_71556_2_.split(" ");
        String s1 = astring[0];
        astring = dropFirstString(astring);
        ICommand icommand = (ICommand)this.commandMap.get(s1);
        int i = this.getUsernameIndex(icommand, astring);
        int j = 0;
        ChatComponentTranslation chatcomponenttranslation;

        try
        {
            if (icommand == null)
            {
                throw new CommandNotFoundException();
            }

            if (icommand.canCommandSenderUseCommand(p_71556_1_))
            {
                CommandEvent event = new CommandEvent(icommand, p_71556_1_, astring);
                if (MinecraftForge.EVENT_BUS.post(event))
                {
                    if (event.exception != null)
                    {
                        throw event.exception;
                    }
                    return 1;
                }

                if (i > -1)
                {
                    EntityPlayerMP[] aentityplayermp = PlayerSelector.matchPlayers(p_71556_1_, astring[i]);
                    String s2 = astring[i];
                    EntityPlayerMP[] aentityplayermp1 = aentityplayermp;
                    int k = aentityplayermp.length;

                    for (int l = 0; l < k; ++l)
                    {
                        EntityPlayerMP entityplayermp = aentityplayermp1[l];
                        astring[i] = entityplayermp.getCommandSenderName();

                        try
                        {
                            icommand.processCommand(p_71556_1_, astring);
                            ++j;
                        }
                        catch (CommandException commandexception1)
                        {
                            ChatComponentTranslation chatcomponenttranslation1 = new ChatComponentTranslation(commandexception1.getMessage(), commandexception1.getErrorOjbects());
                            chatcomponenttranslation1.getChatStyle().setColor(EnumChatFormatting.RED);
                            p_71556_1_.addChatMessage(chatcomponenttranslation1);
                        }
                    }

                    astring[i] = s2;
                }
                else
                {
                    try
                    {
                        icommand.processCommand(p_71556_1_, astring);
                        ++j;
                    }
                    catch (CommandException commandexception)
                    {
                        chatcomponenttranslation = new ChatComponentTranslation(commandexception.getMessage(), commandexception.getErrorOjbects());
                        chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
                        p_71556_1_.addChatMessage(chatcomponenttranslation);
                    }
                }
            }
            else
            {
                ChatComponentTranslation chatcomponenttranslation2 = new ChatComponentTranslation("commands.generic.permission", new Object[0]);
                chatcomponenttranslation2.getChatStyle().setColor(EnumChatFormatting.RED);
                p_71556_1_.addChatMessage(chatcomponenttranslation2);
            }
        }
        catch (WrongUsageException wrongusageexception)
        {
            chatcomponenttranslation = new ChatComponentTranslation("commands.generic.usage", new Object[] {new ChatComponentTranslation(wrongusageexception.getMessage(), wrongusageexception.getErrorOjbects())});
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
            p_71556_1_.addChatMessage(chatcomponenttranslation);
        }
        catch (CommandException commandexception2)
        {
            chatcomponenttranslation = new ChatComponentTranslation(commandexception2.getMessage(), commandexception2.getErrorOjbects());
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
            p_71556_1_.addChatMessage(chatcomponenttranslation);
        }
        catch (Throwable throwable)
        {
            chatcomponenttranslation = new ChatComponentTranslation("commands.generic.exception", new Object[0]);
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
            p_71556_1_.addChatMessage(chatcomponenttranslation);
            logger.error("Couldn\'t process command: \'" + p_71556_2_ + "\'", throwable);
        }

        return j;
    }

    /**
     * adds the command and any aliases it has to the internal map of available commands
     */
    public ICommand registerCommand(ICommand p_71560_1_)
    {
        List list = p_71560_1_.getCommandAliases();
        this.commandMap.put(p_71560_1_.getCommandName(), p_71560_1_);
        this.commandSet.add(p_71560_1_);

        if (list != null)
        {
            Iterator iterator = list.iterator();

            while (iterator.hasNext())
            {
                String s = (String)iterator.next();
                ICommand icommand1 = (ICommand)this.commandMap.get(s);

                if (icommand1 == null || !icommand1.getCommandName().equals(s))
                {
                    this.commandMap.put(s, p_71560_1_);
                }
            }
        }

        return p_71560_1_;
    }

    /**
     * creates a new array and sets elements 0..n-2 to be 0..n-1 of the input (n elements)
     */
    private static String[] dropFirstString(String[] p_71559_0_)
    {
        String[] astring1 = new String[p_71559_0_.length - 1];

        for (int i = 1; i < p_71559_0_.length; ++i)
        {
            astring1[i - 1] = p_71559_0_[i];
        }

        return astring1;
    }

    /**
     * Performs a "begins with" string match on each token in par2. Only returns commands that par1 can use.
     */
    public List getPossibleCommands(ICommandSender p_71558_1_, String p_71558_2_)
    {
        String[] astring = p_71558_2_.split(" ", -1);
        String s1 = astring[0];

        if (astring.length == 1)
        {
            ArrayList arraylist = new ArrayList();
            Iterator iterator = this.commandMap.entrySet().iterator();

            while (iterator.hasNext())
            {
                Entry entry = (Entry)iterator.next();

                if (CommandBase.doesStringStartWith(s1, (String)entry.getKey()) && ((ICommand)entry.getValue()).canCommandSenderUseCommand(p_71558_1_))
                {
                    arraylist.add(entry.getKey());
                }
            }

            return arraylist;
        }
        else
        {
            if (astring.length > 1)
            {
                ICommand icommand = (ICommand)this.commandMap.get(s1);

                if (icommand != null)
                {
                    return icommand.addTabCompletionOptions(p_71558_1_, dropFirstString(astring));
                }
            }

            return null;
        }
    }

    /**
     * returns all commands that the commandSender can use
     */
    public List getPossibleCommands(ICommandSender p_71557_1_)
    {
        ArrayList arraylist = new ArrayList();
        Iterator iterator = this.commandSet.iterator();

        while (iterator.hasNext())
        {
            ICommand icommand = (ICommand)iterator.next();

            if (icommand.canCommandSenderUseCommand(p_71557_1_))
            {
                arraylist.add(icommand);
            }
        }

        return arraylist;
    }

    /**
     * returns a map of string to commads. All commands are returned, not just ones which someone has permission to use.
     */
    public Map getCommands()
    {
        return this.commandMap;
    }

    /**
     * Return a command's first parameter index containing a valid username.
     */
    private int getUsernameIndex(ICommand p_82370_1_, String[] p_82370_2_)
    {
        if (p_82370_1_ == null)
        {
            return -1;
        }
        else
        {
            for (int i = 0; i < p_82370_2_.length; ++i)
            {
                if (p_82370_1_.isUsernameIndex(p_82370_2_, i) && PlayerSelector.matchesMultiplePlayers(p_82370_2_[i]))
                {
                    return i;
                }
            }

            return -1;
        }
    }
}