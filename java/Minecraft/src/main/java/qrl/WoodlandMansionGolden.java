package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.Mirror;
import net.minecraft.util.Rotation;
import net.minecraft.util.datafix.DataFixer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureComponentTemplate;
import net.minecraft.world.gen.structure.WoodlandMansionPieces;
import net.minecraft.world.gen.structure.template.TemplateManager;

/** Real 1.11.2 woodland-mansion piece graph oracle for the C port. */
public final class WoodlandMansionGolden {
    private static long add(long hash, int value) {
        hash ^= value & 0xffffffffL;
        return hash * 0x100000001b3L;
    }

    private static long addString(long hash, String value) {
        for (int i = 0; i < value.length(); ++i) {
            hash ^= value.charAt(i) & 0xff;
            hash *= 0x100000001b3L;
        }
        return hash;
    }

    private static void one(long seed, int rotationIndex) throws Exception {
        TemplateManager manager = new TemplateManager("", new DataFixer(922));
        List<WoodlandMansionPieces.MansionTemplate> pieces =
            new ArrayList<WoodlandMansionPieces.MansionTemplate>();
        BlockPos start = new BlockPos(104, 71, -200);
        Rotation startRotation = Rotation.values()[rotationIndex];
        WoodlandMansionPieces.generateMansion(
            manager, start, startRotation, pieces, new Random(seed));

        Field nameField = WoodlandMansionPieces.MansionTemplate.class
            .getDeclaredField("templateName");
        Field rotationField = WoodlandMansionPieces.MansionTemplate.class
            .getDeclaredField("rotation");
        Field mirrorField = WoodlandMansionPieces.MansionTemplate.class
            .getDeclaredField("mirror");
        Field positionField = StructureComponentTemplate.class
            .getDeclaredField("templatePosition");
        nameField.setAccessible(true);
        rotationField.setAccessible(true);
        mirrorField.setAccessible(true);
        positionField.setAccessible(true);

        long hash = 0xcbf29ce484222325L;
        int markerRooms = 0;
        for (WoodlandMansionPieces.MansionTemplate piece : pieces) {
            String name = (String)nameField.get(piece);
            Rotation rotation = (Rotation)rotationField.get(piece);
            Mirror mirror = (Mirror)mirrorField.get(piece);
            BlockPos pos = (BlockPos)positionField.get(piece);
            StructureBoundingBox box = piece.getBoundingBox();
            hash = addString(hash, name);
            hash = add(hash, pos.getX()); hash = add(hash, pos.getY());
            hash = add(hash, pos.getZ()); hash = add(hash, rotation.ordinal());
            hash = add(hash, mirror.ordinal());
            hash = add(hash, box.minX); hash = add(hash, box.minY);
            hash = add(hash, box.minZ); hash = add(hash, box.maxX);
            hash = add(hash, box.maxY); hash = add(hash, box.maxZ);
            if (name.startsWith("1x") || name.startsWith("2x"))
                ++markerRooms;
            if (System.getenv("MANSION_VERBOSE") != null)
                System.out.printf("P %s %d %d %d %d %d %d %d %d %d %d %d %d%n",
                    name, pos.getX(), pos.getY(), pos.getZ(), rotation.ordinal(),
                    mirror.ordinal(), box.minX, box.minY, box.minZ,
                    box.maxX, box.maxY, box.maxZ);
        }
        System.out.printf("%d %d %d %d %016x%n", seed, rotationIndex,
            pieces.size(), markerRooms, hash);
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        one(0L, 0);
        one(1L, 1);
        one(123456789L, 2);
        one(-99887766L, 3);
        one(0x5eed5eedL, 3);
    }
}
