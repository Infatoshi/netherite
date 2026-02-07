package net.minecraft.event;

import com.google.common.collect.Maps;
import java.util.Map;

public class ClickEvent
{
    private final ClickEvent.Action action;
    private final String value;
    private static final String __OBFID = "CL_00001260";

    public ClickEvent(ClickEvent.Action p_i45156_1_, String p_i45156_2_)
    {
        this.action = p_i45156_1_;
        this.value = p_i45156_2_;
    }

    /**
     * Gets the action to perform when this event is raised.
     */
    public ClickEvent.Action getAction()
    {
        return this.action;
    }

    /**
     * Gets the value to perform the action on when this event is raised.  For example, if the action is "open URL",
     * this would be the URL to open.
     */
    public String getValue()
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
            ClickEvent clickevent = (ClickEvent)p_equals_1_;

            if (this.action != clickevent.action)
            {
                return false;
            }
            else
            {
                if (this.value != null)
                {
                    if (!this.value.equals(clickevent.value))
                    {
                        return false;
                    }
                }
                else if (clickevent.value != null)
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
        return "ClickEvent{action=" + this.action + ", value=\'" + this.value + '\'' + '}';
    }

    public int hashCode()
    {
        int i = this.action.hashCode();
        i = 31 * i + (this.value != null ? this.value.hashCode() : 0);
        return i;
    }

    public static enum Action
    {
        OPEN_URL("open_url", true),
        OPEN_FILE("open_file", false),
        RUN_COMMAND("run_command", true),
        TWITCH_USER_INFO("twitch_user_info", false),
        SUGGEST_COMMAND("suggest_command", true);
        private static final Map nameMapping = Maps.newHashMap();
        private final boolean allowedInChat;
        /** The canonical name used to refer to this action. */
        private final String canonicalName;

        private static final String __OBFID = "CL_00001261";

        private Action(String p_i45155_3_, boolean p_i45155_4_)
        {
            this.canonicalName = p_i45155_3_;
            this.allowedInChat = p_i45155_4_;
        }

        /**
         * Indicates whether this event can be run from chat text.
         */
        public boolean shouldAllowInChat()
        {
            return this.allowedInChat;
        }

        /**
         * Gets the canonical name for this action (e.g., "run_command")
         */
        public String getCanonicalName()
        {
            return this.canonicalName;
        }

        /**
         * Gets a value by its canonical name.
         */
        public static ClickEvent.Action getValueByCanonicalName(String p_150672_0_)
        {
            return (ClickEvent.Action)nameMapping.get(p_150672_0_);
        }

        static
        {
            ClickEvent.Action[] var0 = values();
            int var1 = var0.length;

            for (int var2 = 0; var2 < var1; ++var2)
            {
                ClickEvent.Action var3 = var0[var2];
                nameMapping.put(var3.getCanonicalName(), var3);
            }
        }
    }
}