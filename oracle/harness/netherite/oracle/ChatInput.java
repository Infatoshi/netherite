package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonNull;
import com.google.gson.JsonPrimitive;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiChat;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.ScaledResolution;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

/**
 * The chat screen's input (GuiChat and GuiSleepMP): one tick's keyboard and
 * mouse events, put into LWJGL's own read buffers as Display.update's poll
 * puts a person's, then run by vanilla GuiScreen.handleInput once, over the
 * screen that is up (a key that closes it leaves the rest of the batch to
 * the closed GuiChat, as vanilla's loop does). What the poll leaves for the
 * whole batch is one state: Keyboard's key-down buffer for shift and
 * control, and Mouse's pointer (GuiChat.mouseClicked and drawScreen read
 * Mouse.getX and getY, not the event's position).
 *
 * Tape form, one op per tick:
 *   ["chat", mods, px, py, clip, comp, [ev, ...]]
 * mods: 1 shift, 2 control, as the poll left them; px, py: the pointer in
 * display pixels, y from the bottom (LWJGL's); clip: the system clipboard at
 * the batch's start (GuiScreen.getClipboardString, which control-V pastes),
 * or null to leave it; comp: when the batch has a left press, the chat
 * component under the pointer that GuiChat.mouseClicked acts on
 * (GuiNewChat.func_146236_a with a click event), as [action, value, text]
 * (ClickEvent's canonical name and value, the component's
 * getUnformattedTextForChat), else null: a native replay has no drawn chat
 * lines to find it in, and a Java replay checks it; ev in handleInput's
 * order, every mouse event before
 * every key:
 *   [0, key, char]         a key press (LWJGL code, the UTF-16 unit it typed)
 *   [1, x, y, button]      a mouse press, x and y in display pixels as the
 *   [2, x, y, button]      event carries them; a release
 *   [3, d]                 a wheel turn (Mouse.getEventDWheel)
 *
 * Agent form: the tick's gui list may hold ["type", TEXT] (one key press
 * per UTF-16 unit, code 0), ["key", CODE] or ["key", CODE, CHAR],
 * ["paste", TEXT] (the clipboard set to TEXT, then control-V: code 47, char
 * 22), ["wheel", D], ["chatclick", X, Y, BUTTON] (press and release at GUI
 * pixels X, Y, the pointer moved there) and ["mods", M]; they resolve into
 * the one tape op.
 */
final class ChatInput
{
    private ChatInput() {}

    static boolean isAgentOp(String kind)
    {
        return "type".equals(kind) || "key".equals(kind) || "paste".equals(kind) || "wheel".equals(kind)
            || "chatclick".equals(kind) || "mods".equals(kind);
    }

    private static Object stat(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(null);
    }

    private static void setStat(Class<?> c, String name, int v) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        f.setInt(null, v);
    }

    /** The agent ops of one tick, in order, as the tape op. */
    static JsonArray resolve(Minecraft mc, JsonArray agent)
    {
        int sf = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight).getScaleFactor();
        int mods = 0;
        int px = Mouse.getX(), py = Mouse.getY();
        JsonElement clip = JsonNull.INSTANCE;
        JsonArray mice = new JsonArray(), keys = new JsonArray();
        for (JsonElement e : agent)
        {
            JsonArray op = e.getAsJsonArray();
            String kind = op.get(0).getAsString();
            if ("mods".equals(kind)) mods = op.get(1).getAsInt();
            else if ("type".equals(kind))
            {
                String s = op.get(1).getAsString();
                for (int i = 0; i < s.length(); ++i) keys.add(key(0, s.charAt(i)));
            }
            else if ("key".equals(kind)) keys.add(key(op.get(1).getAsInt(), op.size() > 2 ? op.get(2).getAsInt() : 0));
            else if ("paste".equals(kind))
            {
                if (!clip.isJsonNull() || keys.size() > 0) throw new IllegalArgumentException("chat: paste must come first in its tick");
                clip = new JsonPrimitive(op.get(1).getAsString());
                mods |= 2;
                keys.add(key(Keyboard.KEY_V, 22));
            }
            else if ("wheel".equals(kind))
            {
                JsonArray w = new JsonArray();
                w.add(new JsonPrimitive(3));
                w.add(new JsonPrimitive(op.get(1).getAsInt()));
                mice.add(w);
            }
            else if ("chatclick".equals(kind))
            {
                int x = op.get(1).getAsInt() * sf, y = mc.displayHeight - 1 - op.get(2).getAsInt() * sf, b = op.get(3).getAsInt();
                px = x;
                py = y;
                for (int s = 1; s <= 2; ++s)
                {
                    JsonArray m = new JsonArray();
                    m.add(new JsonPrimitive(s));
                    m.add(new JsonPrimitive(x));
                    m.add(new JsonPrimitive(y));
                    m.add(new JsonPrimitive(b));
                    mice.add(m);
                }
            }
        }
        JsonArray ev = new JsonArray();
        for (JsonElement e : mice) ev.add(e);
        for (JsonElement e : keys) ev.add(e);
        JsonArray op = new JsonArray();
        op.add(new JsonPrimitive("chat"));
        op.add(new JsonPrimitive(mods));
        op.add(new JsonPrimitive(px));
        op.add(new JsonPrimitive(py));
        op.add(clip);
        op.add(comp(mc, ev, px, py));
        op.add(ev);
        return op;
    }

    /** The batch's comp field: GuiChat.mouseClicked's lookup at the pointer. */
    static JsonElement comp(Minecraft mc, JsonArray ev, int px, int py)
    {
        boolean left = false;
        for (JsonElement e : ev)
        {
            JsonArray a = e.getAsJsonArray();
            if (a.get(0).getAsInt() == 1 && a.get(3).getAsInt() == 0) left = true;
        }
        if (!left || !mc.gameSettings.chatLinks) return JsonNull.INSTANCE;
        net.minecraft.util.IChatComponent c = mc.ingameGUI.getChatGUI().func_146236_a(px, py);
        if (c == null || c.getChatStyle().getChatClickEvent() == null) return JsonNull.INSTANCE;
        net.minecraft.event.ClickEvent k = c.getChatStyle().getChatClickEvent();
        JsonArray r = new JsonArray();
        r.add(new JsonPrimitive(k.getAction().getCanonicalName()));
        r.add(new JsonPrimitive(k.getValue()));
        r.add(new JsonPrimitive(c.getUnformattedTextForChat()));
        return r;
    }

    private static JsonArray key(int code, int ch)
    {
        JsonArray k = new JsonArray();
        k.add(new JsonPrimitive(0));
        k.add(new JsonPrimitive(code));
        k.add(new JsonPrimitive(ch));
        return k;
    }

    /** Run one tape op through the chat screen that is up. */
    static void apply(Minecraft mc, JsonArray op)
    {
        if (!(mc.currentScreen instanceof GuiChat)) throw new IllegalArgumentException("chat input without a chat screen");
        if (Mouse.isGrabbed()) throw new IllegalStateException("the mouse is grabbed under the chat screen");
        int mods = op.get(1).getAsInt();
        int px = op.get(2).getAsInt(), py = op.get(3).getAsInt();
        JsonElement clip = op.get(4);
        JsonArray ev = op.get(6).getAsJsonArray();
        JsonElement want = comp(mc, ev, px, py);
        if (!want.equals(op.get(5)))
            throw new IllegalStateException("chat: the click finds " + want + " under the pointer, the tape says " + op.get(5));
        try
        {
            if (!clip.isJsonNull()) GuiScreen.setClipboardString(clip.getAsString());
            ByteBuffer down = (ByteBuffer)stat(Keyboard.class, "keyDownBuffer");
            down.put(Keyboard.KEY_LSHIFT, (byte)((mods & 1) != 0 ? 1 : 0));
            down.put(Keyboard.KEY_LCONTROL, (byte)((mods & 2) != 0 ? 1 : 0));
            setStat(Mouse.class, "x", px);
            setStat(Mouse.class, "y", py);
            ByteBuffer mouse = (ByteBuffer)stat(Mouse.class, "readBuffer");
            ByteBuffer keys = (ByteBuffer)stat(Keyboard.class, "readBuffer");
            /* the read buffers hold 50 events each: a longer batch runs in
             * turns, every mouse event before every key as one call reads
             * them */
            net.minecraft.client.gui.GuiScreen screen = mc.currentScreen;
            int i = 0, n = ev.size();
            while (i < n)
            {
                mouse.compact();
                keys.compact();
                for (; i < n; ++i)
                {
                    JsonArray a = ev.get(i).getAsJsonArray();
                    int kind = a.get(0).getAsInt();
                    ByteBuffer b = kind == 0 ? keys : mouse;
                    if (b.remaining() < 22) break;
                    if (kind == 0) keys.putInt(a.get(1).getAsInt()).put((byte)1).putInt(a.get(2).getAsInt()).putLong(0L).put((byte)0);
                    else if (kind == 1 || kind == 2)
                        mouse.put((byte)a.get(3).getAsInt()).put((byte)(kind == 1 ? 1 : 0)).putInt(a.get(1).getAsInt())
                             .putInt(a.get(2).getAsInt()).putInt(0).putLong(0L);
                    else if (kind == 3) mouse.put((byte)-1).put((byte)0).putInt(px).putInt(py).putInt(a.get(1).getAsInt()).putLong(0L);
                    else throw new IllegalArgumentException("chat: unknown event " + kind);
                }
                mouse.flip();
                keys.flip();
                screen.handleInput();
            }
            down.put(Keyboard.KEY_LSHIFT, (byte)0);
            down.put(Keyboard.KEY_LCONTROL, (byte)0);
        }
        catch (RuntimeException ex) { throw ex; }
        catch (Exception ex) { throw new RuntimeException(ex); }
    }
}
