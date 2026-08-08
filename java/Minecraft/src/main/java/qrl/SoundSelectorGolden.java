package qrl;

import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.client.audio.ISoundEventAccessor;
import net.minecraft.client.audio.Sound;
import net.minecraft.client.audio.SoundEventAccessor;
import net.minecraft.client.audio.SoundHandler;
import net.minecraft.util.ResourceLocation;

/** Direct SoundEventAccessor selection oracle for the generated sound graph. */
public final class SoundSelectorGolden {
    private static final class Edge {
        int node, target, variant, weight;
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
        int nodeCount = 0, soundCount = 0;
        List<long[]> nodeKeys = new ArrayList<long[]>();
        List<Edge> edges = new ArrayList<Edge>();
        List<int[]> roots = new ArrayList<int[]>();
        for (String line : lines) {
            if (line.isEmpty() || line.charAt(0) == '#') continue;
            String[] field = line.split("\\t");
            if (field[0].equals("node")) {
                int node = Integer.parseInt(field[1]);
                nodeCount = Math.max(nodeCount, node + 1);
                nodeKeys.add(new long[] {node,
                    Long.parseUnsignedLong(field[3], 16)});
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
        SoundEventAccessor[] nodes = new SoundEventAccessor[nodeCount];
        Field randomField = SoundEventAccessor.class.getDeclaredField("rnd");
        randomField.setAccessible(true);
        long seed = Long.decode(args[1]).longValue();
        long[] seedKeys = new long[nodeCount];
        for (long[] row : nodeKeys) seedKeys[(int)row[0]] = row[1];
        for (int node = 0; node < nodeCount; ++node) {
            nodes[node] = new SoundEventAccessor(
                new ResourceLocation("qrl", "node_" + node), null);
            ((Random)randomField.get(nodes[node])).setSeed(seed ^ seedKeys[node]);
        }
        for (Edge edge : edges) {
            if (edge.target >= 0) {
                nodes[edge.node].addSound(new Ref(nodes[edge.target]));
            } else {
                nodes[edge.node].addSound(new Sound(
                    "qrl:v/" + edge.variant, 1.0F, 1.0F,
                    edge.weight, Sound.Type.FILE, false));
            }
        }
        int[] rootBySound = new int[soundCount];
        java.util.Arrays.fill(rootBySound, -1);
        for (int[] root : roots) rootBySound[root[0]] = root[1];
        for (int sound = 1; sound < soundCount; ++sound) {
            for (int draw = 0; draw < 16; ++draw) {
                int variant = -1;
                if (rootBySound[sound] >= 0) {
                    Sound selected = nodes[rootBySound[sound]].cloneEntry();
                    if (selected != SoundHandler.MISSING_SOUND) {
                        String path = selected.getSoundLocation()
                            .getResourcePath();
                        variant = Integer.parseInt(path.substring(2));
                    }
                }
                System.out.println(sound + " " + draw + " " + variant);
            }
        }
    }
}
