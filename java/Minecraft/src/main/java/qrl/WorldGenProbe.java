package qrl;

// Worldgen RNG-cursor probe: logs the populate Random's raw 48-bit LCG state at every
// Forge terrain-gen stage boundary, per chunk. Ground truth for the magma/blaze
// worldgen flywheel (magma/trace/genprobe_diff.py): the first checkpoint whose
// cursor differs from the C side pinpoints the exact stage where the streams diverge.
// Enabled only when a log path is configured: qrl_launch.json "genprobe", else the
// run/qrl_genprobe.txt sidecar (its content = the log path).

import com.google.gson.JsonObject;

import java.io.FileWriter;
import java.lang.reflect.Field;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.world.ChunkEvent;
import net.minecraftforge.event.terraingen.DecorateBiomeEvent;
import net.minecraftforge.event.terraingen.OreGenEvent;
import net.minecraftforge.event.terraingen.PopulateChunkEvent;
import net.minecraftforge.fml.common.eventhandler.EventPriority;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;

public class WorldGenProbe {
    private static FileWriter out;
    private static Field seedField;

    public static void install(JsonObject launchCfg) {
        String path = null;
        try {
            if (launchCfg != null && launchCfg.has("genprobe"))
                path = launchCfg.get("genprobe").getAsString();
        } catch (Exception e) { path = null; }
        if (path == null || path.isEmpty()) {
            // sidecar fallback: run/qrl_genprobe.txt containing the log path enables
            // the probe without touching the profile yaml / qrl_launch.json.
            try {
                java.io.File f = new java.io.File("qrl_genprobe.txt");
                if (f.isFile()) {
                    path = new String(java.nio.file.Files.readAllBytes(f.toPath())).trim();
                }
            } catch (Exception e) { }
        }
        if (path == null || path.isEmpty()) return;
        try {
            out = new FileWriter(path, true);
            seedField = Random.class.getDeclaredField("seed");
            seedField.setAccessible(true);
        } catch (Exception e) {
            System.err.println("[genprobe] init failed: " + e);
            return;
        }
        WorldGenProbe p = new WorldGenProbe();
        MinecraftForge.EVENT_BUS.register(p);        // PopulateChunkEvent.Pre/Post
        MinecraftForge.TERRAIN_GEN_BUS.register(p);  // Populate / DecorateBiomeEvent
        MinecraftForge.ORE_GEN_BUS.register(p);      // OreGenEvent.GenerateMinable
        System.err.println("[genprobe] logging worldgen RNG cursors to " + path);
    }

    private static long cursor(Random r) {
        try { return ((AtomicLong) seedField.get(r)).get(); } catch (Exception e) { return -1L; }
    }

    private static synchronized void log(String s) {
        try { out.write(s); out.write('\n'); out.flush(); } catch (Exception e) { }
    }

    // Elapsed-ticks feed for the fluid-CA-timing replay (see magma SPEC / DEVLOG): the
    // between-populate fluid evolution class needs the world's total tick count at every
    // probe line, so the C replay driver can compute how many tickRate-multiples (5 water /
    // 30-10 lava) have had real time to fire between an originating placement and a later
    // read. Appended as a trailing "T<ticks>" token so existing positional-field parsers of
    // this log are unaffected.
    private static String ticks(net.minecraft.world.World w) {
        return w == null ? "T-1" : "T" + w.getTotalWorldTime();
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onPre(PopulateChunkEvent.Pre e) {
        log("PRE " + e.getChunkX() + " " + e.getChunkZ() + " - " + cursor(e.getRand())
            + " " + ticks(e.getWorld()));
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onPopulate(PopulateChunkEvent.Populate e) {
        log("POP " + e.getChunkX() + " " + e.getChunkZ() + " " + e.getType() + " " + cursor(e.getRand())
            + " " + ticks(e.getWorld()));
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onPost(PopulateChunkEvent.Post e) {
        log("POST " + e.getChunkX() + " " + e.getChunkZ() + " - " + cursor(e.getRand())
            + " " + ticks(e.getWorld()));
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onDecoratePre(DecorateBiomeEvent.Pre e) {
        log("DECPRE " + (e.getPos().getX() >> 4) + " " + (e.getPos().getZ() >> 4) + " - " + cursor(e.getRand())
            + " " + ticks(e.getWorld()));
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onDecorate(DecorateBiomeEvent.Decorate e) {
        // trailing x y z: exact event pos (for per-cell passes this is the getHeight
        // result, i.e. the live heightmap value - diff tooling reads it for debugging).
        // T<ticks> appended last so it stays out of the way of the fixed x/y/z tail.
        log("DEC " + (e.getPos().getX() >> 4) + " " + (e.getPos().getZ() >> 4) + " " + e.getType() + " " + cursor(e.getRand())
            + " " + e.getPos().getX() + " " + e.getPos().getY() + " " + e.getPos().getZ()
            + " " + ticks(e.getWorld()));
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onDecoratePost(DecorateBiomeEvent.Post e) {
        log("DECPOST " + (e.getPos().getX() >> 4) + " " + (e.getPos().getZ() >> 4) + " - " + cursor(e.getRand())
            + " " + ticks(e.getWorld()));
    }

    // Chunk-load order feed for the population-cascade emulation (see DEVLOG): a chunk load
    // mid-decorate reseeds the SHARED ChunkProviderOverworld.rand (provideChunk setSeed +
    // surface draws), clobbering the parent chunk's cursor. LOAD lines record the global
    // loaded-set evolution; the trailing cursor is the provider rand AFTER the load settles.
    private static Field generatorRandField;
    private static Object cachedGenerator;
    private static Random providerRand(net.minecraft.world.World w) {
        try {
            net.minecraft.world.chunk.IChunkProvider cp = w.getChunkProvider();
            Field gf = cp.getClass().getDeclaredField("chunkGenerator");
            gf.setAccessible(true);
            Object gen = gf.get(cp);
            if (gen == null) return null;
            if (generatorRandField == null || cachedGenerator == null
                    || cachedGenerator.getClass() != gen.getClass()) {
                Field rf = gen.getClass().getDeclaredField("rand");
                rf.setAccessible(true);
                generatorRandField = rf;
            }
            cachedGenerator = gen;
            return (Random) generatorRandField.get(gen);
        } catch (Exception ex) { return null; }
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onChunkLoad(ChunkEvent.Load e) {
        if (e.getWorld() == null || e.getWorld().isRemote) return;
        if (e.getWorld().provider.getDimension() != 0) return;
        Random r = providerRand(e.getWorld());
        log("LOAD " + e.getChunk().xPosition + " " + e.getChunk().zPosition + " - "
            + (r == null ? -1L : cursor(r)) + " " + ticks(e.getWorld()));
    }

    // Population-cascade capture: ChunkEvent.Load does NOT fire for freshly generated
    // chunks in this Forge build, so instead swap ChunkProviderServer.chunkGenerator
    // (public final, Java-8 modifiers hack) with a delegating proxy that logs
    // "GEN cx cz cursorBefore cursorAfter" around every provideChunk. GEN lines that
    // land inside another chunk's PRE..POST window are exactly the mid-populate
    // provider-rand clobbers the C replay must emulate (cursorAfter = jump target).
    static class GenProxy implements net.minecraft.world.chunk.IChunkGenerator {
        final net.minecraft.world.chunk.IChunkGenerator d;
        final Random rand;
        final net.minecraft.world.World w;
        GenProxy(net.minecraft.world.chunk.IChunkGenerator d, net.minecraft.world.World w) {
            this.d = d;
            this.w = w;
            Random r = null;
            try {
                Field rf = d.getClass().getDeclaredField("rand");
                rf.setAccessible(true);
                r = (Random) rf.get(d);
            } catch (Exception ex) { }
            this.rand = r;
        }
        public net.minecraft.world.chunk.Chunk provideChunk(int x, int z) {
            long before = rand == null ? -1L : cursor(rand);
            net.minecraft.world.chunk.Chunk c = d.provideChunk(x, z);
            log("GEN " + x + " " + z + " " + before + " " + (rand == null ? -1L : cursor(rand))
                + " " + ticks(w));
            return c;
        }
        public void populate(int x, int z) { d.populate(x, z); }
        public boolean generateStructures(net.minecraft.world.chunk.Chunk c, int x, int z) {
            return d.generateStructures(c, x, z);
        }
        public java.util.List<net.minecraft.world.biome.Biome.SpawnListEntry> getPossibleCreatures(
                net.minecraft.entity.EnumCreatureType t, net.minecraft.util.math.BlockPos pos) {
            return d.getPossibleCreatures(t, pos);
        }
        public net.minecraft.util.math.BlockPos getStrongholdGen(net.minecraft.world.World w,
                String name, net.minecraft.util.math.BlockPos pos, boolean p) {
            return d.getStrongholdGen(w, name, pos, p);
        }
        public void recreateStructures(net.minecraft.world.chunk.Chunk c, int x, int z) {
            d.recreateStructures(c, x, z);
        }
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onWorldLoad(net.minecraftforge.event.world.WorldEvent.Load e) {
        if (e.getWorld() == null || e.getWorld().isRemote) return;
        if (e.getWorld().provider.getDimension() != 0) return;
        try {
            net.minecraft.world.chunk.IChunkProvider cp = e.getWorld().getChunkProvider();
            Field gf = cp.getClass().getDeclaredField("chunkGenerator");
            gf.setAccessible(true);
            Object gen = gf.get(cp);
            if (gen == null || gen instanceof GenProxy) return;
            Field mods = Field.class.getDeclaredField("modifiers");
            mods.setAccessible(true);
            mods.setInt(gf, gf.getModifiers() & ~java.lang.reflect.Modifier.FINAL);
            gf.set(cp, new GenProxy((net.minecraft.world.chunk.IChunkGenerator) gen, e.getWorld()));
            System.err.println("[genprobe] chunkGenerator proxied for GEN capture");
        } catch (Exception ex) {
            System.err.println("[genprobe] proxy install failed: " + ex);
        }
    }

    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onOre(OreGenEvent.GenerateMinable e) {
        log("ORE " + (e.getPos().getX() >> 4) + " " + (e.getPos().getZ() >> 4) + " " + e.getType() + " " + cursor(e.getRand())
            + " " + e.getPos().getX() + " " + e.getPos().getY() + " " + e.getPos().getZ());
    }

    // Per-tree-attempt probe: the vanilla decorator's TREE loop is the one worldgen stage
    // whose draw count is block-context-conditional (vined jungle trees, shrub descents,
    // mega ensureGrowable), so stage-boundary cursors alone cannot localize a divergence
    // inside it. DeferredBiomeDecorator fires BiomeEvent.CreateDecorator on each biome's
    // first decorate; substitute a decorator whose genDecorations is a verbatim copy with
    // "TREEATT cx cz i:k6:l:type cursor" / "TREEOK|TREENO cx cz i:type cursor" probes
    // around each tree attempt. Only plain BiomeDecorator instances are substituted.
    @SubscribeEvent(priority = EventPriority.HIGHEST)
    public void onCreateDecorator(net.minecraftforge.event.terraingen.BiomeEvent.CreateDecorator e) {
        if (out == null) return;
        if (e.getOriginalBiomeDecorator().getClass() != net.minecraft.world.biome.BiomeDecorator.class) return;
        e.setNewBiomeDecorator(new TreeProbeDecorator(e.getOriginalBiomeDecorator()));
    }

    static class TreeProbeDecorator extends net.minecraft.world.biome.BiomeDecorator {
        // WorldGenBigTree.heightLimit chain probe: session state that shapes foliage
        // geometry without touching the shared RNG stream
        private static java.lang.reflect.Field HL_FIELD;
        static {
            try {
                HL_FIELD = net.minecraft.world.gen.feature.WorldGenBigTree.class.getDeclaredField("heightLimit");
                HL_FIELD.setAccessible(true);
            } catch (Throwable t) {}
        }
        private static String hl(net.minecraft.world.gen.feature.WorldGenAbstractTree t) {
            if (HL_FIELD == null || !(t instanceof net.minecraft.world.gen.feature.WorldGenBigTree)) return "";
            try { return "/hl=" + HL_FIELD.getInt(t); } catch (Throwable e) { return ""; }
        }
        TreeProbeDecorator(net.minecraft.world.biome.BiomeDecorator o) {
            this.waterlilyPerChunk = o.waterlilyPerChunk;
            this.treesPerChunk = o.treesPerChunk;
            this.extraTreeChance = o.extraTreeChance;
            this.flowersPerChunk = o.flowersPerChunk;
            this.grassPerChunk = o.grassPerChunk;
            this.deadBushPerChunk = o.deadBushPerChunk;
            this.mushroomsPerChunk = o.mushroomsPerChunk;
            this.reedsPerChunk = o.reedsPerChunk;
            this.cactiPerChunk = o.cactiPerChunk;
            this.sandPerChunk = o.sandPerChunk;
            this.sandPerChunk2 = o.sandPerChunk2;
            this.clayPerChunk = o.clayPerChunk;
            this.bigMushroomsPerChunk = o.bigMushroomsPerChunk;
            this.generateLakes = o.generateLakes;
        }

        // Verbatim copy of BiomeDecorator.genDecorations (1.11.2 + Forge patches) with the
        // TREE loop instrumented. Any vanilla change would need re-copying - acceptable for
        // a frozen 1.11.2 tree.
        protected void genDecorations(net.minecraft.world.biome.Biome biomeIn,
                                      net.minecraft.world.World worldIn, Random random) {
            net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(new net.minecraftforge.event.terraingen.DecorateBiomeEvent.Pre(worldIn, random, chunkPos));
            this.generateOres(worldIn, random);

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.SAND))
            for (int i = 0; i < this.sandPerChunk2; ++i) {
                int j = random.nextInt(16) + 8;
                int k = random.nextInt(16) + 8;
                this.sandGen.generate(worldIn, random, worldIn.getTopSolidOrLiquidBlock(this.chunkPos.add(j, 0, k)));
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.CLAY))
            for (int i1 = 0; i1 < this.clayPerChunk; ++i1) {
                int l1 = random.nextInt(16) + 8;
                int i6 = random.nextInt(16) + 8;
                this.clayGen.generate(worldIn, random, worldIn.getTopSolidOrLiquidBlock(this.chunkPos.add(l1, 0, i6)));
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.SAND_PASS2))
            for (int j1 = 0; j1 < this.sandPerChunk; ++j1) {
                int i2 = random.nextInt(16) + 8;
                int j6 = random.nextInt(16) + 8;
                this.gravelAsSandGen.generate(worldIn, random, worldIn.getTopSolidOrLiquidBlock(this.chunkPos.add(i2, 0, j6)));
            }

            int k1 = this.treesPerChunk;
            if (random.nextFloat() < this.extraTreeChance) {
                ++k1;
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.TREE))
            for (int j2 = 0; j2 < k1; ++j2) {
                int k6 = random.nextInt(16) + 8;
                int l = random.nextInt(16) + 8;
                net.minecraft.world.gen.feature.WorldGenAbstractTree worldgenabstracttree = biomeIn.genBigTreeChance(random);
                worldgenabstracttree.setDecorationDefaults();
                net.minecraft.util.math.BlockPos blockpos = worldIn.getHeight(this.chunkPos.add(k6, 0, l));
                String type = worldgenabstracttree.getClass().getSimpleName();
                log("TREEATT " + (this.chunkPos.getX() >> 4) + " " + (this.chunkPos.getZ() >> 4) + " "
                    + j2 + ":" + k6 + ":" + l + ":" + blockpos.getY() + ":" + type + hl(worldgenabstracttree)
                    + " " + cursor(random));
                boolean ok = worldgenabstracttree.generate(worldIn, random, blockpos);
                log((ok ? "TREEOK " : "TREENO ") + (this.chunkPos.getX() >> 4) + " " + (this.chunkPos.getZ() >> 4) + " "
                    + j2 + ":" + type + hl(worldgenabstracttree) + " " + cursor(random));
                if (ok) {
                    worldgenabstracttree.generateSaplings(worldIn, random, blockpos);
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.BIG_SHROOM))
            for (int k2 = 0; k2 < this.bigMushroomsPerChunk; ++k2) {
                int l6 = random.nextInt(16) + 8;
                int k10 = random.nextInt(16) + 8;
                this.bigMushroomGen.generate(worldIn, random, worldIn.getHeight(this.chunkPos.add(l6, 0, k10)));
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.FLOWERS))
            for (int l2 = 0; l2 < this.flowersPerChunk; ++l2) {
                int i7 = random.nextInt(16) + 8;
                int l10 = random.nextInt(16) + 8;
                int j14 = worldIn.getHeight(this.chunkPos.add(i7, 0, l10)).getY() + 32;
                if (j14 > 0) {
                    int k17 = random.nextInt(j14);
                    net.minecraft.util.math.BlockPos blockpos1 = this.chunkPos.add(i7, k17, l10);
                    net.minecraft.block.BlockFlower.EnumFlowerType blockflower$enumflowertype = biomeIn.pickRandomFlower(random, blockpos1);
                    net.minecraft.block.BlockFlower blockflower = blockflower$enumflowertype.getBlockType().getBlock();
                    if (blockflower.getDefaultState().getMaterial() != net.minecraft.block.material.Material.AIR) {
                        this.yellowFlowerGen.setGeneratedBlock(blockflower, blockflower$enumflowertype);
                        this.yellowFlowerGen.generate(worldIn, random, blockpos1);
                    }
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.GRASS))
            for (int i3 = 0; i3 < this.grassPerChunk; ++i3) {
                int j7 = random.nextInt(16) + 8;
                int i11 = random.nextInt(16) + 8;
                int k14 = worldIn.getHeight(this.chunkPos.add(j7, 0, i11)).getY() * 2;
                if (k14 > 0) {
                    int l17 = random.nextInt(k14);
                    biomeIn.getRandomWorldGenForGrass(random).generate(worldIn, random, this.chunkPos.add(j7, l17, i11));
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.DEAD_BUSH))
            for (int j3 = 0; j3 < this.deadBushPerChunk; ++j3) {
                int k7 = random.nextInt(16) + 8;
                int j11 = random.nextInt(16) + 8;
                int l14 = worldIn.getHeight(this.chunkPos.add(k7, 0, j11)).getY() * 2;
                if (l14 > 0) {
                    int i18 = random.nextInt(l14);
                    (new net.minecraft.world.gen.feature.WorldGenDeadBush()).generate(worldIn, random, this.chunkPos.add(k7, i18, j11));
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.LILYPAD))
            for (int k3 = 0; k3 < this.waterlilyPerChunk; ++k3) {
                int l7 = random.nextInt(16) + 8;
                int k11 = random.nextInt(16) + 8;
                int i15 = worldIn.getHeight(this.chunkPos.add(l7, 0, k11)).getY() * 2;
                if (i15 > 0) {
                    int j18 = random.nextInt(i15);
                    net.minecraft.util.math.BlockPos blockpos4;
                    net.minecraft.util.math.BlockPos blockpos7;
                    for (blockpos4 = this.chunkPos.add(l7, j18, k11); blockpos4.getY() > 0; blockpos4 = blockpos7) {
                        blockpos7 = blockpos4.down();
                        if (!worldIn.isAirBlock(blockpos7)) {
                            break;
                        }
                    }
                    this.waterlilyGen.generate(worldIn, random, blockpos4);
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.SHROOM)) {
                for (int l3 = 0; l3 < this.mushroomsPerChunk; ++l3) {
                    if (random.nextInt(4) == 0) {
                        int i8 = random.nextInt(16) + 8;
                        int l11 = random.nextInt(16) + 8;
                        net.minecraft.util.math.BlockPos blockpos2 = worldIn.getHeight(this.chunkPos.add(i8, 0, l11));
                        this.mushroomBrownGen.generate(worldIn, random, blockpos2);
                    }
                    if (random.nextInt(8) == 0) {
                        int j8 = random.nextInt(16) + 8;
                        int i12 = random.nextInt(16) + 8;
                        int j15 = worldIn.getHeight(this.chunkPos.add(j8, 0, i12)).getY() * 2;
                        if (j15 > 0) {
                            int k18 = random.nextInt(j15);
                            net.minecraft.util.math.BlockPos blockpos5 = this.chunkPos.add(j8, k18, i12);
                            this.mushroomRedGen.generate(worldIn, random, blockpos5);
                        }
                    }
                }
                if (random.nextInt(4) == 0) {
                    int i4 = random.nextInt(16) + 8;
                    int k8 = random.nextInt(16) + 8;
                    int j12 = worldIn.getHeight(this.chunkPos.add(i4, 0, k8)).getY() * 2;
                    if (j12 > 0) {
                        int k15 = random.nextInt(j12);
                        this.mushroomBrownGen.generate(worldIn, random, this.chunkPos.add(i4, k15, k8));
                    }
                }
                if (random.nextInt(8) == 0) {
                    int j4 = random.nextInt(16) + 8;
                    int l8 = random.nextInt(16) + 8;
                    int k12 = worldIn.getHeight(this.chunkPos.add(j4, 0, l8)).getY() * 2;
                    if (k12 > 0) {
                        int l15 = random.nextInt(k12);
                        this.mushroomRedGen.generate(worldIn, random, this.chunkPos.add(j4, l15, l8));
                    }
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.REED)) {
                for (int k4 = 0; k4 < this.reedsPerChunk; ++k4) {
                    int i9 = random.nextInt(16) + 8;
                    int l12 = random.nextInt(16) + 8;
                    int i16 = worldIn.getHeight(this.chunkPos.add(i9, 0, l12)).getY() * 2;
                    if (i16 > 0) {
                        int l18 = random.nextInt(i16);
                        this.reedGen.generate(worldIn, random, this.chunkPos.add(i9, l18, l12));
                    }
                }
                for (int l4 = 0; l4 < 10; ++l4) {
                    int j9 = random.nextInt(16) + 8;
                    int i13 = random.nextInt(16) + 8;
                    int j16 = worldIn.getHeight(this.chunkPos.add(j9, 0, i13)).getY() * 2;
                    if (j16 > 0) {
                        int i19 = random.nextInt(j16);
                        this.reedGen.generate(worldIn, random, this.chunkPos.add(j9, i19, i13));
                    }
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.PUMPKIN))
            if (random.nextInt(32) == 0) {
                int i5 = random.nextInt(16) + 8;
                int k9 = random.nextInt(16) + 8;
                int j13 = worldIn.getHeight(this.chunkPos.add(i5, 0, k9)).getY() * 2;
                if (j13 > 0) {
                    int k16 = random.nextInt(j13);
                    (new net.minecraft.world.gen.feature.WorldGenPumpkin()).generate(worldIn, random, this.chunkPos.add(i5, k16, k9));
                }
            }

            if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.CACTUS))
            for (int j5 = 0; j5 < this.cactiPerChunk; ++j5) {
                int l9 = random.nextInt(16) + 8;
                int k13 = random.nextInt(16) + 8;
                int l16 = worldIn.getHeight(this.chunkPos.add(l9, 0, k13)).getY() * 2;
                if (l16 > 0) {
                    int j19 = random.nextInt(l16);
                    this.cactusGen.generate(worldIn, random, this.chunkPos.add(l9, j19, k13));
                }
            }

            if (this.generateLakes) {
                if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.LAKE_WATER))
                for (int k5 = 0; k5 < 50; ++k5) {
                    int i10 = random.nextInt(16) + 8;
                    int l13 = random.nextInt(16) + 8;
                    int i17 = random.nextInt(248) + 8;
                    if (i17 > 0) {
                        int k19 = random.nextInt(i17);
                        net.minecraft.util.math.BlockPos blockpos6 = this.chunkPos.add(i10, k19, l13);
                        (new net.minecraft.world.gen.feature.WorldGenLiquids(net.minecraft.init.Blocks.FLOWING_WATER)).generate(worldIn, random, blockpos6);
                    }
                }
                if (net.minecraftforge.event.terraingen.TerrainGen.decorate(worldIn, random, chunkPos, net.minecraftforge.event.terraingen.DecorateBiomeEvent.Decorate.EventType.LAKE_LAVA))
                for (int l5 = 0; l5 < 20; ++l5) {
                    int j10 = random.nextInt(16) + 8;
                    int i14 = random.nextInt(16) + 8;
                    int j17 = random.nextInt(random.nextInt(random.nextInt(240) + 8) + 8);
                    net.minecraft.util.math.BlockPos blockpos3 = this.chunkPos.add(j10, j17, i14);
                    (new net.minecraft.world.gen.feature.WorldGenLiquids(net.minecraft.init.Blocks.FLOWING_LAVA)).generate(worldIn, random, blockpos3);
                }
            }
            net.minecraftforge.common.MinecraftForge.EVENT_BUS.post(new net.minecraftforge.event.terraingen.DecorateBiomeEvent.Post(worldIn, random, chunkPos));
        }
    }
}
