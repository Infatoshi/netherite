package net.minecraft.oracle;

import java.io.DataOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Singleton recorder for oracle action logs.
 *
 * Binary file format:
 *   Header (32 bytes):
 *     bytes 0-3:   magic "NREC" (0x4E524543)
 *     bytes 4-7:   version (u32, = 1)
 *     bytes 8-15:  world seed (i64)
 *     bytes 16-19: start tick (i32)
 *     bytes 20-23: total action count (u32, patched on close)
 *     bytes 24-31: reserved (zeros)
 *
 *   Each action record:
 *     bytes 0-3:   tick number (u32)
 *     byte  4:     action type ID (u8)
 *     bytes 5-6:   payload length (u16)
 *     bytes 7+:    payload (variable)
 */
public class OracleRecorder
{
    private static final Logger logger = LogManager.getLogger();
    private static final int MAGIC = 0x4E524543; // "NREC"
    private static final int VERSION = 1;

    private static OracleRecorder INSTANCE;

    private DataOutputStream stream;
    private String filePath;
    private int actionCount;
    private boolean recording;

    private OracleRecorder() {}

    public static synchronized OracleRecorder get()
    {
        if (INSTANCE == null)
        {
            INSTANCE = new OracleRecorder();
        }
        return INSTANCE;
    }

    public boolean isRecording()
    {
        return this.recording;
    }

    public void startRecording(String path, long seed, int startTick)
    {
        if (this.recording)
        {
            logger.warn("[Oracle] Already recording, ignoring start request");
            return;
        }

        try
        {
            this.filePath = path;
            this.stream = new DataOutputStream(new FileOutputStream(path));
            this.actionCount = 0;

            // Write header
            this.stream.writeInt(MAGIC);
            this.stream.writeInt(VERSION);
            this.stream.writeLong(seed);
            this.stream.writeInt(startTick);
            this.stream.writeInt(0); // placeholder for action count
            this.stream.writeLong(0L); // reserved

            this.recording = true;
            logger.info("[Oracle] Recording started: {} (seed={}, startTick={})", path, seed, startTick);
        }
        catch (IOException e)
        {
            logger.error("[Oracle] Failed to start recording", e);
        }
    }

    public void recordAction(int tick, int actionType, byte[] payload)
    {
        if (!this.recording) return;

        try
        {
            this.stream.writeInt(tick);
            this.stream.writeByte(actionType);
            this.stream.writeShort(payload.length);
            if (payload.length > 0)
            {
                this.stream.write(payload);
            }
            this.actionCount++;
        }
        catch (IOException e)
        {
            logger.error("[Oracle] Failed to write action", e);
        }
    }

    public void stopRecording()
    {
        if (!this.recording) return;

        try
        {
            this.stream.flush();
            this.stream.close();

            // Patch action count in header (bytes 20-23)
            RandomAccessFile raf = new RandomAccessFile(this.filePath, "rw");
            raf.seek(20);
            raf.writeInt(this.actionCount);
            raf.close();

            logger.info("[Oracle] Recording stopped: " + this.actionCount + " actions written to " + this.filePath);
        }
        catch (IOException e)
        {
            logger.error("[Oracle] Failed to finalize recording", e);
        }
        finally
        {
            this.recording = false;
            this.stream = null;
        }
    }

    public int getActionCount()
    {
        return this.actionCount;
    }
}
