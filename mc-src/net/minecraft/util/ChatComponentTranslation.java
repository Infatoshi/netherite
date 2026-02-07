package net.minecraft.util;

import com.google.common.collect.Iterators;
import com.google.common.collect.Lists;
import java.util.Arrays;
import java.util.IllegalFormatException;
import java.util.Iterator;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ChatComponentTranslation extends ChatComponentStyle
{
    private final String key;
    private final Object[] formatArgs;
    private final Object syncLock = new Object();
    private long lastTranslationUpdateTimeInMilliseconds = -1L;
    /**
     * The discrete elements that make up this component.  For example, this would be ["Prefix, ", "FirstArg",
     * "SecondArg", " again ", "SecondArg", " and ", "FirstArg", " lastly ", "ThirdArg", " and also ", "FirstArg", "
     * again!"] for "translation.test.complex" (see en-US.lang)
     */
    List children = Lists.newArrayList();
    public static final Pattern stringVariablePattern = Pattern.compile("%(?:(\\d+)\\$)?([A-Za-z%]|$)");
    private static final String __OBFID = "CL_00001270";

    public ChatComponentTranslation(String p_i45160_1_, Object ... p_i45160_2_)
    {
        this.key = p_i45160_1_;
        this.formatArgs = p_i45160_2_;
        Object[] aobject = p_i45160_2_;
        int i = p_i45160_2_.length;

        for (int j = 0; j < i; ++j)
        {
            Object object1 = aobject[j];

            if (object1 instanceof IChatComponent)
            {
                ((IChatComponent)object1).getChatStyle().setParentStyle(this.getChatStyle());
            }
        }
    }

    /**
     * ensures that our children are initialized from the most recent string translation mapping.
     */
    synchronized void ensureInitialized()
    {
        Object object = this.syncLock;

        synchronized (this.syncLock)
        {
            long i = StatCollector.getLastTranslationUpdateTimeInMilliseconds();

            if (i == this.lastTranslationUpdateTimeInMilliseconds)
            {
                return;
            }

            this.lastTranslationUpdateTimeInMilliseconds = i;
            this.children.clear();
        }

        try
        {
            this.initializeFromFormat(StatCollector.translateToLocal(this.key));
        }
        catch (ChatComponentTranslationFormatException chatcomponenttranslationformatexception1)
        {
            this.children.clear();

            try
            {
                this.initializeFromFormat(StatCollector.translateToFallback(this.key));
            }
            catch (ChatComponentTranslationFormatException chatcomponenttranslationformatexception)
            {
                throw chatcomponenttranslationformatexception1;
            }
        }
    }

    /**
     * initializes our children from a format string, using the format args to fill in the placeholder variables.
     */
    protected void initializeFromFormat(String p_150269_1_)
    {
        boolean flag = false;
        Matcher matcher = stringVariablePattern.matcher(p_150269_1_);
        int i = 0;
        int j = 0;

        try
        {
            int l;

            for (; matcher.find(j); j = l)
            {
                int k = matcher.start();
                l = matcher.end();

                if (k > j)
                {
                    ChatComponentText chatcomponenttext = new ChatComponentText(String.format(p_150269_1_.substring(j, k), new Object[0]));
                    chatcomponenttext.getChatStyle().setParentStyle(this.getChatStyle());
                    this.children.add(chatcomponenttext);
                }

                String s3 = matcher.group(2);
                String s1 = p_150269_1_.substring(k, l);

                if ("%".equals(s3) && "%%".equals(s1))
                {
                    ChatComponentText chatcomponenttext2 = new ChatComponentText("%");
                    chatcomponenttext2.getChatStyle().setParentStyle(this.getChatStyle());
                    this.children.add(chatcomponenttext2);
                }
                else
                {
                    if (!"s".equals(s3))
                    {
                        throw new ChatComponentTranslationFormatException(this, "Unsupported format: \'" + s1 + "\'");
                    }

                    String s2 = matcher.group(1);
                    int i1 = s2 != null ? Integer.parseInt(s2) - 1 : i++;
                    this.children.add(this.getFormatArgumentAsComponent(i1));
                }
            }

            if (j < p_150269_1_.length())
            {
                ChatComponentText chatcomponenttext1 = new ChatComponentText(String.format(p_150269_1_.substring(j), new Object[0]));
                chatcomponenttext1.getChatStyle().setParentStyle(this.getChatStyle());
                this.children.add(chatcomponenttext1);
            }
        }
        catch (IllegalFormatException illegalformatexception)
        {
            throw new ChatComponentTranslationFormatException(this, illegalformatexception);
        }
    }

    private IChatComponent getFormatArgumentAsComponent(int p_150272_1_)
    {
        if (p_150272_1_ >= this.formatArgs.length)
        {
            throw new ChatComponentTranslationFormatException(this, p_150272_1_);
        }
        else
        {
            Object object = this.formatArgs[p_150272_1_];
            Object object1;

            if (object instanceof IChatComponent)
            {
                object1 = (IChatComponent)object;
            }
            else
            {
                object1 = new ChatComponentText(object == null ? "null" : object.toString());
                ((IChatComponent)object1).getChatStyle().setParentStyle(this.getChatStyle());
            }

            return (IChatComponent)object1;
        }
    }

    public IChatComponent setChatStyle(ChatStyle p_150255_1_)
    {
        super.setChatStyle(p_150255_1_);
        Object[] aobject = this.formatArgs;
        int i = aobject.length;

        for (int j = 0; j < i; ++j)
        {
            Object object = aobject[j];

            if (object instanceof IChatComponent)
            {
                ((IChatComponent)object).getChatStyle().setParentStyle(this.getChatStyle());
            }
        }

        if (this.lastTranslationUpdateTimeInMilliseconds > -1L)
        {
            Iterator iterator = this.children.iterator();

            while (iterator.hasNext())
            {
                IChatComponent ichatcomponent = (IChatComponent)iterator.next();
                ichatcomponent.getChatStyle().setParentStyle(p_150255_1_);
            }
        }

        return this;
    }

    public Iterator iterator()
    {
        this.ensureInitialized();
        return Iterators.concat(createDeepCopyIterator(this.children), createDeepCopyIterator(this.siblings));
    }

    /**
     * Gets the text of this component, without any special formatting codes added, for chat.  TODO: why is this two
     * different methods?
     */
    public String getUnformattedTextForChat()
    {
        this.ensureInitialized();
        StringBuilder stringbuilder = new StringBuilder();
        Iterator iterator = this.children.iterator();

        while (iterator.hasNext())
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            stringbuilder.append(ichatcomponent.getUnformattedTextForChat());
        }

        return stringbuilder.toString();
    }

    /**
     * Creates a copy of this component.  Almost a deep copy, except the style is shallow-copied.
     */
    public ChatComponentTranslation createCopy()
    {
        Object[] aobject = new Object[this.formatArgs.length];

        for (int i = 0; i < this.formatArgs.length; ++i)
        {
            if (this.formatArgs[i] instanceof IChatComponent)
            {
                aobject[i] = ((IChatComponent)this.formatArgs[i]).createCopy();
            }
            else
            {
                aobject[i] = this.formatArgs[i];
            }
        }

        ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation(this.key, aobject);
        chatcomponenttranslation.setChatStyle(this.getChatStyle().createShallowCopy());
        Iterator iterator = this.getSiblings().iterator();

        while (iterator.hasNext())
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            chatcomponenttranslation.appendSibling(ichatcomponent.createCopy());
        }

        return chatcomponenttranslation;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (this == p_equals_1_)
        {
            return true;
        }
        else if (!(p_equals_1_ instanceof ChatComponentTranslation))
        {
            return false;
        }
        else
        {
            ChatComponentTranslation chatcomponenttranslation = (ChatComponentTranslation)p_equals_1_;
            return Arrays.equals(this.formatArgs, chatcomponenttranslation.formatArgs) && this.key.equals(chatcomponenttranslation.key) && super.equals(p_equals_1_);
        }
    }

    public int hashCode()
    {
        int i = super.hashCode();
        i = 31 * i + this.key.hashCode();
        i = 31 * i + Arrays.hashCode(this.formatArgs);
        return i;
    }

    public String toString()
    {
        return "TranslatableComponent{key=\'" + this.key + '\'' + ", args=" + Arrays.toString(this.formatArgs) + ", siblings=" + this.siblings + ", style=" + this.getChatStyle() + '}';
    }

    public String getKey()
    {
        return this.key;
    }

    public Object[] getFormatArgs()
    {
        return this.formatArgs;
    }
}