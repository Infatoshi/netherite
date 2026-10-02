package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.List;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.ContainerFurnace;
import net.minecraft.inventory.ContainerPlayer;
import net.minecraft.inventory.ContainerWorkbench;
import net.minecraft.inventory.InventoryCrafting;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/**
 * Per-tick state rows: capture, digest, tape output, and replay comparison.
 * Captured on the client thread after a tick pair, while the server thread is
 * parked, so server state is read without races.
 */
public final class Rows
{
    static PrintWriter tape;
    static File tapeFile;
    static JsonObject header;   // the tape's first line, kept so setup can join it
    static BufferedReader ref;
    static JsonObject lastRow;
    static JsonObject pending;

    // server-world block writes, folded per tick and chained across ticks
    static long blkHash = 0xcbf29ce484222325L;
    static int blkCount;

    /**
     * The write listener a probe installs to see block changes one at a time
     * (FeatureProbe.record). Null outside a probe.
     */
    public static WriteListener writeListener;

    public interface WriteListener
    {
        /**
         * The write with its setBlock flags, what the populate probe records.
         * A listener that only wants the write implements the five-argument
         * form; this one forwards to it.
         */
        default void onWrite(World w, int x, int y, int z, int id, int meta, int flags)
        {
            onWrite(w, x, y, z, id, meta);
        }

        default void onWrite(World w, int x, int y, int z, int id, int meta) {}

        /**
         * World.setBlock's attempt before the chunk decides anything, and the
         * matching return. The populate probe records these (a no-op attempt
         * still runs the light pass); every other listener ignores them.
         */
        default void onAttempt(World w, int x, int y, int z, int id, int meta, int flags) {}

        default void onAttemptEnd(World w, int x, int y, int z, int id, int meta, int flags) {}
    }

    private Rows() {}

    // ---------------------------------------------------------------- tape io

    static void open(JsonObject h) throws IOException
    {
        if (Oracle.tapePath == null) return;
        header = h;
        tapeFile = new File(Oracle.tapePath);
        if (tapeFile.getParentFile() != null) tapeFile.getParentFile().mkdirs();
        tape = writer(false);
        tape.println(header.toString());
        tape.flush();
    }

    private static PrintWriter writer(boolean append) throws IOException
    {
        return new PrintWriter(new OutputStreamWriter(new AsyncOut(new FileOutputStream(tapeFile, append)), "UTF-8"));
    }

    /**
     * The tape file's writes on a thread of their own, in order: a write that
     * blocks on a busy disk does not hold up the tick. close() returns once
     * every byte is written and the file is closed.
     */
    static final class AsyncOut extends java.io.OutputStream
    {
        private static final byte[] CLOSE = new byte[0];
        private final FileOutputStream file;
        private final java.util.concurrent.LinkedBlockingQueue<byte[]> queue = new java.util.concurrent.LinkedBlockingQueue<byte[]>();
        private final Thread thread;
        private volatile IOException failed;
        private boolean closed;

        AsyncOut(FileOutputStream f)
        {
            file = f;
            thread = new Thread(new Runnable() { public void run() { drain(); } }, "Oracle Tape Writer");
            thread.setDaemon(true);
            thread.start();
        }

        private void drain()
        {
            try
            {
                while (true)
                {
                    byte[] b = queue.take();
                    if (b == CLOSE) break;
                    if (failed == null) file.write(b);
                }
            }
            catch (InterruptedException e) {}
            catch (IOException e) { failed = e; }
            try { file.close(); }
            catch (IOException e) { if (failed == null) failed = e; }
        }

        @Override public void write(int v) throws IOException
        {
            write(new byte[] {(byte)v}, 0, 1);
        }

        @Override public void write(byte[] b, int off, int len) throws IOException
        {
            if (failed != null) throw failed;
            if (len > 0) queue.add(java.util.Arrays.copyOfRange(b, off, off + len));
        }

        @Override public void close() throws IOException
        {
            if (closed) return;
            closed = true;
            queue.add(CLOSE);
            try { thread.join(); }
            catch (InterruptedException e) { throw new IOException(e); }
            if (failed != null) throw failed;
        }
    }

    /**
     * A setup command that ran between the join and the first replayed row
     * (MobFree is one) is part of the world the tape's rows describe, but the
     * header is written before the join. Record it there, once, so a replay of
     * this tape applies it at the same tick: the header's first line is
     * rewritten in place and the writer reopened in append mode. Only the join
     * prologue rows exist at that point, so the file is a few hundred bytes.
     */
    static synchronized void noteSetup(String cls, JsonObject cmd, long tick) throws IOException
    {
        if (tape == null) return;
        if (setup == null)
        {
            setup = new JsonArray();
            header.add("setup", setup);
        }
        JsonObject e = new JsonObject();
        e.addProperty("class", cls);
        e.addProperty("tick", tick);
        JsonObject c = parse(cmd.toString());
        c.remove("cmd");
        c.remove("class");
        e.add("cmd", c);
        setup.add(e);
        rewriteHeader(tapeFile);
    }

    static JsonArray setup;

    private static void rewriteHeader(File f) throws IOException
    {
        tape.flush();
        tape.close();
        StringBuilder body = new StringBuilder();
        BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
        String line;
        boolean first = true;

        while ((line = r.readLine()) != null)
        {
            if (first) { first = false; continue; }
            body.append(line).append('\n');
        }

        r.close();
        PrintWriter w = writer(false);
        w.println(header.toString());
        w.print(body);
        w.flush();
        w.close();
        tape = writer(true);
    }

    /**
     * Writes the row and compares it with the tape. A row whose entity digests
     * are still being computed (a RowHash job) is held and written by the
     * next flush: at the next capture, before any reply or command, and at close.
     */
    static synchronized void emit(JsonObject row)
    {
        if (held != null && held.row == row)
        {
            held.emit = true;
            if (held.job == null) flush();
            return;
        }
        write(row, Check.expect, Check.expectText);
        Check.expect = null;
    }

    private static void write(JsonObject row, JsonObject want, String wantText)
    {
        lastRow = row;
        String text = tape != null || want != null ? row.toString() : null;
        if (tape != null)
        {
            tape.println(text);
            if (Oracle.tick % 20 == 0) tape.flush();
        }
        // the same text as the tape's line: every leaf is equal, no diff to find
        if (want != null && text.equals(wantText)) return;
        Check.compare(want, row);
    }

    /** The captured row whose server entity digests a RowHash job is still computing. */
    static final class Held
    {
        JsonObject row, want;
        String wantText;
        RowHash.Job job;
        List<Entity> ents;
        int nEnts, spAt;
        boolean emit;
    }

    static Held held;

    /**
     * The held row's digests are in the row, and the row is written if it was
     * emitted. A row not emitted yet (a frame row before its frame) stays
     * held, complete, and emit writes it.
     */
    static synchronized void flush()
    {
        Held h = held;
        if (h == null) return;
        if (h.job != null)
        {
            long[] hashes = h.job.awaitHashed();
            h.job = null;
            JsonObject d = h.row.getAsJsonObject("d");
            if (h.spAt >= 0) d.addProperty("sp", hex(hashes[h.spAt]));
            long entH = 0;
            for (int i = 0; i < h.nEnts; ++i)
            {
                entH = entH * 1000003L + h.ents.get(i).getEntityId();
                entH = entH * 1000003L + hashes[i];
            }
            d.addProperty("ents", hex(entH));
        }
        if (h.emit)
        {
            held = null;
            write(h.row, h.want, h.wantText);
        }
    }

    static synchronized void close()
    {
        flush();
        if (pending != null) { emit(pending); pending = null; }
        flush();
        if (tape != null) { tape.flush(); tape.close(); tape = null; }
        if (Det.JERR != null) Det.JERR.flush();
    }

    // ---------------------------------------------------------------- hooks

    /** Patched World.setBlock / setBlockMetadataWithNotify, server worlds only. */
    public static void onBlock(World w, int x, int y, int z, int id, int meta)
    {
        onBlock(w, x, y, z, id, meta, 0);
    }

    /**
     * World.setBlock's attempt, before the chunk write decides whether anything
     * changes, and the matching return. A no-op attempt still runs the light
     * pass, and a structure generator makes many of them, so the populate probe
     * records them; the listener's default methods make them free for the
     * probes that do not.
     */
    public static void onAttempt(World w, int x, int y, int z, int id, int meta, int flags)
    {
        WriteListener l = writeListener;

        if (l != null) l.onAttempt(w, x, y, z, id, meta, flags);
    }

    public static void onAttemptEnd(World w, int x, int y, int z, int id, int meta, int flags)
    {
        WriteListener l = writeListener;

        if (l != null) l.onAttemptEnd(w, x, y, z, id, meta, flags);
    }

    /** The populate lane's recording: the same write with its setBlock flags. */
    public static void onBlock(World w, int x, int y, int z, int id, int meta, int flags)
    {
        long h = blkHash;
        h = (h ^ w.provider.dimensionId) * 0x100000001b3L;
        h = (h ^ x) * 0x100000001b3L;
        h = (h ^ y) * 0x100000001b3L;
        h = (h ^ z) * 0x100000001b3L;
        h = (h ^ id) * 0x100000001b3L;
        h = (h ^ meta) * 0x100000001b3L;
        blkHash = h;
        ++blkCount;
        if (writeListener != null) writeListener.onWrite(w, x, y, z, id, meta, flags);
    }

    // ---------------------------------------------------------------- capture

    /** [item,count,damage], or null when the stack is null. ContainersProbe's
     * stack encoding; these stacks never carry NBT. */
    static JsonArray stackJson(ItemStack s)
    {
        if (s == null) return null;

        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(Integer.valueOf(Item.getIdFromItem(s.getItem()))));
        a.add(new JsonPrimitive(Integer.valueOf(s.stackSize)));
        a.add(new JsonPrimitive(Integer.valueOf(s.getItemDamage())));
        return a;
    }

    /** The non-empty slots of a player inventory, main 0..35 then armor 36..39,
     * each [slot, item, count, damage]. */
    static JsonArray invJson(InventoryPlayer inv)
    {
        JsonArray a = new JsonArray();

        for (int i = 0; i < inv.mainInventory.length; ++i)
        {
            if (inv.mainInventory[i] == null) continue;

            JsonArray s = new JsonArray();
            s.add(new JsonPrimitive(Integer.valueOf(i)));
            s.add(new JsonPrimitive(Integer.valueOf(Item.getIdFromItem(inv.mainInventory[i].getItem()))));
            s.add(new JsonPrimitive(Integer.valueOf(inv.mainInventory[i].stackSize)));
            s.add(new JsonPrimitive(Integer.valueOf(inv.mainInventory[i].getItemDamage())));
            a.add(s);
        }

        for (int i = 0; i < inv.armorInventory.length; ++i)
        {
            if (inv.armorInventory[i] == null) continue;

            JsonArray s = new JsonArray();
            s.add(new JsonPrimitive(Integer.valueOf(36 + i)));
            s.add(new JsonPrimitive(Integer.valueOf(Item.getIdFromItem(inv.armorInventory[i].getItem()))));
            s.add(new JsonPrimitive(Integer.valueOf(inv.armorInventory[i].stackSize)));
            s.add(new JsonPrimitive(Integer.valueOf(inv.armorInventory[i].getItemDamage())));
            a.add(s);
        }

        return a;
    }

    /** The server's open window: null while openContainer is the player's own
     * inventory container, else {"id", "kind", "grid", "res"}. The grid is the
     * craft matrix (2x2 for the player kind, 3x3 for a workbench, one entry per
     * slot), res the craft result stack. */
    static JsonObject winJson(EntityPlayerMP sp)
    {
        Container oc = sp.openContainer;

        if (oc == null || oc == sp.inventoryContainer) return null;

        JsonObject o = new JsonObject();
        o.addProperty("id", oc.windowId);
        o.addProperty("kind", oc.getClass().getSimpleName());

        InventoryCrafting grid = null;

        if (oc instanceof ContainerPlayer) grid = ((ContainerPlayer)oc).craftMatrix;
        else if (oc instanceof ContainerWorkbench) grid = ((ContainerWorkbench)oc).craftMatrix;

        if (grid != null)
        {
            JsonArray g = new JsonArray();

            for (int i = 0; i < grid.getSizeInventory(); ++i)
            {
                JsonArray s = stackJson(grid.getStackInSlot(i));

                if (s == null) g.add(com.google.gson.JsonNull.INSTANCE);
                else g.add(s);
            }

            o.add("grid", g);
        }

        if (oc.getSlot(0) != null)
        {
            JsonArray res = stackJson(oc.getSlot(0).getStack());

            if (res != null) o.add("res", res);
        }

        return o;
    }

    /** The client's open ContainerFurnace progress bars: the fields of the
     * client's own TileEntityFurnace that only the S31s write
     * (updateProgressBar), as [cook time field_145961_j, burn time
     * field_145956_a, fuel total field_145963_i]; null for any other
     * container. */
    static JsonArray furJson(Container oc)
    {
        if (!(oc instanceof ContainerFurnace)) return null;
        net.minecraft.tileentity.TileEntityFurnace f;
        try { f = (net.minecraft.tileentity.TileEntityFurnace)Snapshot.field(oc, "furnace").get(oc); }
        catch (IllegalAccessException e) { throw new IllegalStateException(e); }
        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(Integer.valueOf(f.field_145961_j)));
        a.add(new JsonPrimitive(Integer.valueOf(f.field_145956_a)));
        a.add(new JsonPrimitive(Integer.valueOf(f.field_145963_i)));
        return a;
    }

    static JsonObject capture(Minecraft mc, Act act)
    {
        if (Det.JRAND)
        {
            Det.JERR.println("JRANDMARK t=" + Oracle.tick);
            Det.JERR.flush();
        }
        JsonObject r = new JsonObject();
        r.addProperty("t", Oracle.tick);
        if (act != null)
        {
            JsonObject a = act.toJson();
            if (a.entrySet().size() > 0) r.add("act", a);
        }
        JsonObject d = new JsonObject();

        // the previous row, if held, is complete and written before this one
        flush();
        if (Prof.on) Prof.lap(Prof.FLUSHPREV);

        // The server worlds' entities in list order, then the server player
        // unless the lists already hold it: the list RowHash's job took when
        // the server's tick ended, or taken here. The job's digests complete
        // the row at the next flush (async: the tick loop) or here.
        EntityClientPlayerMP p = mc.thePlayer;
        IntegratedServer s = mc.getIntegratedServer();
        boolean server = s != null && s.worldServers != null && s.worldServers.length > 0 && s.worldServers[0] != null;
        RowHash.Job job = server ? RowHash.take() : null;
        List<Entity> hashed;
        int nEnts, spAt = -1;
        if (job != null)
        {
            hashed = job.es;
            nEnts = job.nEnts;
            spAt = job.spAt;
        }
        else
        {
            hashed = new java.util.ArrayList<Entity>();
            EntityPlayerMP sp0 = null;
            if (server)
            {
                for (WorldServer x : s.worldServers)
                {
                    if (x == null) continue;
                    for (Object o : x.loadedEntityList) hashed.add((Entity)o);
                }
                List players = s.getConfigurationManager().playerEntityList;
                if (!players.isEmpty()) sp0 = (EntityPlayerMP)players.get(0);
            }
            nEnts = hashed.size();
            if (sp0 != null)
            {
                for (int i = 0; i < nEnts && spAt < 0; ++i) if (hashed.get(i) == sp0) spAt = i;
                if (spAt < 0) { spAt = hashed.size(); hashed.add(sp0); }
            }
        }
        boolean async = job != null && async();
        long[] hashes = async ? null : job != null ? job.awaitHashed() : nbtLongs(hashed);
        long cpHash = p != null ? RowHash.clientPlayer(p) : 0L;

        if (p != null)
        {
            JsonObject cp = new JsonObject();
            cp.addProperty("x", p.posX);
            cp.addProperty("y", p.posY);
            cp.addProperty("z", p.posZ);
            cp.addProperty("mx", p.motionX);
            cp.addProperty("my", p.motionY);
            cp.addProperty("mz", p.motionZ);
            cp.addProperty("yaw", p.rotationYaw);
            cp.addProperty("pitch", p.rotationPitch);
            cp.addProperty("og", p.onGround ? 1 : 0);
            cp.addProperty("fd", p.fallDistance);
            cp.addProperty("hp", p.getHealth());
            cp.addProperty("food", p.getFoodStats().getFoodLevel());
            cp.addProperty("hb", p.inventory.currentItem);
            JsonArray cinv = invJson(p.inventory);
            cp.add("inv", cinv);
            JsonElement ccur = stackJson(p.inventory.getItemStack());
            if (ccur != null) cp.add("cur", ccur);
            if (mc.currentScreen != null) cp.addProperty("gui", mc.currentScreen.getClass().getSimpleName());
            JsonArray fur = furJson(p.openContainer);
            if (fur != null) cp.add("fur", fur);
            r.add("cp", cp);
            d.addProperty("cp", hex(cpHash));
        }

        if (server)
        {
            WorldServer ws = s.worldServers[0];
            List players = s.getConfigurationManager().playerEntityList;
            if (!players.isEmpty())
            {
                EntityPlayerMP sp = (EntityPlayerMP)players.get(0);
                JsonObject o = new JsonObject();
                o.addProperty("x", sp.posX);
                o.addProperty("y", sp.posY);
                o.addProperty("z", sp.posZ);
                o.addProperty("hp", sp.getHealth());
                o.addProperty("food", sp.getFoodStats().getFoodLevel());
                o.addProperty("sat", sp.getFoodStats().getSaturationLevel());
                o.addProperty("xp", sp.experienceTotal);
                o.addProperty("dim", sp.dimension);
                JsonArray sinv = invJson(sp.inventory);
                o.add("inv", sinv);
                JsonElement scur = stackJson(sp.inventory.getItemStack());
                if (scur != null) o.add("cur", scur);
                JsonObject win = winJson(sp);
                o.add("win", win != null ? win : com.google.gson.JsonNull.INSTANCE);
                r.add("sp", o);
                d.addProperty("sp", async ? "" : hex(hashes[spAt]));
            }
            JsonObject w = new JsonObject();
            w.addProperty("wt", ws.getWorldTime());
            w.addProperty("tt", ws.getTotalWorldTime());
            long entH = 0;
            int ents = nEnts;
            for (int i = 0; !async && i < nEnts; ++i)
            {
                entH = entH * 1000003L + hashed.get(i).getEntityId();
                entH = entH * 1000003L + hashes[i];
            }
            w.addProperty("ents", ents);
            if (Oracle.detail)
            {
                com.google.gson.JsonArray xs = new com.google.gson.JsonArray();
                // the same lists in the same order as hashed: its digests are these entities' NBT now
                int k = 0;
                for (WorldServer x : s.worldServers)
                {
                    if (x == null) continue;
                    for (Object o : x.loadedEntityList)
                    {
                        Entity e = (Entity)o;
                        long h = hashes != null && k < nEnts && hashed.get(k) == e ? hashes[k] : nbtLong(e);
                        ++k;
                        xs.add(new com.google.gson.JsonPrimitive(e.getEntityId() + ":" + e.getClass().getSimpleName() + ":" + hex(h)));
                    }
                }
                JsonObject x = new JsonObject();
                x.add("sents", xs);
                if (System.getProperty("netherite.enbt", "0").equals("1"))
                {
                    com.google.gson.JsonArray es = new com.google.gson.JsonArray();
                    for (WorldServer xw : s.worldServers)
                    {
                        if (xw == null) continue;
                        for (Object o : xw.loadedEntityList)
                        {
                            Entity e = (Entity)o;
                            net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
                            e.writeToNBT(tag);
                            es.add(new com.google.gson.JsonPrimitive(e.getEntityId() + ":" + StructuresProbe.canon(tag).toString()));
                        }
                    }
                    x.add("enbt", es);
                }
                if (!Oracle.detailNbt.isEmpty())
                {
                    JsonObject nb = new JsonObject();
                    for (WorldServer xw : s.worldServers)
                    {
                        if (xw == null) continue;
                        for (Object o : xw.loadedEntityList)
                        {
                            Entity e = (Entity)o;
                            if (!Oracle.detailNbt.contains(e.getEntityId())) continue;
                            net.minecraft.nbt.NBTTagCompound t = new net.minecraft.nbt.NBTTagCompound();
                            e.writeToNBT(t);
                            nb.addProperty(Integer.toString(e.getEntityId()), t.toString());
                        }
                    }
                    x.add("nbt", nb);
                }
                x.addProperty("nextId", Det.nextId[Det.SERVER]);
                x.addProperty("cnextId", Det.nextId[Det.CLIENT]);
                x.addProperty("fx", mc.effectRenderer.getStatistics());
                x.addProperty("cents", mc.theWorld != null ? mc.theWorld.loadedEntityList.size() : -1);
                com.google.gson.JsonArray cn = new com.google.gson.JsonArray();
                for (String sgn : Det.clientNews) cn.add(new com.google.gson.JsonPrimitive(sgn));
                Det.clientNews.clear();
                x.add("cnew", cn);
                r.add("x", x);
            }
            w.addProperty("bc", blkCount);
            r.add("w", w);
            d.addProperty("ents", async ? "" : hex(entH));
            // RNG streams, one digest per owner so a divergence names its source
            long rng = 0;
            for (WorldServer x : s.worldServers)
            {
                if (x == null) continue;
                rng = rng * 31 + Det.state(x.rand);
                rng = rng * 31 + updateLCG(x);
            }
            d.addProperty("sw", hex(rng));
            d.addProperty("sseed", hex(Det.seederState(Det.SERVER)));
            d.addProperty("smath", hex(Det.mathState(Det.SERVER)));
            d.addProperty("sstat", hex(Det.splitState(Det.SERVER)));
            d.addProperty("blk", hex(blkHash));
        }
        if (mc.theWorld != null)
        {
            d.addProperty("cw", hex(Det.state(mc.theWorld.rand) * 31 + updateLCG(mc.theWorld)));
            d.addProperty("cseed", hex(Det.seederState(Det.CLIENT)));
            d.addProperty("cmath", hex(Det.mathState(Det.CLIENT)));
            d.addProperty("cstat", hex(Det.splitState(Det.CLIENT)));
        }
        blkCount = 0;
        r.add("d", d);
        JsonArray chat = s02(mc);
        if (chat != null) r.add("s02", chat);
        if (async)
        {
            // sp and ents hold their places in d; flush fills them in
            Held h = new Held();
            h.row = r;
            h.want = Check.expect;
            h.wantText = Check.expectText;
            Check.expect = null;
            h.job = job;
            h.ents = hashed;
            h.nEnts = nEnts;
            h.spAt = d.has("sp") ? spAt : -1;
            held = h;
        }
        if (Prof.on) Prof.lap(Prof.CAPTURE);
        return r;
    }

    private static Field recvQueue;

    /**
     * The S02 chat packets the server's tick sent, in send order: they wait in
     * the client connection's receivedPacketsQueue (S02 is not a priority
     * packet) until the next client tick drains it. Each is the component's
     * wire form (IChatComponent.Serializer, what writePacketData sends) and
     * the parts GuiNewChat.func_146237_a iterates it into (each part's
     * formatting code and own text). Null when there are none.
     */
    static JsonArray s02(Minecraft mc)
    {
        if (mc.getNetHandler() == null) return null;
        java.util.Queue q;
        try
        {
            if (recvQueue == null)
            {
                recvQueue = net.minecraft.network.NetworkManager.class.getDeclaredField("receivedPacketsQueue");
                recvQueue.setAccessible(true);
            }
            q = (java.util.Queue)recvQueue.get(mc.getNetHandler().getNetworkManager());
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
        JsonArray out = null;
        for (Object o : q)
        {
            if (!(o instanceof net.minecraft.network.play.server.S02PacketChat)) continue;
            net.minecraft.util.IChatComponent c = ((net.minecraft.network.play.server.S02PacketChat)o).func_148915_c();
            JsonArray m = new JsonArray();
            m.add(new JsonPrimitive(net.minecraft.util.IChatComponent.Serializer.func_150696_a(c)));
            JsonArray parts = new JsonArray();
            for (Object po : com.google.common.collect.Lists.newArrayList(c))
            {
                net.minecraft.util.IChatComponent pc = (net.minecraft.util.IChatComponent)po;
                JsonArray part = new JsonArray();
                part.add(new JsonPrimitive(pc.getChatStyle().getFormattingCode()));
                part.add(new JsonPrimitive(pc.getUnformattedTextForChat()));
                parts.add(part);
            }
            m.add(parts);
            if (out == null) out = new JsonArray();
            out.add(m);
        }
        return out;
    }

    private static Field lcgField;

    static long updateLCG(World w)
    {
        try
        {
            if (lcgField == null)
            {
                lcgField = World.class.getDeclaredField("updateLCG");
                lcgField.setAccessible(true);
            }
            return lcgField.getInt(w);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static long nbtLong(Entity e)
    {
        return RowHash.HASHER.get().hash(e);
    }

    static String nbtHash(Entity e)
    {
        return hex(nbtLong(e));
    }

    /** nbtLong of every entity in the list, in list order, on the calling thread. */
    static long[] nbtLongs(List<Entity> es)
    {
        long[] out = new long[es.size()];
        for (int i = 0; i < out.length; ++i) out[i] = nbtLong(es.get(i));
        return out;
    }

    /**
     * Rows are held for a RowHash job only in the tick loop. A probe that
     * drives ticks from a command and reads lastRow (ClientWorldProbe) gets
     * each row complete at once.
     */
    static boolean async()
    {
        return !Oracle.inFrameSetup && !Oracle.detail;
    }

    static String hex(long v)
    {
        String s = Long.toHexString(v);
        while (s.length() < 16) s = "0" + s;
        return s;
    }

    // ---------------------------------------------------------------- replay comparison

    static final class Check
    {
        static JsonObject expect;
        /** The tape line expect was parsed from. */
        static String expectText;
        static boolean diverged;
        /** The reference tape records every row's S02 packets (header "s02"). */
        static boolean refS02;

        /** want: the tape row set as Check.expect when this row began, null for none. */
        static void compare(JsonObject want, JsonObject got)
        {
            if (want == null || diverged) return;
            String why = diff("", want, got);
            if (why != null)
            {
                diverged = true;
                System.out.println("ORACLE DIVERGE t=" + got.get("t") + " " + why);
                Oracle.finish(3);
            }
        }

        /**
         * First differing leaf, digests first so the report names the owner.
         * Leaves compare as serialized text: both sides come from the same
         * writer, and a float read back as a double would not compare equal.
         *
         * Tapes recorded before 3cfdfe2 (Sep 24) omit sp.inv/sp.win and
         * cp.inv when empty; the replay always writes them, so a missing key
         * on the recorded side is treated as "empty" rather than a divergence.
         * Tapes recorded before cp.fur existed (2026-09-25) lack it for the
         * same reason: a replayed cp.fur the tape does not carry is skipped.
         */
        static String diff(String path, JsonObject want, JsonObject got)
        {
            // s02: a tape recorded before the field existed has none to compare
            if (want.has("s02") || (refS02 && got.has("s02")))
            {
                JsonElement a = want.get("s02"), b = got.get("s02");
                if (a == null || b == null || !a.toString().equals(b.toString()))
                    return "s02 want=" + a + " got=" + b;
            }
            String[] order = {"d", "cp", "sp", "w", "act"};
            for (String k : order)
            {
                if (!want.has(k) && !got.has(k)) continue;
                if (!want.has(k) || !got.has(k)) return path + k + " present want=" + want.has(k) + " got=" + got.has(k);
                JsonObject a = want.getAsJsonObject(k), b = got.getAsJsonObject(k);
                for (Map.Entry<String, JsonElement> e : a.entrySet())
                {
                    if (clientOwn(k, e.getKey())) continue;
                    JsonElement g = b.get(e.getKey());
                    if (g == null && "px".equals(e.getKey())) continue; // frames are optional on replay
                    if (g == null || !g.toString().equals(e.getValue().toString()))
                        return k + "." + e.getKey() + " want=" + e.getValue() + " got=" + g;
                }
                for (Map.Entry<String, JsonElement> e : b.entrySet())
                {
                    if (a.has(e.getKey()) || "px".equals(e.getKey())) continue;
                    if (clientOwn(k, e.getKey())) continue;
                    // an empty inventory or window the old format did not record
                    if (("sp".equals(k) || "cp".equals(k)) && legacyEmpty(e.getValue())) continue;
                    // the client furnace's progress bars, first recorded 2026-09-25
                    if ("cp".equals(k) && "fur".equals(e.getKey())) continue;
                    return k + "." + e.getKey() + " unexpected got=" + e.getValue();
                }
            }
            return null;
        }

        /** A client field --rerecord-client writes without comparing. */
        static boolean clientOwn(String block, String key)
        {
            return Oracle.rerecordClient && "d".equals(block)
                && ("cw".equals(key) || "cseed".equals(key) || "cmath".equals(key) || "cstat".equals(key) || "px".equals(key));
        }

        /** A value an old-format tape omitted: an empty inv array or a null win. */
        static boolean legacyEmpty(JsonElement v)
        {
            if (v == null) return false;
            if (v.isJsonArray()) return v.getAsJsonArray().size() == 0;
            if (v.isJsonNull()) return true;
            if (v.isJsonObject())
            {
                JsonObject o = v.getAsJsonObject();
                // an empty inv array carries no elements; a window object is never legacy-optional
                return o.entrySet().isEmpty();
            }
            return false;
        }
    }

    static BufferedReader openRef(String path) throws IOException
    {
        return new BufferedReader(new InputStreamReader(new FileInputStream(path), "UTF-8"));
    }

    static JsonObject parse(String line)
    {
        return new JsonParser().parse(line).getAsJsonObject();
    }
}
