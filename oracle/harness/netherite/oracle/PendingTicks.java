package netherite.oracle;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.TreeSet;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.chunk.Chunk;

/**
 * WorldServer.getPendingBlockUpdates(chunk, false), the pending ticks a
 * chunk's save writes: vanilla walks the world's whole pending tick set for
 * each chunk it saves, and after a fresh world's spawn area the server
 * unloads (and saves) the 336 chunks outside 128 blocks of spawn over its
 * first ticks, 100 a tick: a third of a warm pool member's server thread
 * in a short seed-start replay (JFR, act2-bisect, lane/replaylat).
 *
 * The answer is the same list: the set's entries in its order whose x is in
 * [16 cx - 2, 16 cx + 16) and z in [16 cz - 2, 16 cz + 16) (vanilla's window,
 * two blocks into the chunks at lower x and z), in a new list, or null when
 * there are none. The first call on a set as it stands scans it as vanilla
 * does; a second call before the set changes (its TreeMap's modCount, and
 * the set itself) indexes every entry under each window it falls in (at
 * most four), in the set's order, and that call and the ones after it until
 * the set changes read the index. The hook only runs while no tick is
 * being processed (pendingTickListEntriesThisTick empty: vanilla then walks
 * nothing else) and never for a remove, which changes the set.
 */
public final class PendingTicks
{
    private PendingTicks() {}

    private static final Field SET_MAP, MAP_MOD;
    static
    {
        try
        {
            SET_MAP = TreeSet.class.getDeclaredField("m");
            SET_MAP.setAccessible(true);
            MAP_MOD = java.util.TreeMap.class.getDeclaredField("modCount");
            MAP_MOD.setAccessible(true);
        }
        catch (NoSuchFieldException e) { throw new IllegalStateException(e); }
    }

    /** The set the index describes, its modCount then, and the index (null until a second call). */
    private static TreeSet<?> set;
    private static int mod;
    private static HashMap<Long, ArrayList<NextTickListEntry>> index;

    public static boolean on()
    {
        return Oracle.active();
    }

    public static synchronized List<NextTickListEntry> of(TreeSet<?> s, Chunk c)
    {
        int m = modCount(s);
        if (s != set || m != mod)
        {
            set = s;
            mod = m;
            index = null;
            return scan(s, c.xPosition, c.zPosition);
        }
        if (index == null) index = build(s);
        ArrayList<NextTickListEntry> b = index.get(key(c.xPosition, c.zPosition));
        return b == null ? null : new ArrayList<NextTickListEntry>(b);
    }

    private static int modCount(TreeSet<?> s)
    {
        try { return MAP_MOD.getInt(SET_MAP.get(s)); }
        catch (IllegalAccessException e) { throw new IllegalStateException(e); }
    }

    private static List<NextTickListEntry> scan(TreeSet<?> s, int cx, int cz)
    {
        int x0 = (cx << 4) - 2, x1 = x0 + 16 + 2, z0 = (cz << 4) - 2, z1 = z0 + 16 + 2;
        ArrayList<NextTickListEntry> r = null;
        for (Iterator<?> i = s.iterator(); i.hasNext();)
        {
            NextTickListEntry e = (NextTickListEntry)i.next();
            if (e.xCoord >= x0 && e.xCoord < x1 && e.zCoord >= z0 && e.zCoord < z1)
            {
                if (r == null) r = new ArrayList<NextTickListEntry>();
                r.add(e);
            }
        }
        return r;
    }

    private static HashMap<Long, ArrayList<NextTickListEntry>> build(TreeSet<?> s)
    {
        HashMap<Long, ArrayList<NextTickListEntry>> ix = new HashMap<Long, ArrayList<NextTickListEntry>>();
        for (Iterator<?> i = s.iterator(); i.hasNext();)
        {
            NextTickListEntry e = (NextTickListEntry)i.next();
            // the windows holding x: chunk x >> 4, and the next one up when x is in its last two blocks
            int ax = e.xCoord >> 4, bx = (e.xCoord + 2) >> 4, az = e.zCoord >> 4, bz = (e.zCoord + 2) >> 4;
            add(ix, ax, az, e);
            if (bx != ax) add(ix, bx, az, e);
            if (bz != az) add(ix, ax, bz, e);
            if (bx != ax && bz != az) add(ix, bx, bz, e);
        }
        return ix;
    }

    private static void add(HashMap<Long, ArrayList<NextTickListEntry>> ix, int cx, int cz, NextTickListEntry e)
    {
        Long k = key(cx, cz);
        ArrayList<NextTickListEntry> b = ix.get(k);
        if (b == null) ix.put(k, b = new ArrayList<NextTickListEntry>());
        b.add(e);
    }

    private static Long key(int cx, int cz)
    {
        return Long.valueOf(((long)cx << 32) ^ (cz & 0xffffffffL));
    }
}
