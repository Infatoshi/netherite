package netherite.oracle;

import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.Display;

/**
 * The host mouse, as a pool JVM hands it to each run. An agent run never
 * moves the mouse, but a GUI draws its hover highlight where LWJGL says the
 * pointer is, and LWJGL reads the X pointer back whenever the grab changes
 * (opening a container ungrabs it and warps it to the window centre). A
 * fresh JVM starts on a fresh Xvfb, pointer at the screen centre; a pool
 * JVM's pointer is wherever the last run's warps left it, and the inventory
 * frame of smoke-s1 drew one hover highlight less (pool job 2 onwards). So
 * between runs the X pointer is warped back to where it was at the pristine
 * point, and LWJGL's Mouse statics and its LinuxMouse (last position, deltas,
 * buttons, event queues) are written back as they were.
 */
final class PoolMouse
{
    private PoolMouse() {}

    static int x0, y0;
    static final List<Object[]> saved = new ArrayList<Object[]>(); // {owner, Field, value}
    static final List<Object[]> buffers = new ArrayList<Object[]>(); // {ByteBuffer, bytes, position, limit}
    static Object linuxMouse;

    static void record() throws Exception
    {
        x0 = Mouse.getX();
        y0 = Mouse.getY();
        saveStatics(Mouse.class);
        Field impl = Mouse.class.getDeclaredField("implementation");
        impl.setAccessible(true);
        Object display = impl.get(null);
        Field mf = display.getClass().getDeclaredField("mouse");
        mf.setAccessible(true);
        linuxMouse = mf.get(display);
        saveFields(linuxMouse);
        for (String n : new String[] {"buttons", "readBuffer"})
        {
            Field f = Mouse.class.getDeclaredField(n);
            f.setAccessible(true);
            saveBuffer((ByteBuffer)f.get(null));
        }
        Field q = linuxMouse.getClass().getDeclaredField("event_queue");
        q.setAccessible(true);
        Object queue = q.get(linuxMouse);
        saveFields(queue);
        for (Field f : queue.getClass().getDeclaredFields())
        {
            if (f.getType() == ByteBuffer.class)
            {
                f.setAccessible(true);
                saveBuffer((ByteBuffer)f.get(queue));
            }
        }
        Field b = linuxMouse.getClass().getDeclaredField("buttons");
        b.setAccessible(true);
        saved.add(new Object[] {b.get(linuxMouse), null, ((byte[])b.get(linuxMouse)).clone()});
    }

    static void saveStatics(Class<?> c) throws Exception
    {
        for (Field f : c.getDeclaredFields())
        {
            int m = f.getModifiers();
            if (!Modifier.isStatic(m) || Modifier.isFinal(m) || !f.getType().isPrimitive()) continue;
            f.setAccessible(true);
            saved.add(new Object[] {null, f, f.get(null)});
        }
    }

    static void saveFields(Object o) throws Exception
    {
        for (Field f : o.getClass().getDeclaredFields())
        {
            int m = f.getModifiers();
            if (Modifier.isStatic(m) || Modifier.isFinal(m) || !f.getType().isPrimitive()) continue;
            f.setAccessible(true);
            saved.add(new Object[] {o, f, f.get(o)});
        }
    }

    static void saveBuffer(ByteBuffer b)
    {
        ByteBuffer d = b.duplicate();
        d.clear();
        byte[] bytes = new byte[d.capacity()];
        d.get(bytes);
        buffers.add(new Object[] {b, bytes, b.position(), b.limit()});
    }

    /** Between runs, on the client thread, with no world. */
    static void restore() throws Exception
    {
        if (Mouse.isGrabbed()) Mouse.setGrabbed(false);
        Mouse.setCursorPosition(x0, y0);
        // the warp's motion reaches LWJGL as an X event; take it before writing the state back
        Field lx = linuxMouse.getClass().getDeclaredField("last_x"), ly = linuxMouse.getClass().getDeclaredField("last_y");
        lx.setAccessible(true);
        ly.setAccessible(true);
        long until = System.nanoTime() + 200000000L;
        do
        {
            Display.processMessages();
            if (lx.getInt(linuxMouse) == x0 && ly.getInt(linuxMouse) == y0) break;
            Thread.sleep(1L);
        }
        while (System.nanoTime() < until);
        for (Object[] s : saved)
        {
            if (s[1] == null) System.arraycopy(s[2], 0, s[0], 0, ((byte[])s[2]).length);
            else ((Field)s[1]).set(s[0], s[2]);
        }
        for (Object[] s : buffers)
        {
            ByteBuffer b = (ByteBuffer)s[0], d = b.duplicate();
            d.clear();
            d.put((byte[])s[1]);
            b.limit((Integer)s[3]);
            b.position((Integer)s[2]);
        }
    }
}
