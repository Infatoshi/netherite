package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.ScaledResolution;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.Display;

/**
 * The raw input of a played session (--raw-rec FILE, play mode; make play
 * writes it beside the tape as NAME.raw.jsonl): every mouse and keyboard
 * event LWJGL delivered to the client, with the tick that consumed it and
 * the frame it arrived in, so the session can be played again through
 * another client's own input path (csrc/play/rawjudge.sh).
 *
 * The file is one JSON object per line, in the order things happened:
 *   {"raw":"netherite-raw",...}          the header: display size, GUI scale, sensitivity, OS
 *   {"m":[button,state,a,b,wheel],"g":G}  a Mouse event as it entered Mouse's read buffer
 *                                         (G 1: grabbed, a b are dx dy; 0: a b are the
 *                                         absolute display position, y from the bottom)
 *   {"k":[key,state,char,repeat]}         a Keyboard event (an LWJGL key code)
 *   {"tick":T}                            tick T begins (Oracle.preTick); the events
 *                                         before it that no earlier tick took are its input
 *   {"clock":MS}                          Minecraft.getSystemTime at the tick's screen input
 *                                         (GuiScreen's double-click and drag clock)
 *   {"late":MS}                           the in-game mouse loop ran MS after the last tick
 *                                         ended (over 200: vanilla skips its wheel and screen
 *                                         handling for every event of the tick)
 *   {"frame":F,"ticks":N,...}             frame F ended: it ran N ticks, then the camera
 *                                         ("cam":1, EntityRenderer turned the player by the
 *                                         MouseHelper deltas "dx" "dy"; "smooth":1 through the
 *                                         cinematic filter), drew at partial tick "pt" with the
 *                                         pointer at "mx" "my" (Mouse.getX/getY)
 *
 * An event is written when it is first seen in the read buffers (at each
 * frame's start, after the Display.update that polled it, and at the tick
 * hooks); vanilla's loops then consume every queued event in the next tick's
 * input block. Keyboard repeats are written too (Keyboard.next skips them
 * while repeat events are off, as they are in game).
 */
final class RawRec
{
    private RawRec() {}

    static String path;
    static PrintWriter out;
    static long frame;
    static long frameTick0;
    /** events in each read buffer that are written and not yet consumed */
    static int mouseSeen, keySeen;
    static boolean clockDone, lateDone;
    static Field fMouseRead, fKeyRead, fSystemTime, fTimer;

    static boolean active()
    {
        return path != null && Oracle.mode == Oracle.PLAY;
    }

    static void open(Minecraft m)
    {
        if (!active() || out != null) return;
        try
        {
            File f = new File(path);
            if (f.getParentFile() != null) f.getParentFile().mkdirs();
            out = new PrintWriter(new OutputStreamWriter(new Rows.AsyncOut(new FileOutputStream(f)), "UTF-8"));
            fMouseRead = Mouse.class.getDeclaredField("readBuffer");
            fMouseRead.setAccessible(true);
            fKeyRead = Keyboard.class.getDeclaredField("readBuffer");
            fKeyRead.setAccessible(true);
            fSystemTime = Minecraft.class.getDeclaredField("systemTime");
            fSystemTime.setAccessible(true);
            fTimer = Minecraft.class.getDeclaredField("timer");
            fTimer.setAccessible(true);
        }
        catch (Exception e) { throw new RuntimeException(e); }
        JsonObject h = new JsonObject();
        h.addProperty("raw", "netherite-raw");
        h.addProperty("v", 1);
        h.addProperty("tape", Oracle.tapePath == null ? "" : new File(Oracle.tapePath).getName());
        h.addProperty("w", m.displayWidth);
        h.addProperty("h", m.displayHeight);
        ScaledResolution sr = new ScaledResolution(m, m.displayWidth, m.displayHeight);
        h.addProperty("sw", sr.getScaledWidth());
        h.addProperty("sh", sr.getScaledHeight());
        h.addProperty("sf", sr.getScaleFactor());
        h.addProperty("sens", m.gameSettings.mouseSensitivity);
        h.addProperty("invert", m.gameSettings.invertMouse ? 1 : 0);
        h.addProperty("mac", Minecraft.isRunningOnMac ? 1 : 0);
        h.addProperty("lwjgl", org.lwjgl.Sys.getVersion());
        out.println(h.toString());
    }

    static void line(JsonObject o)
    {
        if (out != null) out.println(o.toString());
    }

    static ByteBuffer buf(Field f)
    {
        try { return (ByteBuffer)f.get(null); }
        catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }

    /** Every event that entered the read buffers since the last look. */
    static void scan()
    {
        if (out == null || !Mouse.isCreated() || !Keyboard.isCreated()) return;
        ByteBuffer mb = buf(fMouseRead);
        int rem = mb.remaining() / Mouse.EVENT_SIZE;
        if (rem < mouseSeen)
        {
            System.out.println("ORACLE WARN raw: the mouse buffer lost " + (mouseSeen - rem) + " events outside the input block t=" + Oracle.tick);
            mouseSeen = rem;
        }
        if (rem > mouseSeen)
        {
            ByteBuffer d = mb.duplicate();
            d.order(mb.order());
            d.position(mb.limit() - (rem - mouseSeen) * Mouse.EVENT_SIZE);
            int g = Mouse.isGrabbed() ? 1 : 0;
            while (d.remaining() >= Mouse.EVENT_SIZE)
            {
                int button = d.get(), state = d.get(), a = d.getInt(), b = d.getInt(), wheel = d.getInt();
                d.getLong();
                JsonObject o = new JsonObject();
                o.add("m", ints(button, state, a, b, wheel));
                o.addProperty("g", g);
                line(o);
            }
            mouseSeen = rem;
        }
        ByteBuffer kb = buf(fKeyRead);
        rem = kb.remaining() / Keyboard.EVENT_SIZE;
        if (rem < keySeen)
        {
            System.out.println("ORACLE WARN raw: the keyboard buffer lost " + (keySeen - rem) + " events outside the input block t=" + Oracle.tick);
            keySeen = rem;
        }
        if (rem > keySeen)
        {
            ByteBuffer d = kb.duplicate();
            d.order(kb.order());
            d.position(kb.limit() - (rem - keySeen) * Keyboard.EVENT_SIZE);
            while (d.remaining() >= Keyboard.EVENT_SIZE)
            {
                int key = d.getInt() & 0xFF, state = d.get(), ch = d.getInt();
                d.getLong();
                int repeat = d.get();
                JsonObject o = new JsonObject();
                o.add("k", ints(key, state, ch, repeat));
                line(o);
            }
            keySeen = rem;
        }
    }

    static JsonArray ints(int... v)
    {
        JsonArray a = new JsonArray();
        for (int x : v) a.add(new JsonPrimitive(x));
        return a;
    }

    /** Minecraft.runGameLoop, before the frame's ticks: what the last Display.update polled. */
    static void frameStart(Minecraft m)
    {
        if (!active()) return;
        open(m);
        if (RawDrive.active()) RawDrive.inject(m);
        scan();
        frameTick0 = Oracle.tick;
    }

    /** Oracle.preTick: tick T begins. */
    static void tickStart()
    {
        if (out == null) return;
        scan();
        JsonObject o = new JsonObject();
        o.addProperty("tick", Oracle.tick);
        line(o);
        clockDone = lateDone = false;
    }

    /** The tick's input block, before vanilla reads the queues (GUI: currentScreen.handleInput). */
    static void input(Minecraft m, boolean gui)
    {
        if (out == null) return;
        scan();
        if (gui && !clockDone)
        {
            JsonObject o = new JsonObject();
            o.addProperty("clock", Minecraft.getSystemTime());
            line(o);
            clockDone = true;
        }
        if (!gui && !lateDone)
        {
            long since;
            try { since = Minecraft.getSystemTime() - fSystemTime.getLong(m); }
            catch (IllegalAccessException e) { throw new RuntimeException(e); }
            if (since > 200L)
            {
                JsonObject o = new JsonObject();
                o.addProperty("late", since);
                line(o);
            }
            lateDone = true;
        }
    }

    /** After a vanilla input loop consumed the queues. */
    static void inputEnd()
    {
        if (out == null) return;
        mouseSeen = Mouse.isCreated() ? buf(fMouseRead).remaining() / Mouse.EVENT_SIZE : 0;
        keySeen = Keyboard.isCreated() ? buf(fKeyRead).remaining() / Keyboard.EVENT_SIZE : 0;
    }

    /** After the frame was drawn, before Display.update polls the next events. */
    static void frameEnd(Minecraft m)
    {
        if (out == null) return;
        scan();
        JsonObject o = new JsonObject();
        o.addProperty("frame", frame++);
        o.addProperty("ticks", Oracle.tick - frameTick0);
        o.addProperty("t", Oracle.tick);
        // EntityRenderer.updateCameraAndRender's condition for the mouse look
        boolean cam = m.inGameHasFocus && Display.isActive() && !m.skipRenderWorld;
        if (cam)
        {
            o.addProperty("cam", 1);
            o.addProperty("dx", m.mouseHelper.deltaX);
            o.addProperty("dy", m.mouseHelper.deltaY);
            if (m.gameSettings.smoothCamera) o.addProperty("smooth", 1);
        }
        try { o.addProperty("pt", ((net.minecraft.util.Timer)fTimer.get(m)).renderPartialTicks); }
        catch (IllegalAccessException e) { throw new RuntimeException(e); }
        o.addProperty("mx", Mouse.getX());
        o.addProperty("my", Mouse.getY());
        if (!Display.isActive()) o.addProperty("inactive", 1);
        if (m.currentScreen != null) o.addProperty("gui", m.currentScreen.getClass().getSimpleName());
        line(o);
    }

    static void close()
    {
        if (out == null) return;
        out.flush();
        out.close();
        out = null;
    }
}
