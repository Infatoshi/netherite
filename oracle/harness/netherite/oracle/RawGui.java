package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.inventory.Slot;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

/**
 * The GUI input judge's Java half (--raw-gui FILE): raw mouse and keyboard
 * events, in the native client's input-script form, fed to the real
 * GuiContainer through LWJGL's own event queues, so vanilla
 * GuiScreen.handleInput turns them into window clicks exactly as it would a
 * player's. Each line is one event at tick t:
 *   {"t":T,"mouse":1|2|3,"down":1|0,"x":X,"y":Y}   a press or release (left, right, middle)
 *   {"t":T,"motion":1,"x":X,"y":Y}                a pointer motion
 *   {"t":T,"key":"1".."9"|"q"|"e"|"escape"|"shift"|"ctrl","down":1|0}
 * with X and Y in GuiScreen's scaled pixels. At the gui-input point of tick T
 * with a GuiContainer up, the tick's events go into Mouse's and Keyboard's
 * read buffers (display pixels, y from the bottom, as LWJGL reads the X
 * server), and the two things a real player's frames would have left are
 * set: the hovered slot drawScreen finds under the pointer
 * (field_147006_u, the keys' target) as the events before tick T left it
 * (the native client draws one frame after each tick), and Keyboard's
 * key-down state for shift and control as tick T's events leave it (the
 * poll that delivered them). The clicks and the close the
 * screen makes are recorded as the row's gui ops (Oracle.onWindowClick).
 */
final class RawGui
{
    private RawGui() {}

    static String path;
    static final List<JsonObject> events = new ArrayList<JsonObject>();
    static int next;
    static boolean recording;
    // the pointer and the modifiers after the events so far (the next tick's frame)
    static int ptrX, ptrY;
    static boolean lshift, rshift, lctrl, rctrl;

    static boolean active()
    {
        return path != null;
    }

    static void load() throws Exception
    {
        events.clear();
        next = 0;
        ptrX = ptrY = 0;
        lshift = rshift = lctrl = rctrl = false;
        BufferedReader r = Rows.openRef(path);
        try
        {
            String line;
            while ((line = r.readLine()) != null)
            {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                events.add(Rows.parse(line));
            }
        }
        finally { r.close(); }
    }

    static int lwjglKey(String k)
    {
        if ("escape".equals(k)) return Keyboard.KEY_ESCAPE;
        if ("shift".equals(k)) return Keyboard.KEY_LSHIFT;
        if ("ctrl".equals(k)) return Keyboard.KEY_LCONTROL;
        if ("q".equals(k)) return Keyboard.KEY_Q;
        if ("e".equals(k)) return Keyboard.KEY_E;
        if (k.length() == 1 && k.charAt(0) >= '1' && k.charAt(0) <= '9') return Keyboard.KEY_1 + (k.charAt(0) - '1');
        return 0;
    }

    static Object stat(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(null);
    }

    /** At tick's gui-input point: true when this tick's events are in LWJGL's
     * queues and vanilla handleInput must run. */
    static boolean inject(Minecraft mc, long tick)
    {
        if (!(mc.currentScreen instanceof GuiContainer)) return false;
        List<JsonObject> now = new ArrayList<JsonObject>();
        while (next < events.size())
        {
            JsonObject e = events.get(next);
            long t = e.get("t").getAsLong();
            if (t > tick) break;
            if (t == tick) now.add(e);
            else track(e);   // a tick with no screen: its modifiers and pointer still move
            ++next;
        }
        if (now.isEmpty()) return false;
        try
        {
            GuiContainer gui = (GuiContainer)mc.currentScreen;
            if (Mouse.isGrabbed()) throw new IllegalStateException("the mouse is grabbed under a screen");
            int sf = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight).getScaleFactor();
            // drawScreen's hover at the last frame's pointer
            Method at = GuiContainer.class.getDeclaredMethod("func_146975_c", int.class, int.class);
            at.setAccessible(true);
            Slot hovered = (Slot)at.invoke(gui, ptrX, ptrY);
            Field hov = GuiContainer.class.getDeclaredField("field_147006_u");
            hov.setAccessible(true);
            hov.set(gui, hovered != null && hovered.func_111238_b() ? hovered : null);
            ByteBuffer mouse = (ByteBuffer)stat(Mouse.class, "readBuffer");
            ByteBuffer keys = (ByteBuffer)stat(Keyboard.class, "readBuffer");
            mouse.compact();
            keys.compact();
            for (JsonObject e : now)
            {
                if (e.has("key"))
                {
                    int k = lwjglKey(e.get("key").getAsString());
                    boolean d = e.get("down").getAsInt() != 0;
                    if (k != 0)
                    {
                        keys.putInt(k).put((byte)(d ? 1 : 0)).putInt(0).putLong(0L).put((byte)0);
                    }
                }
                else if (e.has("mouse") || e.has("motion"))
                {
                    int gx = e.get("x").getAsInt(), gy = e.get("y").getAsInt();
                    int button = -1;
                    boolean d = false;
                    if (e.has("mouse"))
                    {
                        int b = e.get("mouse").getAsInt();
                        button = b == 1 ? 0 : b == 2 ? 1 : 2;
                        d = e.get("down").getAsInt() != 0;
                    }
                    mouse.put((byte)button).put((byte)(d ? 1 : 0)).putInt(gx * sf).putInt(mc.displayHeight - 1 - gy * sf)
                         .putInt(0).putLong(0L);
                }
                track(e);
            }
            mouse.flip();
            keys.flip();
            // Keyboard.isKeyDown as the Display.update that delivered the
            // events polled it: after them (a Java client session shows a
            // control pressed in the frame of a Q is down for it)
            ByteBuffer down = (ByteBuffer)stat(Keyboard.class, "keyDownBuffer");
            down.put(Keyboard.KEY_LSHIFT, (byte)(lshift ? 1 : 0));
            down.put(Keyboard.KEY_RSHIFT, (byte)(rshift ? 1 : 0));
            down.put(Keyboard.KEY_LCONTROL, (byte)(lctrl ? 1 : 0));
            down.put(Keyboard.KEY_RCONTROL, (byte)(rctrl ? 1 : 0));
        }
        catch (RuntimeException ex) { throw ex; }
        catch (Exception ex) { throw new RuntimeException(ex); }
        recording = true;
        return true;
    }

    /** The state the event leaves for the next frame. */
    static void track(JsonObject e)
    {
        if (e.has("key"))
        {
            String k = e.get("key").getAsString();
            boolean d = e.get("down").getAsInt() != 0;
            if ("shift".equals(k)) lshift = d;
            else if ("ctrl".equals(k)) lctrl = d;
        }
        else if (e.has("mouse") || e.has("motion"))
        {
            ptrX = e.get("x").getAsInt();
            ptrY = e.get("y").getAsInt();
        }
    }

    /** A click or close the screen made while this tick's events ran. */
    static void record(JsonArray op)
    {
        if (Oracle.cur == null) return;
        if (Oracle.cur.gui == null) Oracle.cur.gui = new JsonArray();
        Oracle.cur.gui.add(op);
    }
}
