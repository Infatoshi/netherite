package net.minecraft.world.chunk.storage;

import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.zip.DeflaterOutputStream;
import java.util.zip.GZIPInputStream;
import java.util.zip.InflaterInputStream;
import net.minecraft.server.MinecraftServer;

public class RegionFile
{
    private static final byte[] emptySector = new byte[4096];
    private final File fileName;
    private RandomAccessFile dataFile;
    private final int[] offsets = new int[1024];
    private final int[] chunkTimestamps = new int[1024];
    private ArrayList sectorFree;
    /** McRegion sizeDelta */
    private int sizeDelta;
    private long lastModified;
    private static final String __OBFID = "CL_00000381";

    public RegionFile(File p_i2001_1_)
    {
        this.fileName = p_i2001_1_;
        this.sizeDelta = 0;

        try
        {
            if (p_i2001_1_.exists())
            {
                this.lastModified = p_i2001_1_.lastModified();
            }

            this.dataFile = new RandomAccessFile(p_i2001_1_, "rw");
            int i;

            if (this.dataFile.length() < 4096L)
            {
                for (i = 0; i < 1024; ++i)
                {
                    this.dataFile.writeInt(0);
                }

                for (i = 0; i < 1024; ++i)
                {
                    this.dataFile.writeInt(0);
                }

                this.sizeDelta += 8192;
            }

            if ((this.dataFile.length() & 4095L) != 0L)
            {
                for (i = 0; (long)i < (this.dataFile.length() & 4095L); ++i)
                {
                    this.dataFile.write(0);
                }
            }

            i = (int)this.dataFile.length() / 4096;
            this.sectorFree = new ArrayList(i);
            int j;

            for (j = 0; j < i; ++j)
            {
                this.sectorFree.add(Boolean.valueOf(true));
            }

            this.sectorFree.set(0, Boolean.valueOf(false));
            this.sectorFree.set(1, Boolean.valueOf(false));
            this.dataFile.seek(0L);
            int k;

            for (j = 0; j < 1024; ++j)
            {
                k = this.dataFile.readInt();
                this.offsets[j] = k;

                if (k != 0 && (k >> 8) + (k & 255) <= this.sectorFree.size())
                {
                    for (int l = 0; l < (k & 255); ++l)
                    {
                        this.sectorFree.set((k >> 8) + l, Boolean.valueOf(false));
                    }
                }
            }

            for (j = 0; j < 1024; ++j)
            {
                k = this.dataFile.readInt();
                this.chunkTimestamps[j] = k;
            }
        }
        catch (IOException ioexception)
        {
            ioexception.printStackTrace();
        }
    }

    // This is a copy (sort of) of the method below it, make sure they stay in sync
    public synchronized boolean chunkExists(int x, int z)
    {
        if (this.outOfBounds(x, z)) return false;

        try
        {
            int offset = this.getOffset(x, z);

            if (offset == 0) return false;

            int sectorNumber = offset >> 8;
            int numSectors = offset & 255;

            if (sectorNumber + numSectors > this.sectorFree.size()) return false;

            this.dataFile.seek((long)(sectorNumber * 4096));
            int length = this.dataFile.readInt();

            if (length > 4096 * numSectors || length <= 0) return false;

            byte version = this.dataFile.readByte();

            if (version == 1 || version == 2) return true;
        }
        catch (IOException ioexception)
        {
            return false;
        }

        return false;
    }

    /**
     * args: x, y - get uncompressed chunk stream from the region file
     */
    public synchronized DataInputStream getChunkDataInputStream(int p_76704_1_, int p_76704_2_)
    {
        if (this.outOfBounds(p_76704_1_, p_76704_2_))
        {
            return null;
        }
        else
        {
            try
            {
                int k = this.getOffset(p_76704_1_, p_76704_2_);

                if (k == 0)
                {
                    return null;
                }
                else
                {
                    int l = k >> 8;
                    int i1 = k & 255;

                    if (l + i1 > this.sectorFree.size())
                    {
                        return null;
                    }
                    else
                    {
                        this.dataFile.seek((long)(l * 4096));
                        int j1 = this.dataFile.readInt();

                        if (j1 > 4096 * i1)
                        {
                            return null;
                        }
                        else if (j1 <= 0)
                        {
                            return null;
                        }
                        else
                        {
                            byte b0 = this.dataFile.readByte();
                            byte[] abyte;

                            if (b0 == 1)
                            {
                                abyte = new byte[j1 - 1];
                                this.dataFile.read(abyte);
                                return new DataInputStream(new BufferedInputStream(new GZIPInputStream(new ByteArrayInputStream(abyte))));
                            }
                            else if (b0 == 2)
                            {
                                abyte = new byte[j1 - 1];
                                this.dataFile.read(abyte);
                                return new DataInputStream(new BufferedInputStream(new InflaterInputStream(new ByteArrayInputStream(abyte))));
                            }
                            else
                            {
                                return null;
                            }
                        }
                    }
                }
            }
            catch (IOException ioexception)
            {
                return null;
            }
        }
    }

    /**
     * args: x, z - get an output stream used to write chunk data, data is on disk when the returned stream is closed
     */
    public DataOutputStream getChunkDataOutputStream(int p_76710_1_, int p_76710_2_)
    {
        return this.outOfBounds(p_76710_1_, p_76710_2_) ? null : new DataOutputStream(new DeflaterOutputStream(new RegionFile.ChunkBuffer(p_76710_1_, p_76710_2_)));
    }

    /**
     * args: x, z, data, length - write chunk data at (x, z) to disk
     */
    protected synchronized void write(int p_76706_1_, int p_76706_2_, byte[] p_76706_3_, int p_76706_4_)
    {
        try
        {
            int l = this.getOffset(p_76706_1_, p_76706_2_);
            int i1 = l >> 8;
            int j1 = l & 255;
            int k1 = (p_76706_4_ + 5) / 4096 + 1;

            if (k1 >= 256)
            {
                return;
            }

            if (i1 != 0 && j1 == k1)
            {
                this.write(i1, p_76706_3_, p_76706_4_);
            }
            else
            {
                int l1;

                for (l1 = 0; l1 < j1; ++l1)
                {
                    this.sectorFree.set(i1 + l1, Boolean.valueOf(true));
                }

                l1 = this.sectorFree.indexOf(Boolean.valueOf(true));
                int i2 = 0;
                int j2;

                if (l1 != -1)
                {
                    for (j2 = l1; j2 < this.sectorFree.size(); ++j2)
                    {
                        if (i2 != 0)
                        {
                            if (((Boolean)this.sectorFree.get(j2)).booleanValue())
                            {
                                ++i2;
                            }
                            else
                            {
                                i2 = 0;
                            }
                        }
                        else if (((Boolean)this.sectorFree.get(j2)).booleanValue())
                        {
                            l1 = j2;
                            i2 = 1;
                        }

                        if (i2 >= k1)
                        {
                            break;
                        }
                    }
                }

                if (i2 >= k1)
                {
                    i1 = l1;
                    this.setOffset(p_76706_1_, p_76706_2_, l1 << 8 | k1);

                    for (j2 = 0; j2 < k1; ++j2)
                    {
                        this.sectorFree.set(i1 + j2, Boolean.valueOf(false));
                    }

                    this.write(i1, p_76706_3_, p_76706_4_);
                }
                else
                {
                    this.dataFile.seek(this.dataFile.length());
                    i1 = this.sectorFree.size();

                    for (j2 = 0; j2 < k1; ++j2)
                    {
                        this.dataFile.write(emptySector);
                        this.sectorFree.add(Boolean.valueOf(false));
                    }

                    this.sizeDelta += 4096 * k1;
                    this.write(i1, p_76706_3_, p_76706_4_);
                    this.setOffset(p_76706_1_, p_76706_2_, i1 << 8 | k1);
                }
            }

            this.setChunkTimestamp(p_76706_1_, p_76706_2_, (int)(MinecraftServer.getSystemTimeMillis() / 1000L));
        }
        catch (IOException ioexception)
        {
            ioexception.printStackTrace();
        }
    }

    /**
     * args: sectorNumber, data, length - write the chunk data to this RegionFile
     */
    private void write(int p_76712_1_, byte[] p_76712_2_, int p_76712_3_) throws IOException
    {
        this.dataFile.seek((long)(p_76712_1_ * 4096));
        this.dataFile.writeInt(p_76712_3_ + 1);
        this.dataFile.writeByte(2);
        this.dataFile.write(p_76712_2_, 0, p_76712_3_);
    }

    /**
     * args: x, z - check region bounds
     */
    private boolean outOfBounds(int p_76705_1_, int p_76705_2_)
    {
        return p_76705_1_ < 0 || p_76705_1_ >= 32 || p_76705_2_ < 0 || p_76705_2_ >= 32;
    }

    /**
     * args: x, y - get chunk's offset in region file
     */
    private int getOffset(int p_76707_1_, int p_76707_2_)
    {
        return this.offsets[p_76707_1_ + p_76707_2_ * 32];
    }

    /**
     * args: x, z, - true if chunk has been saved / converted
     */
    public boolean isChunkSaved(int p_76709_1_, int p_76709_2_)
    {
        return this.getOffset(p_76709_1_, p_76709_2_) != 0;
    }

    /**
     * args: x, z, offset - sets the chunk's offset in the region file
     */
    private void setOffset(int p_76711_1_, int p_76711_2_, int p_76711_3_) throws IOException
    {
        this.offsets[p_76711_1_ + p_76711_2_ * 32] = p_76711_3_;
        this.dataFile.seek((long)((p_76711_1_ + p_76711_2_ * 32) * 4));
        this.dataFile.writeInt(p_76711_3_);
    }

    /**
     * args: x, z, timestamp - sets the chunk's write timestamp
     */
    private void setChunkTimestamp(int p_76713_1_, int p_76713_2_, int p_76713_3_) throws IOException
    {
        this.chunkTimestamps[p_76713_1_ + p_76713_2_ * 32] = p_76713_3_;
        this.dataFile.seek((long)(4096 + (p_76713_1_ + p_76713_2_ * 32) * 4));
        this.dataFile.writeInt(p_76713_3_);
    }

    /**
     * close this RegionFile and prevent further writes
     */
    public void close() throws IOException
    {
        if (this.dataFile != null)
        {
            this.dataFile.close();
        }
    }

    class ChunkBuffer extends ByteArrayOutputStream
    {
        private int chunkX;
        private int chunkZ;
        private static final String __OBFID = "CL_00000382";

        public ChunkBuffer(int p_i2000_2_, int p_i2000_3_)
        {
            super(8096);
            this.chunkX = p_i2000_2_;
            this.chunkZ = p_i2000_3_;
        }

        public void close() throws IOException
        {
            RegionFile.this.write(this.chunkX, this.chunkZ, this.buf, this.count);
        }
    }
}