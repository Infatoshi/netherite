// Verbatim MC 1.11.2 WorldEntitySpawner hostile spawn DECISION math (vanilla ground truth).
// Eval-pure: no game launch. Sources inlined from decompiled oracle:
//   net/minecraft/world/WorldEntitySpawner.java
//     findChunksForSpawning eligible ring, getRandomChunkPosition, pack walk, range gates,
//     canCreatureTypeSpawnAtLocation ON_GROUND, isValidEmptySpawnBlock
//   net/minecraft/util/math/MathHelper.java  floor/ceil/roundUp
//   net/minecraft/util/WeightedRandom.java   getRandomItem
//   net/minecraft/entity/monster/EntityMob.java  isValidLightLevel (no thunder)
//   net/minecraft/world/biome/Biome.java     default spawnableMonsterList weights
//   java.util.Random nextInt/nextDouble (JavaRandom stream)
//
// Synthetic packed block/light columns (identical pure fn to core/mob_spawning_oracle.h).
// CUT: Collections.shuffle (sorted cx,cz), PlayerChunkMap/world border, Forge, entity alloc,
// Math.random unseeded pack size -> nextDouble on the same seeded Random.
// Output: %016x lines packing (chunkX, chunkZ, spawnY, entityTypeId, success).

public class Golden {
    static final int MOB_COUNT_DIV = 289;
    static final int MONSTER_CAP = 70;
    static final int CHUNK_RANGE = 8;
    static final int MAX_ELIGIBLE = 225;
    static final int N_PLAYERS = 6;
    static final int N_SEEDS = 8;
    static final int CHUNKS_PER_SEED = 4;
    static final int GROUPS = 3;
    static final int N_MONSTER = 8;
    static final int TOTAL_WEIGHT = 515;
    static final int[] WEIGHTS = {100, 95, 5, 100, 100, 100, 10, 5};

    static final int RES_SPAWN = 0;
    static final int RES_FAIL_INIT_SOLID = 1;
    static final int RES_FAIL_PLAYER = 2;
    static final int RES_FAIL_SPAWN_PT = 3;
    static final int RES_FAIL_PLACE = 4;
    static final int RES_FAIL_LIGHT = 5;
    static final int RES_FAIL_OOB = 6;
    static final int RES_FAIL_PACK0 = 7;
    static final int RES_ELIGIBLE_COUNT = 0xE0;
    static final int RES_ELIGIBLE_CHUNK = 0xE1;
    static final int RES_CAP = 0xE2;
    static final int TYPE_NONE = 0xFF;

    static final int WORLD_SPAWN_X = 0, WORLD_SPAWN_Y = 200, WORLD_SPAWN_Z = 0;
    static final int FLOOR_Y = 60;

    // Block ids (mc_blocks)
    static final int AIR = 0, STONE = 1, GRASS = 2, BEDROCK = 7, TORCH = 50;

    // ---- MathHelper ----
    static int floor(double value) {
        int i = (int) value;
        return value < (double) i ? i - 1 : i;
    }

    static int ceil(double value) {
        int i = (int) value;
        return value > (double) i ? i + 1 : i;
    }

    static int roundUp(int number, int interval) {
        if (interval == 0) return 0;
        if (number == 0) return interval;
        if (number < 0) interval = -interval;
        int i = number % interval;
        return i == 0 ? number : number + interval - i;
    }

    // ---- light_opacity for synthetic height/sky (matches block_props_table for used ids) ----
    static int lightOpacity(int id) {
        if (id == AIR || id == TORCH) return 0;
        return 255; // stone/grass/bedrock
    }

    static boolean isNormalCube(int id) {
        return id == STONE || id == GRASS || id == BEDROCK;
    }

    static boolean isLiquid(int id) {
        return id == 8 || id == 9 || id == 10 || id == 11;
    }

    static int blockId(int x, int y, int z) {
        if (y < 0 || y >= 256) return AIR;
        if (y == 0) return BEDROCK;
        if (y < FLOOR_Y) return STONE;
        if (y == FLOOR_Y) return GRASS;
        int lx = x % 32; if (lx < 0) lx += 32;
        int lz = z % 32; if (lz < 0) lz += 32;
        if (y >= 64 && y <= 66 && lx >= 8 && lx <= 14 && lz >= 8 && lz <= 14)
            return STONE;
        if (y == FLOOR_Y + 1) {
            if ((x == 2 && z == 2) || (x == 13 && z == 13)) return TORCH;
        }
        return AIR;
    }

    static int heightAt(int x, int z) {
        for (int y = 255; y >= 0; --y) {
            if (lightOpacity(blockId(x, y, z)) != 0) return y + 1;
        }
        return 0;
    }

    static int skyLight(int x, int y, int z) {
        for (int yy = y + 1; yy < 256; ++yy) {
            if (lightOpacity(blockId(x, yy, z)) != 0) return 0;
        }
        return 15;
    }

    static int blockLight(int x, int y, int z) {
        int[] txs = {2, 13}, tzs = {2, 13};
        int best = 0;
        for (int t = 0; t < 2; ++t) {
            int dx = x - txs[t]; if (dx < 0) dx = -dx;
            int dy = y - (FLOOR_Y + 1); if (dy < 0) dy = -dy;
            int dz = z - tzs[t]; if (dz < 0) dz = -dz;
            int r = dx; if (dz > r) r = dz; if (dy > r) r = dy;
            if (r >= 14) continue;
            int lv = 14 - r;
            if (lv > best) best = lv;
        }
        return best;
    }

    static int combinedLight(int x, int y, int z) {
        int sky = skyLight(x, y, z);
        int bl = blockLight(x, y, z);
        return sky > bl ? sky : bl;
    }

    static boolean validEmpty(int id) {
        if (isNormalCube(id)) return false;
        if (isLiquid(id)) return false;
        return true;
    }

    static boolean canPlace(int x, int y, int z) {
        int below = blockId(x, y - 1, z);
        if (below == BEDROCK) return false;
        if (!isNormalCube(below)) return false;
        if (!validEmpty(blockId(x, y, z))) return false;
        if (!validEmpty(blockId(x, y + 1, z))) return false;
        return true;
    }

    static long pack(int chunkX, int chunkZ, int spawnY, int typeId, int success) {
        long v = 0L;
        v |= ((long) chunkX) & 0xFFFFL;
        v |= (((long) chunkZ) & 0xFFFFL) << 16;
        v |= (((long) spawnY) & 0xFFFFL) << 32;
        v |= (((long) typeId) & 0xFFL) << 48;
        v |= (((long) success) & 0xFFL) << 56;
        return v;
    }

    static void emit(StringBuilder sb, long v) {
        sb.append(String.format("%016x", v)).append('\n');
    }

    static int pickMonster(java.util.Random r) {
        int weight = r.nextInt(TOTAL_WEIGHT);
        for (int i = 0; i < N_MONSTER; ++i) {
            weight -= WEIGHTS[i];
            if (weight < 0) return i;
        }
        return -1;
    }

    static boolean validLight(java.util.Random r, int x, int y, int z) {
        int sky = skyLight(x, y, z);
        if (sky > r.nextInt(32)) return false;
        int comb = combinedLight(x, y, z);
        return comb <= r.nextInt(8);
    }

    static double distSq(double ax, double ay, double az, double bx, double by, double bz) {
        double dx = ax - bx, dy = ay - by, dz = az - bz;
        return dx * dx + dy * dy + dz * dz;
    }

    static void randomChunkPos(java.util.Random r, int cx, int cz, int[] out) {
        int i = cx * 16 + r.nextInt(16);
        int j = cz * 16 + r.nextInt(16);
        int h = heightAt(i, j);
        int k = roundUp(h + 1, 16);
        int bound = k > 0 ? k : 15;
        int l = r.nextInt(bound);
        out[0] = i; out[1] = l; out[2] = j;
    }

    static int collectEligible(int playerCx, int playerCz, int[] outCx, int[] outCz) {
        int n = 0;
        for (int i1 = -CHUNK_RANGE; i1 <= CHUNK_RANGE; ++i1) {
            for (int j1 = -CHUNK_RANGE; j1 <= CHUNK_RANGE; ++j1) {
                boolean flag = i1 == -CHUNK_RANGE || i1 == CHUNK_RANGE
                        || j1 == -CHUNK_RANGE || j1 == CHUNK_RANGE;
                if (!flag) {
                    outCx[n] = i1 + playerCx;
                    outCz[n] = j1 + playerCz;
                    ++n;
                }
            }
        }
        // Sorted (cz, cx) - CUT Collections.shuffle
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                boolean swap = outCz[b] < outCz[a]
                        || (outCz[b] == outCz[a] && outCx[b] < outCx[a]);
                if (swap) {
                    int t = outCx[a]; outCx[a] = outCx[b]; outCx[b] = t;
                    t = outCz[a]; outCz[a] = outCz[b]; outCz[b] = t;
                }
            }
        }
        return n;
    }

    static final int[][] PCS = {
        {0, 0}, {5, -3}, {-10, 12}, {1, 1}, {20, 20}, {-1, 0},
    };
    static final long[] SEEDS = {
        0L, 1L, 7L, 12345L, 99991L, 42L, 0xC0FFEEL, 0xBADC0DEL,
    };

    static void playerTape(int idx, int[] pc, double[] pxyz) {
        if (idx < 0) idx = 0;
        if (idx >= N_PLAYERS) idx = N_PLAYERS - 1;
        pc[0] = PCS[idx][0];
        pc[1] = PCS[idx][1];
        pxyz[0] = (double) (pc[0] * 16) + 8.5;
        pxyz[1] = (double) (FLOOR_Y + 1);
        pxyz[2] = (double) (pc[1] * 16) + 8.5;
    }

    static void spawnCycle(StringBuilder sb, java.util.Random r,
                           int playerCx, int playerCz,
                           double playerX, double playerY, double playerZ,
                           int existingMonsters) {
        int[] ecx = new int[MAX_ELIGIBLE], ecz = new int[MAX_ELIGIBLE];
        int nElig = collectEligible(playerCx, playerCz, ecx, ecz);
        int cap = MONSTER_CAP * nElig / MOB_COUNT_DIV;
        emit(sb, pack(playerCx, playerCz, nElig & 0xFFFF, cap & 0xFF, RES_CAP));
        if (existingMonsters > cap) return;

        int nUse = nElig < CHUNKS_PER_SEED ? nElig : CHUNKS_PER_SEED;
        int[] pos = new int[3];
        for (int ci = 0; ci < nUse; ++ci) {
            int cx = ecx[ci], cz = ecz[ci];
            randomChunkPos(r, cx, cz, pos);
            int k1 = pos[0], l1 = pos[1], i2 = pos[2];

            if (isNormalCube(blockId(k1, l1, i2))) {
                emit(sb, pack(cx, cz, l1, TYPE_NONE, RES_FAIL_INIT_SOLID));
                continue;
            }

            for (int k2 = 0; k2 < GROUPS; ++k2) {
                int l2 = k1, i3 = l1, j3 = i2;
                int typeId = -1;
                int l3 = ceil(r.nextDouble() * 4.0);
                if (l3 <= 0) {
                    emit(sb, pack(cx, cz, i3, TYPE_NONE, RES_FAIL_PACK0));
                    continue;
                }
                for (int i4 = 0; i4 < l3; ++i4) {
                    l2 += r.nextInt(6) - r.nextInt(6);
                    i3 += r.nextInt(1) - r.nextInt(1);
                    j3 += r.nextInt(6) - r.nextInt(6);
                    float f = (float) l2 + 0.5F;
                    float f1 = (float) j3 + 0.5F;

                    if (i3 < 0 || i3 >= 256) {
                        emit(sb, pack(cx, cz, i3 & 0xFFFF, TYPE_NONE, RES_FAIL_OOB));
                        continue;
                    }
                    double dsqP = distSq((double) f, (double) i3, (double) f1,
                            playerX, playerY, playerZ);
                    if (dsqP < 576.0) {
                        emit(sb, pack(cx, cz, i3, TYPE_NONE, RES_FAIL_PLAYER));
                        continue;
                    }
                    double dsqS = distSq((double) f, (double) i3, (double) f1,
                            (double) WORLD_SPAWN_X, (double) WORLD_SPAWN_Y, (double) WORLD_SPAWN_Z);
                    if (dsqS < 576.0) {
                        emit(sb, pack(cx, cz, i3, TYPE_NONE, RES_FAIL_SPAWN_PT));
                        continue;
                    }
                    if (typeId < 0) {
                        typeId = pickMonster(r);
                        if (typeId < 0) break;
                    }
                    if (!canPlace(l2, i3, j3)) {
                        emit(sb, pack(cx, cz, i3, typeId, RES_FAIL_PLACE));
                        continue;
                    }
                    if (!validLight(r, l2, i3, j3)) {
                        emit(sb, pack(cx, cz, i3, typeId, RES_FAIL_LIGHT));
                        continue;
                    }
                    emit(sb, pack(cx, cz, i3, typeId, RES_SPAWN));
                }
            }
        }
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        int[] ecx = new int[MAX_ELIGIBLE], ecz = new int[MAX_ELIGIBLE];
        int[] pc = new int[2];
        double[] pxyz = new double[3];

        // Phase 1: eligible
        for (int pi = 0; pi < N_PLAYERS; ++pi) {
            playerTape(pi, pc, pxyz);
            int n = collectEligible(pc[0], pc[1], ecx, ecz);
            emit(sb, pack(pc[0], pc[1], n & 0xFFFF, pi & 0xFF, RES_ELIGIBLE_COUNT));
            for (int i = 0; i < n; ++i)
                emit(sb, pack(ecx[i], ecz[i], 0, pi & 0xFF, RES_ELIGIBLE_CHUNK));
        }

        // Phase 2: spawn cycles
        for (int si = 0; si < N_SEEDS; ++si) {
            for (int pi = 0; pi < N_PLAYERS; ++pi) {
                long seed = SEEDS[si];
                java.util.Random r = new java.util.Random(seed ^ ((long) pi * 0x9E3779B97F4A7C15L));
                playerTape(pi, pc, pxyz);
                spawnCycle(sb, r, pc[0], pc[1], pxyz[0], pxyz[1], pxyz[2], 0);
            }
        }
        System.out.print(sb);
    }
}
