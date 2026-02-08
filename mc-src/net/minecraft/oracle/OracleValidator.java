package net.minecraft.oracle;

import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Compares two state snapshots (.nsta files) and reports divergences.
 *
 * Comparison rules:
 *   - Chunk block data: byte-for-byte exact match
 *   - Entity positions: within epsilon (1e-6)
 *   - Player state: position within epsilon, inventory exact, health/hunger exact
 */
public class OracleValidator
{
    private static final Logger logger = LogManager.getLogger();
    private static final int MAGIC = 0x4E535441; // "NSTA"
    private static final double POSITION_EPSILON = 1e-6;
    private static final float FLOAT_EPSILON = 1e-6f;

    public static class ValidationResult
    {
        public boolean passed;
        public int divergenceCount;
        public String firstDivergence;

        public ValidationResult(boolean passed, int divergenceCount, String firstDivergence)
        {
            this.passed = passed;
            this.divergenceCount = divergenceCount;
            this.firstDivergence = firstDivergence;
        }
    }

    /**
     * Compare two .nsta snapshot files.
     * Returns a ValidationResult with pass/fail and divergence details.
     */
    public static ValidationResult compare(String pathA, String pathB)
    {
        try
        {
            DataInputStream disA = new DataInputStream(new FileInputStream(pathA));
            DataInputStream disB = new DataInputStream(new FileInputStream(pathB));

            // Read and compare headers
            int magicA = disA.readInt();
            int magicB = disB.readInt();
            if (magicA != MAGIC || magicB != MAGIC)
            {
                disA.close();
                disB.close();
                return new ValidationResult(false, 1,
                    "Invalid magic: A=0x" + Integer.toHexString(magicA) + " B=0x" + Integer.toHexString(magicB));
            }

            int versionA = disA.readInt();
            int versionB = disB.readInt();
            if (versionA != 1 || versionB != 1)
            {
                disA.close();
                disB.close();
                return new ValidationResult(false, 1,
                    "Version mismatch: A=" + versionA + " B=" + versionB);
            }

            int tickA = disA.readInt();
            int tickB = disB.readInt();
            int dimA = disA.readInt();
            int dimB = disB.readInt();
            int chunkCountA = disA.readInt();
            int chunkCountB = disB.readInt();
            int entityCountA = disA.readInt();
            int entityCountB = disB.readInt();

            StringBuilder errors = new StringBuilder();
            int divergenceCount = 0;

            if (dimA != dimB)
            {
                disA.close();
                disB.close();
                return new ValidationResult(false, 1,
                    "Dimension mismatch: A=" + dimA + " B=" + dimB);
            }

            if (chunkCountA != chunkCountB)
            {
                String msg = "Chunk count mismatch: A=" + chunkCountA + " B=" + chunkCountB;
                if (divergenceCount == 0) errors.append(msg);
                divergenceCount++;
                // Can't continue chunk comparison if counts differ
                disA.close();
                disB.close();
                return new ValidationResult(false, divergenceCount, errors.toString());
            }

            // Compare chunks
            for (int c = 0; c < chunkCountA; c++)
            {
                int cxA = disA.readInt();
                int czA = disA.readInt();
                int cxB = disB.readInt();
                int czB = disB.readInt();

                if (cxA != cxB || czA != czB)
                {
                    String msg = "Chunk " + c + " coord mismatch: A=(" + cxA + "," + czA + ") B=(" + cxB + "," + czB + ")";
                    if (divergenceCount == 0) errors.append(msg);
                    divergenceCount++;
                }

                for (int s = 0; s < 16; s++)
                {
                    int presentA = disA.readUnsignedByte();
                    int presentB = disB.readUnsignedByte();

                    if (presentA != presentB)
                    {
                        String msg = "Chunk (" + cxA + "," + czA + ") section " + s + " presence mismatch: A=" + presentA + " B=" + presentB;
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                        // Skip data for whichever is present
                        if (presentA == 1) { disA.skipBytes(4096 + 2048); }
                        if (presentB == 1) { disB.skipBytes(4096 + 2048); }
                        continue;
                    }

                    if (presentA == 1)
                    {
                        // Compare blockLSB (4096 bytes)
                        byte[] blocksA = new byte[4096];
                        byte[] blocksB = new byte[4096];
                        disA.readFully(blocksA);
                        disB.readFully(blocksB);

                        for (int b = 0; b < 4096; b++)
                        {
                            if (blocksA[b] != blocksB[b])
                            {
                                int localX = b & 0xF;
                                int localY = (b >> 8) & 0xF;
                                int localZ = (b >> 4) & 0xF;
                                String msg = "Block mismatch at chunk (" + cxA + "," + czA + ") section " + s
                                    + " local (" + localX + "," + localY + "," + localZ + "): A=0x"
                                    + Integer.toHexString(blocksA[b] & 0xFF) + " B=0x" + Integer.toHexString(blocksB[b] & 0xFF);
                                if (divergenceCount == 0) errors.append(msg);
                                divergenceCount++;
                                break; // only report first block divergence per section
                            }
                        }

                        // Compare metadata (2048 bytes)
                        byte[] metaA = new byte[2048];
                        byte[] metaB = new byte[2048];
                        disA.readFully(metaA);
                        disB.readFully(metaB);

                        for (int b = 0; b < 2048; b++)
                        {
                            if (metaA[b] != metaB[b])
                            {
                                String msg = "Metadata mismatch at chunk (" + cxA + "," + czA + ") section " + s
                                    + " byte " + b + ": A=0x" + Integer.toHexString(metaA[b] & 0xFF)
                                    + " B=0x" + Integer.toHexString(metaB[b] & 0xFF);
                                if (divergenceCount == 0) errors.append(msg);
                                divergenceCount++;
                                break; // only report first metadata divergence per section
                            }
                        }
                    }
                }
            }

            // Compare entities
            if (entityCountA != entityCountB)
            {
                String msg = "Entity count mismatch: A=" + entityCountA + " B=" + entityCountB;
                if (divergenceCount == 0) errors.append(msg);
                divergenceCount++;
                // Skip remaining data
                disA.close();
                disB.close();
                return new ValidationResult(false, divergenceCount, errors.toString());
            }

            for (int e = 0; e < entityCountA; e++)
            {
                int typeA = disA.readShort();
                int typeB = disB.readShort();

                if (typeA == -1 && typeB == -1)
                {
                    // Player record
                    String playerResult = comparePlayer(disA, disB, e, errors, divergenceCount);
                    if (playerResult != null)
                    {
                        if (divergenceCount == 0) errors.append(playerResult);
                        divergenceCount++;
                    }
                }
                else if (typeA == -1 || typeB == -1)
                {
                    // Type mismatch (one is player, other is entity)
                    String msg = "Entity " + e + " type mismatch: A=" + typeA + " B=" + typeB;
                    if (divergenceCount == 0) errors.append(msg);
                    divergenceCount++;
                    // Can't reliably continue -- different record sizes
                    disA.close();
                    disB.close();
                    return new ValidationResult(false, divergenceCount, errors.toString());
                }
                else
                {
                    // Regular entity
                    if (typeA != typeB)
                    {
                        String msg = "Entity " + e + " type mismatch: A=" + typeA + " B=" + typeB;
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                    }

                    // posX, posY, posZ (f64 x 3)
                    double pxA = disA.readDouble(), pxB = disB.readDouble();
                    double pyA = disA.readDouble(), pyB = disB.readDouble();
                    double pzA = disA.readDouble(), pzB = disB.readDouble();

                    if (Math.abs(pxA - pxB) > POSITION_EPSILON ||
                        Math.abs(pyA - pyB) > POSITION_EPSILON ||
                        Math.abs(pzA - pzB) > POSITION_EPSILON)
                    {
                        String msg = "Entity " + e + " (type=" + typeA + ") position mismatch: A=("
                            + pxA + "," + pyA + "," + pzA + ") B=(" + pxB + "," + pyB + "," + pzB + ")";
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                    }

                    // motionX, motionY, motionZ (f64 x 3)
                    double mxA = disA.readDouble(), mxB = disB.readDouble();
                    double myA = disA.readDouble(), myB = disB.readDouble();
                    double mzA = disA.readDouble(), mzB = disB.readDouble();

                    if (Math.abs(mxA - mxB) > POSITION_EPSILON ||
                        Math.abs(myA - myB) > POSITION_EPSILON ||
                        Math.abs(mzA - mzB) > POSITION_EPSILON)
                    {
                        String msg = "Entity " + e + " (type=" + typeA + ") motion mismatch: A=("
                            + mxA + "," + myA + "," + mzA + ") B=(" + mxB + "," + myB + "," + mzB + ")";
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                    }

                    // yaw, pitch (f32 x 2)
                    float yawA = disA.readFloat(), yawB = disB.readFloat();
                    float pitchA = disA.readFloat(), pitchB = disB.readFloat();

                    // health (f32)
                    float healthA = disA.readFloat(), healthB = disB.readFloat();

                    if (Math.abs(healthA - healthB) > FLOAT_EPSILON)
                    {
                        String msg = "Entity " + e + " (type=" + typeA + ") health mismatch: A=" + healthA + " B=" + healthB;
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                    }
                }
            }

            disA.close();
            disB.close();

            if (divergenceCount == 0)
            {
                logger.info("[Oracle Validator] PASS: snapshots match (tick A={}, tick B={}, dim={})", tickA, tickB, dimA);
                return new ValidationResult(true, 0, null);
            }
            else
            {
                logger.warn("[Oracle Validator] FAIL: " + divergenceCount + " divergence(s). First: " + errors.toString());
                return new ValidationResult(false, divergenceCount, errors.toString());
            }
        }
        catch (IOException e)
        {
            logger.error("[Oracle Validator] Failed to compare snapshots", e);
            return new ValidationResult(false, 1, "IOException: " + e.getMessage());
        }
    }

    /**
     * Compare player records from both streams.
     * Returns null if match, or error string if divergence.
     */
    private static String comparePlayer(DataInputStream disA, DataInputStream disB,
                                         int entityIndex, StringBuilder errors, int currentDivergences) throws IOException
    {
        // posX, posY, posZ
        double pxA = disA.readDouble(), pxB = disB.readDouble();
        double pyA = disA.readDouble(), pyB = disB.readDouble();
        double pzA = disA.readDouble(), pzB = disB.readDouble();

        // motionX, motionY, motionZ
        double mxA = disA.readDouble(), mxB = disB.readDouble();
        double myA = disA.readDouble(), myB = disB.readDouble();
        double mzA = disA.readDouble(), mzB = disB.readDouble();

        // yaw, pitch
        float yawA = disA.readFloat(), yawB = disB.readFloat();
        float pitchA = disA.readFloat(), pitchB = disB.readFloat();

        // health
        float healthA = disA.readFloat(), healthB = disB.readFloat();

        // foodLevel
        int foodA = disA.readInt(), foodB = disB.readInt();

        // saturation
        float satA = disA.readFloat(), satB = disB.readFloat();

        // currentItem
        int itemA = disA.readInt(), itemB = disB.readInt();

        // dimension
        int dimA = disA.readInt(), dimB = disB.readInt();

        // onGround
        int groundA = disA.readUnsignedByte(), groundB = disB.readUnsignedByte();

        // inventory: 40 slots
        boolean invMatch = true;
        String invError = null;
        for (int slot = 0; slot < 40; slot++)
        {
            int presentA = disA.readUnsignedByte();
            int presentB = disB.readUnsignedByte();

            if (presentA != presentB)
            {
                if (invMatch)
                {
                    invError = "Player inventory slot " + slot + " presence mismatch: A=" + presentA + " B=" + presentB;
                    invMatch = false;
                }
                // Read remaining data for present slots
                if (presentA == 1) { disA.readShort(); disA.readByte(); disA.readShort(); }
                if (presentB == 1) { disB.readShort(); disB.readByte(); disB.readShort(); }
                continue;
            }

            if (presentA == 1)
            {
                int idA = disA.readShort(), idB = disB.readShort();
                int sizeA = disA.readByte(), sizeB = disB.readByte();
                int dmgA = disA.readShort(), dmgB = disB.readShort();

                if (invMatch && (idA != idB || sizeA != sizeB || dmgA != dmgB))
                {
                    invError = "Player inventory slot " + slot + " mismatch: A=(id=" + idA + ",size=" + sizeA + ",dmg=" + dmgA
                        + ") B=(id=" + idB + ",size=" + sizeB + ",dmg=" + dmgB + ")";
                    invMatch = false;
                }
            }
        }

        // Check for divergences
        if (Math.abs(pxA - pxB) > POSITION_EPSILON ||
            Math.abs(pyA - pyB) > POSITION_EPSILON ||
            Math.abs(pzA - pzB) > POSITION_EPSILON)
        {
            return "Player position mismatch: A=(" + pxA + "," + pyA + "," + pzA
                + ") B=(" + pxB + "," + pyB + "," + pzB + ")";
        }

        if (Math.abs(healthA - healthB) > FLOAT_EPSILON)
        {
            return "Player health mismatch: A=" + healthA + " B=" + healthB;
        }

        if (foodA != foodB)
        {
            return "Player food mismatch: A=" + foodA + " B=" + foodB;
        }

        if (Math.abs(satA - satB) > FLOAT_EPSILON)
        {
            return "Player saturation mismatch: A=" + satA + " B=" + satB;
        }

        if (dimA != dimB)
        {
            return "Player dimension mismatch: A=" + dimA + " B=" + dimB;
        }

        if (!invMatch)
        {
            return invError;
        }

        return null;
    }

    /**
     * Standalone entry point for comparing two snapshot files.
     * Usage: java OracleValidator <fileA.nsta> <fileB.nsta>
     */
    public static void main(String[] args)
    {
        if (args.length != 2)
        {
            System.err.println("Usage: OracleValidator <snapshotA.nsta> <snapshotB.nsta>");
            System.exit(1);
        }

        ValidationResult result = compare(args[0], args[1]);

        if (result.passed)
        {
            System.out.println("PASS: Snapshots match.");
            System.exit(0);
        }
        else
        {
            System.out.println("FAIL: " + result.divergenceCount + " divergence(s)");
            System.out.println("First divergence: " + result.firstDivergence);
            System.exit(1);
        }
    }
}
