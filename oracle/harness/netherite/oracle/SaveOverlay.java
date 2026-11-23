package netherite.oracle;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.zip.Deflater;
import java.util.zip.DeflaterOutputStream;
import java.util.zip.InflaterInputStream;
import net.minecraft.world.storage.ThreadedFileIOBase;

/**
 * The chunks a checkpoint start saved before its first recorded row.
 *
 * The join ticks of a --from run already run WorldServer.tick, whose
 * ChunkProviderServer.unloadQueuedChunks saves up to 100 queued chunks a tick
 * through AnvilChunkLoader.writeChunkToNBT: blocks as they are then, and
 * TileTicks with t relative to that tick's total time, not the checkpoint's.
 * A chunk saved there is not in the snapshot (it is no longer loaded), and a
 * replay that loads it later must see those bytes, not the checkpoint's older
 * copy. This writes every chunk whose stored NBT differs from the
 * checkpoint's copy to the snapshot's own save/ (region, DIM-1/region,
 * DIM1/region) in the Anvil region layout (RegionFile's sector table, zlib
 * payloads), with zero timestamps so two runs write the same bytes.
 */
final class SaveOverlay
{
    private SaveOverlay() {}

    static final String[] DIMS = {"region", "DIM-1/region", "DIM1/region"};

    /** Returns the number of chunks written under dir/save. */
    static int write(File dir) throws Exception
    {
        // the server's own chunk writes go through the file IO thread
        ThreadedFileIOBase.threadedIOInstance.waitForFinish();
        File live = new File(new File(Oracle.mc.mcDataDir, "saves"), Oracle.worldName);
        File base = Oracle.fromDir == null ? null : new File(Oracle.fromDir, "save");
        File out = new File(dir, "save");
        Oracle.deleteTree(out);
        int n = 0;
        for (String d : DIMS)
        {
            File[] files = new File(live, d).listFiles();
            if (files == null) continue;
            Arrays.sort(files);
            for (File f : files)
            {
                if (!f.getName().endsWith(".mca")) continue;
                String key = d + "/" + f.getName();
                byte[] raw = readAll(f);
                long sum = checksum(raw);
                Written w = written.get(key);
                if (w == null || w.sum != sum || w.len != raw.length)
                {
                    // the region file changed since this run's last snapshot
                    // (or this is the first): the chunks whose stored bytes
                    // differ from the checkpoint's, their payloads as stored
                    byte[][] now = payloads(raw);
                    long[] was = base == null ? null : baseSums(new File(new File(base, d), f.getName()), key);
                    byte[][] keep = new byte[1024][];
                    int k = 0;
                    for (int i = 0; i < 1024; ++i)
                    {
                        if (now[i] == null) continue;
                        if (was != null && was[i] != 0 && sameChunk(now[i], was[i], new File(new File(base, d), f.getName()), i)) continue;
                        keep[i] = now[i];
                        ++k;
                    }
                    w = new Written();
                    w.sum = sum;
                    w.len = raw.length;
                    w.count = k;
                    w.bytes = k == 0 ? null : region(keep);
                    written.put(key, w);
                }
                if (w.count == 0) continue;
                File o = new File(new File(out, d), f.getName());
                o.getParentFile().mkdirs();
                // the same bytes as a file an earlier snapshot of the run wrote:
                // a hard link (keyframes and sweep ticks share most regions)
                boolean linked = false;
                if (w.file != null && w.file.isFile() && w.file.length() == w.bytes.length)
                {
                    try
                    {
                        java.nio.file.Files.createLink(o.toPath(), w.file.toPath());
                        linked = true;
                    }
                    catch (Exception e)
                    {
                        linked = false;
                    }
                }
                if (!linked)
                {
                    FileOutputStream fo = new FileOutputStream(o);
                    try { fo.write(w.bytes); } finally { fo.close(); }
                    w.file = o;
                }
                n += w.count;
            }
        }
        return n;
    }

    /* per live region file ("region/r.0.0.mca"): the file's checksum and
     * length when this run last wrote its overlay, the overlay's bytes and
     * the file it went to */
    static final class Written
    {
        long sum;
        int len, count;
        byte[] bytes;
        File file;
    }

    static final java.util.HashMap<String, Written> written = new java.util.HashMap<String, Written>();
    /* the checkpoint's region files never change: each slot's payload
     * checksum (0: no chunk) */
    static final java.util.HashMap<String, long[]> baseSums = new java.util.HashMap<String, long[]>();

    static long checksum(byte[] b)
    {
        return checksum(b, 0, b.length);
    }

    /** CRC-32 and Adler-32 of the bytes, and never 0 */
    static long checksum(byte[] b, int off, int len)
    {
        java.util.zip.CRC32 c = new java.util.zip.CRC32();
        c.update(b, off, len);
        java.util.zip.Adler32 a = new java.util.zip.Adler32();
        a.update(b, off, len);
        return (c.getValue() << 32 | a.getValue()) | 1L << 63;
    }

    static long[] baseSums(File f, String key) throws IOException
    {
        key = f.getPath();
        long[] s = baseSums.get(key);
        if (s != null) return s;
        s = new long[1024];
        if (f.isFile())
        {
            byte[][] p = payloads(readAll(f));
            for (int i = 0; i < 1024; ++i) if (p[i] != null) s[i] = checksum(p[i]);
        }
        baseSums.put(key, s);
        return s;
    }

    /** A live chunk against the checkpoint's copy of the same slot: equal
     * stored bytes, else equal NBT once both are inflated. */
    static boolean sameChunk(byte[] now, long wasSum, File baseFile, int slot) throws IOException
    {
        long nowSum = checksum(now);
        if (nowSum == wasSum) return true;
        String key = baseFile.getPath() + "#" + slot + "#" + nowSum;
        Boolean same = sameNbt.get(key);
        if (same != null) return same;
        byte[][] base = basePayloads.get(baseFile.getPath());
        if (base == null)
        {
            base = payloads(readAll(baseFile));
            basePayloads.put(baseFile.getPath(), base);
        }
        byte[] was = base[slot];
        same = was != null && Arrays.equals(readAll(inflate(now)), readAll(inflate(was)));
        sameNbt.put(key, same);
        return same;
    }

    /* the checkpoint's payloads by region file, read once, and each live
     * payload's NBT comparison with the checkpoint's by its checksum */
    static final java.util.HashMap<String, byte[][]> basePayloads = new java.util.HashMap<String, byte[][]>();
    static final java.util.HashMap<String, Boolean> sameNbt = new java.util.HashMap<String, Boolean>();

    /** Every slot's stored payload (the kind byte, then the compressed data)
     * in a region file's bytes, by slot x + z * 32. */
    static byte[][] payloads(byte[] b)
    {
        byte[][] out = new byte[1024][];
        for (int i = 0; i < 1024 && 4 * i + 3 < b.length; ++i)
        {
            int off = ((b[4 * i] & 255) << 16) | ((b[4 * i + 1] & 255) << 8) | (b[4 * i + 2] & 255);
            if (off == 0) continue;
            out[i] = payload(b, off * 4096);
        }
        return out;
    }

    /** The payload at byte p of a region file: the kind byte and the data;
     * null when it does not fit or is not a kind RegionFile reads. */
    static byte[] payload(byte[] b, int p)
    {
        if (p + 5 > b.length) return null;
        int len = ((b[p] & 255) << 24) | ((b[p + 1] & 255) << 16) | ((b[p + 2] & 255) << 8) | (b[p + 3] & 255);
        int kind = b[p + 4];
        if (len <= 1 || p + 4 + len > b.length || (kind != 1 && kind != 2)) return null;
        return Arrays.copyOfRange(b, p + 4, p + 4 + len);
    }

    /** The chunk NBT stream of a payload, as RegionFile.getChunkDataInputStream gives it. */
    static DataInputStream inflate(byte[] payload) throws IOException
    {
        InputStream in = new java.io.ByteArrayInputStream(payload, 1, payload.length - 1);
        in = payload[0] == 1 ? new java.util.zip.GZIPInputStream(in) : new InflaterInputStream(in);
        return new DataInputStream(new java.io.BufferedInputStream(in));
    }

    /** Every chunk's decompressed NBT in a region file, by slot x + z * 32;
     * null when the file does not exist. */
    static byte[][] chunks(File f) throws IOException
    {
        if (!f.isFile()) return null;
        byte[][] p = payloads(readAll(f));
        byte[][] out = new byte[1024][];
        for (int i = 0; i < 1024; ++i) if (p[i] != null) out[i] = readAll(inflate(p[i]));
        return out;
    }

    /** A region file of the given payloads (zlib ones as stored, a gzip one
     * re-deflated), sectors from 2 on in slot order, zero timestamps. */
    static byte[] region(byte[][] payloads) throws IOException
    {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        byte[] header = new byte[8192];
        int sector = 2;
        for (int i = 0; i < 1024; ++i)
        {
            if (payloads[i] == null) continue;
            byte[] c;
            if (payloads[i][0] == 2) c = Arrays.copyOfRange(payloads[i], 1, payloads[i].length);
            else
            {
                ByteArrayOutputStream z = new ByteArrayOutputStream();
                DeflaterOutputStream d = new DeflaterOutputStream(z, new Deflater(Deflater.DEFAULT_COMPRESSION));
                d.write(readAll(inflate(payloads[i])));
                d.close();
                c = z.toByteArray();
            }
            int len = c.length + 1;
            int sectors = (len + 4 + 4095) / 4096;
            header[4 * i] = (byte)(sector >> 16);
            header[4 * i + 1] = (byte)(sector >> 8);
            header[4 * i + 2] = (byte)sector;
            header[4 * i + 3] = (byte)sectors;
            byte[] s = new byte[sectors * 4096];
            s[0] = (byte)(len >> 24);
            s[1] = (byte)(len >> 16);
            s[2] = (byte)(len >> 8);
            s[3] = (byte)len;
            s[4] = 2;
            System.arraycopy(c, 0, s, 5, c.length);
            body.write(s);
            sector += sectors;
        }
        ByteArrayOutputStream o = new ByteArrayOutputStream(header.length + body.size());
        o.write(header);
        body.writeTo(o);
        return o.toByteArray();
    }

    static byte[] readAll(File f) throws IOException
    {
        FileInputStream in = new FileInputStream(f);
        try
        {
            return readAll(in);
        }
        finally
        {
            in.close();
        }
    }

    static byte[] readAll(InputStream in) throws IOException
    {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
        return o.toByteArray();
    }
}
