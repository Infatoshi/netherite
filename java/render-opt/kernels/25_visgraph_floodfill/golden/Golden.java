// GOLDEN: verbatim-logic port of MC 1.11.2 VisGraph.computeVisibility() + floodFill +
// getNeighborIndexAtFace + addEdges + INDEX_OF_EDGES static init + SetVisibility.
// Source: src/net/minecraft/client/renderer/chunk/VisGraph.java:40 (+ SetVisibility.java).
// The algorithm body is copied UNCHANGED. Only standalone-uncompilable deps are reduced (none
// affect the output):
//   - com.google.common.collect.Queues.newArrayDeque() -> new java.util.ArrayDeque<>() (same FIFO)
//   - IntegerCache.getInteger(j) -> Integer.valueOf(j) (boxing only, output-irrelevant)
//   - net.minecraft.util.EnumFacing -> a local enum (same order D-U-N-S-W-E, same switch cases)
//   - BlockPos avoided: opaque cells are set by raw index via setOpaqueIndex() (same as
//     setOpaqueCube(): bitSet.set(i,true); --empty).
// Input  (per line): count(set bits) then 64 hex words (16 hex digits each); bit i of the 4096-bit
//   opaque set = word[i>>6] bit (i&63).
// Output (per line): the packed SetVisibility 6x6 bitset as a hex long (bit = a + b*6 for face
//   ordinals a,b; set iff face a sees face b).
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayDeque;
import java.util.BitSet;
import java.util.EnumSet;
import java.util.Queue;
import java.util.Set;

public class Golden {
    // Local stand-in for net.minecraft.util.EnumFacing (order D-U-N-S-W-E).
    enum EnumFacing { DOWN, UP, NORTH, SOUTH, WEST, EAST }

    // ===== SetVisibility (verbatim) =====
    static class SetVisibility {
        private static final int COUNT_FACES = EnumFacing.values().length;
        private final BitSet bitSet;

        public SetVisibility() {
            this.bitSet = new BitSet(COUNT_FACES * COUNT_FACES);
        }

        public void setManyVisible(Set<EnumFacing> facing) {
            for (EnumFacing enumfacing : facing) {
                for (EnumFacing enumfacing1 : facing) {
                    this.setVisible(enumfacing, enumfacing1, true);
                }
            }
        }

        public void setVisible(EnumFacing facing, EnumFacing facing2, boolean p_178619_3_) {
            this.bitSet.set(facing.ordinal() + facing2.ordinal() * COUNT_FACES, p_178619_3_);
            this.bitSet.set(facing2.ordinal() + facing.ordinal() * COUNT_FACES, p_178619_3_);
        }

        public void setAllVisible(boolean visible) {
            this.bitSet.set(0, this.bitSet.size(), visible);
        }

        public boolean isVisible(EnumFacing facing, EnumFacing facing2) {
            return this.bitSet.get(facing.ordinal() + facing2.ordinal() * COUNT_FACES);
        }
    }

    // ===== VisGraph (verbatim algorithm) =====
    static class VisGraph {
        private static final int DX = (int) Math.pow(16.0D, 0.0D);
        private static final int DZ = (int) Math.pow(16.0D, 1.0D);
        private static final int DY = (int) Math.pow(16.0D, 2.0D);
        private final BitSet bitSet = new BitSet(4096);
        private static final int[] INDEX_OF_EDGES = new int[1352];
        private int empty = 4096;

        // stand-in for setOpaqueCube(BlockPos): same effect (bitSet.set(i,true); --empty).
        public void setOpaqueIndex(int i) {
            this.bitSet.set(i, true);
            --this.empty;
        }

        private static int getIndex(int x, int y, int z) {
            return x << 0 | y << 8 | z << 4;
        }

        public SetVisibility computeVisibility() {
            SetVisibility setvisibility = new SetVisibility();

            if (4096 - this.empty < 256) {
                setvisibility.setAllVisible(true);
            } else if (this.empty == 0) {
                setvisibility.setAllVisible(false);
            } else {
                for (int i : INDEX_OF_EDGES) {
                    if (!this.bitSet.get(i)) {
                        setvisibility.setManyVisible(this.floodFill(i));
                    }
                }
            }

            return setvisibility;
        }

        private Set<EnumFacing> floodFill(int p_178604_1_) {
            Set<EnumFacing> set = EnumSet.<EnumFacing>noneOf(EnumFacing.class);
            Queue<Integer> queue = new ArrayDeque<Integer>();
            queue.add(Integer.valueOf(p_178604_1_));
            this.bitSet.set(p_178604_1_, true);

            while (!((Queue) queue).isEmpty()) {
                int i = ((Integer) queue.poll()).intValue();
                this.addEdges(i, set);

                for (EnumFacing enumfacing : EnumFacing.values()) {
                    int j = this.getNeighborIndexAtFace(i, enumfacing);

                    if (j >= 0 && !this.bitSet.get(j)) {
                        this.bitSet.set(j, true);
                        queue.add(Integer.valueOf(j));
                    }
                }
            }

            return set;
        }

        private void addEdges(int p_178610_1_, Set<EnumFacing> p_178610_2_) {
            int i = p_178610_1_ >> 0 & 15;

            if (i == 0) {
                p_178610_2_.add(EnumFacing.WEST);
            } else if (i == 15) {
                p_178610_2_.add(EnumFacing.EAST);
            }

            int j = p_178610_1_ >> 8 & 15;

            if (j == 0) {
                p_178610_2_.add(EnumFacing.DOWN);
            } else if (j == 15) {
                p_178610_2_.add(EnumFacing.UP);
            }

            int k = p_178610_1_ >> 4 & 15;

            if (k == 0) {
                p_178610_2_.add(EnumFacing.NORTH);
            } else if (k == 15) {
                p_178610_2_.add(EnumFacing.SOUTH);
            }
        }

        private int getNeighborIndexAtFace(int p_178603_1_, EnumFacing facing) {
            switch (facing) {
                case DOWN:
                    if ((p_178603_1_ >> 8 & 15) == 0) {
                        return -1;
                    }
                    return p_178603_1_ - DY;
                case UP:
                    if ((p_178603_1_ >> 8 & 15) == 15) {
                        return -1;
                    }
                    return p_178603_1_ + DY;
                case NORTH:
                    if ((p_178603_1_ >> 4 & 15) == 0) {
                        return -1;
                    }
                    return p_178603_1_ - DZ;
                case SOUTH:
                    if ((p_178603_1_ >> 4 & 15) == 15) {
                        return -1;
                    }
                    return p_178603_1_ + DZ;
                case WEST:
                    if ((p_178603_1_ >> 0 & 15) == 0) {
                        return -1;
                    }
                    return p_178603_1_ - DX;
                case EAST:
                    if ((p_178603_1_ >> 0 & 15) == 15) {
                        return -1;
                    }
                    return p_178603_1_ + DX;
                default:
                    return -1;
            }
        }

        static {
            int i = 0;
            int j = 15;
            int k = 0;

            for (int l = 0; l < 16; ++l) {
                for (int i1 = 0; i1 < 16; ++i1) {
                    for (int j1 = 0; j1 < 16; ++j1) {
                        if (l == 0 || l == 15 || i1 == 0 || i1 == 15 || j1 == 0 || j1 == 15) {
                            INDEX_OF_EDGES[k++] = getIndex(l, i1, j1);
                        }
                    }
                }
            }
        }
    }
    // ===== /verbatim =====

    public static void main(String[] args) throws IOException {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        StringBuilder sb = new StringBuilder();
        EnumFacing[] faces = EnumFacing.values();
        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;
            String[] tok = line.split("\\s+");
            if (tok.length != 65) continue;
            int count = Integer.parseInt(tok[0]);
            long[] words = new long[64];
            for (int w = 0; w < 64; ++w) words[w] = Long.parseUnsignedLong(tok[1 + w], 16);

            VisGraph vg = new VisGraph();
            for (int idx = 0; idx < 4096; ++idx) {
                if (((words[idx >> 6] >>> (idx & 63)) & 1L) != 0L) {
                    vg.setOpaqueIndex(idx);
                }
            }
            // sanity: count must equal popcount (gen_inputs guarantees it); kept as documentation.
            SetVisibility sv = vg.computeVisibility();

            long packed = 0L;
            for (int a = 0; a < 6; ++a) {
                for (int b = 0; b < 6; ++b) {
                    if (sv.isVisible(faces[a], faces[b])) {
                        packed |= 1L << (a + b * 6);
                    }
                }
            }
            sb.append(Long.toHexString(packed)).append('\n');
        }
        System.out.print(sb);
    }
}
