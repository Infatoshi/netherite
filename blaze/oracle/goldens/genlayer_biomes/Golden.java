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

    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        GenLayer[] agenlayer = initializeAllBiomeGenerators(seed);
        GenLayer biomeIndexLayer = agenlayer[1];
        IntCache.resetIntCache();
        int[] out = biomeIndexLayer.getInts(0, 0, 16, 16);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 16 * 16; ++i) {
            sb.append(String.format("%08x%n", out[i]));
        }
        System.out.print(sb);
    }
}
