package qrl;

import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import net.minecraft.client.audio.ISoundEventAccessor;
import net.minecraft.client.audio.Sound;
import net.minecraft.client.audio.SoundEventAccessor;
import net.minecraft.client.audio.SoundHandler;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.math.MathHelper;

/** Device-independent SoundManager source descriptor oracle. */
public final class SoundSourceGolden {
    private static final class Edge { int node, target, variant, weight; }
    private static final class Variant {
        float volume, pitch; boolean stream;
    }
    private static final class Ref implements ISoundEventAccessor<Sound> {
        final SoundEventAccessor target;
        Ref(SoundEventAccessor targetIn) { target = targetIn; }
        public int getWeight() { return target.getWeight(); }
        public Sound cloneEntry() { return target.cloneEntry(); }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2)
            throw new IllegalArgumentException("spec and seed required");
        List<String> lines = Files.readAllLines(
            Paths.get(args[0]), StandardCharsets.UTF_8);
        int nodeCount = 0, soundCount = 0, variantCount = 0;
        List<long[]> nodeKeys = new ArrayList<long[]>();
        List<Edge> edges = new ArrayList<Edge>();
        List<String[]> variantRows = new ArrayList<String[]>();
        List<int[]> roots = new ArrayList<int[]>();
        for (String line : lines) {
            if (line.isEmpty() || line.charAt(0) == '#') continue;
            String[] field = line.split("\\t");
            if (field[0].equals("node")) {
                int node = Integer.parseInt(field[1]);
                nodeCount = Math.max(nodeCount, node + 1);
                nodeKeys.add(new long[] {node,
                    Long.parseUnsignedLong(field[3], 16)});
            } else if (field[0].equals("variant")) {
                variantRows.add(field);
                variantCount = Math.max(
                    variantCount, Integer.parseInt(field[1]) + 1);
            } else if (field[0].equals("edge")) {
                Edge edge = new Edge();
                edge.node = Integer.parseInt(field[1]);
                edge.target = Integer.parseInt(field[2]);
                edge.variant = Integer.parseInt(field[3]);
                edge.weight = Integer.parseInt(field[4]);
                edges.add(edge);
            } else if (field[0].equals("root")) {
                int sound = Integer.parseInt(field[1]);
                roots.add(new int[] {sound, Integer.parseInt(field[2])});
                soundCount = Math.max(soundCount, sound + 1);
            }
        }
        Variant[] variants = new Variant[variantCount];
        for (String[] row : variantRows) {
            int index = Integer.parseInt(row[1]);
            Variant variant = new Variant();
            variant.volume = Float.parseFloat(row[3]);
            variant.pitch = Float.parseFloat(row[4]);
            variant.stream = !row[6].equals("0");
            variants[index] = variant;
        }
        long seed = Long.decode(args[1]).longValue();
        long[] seedKeys = new long[nodeCount];
        for (long[] row : nodeKeys) seedKeys[(int)row[0]] = row[1];
        int[] rootBySound = new int[soundCount];
        Arrays.fill(rootBySound, -1);
        for (int[] root : roots) rootBySound[root[0]] = root[1];
        float[] volumes = {-0.5F, 0.25F, 1.0F, 4.0F};
        float[] pitches = {0.1F, 1.0F, 3.0F, 0.75F};
        float[] categories = {1.0F, 0.25F, 1.0F, 0.0F};
        Field randomField = SoundEventAccessor.class.getDeclaredField("rnd");
        randomField.setAccessible(true);
        for (int sound = 1; sound < soundCount; ++sound) {
            for (int test = 0; test < 4; ++test) {
                SoundEventAccessor[] nodes = new SoundEventAccessor[nodeCount];
                for (int node = 0; node < nodeCount; ++node) {
                    nodes[node] = new SoundEventAccessor(
                        new ResourceLocation("qrl", "node_" + node), null);
                    ((Random)randomField.get(nodes[node])).setSeed(
                        seed ^ seedKeys[node]);
                }
                for (Edge edge : edges) {
                    if (edge.target >= 0) {
                        nodes[edge.node].addSound(new Ref(nodes[edge.target]));
                    } else {
                        Variant variant = variants[edge.variant];
                        nodes[edge.node].addSound(new Sound(
                            "qrl:v/" + edge.variant, variant.volume,
                            variant.pitch, edge.weight, Sound.Type.FILE,
                            variant.stream));
                    }
                }
                int selectedIndex = -1;
                Sound selected = SoundHandler.MISSING_SOUND;
                if (rootBySound[sound] >= 0)
                    selected = nodes[rootBySound[sound]].cloneEntry();
                if (selected != SoundHandler.MISSING_SOUND) {
                    String path = selected.getSoundLocation().getResourcePath();
                    selectedIndex = Integer.parseInt(path.substring(2));
                }
                float gain = 0.0F, pitch = 0.0F, range = 0.0F;
                int stream = 0;
                if (selectedIndex >= 0) {
                    float rawVolume = volumes[test] * selected.getVolume();
                    gain = MathHelper.clamp(
                        rawVolume * categories[test], 0.0F, 1.0F);
                    pitch = MathHelper.clamp(
                        pitches[test] * selected.getPitch(), 0.5F, 2.0F);
                    range = 16.0F * (rawVolume > 1.0F ? rawVolume : 1.0F);
                    stream = selected.isStreaming() ? 1 : 0;
                }
                System.out.printf("%d %d %d %08x %08x %08x %d%n",
                    sound, test, selectedIndex,
                    Float.floatToRawIntBits(gain),
                    Float.floatToRawIntBits(pitch),
                    Float.floatToRawIntBits(range), stream);
            }
        }
    }
}
