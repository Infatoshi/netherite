import java.util.Random;
// Verbatim MC 1.11.2 GenLayer biome stack (net/minecraft/world/gen/layer/*.java) assembled by
// GenLayer.initializeAllBiomeGenerators(seed, WorldType.DEFAULT, null) +
// WorldType.DEFAULT.getBiomeLayer(...). The GenLayer ALGORITHM (seeding LCG, nextInt, every
// subclass getInts) is the decompiled MC code unchanged. The only substitutions are:
//   * The Biome OBJECT registry -> exact vanilla integer ids + metadata tables (class BM below),
//     extracted from Biome.registerBiomes / the Biome subclasses / forge BiomeManager. The task
//     sanctions replacing registry lookups with the exact integer ids vanilla returns.
//   * IntCache (pure int[] pooling, no logic; every getInts fully overwrites its output before
//     any read) -> plain `new int[n]`.
// This is the vanilla ground truth for cpu/genlayer_biomes.c + cuda/genlayer_biomes.cu. It keeps
// the real OOP structure so initWorldGenSeed's recursion (incl. GenLayerHills NOT seeding its
// riverLayer parent -> that layer keeps worldGenSeed 0, a vanilla quirk) is reproduced naturally.
//
// Output: biomeIndexLayer.getInts(0,0,16,16) -> 256 biome ids row-major, each %08x, one per line.
public class Golden {

    // ===== biome ids (Biome.registerBiomes) =====
    static final int OCEAN=0, PLAINS=1, DESERT=2, EXTREME_HILLS=3, FOREST=4, TAIGA=5, SWAMP=6,
        RIVER=7, FROZEN_OCEAN=10, FROZEN_RIVER=11, ICE_PLAINS=12, ICE_MOUNTAINS=13, MUSHROOM=14,
        MUSHROOM_SHORE=15, BEACH=16, DESERT_HILLS=17, FOREST_HILLS=18, TAIGA_HILLS=19,
        EXTREME_HILLS_EDGE=20, JUNGLE=21, JUNGLE_HILLS=22, JUNGLE_EDGE=23, DEEP_OCEAN=24,
        STONE_BEACH=25, COLD_BEACH=26, BIRCH_FOREST=27, BIRCH_FOREST_HILLS=28, ROOFED_FOREST=29,
        COLD_TAIGA=30, COLD_TAIGA_HILLS=31, REDWOOD_TAIGA=32, REDWOOD_TAIGA_HILLS=33,
        EXTREME_HILLS_WITH_TREES=34, SAVANNA=35, SAVANNA_PLATEAU=36, MESA=37, MESA_ROCK=38,
        MESA_CLEAR_ROCK=39, MUTATED_PLAINS=129;

    // ===== Biome registry metadata substitution (BM) =====
    // class groups (== getBiomeClass() identity). BiomeForestMutated extends BiomeForest and
    // BiomeSavannaMutated extends BiomeSavanna, inheriting the hardcoded getBiomeClass() return.
    static final int CLS_NONE=-1, CLS_OCEAN=0, CLS_PLAINS=1, CLS_DESERT=2, CLS_HILLS=3, CLS_FOREST=4,
        CLS_TAIGA=5, CLS_SWAMP=6, CLS_RIVER=7, CLS_HELL=8, CLS_END=9, CLS_SNOW=10, CLS_MUSHROOM=11,
        CLS_BEACH=12, CLS_JUNGLE=13, CLS_STONEBEACH=14, CLS_SAVANNA=15, CLS_MESA=16, CLS_VOID=17;
    static final int TC_OCEAN=0, TC_COLD=1, TC_MEDIUM=2, TC_WARM=3;

    static class BM {
        static boolean valid(int id) {
            switch (id) {
                case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
                case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18:
                case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27:
                case 28: case 29: case 30: case 31: case 32: case 33: case 34: case 35: case 36:
                case 37: case 38: case 39: case 127:
                case 129: case 130: case 131: case 132: case 133: case 134: case 140:
                case 149: case 151: case 155: case 156: case 157: case 158:
                case 160: case 161: case 162: case 163: case 164: case 165: case 166: case 167:
                    return true;
                default: return false;
            }
        }
        static int cls(int id) {
            switch (id) {
                case 0: case 10: case 24: return CLS_OCEAN;
                case 1: case 129: return CLS_PLAINS;
                case 2: case 17: case 130: return CLS_DESERT;
                case 3: case 20: case 34: case 131: case 162: return CLS_HILLS;
                case 4: case 18: case 27: case 28: case 29: case 132: case 155: case 156: case 157:
                    return CLS_FOREST;
                case 5: case 19: case 30: case 31: case 32: case 33: case 133: case 158: case 160: case 161:
                    return CLS_TAIGA;
                case 6: case 134: return CLS_SWAMP;
                case 7: case 11: return CLS_RIVER;
                case 8: return CLS_HELL;
                case 9: return CLS_END;
                case 12: case 13: case 140: return CLS_SNOW;
                case 14: case 15: return CLS_MUSHROOM;
                case 16: case 26: return CLS_BEACH;
                case 21: case 22: case 23: case 149: case 151: return CLS_JUNGLE;
                case 25: return CLS_STONEBEACH;
                case 35: case 36: case 163: case 164: return CLS_SAVANNA;
                case 37: case 38: case 39: case 165: case 166: case 167: return CLS_MESA;
                case 127: return CLS_VOID;
                default: return CLS_NONE;
            }
        }
        static int temp(int id) {
            switch (id) {
                case 0: case 10: case 24: return TC_OCEAN;
                case 11: case 12: case 13: case 26: case 30: case 31: case 140: case 158:
                    return TC_COLD;
                case 2: case 8: case 17: case 35: case 36: case 37: case 38: case 39:
                case 130: case 163: case 164: case 165: case 166: case 167:
                    return TC_WARM;
                default: return TC_MEDIUM;
            }
        }
        static boolean snowy(int id) {
            switch (id) {
                case 10: case 11: case 12: case 13: case 26: case 30: case 31: case 140: case 158:
                    return true;
                default: return false;
            }
        }
        static boolean isMutation(int id) {
            switch (id) {
                case 129: case 130: case 131: case 132: case 133: case 134: case 140:
                case 149: case 151: case 155: case 156: case 157: case 158:
                case 160: case 161: case 162: case 163: case 164: case 165: case 166: case 167:
                    return true;
                default: return false;
            }
        }
        static int mutationFor(int baseId) {
            switch (baseId) {
                case 1:  return 129; case 2:  return 130; case 3:  return 131; case 4:  return 132;
                case 5:  return 133; case 6:  return 134; case 12: return 140; case 21: return 149;
                case 23: return 151; case 27: return 155; case 28: return 156; case 29: return 157;
                case 30: return 158; case 32: return 160; case 33: return 161; case 34: return 162;
                case 35: return 163; case 36: return 164; case 37: return 165; case 38: return 166;
                case 39: return 167;
                default: return -1;
            }
        }
        static boolean isOcean(int id) { return id == 0 || id == 10 || id == 24; }
        static boolean isMesa(int id) {
            return id == 37 || id == 38 || id == 39 || id == 165 || id == 166 || id == 167;
        }
    }

    // IntCache substitute (pooling only; no logic effect since every getInts fully writes output)
    static class IntCache {
        static int[] getIntCache(int n) { return new int[n]; }
        static void resetIntCache() {}
    }

    // ===== verbatim GenLayer base (registry calls -> BM) =====
    static abstract class GenLayer {
        private long worldGenSeed;
        protected GenLayer parent;
        private long chunkSeed;
        protected long baseSeed;

        public GenLayer(long p_i2125_1_) {
            this.baseSeed = p_i2125_1_;
            this.baseSeed *= this.baseSeed * 6364136223846793005L + 1442695040888963407L;
            this.baseSeed += p_i2125_1_;
            this.baseSeed *= this.baseSeed * 6364136223846793005L + 1442695040888963407L;
            this.baseSeed += p_i2125_1_;
            this.baseSeed *= this.baseSeed * 6364136223846793005L + 1442695040888963407L;
            this.baseSeed += p_i2125_1_;
        }

        public void initWorldGenSeed(long seed) {
            this.worldGenSeed = seed;
            if (this.parent != null) {
                this.parent.initWorldGenSeed(seed);
            }
            this.worldGenSeed *= this.worldGenSeed * 6364136223846793005L + 1442695040888963407L;
            this.worldGenSeed += this.baseSeed;
            this.worldGenSeed *= this.worldGenSeed * 6364136223846793005L + 1442695040888963407L;
            this.worldGenSeed += this.baseSeed;
            this.worldGenSeed *= this.worldGenSeed * 6364136223846793005L + 1442695040888963407L;
            this.worldGenSeed += this.baseSeed;
        }

        public void initChunkSeed(long p_75903_1_, long p_75903_3_) {
            this.chunkSeed = this.worldGenSeed;
            this.chunkSeed *= this.chunkSeed * 6364136223846793005L + 1442695040888963407L;
            this.chunkSeed += p_75903_1_;
            this.chunkSeed *= this.chunkSeed * 6364136223846793005L + 1442695040888963407L;
            this.chunkSeed += p_75903_3_;
            this.chunkSeed *= this.chunkSeed * 6364136223846793005L + 1442695040888963407L;
            this.chunkSeed += p_75903_1_;
            this.chunkSeed *= this.chunkSeed * 6364136223846793005L + 1442695040888963407L;
            this.chunkSeed += p_75903_3_;
        }

        protected int nextInt(int p_75902_1_) {
            int i = (int)((this.chunkSeed >> 24) % (long)p_75902_1_);
            if (i < 0) {
                i += p_75902_1_;
            }
            this.chunkSeed *= this.chunkSeed * 6364136223846793005L + 1442695040888963407L;
            this.chunkSeed += this.worldGenSeed;
            return i;
        }

        public abstract int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight);

        // biomesEqualOrMesaPlateau: registry -> BM (object identity == reduces to id equality,
        // already covered by a==b; class-equality otherwise; MESA_ROCK/CLEAR_ROCK special case)
        protected static boolean biomesEqualOrMesaPlateau(int biomeIDA, int biomeIDB) {
            if (biomeIDA == biomeIDB) {
                return true;
            } else if (!BM.valid(biomeIDA) || !BM.valid(biomeIDB)) {
                return false;
            } else if (biomeIDA == MESA_ROCK || biomeIDA == MESA_CLEAR_ROCK) {
                return biomeIDB == MESA_ROCK || biomeIDB == MESA_CLEAR_ROCK;
            } else {
                return BM.cls(biomeIDA) == BM.cls(biomeIDB);
            }
        }

        protected static boolean isBiomeOceanic(int p_151618_0_) {
            return BM.isOcean(p_151618_0_);
        }

        protected int selectRandom(int... p_151619_1_) {
            return p_151619_1_[this.nextInt(p_151619_1_.length)];
        }

        protected int selectModeOrRandom(int p_151617_1_, int p_151617_2_, int p_151617_3_, int p_151617_4_) {
            return p_151617_2_ == p_151617_3_ && p_151617_3_ == p_151617_4_ ? p_151617_2_ : (p_151617_1_ == p_151617_2_ && p_151617_1_ == p_151617_3_ ? p_151617_1_ : (p_151617_1_ == p_151617_2_ && p_151617_1_ == p_151617_4_ ? p_151617_1_ : (p_151617_1_ == p_151617_3_ && p_151617_1_ == p_151617_4_ ? p_151617_1_ : (p_151617_1_ == p_151617_2_ && p_151617_3_ != p_151617_4_ ? p_151617_1_ : (p_151617_1_ == p_151617_3_ && p_151617_2_ != p_151617_4_ ? p_151617_1_ : (p_151617_1_ == p_151617_4_ && p_151617_2_ != p_151617_3_ ? p_151617_1_ : (p_151617_2_ == p_151617_3_ && p_151617_1_ != p_151617_4_ ? p_151617_2_ : (p_151617_2_ == p_151617_4_ && p_151617_1_ != p_151617_3_ ? p_151617_2_ : (p_151617_3_ == p_151617_4_ && p_151617_1_ != p_151617_2_ ? p_151617_3_ : this.selectRandom(new int[] {p_151617_1_, p_151617_2_, p_151617_3_, p_151617_4_}))))))))));
        }
    }

    static class GenLayerIsland extends GenLayer {
        public GenLayerIsland(long p_i2124_1_) { super(p_i2124_1_); }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(areaX + j), (long)(areaY + i));
                    aint[j + i * areaWidth] = this.nextInt(10) == 0 ? 1 : 0;
                }
            }
            if (areaX > -areaWidth && areaX <= 0 && areaY > -areaHeight && areaY <= 0) {
                aint[-areaX + -areaY * areaWidth] = 1;
            }
            return aint;
        }
    }

    static class GenLayerZoom extends GenLayer {
        public GenLayerZoom(long p_i2134_1_, GenLayer p_i2134_3_) {
            super(p_i2134_1_);
            super.parent = p_i2134_3_;
        }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX >> 1;
            int j = areaY >> 1;
            int k = (areaWidth >> 1) + 2;
            int l = (areaHeight >> 1) + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int i1 = k - 1 << 1;
            int j1 = l - 1 << 1;
            int[] aint1 = IntCache.getIntCache(i1 * j1);
            for (int k1 = 0; k1 < l - 1; ++k1) {
                int l1 = (k1 << 1) * i1;
                int i2 = 0;
                int j2 = aint[i2 + 0 + (k1 + 0) * k];
                for (int k2 = aint[i2 + 0 + (k1 + 1) * k]; i2 < k - 1; ++i2) {
                    this.initChunkSeed((long)(i2 + i << 1), (long)(k1 + j << 1));
                    int l2 = aint[i2 + 1 + (k1 + 0) * k];
                    int i3 = aint[i2 + 1 + (k1 + 1) * k];
                    aint1[l1] = j2;
                    aint1[l1++ + i1] = this.selectRandom(new int[] {j2, k2});
                    aint1[l1] = this.selectRandom(new int[] {j2, l2});
                    aint1[l1++ + i1] = this.selectModeOrRandom(j2, l2, k2, i3);
                    j2 = l2;
                    k2 = i3;
                }
            }
            int[] aint2 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int j3 = 0; j3 < areaHeight; ++j3) {
                System.arraycopy(aint1, (j3 + (areaY & 1)) * i1 + (areaX & 1), aint2, j3 * areaWidth, areaWidth);
            }
            return aint2;
        }
        public static GenLayer magnify(long p_75915_0_, GenLayer p_75915_2_, int p_75915_3_) {
            GenLayer genlayer = p_75915_2_;
            for (int i = 0; i < p_75915_3_; ++i) {
                genlayer = new GenLayerZoom(p_75915_0_ + (long)i, genlayer);
            }
            return genlayer;
        }
    }

    static class GenLayerFuzzyZoom extends GenLayerZoom {
        public GenLayerFuzzyZoom(long p_i2123_1_, GenLayer p_i2123_3_) { super(p_i2123_1_, p_i2123_3_); }
        protected int selectModeOrRandom(int p_151617_1_, int p_151617_2_, int p_151617_3_, int p_151617_4_) {
            return this.selectRandom(new int[] {p_151617_1_, p_151617_2_, p_151617_3_, p_151617_4_});
        }
    }

    static class GenLayerAddIsland extends GenLayer {
        public GenLayerAddIsland(long p_i2119_1_, GenLayer p_i2119_3_) { super(p_i2119_1_); this.parent = p_i2119_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 0 + (i1 + 0) * k];
                    int l1 = aint[j1 + 2 + (i1 + 0) * k];
                    int i2 = aint[j1 + 0 + (i1 + 2) * k];
                    int j2 = aint[j1 + 2 + (i1 + 2) * k];
                    int k2 = aint[j1 + 1 + (i1 + 1) * k];
                    this.initChunkSeed((long)(j1 + areaX), (long)(i1 + areaY));
                    if (k2 != 0 || k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0) {
                        if (k2 > 0 && (k1 == 0 || l1 == 0 || i2 == 0 || j2 == 0)) {
                            if (this.nextInt(5) == 0) {
                                if (k2 == 4) { aint1[j1 + i1 * areaWidth] = 4; }
                                else { aint1[j1 + i1 * areaWidth] = 0; }
                            } else { aint1[j1 + i1 * areaWidth] = k2; }
                        } else { aint1[j1 + i1 * areaWidth] = k2; }
                    } else {
                        int l2 = 1;
                        int i3 = 1;
                        if (k1 != 0 && this.nextInt(l2++) == 0) { i3 = k1; }
                        if (l1 != 0 && this.nextInt(l2++) == 0) { i3 = l1; }
                        if (i2 != 0 && this.nextInt(l2++) == 0) { i3 = i2; }
                        if (j2 != 0 && this.nextInt(l2++) == 0) { i3 = j2; }
                        if (this.nextInt(3) == 0) { aint1[j1 + i1 * areaWidth] = i3; }
                        else if (i3 == 4) { aint1[j1 + i1 * areaWidth] = 4; }
                        else { aint1[j1 + i1 * areaWidth] = 0; }
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerRemoveTooMuchOcean extends GenLayer {
        public GenLayerRemoveTooMuchOcean(long p_i45480_1_, GenLayer p_i45480_3_) { super(p_i45480_1_); this.parent = p_i45480_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (areaWidth + 2)];
                    int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (areaWidth + 2)];
                    int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (areaWidth + 2)];
                    int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (areaWidth + 2)];
                    int k2 = aint[j1 + 1 + (i1 + 1) * k];
                    aint1[j1 + i1 * areaWidth] = k2;
                    this.initChunkSeed((long)(j1 + areaX), (long)(i1 + areaY));
                    if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && this.nextInt(2) == 0) {
                        aint1[j1 + i1 * areaWidth] = 1;
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerAddSnow extends GenLayer {
        public GenLayerAddSnow(long p_i2121_1_, GenLayer p_i2121_3_) { super(p_i2121_1_); this.parent = p_i2121_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 1 + (i1 + 1) * k];
                    this.initChunkSeed((long)(j1 + areaX), (long)(i1 + areaY));
                    if (k1 == 0) { aint1[j1 + i1 * areaWidth] = 0; }
                    else {
                        int l1 = this.nextInt(6);
                        if (l1 == 0) { l1 = 4; }
                        else if (l1 <= 1) { l1 = 3; }
                        else { l1 = 1; }
                        aint1[j1 + i1 * areaWidth] = l1;
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerEdge extends GenLayer {
        public static enum Mode { COOL_WARM, HEAT_ICE, SPECIAL; }
        private final Mode mode;
        public GenLayerEdge(long p_i45474_1_, GenLayer p_i45474_3_, Mode p_i45474_4_) {
            super(p_i45474_1_); this.parent = p_i45474_3_; this.mode = p_i45474_4_;
        }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            switch (this.mode) {
                case COOL_WARM: default: return this.getIntsCoolWarm(areaX, areaY, areaWidth, areaHeight);
                case HEAT_ICE: return this.getIntsHeatIce(areaX, areaY, areaWidth, areaHeight);
                case SPECIAL: return this.getIntsSpecial(areaX, areaY, areaWidth, areaHeight);
            }
        }
        private int[] getIntsCoolWarm(int p_151626_1_, int p_151626_2_, int p_151626_3_, int p_151626_4_) {
            int i = p_151626_1_ - 1;
            int j = p_151626_2_ - 1;
            int k = 1 + p_151626_3_ + 1;
            int l = 1 + p_151626_4_ + 1;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(p_151626_3_ * p_151626_4_);
            for (int i1 = 0; i1 < p_151626_4_; ++i1) {
                for (int j1 = 0; j1 < p_151626_3_; ++j1) {
                    this.initChunkSeed((long)(j1 + p_151626_1_), (long)(i1 + p_151626_2_));
                    int k1 = aint[j1 + 1 + (i1 + 1) * k];
                    if (k1 == 1) {
                        int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                        int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                        int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                        int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                        boolean flag = l1 == 3 || i2 == 3 || j2 == 3 || k2 == 3;
                        boolean flag1 = l1 == 4 || i2 == 4 || j2 == 4 || k2 == 4;
                        if (flag || flag1) { k1 = 2; }
                    }
                    aint1[j1 + i1 * p_151626_3_] = k1;
                }
            }
            return aint1;
        }
        private int[] getIntsHeatIce(int p_151624_1_, int p_151624_2_, int p_151624_3_, int p_151624_4_) {
            int i = p_151624_1_ - 1;
            int j = p_151624_2_ - 1;
            int k = 1 + p_151624_3_ + 1;
            int l = 1 + p_151624_4_ + 1;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(p_151624_3_ * p_151624_4_);
            for (int i1 = 0; i1 < p_151624_4_; ++i1) {
                for (int j1 = 0; j1 < p_151624_3_; ++j1) {
                    int k1 = aint[j1 + 1 + (i1 + 1) * k];
                    if (k1 == 4) {
                        int l1 = aint[j1 + 1 + (i1 + 1 - 1) * k];
                        int i2 = aint[j1 + 1 + 1 + (i1 + 1) * k];
                        int j2 = aint[j1 + 1 - 1 + (i1 + 1) * k];
                        int k2 = aint[j1 + 1 + (i1 + 1 + 1) * k];
                        boolean flag = l1 == 2 || i2 == 2 || j2 == 2 || k2 == 2;
                        boolean flag1 = l1 == 1 || i2 == 1 || j2 == 1 || k2 == 1;
                        if (flag1 || flag) { k1 = 3; }
                    }
                    aint1[j1 + i1 * p_151624_3_] = k1;
                }
            }
            return aint1;
        }
        private int[] getIntsSpecial(int p_151625_1_, int p_151625_2_, int p_151625_3_, int p_151625_4_) {
            int[] aint = this.parent.getInts(p_151625_1_, p_151625_2_, p_151625_3_, p_151625_4_);
            int[] aint1 = IntCache.getIntCache(p_151625_3_ * p_151625_4_);
            for (int i = 0; i < p_151625_4_; ++i) {
                for (int j = 0; j < p_151625_3_; ++j) {
                    this.initChunkSeed((long)(j + p_151625_1_), (long)(i + p_151625_2_));
                    int k = aint[j + i * p_151625_3_];
                    if (k != 0 && this.nextInt(13) == 0) {
                        k |= 1 + this.nextInt(15) << 8 & 3840;
                    }
                    aint1[j + i * p_151625_3_] = k;
                }
            }
            return aint1;
        }
    }

    static class GenLayerAddMushroomIsland extends GenLayer {
        public GenLayerAddMushroomIsland(long p_i2120_1_, GenLayer p_i2120_3_) { super(p_i2120_1_); this.parent = p_i2120_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 0 + (i1 + 0) * k];
                    int l1 = aint[j1 + 2 + (i1 + 0) * k];
                    int i2 = aint[j1 + 0 + (i1 + 2) * k];
                    int j2 = aint[j1 + 2 + (i1 + 2) * k];
                    int k2 = aint[j1 + 1 + (i1 + 1) * k];
                    this.initChunkSeed((long)(j1 + areaX), (long)(i1 + areaY));
                    if (k2 == 0 && k1 == 0 && l1 == 0 && i2 == 0 && j2 == 0 && this.nextInt(100) == 0) {
                        aint1[j1 + i1 * areaWidth] = MUSHROOM;   // Biome.getIdForBiome(Biomes.MUSHROOM_ISLAND)
                    } else {
                        aint1[j1 + i1 * areaWidth] = k2;
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerDeepOcean extends GenLayer {
        public GenLayerDeepOcean(long p_i45472_1_, GenLayer p_i45472_3_) { super(p_i45472_1_); this.parent = p_i45472_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 1 + (i1 + 1 - 1) * (areaWidth + 2)];
                    int l1 = aint[j1 + 1 + 1 + (i1 + 1) * (areaWidth + 2)];
                    int i2 = aint[j1 + 1 - 1 + (i1 + 1) * (areaWidth + 2)];
                    int j2 = aint[j1 + 1 + (i1 + 1 + 1) * (areaWidth + 2)];
                    int k2 = aint[j1 + 1 + (i1 + 1) * k];
                    int l2 = 0;
                    if (k1 == 0) { ++l2; }
                    if (l1 == 0) { ++l2; }
                    if (i2 == 0) { ++l2; }
                    if (j2 == 0) { ++l2; }
                    if (k2 == 0 && l2 > 3) { aint1[j1 + i1 * areaWidth] = DEEP_OCEAN; }
                    else { aint1[j1 + i1 * areaWidth] = k2; }
                }
            }
            return aint1;
        }
    }

    static class GenLayerRiverInit extends GenLayer {
        public GenLayerRiverInit(long p_i2127_1_, GenLayer p_i2127_3_) { super(p_i2127_1_); this.parent = p_i2127_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX, areaY, areaWidth, areaHeight);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    aint1[j + i * areaWidth] = aint[j + i * areaWidth] > 0 ? this.nextInt(299999) + 2 : 0;
                }
            }
            return aint1;
        }
    }

    static class GenLayerBiome extends GenLayer {
        // vanilla DEFAULT (unmodded) weighted biome lists assembled in the constructor:
        //   DESERT(type0): DESERT/30, SAVANNA/20, PLAINS/10        (BiomeManager DESERT list empty + 3 adds)
        //   WARM (type1):  FOREST/10, ROOFED_FOREST/10, EXTREME_HILLS/10, PLAINS/10, BIRCH_FOREST/10, SWAMP/10
        //   COOL (type2):  FOREST/10, EXTREME_HILLS/10, TAIGA/10, PLAINS/10
        //   ICY  (type3):  ICE_PLAINS/30, COLD_TAIGA/10
        // isTypeListModded == false (vanilla) -> weight = nextInt(total/10) * 10.
        public GenLayerBiome(long p_i45560_1_, GenLayer p_i45560_3_) { super(p_i45560_1_); this.parent = p_i45560_3_; }
        private int getWeightedBiomeEntry(int type) {
            int[] ids; int[] wts;
            if (type == 0) { ids = new int[]{DESERT, SAVANNA, PLAINS}; wts = new int[]{30, 20, 10}; }
            else if (type == 1) { ids = new int[]{FOREST, ROOFED_FOREST, EXTREME_HILLS, PLAINS, BIRCH_FOREST, SWAMP}; wts = new int[]{10,10,10,10,10,10}; }
            else if (type == 2) { ids = new int[]{FOREST, EXTREME_HILLS, TAIGA, PLAINS}; wts = new int[]{10,10,10,10}; }
            else { ids = new int[]{ICE_PLAINS, COLD_TAIGA}; wts = new int[]{30, 10}; }
            int total = 0; for (int w : wts) total += w;
            int weight = nextInt(total / 10) * 10;
            for (int z = 0; z < ids.length; ++z) { weight -= wts[z]; if (weight < 0) return ids[z]; }
            return ids[ids.length - 1];
        }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX, areaY, areaWidth, areaHeight);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    int k = aint[j + i * areaWidth];
                    int l = (k & 3840) >> 8;
                    k = k & -3841;
                    // settings == null for DEFAULT overworld -> skip fixedBiome branch
                    if (isBiomeOceanic(k)) {
                        aint1[j + i * areaWidth] = k;
                    } else if (k == MUSHROOM) {
                        aint1[j + i * areaWidth] = k;
                    } else if (k == 1) {
                        if (l > 0) {
                            if (this.nextInt(3) == 0) { aint1[j + i * areaWidth] = MESA_CLEAR_ROCK; }
                            else { aint1[j + i * areaWidth] = MESA_ROCK; }
                        } else { aint1[j + i * areaWidth] = getWeightedBiomeEntry(0); }
                    } else if (k == 2) {
                        if (l > 0) { aint1[j + i * areaWidth] = JUNGLE; }
                        else { aint1[j + i * areaWidth] = getWeightedBiomeEntry(1); }
                    } else if (k == 3) {
                        if (l > 0) { aint1[j + i * areaWidth] = REDWOOD_TAIGA; }
                        else { aint1[j + i * areaWidth] = getWeightedBiomeEntry(2); }
                    } else if (k == 4) {
                        aint1[j + i * areaWidth] = getWeightedBiomeEntry(3);
                    } else {
                        aint1[j + i * areaWidth] = MUSHROOM;
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerBiomeEdge extends GenLayer {
        public GenLayerBiomeEdge(long p_i45475_1_, GenLayer p_i45475_3_) { super(p_i45475_1_); this.parent = p_i45475_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                    if (!this.replaceBiomeEdgeIfNecessary(aint, aint1, j, i, areaWidth, k, EXTREME_HILLS, EXTREME_HILLS_EDGE) && !this.replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, MESA_ROCK, MESA) && !this.replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, MESA_CLEAR_ROCK, MESA) && !this.replaceBiomeEdge(aint, aint1, j, i, areaWidth, k, REDWOOD_TAIGA, TAIGA)) {
                        if (k == DESERT) {
                            int l1 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                            int i2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                            int j2 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                            int k2 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                            if (l1 != ICE_PLAINS && i2 != ICE_PLAINS && j2 != ICE_PLAINS && k2 != ICE_PLAINS) {
                                aint1[j + i * areaWidth] = k;
                            } else {
                                aint1[j + i * areaWidth] = EXTREME_HILLS_WITH_TREES;
                            }
                        } else if (k == SWAMP) {
                            int l = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                            int i1 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                            int j1 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                            int k1 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                            if (l != DESERT && i1 != DESERT && j1 != DESERT && k1 != DESERT && l != COLD_TAIGA && i1 != COLD_TAIGA && j1 != COLD_TAIGA && k1 != COLD_TAIGA && l != ICE_PLAINS && i1 != ICE_PLAINS && j1 != ICE_PLAINS && k1 != ICE_PLAINS) {
                                if (l != JUNGLE && k1 != JUNGLE && i1 != JUNGLE && j1 != JUNGLE) {
                                    aint1[j + i * areaWidth] = k;
                                } else {
                                    aint1[j + i * areaWidth] = JUNGLE_EDGE;
                                }
                            } else {
                                aint1[j + i * areaWidth] = PLAINS;
                            }
                        } else {
                            aint1[j + i * areaWidth] = k;
                        }
                    }
                }
            }
            return aint1;
        }
        private boolean replaceBiomeEdgeIfNecessary(int[] p_151636_1_, int[] p_151636_2_, int p_151636_3_, int p_151636_4_, int p_151636_5_, int p_151636_6_, int p_151636_7_, int p_151636_8_) {
            if (!biomesEqualOrMesaPlateau(p_151636_6_, p_151636_7_)) { return false; }
            else {
                int i = p_151636_1_[p_151636_3_ + 1 + (p_151636_4_ + 1 - 1) * (p_151636_5_ + 2)];
                int j = p_151636_1_[p_151636_3_ + 1 + 1 + (p_151636_4_ + 1) * (p_151636_5_ + 2)];
                int k = p_151636_1_[p_151636_3_ + 1 - 1 + (p_151636_4_ + 1) * (p_151636_5_ + 2)];
                int l = p_151636_1_[p_151636_3_ + 1 + (p_151636_4_ + 1 + 1) * (p_151636_5_ + 2)];
                if (this.canBiomesBeNeighbors(i, p_151636_7_) && this.canBiomesBeNeighbors(j, p_151636_7_) && this.canBiomesBeNeighbors(k, p_151636_7_) && this.canBiomesBeNeighbors(l, p_151636_7_)) {
                    p_151636_2_[p_151636_3_ + p_151636_4_ * p_151636_5_] = p_151636_6_;
                } else {
                    p_151636_2_[p_151636_3_ + p_151636_4_ * p_151636_5_] = p_151636_8_;
                }
                return true;
            }
        }
        private boolean replaceBiomeEdge(int[] p_151635_1_, int[] p_151635_2_, int p_151635_3_, int p_151635_4_, int p_151635_5_, int p_151635_6_, int p_151635_7_, int p_151635_8_) {
            if (p_151635_6_ != p_151635_7_) { return false; }
            else {
                int i = p_151635_1_[p_151635_3_ + 1 + (p_151635_4_ + 1 - 1) * (p_151635_5_ + 2)];
                int j = p_151635_1_[p_151635_3_ + 1 + 1 + (p_151635_4_ + 1) * (p_151635_5_ + 2)];
                int k = p_151635_1_[p_151635_3_ + 1 - 1 + (p_151635_4_ + 1) * (p_151635_5_ + 2)];
                int l = p_151635_1_[p_151635_3_ + 1 + (p_151635_4_ + 1 + 1) * (p_151635_5_ + 2)];
                if (biomesEqualOrMesaPlateau(i, p_151635_7_) && biomesEqualOrMesaPlateau(j, p_151635_7_) && biomesEqualOrMesaPlateau(k, p_151635_7_) && biomesEqualOrMesaPlateau(l, p_151635_7_)) {
                    p_151635_2_[p_151635_3_ + p_151635_4_ * p_151635_5_] = p_151635_6_;
                } else {
                    p_151635_2_[p_151635_3_ + p_151635_4_ * p_151635_5_] = p_151635_8_;
                }
                return true;
            }
        }
        private boolean canBiomesBeNeighbors(int p_151634_1_, int p_151634_2_) {
            if (biomesEqualOrMesaPlateau(p_151634_1_, p_151634_2_)) { return true; }
            else {
                if (BM.valid(p_151634_1_) && BM.valid(p_151634_2_)) {
                    int t0 = BM.temp(p_151634_1_);
                    int t1 = BM.temp(p_151634_2_);
                    return t0 == t1 || t0 == TC_MEDIUM || t1 == TC_MEDIUM;
                } else {
                    return false;
                }
            }
        }
    }

    static class GenLayerHills extends GenLayer {
        private final GenLayer riverLayer;
        public GenLayerHills(long p_i45479_1_, GenLayer p_i45479_3_, GenLayer p_i45479_4_) {
            super(p_i45479_1_); this.parent = p_i45479_3_; this.riverLayer = p_i45479_4_;
        }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
            int[] aint1 = this.riverLayer.getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
            int[] aint2 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                    int l = aint1[j + 1 + (i + 1) * (areaWidth + 2)];
                    boolean flag = (l - 2) % 29 == 0;
                    boolean biomeValid = BM.valid(k);
                    boolean flag1 = biomeValid && BM.isMutation(k);
                    if (k != 0 && l >= 2 && (l - 2) % 29 == 1 && !flag1) {
                        int biome3 = BM.mutationFor(k);   // Biome.getMutationForBiome(biome)
                        aint2[j + i * areaWidth] = biome3 < 0 ? k : biome3;
                    } else if (this.nextInt(3) != 0 && !flag) {
                        aint2[j + i * areaWidth] = k;
                    } else {
                        int biome1 = k;
                        if (k == DESERT) { biome1 = DESERT_HILLS; }
                        else if (k == FOREST) { biome1 = FOREST_HILLS; }
                        else if (k == BIRCH_FOREST) { biome1 = BIRCH_FOREST_HILLS; }
                        else if (k == ROOFED_FOREST) { biome1 = PLAINS; }
                        else if (k == TAIGA) { biome1 = TAIGA_HILLS; }
                        else if (k == REDWOOD_TAIGA) { biome1 = REDWOOD_TAIGA_HILLS; }
                        else if (k == COLD_TAIGA) { biome1 = COLD_TAIGA_HILLS; }
                        else if (k == PLAINS) {
                            if (this.nextInt(3) == 0) { biome1 = FOREST_HILLS; }
                            else { biome1 = FOREST; }
                        }
                        else if (k == ICE_PLAINS) { biome1 = ICE_MOUNTAINS; }
                        else if (k == JUNGLE) { biome1 = JUNGLE_HILLS; }
                        else if (k == OCEAN) { biome1 = DEEP_OCEAN; }
                        else if (k == EXTREME_HILLS) { biome1 = EXTREME_HILLS_WITH_TREES; }
                        else if (k == SAVANNA) { biome1 = SAVANNA_PLATEAU; }
                        else if (biomesEqualOrMesaPlateau(k, MESA_ROCK)) { biome1 = MESA; }
                        else if (k == DEEP_OCEAN && this.nextInt(3) == 0) {
                            int i1 = this.nextInt(2);
                            if (i1 == 0) { biome1 = PLAINS; } else { biome1 = FOREST; }
                        }
                        int j2 = biome1;   // Biome.getIdForBiome(biome1)
                        if (flag && j2 != k) {
                            int biome2 = BM.mutationFor(biome1);
                            j2 = biome2 < 0 ? k : biome2;
                        }
                        if (j2 == k) {
                            aint2[j + i * areaWidth] = k;
                        } else {
                            int k2 = aint[j + 1 + (i + 0) * (areaWidth + 2)];
                            int j1 = aint[j + 2 + (i + 1) * (areaWidth + 2)];
                            int k1 = aint[j + 0 + (i + 1) * (areaWidth + 2)];
                            int l1 = aint[j + 1 + (i + 2) * (areaWidth + 2)];
                            int i2 = 0;
                            if (biomesEqualOrMesaPlateau(k2, k)) { ++i2; }
                            if (biomesEqualOrMesaPlateau(j1, k)) { ++i2; }
                            if (biomesEqualOrMesaPlateau(k1, k)) { ++i2; }
                            if (biomesEqualOrMesaPlateau(l1, k)) { ++i2; }
                            if (i2 >= 3) { aint2[j + i * areaWidth] = j2; }
                            else { aint2[j + i * areaWidth] = k; }
                        }
                    }
                }
            }
            return aint2;
        }
    }

    static class GenLayerRiver extends GenLayer {
        public GenLayerRiver(long p_i2128_1_, GenLayer p_i2128_3_) { super(p_i2128_1_); super.parent = p_i2128_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = this.riverFilter(aint[j1 + 0 + (i1 + 1) * k]);
                    int l1 = this.riverFilter(aint[j1 + 2 + (i1 + 1) * k]);
                    int i2 = this.riverFilter(aint[j1 + 1 + (i1 + 0) * k]);
                    int j2 = this.riverFilter(aint[j1 + 1 + (i1 + 2) * k]);
                    int k2 = this.riverFilter(aint[j1 + 1 + (i1 + 1) * k]);
                    if (k2 == k1 && k2 == i2 && k2 == l1 && k2 == j2) {
                        aint1[j1 + i1 * areaWidth] = -1;
                    } else {
                        aint1[j1 + i1 * areaWidth] = RIVER;
                    }
                }
            }
            return aint1;
        }
        private int riverFilter(int p_151630_1_) {
            return p_151630_1_ >= 2 ? 2 + (p_151630_1_ & 1) : p_151630_1_;
        }
    }

    static class GenLayerSmooth extends GenLayer {
        public GenLayerSmooth(long p_i2131_1_, GenLayer p_i2131_3_) { super(p_i2131_1_); super.parent = p_i2131_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int i = areaX - 1;
            int j = areaY - 1;
            int k = areaWidth + 2;
            int l = areaHeight + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i1 = 0; i1 < areaHeight; ++i1) {
                for (int j1 = 0; j1 < areaWidth; ++j1) {
                    int k1 = aint[j1 + 0 + (i1 + 1) * k];
                    int l1 = aint[j1 + 2 + (i1 + 1) * k];
                    int i2 = aint[j1 + 1 + (i1 + 0) * k];
                    int j2 = aint[j1 + 1 + (i1 + 2) * k];
                    int k2 = aint[j1 + 1 + (i1 + 1) * k];
                    if (k1 == l1 && i2 == j2) {
                        this.initChunkSeed((long)(j1 + areaX), (long)(i1 + areaY));
                        if (this.nextInt(2) == 0) { k2 = k1; }
                        else { k2 = i2; }
                    } else {
                        if (k1 == l1) { k2 = k1; }
                        if (i2 == j2) { k2 = i2; }
                    }
                    aint1[j1 + i1 * areaWidth] = k2;
                }
            }
            return aint1;
        }
    }

    static class GenLayerRareBiome extends GenLayer {
        public GenLayerRareBiome(long p_i45478_1_, GenLayer p_i45478_3_) { super(p_i45478_1_); this.parent = p_i45478_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                    if (this.nextInt(57) == 0) {
                        if (k == PLAINS) {
                            aint1[j + i * areaWidth] = MUTATED_PLAINS;
                        } else {
                            aint1[j + i * areaWidth] = k;
                        }
                    } else {
                        aint1[j + i * areaWidth] = k;
                    }
                }
            }
            return aint1;
        }
    }

    static class GenLayerShore extends GenLayer {
        public GenLayerShore(long p_i2130_1_, GenLayer p_i2130_3_) { super(p_i2130_1_); this.parent = p_i2130_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.parent.getInts(areaX - 1, areaY - 1, areaWidth + 2, areaHeight + 2);
            int[] aint1 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaHeight; ++i) {
                for (int j = 0; j < areaWidth; ++j) {
                    this.initChunkSeed((long)(j + areaX), (long)(i + areaY));
                    int k = aint[j + 1 + (i + 1) * (areaWidth + 2)];
                    boolean isJungle = BM.valid(k) && BM.cls(k) == CLS_JUNGLE;   // biome.getBiomeClass() == BiomeJungle.class
                    if (k == MUSHROOM) {
                        int j2 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int i3 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int l3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int k4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                        if (j2 != OCEAN && i3 != OCEAN && l3 != OCEAN && k4 != OCEAN) {
                            aint1[j + i * areaWidth] = k;
                        } else {
                            aint1[j + i * areaWidth] = MUSHROOM_SHORE;
                        }
                    } else if (isJungle) {
                        int i2 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                        int l2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                        int k3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                        int j4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                        if (this.isJungleCompatible(i2) && this.isJungleCompatible(l2) && this.isJungleCompatible(k3) && this.isJungleCompatible(j4)) {
                            if (!isBiomeOceanic(i2) && !isBiomeOceanic(l2) && !isBiomeOceanic(k3) && !isBiomeOceanic(j4)) {
                                aint1[j + i * areaWidth] = k;
                            } else {
                                aint1[j + i * areaWidth] = BEACH;
                            }
                        } else {
                            aint1[j + i * areaWidth] = JUNGLE_EDGE;
                        }
                    } else if (k != EXTREME_HILLS && k != EXTREME_HILLS_WITH_TREES && k != EXTREME_HILLS_EDGE) {
                        boolean snowy = BM.valid(k) && BM.snowy(k);   // biome.isSnowyBiome()
                        if (snowy) {
                            this.replaceIfNeighborOcean(aint, aint1, j, i, areaWidth, k, COLD_BEACH);
                        } else if (k != MESA && k != MESA_ROCK) {
                            if (k != OCEAN && k != DEEP_OCEAN && k != RIVER && k != SWAMP) {
                                int l1 = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                                int k2 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                                int j3 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                                int i4 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                                if (!isBiomeOceanic(l1) && !isBiomeOceanic(k2) && !isBiomeOceanic(j3) && !isBiomeOceanic(i4)) {
                                    aint1[j + i * areaWidth] = k;
                                } else {
                                    aint1[j + i * areaWidth] = BEACH;
                                }
                            } else {
                                aint1[j + i * areaWidth] = k;
                            }
                        } else {
                            int l = aint[j + 1 + (i + 1 - 1) * (areaWidth + 2)];
                            int i1 = aint[j + 1 + 1 + (i + 1) * (areaWidth + 2)];
                            int j1 = aint[j + 1 - 1 + (i + 1) * (areaWidth + 2)];
                            int k1 = aint[j + 1 + (i + 1 + 1) * (areaWidth + 2)];
                            if (!isBiomeOceanic(l) && !isBiomeOceanic(i1) && !isBiomeOceanic(j1) && !isBiomeOceanic(k1)) {
                                if (this.isMesa(l) && this.isMesa(i1) && this.isMesa(j1) && this.isMesa(k1)) {
                                    aint1[j + i * areaWidth] = k;
                                } else {
                                    aint1[j + i * areaWidth] = DESERT;
                                }
                            } else {
                                aint1[j + i * areaWidth] = k;
                            }
                        }
                    } else {
                        this.replaceIfNeighborOcean(aint, aint1, j, i, areaWidth, k, STONE_BEACH);
                    }
                }
            }
            return aint1;
        }
        private void replaceIfNeighborOcean(int[] p_151632_1_, int[] p_151632_2_, int p_151632_3_, int p_151632_4_, int p_151632_5_, int p_151632_6_, int p_151632_7_) {
            if (isBiomeOceanic(p_151632_6_)) {
                p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_6_;
            } else {
                int i = p_151632_1_[p_151632_3_ + 1 + (p_151632_4_ + 1 - 1) * (p_151632_5_ + 2)];
                int j = p_151632_1_[p_151632_3_ + 1 + 1 + (p_151632_4_ + 1) * (p_151632_5_ + 2)];
                int k = p_151632_1_[p_151632_3_ + 1 - 1 + (p_151632_4_ + 1) * (p_151632_5_ + 2)];
                int l = p_151632_1_[p_151632_3_ + 1 + (p_151632_4_ + 1 + 1) * (p_151632_5_ + 2)];
                if (!isBiomeOceanic(i) && !isBiomeOceanic(j) && !isBiomeOceanic(k) && !isBiomeOceanic(l)) {
                    p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_6_;
                } else {
                    p_151632_2_[p_151632_3_ + p_151632_4_ * p_151632_5_] = p_151632_7_;
                }
            }
        }
        private boolean isJungleCompatible(int p_151631_1_) {
            return BM.valid(p_151631_1_) && BM.cls(p_151631_1_) == CLS_JUNGLE ? true : p_151631_1_ == JUNGLE_EDGE || p_151631_1_ == JUNGLE || p_151631_1_ == JUNGLE_HILLS || p_151631_1_ == FOREST || p_151631_1_ == TAIGA || isBiomeOceanic(p_151631_1_);
        }
        private boolean isMesa(int p_151633_1_) {
            return BM.isMesa(p_151633_1_);
        }
    }

    static class GenLayerRiverMix extends GenLayer {
        private final GenLayer biomePatternGeneratorChain;
        private final GenLayer riverPatternGeneratorChain;
        public GenLayerRiverMix(long p_i2129_1_, GenLayer p_i2129_3_, GenLayer p_i2129_4_) {
            super(p_i2129_1_); this.biomePatternGeneratorChain = p_i2129_3_; this.riverPatternGeneratorChain = p_i2129_4_;
        }
        public void initWorldGenSeed(long seed) {
            this.biomePatternGeneratorChain.initWorldGenSeed(seed);
            this.riverPatternGeneratorChain.initWorldGenSeed(seed);
            super.initWorldGenSeed(seed);
        }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            int[] aint = this.biomePatternGeneratorChain.getInts(areaX, areaY, areaWidth, areaHeight);
            int[] aint1 = this.riverPatternGeneratorChain.getInts(areaX, areaY, areaWidth, areaHeight);
            int[] aint2 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int i = 0; i < areaWidth * areaHeight; ++i) {
                if (aint[i] != OCEAN && aint[i] != DEEP_OCEAN) {
                    if (aint1[i] == RIVER) {
                        if (aint[i] == ICE_PLAINS) {
                            aint2[i] = FROZEN_RIVER;
                        } else if (aint[i] != MUSHROOM && aint[i] != MUSHROOM_SHORE) {
                            aint2[i] = aint1[i] & 255;
                        } else {
                            aint2[i] = MUSHROOM_SHORE;
                        }
                    } else {
                        aint2[i] = aint[i];
                    }
                } else {
                    aint2[i] = aint[i];
                }
            }
            return aint2;
        }
    }

    static class GenLayerVoronoiZoom extends GenLayer {
        public GenLayerVoronoiZoom(long p_i2133_1_, GenLayer p_i2133_3_) { super(p_i2133_1_); super.parent = p_i2133_3_; }
        public int[] getInts(int areaX, int areaY, int areaWidth, int areaHeight) {
            areaX = areaX - 2;
            areaY = areaY - 2;
            int i = areaX >> 2;
            int j = areaY >> 2;
            int k = (areaWidth >> 2) + 2;
            int l = (areaHeight >> 2) + 2;
            int[] aint = this.parent.getInts(i, j, k, l);
            int i1 = k - 1 << 2;
            int j1 = l - 1 << 2;
            int[] aint1 = IntCache.getIntCache(i1 * j1);
            for (int k1 = 0; k1 < l - 1; ++k1) {
                int l1 = 0;
                int i2 = aint[l1 + 0 + (k1 + 0) * k];
                for (int j2 = aint[l1 + 0 + (k1 + 1) * k]; l1 < k - 1; ++l1) {
                    double d0 = 3.6D;
                    this.initChunkSeed((long)(l1 + i << 2), (long)(k1 + j << 2));
                    double d1 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D;
                    double d2 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D;
                    this.initChunkSeed((long)(l1 + i + 1 << 2), (long)(k1 + j << 2));
                    double d3 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D + 4.0D;
                    double d4 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D;
                    this.initChunkSeed((long)(l1 + i << 2), (long)(k1 + j + 1 << 2));
                    double d5 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D;
                    double d6 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D + 4.0D;
                    this.initChunkSeed((long)(l1 + i + 1 << 2), (long)(k1 + j + 1 << 2));
                    double d7 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D + 4.0D;
                    double d8 = ((double)this.nextInt(1024) / 1024.0D - 0.5D) * 3.6D + 4.0D;
                    int k2 = aint[l1 + 1 + (k1 + 0) * k] & 255;
                    int l2 = aint[l1 + 1 + (k1 + 1) * k] & 255;
                    for (int i3 = 0; i3 < 4; ++i3) {
                        int j3 = ((k1 << 2) + i3) * i1 + (l1 << 2);
                        for (int k3 = 0; k3 < 4; ++k3) {
                            double d9 = ((double)i3 - d2) * ((double)i3 - d2) + ((double)k3 - d1) * ((double)k3 - d1);
                            double d10 = ((double)i3 - d4) * ((double)i3 - d4) + ((double)k3 - d3) * ((double)k3 - d3);
                            double d11 = ((double)i3 - d6) * ((double)i3 - d6) + ((double)k3 - d5) * ((double)k3 - d5);
                            double d12 = ((double)i3 - d8) * ((double)i3 - d8) + ((double)k3 - d7) * ((double)k3 - d7);
                            if (d9 < d10 && d9 < d11 && d9 < d12) { aint1[j3++] = i2; }
                            else if (d10 < d9 && d10 < d11 && d10 < d12) { aint1[j3++] = k2; }
                            else if (d11 < d9 && d11 < d10 && d11 < d12) { aint1[j3++] = j2; }
                            else { aint1[j3++] = l2; }
                        }
                    }
                    i2 = k2;
                    j2 = l2;
                }
            }
            int[] aint2 = IntCache.getIntCache(areaWidth * areaHeight);
            for (int l3 = 0; l3 < areaHeight; ++l3) {
                System.arraycopy(aint1, (l3 + (areaY & 3)) * i1 + (areaX & 3), aint2, l3 * areaWidth, areaWidth);
            }
            return aint2;
        }
    }

    // verbatim GenLayer.initializeAllBiomeGenerators + WorldType.DEFAULT.getBiomeLayer,
    // specialized to WorldType.DEFAULT with ChunkProviderSettings == null (vanilla new world):
    // biomeSize = riverSize = 4, not LARGE_BIOMES, getModdedBiomeSize is identity (no mods).
    static GenLayer[] initializeAllBiomeGenerators(long seed) {
        GenLayer genlayer = new GenLayerIsland(1L);
        genlayer = new GenLayerFuzzyZoom(2000L, genlayer);
        GenLayerAddIsland genlayeraddisland = new GenLayerAddIsland(1L, genlayer);
        GenLayerZoom genlayerzoom = new GenLayerZoom(2001L, genlayeraddisland);
        GenLayerAddIsland genlayeraddisland1 = new GenLayerAddIsland(2L, genlayerzoom);
        genlayeraddisland1 = new GenLayerAddIsland(50L, genlayeraddisland1);
        genlayeraddisland1 = new GenLayerAddIsland(70L, genlayeraddisland1);
        GenLayerRemoveTooMuchOcean genlayerremovetoomuchocean = new GenLayerRemoveTooMuchOcean(2L, genlayeraddisland1);
        GenLayerAddSnow genlayeraddsnow = new GenLayerAddSnow(2L, genlayerremovetoomuchocean);
        GenLayerAddIsland genlayeraddisland2 = new GenLayerAddIsland(3L, genlayeraddsnow);
        GenLayerEdge genlayeredge = new GenLayerEdge(2L, genlayeraddisland2, GenLayerEdge.Mode.COOL_WARM);
        genlayeredge = new GenLayerEdge(2L, genlayeredge, GenLayerEdge.Mode.HEAT_ICE);
        genlayeredge = new GenLayerEdge(3L, genlayeredge, GenLayerEdge.Mode.SPECIAL);
        GenLayerZoom genlayerzoom1 = new GenLayerZoom(2002L, genlayeredge);
        genlayerzoom1 = new GenLayerZoom(2003L, genlayerzoom1);
        GenLayerAddIsland genlayeraddisland3 = new GenLayerAddIsland(4L, genlayerzoom1);
        GenLayerAddMushroomIsland genlayeraddmushroomisland = new GenLayerAddMushroomIsland(5L, genlayeraddisland3);
        GenLayerDeepOcean genlayerdeepocean = new GenLayerDeepOcean(4L, genlayeraddmushroomisland);
        GenLayer genlayer4 = GenLayerZoom.magnify(1000L, genlayerdeepocean, 0);
        int i = 4;   // biomeSize (settings null, not LARGE_BIOMES)
        int j = 4;   // riverSize

        GenLayer lvt_7_1_ = GenLayerZoom.magnify(1000L, genlayer4, 0);
        GenLayerRiverInit genlayerriverinit = new GenLayerRiverInit(100L, lvt_7_1_);
        GenLayer lvt_9_1_ = GenLayerZoom.magnify(1000L, genlayerriverinit, 2);
        GenLayer genlayerbiomeedge = getBiomeLayer(seed, genlayer4);   // WorldType.DEFAULT.getBiomeLayer
        GenLayer genlayerhills = new GenLayerHills(1000L, genlayerbiomeedge, lvt_9_1_);
        GenLayer genlayer5 = GenLayerZoom.magnify(1000L, genlayerriverinit, 2);
        genlayer5 = GenLayerZoom.magnify(1000L, genlayer5, j);
        GenLayerRiver genlayerriver = new GenLayerRiver(1L, genlayer5);
        GenLayerSmooth genlayersmooth = new GenLayerSmooth(1000L, genlayerriver);
        genlayerhills = new GenLayerRareBiome(1001L, genlayerhills);

        for (int k = 0; k < i; ++k) {
            genlayerhills = new GenLayerZoom((long)(1000 + k), genlayerhills);
            if (k == 0) { genlayerhills = new GenLayerAddIsland(3L, genlayerhills); }
            if (k == 1 || i == 1) { genlayerhills = new GenLayerShore(1000L, genlayerhills); }
        }

        GenLayerSmooth genlayersmooth1 = new GenLayerSmooth(1000L, genlayerhills);
        GenLayerRiverMix genlayerrivermix = new GenLayerRiverMix(100L, genlayersmooth1, genlayersmooth);
        GenLayer genlayer3 = new GenLayerVoronoiZoom(10L, genlayerrivermix);
        genlayerrivermix.initWorldGenSeed(seed);
        genlayer3.initWorldGenSeed(seed);
        return new GenLayer[] {genlayerrivermix, genlayer3, genlayerrivermix};
    }

    static GenLayer getBiomeLayer(long worldSeed, GenLayer parentLayer) {
        GenLayer ret = new GenLayerBiome(200L, parentLayer);
        ret = GenLayerZoom.magnify(1000L, ret, 2);
        ret = new GenLayerBiomeEdge(1000L, ret);
        return ret;
    }


    // ===================================================================================
    // ===== INTEGRATION ADDITIONS (verbatim ChunkProviderOverworld / Biome / MapGen*) =====
    // The GenLayer stack above is the verbatim genlayer_biomes golden. Everything below is the
    // verbatim decompiled MC for the rest of provideChunk-minus-structures, with the SAME sanctioned
    // shims (block-state ids -> small ints; the Biome OBJECT graph -> the exact-literal integer table
    // BP; ChunkPrimer char[65536]; per-biome mutable topBlock/fillerBlock -> curTop[]/curFiller[]).

    // ----- unified block-state id substitution (identical to core/chunk_provider.h CB_*) -----
    static final int AIR=0, STONE=1, WATER=2, GRASS=3, DIRT=4, BEDROCK=5, GRAVEL=6, SAND=7,
        SANDSTONE=8, RED_SANDSTONE=9, ICE=10, LAVA=11, FLOWING_LAVA=12, FLOWING_WATER=13,
        WATER_LILY=14, MYCELIUM=15, SNOW_LAYER=16, HARDENED_CLAY=17, STAINED_HARDENED_CLAY=18,
        PODZOL=19, COARSE_DIRT=20;
    static final int SEA_LEVEL = 63;
    static final int SURF_BASE=0, SURF_HILLS=1, SURF_TAIGA=2, SURF_SWAMP=3;
    static final int HILLS_NORMAL=0, HILLS_EXTRA_TREES=1, HILLS_MUTATED=2;
    static final int TAIGA_NORMAL=0, TAIGA_MEGA=1, TAIGA_MEGA_SPRUCE=2;

    // ----- REAL biome property table (sanctioned shim) = exact Biome.registerBiomes literals +
    // the Biome subclass constructor top/filler + which subclass overrides genTerrainBlocks. -----
    static class BP {
        static float baseHeight(int id) {
            switch (id) {
                case 0: return -1.0F;        case 1: return 0.125F;       case 2: return 0.125F;
                case 3: return 1.0F;         case 4: return 0.1F;         case 5: return 0.2F;
                case 6: return -0.2F;        case 7: return -0.5F;        case 8: return 0.1F;
                case 9: return 0.1F;         case 10: return -1.0F;       case 11: return -0.5F;
                case 12: return 0.125F;      case 13: return 0.45F;       case 14: return 0.2F;
                case 15: return 0.0F;        case 16: return 0.0F;        case 17: return 0.45F;
                case 18: return 0.45F;       case 19: return 0.45F;       case 20: return 0.8F;
                case 21: return 0.1F;        case 22: return 0.45F;       case 23: return 0.1F;
                case 24: return -1.8F;       case 25: return 0.1F;        case 26: return 0.0F;
                case 27: return 0.1F;        case 28: return 0.45F;       case 29: return 0.1F;
                case 30: return 0.2F;        case 31: return 0.45F;       case 32: return 0.2F;
                case 33: return 0.45F;       case 34: return 1.0F;        case 35: return 0.125F;
                case 36: return 1.5F;        case 37: return 0.1F;        case 38: return 1.5F;
                case 39: return 1.5F;        case 127: return 0.1F;       case 129: return 0.125F;
                case 130: return 0.225F;     case 131: return 1.0F;       case 132: return 0.1F;
                case 133: return 0.3F;       case 134: return -0.1F;      case 140: return 0.425F;
                case 149: return 0.2F;       case 151: return 0.2F;       case 155: return 0.2F;
                case 156: return 0.55F;      case 157: return 0.2F;       case 158: return 0.3F;
                case 160: return 0.2F;       case 161: return 0.2F;       case 162: return 1.0F;
                case 163: return 0.3625F;    case 164: return 1.05F;      case 165: return 0.1F;
                case 166: return 0.45F;      case 167: return 0.45F;
                default: return 0.1F;
            }
        }
        static float heightVariation(int id) {
            switch (id) {
                case 0: return 0.1F;         case 1: return 0.05F;        case 2: return 0.05F;
                case 3: return 0.5F;         case 4: return 0.2F;         case 5: return 0.2F;
                case 6: return 0.1F;         case 7: return 0.0F;         case 8: return 0.2F;
                case 9: return 0.2F;         case 10: return 0.1F;        case 11: return 0.0F;
                case 12: return 0.05F;       case 13: return 0.3F;        case 14: return 0.3F;
                case 15: return 0.025F;      case 16: return 0.025F;      case 17: return 0.3F;
                case 18: return 0.3F;        case 19: return 0.3F;        case 20: return 0.3F;
                case 21: return 0.2F;        case 22: return 0.3F;        case 23: return 0.2F;
                case 24: return 0.1F;        case 25: return 0.8F;        case 26: return 0.025F;
                case 27: return 0.2F;        case 28: return 0.3F;        case 29: return 0.2F;
                case 30: return 0.2F;        case 31: return 0.3F;        case 32: return 0.2F;
                case 33: return 0.3F;        case 34: return 0.5F;        case 35: return 0.05F;
                case 36: return 0.025F;      case 37: return 0.2F;        case 38: return 0.025F;
                case 39: return 0.025F;      case 127: return 0.2F;       case 129: return 0.05F;
                case 130: return 0.25F;      case 131: return 0.5F;       case 132: return 0.4F;
                case 133: return 0.4F;       case 134: return 0.3F;       case 140: return 0.45000002F;
                case 149: return 0.4F;       case 151: return 0.4F;       case 155: return 0.4F;
                case 156: return 0.5F;       case 157: return 0.4F;       case 158: return 0.4F;
                case 160: return 0.2F;       case 161: return 0.2F;       case 162: return 0.5F;
                case 163: return 1.225F;     case 164: return 1.2125001F; case 165: return 0.2F;
                case 166: return 0.3F;       case 167: return 0.3F;
                default: return 0.2F;
            }
        }
        static float temperature(int id) {
            switch (id) {
                case 0: return 0.5F;         case 1: return 0.8F;         case 2: return 2.0F;
                case 3: return 0.2F;         case 4: return 0.7F;         case 5: return 0.25F;
                case 6: return 0.8F;         case 7: return 0.5F;         case 8: return 2.0F;
                case 9: return 0.5F;         case 10: return 0.0F;        case 11: return 0.0F;
                case 12: return 0.0F;        case 13: return 0.0F;        case 14: return 0.9F;
                case 15: return 0.9F;        case 16: return 0.8F;        case 17: return 2.0F;
                case 18: return 0.7F;        case 19: return 0.25F;       case 20: return 0.2F;
                case 21: return 0.95F;       case 22: return 0.95F;       case 23: return 0.95F;
                case 24: return 0.5F;        case 25: return 0.2F;        case 26: return 0.05F;
                case 27: return 0.6F;        case 28: return 0.6F;        case 29: return 0.7F;
                case 30: return -0.5F;       case 31: return -0.5F;       case 32: return 0.3F;
                case 33: return 0.3F;        case 34: return 0.2F;        case 35: return 1.2F;
                case 36: return 1.0F;        case 37: return 2.0F;        case 38: return 2.0F;
                case 39: return 2.0F;        case 127: return 0.5F;       case 129: return 0.8F;
                case 130: return 2.0F;       case 131: return 0.2F;       case 132: return 0.7F;
                case 133: return 0.25F;      case 134: return 0.8F;       case 140: return 0.0F;
                case 149: return 0.95F;      case 151: return 0.95F;      case 155: return 0.6F;
                case 156: return 0.6F;       case 157: return 0.7F;       case 158: return -0.5F;
                case 160: return 0.25F;      case 161: return 0.25F;      case 162: return 0.2F;
                case 163: return 1.1F;       case 164: return 1.0F;       case 165: return 2.0F;
                case 166: return 2.0F;       case 167: return 2.0F;
                default: return 0.5F;
            }
        }
        static int defTop(int id) {
            switch (id) {
                case 2: case 17: case 130: return SAND;
                case 16: case 26: return SAND;
                case 25: return STONE;
                case 14: case 15: return MYCELIUM;
                default: return GRASS;
            }
        }
        static int defFiller(int id) {
            switch (id) {
                case 2: case 17: case 130: return SAND;
                case 16: case 26: return SAND;
                case 25: return STONE;
                default: return DIRT;
            }
        }
        static int surfType(int id) {
            switch (id) {
                case 3: case 20: case 34: case 131: case 162: return SURF_HILLS;
                case 5: case 19: case 30: case 31: case 32: case 33:
                case 133: case 158: case 160: case 161: return SURF_TAIGA;
                case 6: case 134: return SURF_SWAMP;
                default: return SURF_BASE;
            }
        }
        static int hillsType(int id) {
            switch (id) {
                case 20: case 34: return HILLS_EXTRA_TREES;
                case 131: case 162: return HILLS_MUTATED;
                default: return HILLS_NORMAL;
            }
        }
        static int taigaType(int id) {
            switch (id) {
                case 32: case 33: return TAIGA_MEGA;
                case 160: case 161: return TAIGA_MEGA_SPRUCE;
                default: return TAIGA_NORMAL;
            }
        }
        static boolean caveException(int id) { return id == 16 || id == 2; }
        static boolean ravineException(int id) { return id == 16 || id == 2 || id == 14 || id == 15; }
    }

    // ----- verbatim NoiseGeneratorImproved + NoiseGeneratorOctaves (net/minecraft/world/gen) -----
    static class NoiseGeneratorImproved {
        private final int[] permutations;
        public double xCoord, yCoord, zCoord;
        private static final double[] GRAD_X = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
        private static final double[] GRAD_Y = {1,1,-1,-1,0,0,0,0,1,-1,1,-1,1,-1,1,-1};
        private static final double[] GRAD_Z = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
        private static final double[] GRAD_2X = {1,-1,1,-1,1,-1,1,-1,0,0,0,0,1,0,-1,0};
        private static final double[] GRAD_2Z = {0,0,0,0,1,1,-1,-1,1,1,-1,-1,0,1,0,-1};
        public NoiseGeneratorImproved(Random p) {
            this.permutations = new int[512];
            this.xCoord = p.nextDouble() * 256.0D;
            this.yCoord = p.nextDouble() * 256.0D;
            this.zCoord = p.nextDouble() * 256.0D;
            for (int i = 0; i < 256; this.permutations[i] = i++) { ; }
            for (int l = 0; l < 256; ++l) {
                int j = p.nextInt(256 - l) + l;
                int k = this.permutations[l];
                this.permutations[l] = this.permutations[j];
                this.permutations[j] = k;
                this.permutations[l + 256] = this.permutations[l];
            }
        }
        public final double lerp(double a, double b, double c) { return b + a * (c - b); }
        public final double grad2(int h, double x, double z) { int i = h & 15; return GRAD_2X[i] * x + GRAD_2Z[i] * z; }
        public final double grad(int h, double x, double y, double z) { int i = h & 15; return GRAD_X[i] * x + GRAD_Y[i] * y + GRAD_Z[i] * z; }
        public void populateNoiseArray(double[] noiseArray, double xOffset, double yOffset, double zOffset, int xSize, int ySize, int zSize, double xScale, double yScale, double zScale, double noiseScale) {
            if (ySize == 1) {
                int l5 = 0; double d16 = 1.0D / noiseScale;
                for (int j2 = 0; j2 < xSize; ++j2) {
                    double d17 = xOffset + (double)j2 * xScale + this.xCoord;
                    int i6 = (int)d17; if (d17 < (double)i6) --i6;
                    int k2 = i6 & 255; d17 = d17 - (double)i6;
                    double d18 = d17 * d17 * d17 * (d17 * (d17 * 6.0D - 15.0D) + 10.0D);
                    for (int j6 = 0; j6 < zSize; ++j6) {
                        double d19 = zOffset + (double)j6 * zScale + this.zCoord;
                        int k6 = (int)d19; if (d19 < (double)k6) --k6;
                        int l6 = k6 & 255; d19 = d19 - (double)k6;
                        double d20 = d19 * d19 * d19 * (d19 * (d19 * 6.0D - 15.0D) + 10.0D);
                        int i5 = this.permutations[k2] + 0;
                        int j5 = this.permutations[i5] + l6;
                        int j  = this.permutations[k2 + 1] + 0;
                        int k5 = this.permutations[j] + l6;
                        double d14 = this.lerp(d18, this.grad2(this.permutations[j5], d17, d19), this.grad(this.permutations[k5], d17 - 1.0D, 0.0D, d19));
                        double d15 = this.lerp(d18, this.grad(this.permutations[j5 + 1], d17, 0.0D, d19 - 1.0D), this.grad(this.permutations[k5 + 1], d17 - 1.0D, 0.0D, d19 - 1.0D));
                        double d21 = this.lerp(d20, d14, d15);
                        noiseArray[l5++] += d21 * d16;
                    }
                }
            } else {
                int i = 0; double d0 = 1.0D / noiseScale; int k = -1;
                int l = 0, i1 = 0, j1 = 0, k1 = 0, l1 = 0, i2 = 0;
                double d1 = 0, d2 = 0, d3 = 0, d4 = 0;
                for (int l2 = 0; l2 < xSize; ++l2) {
                    double d5 = xOffset + (double)l2 * xScale + this.xCoord;
                    int i3 = (int)d5; if (d5 < (double)i3) --i3;
                    int j3 = i3 & 255; d5 = d5 - (double)i3;
                    double d6 = d5 * d5 * d5 * (d5 * (d5 * 6.0D - 15.0D) + 10.0D);
                    for (int k3 = 0; k3 < zSize; ++k3) {
                        double d7 = zOffset + (double)k3 * zScale + this.zCoord;
                        int l3 = (int)d7; if (d7 < (double)l3) --l3;
                        int i4 = l3 & 255; d7 = d7 - (double)l3;
                        double d8 = d7 * d7 * d7 * (d7 * (d7 * 6.0D - 15.0D) + 10.0D);
                        for (int j4 = 0; j4 < ySize; ++j4) {
                            double d9 = yOffset + (double)j4 * yScale + this.yCoord;
                            int k4 = (int)d9; if (d9 < (double)k4) --k4;
                            int l4 = k4 & 255; d9 = d9 - (double)k4;
                            double d10 = d9 * d9 * d9 * (d9 * (d9 * 6.0D - 15.0D) + 10.0D);
                            if (j4 == 0 || l4 != k) {
                                k = l4;
                                l = this.permutations[j3] + l4;
                                i1 = this.permutations[l] + i4;
                                j1 = this.permutations[l + 1] + i4;
                                k1 = this.permutations[j3 + 1] + l4;
                                l1 = this.permutations[k1] + i4;
                                i2 = this.permutations[k1 + 1] + i4;
                                d1 = this.lerp(d6, this.grad(this.permutations[i1], d5, d9, d7), this.grad(this.permutations[l1], d5 - 1.0D, d9, d7));
                                d2 = this.lerp(d6, this.grad(this.permutations[j1], d5, d9 - 1.0D, d7), this.grad(this.permutations[i2], d5 - 1.0D, d9 - 1.0D, d7));
                                d3 = this.lerp(d6, this.grad(this.permutations[i1 + 1], d5, d9, d7 - 1.0D), this.grad(this.permutations[l1 + 1], d5 - 1.0D, d9, d7 - 1.0D));
                                d4 = this.lerp(d6, this.grad(this.permutations[j1 + 1], d5, d9 - 1.0D, d7 - 1.0D), this.grad(this.permutations[i2 + 1], d5 - 1.0D, d9 - 1.0D, d7 - 1.0D));
                            }
                            double d11 = this.lerp(d10, d1, d2);
                            double d12 = this.lerp(d10, d3, d4);
                            double d13 = this.lerp(d8, d11, d12);
                            noiseArray[i++] += d13 * d0;
                        }
                    }
                }
            }
        }
    }

    static class NoiseGeneratorOctaves {
        private final NoiseGeneratorImproved[] generatorCollection;
        private final int octaves;
        public NoiseGeneratorOctaves(Random seed, int octavesIn) {
            this.octaves = octavesIn;
            this.generatorCollection = new NoiseGeneratorImproved[octavesIn];
            for (int i = 0; i < octavesIn; ++i) this.generatorCollection[i] = new NoiseGeneratorImproved(seed);
        }
        public double[] generateNoiseOctaves(double[] noiseArray, int xOffset, int yOffset, int zOffset, int xSize, int ySize, int zSize, double xScale, double yScale, double zScale) {
            if (noiseArray == null) noiseArray = new double[xSize * ySize * zSize];
            else for (int i = 0; i < noiseArray.length; ++i) noiseArray[i] = 0.0D;
            double d3 = 1.0D;
            for (int j = 0; j < this.octaves; ++j) {
                double d0 = (double)xOffset * d3 * xScale;
                double d1 = (double)yOffset * d3 * yScale;
                double d2 = (double)zOffset * d3 * zScale;
                long k = lfloor(d0);
                long l = lfloor(d2);
                d0 = d0 - (double)k;
                d2 = d2 - (double)l;
                k = k % 16777216L;
                l = l % 16777216L;
                d0 = d0 + (double)k;
                d2 = d2 + (double)l;
                this.generatorCollection[j].populateNoiseArray(noiseArray, d0, d1, d2, xSize, ySize, zSize, xScale * d3, yScale * d3, zScale * d3, d3);
                d3 /= 2.0D;
            }
            return noiseArray;
        }
        public double[] generateNoiseOctaves(double[] noiseArray, int xOffset, int zOffset, int xSize, int zSize, double xScale, double zScale, double p_76305_10_) {
            return this.generateNoiseOctaves(noiseArray, xOffset, 10, zOffset, xSize, 1, zSize, xScale, 1.0D, zScale);
        }
    }
    static long lfloor(double value) { long i = (long)value; return value < (double)i ? i - 1L : i; }
    static double clampedLerp(double lowerBnd, double upperBnd, double slide) {
        return slide < 0.0D ? lowerBnd : (slide > 1.0D ? upperBnd : lowerBnd + (upperBnd - lowerBnd) * slide);
    }
    static float sqrtf(float value) { return (float)Math.sqrt((double)value); }

    // ----- verbatim NoiseGeneratorSimplex (ctor + getValue + add) + NoiseGeneratorPerlin -----
    static class NoiseGeneratorSimplex {
        private static final int[][] grad3 = new int[][] {{1, 1, 0}, { -1, 1, 0}, {1, -1, 0}, { -1, -1, 0}, {1, 0, 1}, { -1, 0, 1}, {1, 0, -1}, { -1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        public static final double SQRT_3 = Math.sqrt(3.0D);
        private final int[] p;
        public double xo, yo, zo;
        private static final double F2 = 0.5D * (SQRT_3 - 1.0D);
        private static final double G2 = (3.0D - SQRT_3) / 6.0D;
        public NoiseGeneratorSimplex(Random p_i45471_1_) {
            this.p = new int[512];
            this.xo = p_i45471_1_.nextDouble() * 256.0D;
            this.yo = p_i45471_1_.nextDouble() * 256.0D;
            this.zo = p_i45471_1_.nextDouble() * 256.0D;
            for (int i = 0; i < 256; this.p[i] = i++) { ; }
            for (int l = 0; l < 256; ++l) {
                int j = p_i45471_1_.nextInt(256 - l) + l;
                int k = this.p[l];
                this.p[l] = this.p[j];
                this.p[j] = k;
                this.p[l + 256] = this.p[l];
            }
        }
        private static int fastFloor(double value) { return value > 0.0D ? (int)value : (int)value - 1; }
        private static double dot(int[] g, double x, double y) { return (double)g[0] * x + (double)g[1] * y; }
        public double getValue(double p_151605_1_, double p_151605_3_) {
            double d3 = 0.5D * (SQRT_3 - 1.0D);
            double d4 = (p_151605_1_ + p_151605_3_) * d3;
            int i = fastFloor(p_151605_1_ + d4);
            int j = fastFloor(p_151605_3_ + d4);
            double d5 = (3.0D - SQRT_3) / 6.0D;
            double d6 = (double)(i + j) * d5;
            double d7 = (double)i - d6;
            double d8 = (double)j - d6;
            double d9 = p_151605_1_ - d7;
            double d10 = p_151605_3_ - d8;
            int k, l;
            if (d9 > d10) { k = 1; l = 0; } else { k = 0; l = 1; }
            double d11 = d9 - (double)k + d5;
            double d12 = d10 - (double)l + d5;
            double d13 = d9 - 1.0D + 2.0D * d5;
            double d14 = d10 - 1.0D + 2.0D * d5;
            int i1 = i & 255;
            int j1 = j & 255;
            int k1 = this.p[i1 + this.p[j1]] % 12;
            int l1 = this.p[i1 + k + this.p[j1 + l]] % 12;
            int i2 = this.p[i1 + 1 + this.p[j1 + 1]] % 12;
            double d15 = 0.5D - d9 * d9 - d10 * d10;
            double d0;
            if (d15 < 0.0D) { d0 = 0.0D; } else { d15 = d15 * d15; d0 = d15 * d15 * dot(grad3[k1], d9, d10); }
            double d16 = 0.5D - d11 * d11 - d12 * d12;
            double d1;
            if (d16 < 0.0D) { d1 = 0.0D; } else { d16 = d16 * d16; d1 = d16 * d16 * dot(grad3[l1], d11, d12); }
            double d17 = 0.5D - d13 * d13 - d14 * d14;
            double d2;
            if (d17 < 0.0D) { d2 = 0.0D; } else { d17 = d17 * d17; d2 = d17 * d17 * dot(grad3[i2], d13, d14); }
            return 70.0D * (d0 + d1 + d2);
        }
        public void add(double[] p_151606_1_, double p_151606_2_, double p_151606_4_, int p_151606_6_, int p_151606_7_, double p_151606_8_, double p_151606_10_, double p_151606_12_) {
            int i = 0;
            for (int j = 0; j < p_151606_7_; ++j) {
                double d0 = (p_151606_4_ + (double)j) * p_151606_10_ + this.yo;
                for (int k = 0; k < p_151606_6_; ++k) {
                    double d1 = (p_151606_2_ + (double)k) * p_151606_8_ + this.xo;
                    double d5 = (d1 + d0) * F2;
                    int l = fastFloor(d1 + d5);
                    int i1 = fastFloor(d0 + d5);
                    double d6 = (double)(l + i1) * G2;
                    double d7 = (double)l - d6;
                    double d8 = (double)i1 - d6;
                    double d9 = d1 - d7;
                    double d10 = d0 - d8;
                    int j1, k1;
                    if (d9 > d10) { j1 = 1; k1 = 0; } else { j1 = 0; k1 = 1; }
                    double d11 = d9 - (double)j1 + G2;
                    double d12 = d10 - (double)k1 + G2;
                    double d13 = d9 - 1.0D + 2.0D * G2;
                    double d14 = d10 - 1.0D + 2.0D * G2;
                    int l1 = l & 255;
                    int i2 = i1 & 255;
                    int j2 = this.p[l1 + this.p[i2]] % 12;
                    int k2 = this.p[l1 + j1 + this.p[i2 + k1]] % 12;
                    int l2 = this.p[l1 + 1 + this.p[i2 + 1]] % 12;
                    double d15 = 0.5D - d9 * d9 - d10 * d10;
                    double d2;
                    if (d15 < 0.0D) { d2 = 0.0D; } else { d15 = d15 * d15; d2 = d15 * d15 * dot(grad3[j2], d9, d10); }
                    double d16 = 0.5D - d11 * d11 - d12 * d12;
                    double d3;
                    if (d16 < 0.0D) { d3 = 0.0D; } else { d16 = d16 * d16; d3 = d16 * d16 * dot(grad3[k2], d11, d12); }
                    double d17 = 0.5D - d13 * d13 - d14 * d14;
                    double d4;
                    if (d17 < 0.0D) { d4 = 0.0D; } else { d17 = d17 * d17; d4 = d17 * d17 * dot(grad3[l2], d13, d14); }
                    int i3 = i++;
                    p_151606_1_[i3] += 70.0D * (d2 + d3 + d4) * p_151606_12_;
                }
            }
        }
    }

    static class NoiseGeneratorPerlin {
        private final NoiseGeneratorSimplex[] noiseLevels;
        private final int levels;
        public NoiseGeneratorPerlin(Random p_i45470_1_, int p_i45470_2_) {
            this.levels = p_i45470_2_;
            this.noiseLevels = new NoiseGeneratorSimplex[p_i45470_2_];
            for (int i = 0; i < p_i45470_2_; ++i) this.noiseLevels[i] = new NoiseGeneratorSimplex(p_i45470_1_);
        }
        public double getValue(double p_151601_1_, double p_151601_3_) {
            double d0 = 0.0D;
            double d1 = 1.0D;
            for (int i = 0; i < this.levels; ++i) {
                d0 += this.noiseLevels[i].getValue(p_151601_1_ * d1, p_151601_3_ * d1) / d1;
                d1 /= 2.0D;
            }
            return d0;
        }
        public double[] getRegion(double[] p_151599_1_, double p_151599_2_, double p_151599_4_, int p_151599_6_, int p_151599_7_, double p_151599_8_, double p_151599_10_, double p_151599_12_) {
            return this.getRegion(p_151599_1_, p_151599_2_, p_151599_4_, p_151599_6_, p_151599_7_, p_151599_8_, p_151599_10_, p_151599_12_, 0.5D);
        }
        public double[] getRegion(double[] p_151600_1_, double p_151600_2_, double p_151600_4_, int p_151600_6_, int p_151600_7_, double p_151600_8_, double p_151600_10_, double p_151600_12_, double p_151600_14_) {
            if (p_151600_1_ != null && p_151600_1_.length >= p_151600_6_ * p_151600_7_) {
                for (int i = 0; i < p_151600_1_.length; ++i) p_151600_1_[i] = 0.0D;
            } else { p_151600_1_ = new double[p_151600_6_ * p_151600_7_]; }
            double d1 = 1.0D, d0 = 1.0D;
            for (int j = 0; j < this.levels; ++j) {
                this.noiseLevels[j].add(p_151600_1_, p_151600_2_, p_151600_4_, p_151600_6_, p_151600_7_, p_151600_8_ * d0 * d1, p_151600_10_ * d0 * d1, 0.55D / d1);
                d0 *= p_151600_12_;
                d1 *= p_151600_14_;
            }
            return p_151600_1_;
        }
    }

    // ----- verbatim MathHelper (net/minecraft/util/math) sin/cos/floor + SIN_TABLE -----
    private static final float[] SIN_TABLE = new float[65536];
    static { for (int i = 0; i < 65536; ++i) SIN_TABLE[i] = (float)Math.sin((double)i * Math.PI * 2.0D / 65536.0D); }
    static float mhsin(float value) { return SIN_TABLE[(int)(value * 10430.378F) & 65535]; }
    static float mhcos(float value) { return SIN_TABLE[(int)(value * 10430.378F + 16384.0F) & 65535]; }
    static int mhfloor(double value) { int i = (int)value; return value < (double)i ? i - 1 : i; }

    // ----- ChunkPrimer (net/minecraft/world/chunk/ChunkPrimer): char[65536], x<<12|z<<8|y -----
    static class ChunkPrimer {
        final char[] data = new char[65536];
        int getBlockState(int x, int y, int z) { return this.data[getBlockIndex(x, y, z)]; }
        void setBlockState(int x, int y, int z, int state) { this.data[getBlockIndex(x, y, z)] = (char)state; }
        static int getBlockIndex(int x, int y, int z) { return x << 12 | z << 8 | y; }
    }

    // ===== state shared by the verbatim ChunkProviderOverworld / MapGen* bodies =====
    static double[] heightMap = new double[825];
    static double[] depthBuffer = new double[256];
    static double[] mainNoiseRegion, minLimitRegion, maxLimitRegion, depthRegion;
    static int[] curTop = new int[256];      // Biome.topBlock per id (mutable singleton field)
    static int[] curFiller = new int[256];   // Biome.fillerBlock per id
    static int[] fullBiome;                   // biomeIndexLayer.getInts(0,0,16,16) (world.getBiome)
    static ChunkPrimer primer;
    static long worldSeed;
    static NoiseGeneratorOctaves minLimitPerlinNoise, maxLimitPerlinNoise, mainPerlinNoise, depthNoise;
    static int[] biomesForGeneration;         // genBiomes.getInts(-2,-2,10,10) (low-res)

    // ===== verbatim ChunkProviderOverworld.generateHeightmap (per-biome baseHeight/heightVariation) =====
    static void generateHeightmap(int p_185978_1_, int p_185978_2_, int p_185978_3_) {
        depthRegion = depthNoise.generateNoiseOctaves(depthRegion, p_185978_1_, p_185978_3_, 5, 5, 200.0D, 200.0D, 0.5D);
        float f = 684.412F;
        float f1 = 684.412F;
        mainNoiseRegion = mainPerlinNoise.generateNoiseOctaves(mainNoiseRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)(f / 80.0F), (double)(f1 / 160.0F), (double)(f / 80.0F));
        minLimitRegion = minLimitPerlinNoise.generateNoiseOctaves(minLimitRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)f, (double)f1, (double)f);
        maxLimitRegion = maxLimitPerlinNoise.generateNoiseOctaves(maxLimitRegion, p_185978_1_, p_185978_2_, p_185978_3_, 5, 33, 5, (double)f, (double)f1, (double)f);
        int i = 0;
        int j = 0;
        float[] biomeWeights = new float[25];
        for (int a = -2; a <= 2; ++a)
            for (int b = -2; b <= 2; ++b)
                biomeWeights[a + 2 + (b + 2) * 5] = 10.0F / sqrtf((float)(a * a + b * b) + 0.2F);
        for (int k = 0; k < 5; ++k) {
            for (int l = 0; l < 5; ++l) {
                float f2 = 0.0F;
                float f3 = 0.0F;
                float f4 = 0.0F;
                int biome = biomesForGeneration[k + 2 + (l + 2) * 10];
                for (int j1 = -2; j1 <= 2; ++j1) {
                    for (int k1 = -2; k1 <= 2; ++k1) {
                        int biome1 = biomesForGeneration[k + j1 + 2 + (l + k1 + 2) * 10];
                        float f5 = 0.0F + BP.baseHeight(biome1) * 1.0F;
                        float f6 = 0.0F + BP.heightVariation(biome1) * 1.0F;
                        // terrainType != AMPLIFIED
                        float f7 = biomeWeights[j1 + 2 + (k1 + 2) * 5] / (f5 + 2.0F);
                        if (BP.baseHeight(biome1) > BP.baseHeight(biome)) f7 /= 2.0F;
                        f2 += f6 * f7;
                        f3 += f5 * f7;
                        f4 += f7;
                    }
                }
                f2 = f2 / f4;
                f3 = f3 / f4;
                f2 = f2 * 0.9F + 0.1F;
                f3 = (f3 * 4.0F - 1.0F) / 8.0F;
                double d7 = depthRegion[j] / 8000.0D;
                if (d7 < 0.0D) d7 = -d7 * 0.3D;
                d7 = d7 * 3.0D - 2.0D;
                if (d7 < 0.0D) {
                    d7 = d7 / 2.0D;
                    if (d7 < -1.0D) d7 = -1.0D;
                    d7 = d7 / 1.4D;
                    d7 = d7 / 2.0D;
                } else {
                    if (d7 > 1.0D) d7 = 1.0D;
                    d7 = d7 / 8.0D;
                }
                ++j;
                double d8 = (double)f3;
                double d9 = (double)f2;
                d8 = d8 + d7 * 0.2D;
                d8 = d8 * (double)8.5F / 8.0D;
                double d0 = (double)8.5F + d8 * 4.0D;
                for (int l1 = 0; l1 < 33; ++l1) {
                    double d1 = ((double)l1 - d0) * (double)12.0F * 128.0D / 256.0D / d9;
                    if (d1 < 0.0D) d1 *= 4.0D;
                    double d2 = minLimitRegion[i] / (double)512.0F;
                    double d3 = maxLimitRegion[i] / (double)512.0F;
                    double d4 = (mainNoiseRegion[i] / 10.0D + 1.0D) / 2.0D;
                    double d5 = clampedLerp(d2, d3, d4) - d1;
                    if (l1 > 29) {
                        double d6 = (double)((float)(l1 - 29) / 3.0F);
                        d5 = d5 * (1.0D - d6) + -10.0D * d6;
                    }
                    heightMap[i] = d5;
                    ++i;
                }
            }
        }
    }

    // ===== verbatim ChunkProviderOverworld.setBlocksInChunk (density -> STONE / oceanBlock(WATER)) =====
    static void setBlocksInChunk() {
        for (int i = 0; i < 4; ++i) {
            int j = i * 5;
            int k = (i + 1) * 5;
            for (int l = 0; l < 4; ++l) {
                int i1 = (j + l) * 33;
                int j1 = (j + l + 1) * 33;
                int k1 = (k + l) * 33;
                int l1 = (k + l + 1) * 33;
                for (int i2 = 0; i2 < 32; ++i2) {
                    double d0 = 0.125D;
                    double d1 = heightMap[i1 + i2];
                    double d2 = heightMap[j1 + i2];
                    double d3 = heightMap[k1 + i2];
                    double d4 = heightMap[l1 + i2];
                    double d5 = (heightMap[i1 + i2 + 1] - d1) * 0.125D;
                    double d6 = (heightMap[j1 + i2 + 1] - d2) * 0.125D;
                    double d7 = (heightMap[k1 + i2 + 1] - d3) * 0.125D;
                    double d8 = (heightMap[l1 + i2 + 1] - d4) * 0.125D;
                    for (int j2 = 0; j2 < 8; ++j2) {
                        double d9 = 0.25D;
                        double d10 = d1;
                        double d11 = d2;
                        double d12 = (d3 - d1) * 0.25D;
                        double d13 = (d4 - d2) * 0.25D;
                        for (int k2 = 0; k2 < 4; ++k2) {
                            double d14 = 0.25D;
                            double d16 = (d11 - d10) * 0.25D;
                            double lvt_45_1_ = d10 - d16;
                            for (int l2 = 0; l2 < 4; ++l2) {
                                if ((lvt_45_1_ += d16) > 0.0D) {
                                    primer.setBlockState(i * 4 + k2, i2 * 8 + j2, l * 4 + l2, STONE);
                                } else if (i2 * 8 + j2 < SEA_LEVEL) {
                                    primer.setBlockState(i * 4 + k2, i2 * 8 + j2, l * 4 + l2, WATER);
                                }
                            }
                            d10 += d12;
                            d11 += d13;
                        }
                        d1 += d5; d2 += d6; d3 += d7; d4 += d8;
                    }
                }
            }
        }
    }

    // getFloatTemperature: y<=64 reachable here -> flat temperature (TEMPERATURE_NOISE branch dead).
    static float getFloatTemperature(float temp, int y) {
        if (y > 64) { return temp; } else { return temp; }
    }

    // ===== verbatim Biome.generateBiomeTerrain (this.topBlock/fillerBlock -> top/filler args) =====
    static void generateBiomeTerrain(Random rand, ChunkPrimer chunkPrimerIn, int x, int z, double noiseVal, int topBlock, int fillerBlock, float temp) {
        int i = SEA_LEVEL;
        int iblockstate = topBlock;
        int iblockstate1 = fillerBlock;
        int j = -1;
        int k = (int)(noiseVal / 3.0D + 3.0D + rand.nextDouble() * 0.25D);
        int l = x & 15;
        int i1 = z & 15;
        for (int j1 = 255; j1 >= 0; --j1) {
            if (j1 <= rand.nextInt(5)) {
                chunkPrimerIn.setBlockState(i1, j1, l, BEDROCK);
            } else {
                int iblockstate2 = chunkPrimerIn.getBlockState(i1, j1, l);
                if (iblockstate2 == AIR) {
                    j = -1;
                } else if (iblockstate2 == STONE) {
                    if (j == -1) {
                        if (k <= 0) {
                            iblockstate = AIR;
                            iblockstate1 = STONE;
                        } else if (j1 >= i - 4 && j1 <= i + 1) {
                            iblockstate = topBlock;
                            iblockstate1 = fillerBlock;
                        }
                        if (j1 < i && iblockstate == AIR) {
                            if (getFloatTemperature(temp, j1) < 0.15F) iblockstate = ICE;
                            else iblockstate = WATER;
                        }
                        j = k;
                        if (j1 >= i - 1) {
                            chunkPrimerIn.setBlockState(i1, j1, l, iblockstate);
                        } else if (j1 < i - 7 - k) {
                            iblockstate = AIR;
                            iblockstate1 = STONE;
                            chunkPrimerIn.setBlockState(i1, j1, l, GRAVEL);
                        } else {
                            chunkPrimerIn.setBlockState(i1, j1, l, iblockstate1);
                        }
                    } else if (j > 0) {
                        --j;
                        chunkPrimerIn.setBlockState(i1, j1, l, iblockstate1);
                        if (j == 0 && iblockstate1 == SAND && k > 1) {
                            j = rand.nextInt(4) + Math.max(0, j1 - 63);
                            iblockstate1 = SANDSTONE;
                        }
                    }
                }
            }
        }
    }

    // genTerrainBlocks dispatch = verbatim Biome / BiomeHills / BiomeTaiga / BiomeSwamp methods,
    // with the singleton this.topBlock/fillerBlock fields modeled as curTop[id]/curFiller[id].
    static NoiseGeneratorPerlin GRASS_COLOR_NOISE;
    static void genTerrainBlocks(int biome, Random rand, ChunkPrimer primer, int x, int z, double noiseVal) {
        int type = BP.surfType(biome);
        if (type == SURF_HILLS) {
            curTop[biome] = GRASS;
            curFiller[biome] = DIRT;
            int ht = BP.hillsType(biome);
            if ((noiseVal < -1.0D || noiseVal > 2.0D) && ht == HILLS_MUTATED) {
                curTop[biome] = GRAVEL;
                curFiller[biome] = GRAVEL;
            } else if (noiseVal > 1.0D && ht != HILLS_EXTRA_TREES) {
                curTop[biome] = STONE;
                curFiller[biome] = STONE;
            }
        } else if (type == SURF_TAIGA) {
            int tt = BP.taigaType(biome);
            if (tt == TAIGA_MEGA || tt == TAIGA_MEGA_SPRUCE) {
                curTop[biome] = GRASS;
                curFiller[biome] = DIRT;
                if (noiseVal > 1.75D) curTop[biome] = COARSE_DIRT;
                else if (noiseVal > -0.95D) curTop[biome] = PODZOL;
            }
        } else if (type == SURF_SWAMP) {
            double d0 = GRASS_COLOR_NOISE.getValue((double)x * 0.25D, (double)z * 0.25D);
            if (d0 > 0.0D) {
                int i = x & 15;
                int j = z & 15;
                for (int k = 255; k >= 0; --k) {
                    if (primer.getBlockState(j, k, i) != AIR) {
                        if (k == 62 && primer.getBlockState(j, k, i) != WATER) {
                            primer.setBlockState(j, k, i, WATER);
                            if (d0 < 0.12D) primer.setBlockState(j, k + 1, i, WATER_LILY);
                        }
                        break;
                    }
                }
            }
        }
        generateBiomeTerrain(rand, primer, x, z, noiseVal, curTop[biome], curFiller[biome], BP.temperature(biome));
    }

    // ===== verbatim MapGenCaves (world.getBiome -> fullBiome; biome.topBlock -> curTop[id]) =====
    static int caveRange = 8;
    static Random caveRand = new Random();
    static boolean caveCanReplace(int s, int up) {
        return s == STONE ? true : (s == DIRT ? true : (s == GRASS ? true : (s == HARDENED_CLAY ? true : (s == STAINED_HARDENED_CLAY ? true : (s == SANDSTONE ? true : (s == RED_SANDSTONE ? true : (s == MYCELIUM ? true : (s == SNOW_LAYER ? true : (s == SAND || s == GRAVEL) && up != WATER && up != FLOWING_WATER))))))));
    }
    static boolean caveIsOcean(int x, int y, int z) {
        int block = primer.getBlockState(x, y, z);
        return block == FLOWING_WATER || block == WATER;
    }
    static boolean caveIsTop(int x, int y, int z, int chunkX, int chunkZ) {
        int biome = fullBiome[x + z * 16];   // per-chunk (local) biome array
        int state = primer.getBlockState(x, y, z);
        return BP.caveException(biome) ? state == GRASS : state == curTop[biome];
    }
    static void caveDigBlock(int x, int y, int z, int chunkX, int chunkZ, boolean foundTop, int state, int up) {
        int biome = fullBiome[x + z * 16];   // per-chunk (local) biome array
        int top = curTop[biome];
        int filler = curFiller[biome];
        if (caveCanReplace(state, up) || state == top || state == filler) {
            if (y - 1 < 10) {
                primer.setBlockState(x, y, z, LAVA);
            } else {
                primer.setBlockState(x, y, z, AIR);
                if (foundTop && primer.getBlockState(x, y - 1, z) == filler) primer.setBlockState(x, y - 1, z, top);
            }
        }
    }
    static void caveAddRoom(long p_180703_1_, int p_180703_3_, int p_180703_4_, double p_180703_6_, double p_180703_8_, double p_180703_10_) {
        caveAddTunnel(p_180703_1_, p_180703_3_, p_180703_4_, p_180703_6_, p_180703_8_, p_180703_10_, 1.0F + caveRand.nextFloat() * 6.0F, 0.0F, 0.0F, -1, -1, 0.5D);
    }
    static void caveAddTunnel(long p_180702_1_, int p_180702_3_, int p_180702_4_, double p_180702_6_, double p_180702_8_, double p_180702_10_, float p_180702_12_, float p_180702_13_, float p_180702_14_, int p_180702_15_, int p_180702_16_, double p_180702_17_) {
        double d0 = (double)(p_180702_3_ * 16 + 8);
        double d1 = (double)(p_180702_4_ * 16 + 8);
        float f = 0.0F;
        float f1 = 0.0F;
        Random random = new Random(p_180702_1_);
        if (p_180702_16_ <= 0) {
            int i = caveRange * 16 - 16;
            p_180702_16_ = i - random.nextInt(i / 4);
        }
        boolean flag2 = false;
        if (p_180702_15_ == -1) { p_180702_15_ = p_180702_16_ / 2; flag2 = true; }
        int j = random.nextInt(p_180702_16_ / 2) + p_180702_16_ / 4;
        for (boolean flag = random.nextInt(6) == 0; p_180702_15_ < p_180702_16_; ++p_180702_15_) {
            double d2 = 1.5D + (double)(mhsin((float)p_180702_15_ * (float)Math.PI / (float)p_180702_16_) * p_180702_12_);
            double d3 = d2 * p_180702_17_;
            float f2 = mhcos(p_180702_14_);
            float f3 = mhsin(p_180702_14_);
            p_180702_6_ += (double)(mhcos(p_180702_13_) * f2);
            p_180702_8_ += (double)f3;
            p_180702_10_ += (double)(mhsin(p_180702_13_) * f2);
            if (flag) { p_180702_14_ = p_180702_14_ * 0.92F; } else { p_180702_14_ = p_180702_14_ * 0.7F; }
            p_180702_14_ = p_180702_14_ + f1 * 0.1F;
            p_180702_13_ += f * 0.1F;
            f1 = f1 * 0.9F;
            f = f * 0.75F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;
            if (!flag2 && p_180702_15_ == j && p_180702_12_ > 1.0F && p_180702_16_ > 0) {
                caveAddTunnel(random.nextLong(), p_180702_3_, p_180702_4_, p_180702_6_, p_180702_8_, p_180702_10_, random.nextFloat() * 0.5F + 0.5F, p_180702_13_ - ((float)Math.PI / 2F), p_180702_14_ / 3.0F, p_180702_15_, p_180702_16_, 1.0D);
                caveAddTunnel(random.nextLong(), p_180702_3_, p_180702_4_, p_180702_6_, p_180702_8_, p_180702_10_, random.nextFloat() * 0.5F + 0.5F, p_180702_13_ + ((float)Math.PI / 2F), p_180702_14_ / 3.0F, p_180702_15_, p_180702_16_, 1.0D);
                return;
            }
            if (flag2 || random.nextInt(4) != 0) {
                double d4 = p_180702_6_ - d0;
                double d5 = p_180702_10_ - d1;
                double d6 = (double)(p_180702_16_ - p_180702_15_);
                double d7 = (double)(p_180702_12_ + 2.0F + 16.0F);
                if (d4 * d4 + d5 * d5 - d6 * d6 > d7 * d7) return;
                if (p_180702_6_ >= d0 - 16.0D - d2 * 2.0D && p_180702_10_ >= d1 - 16.0D - d2 * 2.0D && p_180702_6_ <= d0 + 16.0D + d2 * 2.0D && p_180702_10_ <= d1 + 16.0D + d2 * 2.0D) {
                    int k2 = mhfloor(p_180702_6_ - d2) - p_180702_3_ * 16 - 1;
                    int k = mhfloor(p_180702_6_ + d2) - p_180702_3_ * 16 + 1;
                    int l2 = mhfloor(p_180702_8_ - d3) - 1;
                    int l = mhfloor(p_180702_8_ + d3) + 1;
                    int i3 = mhfloor(p_180702_10_ - d2) - p_180702_4_ * 16 - 1;
                    int i1 = mhfloor(p_180702_10_ + d2) - p_180702_4_ * 16 + 1;
                    if (k2 < 0) k2 = 0;
                    if (k > 16) k = 16;
                    if (l2 < 1) l2 = 1;
                    if (l > 248) l = 248;
                    if (i3 < 0) i3 = 0;
                    if (i1 > 16) i1 = 16;
                    boolean flag3 = false;
                    for (int j1 = k2; !flag3 && j1 < k; ++j1) {
                        for (int k1 = i3; !flag3 && k1 < i1; ++k1) {
                            for (int l1 = l + 1; !flag3 && l1 >= l2 - 1; --l1) {
                                if (l1 >= 0 && l1 < 256) {
                                    if (caveIsOcean(j1, l1, k1)) flag3 = true;
                                    if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                                }
                            }
                        }
                    }
                    if (!flag3) {
                        for (int j3 = k2; j3 < k; ++j3) {
                            double d10 = ((double)(j3 + p_180702_3_ * 16) + 0.5D - p_180702_6_) / d2;
                            for (int i2 = i3; i2 < i1; ++i2) {
                                double d8 = ((double)(i2 + p_180702_4_ * 16) + 0.5D - p_180702_10_) / d2;
                                boolean flag1 = false;
                                if (d10 * d10 + d8 * d8 < 1.0D) {
                                    for (int j2 = l; j2 > l2; --j2) {
                                        double d9 = ((double)(j2 - 1) + 0.5D - p_180702_8_) / d3;
                                        if (d9 > -0.7D && d10 * d10 + d9 * d9 + d8 * d8 < 1.0D) {
                                            int iblockstate1 = primer.getBlockState(j3, j2, i2);
                                            int iblockstate2 = primer.getBlockState(j3, j2 + 1, i2);
                                            if (caveIsTop(j3, j2, i2, p_180702_3_, p_180702_4_)) flag1 = true;
                                            caveDigBlock(j3, j2, i2, p_180702_3_, p_180702_4_, flag1, iblockstate1, iblockstate2);
                                        }
                                    }
                                }
                            }
                        }
                        if (flag2) break;
                    }
                }
            }
        }
    }
    static void caveRecursive(int chunkX, int chunkZ, int p_180701_4_, int p_180701_5_) {
        int i = caveRand.nextInt(caveRand.nextInt(caveRand.nextInt(15) + 1) + 1);
        if (caveRand.nextInt(7) != 0) i = 0;
        for (int j = 0; j < i; ++j) {
            double d0 = (double)(chunkX * 16 + caveRand.nextInt(16));
            double d1 = (double)caveRand.nextInt(caveRand.nextInt(120) + 8);
            double d2 = (double)(chunkZ * 16 + caveRand.nextInt(16));
            int k = 1;
            if (caveRand.nextInt(4) == 0) {
                caveAddRoom(caveRand.nextLong(), p_180701_4_, p_180701_5_, d0, d1, d2);
                k += caveRand.nextInt(4);
            }
            for (int l = 0; l < k; ++l) {
                float f = caveRand.nextFloat() * ((float)Math.PI * 2F);
                float f1 = (caveRand.nextFloat() - 0.5F) * 2.0F / 8.0F;
                float f2 = caveRand.nextFloat() * 2.0F + caveRand.nextFloat();
                if (caveRand.nextInt(10) == 0) f2 *= caveRand.nextFloat() * caveRand.nextFloat() * 3.0F + 1.0F;
                caveAddTunnel(caveRand.nextLong(), p_180701_4_, p_180701_5_, d0, d1, d2, f2, f, f1, 0, 0, 1.0D);
            }
        }
    }
    static void caveGenerate(int x, int z) {
        int i = caveRange;
        caveRand.setSeed(worldSeed);
        long j = caveRand.nextLong();
        long k = caveRand.nextLong();
        for (int l = x - i; l <= x + i; ++l) {
            for (int i1 = z - i; i1 <= z + i; ++i1) {
                long j1 = (long)l * j;
                long k1 = (long)i1 * k;
                caveRand.setSeed(j1 ^ k1 ^ worldSeed);
                caveRecursive(l, i1, x, z);
            }
        }
    }

    // ===== verbatim MapGenRavine (world.getBiome -> fullBiome; biome.topBlock -> curTop[id]) =====
    static int ravineRange = 8;
    static Random ravineRand = new Random();
    static final float[] rs = new float[1024];
    static boolean ravineIsOcean(int x, int y, int z) {
        int block = primer.getBlockState(x, y, z);
        return block == FLOWING_WATER || block == WATER;
    }
    static boolean ravineIsTop(int x, int y, int z, int chunkX, int chunkZ) {
        int biome = fullBiome[x + z * 16];   // per-chunk (local) biome array
        int state = primer.getBlockState(x, y, z);
        return BP.ravineException(biome) ? state == GRASS : state == curTop[biome];
    }
    static void ravineDigBlock(int x, int y, int z, int chunkX, int chunkZ, boolean foundTop) {
        int biome = fullBiome[x + z * 16];   // per-chunk (local) biome array
        int state = primer.getBlockState(x, y, z);
        int top = curTop[biome];
        int filler = curFiller[biome];
        if (state == STONE || state == top || state == filler) {
            if (y - 1 < 10) {
                primer.setBlockState(x, y, z, FLOWING_LAVA);
            } else {
                primer.setBlockState(x, y, z, AIR);
                if (foundTop && primer.getBlockState(x, y - 1, z) == filler) primer.setBlockState(x, y - 1, z, top);
            }
        }
    }
    static void ravineAddTunnel(long p_180707_1_, int p_180707_3_, int p_180707_4_, double p_180707_6_, double p_180707_8_, double p_180707_10_, float p_180707_12_, float p_180707_13_, float p_180707_14_, int p_180707_15_, int p_180707_16_, double p_180707_17_) {
        Random random = new Random(p_180707_1_);
        double d0 = (double)(p_180707_3_ * 16 + 8);
        double d1 = (double)(p_180707_4_ * 16 + 8);
        float f = 0.0F;
        float f1 = 0.0F;
        if (p_180707_16_ <= 0) {
            int i = ravineRange * 16 - 16;
            p_180707_16_ = i - random.nextInt(i / 4);
        }
        boolean flag1 = false;
        if (p_180707_15_ == -1) { p_180707_15_ = p_180707_16_ / 2; flag1 = true; }
        float f2 = 1.0F;
        for (int j = 0; j < 256; ++j) {
            if (j == 0 || random.nextInt(3) == 0) f2 = 1.0F + random.nextFloat() * random.nextFloat();
            rs[j] = f2 * f2;
        }
        for (; p_180707_15_ < p_180707_16_; ++p_180707_15_) {
            double d9 = 1.5D + (double)(mhsin((float)p_180707_15_ * (float)Math.PI / (float)p_180707_16_) * p_180707_12_);
            double d2 = d9 * p_180707_17_;
            d9 = d9 * ((double)random.nextFloat() * 0.25D + 0.75D);
            d2 = d2 * ((double)random.nextFloat() * 0.25D + 0.75D);
            float f3 = mhcos(p_180707_14_);
            float f4 = mhsin(p_180707_14_);
            p_180707_6_ += (double)(mhcos(p_180707_13_) * f3);
            p_180707_8_ += (double)f4;
            p_180707_10_ += (double)(mhsin(p_180707_13_) * f3);
            p_180707_14_ = p_180707_14_ * 0.7F;
            p_180707_14_ = p_180707_14_ + f1 * 0.05F;
            p_180707_13_ += f * 0.05F;
            f1 = f1 * 0.8F;
            f = f * 0.5F;
            f1 = f1 + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2.0F;
            f = f + (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4.0F;
            if (flag1 || random.nextInt(4) != 0) {
                double d3 = p_180707_6_ - d0;
                double d4 = p_180707_10_ - d1;
                double d5 = (double)(p_180707_16_ - p_180707_15_);
                double d6 = (double)(p_180707_12_ + 2.0F + 16.0F);
                if (d3 * d3 + d4 * d4 - d5 * d5 > d6 * d6) return;
                if (p_180707_6_ >= d0 - 16.0D - d9 * 2.0D && p_180707_10_ >= d1 - 16.0D - d9 * 2.0D && p_180707_6_ <= d0 + 16.0D + d9 * 2.0D && p_180707_10_ <= d1 + 16.0D + d9 * 2.0D) {
                    int k2 = mhfloor(p_180707_6_ - d9) - p_180707_3_ * 16 - 1;
                    int k = mhfloor(p_180707_6_ + d9) - p_180707_3_ * 16 + 1;
                    int l2 = mhfloor(p_180707_8_ - d2) - 1;
                    int l = mhfloor(p_180707_8_ + d2) + 1;
                    int i3 = mhfloor(p_180707_10_ - d9) - p_180707_4_ * 16 - 1;
                    int i1 = mhfloor(p_180707_10_ + d9) - p_180707_4_ * 16 + 1;
                    if (k2 < 0) k2 = 0;
                    if (k > 16) k = 16;
                    if (l2 < 1) l2 = 1;
                    if (l > 248) l = 248;
                    if (i3 < 0) i3 = 0;
                    if (i1 > 16) i1 = 16;
                    boolean flag2 = false;
                    for (int j1 = k2; !flag2 && j1 < k; ++j1) {
                        for (int k1 = i3; !flag2 && k1 < i1; ++k1) {
                            for (int l1 = l + 1; !flag2 && l1 >= l2 - 1; --l1) {
                                if (l1 >= 0 && l1 < 256) {
                                    if (ravineIsOcean(j1, l1, k1)) flag2 = true;
                                    if (l1 != l2 - 1 && j1 != k2 && j1 != k - 1 && k1 != i3 && k1 != i1 - 1) l1 = l2;
                                }
                            }
                        }
                    }
                    if (!flag2) {
                        for (int j3 = k2; j3 < k; ++j3) {
                            double d10 = ((double)(j3 + p_180707_3_ * 16) + 0.5D - p_180707_6_) / d9;
                            for (int i2 = i3; i2 < i1; ++i2) {
                                double d7 = ((double)(i2 + p_180707_4_ * 16) + 0.5D - p_180707_10_) / d9;
                                boolean flag = false;
                                if (d10 * d10 + d7 * d7 < 1.0D) {
                                    for (int j2 = l; j2 > l2; --j2) {
                                        double d8 = ((double)(j2 - 1) + 0.5D - p_180707_8_) / d2;
                                        if ((d10 * d10 + d7 * d7) * (double)rs[j2 - 1] + d8 * d8 / 6.0D < 1.0D) {
                                            if (ravineIsTop(j3, j2, i2, p_180707_3_, p_180707_4_)) flag = true;
                                            ravineDigBlock(j3, j2, i2, p_180707_3_, p_180707_4_, flag);
                                        }
                                    }
                                }
                            }
                        }
                        if (flag1) break;
                    }
                }
            }
        }
    }
    static void ravineRecursive(int chunkX, int chunkZ, int p_180701_4_, int p_180701_5_) {
        if (ravineRand.nextInt(50) == 0) {
            double d0 = (double)(chunkX * 16 + ravineRand.nextInt(16));
            double d1 = (double)(ravineRand.nextInt(ravineRand.nextInt(40) + 8) + 20);
            double d2 = (double)(chunkZ * 16 + ravineRand.nextInt(16));
            for (int j = 0; j < 1; ++j) {
                float f = ravineRand.nextFloat() * ((float)Math.PI * 2F);
                float f1 = (ravineRand.nextFloat() - 0.5F) * 2.0F / 8.0F;
                float f2 = (ravineRand.nextFloat() * 2.0F + ravineRand.nextFloat()) * 2.0F;
                ravineAddTunnel(ravineRand.nextLong(), p_180701_4_, p_180701_5_, d0, d1, d2, f2, f, f1, 0, 0, 3.0D);
            }
        }
    }
    static void ravineGenerate(int x, int z) {
        int i = ravineRange;
        ravineRand.setSeed(worldSeed);
        long j = ravineRand.nextLong();
        long k = ravineRand.nextLong();
        for (int l = x - i; l <= x + i; ++l) {
            for (int i1 = z - i; i1 <= z + i; ++i1) {
                long j1 = (long)l * j;
                long k1 = (long)i1 * k;
                ravineRand.setSeed(j1 ^ k1 ^ worldSeed);
                ravineRecursive(l, i1, x, z);
            }
        }
    }

    // =====================================================================================
    // ===== POPULATE / DECORATION (verbatim ChunkProviderOverworld.populate + BiomeDecorator +
    // =====  Biome.decorate overrides + WorldGen* feature bodies), over a 2x2-chunk world model.
    // =====  Sanctioned shims (identical to core/populate.h): block ids -> small ints; World/Block
    // =====  predicates -> integer-id checks; getLight = exposed?15:0; immediateBlockTick = no-op;
    // =====  getBiome = voronoi. Cuts: animals (no blocks), Forge hooks (no-ops), big mushroom &
    // =====  cactus (perChunk 0, unreached). See core/populate.h header for full justification.
    // =====================================================================================

    // new feature block codes (0..20 == the CB_*/AIR..COARSE_DIRT set above)
    static final int GRANITE=21, DIORITE=22, ANDESITE=23, COAL_ORE=24, IRON_ORE=25, GOLD_ORE=26,
        REDSTONE_ORE=27, DIAMOND_ORE=28, LAPIS_ORE=29, CLAY=30, LOG_OAK=31, LOG_BIRCH=32, LOG_SPRUCE=33,
        LEAVES_OAK=34, LEAVES_BIRCH=35, LEAVES_SPRUCE=36, LOG_OAK_X=37, LOG_OAK_Z=38, TALLGRASS=39,
        FERN=40, DEADBUSH=41, BROWN_MUSHROOM=42, RED_MUSHROOM=43, REEDS=44, COBBLESTONE=45,
        MOSSY_COBBLESTONE=46, MOB_SPAWNER=47, BONE_BLOCK=48, CHEST=49, YELLOW_FLOWER=50,
        RED_FLOWER_BASE=51, DPLANT_LOWER_BASE=60, DPLANT_UPPER=66, PUMPKIN_BASE=67, VINE_BASE=71;

    static final int W_X=32, W_Y=256, W_Z=32, W_N=W_X*W_Y*W_Z;
    static char[] world;
    static int[] wbiome = new int[W_X*W_Z];
    static int bigtreeHeightLimit;
    static NoiseGeneratorPerlin surfaceNoiseG;
    static GenLayer genBiomesG, biomeIndexLayerG;

    static int widx(int x,int y,int z){ return (x*W_Z+z)*W_Y+y; }
    static boolean winb(int x,int y,int z){ return x>=0&&x<W_X&&y>=0&&y<W_Y&&z>=0&&z<W_Z; }
    static int wget(int x,int y,int z){ return winb(x,y,z)? world[widx(x,y,z)] : AIR; }
    static void wset(int x,int y,int z,int v){ if(winb(x,y,z)) world[widx(x,y,z)]=(char)v; }

    static int pbOpacity(int c){
        switch(c){
            case AIR: case TALLGRASS: case FERN: case DEADBUSH: case BROWN_MUSHROOM:
            case RED_MUSHROOM: case REEDS: case WATER_LILY: case SNOW_LAYER: case YELLOW_FLOWER:
            case DPLANT_UPPER: return 0;
            case LEAVES_OAK: case LEAVES_BIRCH: case LEAVES_SPRUCE: return 1;
            case WATER: case FLOWING_WATER: case ICE: return 3;
            default:
                if(c>=RED_FLOWER_BASE && c<RED_FLOWER_BASE+9) return 0;
                if(c>=DPLANT_LOWER_BASE && c<=DPLANT_UPPER) return 0;
                if(c>=VINE_BASE && c<VINE_BASE+4) return 0;
                return 255;
        }
    }
    static boolean pbIsAir(int c){ return c==AIR; }
    static boolean pbIsWater(int c){ return c==WATER||c==FLOWING_WATER; }
    static boolean pbIsLava(int c){ return c==LAVA||c==FLOWING_LAVA; }
    static boolean pbIsLiquid(int c){ return pbIsWater(c)||pbIsLava(c); }
    static boolean pbIsLeaves(int c){ return c==LEAVES_OAK||c==LEAVES_BIRCH||c==LEAVES_SPRUCE; }
    static boolean pbIsLog(int c){ return c==LOG_OAK||c==LOG_BIRCH||c==LOG_SPRUCE||c==LOG_OAK_X||c==LOG_OAK_Z; }
    static boolean pbIsVine(int c){ return c>=VINE_BASE && c<VINE_BASE+4; }
    static boolean pbIsPlant(int c){
        if(c==TALLGRASS||c==FERN||c==DEADBUSH||c==BROWN_MUSHROOM||c==RED_MUSHROOM||c==REEDS||c==WATER_LILY||c==YELLOW_FLOWER) return true;
        if(c>=RED_FLOWER_BASE && c<RED_FLOWER_BASE+9) return true;
        if(c>=DPLANT_LOWER_BASE && c<=DPLANT_UPPER) return true;
        return false;
    }
    static boolean pbBlocksMovement(int c){
        if(pbIsAir(c)||pbIsLiquid(c)||pbIsPlant(c)||pbIsVine(c)||c==SNOW_LAYER) return false;
        return true;
    }
    static boolean pbIsSolid(int c){ return pbBlocksMovement(c); }
    static boolean pbIsStone(int c){ return c==STONE||c==GRANITE||c==DIORITE||c==ANDESITE; }
    static boolean pbIsDirt(int c){ return c==DIRT||c==PODZOL||c==COARSE_DIRT; }
    static boolean pbNaturalStone(int c){ return pbIsStone(c); }
    static boolean pbCanGrowInto(int c){ return pbIsAir(c)||pbIsLeaves(c)||c==GRASS||pbIsDirt(c)||pbIsLog(c)||pbIsVine(c); }
    static boolean pbCanBeReplacedByLeaves(int c){ return pbIsAir(c)||pbIsLeaves(c); }
    static boolean pbCanSustainSapling(int s){ return s==GRASS||pbIsDirt(s); }
    static boolean pbCanSustainBush(int s){ return s==GRASS||pbIsDirt(s); }

    static boolean wIsAir(int x,int y,int z){ return pbIsAir(wget(x,y,z)); }
    static int wHeight(int x,int z){ for(int y=W_Y-1;y>=0;--y) if(pbOpacity(wget(x,y,z))>0) return y+1; return 0; }
    static int wLight(int x,int y,int z){ return y>=wHeight(x,z)?15:0; }
    static int wTopSolidOrLiquid(int x,int z){
        int by=W_Y;
        while(by>=1){ int s=wget(x,by-1,z); if(pbBlocksMovement(s)&&!pbIsLeaves(s)) break; --by; }
        return by;
    }
    static int wGetBiome(int x,int z){ if(x<0||x>=W_X||z<0||z>=W_Z) return 1; return wbiome[x*W_Z+z]; }

    static boolean wgIsReplaceableTree(int x,int y,int z){
        int c=wget(x,y,z); return pbIsAir(c)||pbIsLeaves(c)||pbIsLog(c)||pbCanGrowInto(c);
    }
    static void wgOnPlantGrow(int x,int y,int z){ if(wget(x,y,z)==GRASS) wset(x,y,z,DIRT); }

    // ----- WorldGenMinable.generate (StonePredicate = natural stone) -----
    static void wgMinable(Random rand,int posX,int posY,int posZ,int num,int ore){
        float f = rand.nextFloat() * (float)Math.PI;
        double d0 = (double)((float)(posX+8) + mhsin(f)*(float)num/8.0F);
        double d1 = (double)((float)(posX+8) - mhsin(f)*(float)num/8.0F);
        double d2 = (double)((float)(posZ+8) + mhcos(f)*(float)num/8.0F);
        double d3 = (double)((float)(posZ+8) - mhcos(f)*(float)num/8.0F);
        double d4 = (double)(posY + rand.nextInt(3) - 2);
        double d5 = (double)(posY + rand.nextInt(3) - 2);
        for(int i=0;i<num;++i){
            float f1=(float)i/(float)num;
            double d6=d0+(d1-d0)*(double)f1;
            double d7=d4+(d5-d4)*(double)f1;
            double d8=d2+(d3-d2)*(double)f1;
            double d9=rand.nextDouble()*(double)num/16.0D;
            double d10=(double)(mhsin((float)Math.PI*f1)+1.0F)*d9+1.0D;
            double d11=(double)(mhsin((float)Math.PI*f1)+1.0F)*d9+1.0D;
            int j=mhfloor(d6-d10/2.0D), k=mhfloor(d7-d11/2.0D), l=mhfloor(d8-d10/2.0D);
            int i1=mhfloor(d6+d10/2.0D), j1=mhfloor(d7+d11/2.0D), k1=mhfloor(d8+d10/2.0D);
            for(int l1=j;l1<=i1;++l1){
                double d12=((double)l1+0.5D-d6)/(d10/2.0D);
                if(d12*d12<1.0D){
                    for(int i2=k;i2<=j1;++i2){
                        double d13=((double)i2+0.5D-d7)/(d11/2.0D);
                        if(d12*d12+d13*d13<1.0D){
                            for(int j2=l;j2<=k1;++j2){
                                double d14=((double)j2+0.5D-d8)/(d10/2.0D);
                                if(d12*d12+d13*d13+d14*d14<1.0D){
                                    if(pbNaturalStone(wget(l1,i2,j2))) wset(l1,i2,j2,ore);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // ----- WorldGenSand / WorldGenClay -----
    static void wgSand(Random rand,int px,int py,int pz,int radius,int block){
        if(!pbIsWater(wget(px,py,pz))) return;
        int i=rand.nextInt(radius-2)+2;
        for(int k=px-i;k<=px+i;++k) for(int l=pz-i;l<=pz+i;++l){
            int i1=k-px,j1=l-pz;
            if(i1*i1+j1*j1<=i*i) for(int k1=py-2;k1<=py+2;++k1){
                int b=wget(k,k1,l); if(b==DIRT||b==GRASS) wset(k,k1,l,block);
            }
        }
    }
    static void wgClay(Random rand,int px,int py,int pz,int num){
        if(!pbIsWater(wget(px,py,pz))) return;
        int i=rand.nextInt(num-2)+2;
        for(int k=px-i;k<=px+i;++k) for(int l=pz-i;l<=pz+i;++l){
            int i1=k-px,j1=l-pz;
            if(i1*i1+j1*j1<=i*i) for(int k1=py-1;k1<=py+1;++k1){
                int b=wget(k,k1,l); if(b==DIRT||b==CLAY) wset(k,k1,l,CLAY);
            }
        }
    }
    // ----- WorldGenTrees (oak, vinesGrow=false) -----
    static boolean wgTrees(Random rand,int posX,int posY,int posZ,int metaWood,int metaLeaves){
        int i=rand.nextInt(3)+4; boolean flag=true;
        if(posY>=1 && posY+i+1<=W_Y){
            for(int j=posY;j<=posY+1+i;++j){
                int k=1; if(j==posY) k=0; if(j>=posY+1+i-2) k=2;
                for(int l=posX-k;l<=posX+k&&flag;++l) for(int i1=posZ-k;i1<=posZ+k&&flag;++i1){
                    if(j>=0&&j<W_Y){ if(!wgIsReplaceableTree(l,j,i1)) flag=false; } else flag=false;
                }
            }
            if(!flag) return false;
            int state=wget(posX,posY-1,posZ);
            if(pbCanSustainSapling(state) && posY<W_Y-i-1){
                wgOnPlantGrow(posX,posY-1,posZ);
                for(int i3=posY-3+i;i3<=posY+i;++i3){
                    int i4=i3-(posY+i); int j1=1-i4/2;
                    for(int k1=posX-j1;k1<=posX+j1;++k1){
                        int l1=k1-posX;
                        for(int i2=posZ-j1;i2<=posZ+j1;++i2){
                            int j2=i2-posZ;
                            if(Math.abs(l1)!=j1 || Math.abs(j2)!=j1 || rand.nextInt(2)!=0 && i4!=0){
                                int cs=wget(k1,i3,i2);
                                if(pbIsAir(cs)||pbIsLeaves(cs)) wset(k1,i3,i2,metaLeaves);
                            }
                        }
                    }
                }
                for(int j3=0;j3<i;++j3){
                    int cs=wget(posX,posY+j3,posZ);
                    if(pbIsAir(cs)||pbIsLeaves(cs)) wset(posX,posY+j3,posZ,metaWood);
                }
                return true;
            }
            return false;
        }
        return false;
    }
    // ----- WorldGenBirchTree (useExtraRandomHeight=false) -----
    static boolean wgBirch(Random rand,int posX,int posY,int posZ){
        int i=rand.nextInt(3)+5; boolean flag=true;
        if(posY>=1 && posY+i+1<=256){
            for(int j=posY;j<=posY+1+i;++j){
                int k=1; if(j==posY) k=0; if(j>=posY+1+i-2) k=2;
                for(int l=posX-k;l<=posX+k&&flag;++l) for(int i1=posZ-k;i1<=posZ+k&&flag;++i1){
                    if(j>=0&&j<W_Y){ if(!wgIsReplaceableTree(l,j,i1)) flag=false; } else flag=false;
                }
            }
            if(!flag) return false;
            int state=wget(posX,posY-1,posZ);
            if(pbCanSustainSapling(state) && posY<W_Y-i-1){
                wgOnPlantGrow(posX,posY-1,posZ);
                for(int i2=posY-3+i;i2<=posY+i;++i2){
                    int k2=i2-(posY+i); int l2=1-k2/2;
                    for(int i3=posX-l2;i3<=posX+l2;++i3){
                        int j1=i3-posX;
                        for(int k1=posZ-l2;k1<=posZ+l2;++k1){
                            int l1=k1-posZ;
                            if(Math.abs(j1)!=l2 || Math.abs(l1)!=l2 || rand.nextInt(2)!=0 && k2!=0){
                                int s2=wget(i3,i2,k1);
                                if(pbIsAir(s2)) wset(i3,i2,k1,LEAVES_BIRCH);
                            }
                        }
                    }
                }
                for(int j2=0;j2<i;++j2){
                    int s2=wget(posX,posY+j2,posZ);
                    if(pbIsAir(s2)||pbIsLeaves(s2)) wset(posX,posY+j2,posZ,LOG_BIRCH);
                }
                return true;
            }
            return false;
        }
        return false;
    }
    // ----- WorldGenSwamp (oak swamp tree; vines) -----
    static void wgAddVine(int x,int y,int z,int dir){
        wset(x,y,z,VINE_BASE+dir);
        int i=4;
        for(int yy=y-1; wIsAir(x,yy,z)&&i>0; --i){ wset(x,yy,z,VINE_BASE+dir); --yy; }
    }
    static boolean wgSwamp(Random rand,int posX,int posY,int posZ){
        int i=rand.nextInt(4)+5;
        while(wget(posX,posY-1,posZ)==WATER||wget(posX,posY-1,posZ)==FLOWING_WATER) --posY;
        boolean flag=true;
        if(posY>=1 && posY+i+1<=256){
            for(int j=posY;j<=posY+1+i;++j){
                int k=1; if(j==posY) k=0; if(j>=posY+1+i-2) k=3;
                for(int l=posX-k;l<=posX+k&&flag;++l) for(int i1=posZ-k;i1<=posZ+k&&flag;++i1){
                    if(j>=0&&j<256){
                        int c=wget(l,j,i1);
                        if(!pbIsAir(c)&&!pbIsLeaves(c)){
                            if(c!=WATER&&c!=FLOWING_WATER) flag=false;
                            else if(j>posY) flag=false;
                        }
                    } else flag=false;
                }
            }
            if(!flag) return false;
            int state=wget(posX,posY-1,posZ);
            if(pbCanSustainSapling(state) && posY<W_Y-i-1){
                wgOnPlantGrow(posX,posY-1,posZ);
                for(int k1=posY-3+i;k1<=posY+i;++k1){
                    int j2=k1-(posY+i); int l2=2-j2/2;
                    for(int j3=posX-l2;j3<=posX+l2;++j3){
                        int k3=j3-posX;
                        for(int i4=posZ-l2;i4<=posZ+l2;++i4){
                            int j1=i4-posZ;
                            if(Math.abs(k3)!=l2 || Math.abs(j1)!=l2 || rand.nextInt(2)!=0 && j2!=0){
                                if(pbCanBeReplacedByLeaves(wget(j3,k1,i4))) wset(j3,k1,i4,LEAVES_OAK);
                            }
                        }
                    }
                }
                for(int l1=0;l1<i;++l1){
                    int c=wget(posX,posY+l1,posZ);
                    if(pbIsAir(c)||pbIsLeaves(c)||c==FLOWING_WATER||c==WATER) wset(posX,posY+l1,posZ,LOG_OAK);
                }
                for(int i2=posY-3+i;i2<=posY+i;++i2){
                    int k2=i2-(posY+i); int i3=2-k2/2;
                    for(int l3=posX-i3;l3<=posX+i3;++l3) for(int j4=posZ-i3;j4<=posZ+i3;++j4){
                        if(pbIsLeaves(wget(l3,i2,j4))){
                            if(rand.nextInt(4)==0 && wIsAir(l3-1,i2,j4)) wgAddVine(l3-1,i2,j4,0);
                            if(rand.nextInt(4)==0 && wIsAir(l3+1,i2,j4)) wgAddVine(l3+1,i2,j4,1);
                            if(rand.nextInt(4)==0 && wIsAir(l3,i2,j4-1)) wgAddVine(l3,i2,j4-1,2);
                            if(rand.nextInt(4)==0 && wIsAir(l3,i2,j4+1)) wgAddVine(l3,i2,j4+1,3);
                        }
                    }
                }
                return true;
            }
            return false;
        }
        return false;
    }
    // ----- WorldGenTaiga1 (pine) -----
    static boolean wgTaiga1(Random rand,int posX,int posY,int posZ){
        int i=rand.nextInt(5)+7;
        int j=i-rand.nextInt(2)-3;
        int k=i-j;
        int l=1+rand.nextInt(k+1);
        if(posY>=1 && posY+i+1<=256){
            boolean flag=true;
            for(int i1=posY;i1<=posY+1+i&&flag;++i1){
                int j1; if(i1-posY<j) j1=0; else j1=l;
                for(int k1=posX-j1;k1<=posX+j1&&flag;++k1) for(int l1=posZ-j1;l1<=posZ+j1&&flag;++l1){
                    if(i1>=0&&i1<256){ if(!wgIsReplaceableTree(k1,i1,l1)) flag=false; } else flag=false;
                }
            }
            if(!flag) return false;
            int state=wget(posX,posY-1,posZ);
            if(pbCanSustainSapling(state) && posY<256-i-1){
                wgOnPlantGrow(posX,posY-1,posZ);
                int k2=0;
                for(int l2=posY+i;l2>=posY+j;--l2){
                    for(int j3=posX-k2;j3<=posX+k2;++j3){
                        int k3=j3-posX;
                        for(int i2=posZ-k2;i2<=posZ+k2;++i2){
                            int j2=i2-posZ;
                            if(Math.abs(k3)!=k2 || Math.abs(j2)!=k2 || k2<=0){
                                if(pbCanBeReplacedByLeaves(wget(j3,l2,i2))) wset(j3,l2,i2,LEAVES_SPRUCE);
                            }
                        }
                    }
                    if(k2>=1 && l2==posY+j+1) --k2; else if(k2<l) ++k2;
                }
                for(int i3=0;i3<i-1;++i3){
                    int c=wget(posX,posY+i3,posZ);
                    if(pbIsAir(c)||pbIsLeaves(c)) wset(posX,posY+i3,posZ,LOG_SPRUCE);
                }
                return true;
            }
            return false;
        }
        return false;
    }
    // ----- WorldGenTaiga2 (spruce) -----
    static boolean wgTaiga2(Random rand,int posX,int posY,int posZ){
        int i=rand.nextInt(4)+6;
        int j=1+rand.nextInt(2);
        int k=i-j;
        int l=2+rand.nextInt(2);
        boolean flag=true;
        if(posY>=1 && posY+i+1<=W_Y){
            for(int i1=posY;i1<=posY+1+i&&flag;++i1){
                int j1; if(i1-posY<j) j1=0; else j1=l;
                for(int k1=posX-j1;k1<=posX+j1&&flag;++k1) for(int l1=posZ-j1;l1<=posZ+j1&&flag;++l1){
                    if(i1>=0&&i1<W_Y){ int c=wget(k1,i1,l1); if(!pbIsAir(c)&&!pbIsLeaves(c)) flag=false; }
                    else flag=false;
                }
            }
            if(!flag) return false;
            int state=wget(posX,posY-1,posZ);
            if(pbCanSustainSapling(state) && posY<W_Y-i-1){
                wgOnPlantGrow(posX,posY-1,posZ);
                int i3=rand.nextInt(2); int j3=1, k3=0;
                for(int l3=0;l3<=k;++l3){
                    int j4=posY+i-l3;
                    for(int i2=posX-i3;i2<=posX+i3;++i2){
                        int j2=i2-posX;
                        for(int k2=posZ-i3;k2<=posZ+i3;++k2){
                            int l2=k2-posZ;
                            if(Math.abs(j2)!=i3 || Math.abs(l2)!=i3 || i3<=0){
                                if(pbCanBeReplacedByLeaves(wget(i2,j4,k2))) wset(i2,j4,k2,LEAVES_SPRUCE);
                            }
                        }
                    }
                    if(i3>=j3){ i3=k3; k3=1; ++j3; if(j3>l) j3=l; } else ++i3;
                }
                int i4=rand.nextInt(3);
                for(int k4=0;k4<i-i4;++k4){
                    int c=wget(posX,posY+k4,posZ);
                    if(pbIsAir(c)||pbIsLeaves(c)) wset(posX,posY+k4,posZ,LOG_SPRUCE);
                }
                return true;
            }
            return false;
        }
        return false;
    }
    // ----- WorldGenBigTree (big oak; own Random; persistent heightLimit) -----
    static int btGreatest(int x,int y,int z){ int i=Math.abs(x),j=Math.abs(y),k=Math.abs(z); return k>i&&k>j?k:(j>i?j:i); }
    static float btLayerSize(int hl,int y){
        if((float)y<(float)hl*0.3F) return -1.0F;
        float f=(float)hl/2.0F, f1=f-(float)y;
        float f2=(float)Math.sqrt((double)(f*f-f1*f1));
        if(f1==0.0F) f2=f; else if(Math.abs(f1)>=f) return 0.0F;
        return f2*0.5F;
    }
    static float btLeafSize(int ldl,int y){ return (y>=0&&y<ldl)?(y!=0&&y!=ldl-1?3.0F:2.0F):-1.0F; }
    static int btCheckLine(int x0,int y0,int z0,int x1,int y1,int z1){
        int bx=x1-x0,by=y1-y0,bz=z1-z0; int i=btGreatest(bx,by,bz); if(i==0) return -1;
        float f=(float)bx/(float)i, f1=(float)by/(float)i, f2=(float)bz/(float)i;
        for(int j=0;j<=i;++j){
            int px=x0+mhfloor((double)(0.5F+(float)j*f));
            int py=y0+mhfloor((double)(0.5F+(float)j*f1));
            int pz=z0+mhfloor((double)(0.5F+(float)j*f2));
            if(!wgIsReplaceableTree(px,py,pz)) return j;
        }
        return -1;
    }
    static void btCrosSection(int x,int y,int z,float sz,int leaf){
        int i=(int)((double)sz+0.618D);
        for(int j=-i;j<=i;++j) for(int k=-i;k<=i;++k){
            if(Math.pow((double)Math.abs(j)+0.5D,2.0D)+Math.pow((double)Math.abs(k)+0.5D,2.0D)<=(double)(sz*sz)){
                int c=wget(x+j,y,z+k);
                if(pbIsAir(c)||pbIsLeaves(c)) wset(x+j,y,z+k,leaf);
            }
        }
    }
    static int btLogAxis(int x0,int z0,int x1,int z1){
        int i=Math.abs(x1-x0), j=Math.abs(z1-z0), k=Math.max(i,j);
        if(k>0){ if(i==k) return LOG_OAK_X; if(j==k) return LOG_OAK_Z; }
        return LOG_OAK;
    }
    static void btLimb(int x0,int y0,int z0,int x1,int y1,int z1){
        int bx=x1-x0,by=y1-y0,bz=z1-z0; int i=btGreatest(bx,by,bz);
        float f=(float)bx/(float)i, f1=(float)by/(float)i, f2=(float)bz/(float)i;
        for(int j=0;j<=i;++j){
            int px=x0+mhfloor((double)(0.5F+(float)j*f));
            int py=y0+mhfloor((double)(0.5F+(float)j*f1));
            int pz=z0+mhfloor((double)(0.5F+(float)j*f2));
            wset(px,py,pz, btLogAxis(x0,z0,px,pz));
        }
    }
    static int[] btFolX=new int[4096], btFolY=new int[4096], btFolZ=new int[4096], btFolB=new int[4096];
    static boolean wgBigTree(Random mainr,int posX,int posY,int posZ){
        int ldl=5;
        Random rr=new Random(mainr.nextLong());
        if(bigtreeHeightLimit==0) bigtreeHeightLimit=5+rr.nextInt(12);
        int hl=bigtreeHeightLimit;
        int soil=wget(posX,posY-1,posZ);
        if(!pbCanSustainSapling(soil)) return false;
        int chk=btCheckLine(posX,posY,posZ,posX,posY+hl-1,posZ);
        if(chk==-1){} else if(chk<6) return false; else hl=chk;
        bigtreeHeightLimit=hl;
        int height=(int)((double)hl*0.618D); if(height>=hl) height=hl-1;
        int ii=(int)(1.382D+Math.pow((double)hl/13.0D,2.0D)); if(ii<1) ii=1;
        int jj=posY+height; int kk=hl-ldl; int nf=0;
        btFolX[nf]=posX; btFolY[nf]=posY+kk; btFolZ[nf]=posZ; btFolB[nf]=jj; ++nf;
        for(;kk>=0;--kk){
            float f=btLayerSize(hl,kk);
            if(f>=0.0F){
                for(int l=0;l<ii;++l){
                    double d0=1.0D*(double)f*((double)rr.nextFloat()+0.328D);
                    double d1=(double)(rr.nextFloat()*2.0F)*Math.PI;
                    double d2=d0*Math.sin(d1)+0.5D;
                    double d3=d0*Math.cos(d1)+0.5D;
                    int bx=posX+mhfloor(d2), by=posY+(kk-1), bz=posZ+mhfloor(d3);
                    if(btCheckLine(bx,by,bz,bx,by+ldl,bz)==-1){
                        int i1=posX-bx, j1=posZ-bz;
                        double d4=(double)by-Math.sqrt((double)(i1*i1+j1*j1))*0.381D;
                        int k1=d4>(double)jj?jj:(int)d4;
                        if(btCheckLine(posX,k1,posZ,bx,by,bz)==-1 && nf<4096){
                            btFolX[nf]=bx; btFolY[nf]=by; btFolZ[nf]=bz; btFolB[nf]=k1; ++nf;
                        }
                    }
                }
            }
        }
        for(int n=0;n<nf;++n) for(int i=0;i<ldl;++i)
            btCrosSection(btFolX[n],btFolY[n]+i,btFolZ[n],btLeafSize(ldl,i),LEAVES_OAK);
        btLimb(posX,posY,posZ,posX,posY+height,posZ);
        for(int n=0;n<nf;++n){
            int i=btFolB[n];
            if(!(posX==btFolX[n]&&i==btFolY[n]&&posZ==btFolZ[n]) && (double)(i-posY)>=(double)hl*0.2D)
                btLimb(posX,i,posZ,btFolX[n],btFolY[n],btFolZ[n]);
        }
        return true;
    }
    // ----- small plants -----
    static boolean pbCanSustainBushPos(int x,int y,int z){ return pbCanSustainBush(wget(x,y-1,z)); }
    static void wgTallGrass(Random rand,int x,int y,int z,int state){
        while((pbIsAir(wget(x,y,z))||pbIsLeaves(wget(x,y,z)))&&y>0) --y;
        for(int i=0;i<128;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&pbCanSustainBushPos(bx,by,bz)) wset(bx,by,bz,state);
        }
    }
    static void wgFlowers(Random rand,int x,int y,int z,int state){
        for(int i=0;i<64;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&pbCanSustainBushPos(bx,by,bz)) wset(bx,by,bz,state);
        }
    }
    static boolean pbDeadbushOk(int s){ return pbCanSustainBush(s)||s==SAND||s==HARDENED_CLAY||s==STAINED_HARDENED_CLAY; }
    static void wgDeadbush(Random rand,int x,int y,int z){
        while((pbIsAir(wget(x,y,z))||pbIsLeaves(wget(x,y,z)))&&y>0) --y;
        for(int i=0;i<4;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&pbDeadbushOk(wget(bx,by-1,bz))) wset(bx,by,bz,DEADBUSH);
        }
    }
    static void wgWaterlily(Random rand,int x,int y,int z){
        for(int i=0;i<10;++i){
            int j=x+rand.nextInt(8)-rand.nextInt(8), kk=y+rand.nextInt(4)-rand.nextInt(4), l=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(j,kk,l)){ int below=wget(j,kk-1,l); if(below==WATER||below==ICE) wset(j,kk,l,WATER_LILY); }
        }
    }
    static boolean wgMushroomCanStay(int x,int y,int z){
        if(y<0||y>=256) return false;
        int below=wget(x,y-1,z);
        if(below==MYCELIUM) return true;
        if(below==PODZOL) return true;
        return wLight(x,y,z)<13 && pbBlocksMovement(below);
    }
    static void wgBush(Random rand,int x,int y,int z,int block){
        for(int i=0;i<64;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&wgMushroomCanStay(bx,by,bz)) wset(bx,by,bz,block);
        }
    }
    static boolean wgReedCanStay(int x,int y,int z){
        int below=wget(x,y-1,z);
        if(below==REEDS) return true;
        if(below!=GRASS && !pbIsDirt(below) && below!=SAND) return false;
        if(pbIsWater(wget(x-1,y-1,z))||pbIsWater(wget(x+1,y-1,z))||pbIsWater(wget(x,y-1,z-1))||pbIsWater(wget(x,y-1,z+1))) return true;
        return false;
    }
    static void wgReed(Random rand,int x,int y,int z){
        for(int i=0;i<20;++i){
            int bx=x+rand.nextInt(4)-rand.nextInt(4), by=y, bz=z+rand.nextInt(4)-rand.nextInt(4);
            if(wIsAir(bx,by,bz)){
                int bdy=by-1;
                if(pbIsWater(wget(bx-1,bdy,bz))||pbIsWater(wget(bx+1,bdy,bz))||pbIsWater(wget(bx,bdy,bz-1))||pbIsWater(wget(bx,bdy,bz+1))){
                    int jj=2+rand.nextInt(rand.nextInt(3)+1);
                    for(int kk=0;kk<jj;++kk) if(wgReedCanStay(bx,by,bz)) wset(bx,by+kk,bz,REEDS);
                }
            }
        }
    }
    static void wgPumpkin(Random rand,int x,int y,int z){
        for(int i=0;i<64;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&wget(bx,by-1,bz)==GRASS){
                int faceSel=rand.nextInt(4);
                wset(bx,by,bz,PUMPKIN_BASE+faceSel);
            }
        }
    }
    static void wgLiquids(int x,int y,int z,int block){
        if(wget(x,y+1,z)!=STONE) return;
        if(wget(x,y-1,z)!=STONE) return;
        int here=wget(x,y,z); if(!pbIsAir(here)&&here!=STONE) return;
        int i=0;
        if(wget(x-1,y,z)==STONE) ++i; if(wget(x+1,y,z)==STONE) ++i;
        if(wget(x,y,z-1)==STONE) ++i; if(wget(x,y,z+1)==STONE) ++i;
        int j=0;
        if(wIsAir(x-1,y,z)) ++j; if(wIsAir(x+1,y,z)) ++j;
        if(wIsAir(x,y,z-1)) ++j; if(wIsAir(x,y,z+1)) ++j;
        if(i==3 && j==1) wset(x,y,z,block);   // immediateBlockTick: no-op
    }
    static boolean wgDplantCanPlace(int x,int y,int z){
        if(!pbCanSustainBush(wget(x,y-1,z))) return false;
        if(!wIsAir(x,y+1,z)) return false;
        return true;
    }
    static boolean wgDoublePlant(Random rand,int x,int y,int z,int type){
        boolean flag=false;
        for(int i=0;i<64;++i){
            int bx=x+rand.nextInt(8)-rand.nextInt(8), by=y+rand.nextInt(4)-rand.nextInt(4), bz=z+rand.nextInt(8)-rand.nextInt(8);
            if(wIsAir(bx,by,bz)&&wgDplantCanPlace(bx,by,bz)){
                wset(bx,by,bz,DPLANT_LOWER_BASE+type);
                wset(bx,by+1,bz,DPLANT_UPPER);
                flag=true;
            }
        }
        return flag;
    }
    static void wgDungeons(Random rand,int posX,int posY,int posZ){
        int j=rand.nextInt(2)+2; int k=-j-1, l=j+1;
        int k1=rand.nextInt(2)+2; int l1=-k1-1, i2=k1+1; int j2=0;
        for(int k2=k;k2<=l;++k2) for(int l2=-1;l2<=4;++l2) for(int i3=l1;i3<=i2;++i3){
            boolean flag=pbIsSolid(wget(posX+k2,posY+l2,posZ+i3));
            if(l2==-1 && !flag) return;
            if(l2==4 && !flag) return;
            if((k2==k||k2==l||i3==l1||i3==i2)&&l2==0&&wIsAir(posX+k2,posY+l2,posZ+i3)&&wIsAir(posX+k2,posY+l2+1,posZ+i3)) ++j2;
        }
        if(j2>=1 && j2<=5){
            for(int k3=k;k3<=l;++k3) for(int i4=3;i4>=-1;--i4) for(int k4=l1;k4<=i2;++k4){
                int bx=posX+k3, by=posY+i4, bz=posZ+k4;
                if(k3!=k && i4!=-1 && k4!=l1 && k3!=l && i4!=4 && k4!=i2){
                    if(wget(bx,by,bz)!=CHEST) wset(bx,by,bz,AIR);
                } else if(by>=0 && !pbIsSolid(wget(bx,by-1,bz))){
                    wset(bx,by,bz,AIR);
                } else if(pbIsSolid(wget(bx,by,bz)) && wget(bx,by,bz)!=CHEST){
                    if(i4==-1 && rand.nextInt(4)!=0) wset(bx,by,bz,MOSSY_COBBLESTONE);
                    else wset(bx,by,bz,COBBLESTONE);
                }
            }
            for(int l3=0;l3<2;++l3) for(int j4=0;j4<3;++j4){
                int l4=posX+rand.nextInt(j*2+1)-j, i5=posY, j5=posZ+rand.nextInt(k1*2+1)-k1;
                if(wIsAir(l4,i5,j5)){
                    int j3=0;
                    if(pbIsSolid(wget(l4+1,i5,j5))) ++j3; if(pbIsSolid(wget(l4-1,i5,j5))) ++j3;
                    if(pbIsSolid(wget(l4,i5,j5+1))) ++j3; if(pbIsSolid(wget(l4,i5,j5-1))) ++j3;
                    if(j3==1){ wset(l4,i5,j5,CHEST); rand.nextLong(); break; }
                }
            }
            wset(posX,posY,posZ,MOB_SPAWNER);
            rand.nextInt(400);
        }
    }
    static boolean lakeSolidW(int c){ return !pbIsAir(c)&&!pbIsLiquid(c); }
    static boolean wgLakes(Random rand,int posX,int posY,int posZ,int liquid){
        int px=posX-8, py=posY, pz=posZ-8;
        for(; py>5 && wIsAir(px,py,pz); --py);
        if(py<=4) return false;
        py-=4;
        boolean[] ab=new boolean[2048];
        int i=rand.nextInt(4)+4;
        for(int jj=0;jj<i;++jj){
            double d0=rand.nextDouble()*6.0D+3.0D;
            double d1=rand.nextDouble()*4.0D+2.0D;
            double d2=rand.nextDouble()*6.0D+3.0D;
            double d3=rand.nextDouble()*(16.0D-d0-2.0D)+1.0D+d0/2.0D;
            double d4=rand.nextDouble()*(8.0D-d1-4.0D)+2.0D+d1/2.0D;
            double d5=rand.nextDouble()*(16.0D-d2-2.0D)+1.0D+d2/2.0D;
            for(int l=1;l<15;++l) for(int i1=1;i1<15;++i1) for(int j1=1;j1<7;++j1){
                double d6=((double)l-d3)/(d0/2.0D);
                double d7=((double)j1-d4)/(d1/2.0D);
                double d8=((double)i1-d5)/(d2/2.0D);
                if(d6*d6+d7*d7+d8*d8<1.0D) ab[(l*16+i1)*8+j1]=true;
            }
        }
        for(int k1=0;k1<16;++k1) for(int l2=0;l2<16;++l2) for(int kk=0;kk<8;++kk){
            boolean flag = !ab[(k1*16+l2)*8+kk] &&
                (k1<15&&ab[((k1+1)*16+l2)*8+kk] || k1>0&&ab[((k1-1)*16+l2)*8+kk] ||
                 l2<15&&ab[(k1*16+l2+1)*8+kk] || l2>0&&ab[(k1*16+(l2-1))*8+kk] ||
                 kk<7&&ab[(k1*16+l2)*8+kk+1] || kk>0&&ab[(k1*16+l2)*8+(kk-1)]);
            if(flag){
                int state=wget(px+k1,py+kk,pz+l2);
                if(kk>=4 && pbIsLiquid(state)) return false;
                if(kk<4 && !lakeSolidW(state) && state!=liquid) return false;
            }
        }
        for(int l1=0;l1<16;++l1) for(int i3=0;i3<16;++i3) for(int i4=0;i4<8;++i4)
            if(ab[(l1*16+i3)*8+i4]) wset(px+l1,py+i4,pz+i3, i4>=4?AIR:liquid);
        for(int i2=0;i2<16;++i2) for(int j3=0;j3<16;++j3) for(int j4=4;j4<8;++j4)
            if(ab[(i2*16+j3)*8+j4]){
                int bx=px+i2, by=py+(j4-1), bz=pz+j3;
                if(wget(bx,by,bz)==DIRT && wLight(px+i2,py+j4,pz+j3)>0) wset(bx,by,bz,GRASS);
            }
        if(liquid==LAVA||liquid==FLOWING_LAVA){
            for(int j2=0;j2<16;++j2) for(int k3=0;k3<16;++k3) for(int k4=0;k4<8;++k4){
                boolean flag1 = !ab[(j2*16+k3)*8+k4] &&
                    (j2<15&&ab[((j2+1)*16+k3)*8+k4] || j2>0&&ab[((j2-1)*16+k3)*8+k4] ||
                     k3<15&&ab[(j2*16+k3+1)*8+k4] || k3>0&&ab[(j2*16+(k3-1))*8+k4] ||
                     k4<7&&ab[(j2*16+k3)*8+k4+1] || k4>0&&ab[(j2*16+k3)*8+(k4-1)]);
                if(flag1 && (k4<4 || rand.nextInt(2)!=0) && lakeSolidW(wget(px+j2,py+k4,pz+k3)))
                    wset(px+j2,py+k4,pz+k3,STONE);
            }
        }
        return true;
    }

    // ----- BiomeDecorator.generateOres + genStandardOre1/2 (Factory defaults) -----
    static void genStandardOre1(Random r,int count,int size,int minH,int maxH,int ore){
        if(maxH<minH){ int t=minH; minH=maxH; maxH=t; }
        else if(maxH==minH){ if(minH<255) ++maxH; else --minH; }
        for(int j=0;j<count;++j){
            int bx=r.nextInt(16), by=r.nextInt(maxH-minH)+minH, bz=r.nextInt(16);
            wgMinable(r,bx,by,bz,size,ore);
        }
    }
    static void genStandardOre2(Random r,int count,int size,int center,int spread,int ore){
        for(int i=0;i<count;++i){
            int bx=r.nextInt(16), by=r.nextInt(spread)+r.nextInt(spread)+center-spread, bz=r.nextInt(16);
            wgMinable(r,bx,by,bz,size,ore);
        }
    }
    static void generateOres(Random r){
        genStandardOre1(r,10,33,0,256,DIRT);
        genStandardOre1(r,8,33,0,256,GRAVEL);
        genStandardOre1(r,10,33,0,80,DIORITE);
        genStandardOre1(r,10,33,0,80,GRANITE);
        genStandardOre1(r,10,33,0,80,ANDESITE);
        genStandardOre1(r,20,17,0,128,COAL_ORE);
        genStandardOre1(r,20,9,0,64,IRON_ORE);
        genStandardOre1(r,2,9,0,32,GOLD_ORE);
        genStandardOre1(r,8,8,0,16,REDSTONE_ORE);
        genStandardOre1(r,1,8,0,16,DIAMOND_ORE);
        genStandardOre2(r,1,7,16,16,LAPIS_ORE);
    }

    static void bdGenTree(Random r,int biome,int px,int py,int pz){
        if(biome==6){ wgSwamp(r,px,py,pz); }
        else if(biome==133){ if(r.nextInt(3)==0) wgTaiga1(r,px,py,pz); else wgTaiga2(r,px,py,pz); }
        else { int a=r.nextInt(5);
            if(a!=0){ int b=r.nextInt(10); if(b==0) wgBigTree(r,px,py,pz); else wgTrees(r,px,py,pz,LOG_OAK,LEAVES_OAK); }
            else wgBirch(r,px,py,pz);
        }
    }
    static int bdGrassState(Random r,int biome){
        if(biome==133) return r.nextInt(5)>0?FERN:TALLGRASS;
        return TALLGRASS;
    }
    static int bdFlowerState(Random r,int biome){
        if(biome==6) return RED_FLOWER_BASE+1;
        int v=r.nextInt(3); return v>0?YELLOW_FLOWER:(RED_FLOWER_BASE+0);
    }

    static void bdGenDecorations(Random r,int biome,int treesPerChunk,int flowersPerChunk,int grassPerChunk,
            int deadBushPerChunk,int mushroomsPerChunk,int reedsPerChunk,int sandPerChunk,int sandPerChunk2,
            int clayPerChunk,int waterlilyPerChunk){
        generateOres(r);
        for(int i=0;i<sandPerChunk2;++i){ int j=r.nextInt(16)+8, k=r.nextInt(16)+8; wgSand(r,j,wTopSolidOrLiquid(j,k),k,7,SAND); }
        for(int i=0;i<clayPerChunk;++i){ int j=r.nextInt(16)+8, k=r.nextInt(16)+8; wgClay(r,j,wTopSolidOrLiquid(j,k),k,4); }
        for(int i=0;i<sandPerChunk;++i){ int j=r.nextInt(16)+8, k=r.nextInt(16)+8; wgSand(r,j,wTopSolidOrLiquid(j,k),k,6,GRAVEL); }
        {
            int k1=treesPerChunk;
            if(r.nextFloat()<0.1F) ++k1;
            for(int j2=0;j2<k1;++j2){
                int k6=r.nextInt(16)+8, l=r.nextInt(16)+8;
                int py=wHeight(k6,l);
                bdGenTree(r,biome,k6,py,l);
            }
        }
        for(int l2=0;l2<flowersPerChunk;++l2){
            int i7=r.nextInt(16)+8, l10=r.nextInt(16)+8;
            int j14=wHeight(i7,l10)+32;
            if(j14>0){ int k17=r.nextInt(j14); int st=bdFlowerState(r,biome); wgFlowers(r,i7,k17,l10,st); }
        }
        for(int i3=0;i3<grassPerChunk;++i3){
            int j7=r.nextInt(16)+8, i11=r.nextInt(16)+8;
            int k14=wHeight(j7,i11)*2;
            if(k14>0){ int l17=r.nextInt(k14); int st=bdGrassState(r,biome); wgTallGrass(r,j7,l17,i11,st); }
        }
        for(int j3=0;j3<deadBushPerChunk;++j3){
            int k7=r.nextInt(16)+8, j11=r.nextInt(16)+8;
            int l14=wHeight(k7,j11)*2;
            if(l14>0){ int i18=r.nextInt(l14); wgDeadbush(r,k7,i18,j11); }
        }
        for(int k3=0;k3<waterlilyPerChunk;++k3){
            int l7=r.nextInt(16)+8, k11=r.nextInt(16)+8;
            int i15=wHeight(l7,k11)*2;
            if(i15>0){ int j18=r.nextInt(i15); int by=j18; for(;by>0;--by) if(!wIsAir(l7,by-1,k11)) break; wgWaterlily(r,l7,by,k11); }
        }
        for(int l3=0;l3<mushroomsPerChunk;++l3){
            if(r.nextInt(4)==0){ int i8=r.nextInt(16)+8, l11=r.nextInt(16)+8; wgBush(r,i8,wHeight(i8,l11),l11,BROWN_MUSHROOM); }
            if(r.nextInt(8)==0){ int j8=r.nextInt(16)+8, i12=r.nextInt(16)+8; int j15=wHeight(j8,i12)*2;
                if(j15>0){ int k18=r.nextInt(j15); wgBush(r,j8,k18,i12,RED_MUSHROOM); } }
        }
        if(r.nextInt(4)==0){ int i4=r.nextInt(16)+8, k8=r.nextInt(16)+8; int j12=wHeight(i4,k8)*2;
            if(j12>0){ int k15=r.nextInt(j12); wgBush(r,i4,k15,k8,BROWN_MUSHROOM); } }
        if(r.nextInt(8)==0){ int j4=r.nextInt(16)+8, l8=r.nextInt(16)+8; int k12=wHeight(j4,l8)*2;
            if(k12>0){ int l15=r.nextInt(k12); wgBush(r,j4,l15,l8,RED_MUSHROOM); } }
        for(int k4=0;k4<reedsPerChunk;++k4){
            int i9=r.nextInt(16)+8, l12=r.nextInt(16)+8; int i16=wHeight(i9,l12)*2;
            if(i16>0){ int l18=r.nextInt(i16); wgReed(r,i9,l18,l12); }
        }
        for(int l4=0;l4<10;++l4){
            int j9=r.nextInt(16)+8, i13=r.nextInt(16)+8; int j16=wHeight(j9,i13)*2;
            if(j16>0){ int i19=r.nextInt(j16); wgReed(r,j9,i19,i13); }
        }
        if(r.nextInt(32)==0){ int i5=r.nextInt(16)+8, k9=r.nextInt(16)+8; int j13=wHeight(i5,k9)*2;
            if(j13>0){ int k16=r.nextInt(j13); wgPumpkin(r,i5,k16,k9); } }
        for(int k5=0;k5<50;++k5){
            int i10=r.nextInt(16)+8, l13=r.nextInt(16)+8, i17=r.nextInt(248)+8;
            if(i17>0){ int k19=r.nextInt(i17); wgLiquids(i10,k19,l13,FLOWING_WATER); }
        }
        for(int l5=0;l5<20;++l5){
            int j10=r.nextInt(16)+8, i14=r.nextInt(16)+8;
            int j17=r.nextInt(r.nextInt(r.nextInt(240)+8)+8);
            wgLiquids(j10,j17,i14,FLOWING_LAVA);
        }
    }

    static void forestAddDoublePlants(Random r,int count){
        for(int i=0;i<count;++i){
            int j=r.nextInt(3);
            int type=(j==0)?1:(j==1?4:5);
            for(int k=0;k<5;++k){
                int l=r.nextInt(16)+8, i1=r.nextInt(16)+8;
                int j1=r.nextInt(wHeight(l,i1)+32);
                if(wgDoublePlant(r,l,j1,i1,type)) break;
            }
        }
    }
    static void biomeDecorate(Random r,int biome){
        if(biome==4){
            int i=r.nextInt(5)-3;
            forestAddDoublePlants(r,i);
            bdGenDecorations(r,biome,10,2,2,0,0,0,1,3,1,0);
        } else if(biome==6){
            bdGenDecorations(r,biome,2,1,5,1,8,10,0,0,1,4);
            if(r.nextInt(64)==0){ /* WorldGenFossils (separate Random + .nbt). Must NOT fire for verified seeds. */ }
        } else {
            for(int i1=0;i1<7;++i1){
                int j1=r.nextInt(16)+8, k1=r.nextInt(16)+8;
                int l1=r.nextInt(wHeight(j1,k1)+32);
                wgDoublePlant(r,j1,l1,k1,3);
            }
            bdGenDecorations(r,biome,10,2,1,0,1,0,1,3,1,0);
        }
    }

    static void populate(long seed){
        int biome=wGetBiome(16,16);
        Random rand=new Random();
        rand.setSeed(seed);
        long k=rand.nextLong()/2L*2L+1L;
        long l=rand.nextLong()/2L*2L+1L;
        rand.setSeed((long)0*k+(long)0*l ^ seed);
        if(biome!=2 && biome!=17 && rand.nextInt(4)==0){
            int i1=rand.nextInt(16)+8, j1=rand.nextInt(256), k1=rand.nextInt(16)+8;
            wgLakes(rand,i1,j1,k1,WATER);
        }
        if(rand.nextInt(8)==0){
            int i2=rand.nextInt(16)+8, l2=rand.nextInt(rand.nextInt(248)+8), k3=rand.nextInt(16)+8;
            if(l2<SEA_LEVEL || rand.nextInt(10)==0) wgLakes(rand,i2,l2,k3,LAVA);
        }
        for(int j2=0;j2<8;++j2){
            int i3=rand.nextInt(16)+8, l3=rand.nextInt(256), l1=rand.nextInt(16)+8;
            wgDungeons(rand,i3,l3,l1);
        }
        biomeDecorate(rand,biome);
    }

    // provideChunk(cx,cz) MINUS structures/Chunk/skylight, into the per-chunk primer (verbatim path).
    static void provideChunk(int chunkX,int chunkZ){
        Random thisRand=new Random();
        thisRand.setSeed((long)chunkX*341873128712L + (long)chunkZ*132897987541L);
        primer=new ChunkPrimer();
        biomesForGeneration=genBiomesG.getInts(chunkX*4-2, chunkZ*4-2, 10, 10);
        generateHeightmap(chunkX*4, 0, chunkZ*4);
        setBlocksInChunk();
        fullBiome=biomeIndexLayerG.getInts(chunkX*16, chunkZ*16, 16, 16);
        for(int b=0;b<256;++b){ curTop[b]=BP.defTop(b); curFiller[b]=BP.defFiller(b); }
        depthBuffer=surfaceNoiseG.getRegion(depthBuffer, (double)(chunkX*16), (double)(chunkZ*16), 16, 16, 0.0625D, 0.0625D, 1.0D);
        for(int i=0;i<16;++i) for(int j=0;j<16;++j){
            int biome=fullBiome[j+i*16];
            genTerrainBlocks(biome, thisRand, primer, chunkX*16+i, chunkZ*16+j, depthBuffer[j+i*16]);
        }
        caveGenerate(chunkX, chunkZ);
        ravineGenerate(chunkX, chunkZ);
    }

    // ===== main: build the 2x2-chunk world via provideChunk, populate(0,0), dump the world. =====
    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        worldSeed = seed;

        GenLayer[] agenlayer = initializeAllBiomeGenerators(seed);
        genBiomesG = agenlayer[0];
        biomeIndexLayerG = agenlayer[1];

        Random rand = new Random(seed);
        minLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        maxLimitPerlinNoise = new NoiseGeneratorOctaves(rand, 16);
        mainPerlinNoise = new NoiseGeneratorOctaves(rand, 8);
        surfaceNoiseG = new NoiseGeneratorPerlin(rand, 4);
        NoiseGeneratorOctaves scaleNoise = new NoiseGeneratorOctaves(rand, 10);
        depthNoise = new NoiseGeneratorOctaves(rand, 16);
        GRASS_COLOR_NOISE = new NoiseGeneratorPerlin(new Random(2345L), 1);

        world = new char[W_N];
        bigtreeHeightLimit = 0;

        // full-res voronoi biome over [0,32)^2 (idx x*32+z).
        int[] fb = biomeIndexLayerG.getInts(0, 0, W_X, W_Z);
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z)
                wbiome[x * W_Z + z] = fb[z + x * W_Z];

        // provide chunks (0,0),(1,0),(0,1),(1,1) into the world.
        for (int cx = 0; cx < 2; ++cx) {
            for (int cz = 0; cz < 2; ++cz) {
                provideChunk(cx, cz);
                for (int lx = 0; lx < 16; ++lx)
                    for (int lz = 0; lz < 16; ++lz)
                        for (int y = 0; y < 256; ++y)
                            wset(cx * 16 + lx, y, cz * 16 + lz, primer.getBlockState(lx, y, lz));
            }
        }

        populate(seed);

        StringBuilder sb = new StringBuilder();
        for (int idx = 0; idx < W_N; ++idx) sb.append(String.format("%04x%n", (int)world[idx]));
        System.out.print(sb);
    }

}
