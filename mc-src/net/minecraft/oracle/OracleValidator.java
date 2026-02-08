package net.minecraft.oracle;

import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Compares two state snapshots (.nsta files) and reports categorized divergences.
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

    // Entity type ID to name mapping (MC 1.7.10)
    private static String entityTypeName(int id)
    {
        switch (id)
        {
            case 1: return "Item";
            case 2: return "XPOrb";
            case 9: return "Painting";
            case 10: return "Arrow";
            case 11: return "Snowball";
            case 12: return "Fireball";
            case 13: return "SmallFireball";
            case 14: return "ThrownEnderpearl";
            case 15: return "EyeOfEnder";
            case 16: return "ThrownPotion";
            case 17: return "ThrownExpBottle";
            case 18: return "ItemFrame";
            case 19: return "WitherSkull";
            case 20: return "PrimedTnt";
            case 21: return "FallingSand";
            case 22: return "FireworksRocketEntity";
            case 40: return "Minecart";
            case 41: return "Boat";
            case 48: return "Mob";
            case 49: return "Monster";
            case 50: return "Creeper";
            case 51: return "Skeleton";
            case 52: return "Spider";
            case 53: return "Giant";
            case 54: return "Zombie";
            case 55: return "Slime";
            case 56: return "Ghast";
            case 57: return "PigZombie";
            case 58: return "Enderman";
            case 59: return "CaveSpider";
            case 60: return "Silverfish";
            case 61: return "Blaze";
            case 62: return "LavaSlime";
            case 63: return "EnderDragon";
            case 64: return "WitherBoss";
            case 65: return "Bat";
            case 66: return "Witch";
            case 90: return "Pig";
            case 91: return "Sheep";
            case 92: return "Cow";
            case 93: return "Chicken";
            case 94: return "Squid";
            case 95: return "Wolf";
            case 96: return "MushroomCow";
            case 97: return "SnowMan";
            case 98: return "Ozelot";
            case 99: return "VillagerGolem";
            case 100: return "EntityHorse";
            case 120: return "Villager";
            case 200: return "EnderCrystal";
            default: return "Unknown(" + id + ")";
        }
    }

    public static class ValidationResult
    {
        public boolean passed;
        public int divergenceCount;
        public String firstDivergence;
        // Categorized counts
        public int blockDivergences;
        public int metadataDivergences;
        public int entityCountMismatch; // 0 or 1
        public int entityTypeDivergences;
        public int entityPositionDivergences;
        public int entityMotionDivergences;
        public int entityHealthDivergences;
        public int playerDivergences;
        public String summary;

        public ValidationResult(boolean passed, int divergenceCount, String firstDivergence)
        {
            this.passed = passed;
            this.divergenceCount = divergenceCount;
            this.firstDivergence = firstDivergence;
        }
    }

    /**
     * Compare two .nsta snapshot files.
     * Returns a ValidationResult with pass/fail and categorized divergence details.
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
            int blockDivergences = 0;
            int metadataDivergences = 0;
            int entityCountMismatch = 0;
            int entityTypeDivergences = 0;
            int entityPositionDivergences = 0;
            int entityMotionDivergences = 0;
            int entityHealthDivergences = 0;
            int playerDivergences = 0;

            // Log header info
            System.out.println("[Validator] File A: tick=" + tickA + " dim=" + dimA + " chunks=" + chunkCountA + " entities=" + entityCountA);
            System.out.println("[Validator] File B: tick=" + tickB + " dim=" + dimB + " chunks=" + chunkCountB + " entities=" + entityCountB);

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
                disA.close();
                disB.close();
                return new ValidationResult(false, divergenceCount, errors.toString());
            }

            // Compare chunks -- track per-section divergences
            int sectionsWithBlockDiffs = 0;
            int sectionsWithMetaDiffs = 0;
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
                        blockDivergences++;
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

                        int blockDiffCount = 0;
                        for (int b = 0; b < 4096; b++)
                        {
                            if (blocksA[b] != blocksB[b])
                            {
                                blockDiffCount++;
                                if (blockDivergences == 0)
                                {
                                    int localX = b & 0xF;
                                    int localY = (b >> 8) & 0xF;
                                    int localZ = (b >> 4) & 0xF;
                                    String msg = "Block mismatch at chunk (" + cxA + "," + czA + ") section " + s
                                        + " local (" + localX + "," + localY + "," + localZ + "): A=0x"
                                        + Integer.toHexString(blocksA[b] & 0xFF) + " B=0x" + Integer.toHexString(blocksB[b] & 0xFF);
                                    if (divergenceCount == 0) errors.append(msg);
                                }
                            }
                        }
                        if (blockDiffCount > 0)
                        {
                            blockDivergences += blockDiffCount;
                            divergenceCount++;
                            sectionsWithBlockDiffs++;
                        }

                        // Compare metadata (2048 bytes)
                        byte[] metaA = new byte[2048];
                        byte[] metaB = new byte[2048];
                        disA.readFully(metaA);
                        disB.readFully(metaB);

                        int metaDiffCount = 0;
                        for (int b = 0; b < 2048; b++)
                        {
                            if (metaA[b] != metaB[b])
                            {
                                metaDiffCount++;
                                if (metadataDivergences == 0 && blockDivergences == 0)
                                {
                                    String msg = "Metadata mismatch at chunk (" + cxA + "," + czA + ") section " + s
                                        + " byte " + b + ": A=0x" + Integer.toHexString(metaA[b] & 0xFF)
                                        + " B=0x" + Integer.toHexString(metaB[b] & 0xFF);
                                    if (divergenceCount == 0) errors.append(msg);
                                }
                            }
                        }
                        if (metaDiffCount > 0)
                        {
                            metadataDivergences += metaDiffCount;
                            divergenceCount++;
                            sectionsWithMetaDiffs++;
                        }
                    }
                }
            }

            // Compare entities
            if (entityCountA != entityCountB)
            {
                entityCountMismatch = 1;
                String msg = "Entity count mismatch: A=" + entityCountA + " B=" + entityCountB;
                System.out.println("[Validator] " + msg);
                if (divergenceCount == 0) errors.append(msg);
                divergenceCount++;
            }

            // Pair-wise entity comparison (compare min of both counts)
            int minEntities = Math.min(entityCountA, entityCountB);
            for (int e = 0; e < minEntities; e++)
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
                        playerDivergences++;
                    }
                }
                else if (typeA == -1 || typeB == -1)
                {
                    String msg = "Entity " + e + " type mismatch: A=" + typeA + " B=" + typeB + " (one is player)";
                    if (divergenceCount == 0) errors.append(msg);
                    divergenceCount++;
                    entityTypeDivergences++;
                    disA.close();
                    disB.close();
                    ValidationResult result = new ValidationResult(false, divergenceCount, errors.toString());
                    result.summary = "ABORTED: player/entity ordering mismatch at index " + e;
                    System.out.println(result.summary);
                    return result;
                }
                else
                {
                    // Regular entity
                    String nameA = entityTypeName(typeA);
                    String nameB = entityTypeName(typeB);

                    if (typeA != typeB)
                    {
                        String msg = "Entity " + e + " type mismatch: A=" + nameA + "(" + typeA + ") B=" + nameB + "(" + typeB + ")";
                        if (divergenceCount == 0) errors.append(msg);
                        divergenceCount++;
                        entityTypeDivergences++;
                    }

                    // posX, posY, posZ (f64 x 3)
                    double pxA = disA.readDouble(), pxB = disB.readDouble();
                    double pyA = disA.readDouble(), pyB = disB.readDouble();
                    double pzA = disA.readDouble(), pzB = disB.readDouble();

                    if (Math.abs(pxA - pxB) > POSITION_EPSILON ||
                        Math.abs(pyA - pyB) > POSITION_EPSILON ||
                        Math.abs(pzA - pzB) > POSITION_EPSILON)
                    {
                        double dist = Math.sqrt((pxA-pxB)*(pxA-pxB) + (pyA-pyB)*(pyA-pyB) + (pzA-pzB)*(pzA-pzB));
                        // Print first 10 position divergences for debugging
                        if (entityPositionDivergences < 10)
                        {
                            System.out.println("[Validator] Entity " + e + " " + nameA
                                + " pos diverged by " + String.format("%.4f", dist)
                                + " blocks: A=(" + String.format("%.2f,%.2f,%.2f", pxA, pyA, pzA)
                                + ") B=(" + String.format("%.2f,%.2f,%.2f", pxB, pyB, pzB) + ")");
                        }
                        if (divergenceCount == 0)
                        {
                            errors.append("Entity " + e + " " + nameA + " position mismatch (dist=" + String.format("%.4f", dist) + ")");
                        }
                        divergenceCount++;
                        entityPositionDivergences++;
                    }

                    // motionX, motionY, motionZ (f64 x 3)
                    double mxA = disA.readDouble(), mxB = disB.readDouble();
                    double myA = disA.readDouble(), myB = disB.readDouble();
                    double mzA = disA.readDouble(), mzB = disB.readDouble();

                    if (Math.abs(mxA - mxB) > POSITION_EPSILON ||
                        Math.abs(myA - myB) > POSITION_EPSILON ||
                        Math.abs(mzA - mzB) > POSITION_EPSILON)
                    {
                        if (entityMotionDivergences < 10)
                        {
                            System.out.println("[Validator] Entity " + e + " " + nameA
                                + " motion diverged: A=(" + String.format("%.6f,%.6f,%.6f", mxA, myA, mzA)
                                + ") B=(" + String.format("%.6f,%.6f,%.6f", mxB, myB, mzB)
                                + ") pos=(" + String.format("%.2f,%.2f,%.2f", pxA, pyA, pzA) + ")");
                        }
                        if (divergenceCount == 0)
                        {
                            errors.append("Entity " + e + " " + nameA + " motion mismatch");
                        }
                        divergenceCount++;
                        entityMotionDivergences++;
                    }

                    // yaw, pitch (f32 x 2)
                    float yawA = disA.readFloat(), yawB = disB.readFloat();
                    float pitchA = disA.readFloat(), pitchB = disB.readFloat();

                    // health (f32)
                    float healthA = disA.readFloat(), healthB = disB.readFloat();

                    if (Math.abs(healthA - healthB) > FLOAT_EPSILON)
                    {
                        if (divergenceCount == 0)
                        {
                            errors.append("Entity " + e + " " + nameA + " health mismatch: A=" + healthA + " B=" + healthB);
                        }
                        divergenceCount++;
                        entityHealthDivergences++;
                    }
                }
            }

            // Skip and report extra entities from the larger file
            DataInputStream disExtra = (entityCountA > entityCountB) ? disA : disB;
            int extraCount = Math.abs(entityCountA - entityCountB);
            String extraLabel = (entityCountA > entityCountB) ? "A" : "B";
            for (int e = 0; e < extraCount; e++)
            {
                int typeExtra = disExtra.readShort();
                if (typeExtra == -1)
                {
                    // Player: skip all player fields
                    disExtra.skipBytes(6*8 + 2*4 + 4 + 4 + 4 + 4 + 4 + 1); // pos,motion,yaw,pitch,health,food,sat,item,dim,ground
                    for (int slot = 0; slot < 40; slot++)
                    {
                        int present = disExtra.readUnsignedByte();
                        if (present == 1) disExtra.skipBytes(2 + 1 + 2);
                    }
                    if (e < 5) System.out.println("[Validator] Extra entity in " + extraLabel + " [" + (minEntities+e) + "]: Player");
                }
                else
                {
                    // Regular entity: skip pos(3*8), motion(3*8), yaw(4), pitch(4), health(4)
                    disExtra.skipBytes(6*8 + 2*4 + 4);
                    if (e < 5) System.out.println("[Validator] Extra entity in " + extraLabel + " [" + (minEntities+e) + "]: " + entityTypeName(typeExtra));
                }
            }

            disA.close();
            disB.close();

            String summary = buildSummary(blockDivergences, metadataDivergences,
                sectionsWithBlockDiffs, sectionsWithMetaDiffs,
                entityCountMismatch, entityCountA, entityCountB,
                entityTypeDivergences, entityPositionDivergences,
                entityMotionDivergences, entityHealthDivergences, playerDivergences);

            if (divergenceCount == 0)
            {
                System.out.println("[Validator] PASS: snapshots match perfectly");
                ValidationResult result = new ValidationResult(true, 0, null);
                result.summary = summary;
                System.out.println(summary);
                return result;
            }
            else
            {
                System.out.println("[Validator] FAIL: " + divergenceCount + " total divergence(s)");
                System.out.println(summary);
                ValidationResult result = new ValidationResult(false, divergenceCount, errors.toString());
                result.blockDivergences = blockDivergences;
                result.metadataDivergences = metadataDivergences;
                result.entityCountMismatch = entityCountMismatch;
                result.entityTypeDivergences = entityTypeDivergences;
                result.entityPositionDivergences = entityPositionDivergences;
                result.entityMotionDivergences = entityMotionDivergences;
                result.entityHealthDivergences = entityHealthDivergences;
                result.playerDivergences = playerDivergences;
                result.summary = summary;
                return result;
            }
        }
        catch (IOException e)
        {
            logger.error("[Oracle Validator] Failed to compare snapshots", e);
            return new ValidationResult(false, 1, "IOException: " + e.getMessage());
        }
    }

    private static String buildSummary(int blockDivs, int metaDivs,
                                        int sectionsWithBlocks, int sectionsWithMeta,
                                        int entityCountMismatch, int entityCountA, int entityCountB,
                                        int entityTypeDivs, int entityPosDivs,
                                        int entityMotionDivs, int entityHealthDivs, int playerDivs)
    {
        StringBuilder sb = new StringBuilder();
        sb.append("\n========== DIVERGENCE SUMMARY ==========\n");
        sb.append("  Block divergences:    " + blockDivs + " bytes in " + sectionsWithBlocks + " sections\n");
        sb.append("  Metadata divergences: " + metaDivs + " bytes in " + sectionsWithMeta + " sections\n");
        sb.append("  Entity count:         ");
        if (entityCountMismatch > 0)
            sb.append("MISMATCH A=" + entityCountA + " B=" + entityCountB + "\n");
        else
            sb.append("match (" + entityCountA + ")\n");
        sb.append("  Entity type mismatches:     " + entityTypeDivs + "\n");
        sb.append("  Entity position divergences: " + entityPosDivs + "\n");
        sb.append("  Entity motion divergences:   " + entityMotionDivs + "\n");
        sb.append("  Entity health divergences:   " + entityHealthDivs + "\n");
        sb.append("  Player divergences:          " + playerDivs + "\n");
        sb.append("=========================================\n");
        return sb.toString();
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

        // Print player state for debugging
        System.out.println("[Validator] Player A: pos=(" + String.format("%.2f,%.2f,%.2f", pxA, pyA, pzA)
            + ") food=" + foodA + " health=" + healthA + " dim=" + dimA);
        System.out.println("[Validator] Player B: pos=(" + String.format("%.2f,%.2f,%.2f", pxB, pyB, pzB)
            + ") food=" + foodB + " health=" + healthB + " dim=" + dimB);

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
