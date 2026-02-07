package net.minecraft.event;

import com.google.common.collect.Maps;
import java.util.Map;
import net.minecraft.util.IChatComponent;

public class HoverEvent
{
    private final HoverEvent.Action action;
    private final IChatComponent value;
    private static final String __OBFID = "CL_00001264";

    public HoverEvent(HoverEvent.Action p_i45158_1_, IChatComponent p_i45158_2_)
    {
        this.action = p_i45158_1_;
        this.value = p_i45158_2_;
    }

    /**
     * Gets the action to perform when this event is raised.
     */
    public HoverEvent.Action getAction()
    {
        return this.action;
    }

    /**
     * Gets the value to perform the action on when this event is raised.  For example, if the action is "show item",
     * this would be the item to show.
     */
    public IChatComponent getValue()
    {
        return this.value;
    }

    public boolean equals(Object p_equals_1_)
    {
        if (this == p_equals_1_)
        {
            return true;
        }
        else if (p_equals_1_ != null && this.getClass() == p_equals_1_.getClass())
        {
            HoverEvent hoverevent = (HoverEvent)p_equals_1_;

            if (this.action != hoverevent.action)
            {
                return false;
            }
            else
            {
                if (this.value != null)
                {
                    if (!this.value.equals(hoverevent.value))
                    {
                        return false;
                    }
                }
                else if (hoverevent.value != null)
                {
                    return false;
                }

                return true;
            }
        }
        else
        {
            return false;
        }
    }

    public String toString()
    {
        return "HoverEvent{action=" + this.action + ", value=\'" + this.value + '\'' + '}';
    }

    public int hashCode()
    {
        int i = this.action.hashCode();
        i = 31 * i + (this.value != null ? this.value.hashCode() : 0);
        return i;
    }

    public static enum Action
    {
        SHOW_TEXT("show_text", true),
        SHOW_ACHIEVEMENT("show_achievement", true),
        SHOW_ITEM("show_item", true);
        private static final Map nameMapping = Maps.newHashMap();
        private final boolean allowedInChat;
        private final String canonicalName;

        private static final String __OBFID = "CL_00001265";

        private Action(String p_i45157_3_, boolean p_i45157_4_)
        {
            this.canonicalName = p_i45157_3_;
            this.allowedInChat = p_i45157_4_;
        }

        /**
         * Indicates whether this event can be run from chat text.
         */
        public boolean shouldAllowInChat()
        {
            return this.allowedInChat;
        }

        /**
         * Gets the canonical name for this action (e.g., "show_achievement")
         */
        public String getCanonicalName()
        {
            return this.canonicalName;
        }

        /**
         * Gets a value by its canonical name.
         */
        public static HoverEvent.Action getValueByCanonicalName(String p_150684_0_)
        {
            return (HoverEvent.Action)nameMapping.get(p_150684_0_);
        }

        static
        {
            HoverEvent.Action[] var0 = values();
            int var1 = var0.length;

            for (int var2 = 0; var2 < var1; ++var2)
            {
                HoverEvent.Action var3 = var0[var2];
                nameMapping.put(var3.getCanonicalName(), var3);
            }
        }
    }
}