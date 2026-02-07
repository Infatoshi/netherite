package net.minecraft.util;

import com.google.common.base.Function;
import com.google.common.collect.Iterators;
import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;

public abstract class ChatComponentStyle implements IChatComponent
{
    /**
     * The later siblings of this component.  If this component turns the text bold, that will apply to all the siblings
     * until a later sibling turns the text something else.
     */
    protected List siblings = Lists.newArrayList();
    private ChatStyle style;
    private static final String __OBFID = "CL_00001257";

    /**
     * Appends the given component to the end of this one.
     */
    public IChatComponent appendSibling(IChatComponent p_150257_1_)
    {
        p_150257_1_.getChatStyle().setParentStyle(this.getChatStyle());
        this.siblings.add(p_150257_1_);
        return this;
    }

    /**
     * Gets the sibling components of this one.
     */
    public List getSiblings()
    {
        return this.siblings;
    }

    /**
     * Appends the given text to the end of this component.
     */
    public IChatComponent appendText(String p_150258_1_)
    {
        return this.appendSibling(new ChatComponentText(p_150258_1_));
    }

    public IChatComponent setChatStyle(ChatStyle p_150255_1_)
    {
        this.style = p_150255_1_;
        Iterator iterator = this.siblings.iterator();

        while (iterator.hasNext())
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            ichatcomponent.getChatStyle().setParentStyle(this.getChatStyle());
        }

        return this;
    }

    public ChatStyle getChatStyle()
    {
        if (this.style == null)
        {
            this.style = new ChatStyle();
            Iterator iterator = this.siblings.iterator();

            while (iterator.hasNext())
            {
                IChatComponent ichatcomponent = (IChatComponent)iterator.next();
                ichatcomponent.getChatStyle().setParentStyle(this.style);
            }
        }

        return this.style;
    }

    public Iterator iterator()
    {
        return Iterators.concat(Iterators.forArray(new ChatComponentStyle[] {this}), createDeepCopyIterator(this.siblings));
    }

    /**
     * Gets the text of this component, without any special formatting codes added.  TODO: why is this two different
     * methods?
     */
    public final String getUnformattedText()
    {
        StringBuilder stringbuilder = new StringBuilder();
        Iterator iterator = this.iterator();

        while (iterator.hasNext())
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            stringbuilder.append(ichatcomponent.getUnformattedTextForChat());
        }

        return stringbuilder.toString();
    }

    /**
     * Gets the text of this component, with formatting codes added for rendering.
     */
    @SideOnly(Side.CLIENT)
    public final String getFormattedText()
    {
        StringBuilder stringbuilder = new StringBuilder();
        Iterator iterator = this.iterator();

        while (iterator.hasNext())
        {
            IChatComponent ichatcomponent = (IChatComponent)iterator.next();
            stringbuilder.append(ichatcomponent.getChatStyle().getFormattingCode());
            stringbuilder.append(ichatcomponent.getUnformattedTextForChat());
            stringbuilder.append(EnumChatFormatting.RESET);
        }

        return stringbuilder.toString();
    }

    /**
     * Creates an iterator that iterates over the given components, returning deep copies of each component in turn so
     * that the properties of the returned objects will remain externally consistent after being returned.
     */
    public static Iterator createDeepCopyIterator(Iterable p_150262_0_)
    {
        Iterator iterator = Iterators.concat(Iterators.transform(p_150262_0_.iterator(), new Function()
        {
            private static final String __OBFID = "CL_00001258";
            public Iterator apply(IChatComponent p_apply_1_)
            {
                return p_apply_1_.iterator();
            }
            public Object apply(Object p_apply_1_)
            {
                return this.apply((IChatComponent)p_apply_1_);
            }
        }));
        iterator = Iterators.transform(iterator, new Function()
        {
            private static final String __OBFID = "CL_00001259";
            public IChatComponent apply(IChatComponent p_apply_1_)
            {
                IChatComponent ichatcomponent1 = p_apply_1_.createCopy();
                ichatcomponent1.setChatStyle(ichatcomponent1.getChatStyle().createDeepCopy());
                return ichatcomponent1;
            }
            public Object apply(Object p_apply_1_)
            {
                return this.apply((IChatComponent)p_apply_1_);
            }
        });
        return iterator;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (this == p_equals_1_)
        {
            return true;
        }
        else if (!(p_equals_1_ instanceof ChatComponentStyle))
        {
            return false;
        }
        else
        {
            ChatComponentStyle chatcomponentstyle = (ChatComponentStyle)p_equals_1_;
            return this.siblings.equals(chatcomponentstyle.siblings) && this.getChatStyle().equals(chatcomponentstyle.getChatStyle());
        }
    }

    public int hashCode()
    {
        return 31 * this.style.hashCode() + this.siblings.hashCode();
    }

    public String toString()
    {
        return "BaseComponent{style=" + this.style + ", siblings=" + this.siblings + '}';
    }
}