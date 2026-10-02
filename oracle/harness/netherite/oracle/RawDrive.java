package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.gui.ScaledResolution;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.inventory.Slot;
import org.lwjgl.input.Keyboard;
import org.lwjgl.input.Mouse;

/**
 * A stand-in for a person at the play-mode client (--raw-drive FILE, with
 * make play DRIVE=, oracle/tests/rawplay.sh): at the start of each frame,
 * right after the Display.update that polls a real keyboard and mouse, it
 * puts its events into LWJGL's own state the way that poll does (the Mouse
 * and Keyboard read buffers, Mouse's accumulated dx and dy, its pointer and
 * buttons, Keyboard's key-down buffer), so vanilla reads them exactly as it
 * reads a person's; the frame rate, the ticks each frame runs and the
 * partial ticks are the wall clock's. Like a person it looks at the screen
 * (the client's world and player) to decide what to press next. The raw
 * recorder (RawRec) writes what LWJGL delivered, not what this asked for.
 *
 * FILE is a list of steps run one after another (a step with "t" waits
 * until that tick). Instant steps:
 *   {"key":NAME,"down":1|0}  {"tap":NAME}       a key (Keyboard.getKeyIndex names: W, SPACE, LCONTROL, E, 1)
 *   {"button":B,"down":1|0}  {"click":B}        a mouse button (0 left, 1 right) at the pointer
 *   {"wheel":D}                                 a wheel notch
 *   {"turn":[DX,DY],"ticks":N}                  mouse counts spread over N ticks of wall time
 *   {"find":[ids],"r":R,"dy":[lo,hi],"as":NAME} the nearest such block to the feet (y from feet+lo to feet+hi)
 *   {"log":TEXT}                                a line in the client log
 *   {"dump":R}                                  the block ids around the feet, in the log
 *   {"finditem":ID,"as":NAME,"from":S}          the open screen's first slot from S holding ID (0: empty)
 *   {"select":ID}                               the hotbar key of the slot holding item ID
 *   {"mark":NAME,"from":NAME2,"off":[dx,dy,dz]} a block position relative to another
 *   {"tap":NAME,"ifscreen":1}                   the tap only while a container screen is up
 *   {"close":1,"with":K}                        E (or key K: ESCAPE), when a container screen is up
 * (a window that lost the mouse gets a left click first, as a person gives it; on the death
 * screen the steps wait: R once, then the Respawn button every second from 30 ticks)
 * Steps that take frames:
 *   {"wait":N}  {"until":T}                     ticks
 *   {"goto":P,"within":D,"ticks":N,"sprint":1,"jumpy":1}   walk (W, LCONTROL, SPACE) steering with the mouse
 *   {"aim":P,"ticks":N}                         turn to face a point (P: a name, [x,y,z] or a block name + "at")
 *   {"face":[YAW,PITCH],"ticks":N}              turn to an absolute look
 *   {"mine":NAME,"ticks":N}                     face the block and hold the left button until it breaks
 *   {"use":NAME,"at":"top"|"side","ticks":N}    face the block's top (or near side) and click the right button
 *   {"attack":"Class|Class","r":R,"ticks":N}    chase the nearest living one and hit it until it dies
 *   {"hold":B,"ticks":N}                        hold a button N ticks (eating)
 *   {"collect":R,"within":D,"ticks":N}          walk over the nearest drop within R
 *   {"place":ID,"on":[ids],"as":NAME}           the held block on nearby ground, checked, another spot on a miss
 *   {"open":NAME}                               right-click the block until its screen is up
 *   {"tunnel":N,"yaw":Y}                        a two-high tunnel N blocks that way, walking in
 *   {"run":YAW,"ticks":N}                       sprint-jump that way, back when stopped
 *   {"attack":...,"kills":K}                    ... one target at a time, done after K deaths
 *   {"slot":S,"button":B}                       over the open screen's slot S (Container index), click B
 *   {"slotdown":S,"button":B} {"slotmove":S} {"slotup":S,"button":B}   a drag across slots
 *   ("nomove":1: the press without the small motion after it)
 *   {"dbl":S}                                   two left clicks on slot S a frame apart
 *   {"outside":1,"button":B}                    a click outside the screen's window
 *   (a slot step's "off":[DX,DY] moves the pointer that far from the slot's centre; "pace":"frame"
 *   takes each move, press and release a frame apart rather than a tick, "burst" all in one frame)
 *   {"hover":S,"off":[DX,DY]}                   the pointer over slot S, no click (instant)
 *   {"point":[GX,GY]}                           the pointer to GuiScreen pixel GX, GY (instant)
 *   {"glide":S|[GX,GY],"frames":N,"off":[DX,DY]} the pointer moved there over N frames, a motion each
 *   {"finditem":-1,"nth":K,...}                 the K-th (mod their count) slot from S holding anything
 *   {"digdown":N}                               look down and dig N blocks under the feet
 *   {"pillar":N}                                jump and place the held block under the feet N times
 *   {"end":1}                                   the session ends (the tape and the raw file close)
 */
final class RawDrive
{
    private RawDrive() {}

    static String path;
    static final List<JsonObject> steps = new ArrayList<JsonObject>();
    static int next;
    static JsonObject cur;
    static long curTick;       // the tick the current step started
    static int curFrames;      // frames the current step has run
    static int phase;          // the step's own state
    static long phaseTick;
    static final Map<String, int[]> marks = new HashMap<String, int[]>();
    static final Set<Integer> keysDown = new HashSet<Integer>();
    static final boolean[] buttonsDown = new boolean[3];
    static final List<int[]> releases = new ArrayList<int[]>();   // {0 key / 1 button, code}: up next frame
    static int ptrX = -1, ptrY = -1;          // display pixels, y from the bottom
    static final List<long[]> turns = new ArrayList<long[]>();    // {dx, dy, startNanos, durNanos, doneX, doneY}
    static int frameDx, frameDy;              // mouse counts this frame
    static ByteBuffer mb, kb;
    static boolean grabbed;
    static long now;
    static Minecraft mc;
    static final double DEG = 0.15D;          // Entity.setAngles per count at sensitivity 0.5

    static boolean active()
    {
        return path != null;
    }

    static void load() throws Exception
    {
        BufferedReader r = Rows.openRef(path);
        try
        {
            String l;
            while ((l = r.readLine()) != null)
            {
                l = l.trim();
                if (l.isEmpty() || l.startsWith("#")) continue;
                steps.add(Rows.parse(l));
            }
        }
        finally { r.close(); }
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static int key(String name)
    {
        int k = Keyboard.getKeyIndex(name.toUpperCase());
        if (k == Keyboard.KEY_NONE) throw new IllegalArgumentException("raw drive: no key " + name);
        return k;
    }

    static void say(String s)
    {
        System.out.println("ORACLE RAW DRIVE t=" + Oracle.tick + " " + s);
    }

    // ------------------------------------------------------------------ events

    static void keyEv(int k, boolean down)
    {
        if (down) keysDown.add(k); else keysDown.remove(k);
        if (down) repeats.put(k, new long[] {now, 0}); else repeats.remove(k);
        char ch = down ? Keyboard.getKeyName(k).toLowerCase().charAt(0) : 0;
        kb.putInt(k).put((byte)(down ? 1 : 0)).putInt(ch).putLong(now).put((byte)0);
    }

    /** X's autorepeat for a held key (Xvfb's 660 ms delay, 25 a second), as
     *  LWJGL queues it: a press flagged repeat, which Keyboard.next skips
     *  while repeat events are off (in game and under a container screen).
     *  Modifiers do not repeat. */
    static final Map<Integer, long[]> repeats = new HashMap<Integer, long[]>();   // key: {down nanos, repeats sent}

    static void keyRepeats()
    {
        for (Map.Entry<Integer, long[]> e : repeats.entrySet())
        {
            int k = e.getKey();
            if (k == Keyboard.KEY_LSHIFT || k == Keyboard.KEY_RSHIFT || k == Keyboard.KEY_LCONTROL || k == Keyboard.KEY_RCONTROL
                || k == Keyboard.KEY_LMENU || k == Keyboard.KEY_RMENU || k == Keyboard.KEY_LMETA || k == Keyboard.KEY_RMETA) continue;
            long[] r = e.getValue();
            long due = now - r[0] < 660000000L ? 0 : (now - r[0] - 660000000L) / 40000000L + 1;
            char ch = Keyboard.getKeyName(k).toLowerCase().charAt(0);
            for (int i = 0; r[1] < due && i < 5; ++i, ++r[1])
                kb.putInt(k).put((byte)1).putInt(ch).putLong(now).put((byte)1);
            r[1] = Math.max(r[1], due);
        }
    }

    static void tapKey(int k)
    {
        keyEv(k, true);
        releases.add(new int[] {0, k});
    }

    static void mouseEv(int button, boolean down, int wheel)
    {
        if (button >= 0 && button < 3) buttonsDown[button] = down;
        mb.put((byte)button).put((byte)(down ? 1 : 0));
        if (grabbed) mb.putInt(0).putInt(0);
        else mb.putInt(ptrX).putInt(ptrY);
        mb.putInt(wheel).putLong(now);
    }

    static void clickButton(int b)
    {
        mouseEv(b, true, 0);
        releases.add(new int[] {1, b});
    }

    /** The pointer to GuiScreen pixel (gx, gy), one motion event. */
    static void pointTo(int gx, int gy)
    {
        int sf = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight).getScaleFactor();
        int sh = mc.displayHeight / sf;
        ptrX = gx * sf + 1;
        ptrY = (sh - 1 - gy) * sf + 1;
        mouseEv(-1, false, 0);
    }

    static void setKey(int k, boolean down)
    {
        if (keysDown.contains(k) != down) keyEv(k, down);
    }

    // ------------------------------------------------------------------ looking

    static EntityClientPlayerMP pl()
    {
        return mc.thePlayer;
    }

    static double wrap(double a)
    {
        a %= 360.0D;
        if (a >= 180.0D) a -= 360.0D;
        if (a < -180.0D) a += 360.0D;
        return a;
    }

    /** Turn toward yaw/pitch this frame, at most MAX degrees; true when there. */
    static boolean turnTo(double yaw, double pitch, double max)
    {
        if (!grabbed || !mc.inGameHasFocus) return false;
        double dyaw = wrap(yaw - pl().rotationYaw), dp = pitch - pl().rotationPitch;
        double m = Math.max(Math.abs(dyaw), Math.abs(dp));
        if (m < 0.2D) return true;
        double s = m > max ? max / m : 1.0D;
        int cx = (int)Math.round(dyaw * s / DEG), cy = (int)Math.round(dp * s / DEG);
        frameDx += cx;
        frameDy -= cy;
        return m <= max && Math.abs(dyaw - cx * DEG) < 0.2D && Math.abs(dp - cy * DEG) < 0.2D;
    }

    /** The look the next tick will use is within half a degree of yaw, pitch. */
    static boolean onTarget(double yaw, double pitch)
    {
        return Math.abs(wrap(yaw - pl().rotationYaw)) < 0.5D && Math.abs(pitch - pl().rotationPitch) < 0.5D;
    }

    static double[] lookAt(double x, double y, double z)
    {
        double dx = x - pl().posX, dy = y - pl().posY, dz = z - pl().posZ;
        double yaw = Math.atan2(-dx, dz) * 180.0D / Math.PI;
        double pitch = -Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)) * 180.0D / Math.PI;
        return new double[] {yaw, pitch};
    }

    /** The point a step names: {"at":...} on a named block, or [x,y,z]. */
    static double[] point(JsonElement p, String at)
    {
        if (p.isJsonArray())
        {
            JsonArray a = p.getAsJsonArray();
            return new double[] {a.get(0).getAsDouble(), a.get(1).getAsDouble(), a.get(2).getAsDouble()};
        }
        if (p.getAsString().startsWith("entity:"))
        {
            Entity e = nearest(p.getAsString().substring(7), 64.0D);
            return e == null ? null : new double[] {e.posX, e.posY, e.posZ};
        }
        int[] b = marks.get(p.getAsString());
        if (b == null) return null;
        if ("top".equals(at)) return new double[] {b[0] + 0.5D, b[1] + 1.0D, b[2] + 0.5D};
        if ("feet".equals(at)) return new double[] {b[0] + 0.5D, b[1], b[2] + 0.5D};
        // the face toward the eye, at its centre
        double cx = b[0] + 0.5D, cy = b[1] + 0.5D, cz = b[2] + 0.5D;
        double ex = pl().posX - cx, ey = pl().posY - cy, ez = pl().posZ - cz;
        double ax = Math.abs(ex), ay = Math.abs(ey), az = Math.abs(ez);
        if (ay >= ax && ay >= az) return new double[] {cx, cy + Math.signum(ey) * 0.49D, cz};
        if (ax >= az) return new double[] {cx + Math.signum(ex) * 0.49D, cy, cz};
        return new double[] {cx, cy, cz + Math.signum(ez) * 0.49D};
    }

    static int feetY()
    {
        return (int)Math.floor(pl().boundingBox.minY + 0.001D);
    }

    static Entity nearest(String classes, double r)
    {
        Entity best = null;
        double bd = r * r;
        for (Object o : mc.theWorld.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (e == pl() || e.isDead || !("|" + classes + "|").contains("|" + e.getClass().getSimpleName() + "|")) continue;
            if (e instanceof EntityLivingBase && ((EntityLivingBase)e).getHealth() <= 0.0F) continue;
            double d = pl().getDistanceSqToEntity(e);
            if (d < bd) { bd = d; best = e; }
        }
        return best;
    }

    // ------------------------------------------------------------------ the screen

    static int[] slotAt(int s)
    {
        if (!(mc.currentScreen instanceof GuiContainer)) return null;
        GuiContainer g = (GuiContainer)mc.currentScreen;
        try
        {
            int left = field(GuiContainer.class, "field_147003_i").getInt(g);
            int top = field(GuiContainer.class, "field_147009_r").getInt(g);
            if (s < 0 || s >= g.field_147002_h.inventorySlots.size()) return null;
            Slot slot = (Slot)g.field_147002_h.inventorySlots.get(s);
            return new int[] {left + slot.xDisplayPosition + 8, top + slot.yDisplayPosition + 8};
        }
        catch (RuntimeException e) { throw e; }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    // ------------------------------------------------------------------ the frame

    static void inject(Minecraft m)
    {
        mc = m;
        if (!Mouse.isCreated() || !Keyboard.isCreated()) return;
        try
        {
            mb = (ByteBuffer)field(Mouse.class, "readBuffer").get(null);
            kb = (ByteBuffer)field(Keyboard.class, "readBuffer").get(null);
            mb.compact();
            kb.compact();
            now = System.nanoTime();
            boolean was = grabbed;
            grabbed = Mouse.isGrabbed();
            // MouseHelper.ungrabMouseCursor's setCursorPosition: a person's
            // pointer is at the window's centre when a screen opens
            if (was && !grabbed) { ptrX = mc.displayWidth / 2; ptrY = mc.displayHeight / 2; }
            frameDx = frameDy = 0;
            if (ptrX < 0) { ptrX = mc.displayWidth / 2; ptrY = mc.displayHeight / 2; }
            for (int[] r : releases)
            {
                if (r[0] == 0) keyEv(r[1], false);
                else mouseEv(r[1], false, 0);
            }
            releases.clear();
            keyRepeats();
            if (Oracle.joined && mc.thePlayer != null && mc.theWorld != null) runSteps();
            mb.flip();
            kb.flip();
            // the polled state
            if (!grabbed)
            {
                field(Mouse.class, "x").setInt(null, ptrX);
                field(Mouse.class, "y").setInt(null, ptrY);
                field(Mouse.class, "absolute_x").setInt(null, ptrX);
                field(Mouse.class, "absolute_y").setInt(null, ptrY);
            }
            ByteBuffer buttons = (ByteBuffer)field(Mouse.class, "buttons").get(null);
            for (int b = 0; b < 3; ++b) buttons.put(b, (byte)(buttonsDown[b] ? 1 : 0));
            ByteBuffer down = (ByteBuffer)field(Keyboard.class, "keyDownBuffer").get(null);
            for (int k : keysDown) down.put(k, (byte)1);
            for (int i = 0; i < turns.size(); )
            {
                long[] t = turns.get(i);
                double f = Math.min(1.0D, (double)(now - t[2]) / (double)t[3]);
                long wx = (long)Math.floor(t[0] * f), wy = (long)Math.floor(t[1] * f);
                frameDx += (int)(wx - t[4]);
                frameDy += (int)(wy - t[5]);
                t[4] = wx;
                t[5] = wy;
                if (f >= 1.0D) turns.remove(i); else ++i;
            }
            if (grabbed && (frameDx != 0 || frameDy != 0))
            {
                Field fdx = field(Mouse.class, "dx"), fdy = field(Mouse.class, "dy");
                fdx.setInt(null, fdx.getInt(null) + frameDx);
                fdy.setInt(null, fdy.getInt(null) + frameDy);
            }
        }
        catch (RuntimeException ex) { throw ex; }
        catch (Exception ex) { throw new RuntimeException(ex); }
    }

    static long refocusTick = -100;

    static long deadTick = -1, deadAct = -1;

    static void runSteps()
    {
        // dead: a person tries R, then clicks Respawn once the buttons wake (20 ticks)
        if (mc.currentScreen instanceof net.minecraft.client.gui.GuiGameOver)
        {
            if (deadTick < 0) { deadTick = Oracle.tick; say("dead: R, then the Respawn button"); }
            long d = Oracle.tick - deadTick;
            if (d == 5 && deadAct != Oracle.tick) { tapKey(key("R")); deadAct = Oracle.tick; }
            if (d >= 30 && d % 20 == 10 && deadAct != Oracle.tick)
            {
                deadAct = Oracle.tick;
                ScaledResolution sr = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight);
                pointTo(sr.getScaledWidth() / 2, sr.getScaledHeight() / 4 + 82);
                clickButton(0);
            }
            return;
        }
        deadTick = -1;
        // a window that lost the mouse (Escape in game) gets a click, as a person gives it
        if (mc.currentScreen == null && !mc.inGameHasFocus && Oracle.tick - refocusTick > 10)
        {
            say("refocus: a click into the window");
            clickButton(0);
            refocusTick = Oracle.tick;
            return;
        }
        for (int guard = 0; guard < 64; ++guard)
        {
            if (cur == null)
            {
                if (next >= steps.size()) return;
                JsonObject s = steps.get(next);
                if (s.has("t") && s.get("t").getAsLong() > Oracle.tick) return;
                cur = s;
                ++next;
                curTick = Oracle.tick;
                curFrames = 0;
                phase = 0;
                phaseTick = Oracle.tick;
            }
            boolean done = step(cur);
            ++curFrames;
            if (Oracle.done) return;
            if (!done) return;
            cur = null;
        }
    }

    static long elapsed()
    {
        return Oracle.tick - curTick;
    }

    static int ticks(JsonObject s, int def)
    {
        return s.has("ticks") ? s.get("ticks").getAsInt() : def;
    }

    /** One frame of step S; true when it is finished. */
    static boolean step(JsonObject s)
    {
        if (s.has("end"))
        {
            say("end");
            Oracle.finish(0);
            return true;
        }
        if (s.has("log")) { say(s.get("log").getAsString()); return true; }
        if (s.has("close"))
        {
            // the open screen's close key (E), nothing when none is up
            if (mc.currentScreen instanceof GuiContainer) tapKey(key(s.has("with") ? s.get("with").getAsString() : "E"));
            return true;
        }
        if (s.has("hover"))
        {
            if (!(mc.currentScreen instanceof GuiContainer)) return true;
            int n = slotIndex(s.get("hover"));
            int[] c = n < 0 ? null : slotAt(n);
            if (c != null) pointTo(c[0] + off(s, 0), c[1] + off(s, 1));
            return true;
        }
        if (s.has("point"))
        {
            JsonArray a = s.getAsJsonArray("point");
            if (mc.currentScreen instanceof GuiContainer) pointTo(a.get(0).getAsInt(), a.get(1).getAsInt());
            return true;
        }
        if (s.has("glide")) return glide(s);
        if (s.has("slot") || s.has("slotdown") || s.has("slotup") || s.has("slotmove") || s.has("dbl") || s.has("outside"))
            return slotStep(s);
        if (s.has("key")) { setKey(key(s.get("key").getAsString()), s.get("down").getAsInt() != 0); return true; }
        if (s.has("tap"))
        {
            // "ifscreen":1 only while a container screen is up (Escape in game is not judged)
            if (!s.has("ifscreen") || mc.currentScreen instanceof GuiContainer) tapKey(key(s.get("tap").getAsString()));
            return true;
        }
        if (s.has("button")) { mouseEv(s.get("button").getAsInt(), s.get("down").getAsInt() != 0, 0); return true; }
        if (s.has("click")) { clickButton(s.get("click").getAsInt()); return true; }
        if (s.has("wheel")) { mouseEv(-1, false, s.get("wheel").getAsInt() * 120); return true; }
        if (s.has("turn"))
        {
            JsonArray p = s.getAsJsonArray("turn");
            turns.add(new long[] {p.get(0).getAsLong(), p.get(1).getAsLong(), now, Math.max(1, ticks(s, 1)) * 50000000L, 0, 0});
            return true;
        }
        if (s.has("find")) { find(s); return true; }
        if (s.has("finditem")) { findItem(s); return true; }
        if (s.has("select")) { select(s); return true; }
        if (s.has("mark"))
        {
            int[] b = marks.get(s.get("from").getAsString());
            JsonArray o = s.getAsJsonArray("off");
            if (b != null) marks.put(s.get("mark").getAsString(), new int[] {b[0] + o.get(0).getAsInt(), b[1] + o.get(1).getAsInt(), b[2] + o.get(2).getAsInt()});
            return true;
        }
        if (s.has("dump"))
        {
            // the blocks around the feet, one line per layer (a scenario author's look around)
            int r = s.get("dump").getAsInt(), px = (int)Math.floor(pl().posX), pz = (int)Math.floor(pl().posZ), fy = feetY();
            say("dump at " + px + "," + fy + "," + pz + " (rows z, columns x from " + (px - r) + ")");
            for (int y = fy + 3; y >= fy - 2; --y)
                for (int z = pz - r; z <= pz + r; ++z)
                {
                    StringBuilder b = new StringBuilder();
                    for (int x = px - r; x <= px + r; ++x)
                        b.append(String.format("%4d", Block.getIdFromBlock(mc.theWorld.getBlock(x, y, z))));
                    say("y" + y + " z" + z + b);
                }
            return true;
        }
        if (s.has("wait")) return elapsed() >= s.get("wait").getAsLong();
        if (s.has("until")) return Oracle.tick >= s.get("until").getAsLong();
        if (s.has("goto") || s.has("collect")) return gotoStep(s);
        if (s.has("aim"))
        {
            double[] p = point(s.get("aim"), s.has("at") ? s.get("at").getAsString() : null);
            if (p == null) return true;
            double[] l = lookAt(p[0], p[1], p[2]);
            return turnTo(l[0], l[1], 12.0D) || elapsed() >= ticks(s, 40);
        }
        if (s.has("face"))
        {
            JsonArray a = s.getAsJsonArray("face");
            return turnTo(a.get(0).getAsDouble(), a.get(1).getAsDouble(), 12.0D) || elapsed() >= ticks(s, 40);
        }
        if (s.has("mine")) return mine(s.get("mine").getAsString(), ticks(s, 200), curTick);
        if (s.has("use")) return use(s);
        if (s.has("place")) return place(s);
        if (s.has("open")) return open(s);
        if (s.has("attack")) return attack(s);
        if (s.has("run")) return run(s);
        if (s.has("tunnel")) return tunnel(s);
        if (s.has("hold"))
        {
            int b = s.get("hold").getAsInt();
            if (phase == 0) { mouseEv(b, true, 0); phase = 1; return false; }
            if (elapsed() < ticks(s, 40)) return false;
            mouseEv(b, false, 0);
            return true;
        }
        if (s.has("digdown")) return digdown(s);
        if (s.has("pillar")) return pillar(s);
        throw new IllegalArgumentException("raw drive: unknown step " + s);
    }

    /** A slot step's slot: a Container index, or a name a finditem step set. */
    static int slotIndex(JsonElement e)
    {
        if (e.isJsonPrimitive() && e.getAsJsonPrimitive().isNumber()) return e.getAsInt();
        int[] m = marks.get(e.getAsString());
        return m == null ? -1 : m[0];
    }

    /** {"finditem":ID,"as":NAME,"from":LO}: the open screen's first slot at or after LO holding item ID. */
    static void findItem(JsonObject s)
    {
        String name = s.get("as").getAsString();
        marks.remove(name);
        if (!(mc.currentScreen instanceof GuiContainer)) { say("finditem without a screen"); return; }
        List<?> slots = ((GuiContainer)mc.currentScreen).field_147002_h.inventorySlots;
        int id = s.get("finditem").getAsInt(), from = s.has("from") ? s.get("from").getAsInt() : 0;
        List<Integer> found = new ArrayList<Integer>();
        for (int i = from; i < slots.size(); ++i)
        {
            net.minecraft.item.ItemStack st = ((Slot)slots.get(i)).getStack();
            boolean empty = st == null;
            if (id == 0 ? empty : id < 0 ? !empty : !empty && net.minecraft.item.Item.getIdFromItem(st.getItem()) == id)
                found.add(i);
        }
        if (found.isEmpty()) { say("finditem " + id + ": none"); return; }
        int i = found.get(s.has("nth") ? s.get("nth").getAsInt() % found.size() : 0);
        marks.put(name, new int[] {i});
        say("finditem " + id + ": slot " + i + " as " + name);
    }

    /** {"select":ID}: the hotbar key of the first hotbar slot holding item ID. */
    static void select(JsonObject s)
    {
        int id = s.get("select").getAsInt();
        for (int i = 0; i < 9; ++i)
        {
            net.minecraft.item.ItemStack st = pl().inventory.mainInventory[i];
            if (st != null && net.minecraft.item.Item.getIdFromItem(st.getItem()) == id)
            {
                tapKey(key(Integer.toString(i + 1)));
                return;
            }
        }
        say("select " + id + ": not in the hotbar");
    }

    static void find(JsonObject s)
    {
        Set<Integer> ids = new HashSet<Integer>();
        for (JsonElement e : s.getAsJsonArray("find")) ids.add(e.getAsInt());
        int r = s.has("r") ? s.get("r").getAsInt() : 16;
        int lo = -1, hi = 2;
        if (s.has("dy")) { lo = s.getAsJsonArray("dy").get(0).getAsInt(); hi = s.getAsJsonArray("dy").get(1).getAsInt(); }
        int px = (int)Math.floor(pl().posX), pz = (int)Math.floor(pl().posZ), fy = feetY();
        int[] best = null;
        double bd = Double.MAX_VALUE;
        for (int x = px - r; x <= px + r; ++x)
            for (int z = pz - r; z <= pz + r; ++z)
                for (int y = fy + lo; y <= fy + hi; ++y)
                {
                    if (!ids.contains(Block.getIdFromBlock(mc.theWorld.getBlock(x, y, z)))) continue;
                    if (s.has("min") && Math.hypot(x + 0.5D - pl().posX, z + 0.5D - pl().posZ) < s.get("min").getAsDouble()) continue;
                    if (s.has("open") && !mc.theWorld.isAirBlock(x, y + 1, z)) continue;
                    if (tried.contains(x + "," + y + "," + z)) continue;
                    double d = (x + 0.5D - pl().posX) * (x + 0.5D - pl().posX) + (z + 0.5D - pl().posZ) * (z + 0.5D - pl().posZ)
                        + (y - fy) * (y - fy);
                    if (d < bd) { bd = d; best = new int[] {x, y, z}; }
                }
        String name = s.has("as") ? s.get("as").getAsString() : "target";
        if (best == null) { marks.remove(name); say("find " + ids + ": none within " + r); }
        else { marks.put(name, best); say("find " + ids + ": " + best[0] + "," + best[1] + "," + best[2] + " as " + name); }
    }

    static double lastX, lastZ;
    static int still;

    static boolean gotoStep(JsonObject s)
    {
        double[] p;
        if (s.has("collect"))
        {
            // the nearest dropped item within R: a person walking over their drops
            Entity it = nearest("EntityItem|EntityXPOrb", s.get("collect").getAsDouble());
            p = it == null ? null : new double[] {it.posX, it.posY, it.posZ};
        }
        else p = point(s.get("goto"), s.has("at") ? s.get("at").getAsString() : "feet");
        int w = key("W"), sp = key("LCONTROL"), jump = key("SPACE");
        double within = s.has("within") ? s.get("within").getAsDouble() : 0.6D;
        boolean sprint = s.has("sprint") && s.get("sprint").getAsInt() != 0;
        boolean jumpy = s.has("jumpy") && s.get("jumpy").getAsInt() != 0;
        double dx = p == null ? 0 : p[0] - pl().posX, dz = p == null ? 0 : p[2] - pl().posZ;
        if (phase == 2)
        {
            // clearing the block that stopped the walk
            if (!mine("clear", 60, phaseTick)) return false;
            phase = 0;
            still = 0;
        }
        if (p == null || dx * dx + dz * dz < within * within || elapsed() >= ticks(s, 400))
        {
            setKey(w, false);
            setKey(sp, false);
            if (keysDown.contains(jump)) keyEv(jump, false);
            return true;
        }
        if (phase == 0) { phase = 1; lastX = pl().posX; lastZ = pl().posZ; still = 0; phaseTick = Oracle.tick; }
        double yaw = Math.atan2(-dx, dz) * 180.0D / Math.PI;
        turnTo(yaw, s.has("pitch") ? s.get("pitch").getAsDouble() : 10.0D, 15.0D);
        setKey(w, Math.abs(wrap(yaw - pl().rotationYaw)) < 45.0D);
        if (sprint) setKey(sp, true);
        if (pl().onGround && (jumpy || pl().isCollidedHorizontally) && !keysDown.contains(jump)) tapKey(jump);
        if (Oracle.tick - phaseTick >= 10)
        {
            double moved = Math.hypot(pl().posX - lastX, pl().posZ - lastZ);
            still = moved < 0.1D ? still + 1 : 0;
            lastX = pl().posX;
            lastZ = pl().posZ;
            phaseTick = Oracle.tick;
            if (still >= 2)
            {
                // stuck: the block in the way at head height, else at the feet
                double r = Math.toRadians(pl().rotationYaw);
                int bx = (int)Math.floor(pl().posX - Math.sin(r) * 0.9D), bz = (int)Math.floor(pl().posZ + Math.cos(r) * 0.9D);
                int fy = feetY();
                int by = !mc.theWorld.isAirBlock(bx, fy + 1, bz) ? fy + 1 : !mc.theWorld.isAirBlock(bx, fy, bz) ? fy : -1;
                if (by >= 0)
                {
                    say("goto: stuck, clearing " + bx + "," + by + "," + bz);
                    marks.put("clear", new int[] {bx, by, bz});
                    setKey(w, false);
                    setKey(sp, false);
                    phase = 2;
                    phaseTick = Oracle.tick;
                }
            }
        }
        return false;
    }

    static boolean mine(String name, int limit, long since)
    {
        int[] b = marks.get(name);
        if (b == null) return true;
        boolean air = mc.theWorld.isAirBlock(b[0], b[1], b[2]);
        if (air || Oracle.tick - since >= limit)
        {
            if (buttonsDown[0]) mouseEv(0, false, 0);
            if (!air) say("mine " + name + ": gave up");
            return true;
        }
        double[] p = point(new com.google.gson.JsonPrimitive(name), null);
        double[] l = lookAt(p[0], p[1], p[2]);
        turnTo(l[0], l[1], 12.0D);
        boolean aimed = onTarget(l[0], l[1]);
        net.minecraft.util.MovingObjectPosition mo = mc.objectMouseOver;
        boolean on = mo != null && mo.typeOfHit == net.minecraft.util.MovingObjectPosition.MovingObjectType.BLOCK
            && mo.blockX == b[0] && mo.blockY == b[1] && mo.blockZ == b[2];
        if ((aimed || on) && !buttonsDown[0]) mouseEv(0, true, 0);
        return false;
    }

    static boolean use(JsonObject s)
    {
        String at = s.has("at") ? s.get("at").getAsString() : "top";
        double[] p = point(s.get("use"), at);
        if (p == null) return true;
        if (phase == 1) return elapsed() - (phaseTick - curTick) >= 2;
        double[] l = lookAt(p[0], p[1], p[2]);
        turnTo(l[0], l[1], 12.0D);
        if (onTarget(l[0], l[1]) || elapsed() >= ticks(s, 40))
        {
            net.minecraft.util.MovingObjectPosition mo = mc.objectMouseOver;
            say("use " + s.get("use") + ": the crosshair is on " + (mo == null ? "nothing" : mo.typeOfHit + " " + mo.blockX + ","
                + mo.blockY + "," + mo.blockZ + " side " + mo.sideHit) + ", holding "
                + (pl().getHeldItem() == null ? "nothing" : pl().getHeldItem().getDisplayName()));
            clickButton(1);
            phase = 1;
            phaseTick = Oracle.tick;
        }
        return false;
    }

    static final Set<String> tried = new HashSet<String>();

    /** {"place":BLOCKID,"on":[ids],"as":NAME}: the held block on the top of a
     * nearby ground block with air above, checked, another spot on a miss */
    static boolean place(JsonObject s)
    {
        int id = s.get("place").getAsInt();
        String name = s.get("as").getAsString();
        if (phase == 0)
        {
            if (elapsed() >= ticks(s, 200)) { say("place " + id + ": gave up"); return true; }
            net.minecraft.item.ItemStack held = pl().getHeldItem();
            if (held == null || net.minecraft.item.Item.getIdFromItem(held.getItem()) != id)
            {
                say("place " + id + ": not in the hand");
                return true;
            }
            JsonObject f = new JsonObject();
            f.add("find", s.has("on") ? s.get("on") : Rows.parse("{\"a\":[2,3]}").get("a"));
            f.addProperty("r", 4);
            JsonArray dy = new JsonArray();
            dy.add(new com.google.gson.JsonPrimitive(-1));
            dy.add(new com.google.gson.JsonPrimitive(-1));
            f.add("dy", dy);
            f.addProperty("min", 1.8D);
            f.addProperty("open", 1);
            f.addProperty("as", name + ".ground");
            find(f);
            int[] g = marks.get(name + ".ground");
            if (g == null) return true;
            marks.put(name, new int[] {g[0], g[1] + 1, g[2]});
            phase = 1;
        }
        if (phase == 1)
        {
            int[] g = marks.get(name + ".ground");
            double[] l = lookAt(g[0] + 0.5D, g[1] + 1.0D, g[2] + 0.5D);
            turnTo(l[0], l[1], 12.0D);
            if (!onTarget(l[0], l[1])) return false;
            clickButton(1);
            phase = 2;
            phaseTick = Oracle.tick;
            return false;
        }
        if (Oracle.tick - phaseTick < 4) return false;
        int[] b = marks.get(name);
        if (Block.getIdFromBlock(mc.theWorld.getBlock(b[0], b[1], b[2])) == id)
        {
            say("place " + id + ": at " + b[0] + "," + b[1] + "," + b[2] + " as " + name);
            return true;
        }
        say("place " + id + ": missed at " + b[0] + "," + b[1] + "," + b[2] + ", another spot");
        tried.add(b[0] + "," + (b[1] - 1) + "," + b[2]);
        phase = 0;
        return false;
    }

    /** {"open":NAME}: right-click the block until its screen is up */
    static boolean open(JsonObject s)
    {
        if (mc.currentScreen instanceof GuiContainer) { if (Oracle.tick - phaseTick >= 3) return true; return false; }
        if (elapsed() >= ticks(s, 120)) { say("open " + s.get("open") + ": gave up"); return true; }
        double[] p = point(s.get("open"), "face");
        if (p == null) return true;
        double[] l = lookAt(p[0], p[1], p[2]);
        turnTo(l[0], l[1], 12.0D);
        if (onTarget(l[0], l[1]) && (phase == 0 || Oracle.tick - phaseTick >= 10))
        {
            clickButton(1);
            phase = 1;
            phaseTick = Oracle.tick;
        }
        return false;
    }

    /** {"tunnel":N,"yaw":Y}: a two-high tunnel N blocks that way, walking into
     * each column once its two blocks are out (its drops come with it) */
    static boolean tunnel(JsonObject s)
    {
        int n = s.get("tunnel").getAsInt();
        double yaw = s.get("yaw").getAsDouble();
        int w = key("W");
        if (curFrames == 0) { marks.put("tstart", new int[] {(int)Math.floor(pl().posX), feetY(), (int)Math.floor(pl().posZ)}); phaseTick = Oracle.tick; }
        int col = phase / 3, sub = phase % 3;
        if (col >= n || elapsed() >= ticks(s, 300 * n))
        {
            setKey(w, false);
            return true;
        }
        int dx = -(int)Math.round(Math.sin(Math.toRadians(yaw))), dz = (int)Math.round(Math.cos(Math.toRadians(yaw)));
        int[] st = marks.get("tstart");
        int cx = st[0] + dx * (col + 1), cz = st[2] + dz * (col + 1), fy = st[1];
        if (sub < 2)
        {
            marks.put("tb", new int[] {cx, fy + 1 - sub, cz});
            if (mine("tb", 100, phaseTick)) { ++phase; phaseTick = Oracle.tick; }
            return false;
        }
        double tx = cx + 0.5D - pl().posX, tz = cz + 0.5D - pl().posZ;
        double[] l = lookAt(cx + 0.5D, pl().posY, cz + 0.5D);
        turnTo(l[0], 10.0D, 15.0D);
        if (tx * tx + tz * tz < 0.04D || Oracle.tick - phaseTick > 40)
        {
            setKey(w, false);
            ++phase;
            phaseTick = Oracle.tick;
            return false;
        }
        setKey(w, Math.abs(wrap(l[0] - pl().rotationYaw)) < 30.0D);
        return false;
    }

    static double runYaw;

    /** {"run":YAW,"ticks":N}: sprint-jump that way for N ticks, back the other way when stopped */
    static boolean run(JsonObject s)
    {
        int w = key("W"), sp = key("LCONTROL"), jump = key("SPACE");
        if (phase == 0) { runYaw = s.get("run").getAsDouble(); phase = 1; phaseTick = Oracle.tick; lastX = pl().posX; lastZ = pl().posZ; }
        if (elapsed() >= ticks(s, 100))
        {
            setKey(w, false);
            setKey(sp, false);
            return true;
        }
        turnTo(runYaw, 5.0D, 15.0D);
        setKey(w, true);
        setKey(sp, true);
        if (pl().onGround && !keysDown.contains(jump)) tapKey(jump);
        if (Oracle.tick - phaseTick >= 15)
        {
            if (Math.hypot(pl().posX - lastX, pl().posZ - lastZ) < 0.5D) runYaw += 180.0D;
            lastX = pl().posX;
            lastZ = pl().posZ;
            phaseTick = Oracle.tick;
        }
        return false;
    }

    static long lastHit;
    static int targetId, kills;

    static boolean attack(JsonObject s)
    {
        double r = s.has("r") ? s.get("r").getAsDouble() : 24.0D;
        // one target until it dies (a person chases the one they hit)
        Entity e = curFrames == 0 ? null : mc.theWorld.getEntityByID(targetId);
        if (e == null || e.isDead || (e instanceof EntityLivingBase && ((EntityLivingBase)e).getHealth() <= 0.0F))
        {
            if (curFrames == 0) kills = 0;
            if (e != null && curFrames > 0)
            {
                say("attack: " + e.getClass().getSimpleName() + " " + targetId + " died");
                if (s.has("kills") && ++kills >= s.get("kills").getAsInt())
                {
                    setKey(key("W"), false);
                    setKey(key("LCONTROL"), false);
                    return true;
                }
            }
            e = nearest(s.get("attack").getAsString(), r);
            if (e != null) targetId = e.getEntityId();
        }
        int w = key("W"), sp = key("LCONTROL");
        if (e == null || elapsed() >= ticks(s, 600))
        {
            setKey(w, false);
            setKey(sp, false);
            say("attack " + s.get("attack").getAsString() + ": " + (e == null ? "none left" : "gave up"));
            return true;
        }
        double d = pl().getDistanceToEntity(e);
        if (curFrames % 60 == 0)
            say(String.format("attack: %s %d at %.1f %.1f %.1f hp %.1f, %.1f away, the player at %.1f %.1f %.1f",
                e.getClass().getSimpleName(), e.getEntityId(), e.posX, e.posY, e.posZ,
                e instanceof EntityLivingBase ? ((EntityLivingBase)e).getHealth() : 0.0F, d, pl().posX, pl().boundingBox.minY, pl().posZ));
        if (d > 2.8D || phase == 2)
        {
            // the chase: the walk's own steering, jumps and clearing
            JsonObject g = new JsonObject();
            JsonArray p = new JsonArray();
            p.add(new com.google.gson.JsonPrimitive(e.posX));
            p.add(new com.google.gson.JsonPrimitive(e.posY));
            p.add(new com.google.gson.JsonPrimitive(e.posZ));
            g.add("goto", p);
            g.addProperty("within", 2.6D);
            g.addProperty("ticks", 100000);
            g.addProperty("sprint", 1);
            gotoStep(g);
            return false;
        }
        setKey(w, false);
        setKey(sp, false);
        phase = 0;
        double[] l = lookAt(e.posX, e.boundingBox.minY + e.height * 0.5D, e.posZ);
        turnTo(l[0], l[1], 20.0D);
        boolean on = mc.objectMouseOver != null && mc.objectMouseOver.entityHit == e;
        if ((on || onTarget(l[0], l[1])) && Oracle.tick - lastHit >= 12)
        {
            clickButton(0);
            lastHit = Oracle.tick;
        }
        return false;
    }

    static boolean slotStep(JsonObject s)
    {
        if (!(mc.currentScreen instanceof GuiContainer)) { say("slot step without a screen: " + s); return true; }
        int b = s.has("button") ? s.get("button").getAsInt() : 0;
        String pace = s.has("pace") ? s.get("pace").getAsString() : "tick";
        if ("burst".equals(pace))
        {
            // a fast hand: the move, the press and the release in one frame
            boolean done = false;
            for (int i = 0; i < 6 && !done; ++i) done = slotPhase(s, b);
            return true;
        }
        // one phase a tick: a hand's pace, each press and release its own tick
        // ("pace":"frame": a frame apart)
        if (phase > 0 && Oracle.tick == phaseTick && !"frame".equals(pace)) return false;
        phaseTick = Oracle.tick;
        return slotPhase(s, b);
    }

    static int off(JsonObject s, int i)
    {
        return s.has("off") ? s.getAsJsonArray("off").get(i).getAsInt() : 0;
    }

    static int glideX0, glideY0;

    /** {"glide":S|[GX,GY],"frames":N}: the pointer there over N frames, one motion a frame */
    static boolean glide(JsonObject s)
    {
        if (!(mc.currentScreen instanceof GuiContainer)) return true;
        int sf = new ScaledResolution(mc, mc.displayWidth, mc.displayHeight).getScaleFactor();
        int sh = mc.displayHeight / sf;
        if (phase == 0) { glideX0 = ptrX; glideY0 = ptrY; phase = 1; }
        int gx, gy;
        if (s.get("glide").isJsonArray())
        {
            gx = s.getAsJsonArray("glide").get(0).getAsInt();
            gy = s.getAsJsonArray("glide").get(1).getAsInt();
        }
        else
        {
            int n = slotIndex(s.get("glide"));
            int[] c = n < 0 ? null : slotAt(n);
            if (c == null) return true;
            gx = c[0];
            gy = c[1];
        }
        int tx = (gx + off(s, 0)) * sf + 1, ty = (sh - 1 - gy - off(s, 1)) * sf + 1;
        int n = Math.max(1, s.has("frames") ? s.get("frames").getAsInt() : 4);
        int k = Math.min(n, curFrames + 1);
        ptrX = glideX0 + (tx - glideX0) * k / n;
        ptrY = glideY0 + (ty - glideY0) * k / n;
        mouseEv(-1, false, 0);
        return k >= n;
    }

    /** One phase of a slot step; true when the step is finished. */
    static boolean slotPhase(JsonObject s, int b)
    {
        // a frame to move the pointer, then the press or release
        if (phase == 0)
        {
            if (s.has("outside")) pointTo(4, 4);
            else
            {
                int n = slotIndex(s.has("slot") ? s.get("slot") : s.has("slotdown") ? s.get("slotdown")
                    : s.has("slotup") ? s.get("slotup") : s.has("slotmove") ? s.get("slotmove") : s.get("dbl"));
                int[] c = n < 0 ? null : slotAt(n);
                if (c == null) { say("no slot for " + s); return true; }
                pointTo(c[0] + off(s, 0), c[1] + off(s, 1));
            }
            phase = 1;
            return s.has("slotmove");
        }
        if (phase == 1)
        {
            if (s.has("slotdown")) { mouseEv(b, true, 0); phase = 5; return s.has("nomove"); }
            if (s.has("slotup")) { mouseEv(b, false, 0); return true; }
            mouseEv(b, true, 0);
            phase = 2;
            return false;
        }
        if (phase == 2)
        {
            mouseEv(b, false, 0);
            if (!s.has("dbl")) return true;
            phase = 3;
            return false;
        }
        if (phase == 5)
        {
            // the pointer moves a little inside the pressed slot, as a hand does
            ptrX += 2;
            mouseEv(-1, false, 0);
            return true;
        }
        if (phase == 3) { mouseEv(0, true, 0); phase = 4; return false; }
        mouseEv(0, false, 0);
        return true;
    }

    static boolean digdown(JsonObject s)
    {
        int n = s.get("digdown").getAsInt();
        if (phase == 0) { marks.put("digstart", new int[] {0, feetY(), 0}); phase = 1; }
        int start = marks.get("digstart")[1];
        if ((feetY() <= start - n && pl().onGround) || elapsed() >= ticks(s, 120 * n))
        {
            if (buttonsDown[0]) mouseEv(0, false, 0);
            return true;
        }
        turnTo(pl().rotationYaw, 90.0D, 20.0D);
        if (!buttonsDown[0] && pl().rotationPitch > 80.0F) mouseEv(0, true, 0);
        return false;
    }

    static boolean pillar(JsonObject s)
    {
        int n = s.get("pillar").getAsInt();
        int jump = key("SPACE");
        if (elapsed() >= ticks(s, 60 * n) || phase >= 2 * n) return pl().onGround || elapsed() >= ticks(s, 60 * n);
        if (!turnTo(pl().rotationYaw, 90.0D, 20.0D) && pl().rotationPitch < 85.0F) return false;
        if (phase % 2 == 0)
        {
            if (pl().onGround && !keysDown.contains(jump))
            {
                marks.put("pillar", new int[] {0, feetY(), 0});
                tapKey(jump);
                ++phase;
            }
            return false;
        }
        int base = marks.get("pillar")[1];
        if (pl().boundingBox.minY > base + 1.05D) { clickButton(1); ++phase; }
        return false;
    }
}
