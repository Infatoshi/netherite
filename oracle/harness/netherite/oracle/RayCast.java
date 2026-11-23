package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.io.File;
import java.io.IOException;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ChunkProviderClient;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.monster.EntityBlaze;
import net.minecraft.entity.monster.EntityCaveSpider;
import net.minecraft.entity.monster.EntityCreeper;
import net.minecraft.entity.monster.EntityEnderman;
import net.minecraft.entity.monster.EntityGhast;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.entity.monster.EntityMagmaCube;
import net.minecraft.entity.monster.EntityPigZombie;
import net.minecraft.entity.monster.EntitySilverfish;
import net.minecraft.entity.monster.EntitySkeleton;
import net.minecraft.entity.monster.EntitySlime;
import net.minecraft.entity.monster.EntitySpider;
import net.minecraft.entity.monster.EntityWitch;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityBat;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntityCow;
import net.minecraft.entity.passive.EntityMooshroom;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.passive.EntitySquid;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.LongHashMap;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.NibbleArray;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
import net.minecraft.world.gen.NoiseGeneratorPerlin;

/**
 * The policy's observation drawn by the oracle (GOAL.md "Done means" 4, lane/parity): csrc/runtime/raycast.c's
 * textured ray cast, its FP64 reference instance (raycast_ref: every cell, light and biome read through the world's
 * chunks), computed from this client's own state as each row is captured: the client world's chunks (the ids,
 * metadata, block and sky light, biomes and height maps of ChunkProviderClient's loaded chunks), the client player's
 * eye and look, the client world's clock, the server world's weather, the client world's living copies and fireballs,
 * and the player's dimension's server items, XP orbs and arrows (the C cast's own sources, raycast.c entities()).
 * raycast.h has the definition; every expression below is raycast.c's, in its order (Java's double and float
 * arithmetic is IEEE's, as C's with -ffp-contract=off; MathHelper.sin is the C's mh_sin table, StrictMath.cos and
 * pow its fdlibm). The tables are the ones the C cast loads, which the oracle made: the play scene's table.bin and
 * terrain atlas (out/java/render/forest-fast, MeshProbe's) and out/assets' textures (the entities' flat colours).
 *
 * client.camera raycast (with client.observation WxH, client.graphics, client.gamma, client.channels) or Main's
 * --raycast WxH[:fancy] (a replay: a tape's world takes no --set) puts each row's frame digest in the row as d.rc:
 * FNV-1a 64 over the frame's bytes, pool_gate --raycast-rows' digest of C's cast of the same row. --raycast-dump
 * DIR:EVERY also writes the frame of every EVERY-th row as DIR/r_TTTTTT.png (pxdiff's input against pool_gate
 * --raycast-dump's). Read only: the cast changes nothing the game reads.
 */
public final class RayCast
{
    /** The spec (raycast.h struct raycast_spec): on, the size, channels, graphics and gamma. */
    static boolean on;
    static int specW, specH, specCh = 3;
    static boolean specFancy;
    static float specGamma;
    /** --raycast-dump DIR:EVERY (or DIR:@T, row T's alone: dumpAt) */
    static String dumpDir;
    static int dumpEvery;
    static long dumpAt = -1;

    private RayCast() {}

    /** WxH[:fancy] (pool_gate --raycast's form); false when it does not parse. */
    static boolean parse(String v)
    {
        String[] p = v.split(":");
        String[] wh = p[0].split("x");
        if (wh.length < 2 || wh.length > 3) return false;
        try
        {
            specW = Integer.parseInt(wh[0]);
            specH = Integer.parseInt(wh[1]);
            specCh = wh.length == 3 ? Integer.parseInt(wh[2]) : 3;
        }
        catch (NumberFormatException e) { return false; }
        specFancy = false;
        for (int i = 1; i < p.length; ++i)
        {
            if (p[i].equals("fancy")) specFancy = true;
            else if (!p[i].equals("f64")) return false;
        }
        on = specW >= 1 && specH >= 1 && (specCh == 1 || specCh == 3);
        return on;
    }

    // ------------------------------------------------------------------ the tables (raycast.c load)

    static final int NONE = 0xffff;
    static final int RK_AIR = 0, RK_CUBE = 1, RK_BOX = 2, RK_CROSS = 3, RK_WATER = 4, RK_LAVA = 5;
    static final int TN_NONE = 0, TN_GRASS_TOP = 1, TN_GRASS = 2, TN_FOLIAGE = 3, TN_OLDLEAF = 4, TN_WATER = 5, TN_TALLGRASS = 6,
        TN_STEM = 7, TN_DPLANT = 8;
    static final int RC_ENTS = 64;
    static final double TAN_HALF = 0.7002075382097097;
    static final float PIF = 3.1415927f;
    static final float DEG2RAD = 0.017453292f;
    static final float[] SHADE = {0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f};
    static final float[] WHITE = {1.0f, 1.0f, 1.0f};

    static final class Tables
    {
        String err;
        int[][] tex;                /* a sprite's 16x16 texels, r | g << 8 | b << 16 | a << 24, v then u */
        int[][] face = new int[2][];/* [graphics fancy][(id * 16 + meta) * 6 + side]: a sprite, NONE none */
        double[] bounds;            /* [(id * 16 + meta) * 6] */
        byte[] kind = new byte[4096], tint = new byte[4096], emit = new byte[4096], cullSame = new byte[4096];
        float[][] grass = new float[256][3], foliage = new float[256][3], water = new float[256][3];
        float[] unknown = {128.0f, 128.0f, 128.0f}, item = new float[3], orb = new float[3], arrow = new float[3],
            fireball = {230.0f, 120.0f, 30.0f};
        float[][] mob = new float[MOB.length][3];
        float[] endSky = new float[3], underwater = new float[3];
        int waterSpr, lavaSpr;
        int[] dpLow = new int[6], dpHigh = new int[6];
        float[] biomeTemp = new float[256];
    }

    /** the living kinds' textures (raycast.c MOB), by class: a subclass before its base */
    static final Class<?>[] MOB_CLASS = {
        EntityMooshroom.class, EntityCow.class, EntityPig.class, EntityChicken.class, EntitySheep.class, EntityVillager.class,
        EntityIronGolem.class, EntityMagmaCube.class, EntitySlime.class, EntityPlayer.class, EntityPigZombie.class,
        EntityZombie.class, EntitySkeleton.class, EntityCreeper.class, EntityCaveSpider.class, EntitySpider.class,
        EntityGhast.class, EntitySquid.class, EntityBat.class, EntityEnderman.class, EntityWitch.class, EntitySilverfish.class,
        EntityBlaze.class,
    };
    static final String[] MOB = {
        "mobs/mooshroom.rgba", "mobs/cow.rgba", "mobs/pig.rgba", "mobs/chicken.rgba", "mobs/sheep_fur.rgba", "mobs/villager.rgba",
        "mobs/iron_golem.rgba", "mobs/magmacube.rgba", "mobs/slime.rgba", "mobs/steve.rgba", "mobs/pigman.rgba",
        "mobs/zombie.rgba", "mobs/skeleton.rgba", "mobs/creeper.rgba", "mobs/cave_spider.rgba", "mobs/spider.rgba",
        "mobs/ghast.rgba", "mobs/squid.rgba", "mobs/bat.rgba", "mobs/enderman.rgba", "mobs/witch.rgba", "mobs/silverfish.rgba",
        "mobs/blaze.rgba",
    };

    static Tables T;

    /** The checkout: Main's --assets is ROOT/out/java/assets. */
    static File root()
    {
        File a = new File(Main.assets).getAbsoluteFile();
        return a.getParentFile().getParentFile().getParentFile();
    }

    static Tables tables()
    {
        if (T == null)
        {
            Tables t = new Tables();
            try { load(t, root()); }
            catch (IOException e) { t.err = "raycast: " + e.getMessage(); }
            if (t.err != null) System.out.println("ORACLE RAYCAST " + t.err);
            T = t;
        }
        return T.err == null ? T : null;
    }

    static int u16(ByteBuffer b) { return b.getShort() & 0xffff; }

    static String str(ByteBuffer b)
    {
        int n = u16(b);
        byte[] s = new byte[n];
        b.get(s);
        return new String(s, java.nio.charset.StandardCharsets.UTF_8);
    }

    /** a texture's opaque texels' mean (raycast.c mean_rgba) */
    static void meanRgba(File root, String name, float[] out) throws IOException
    {
        byte[] b = Files.readAllBytes(new File(root, "out/assets/" + name).toPath());
        double s0 = 0, s1 = 0, s2 = 0, c = 0;
        for (int i = 0; i + 3 < b.length; i += 4)
            if ((b[i + 3] & 255) > 25)
            {
                s0 += b[i] & 255;
                s1 += b[i + 1] & 255;
                s2 += b[i + 2] & 255;
                c += 1;
            }
        out[0] = c > 0 ? (float)(s0 / c) : 128.0f;
        out[1] = c > 0 ? (float)(s1 / c) : 128.0f;
        out[2] = c > 0 ? (float)(s2 / c) : 128.0f;
    }

    static float clamp01(float f) { return f < 0.0F ? 0.0F : f > 1.0F ? 1.0F : f; }

    static int colormap(int[] map, double temperature, double humidity)
    {
        double h = humidity * temperature;
        int a = (int)((1.0 - temperature) * 255.0);
        int b = (int)((1.0 - h) * 255.0);
        return map[(b << 8) | a];
    }

    static void rgb01(int c, float[] o)
    {
        o[0] = (float)(c >> 16 & 255) / 255.0f;
        o[1] = (float)(c >> 8 & 255) / 255.0f;
        o[2] = (float)(c & 255) / 255.0f;
    }

    /** table.bin's biomes: temperature, rainfall, water, grass and foliage constants, kinds, base */
    static float[] bTemp = new float[256], bRain = new float[256];
    static int[] bWater = new int[256], bGrassC = new int[256], bFolC = new int[256], bGrassK = new int[256], bFolK = new int[256],
        bBase = new int[256];

    /** render_blocks.c biome_color at the biome's base temperature */
    static int biomeColor(int[] grassMap, int[] folMap, int slot, boolean grass, int depth)
    {
        slot &= 255;
        int kind = grass ? bGrassK[slot] : bFolK[slot];
        if (kind == 4 && depth < 4 && bBase[slot] >= 0) return biomeColor(grassMap, folMap, bBase[slot], grass, depth + 1);
        if (kind == 3) return grass ? bGrassC[slot] : bFolC[slot];
        if (kind == 2) return 6975545;
        float t = clamp01(bTemp[slot]), h = clamp01(bRain[slot]);
        if (grass)
        {
            int c = colormap(grassMap, (double)t, (double)h);
            return kind == 1 ? (((c & 16711422) + 2634760 + 2) >> 1) : c;
        }
        return colormap(folMap, (double)t, (double)h);
    }

    static void load(Tables t, File root) throws IOException
    {
        File scene = new File(root, "out/java/render/forest-fast");
        ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(new File(scene, "table.bin").toPath())).order(ByteOrder.LITTLE_ENDIAN);
        if (b.getInt() != 0x3142544d || b.getInt() != 1 || b.getInt() != 4096 || b.getInt() != 16 || b.getInt() != 6)
            throw new IOException(scene + "/table.bin is not this build's layout");
        int nicon = b.getInt(), nspecial = b.getInt();
        String[] icon = new String[nicon], special = new String[nspecial];
        for (int i = 0; i < nicon; ++i) icon[i] = str(b);
        for (int i = 0; i < nspecial; ++i) special[i] = str(b);
        int nf = 4096 * 16 * 6;
        int[] iconIndex = new int[nf];
        for (int i = 0; i < nf; ++i) iconIndex[i] = u16(b);
        int[] grassMap = new int[65536], folMap = new int[65536];
        for (int i = 0; i < 65536; ++i) grassMap[i] = b.getInt();
        for (int i = 0; i < 65536; ++i) folMap[i] = b.getInt();
        b.position(b.position() + 4096 * 12);
        t.bounds = new double[nf];
        for (int i = 0; i < nf; ++i) t.bounds[i] = b.getDouble();
        for (int i = 0; i < 256; ++i)
        {
            int o = b.position();
            bTemp[i] = b.getFloat(o);
            bRain[i] = b.getFloat(o + 4);
            bWater[i] = b.getInt(o + 8);
            bGrassC[i] = b.getInt(o + 12);
            bFolC[i] = b.getInt(o + 16);
            bGrassK[i] = b.get(o + 20) & 255;
            bFolK[i] = b.get(o + 21) & 255;
            bBase[i] = b.getShort(o + 22);
            b.position(o + 32);
        }

        // the atlas: every sprite as 16x16 texels, nearest
        JsonObject aj = Rows.parse(new String(Files.readAllBytes(new File(scene, "atlas.json").toPath()), "UTF-8"));
        int aw = aj.get("atlas_width").getAsInt(), ah = aj.get("atlas_height").getAsInt();
        byte[] atlas = Files.readAllBytes(new File(scene, "atlas.rgba").toPath());
        if (aw <= 0 || ah <= 0 || atlas.length != aw * ah * 4) throw new IOException(scene + "/atlas.rgba is not " + aw + "x" + ah + " RGBA");
        JsonArray sprites = aj.getAsJsonArray("sprites");
        int ns = sprites.size();
        String[] sname = new String[ns];
        t.tex = new int[ns][256];
        for (int s = 0; s < ns; ++s)
        {
            JsonObject o = sprites.get(s).getAsJsonObject();
            sname[s] = o.get("name").getAsString();
            float minU = Float.intBitsToFloat((int)o.get("minU").getAsLong()), maxU = Float.intBitsToFloat((int)o.get("maxU").getAsLong());
            float minV = Float.intBitsToFloat((int)o.get("minV").getAsLong()), maxV = Float.intBitsToFloat((int)o.get("maxV").getAsLong());
            double x0 = (double)minU * aw, x1 = (double)maxU * aw, y0 = (double)minV * ah, y1 = (double)maxV * ah;
            for (int v = 0; v < 16; ++v)
                for (int u = 0; u < 16; ++u)
                {
                    int x = (int)Math.floor(x0 + (x1 - x0) * ((double)u + 0.5) / 16.0);
                    int y = (int)Math.floor(y0 + (y1 - y0) * ((double)v + 0.5) / 16.0);
                    if (x < 0) x = 0;
                    if (x >= aw) x = aw - 1;
                    if (y < 0) y = 0;
                    if (y >= ah) y = ah - 1;
                    int p = (y * aw + x) * 4;
                    t.tex[s][v * 16 + u] = (atlas[p] & 255) | (atlas[p + 1] & 255) << 8 | (atlas[p + 2] & 255) << 16 | (atlas[p + 3] & 255) << 24;
                }
        }
        int[] spr = new int[nicon + 1], fancy = new int[nicon + 1];
        for (int i = 0; i < nicon; ++i)
        {
            spr[i] = fancy[i] = find(sname, icon[i]);
            if (icon[i].length() > 7 && icon[i].endsWith("_opaque"))
            {
                int f = find(sname, icon[i].substring(0, icon[i].length() - 7));
                if (f >= 0) fancy[i] = f;
            }
        }
        t.face[0] = new int[nf];
        t.face[1] = new int[nf];
        for (int i = 0; i < nf; ++i)
        {
            int ic = iconIndex[i];
            t.face[0][i] = ic != NONE && ic < nicon && spr[ic] >= 0 ? spr[ic] : NONE;
            t.face[1][i] = ic != NONE && ic < nicon && fancy[ic] >= 0 ? fancy[ic] : NONE;
        }
        for (int id = 0; id < 4096; ++id)
        {
            Block bd = Block.getBlockById(id);
            boolean exists = id != 0 && bd != null && bd != Blocks.air;
            boolean any = false;
            for (int m = 0; m < 16 && !any; ++m)
                for (int s = 0; s < 6 && !any; ++s) any = t.face[0][(id * 16 + m) * 6 + s] != NONE;
            int rt = exists ? bd.getRenderType() : -1;
            int o = id * 16 * 6;
            double[] bb = t.bounds;
            boolean full = bb[o] == 0.0 && bb[o + 1] == 0.0 && bb[o + 2] == 0.0 && bb[o + 3] == 1.0 && bb[o + 4] == 1.0 && bb[o + 5] == 1.0;
            int k;
            if (id == 0 || !exists || !any) k = RK_AIR;
            else if (id == 8 || id == 9) k = RK_WATER;
            else if (id == 10 || id == 11) k = RK_LAVA;
            else if (rt == 1 || rt == 6 || rt == 19 || rt == 40) k = RK_CROSS;
            else if (full && rt != 2) k = RK_CUBE;
            else k = RK_BOX;
            t.kind[id] = (byte)k;
            t.emit[id] = (byte)(exists ? bd.getLightValue() : 0);
            t.cullSame[id] = (byte)(id == 20 || id == 95 || id == 79 || id == 102 || id == 160 ? 1 : 0);
            t.tint[id] = (byte)(id == 2 ? TN_GRASS_TOP : id == 18 ? TN_OLDLEAF : id == 161 || id == 106 ? TN_FOLIAGE
                : id == 8 || id == 9 ? TN_WATER : id == 31 ? TN_TALLGRASS : id == 83 ? TN_GRASS
                : id == 104 || id == 105 ? TN_STEM : id == 175 ? TN_DPLANT : TN_NONE);
        }
        for (int s = 0; s < 256; ++s)
        {
            rgb01(biomeColor(grassMap, folMap, s, true, 0), t.grass[s]);
            rgb01(biomeColor(grassMap, folMap, s, false, 0), t.foliage[s]);
            rgb01(bWater[s], t.water[s]);
            t.biomeTemp[s] = bTemp[s];
        }
        t.waterSpr = t.face[0][(9 * 16) * 6 + 1];
        t.lavaSpr = t.face[0][(11 * 16) * 6 + 1];
        for (int v = 0; v < 6; ++v)
        {
            int lo = 2 + v < nspecial ? find(sname, special[2 + v]) : -1;
            int hi = 8 + v < nspecial ? find(sname, special[8 + v]) : -1;
            t.dpLow[v] = lo >= 0 ? lo : NONE;
            t.dpHigh[v] = hi >= 0 ? hi : NONE;
        }
        for (int k = 0; k < MOB.length; ++k) meanRgba(root, MOB[k], t.mob[k]);
        meanRgba(root, "items.rgba", t.item);
        meanRgba(root, "orb.rgba", t.orb);
        meanRgba(root, "arrow.rgba", t.arrow);
        meanRgba(root, "misc/underwater.rgba", t.underwater);
        // RenderGlobal's End sky: the texture times 0x282828
        byte[] es = Files.readAllBytes(new File(root, "out/assets/end_sky.rgb").toPath());
        double[] s3 = {0, 0, 0};
        for (int i = 0; i + 2 < es.length; i += 3)
        {
            s3[0] += es[i] & 255;
            s3[1] += es[i + 1] & 255;
            s3[2] += es[i + 2] & 255;
        }
        for (int k = 0; k < 3; ++k) t.endSky[k] = es.length >= 3 ? (float)(s3[k] / (double)(es.length / 3)) * (40.0f / 255.0f) : 10.0f;
    }

    static int find(String[] names, String n)
    {
        for (int i = 0; i < names.length; ++i) if (names[i].equals(n)) return i;
        return -1;
    }

    // ------------------------------------------------------------------ one cast's state (raycast.c struct rc)

    WorldClient w;
    LongHashMap chunks;
    double ox, oy, oz;
    double fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect;
    int eyeX, eyeY, eyeZ, liquid, range, dim, ch;
    boolean noSky, fancy;
    float[][] lm = new float[256][3];
    float[] sky = new float[3], fog = new float[3];
    boolean expFog, uw;
    float[] uwRgb = new float[3];
    float fs, fe, fd, skyEnd, fspan;
    int nent;
    double[][] ebox = new double[RC_ENTS][6];
    int[] ei0 = new int[RC_ENTS], ei1 = new int[RC_ENTS], ej0 = new int[RC_ENTS], ej1 = new int[RC_ENTS], elmi = new int[RC_ENTS];
    float[][] ergb = new float[RC_ENTS][];
    Tables t;

    /* the chunks looked up, direct mapped by (cx, cz) mod 32 (a cast's window is at most 22 chunks across); each walker
     * its own (the walkers read chunkMapping, which nothing writes while a row is captured) */
    int lastCx = Integer.MIN_VALUE, lastCz;
    Chunk lastC;
    final int[] slotX = new int[1024], slotZ = new int[1024];
    final Chunk[] slotC = new Chunk[1024];
    { java.util.Arrays.fill(slotX, Integer.MIN_VALUE); }

    Chunk chunk(int cx, int cz)
    {
        if (cx == lastCx && cz == lastCz) return lastC;
        int s = (cx & 31) << 5 | (cz & 31);
        if (slotX[s] != cx || slotZ[s] != cz)
        {
            slotX[s] = cx;
            slotZ[s] = cz;
            slotC[s] = (Chunk)chunks.getValueByKey(net.minecraft.world.ChunkCoordIntPair.chunkXZ2Int(cx, cz));
        }
        lastCx = cx;
        lastCz = cz;
        lastC = slotC[s];
        return lastC;
    }

    /** a walker sharing this cast's setup, with scratch and a chunk cache of its own */
    RayCast fork()
    {
        RayCast c = new RayCast();
        c.t = t;
        c.w = w;
        c.chunks = chunks;
        c.ox = ox; c.oy = oy; c.oz = oz;
        c.fx = fx; c.fy = fy; c.fz = fz; c.rx = rx; c.ry = ry; c.rz = rz; c.ux = ux; c.uy = uy; c.uz = uz; c.aspect = aspect;
        c.eyeX = eyeX; c.eyeY = eyeY; c.eyeZ = eyeZ; c.liquid = liquid; c.range = range; c.dim = dim; c.ch = ch;
        c.noSky = noSky; c.fancy = fancy;
        c.lm = lm; c.sky = sky; c.fog = fog; c.expFog = expFog; c.uw = uw; c.uwRgb = uwRgb;
        c.fs = fs; c.fe = fe; c.fd = fd; c.skyEnd = skyEnd; c.fspan = fspan;
        c.nent = nent; c.ebox = ebox; c.ei0 = ei0; c.ei1 = ei1; c.ej0 = ej0; c.ej1 = ej1; c.ergb = ergb; c.elmi = elmi;
        c.rays = rays;
        return c;
    }

    /** the walkers' threads (the cast's rows split among them: each pixel's walk is its own, the same bytes) */
    static java.util.concurrent.ExecutorService workers;
    static final int NTHREADS = Math.max(1, Math.min(8, Runtime.getRuntime().availableProcessors() / 2));

    static int liquidOf(int id) { return id == 8 || id == 9 ? 8 : id == 10 || id == 11 ? 10 : 0; }

    /** cell_ref: the id, and the metadata in meta[0] */
    int cell(int x, int y, int z, int[] meta)
    {
        meta[0] = 0;
        if (y < 0 || y > 255) return 0;
        Chunk c = chunk(x >> 4, z >> 4);
        if (c == null) return 0;
        ExtendedBlockStorage s = c.getBlockStorageArray()[y >> 4];
        if (s == null) return 0;
        int lx = x & 15, ly = y & 15, lz = z & 15;
        int id = s.getBlockLSBArray()[ly << 8 | lz << 4 | lx] & 255;
        NibbleArray hi = s.getBlockMSBArray();
        if (hi != null) id |= hi.get(lx, ly, lz) << 8;
        if (id != 0) meta[0] = s.getExtBlockMetadata(lx, ly, lz);
        return id;
    }

    /** light_ref (chunk_light over the client chunk): sky << 4 | block */
    int light(int x, int y, int z)
    {
        if (y < 0) return 0;
        if (y > 255) y = 255;
        Chunk c = chunk(x >> 4, z >> 4);
        if (c == null) return noSky ? 0 : 15 << 4;
        ExtendedBlockStorage s = c.getBlockStorageArray()[y >> 4];
        if (s == null) return !noSky && y >= c.heightMap[(z & 15) << 4 | (x & 15)] ? 15 << 4 : 0;
        int sk = noSky ? 0 : s.getExtSkylightValue(x & 15, y & 15, z & 15);
        return sk << 4 | s.getExtBlocklightValue(x & 15, y & 15, z & 15);
    }

    int biome(int x, int z)
    {
        Chunk c = chunk(x >> 4, z >> 4);
        return c != null ? c.getBiomeArray()[(z & 15) << 4 | (x & 15)] & 255 : 0;
    }

    final int[] m2 = new int[1];
    final float[] tmp = new float[3];

    /** tint_of: the block's colour multiplier on a face, 0..1 */
    float[] tintOf(int id, int meta, int side, int x, int y, int z)
    {
        int tn = t.tint[id];
        if (tn == TN_NONE) return WHITE;
        int bio = biome(x, z);
        switch (tn)
        {
        case TN_GRASS_TOP: return side == 1 ? t.grass[bio] : WHITE;
        case TN_GRASS: return t.grass[bio];
        case TN_FOLIAGE: return t.foliage[bio];
        case TN_OLDLEAF:
            if ((meta & 3) == 1) { rgb01(6396257, tmp); return tmp; }
            if ((meta & 3) == 2) { rgb01(8431445, tmp); return tmp; }
            return t.foliage[bio];
        case TN_WATER: return t.water[bio];
        case TN_TALLGRASS: return meta == 0 ? WHITE : t.grass[bio];
        case TN_STEM: rgb01(meta * 32 << 16 | (255 - meta * 8) << 8 | meta * 4, tmp); return tmp;
        case TN_DPLANT:
        {
            int v = meta & 7;
            if ((meta & 8) != 0)
            {
                cell(x, y - 1, z, m2);
                v = m2[0] & 7;
            }
            return v == 2 || v == 3 ? t.grass[bio] : WHITE;
        }
        }
        return WHITE;
    }

    /** exp_neg_f: e to the x (x <= 0) as a float, raycast.c's arithmetic */
    static float expNeg(double x)
    {
        if (!(x > -40.0)) return 0.0f;
        int n = (int)(x * 1.4426950408889634 - 0.5);
        double r = x - (double)n * 0.6931471805599453;
        double r2 = r * r, r4 = r2 * r2;
        double a = 1.0 + r, b = 0.5 + r * (1.0 / 6.0), c = 1.0 / 24.0 + r * (1.0 / 120.0), d = 1.0 / 720.0 + r * (1.0 / 5040.0);
        double p = (a + r2 * b) + r4 * ((c + r2 * d) + r4 * (1.0 / 40320.0));
        int lo = (int)(Double.doubleToRawLongBits(p) & 0x1fffffffL);
        if (Integer.compareUnsigned(lo - ((1 << 28) - 4096), 8192) <= 0)
            p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6.0 + r * (1.0 / 24.0 + r * (1.0 / 120.0 + r * (1.0 / 720.0 + r * (1.0 / 5040.0 + r * (1.0 / 40320.0))))))));
        double s = Double.longBitsToDouble((long)(n + 1023) << 52);
        return (float)(p * s);
    }

    /** lit: a lit, fogged colour (0..255) into out */
    void lit(float r, float g, float b, float[] tint, int sky, int block, int emit, float shade, double depth, float[] out)
    {
        if (block < emit) block = emit;
        int li = sky * 16 + block;
        float rt0 = r * tint[0], rt1 = g * tint[1], rt2 = b * tint[2];
        float f;
        if (expFog) f = expNeg(-(double)fd * depth);
        else
        {
            f = (fe - (float)depth) * fspan;
            f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        }
        float[] l = lm[li];
        out[0] = rt0 * l[0] * shade;
        out[1] = rt1 * l[1] * shade;
        out[2] = rt2 * l[2] * shade;
        out[0] = out[0] * f + fog[0] * (1.0f - f);
        out[1] = out[1] * f + fog[1] * (1.0f - f);
        out[2] = out[2] * f + fog[2] * (1.0f - f);
    }

    /** a face's texel at local (lx, ly, lz) in the cell */
    int texel(int spr, int face, double lx, double ly, double lz)
    {
        double u, v;
        switch (face)
        {
        case 0: case 1: u = lx; v = lz; break;
        case 2: u = 1.0 - lx; v = 1.0 - ly; break;
        case 3: u = lx; v = 1.0 - ly; break;
        case 4: u = lz; v = 1.0 - ly; break;
        default: u = 1.0 - lz; v = 1.0 - ly; break;
        }
        int iu = (int)(u * 16.0), iv = (int)(v * 16.0);
        iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
        iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
        return t.tex[spr][iv * 16 + iu];
    }

    static int texA(int tt) { return tt >>> 24; }

    static boolean texPass(int tt) { return (tt >>> 24) > 25; }

    static int entryFace(int axis, int s)
    {
        return axis == 0 ? (s > 0 ? 4 : 5) : axis == 1 ? (s > 0 ? 0 : 1) : (s > 0 ? 2 : 3);
    }

    // the walk's colour so far (acc, trans) and what it ended on (col)
    final float[] acc = new float[3], col = new float[3];
    final int[] mmS = new int[1];
    float trans;

    void thruAdd(float a, float[] c)
    {
        acc[0] += trans * a * c[0];
        acc[1] += trans * a * c[1];
        acc[2] += trans * a * c[2];
        trans *= 1.0f - a;
    }

    /** the water's surface seen from below, stepping up into cell (ix, iy, iz) at t */
    void surfaceUp(int ix, int iy, int iz, double t_, double dx, double dz, double fwd)
    {
        int l = light(ix, iy - 1, iz);
        int tw = texel(t.waterSpr, 1, ox + dx * t_ - (double)ix, 0.0, oz + dz * t_ - (double)iz);
        float[] tint = tintOf(9, 0, 1, ix, iy - 1, iz);
        float[] c = new float[3];
        lit((float)(tw & 255), (float)(tw >> 8 & 255), (float)(tw >> 16 & 255), tint, l >> 4, l & 15, 0, SHADE[1], t_ * fwd, c);
        float a = (float)texA(tw) / 255.0f;
        thruAdd(a, c);
    }

    final double[] o3 = new double[3], i3 = new double[3];
    final int[] s3 = new int[3];
    static final int[] BI = {1, 4, 2, 5, 0, 3};

    /** walk (fast 0): one ray from the eye; the pixel's bytes into out at o */
    void walk(double dx, double dy, double dz, double fwd, double idx, double idy, double idz, double tx, double ty, double tz,
              double te, int ent, int eface, byte[] out, int o)
    {
        int ix = eyeX, iy = eyeY, iz = eyeZ;
        final int sx = (dx > 0 ? 1 : 0) - (dx < 0 ? 1 : 0), sy = (dy > 0 ? 1 : 0) - (dy < 0 ? 1 : 0), sz = (dz > 0 ? 1 : 0) - (dz < 0 ? 1 : 0);
        final double range = (double)this.range;
        acc[0] = acc[1] = acc[2] = 0.0f;
        trans = 1.0f;
        boolean inWater = false;
        int cull = -1;
        final int eyeLiquid = liquid;
        boolean eyeWasLiquid = eyeLiquid != 0;
        int axis = 0, id, meta;
        double t_ = 0.0;
        final int[] mm = mmS;
        int end; // 0 done, 1 entity, 2 miss
        walk:
        for (;;)
        {
            // dda_step
            if (tx <= ty && tx <= tz)
            {
                t_ = tx;
                ix += sx;
                tx = ((double)(ix + (sx > 0 ? 1 : 0)) - ox) * idx;
                axis = 0;
            }
            else if (ty <= tz)
            {
                t_ = ty;
                iy += sy;
                ty = ((double)(iy + (sy > 0 ? 1 : 0)) - oy) * idy;
                axis = 1;
            }
            else
            {
                t_ = tz;
                iz += sz;
                tz = ((double)(iz + (sz > 0 ? 1 : 0)) - oz) * idz;
                axis = 2;
            }
            if (t_ >= te) { end = 1; break; }
            if (!(t_ <= range) || iy > 255 || iy < 0) { end = 2; break; }
            id = cell(ix, iy, iz, mm);
            meta = mm[0];
            // block:
            final int px = ix - (axis == 0 ? sx : 0), py = iy - (axis == 1 ? sy : 0), pz = iz - (axis == 2 ? sz : 0);
            final int face = entryFace(axis, axis == 0 ? sx : axis == 1 ? sy : sz);
            if (eyeWasLiquid && eyeLiquid == 8 && liquidOf(id) != 8 && axis == 1 && sy > 0 && t.waterSpr != NONE)
                surfaceUp(ix, iy, iz, t_, dx, dz, fwd);
            eyeWasLiquid = eyeLiquid != 0 && liquidOf(id) == eyeLiquid;
            final int kind = t.kind[id];
            if (kind == RK_AIR || (eyeLiquid != 0 && liquidOf(id) == eyeLiquid))
            {
                if (kind == RK_AIR) inWater = false;
                cull = -1;
                continue;
            }
            final double texit = tx < ty ? (tx < tz ? tx : tz) : (ty < tz ? ty : tz);
            double hx = ox + dx * t_ - (double)ix, hy = oy + dy * t_ - (double)iy, hz = oz + dz * t_ - (double)iz;
            if (kind == RK_WATER || kind == RK_LAVA)
            {
                if (kind == RK_WATER && inWater) continue;
                int above = iy < 255 ? cell(ix, iy + 1, iz, m2) : 0;
                double h = liquidOf(above) == liquidOf(id) ? 1.0 : 1.0 - (double)((meta >= 8 ? 0 : meta) + 1) / 9.0;
                double th = t_;
                int hf = face;
                if (hy > h)
                {
                    if (sy >= 0) { cull = -1; continue; }
                    th = ((double)iy + h - oy) * idy;
                    if (th > texit) { cull = -1; continue; }
                    hf = 1;
                    hx = ox + dx * th - (double)ix;
                    hy = h;
                    hz = oz + dz * th - (double)iz;
                }
                if (th >= te) { end = 1; break; }
                int l = light(ix, iy, iz);
                int spr = kind == RK_WATER ? t.waterSpr : t.lavaSpr;
                int tt = spr != NONE ? texel(spr, hf, hx, hy, hz) : 0xff808080;
                float[] tint = tintOf(id, meta, hf, ix, iy, iz);
                lit((float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, l >> 4, l & 15, t.emit[id], SHADE[hf], th * fwd, col);
                if (kind == RK_LAVA) { end = 0; break; }
                float a = (float)texA(tt) / 255.0f;
                thruAdd(a, col);
                inWater = true;
                cull = -1;
                continue;
            }
            inWater = false;
            int key = id | meta << 12;
            if (t.cullSame[id] != 0 && key == cull) continue;
            cull = -1;
            final int[] faces = t.face[fancy ? 1 : 0];
            final int fo = (id * 16 + meta) * 6;
            if (kind == RK_CUBE)
            {
                int spr = faces[fo + face];
                int tt = spr != NONE ? texel(spr, face, hx, hy, hz) : 0;
                if (texPass(tt))
                {
                    int l = light(px, py, pz);
                    float[] tint = tintOf(id, meta, face, ix, iy, iz);
                    lit((float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, l >> 4, l & 15, t.emit[id], SHADE[face], t_ * fwd, col);
                    end = 0;
                    break;
                }
                if (t.cullSame[id] != 0) cull = key;
            }
            else if (kind == RK_BOX)
            {
                final double[] b = t.bounds;
                o3[0] = ox - (double)ix;
                o3[1] = oy - (double)iy;
                o3[2] = oz - (double)iz;
                i3[0] = idx;
                i3[1] = idy;
                i3[2] = idz;
                s3[0] = sx;
                s3[1] = sy;
                s3[2] = sz;
                double tn = Double.NEGATIVE_INFINITY, tf = Double.POSITIVE_INFINITY;
                int ax = -1;
                boolean miss = false;
                for (int a = 0; a < 3 && !miss; ++a)
                {
                    if (s3[a] == 0)
                    {
                        if (o3[a] < b[fo + a] || o3[a] > b[fo + a + 3]) miss = true;
                        continue;
                    }
                    double t0 = (b[fo + a] - o3[a]) * i3[a], t1 = (b[fo + a + 3] - o3[a]) * i3[a];
                    if (t0 > t1) { double s = t0; t0 = t1; t1 = s; }
                    if (t0 > tn) { tn = t0; ax = a; }
                    if (t1 < tf) tf = t1;
                }
                if (!miss && ax >= 0 && tn <= tf && tn > 0.0 && tn <= texit)
                {
                    if (tn >= te) { end = 1; break; }
                    int bf = entryFace(ax, s3[ax]);
                    int spr = faces[fo + bf];
                    double bx = ox + dx * tn - (double)ix, by = oy + dy * tn - (double)iy, bz = oz + dz * tn - (double)iz;
                    int tt = spr != NONE ? texel(spr, bf, bx, by, bz) : 0;
                    if (texPass(tt))
                    {
                        double bd = b[fo + BI[bf]];
                        boolean edge = (bf & 1) != 0 ? bd == 1.0 : bd == 0.0;
                        int l = edge ? light(px, py, pz) : light(ix, iy, iz);
                        float[] tint = tintOf(id, meta, bf, ix, iy, iz);
                        lit((float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, l >> 4, l & 15, t.emit[id], SHADE[bf], tn * fwd, col);
                        end = 0;
                        break;
                    }
                }
            }
            else
            {
                // RK_CROSS: the planes x = z and x + z = 1, 0.05 to 0.95 across, the cell's height
                double lx0 = ox - (double)ix, lz0 = oz - (double)iz, ly0 = oy - (double)iy;
                double tp0 = Double.POSITIVE_INFINITY, tp1 = Double.POSITIVE_INFINITY;
                double a1 = dx - dz, a2 = dx + dz;
                if (a1 != 0.0) tp0 = -(lx0 - lz0) / a1;
                if (a2 != 0.0) tp1 = (1.0 - lx0 - lz0) / a2;
                if (tp1 < tp0) { double s = tp0; tp0 = tp1; tp1 = s; }
                int spr = faces[fo];
                if (id == 175)
                {
                    int v = meta & 7;
                    if ((meta & 8) != 0)
                    {
                        int below = cell(ix, iy - 1, iz, m2);
                        v = below == 175 ? m2[0] & 7 : 6;
                    }
                    spr = v < 6 ? ((meta & 8) != 0 ? t.dpHigh[v] : t.dpLow[v]) : NONE;
                }
                for (int q = 0; q < 2 && spr != NONE; ++q)
                {
                    double tq = q == 0 ? tp0 : tp1;
                    if (!(tq >= t_ && tq <= texit)) continue;
                    double qx = lx0 + dx * tq, qy = ly0 + dy * tq;
                    if (qx < 0.05 || qx > 0.95 || qy < 0.0 || qy > 1.0) continue;
                    if (tq >= te) { end = 1; break walk; }
                    double u = (qx - 0.05) / 0.9;
                    int iu = (int)(u * 16.0), iv = (int)((1.0 - qy) * 16.0);
                    iu = iu < 0 ? 0 : iu > 15 ? 15 : iu;
                    iv = iv < 0 ? 0 : iv > 15 ? 15 : iv;
                    int tt = t.tex[spr][iv * 16 + iu];
                    if (!texPass(tt)) continue;
                    int l = light(ix, iy, iz);
                    float[] tint = tintOf(id, meta, 2, ix, iy, iz);
                    lit((float)(tt & 255), (float)(tt >> 8 & 255), (float)(tt >> 16 & 255), tint, l >> 4, l & 15, t.emit[id], 1.0f, tq * fwd, col);
                    end = 0;
                    break walk;
                }
            }
        }
        if (end == 2 && ent >= 0) end = 1;
        if (rays != null)
        {
            // raycast_rays' record: kind 0 a block (its cell, axis, distance), 1 the sky or fog, 2 an entity
            int[] r = rays[pixAt];
            r[0] = end == 0 ? 0 : end == 2 ? 1 : 2;
            r[1] = end == 0 ? ix : end == 1 ? ent : 0;
            r[2] = end == 0 ? iy : 0;
            r[3] = end == 0 ? iz : 0;
            r[4] = end == 0 ? axis : end == 1 ? eface : 0;
            r[5] = end == 0 ? (int)Math.floor(t_ * 256.0) : end == 1 ? (int)Math.floor(te * 256.0) : 0;
        }
        if (end == 1)
        {
            lit(ergb[ent][0], ergb[ent][1], ergb[ent][2], WHITE, elmi[ent] >> 4, elmi[ent] & 15, 0, SHADE[eface], te * fwd, col);
        }
        else if (end == 2)
        {
            if (expFog || dim == -1) System.arraycopy(fog, 0, col, 0, 3);
            else if (dim == 1) System.arraycopy(t.endSky, 0, col, 0, 3);
            else if (dy <= 0.0) System.arraycopy(fog, 0, col, 0, 3);
            else
            {
                double z = 16.0 / dy * fwd;
                float f = (skyEnd - (float)z) / skyEnd;
                f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                col[0] = sky[0] * f + fog[0] * (1.0f - f);
                col[1] = sky[1] * f + fog[1] * (1.0f - f);
                col[2] = sky[2] * f + fog[2] * (1.0f - f);
            }
        }
        // pixel_out
        int[] bb = new int[3];
        for (int c = 0; c < 3; ++c)
        {
            float v = acc[c] + trans * col[c];
            if (uw) v = v * 0.5f + uwRgb[c] * 0.5f;
            bb[c] = v >= 255.0f ? 255 : v <= 0.0f ? 0 : ((int)(v + 0.5f)) & 255;
        }
        if (ch == 1) out[o] = (byte)((77 * bb[0] + 150 * bb[1] + 29 * bb[2] + 128) >>> 8);
        else
        {
            out[o] = (byte)bb[0];
            out[o + 1] = (byte)bb[1];
            out[o + 2] = (byte)bb[2];
        }
    }

    // ------------------------------------------------------------------ the frame's light, sky and fog (rc_light_port)

    /** what rc_light_port reads of rs_in (build_rs's; every other input 0) */
    int rd, wtDim;
    long wt;
    float rain, thu, gamma, yaw, pit, fc1;
    int skytemp, mat, brf;
    double py;
    float[] lbt = new float[16];

    static float celestial(int dim, long wt, float pt)
    {
        if (dim == -1) return 0.5f;
        if (dim == 1) return 0.0f;
        int v4 = (int)(wt % 24000L);
        float v5 = ((float)v4 + pt) / 24000.0f - 0.25f;
        if (v5 < 0.0f) v5 = v5 + 1.0f;
        if (v5 > 1.0f) v5 = v5 - 1.0f;
        float v6 = v5;
        float v7 = (float)((StrictMath.cos((double)v5 * Math.PI) + 1.0) / 2.0);
        v5 = 1.0f - v7;
        v5 = v6 + (v5 - v6) / 3.0f;
        return v5;
    }

    float sunBrightness()
    {
        float a = celestial(dim, wt, 1.0f);
        float v3 = 1.0f - (MathHelper.cos(a * PIF * 2.0f) * 2.0f + 0.2f);
        if (v3 < 0.0f) v3 = 0.0f;
        if (v3 > 1.0f) v3 = 1.0f;
        v3 = 1.0f - v3;
        v3 = (float)((double)v3 * (1.0 - (double)(rainAt() * 5.0f) / 16.0));
        v3 = (float)((double)v3 * (1.0 - (double)(thunderAt() * 5.0f) / 16.0));
        return v3 * 0.8f + 0.2f;
    }

    /* rc_rain and rc_thunder at pt 1 with prev = the value (build_rs) */
    float rainAt() { return rain + (rain - rain) * 1.0f; }

    float thunderAt() { return (thu + (thu - thu) * 1.0f) * rainAt(); }

    /** rc_sunrise: r[0..3] and true, or false */
    boolean sunrise(float[] r)
    {
        float a = celestial(dim, wt, 1.0f);
        float v4 = MathHelper.cos(a * PIF * 2.0f) - 0.0f;
        float v5 = -0.0f;
        if (v4 >= v5 - 0.4f && v4 <= v5 + 0.4f)
        {
            float v6 = (v4 - v5) / 0.4f * 0.5f + 0.5f;
            float v7 = 1.0f - (1.0f - MathHelper.sin(v6 * PIF)) * 0.99f;
            v7 = v7 * v7;
            r[0] = v6 * 0.3f + 0.7f;
            r[1] = v6 * v6 * 0.7f + 0.2f;
            r[2] = v6 * v6 * 0.0f + 0.2f;
            r[3] = v7;
            return true;
        }
        return false;
    }

    void providerFog(double[] v)
    {
        if (dim == -1)
        {
            v[0] = 0.20000000298023224;
            v[1] = 0.029999999329447746;
            v[2] = 0.029999999329447746;
            return;
        }
        if (dim == 1)
        {
            v[0] = (float)(0xA080A0 >> 16 & 255) / 255.0f * 0.15f;
            v[1] = (float)(0xA080A0 >> 8 & 255) / 255.0f * 0.15f;
            v[2] = (float)(0xA080A0 & 255) / 255.0f * 0.15f;
            return;
        }
        float a = celestial(dim, wt, 1.0f);
        float v3 = MathHelper.cos(a * PIF * 2.0f) * 2.0f + 0.5f;
        if (v3 < 0.0f) v3 = 0.0f;
        if (v3 > 1.0f) v3 = 1.0f;
        float v4 = 0.7529412f;
        float v5 = 0.84705883f;
        float v6 = 1.0f;
        v4 = v4 * (v3 * 0.94f + 0.06f);
        v5 = v5 * (v3 * 0.94f + 0.06f);
        v6 = v6 * (v3 * 0.91f + 0.09f);
        v[0] = (double)v4;
        v[1] = (double)v5;
        v[2] = (double)v6;
    }

    void skyColor(double[] v)
    {
        float a = celestial(dim, wt, 1.0f);
        float v4 = MathHelper.cos(a * PIF * 2.0f) * 2.0f + 0.5f;
        if (v4 < 0.0f) v4 = 0.0f;
        if (v4 > 1.0f) v4 = 1.0f;
        int tc = dim == 1 ? 0 : skytemp;
        float v11 = (float)(tc >> 16 & 255) / 255.0f;
        float v12 = (float)(tc >> 8 & 255) / 255.0f;
        float v13 = (float)(tc & 255) / 255.0f;
        v11 = v11 * v4;
        v12 = v12 * v4;
        v13 = v13 * v4;
        float v14 = rainAt();
        float v15, v16;
        if (v14 > 0.0f)
        {
            v15 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.6f;
            v16 = 1.0f - v14 * 0.75f;
            v11 = v11 * v16 + v15 * (1.0f - v16);
            v12 = v12 * v16 + v15 * (1.0f - v16);
            v13 = v13 * v16 + v15 * (1.0f - v16);
        }
        v15 = thunderAt();
        if (v15 > 0.0f)
        {
            v16 = (v11 * 0.3f + v12 * 0.59f + v13 * 0.11f) * 0.2f;
            float v17 = 1.0f - v15 * 0.75f;
            v11 = v11 * v17 + v16 * (1.0f - v17);
            v12 = v12 * v17 + v16 * (1.0f - v17);
            v13 = v13 * v17 + v16 * (1.0f - v17);
        }
        v[0] = (double)v11;
        v[1] = (double)v12;
        v[2] = (double)v13;
    }

    /** rc_light_port into the cast's lightmap (as floats), sky, fog and fog form */
    void lightPort()
    {
        double[] skyd = new double[3];
        skyColor(skyd);
        float sun1 = sunBrightness();
        int[] lmi = new int[256];
        for (int i = 0; i < 256; ++i)
        {
            float v4 = sun1 * 0.95f + 0.05f;
            float v5 = lbt[i / 16] * v4;
            float v6 = lbt[i % 16] * (0.0f * 0.1f + 1.5f);
            float v7 = v5 * (sun1 * 0.65f + 0.35f);
            float v8 = v5 * (sun1 * 0.65f + 0.35f);
            float v11 = v6 * ((v6 * 0.6f + 0.4f) * 0.6f + 0.4f);
            float v12 = v6 * (v6 * v6 * 0.6f + 0.4f);
            float v13 = v7 + v6;
            float v14 = v8 + v11;
            float v15 = v5 + v12;
            v13 = v13 * 0.96f + 0.03f;
            v14 = v14 * 0.96f + 0.03f;
            v15 = v15 * 0.96f + 0.03f;
            if (dim == 1)
            {
                v13 = 0.22f + v6 * 0.75f;
                v14 = 0.28f + v11 * 0.75f;
                v15 = 0.25f + v12 * 0.75f;
            }
            if (v13 > 1.0f) v13 = 1.0f;
            if (v14 > 1.0f) v14 = 1.0f;
            if (v15 > 1.0f) v15 = 1.0f;
            float v16 = gamma;
            float v17 = 1.0f - v13;
            float v18 = 1.0f - v14;
            float v19 = 1.0f - v15;
            v17 = 1.0f - v17 * v17 * v17 * v17;
            v18 = 1.0f - v18 * v18 * v18 * v18;
            v19 = 1.0f - v19 * v19 * v19 * v19;
            v13 = v13 * (1.0f - v16) + v17 * v16;
            v14 = v14 * (1.0f - v16) + v18 * v16;
            v15 = v15 * (1.0f - v16) + v19 * v16;
            v13 = v13 * 0.96f + 0.03f;
            v14 = v14 * 0.96f + 0.03f;
            v15 = v15 * 0.96f + 0.03f;
            if (v13 > 1.0f) v13 = 1.0f;
            if (v14 > 1.0f) v14 = 1.0f;
            if (v15 > 1.0f) v15 = 1.0f;
            if (v13 < 0.0f) v13 = 0.0f;
            if (v14 < 0.0f) v14 = 0.0f;
            if (v15 < 0.0f) v15 = 0.0f;
            int v21 = (int)(v13 * 255.0f);
            int v22 = (int)(v14 * 255.0f);
            int v23 = (int)(v15 * 255.0f);
            lmi[i] = 255 << 24 | v21 << 16 | v22 << 8 | v23;
        }
        // EntityRenderer.updateFogColor
        float fcr, fcg, fcb;
        {
            float v4 = 0.25f + 0.75f * (float)rd / 16.0f;
            v4 = 1.0f - (float)StrictMath.pow((double)v4, 0.25);
            double[] sk = new double[3];
            skyColor(sk);
            float v6 = (float)sk[0];
            float v7 = (float)sk[1];
            float v8 = (float)sk[2];
            double[] fg = new double[3];
            providerFog(fg);
            fcr = (float)fg[0];
            fcg = (float)fg[1];
            fcb = (float)fg[2];
            float v11;
            if (rd >= 4)
            {
                float v3 = MathHelper.sin(-yaw * DEG2RAD - PIF);
                float v4b = -MathHelper.cos(-pit * DEG2RAD);
                double look0 = (double)(v3 * v4b);
                float angr = celestial(dim, wt, 1.0f) * PIF * 2.0f;
                float x = MathHelper.sin(angr) > 0.0f ? -1.0f : 1.0f;
                v11 = (float)(look0 * (double)x);
                if (v11 < 0.0f) v11 = 0.0f;
                if (v11 > 0.0f)
                {
                    float[] rise = new float[4];
                    if (sunrise(rise))
                    {
                        v11 = v11 * rise[3];
                        fcr = fcr * (1.0f - v11) + rise[0] * v11;
                        fcg = fcg * (1.0f - v11) + rise[1] * v11;
                        fcb = fcb * (1.0f - v11) + rise[2] * v11;
                    }
                }
            }
            fcr = fcr + (v6 - fcr) * v4;
            fcg = fcg + (v7 - fcg) * v4;
            fcb = fcb + (v8 - fcb) * v4;
            float v19 = rainAt();
            if (v19 > 0.0f)
            {
                v11 = 1.0f - v19 * 0.5f;
                float v20 = 1.0f - v19 * 0.4f;
                fcr = fcr * v11;
                fcg = fcg * v11;
                fcb = fcb * v20;
            }
            v11 = thunderAt();
            if (v11 > 0.0f)
            {
                float v20 = 1.0f - v11 * 0.5f;
                fcr = fcr * v20;
                fcg = fcg * v20;
                fcb = fcb * v20;
            }
            if (mat == 1)
            {
                float v22 = (float)0 * 0.2f;
                fcr = 0.02f + v22;
                fcg = 0.02f + v22;
                fcb = 0.2f + v22;
            }
            else if (mat == 2)
            {
                fcr = 0.6f;
                fcg = 0.1f;
                fcb = 0.0f;
            }
            float v22 = fc1 + (fc1 - fc1) * 1.0f;
            fcr = fcr * v22;
            fcg = fcg * v22;
            fcb = fcb * v22;
            double v14 = (py + (py - py) * (double)1.0f) * 0.03125;
            if (v14 < 1.0)
            {
                if (v14 < 0.0) v14 = 0.0;
                v14 = v14 * v14;
                fcr = (float)((double)fcr * v14);
                fcg = (float)((double)fcg * v14);
                fcb = (float)((double)fcb * v14);
            }
        }
        float far = (float)(rd * 16);
        // EntityRenderer.setupFog(0, pt)
        int gfogm;
        float gfogs = 0.0f, gfoge = 0.0f, gfogd = 0.0f;
        if (mat == 1)
        {
            gfogm = 0x0800;
            gfogd = 0.1f - (float)0 * 0.03f;
        }
        else if (mat == 2)
        {
            gfogm = 0x0800;
            gfogd = 2.0f;
        }
        else
        {
            float v6 = far;
            if (dim == 0)
            {
                double v10 = (double)((brf & 15728640) >> 20) / 16.0 + (py + (py - py) * (double)1.0f + 4.0) / 32.0;
                if (v10 < 1.0)
                {
                    if (v10 < 0.0) v10 = 0.0;
                    v10 = v10 * v10;
                    float v9 = 100.0f * (float)v10;
                    if (v9 < 5.0f) v9 = 5.0f;
                    if (v6 > v9) v6 = v9;
                }
            }
            gfogm = 0x2601;
            gfogs = v6 * 0.75f;
            gfoge = v6;
            if (dim == -1 || dim == 1)
            {
                gfogs = v6 * 0.05f;
                float e = v6 < 192.0f ? v6 : 192.0f;
                gfoge = e * 0.5f;
            }
        }
        // setup's part: the lightmap as floats, sky and fog in 0..255, the fog's form
        for (int i = 0; i < 256; ++i)
        {
            lm[i][0] = (float)(lmi[i] >> 16 & 255) / 255.0f;
            lm[i][1] = (float)(lmi[i] >> 8 & 255) / 255.0f;
            lm[i][2] = (float)(lmi[i] & 255) / 255.0f;
        }
        sky[0] = (float)skyd[0] * 255.0f;
        sky[1] = (float)skyd[1] * 255.0f;
        sky[2] = (float)skyd[2] * 255.0f;
        fog[0] = fcr * 255.0f;
        fog[1] = fcg * 255.0f;
        fog[2] = fcb * 255.0f;
        for (int c = 0; c < 3; ++c)
        {
            sky[c] = sky[c] < 0.0f ? 0.0f : sky[c] > 255.0f ? 255.0f : sky[c];
            fog[c] = fog[c] < 0.0f ? 0.0f : fog[c] > 255.0f ? 255.0f : fog[c];
        }
        expFog = gfogm == 0x0800;
        fs = gfogs;
        fe = gfoge;
        fd = gfogd;
        if (!(fe > fs)) fe = fs + 1.0f;
        fspan = 1.0f / (fe - fs);
        skyEnd = far * 0.8f;
    }

    /** BiomeGenBase.getSkyColorByTemp (java.awt.Color.HSBtoRGB) */
    static int skyColorByTemp(float f)
    {
        f /= 3.0F;
        if (f < -1.0F) f = -1.0F;
        if (f > 1.0F) f = 1.0F;
        return hsbToRgb(0.62222224F - f * 0.05F, 0.5F + f * 0.1F, 1.0F);
    }

    static int hsbToRgb(float hue, float sat, float bri)
    {
        int r = 0, g = 0, b = 0;
        if (sat == 0.0F) r = g = b = (int)(bri * 255.0F + 0.5F);
        else
        {
            float h = (hue - (float)Math.floor(hue)) * 6.0F;
            float f = h - (float)Math.floor(h);
            float p = bri * (1.0F - sat);
            float q = bri * (1.0F - sat * f);
            float tt = bri * (1.0F - (sat * (1.0F - f)));
            switch ((int)h)
            {
            case 0: r = (int)(bri * 255.0F + 0.5F); g = (int)(tt * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
            case 1: r = (int)(q * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(p * 255.0F + 0.5F); break;
            case 2: r = (int)(p * 255.0F + 0.5F); g = (int)(bri * 255.0F + 0.5F); b = (int)(tt * 255.0F + 0.5F); break;
            case 3: r = (int)(p * 255.0F + 0.5F); g = (int)(q * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
            case 4: r = (int)(tt * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(bri * 255.0F + 0.5F); break;
            case 5: r = (int)(bri * 255.0F + 0.5F); g = (int)(p * 255.0F + 0.5F); b = (int)(q * 255.0F + 0.5F); break;
            }
        }
        return 0xff000000 | r << 16 | g << 8 | b;
    }

    // ------------------------------------------------------------------ entities (raycast.c add_ent, entities)

    void addEnt(AxisAlignedBB bx, float[] rgb)
    {
        if (nent >= RC_ENTS) return;
        double cx = (bx.minX + bx.maxX) * 0.5, cy = (bx.minY + bx.maxY) * 0.5, cz = (bx.minZ + bx.maxZ) * 0.5;
        double ddx = (cx - 0.0) - ox, ddy = (cy - 0.0) - oy, ddz = (cz - 0.0) - oz;
        double rr = (double)range + 2.0;
        if (ddx * ddx + ddy * ddy + ddz * ddz > rr * rr) return;
        double[] e = ebox[nent];
        e[0] = bx.minX - 0.0;
        e[1] = bx.minY - 0.0;
        e[2] = bx.minZ - 0.0;
        e[3] = bx.maxX - 0.0;
        e[4] = bx.maxY - 0.0;
        e[5] = bx.maxZ - 0.0;
        if (!(e[3] > e[0] && e[4] > e[1] && e[5] > e[2])) return;
        ergb[nent] = rgb;
        int l = light((int)Math.floor(cx), (int)Math.floor(bx.minY + (bx.maxY - bx.minY) * 0.66), (int)Math.floor(cz));
        elmi[nent] = (l >> 4) * 16 + (l & 15);
        double i0 = Double.POSITIVE_INFINITY, i1 = Double.NEGATIVE_INFINITY, j0 = Double.POSITIVE_INFINITY, j1 = Double.NEGATIVE_INFINITY;
        boolean behind = false;
        int ahead = 0;
        for (int c = 0; c < 8; ++c)
        {
            double px = e[(c & 1) != 0 ? 3 : 0] - ox, py_ = e[(c & 2) != 0 ? 4 : 1] - oy, pz = e[(c & 4) != 0 ? 5 : 2] - oz;
            double z = px * fx + py_ * fy + pz * fz;
            if (z < 0.05) { behind = true; continue; }
            ++ahead;
            double x = (px * rx + py_ * ry + pz * rz) / z, y = (px * ux + py_ * uy + pz * uz) / z;
            double ii = (x / (TAN_HALF * aspect) + 1.0) * 0.5 * 4096.0, jj = (1.0 - y / TAN_HALF) * 0.5 * 4096.0;
            if (ii < i0) i0 = ii;
            if (ii > i1) i1 = ii;
            if (jj < j0) j0 = jj;
            if (jj > j1) j1 = jj;
        }
        if (ahead == 0) return;
        if (behind)
        {
            ei0[nent] = ej0[nent] = Integer.MIN_VALUE / 2;
            ei1[nent] = ej1[nent] = Integer.MAX_VALUE / 2;
        }
        else
        {
            ei0[nent] = (int)Math.floor(i0) - 1;
            ei1[nent] = (int)-Math.floor(-i1) + 1;
            ej0[nent] = (int)Math.floor(j0) - 1;
            ej1[nent] = (int)-Math.floor(-j1) + 1;
        }
        nent++;
    }

    float[] mobRgb(Entity e)
    {
        for (int k = 0; k < MOB_CLASS.length; ++k) if (MOB_CLASS[k].isInstance(e)) return t.mob[k];
        return t.unknown;
    }

    void entities(Minecraft mc, int w_, int h_)
    {
        nent = 0;
        for (Object o : w.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (e == mc.thePlayer || e.isDead || e instanceof EntityDragon) continue;
            boolean fire = e instanceof EntityLargeFireball || e instanceof EntitySmallFireball;
            if (!(e instanceof EntityLivingBase) && !fire) continue;
            addEnt(e.boundingBox, fire ? t.fireball : mobRgb(e));
        }
        // the dragon after the living copies (raycast.c: the C client holds it apart, as a pick object)
        for (Object o : w.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (e instanceof EntityDragon && !e.isDead) addEnt(e.boundingBox, t.unknown);
        }
        IntegratedServer s = mc.getIntegratedServer();
        EntityPlayerMP sp = s != null && !s.getConfigurationManager().playerEntityList.isEmpty()
            ? (EntityPlayerMP)s.getConfigurationManager().playerEntityList.get(0) : null;
        WorldServer ws = sp != null ? s.worldServerForDimension(sp.dimension) : null;
        if (ws != null)
            for (Object o : ws.loadedEntityList)
            {
                Entity e = (Entity)o;
                if (e.isDead) continue;
                float[] rgb = e instanceof EntityItem ? t.item : e instanceof EntityXPOrb ? t.orb : e instanceof EntityArrow ? t.arrow : null;
                if (rgb == null) continue;
                addEnt(e.boundingBox, rgb);
            }
        // the rectangles from 1/4096 of the frame to pixels; one off the frame goes
        int kept = 0;
        for (int e = 0; e < nent; ++e)
        {
            if (ei0[e] > Integer.MIN_VALUE / 4)
            {
                ei0[e] = (int)Math.floor((double)ei0[e] / 4096.0 * w_ - 0.5) - 1;
                ei1[e] = (int)Math.ceil((double)ei1[e] / 4096.0 * w_ - 0.5) + 1;
                ej0[e] = (int)Math.floor((double)ej0[e] / 4096.0 * h_ - 0.5) - 1;
                ej1[e] = (int)Math.ceil((double)ej1[e] / 4096.0 * h_ - 0.5) + 1;
                if (ei1[e] < 0 || ei0[e] >= w_ || ej1[e] < 0 || ej0[e] >= h_) continue;
            }
            if (kept != e)
            {
                double[] sw = ebox[kept];
                ebox[kept] = ebox[e];
                ebox[e] = sw;
                ei0[kept] = ei0[e];
                ei1[kept] = ei1[e];
                ej0[kept] = ej0[e];
                ej1[kept] = ej1[e];
                ergb[kept] = ergb[e];
                elmi[kept] = elmi[e];
            }
            ++kept;
        }
        nent = kept;
    }

    // ------------------------------------------------------------------ the cast (raycast.c setup, rays_fill, walk)

    static Field chunkMapping, prevRain, prevThunder, perlin;

    static Object get(Field f, Object o)
    {
        try { return f.get(o); }
        catch (IllegalAccessException e) { throw new RuntimeException(e); }
    }

    static Field field(Class<?> c, String n)
    {
        try
        {
            Field f = c.getDeclaredField(n);
            f.setAccessible(true);
            return f;
        }
        catch (NoSuchFieldException e) { throw new RuntimeException(e); }
    }

    /**
     * The frame of the client's view now into rgb (w * h * ch bytes); false (rgb black) with no client world or no
     * tables (raycast.c cast's -1).
     */
    static boolean cast(Minecraft mc, int w_, int h_, int ch, boolean fancy, float gamma, byte[] rgb)
    {
        java.util.Arrays.fill(rgb, (byte)0);
        Tables t = tables();
        if (mc.theWorld == null || mc.thePlayer == null || t == null) return false;
        if (chunkMapping == null)
        {
            chunkMapping = field(ChunkProviderClient.class, "chunkMapping");
            prevRain = field(World.class, "prevRainingStrength");
            prevThunder = field(World.class, "prevThunderingStrength");
            perlin = field(BiomeGenBase.class, "field_150605_ac");
        }
        RayCast k = new RayCast();
        k.t = t;
        k.w = mc.theWorld;
        k.chunks = (LongHashMap)get(chunkMapping, k.w.getChunkProvider());
        k.ch = ch;
        if (traceFile != null) k.rays = new int[w_ * h_][6];
        Entity p = mc.thePlayer;
        // the eye (EntityRenderer.orientCamera's yOffset - 1.62)
        double wx = p.posX, wy = p.posY + (double)(p.yOffset - 1.62F), wz = p.posZ;
        k.ox = wx;
        k.oy = wy;
        k.oz = wz;
        k.eyeX = (int)Math.floor(k.ox);
        k.eyeY = (int)Math.floor(k.oy);
        k.eyeZ = (int)Math.floor(k.oz);
        int rd = mc.gameSettings.renderDistanceChunks;
        if (rd < 2) rd = 4;
        k.range = rd * 16;
        k.dim = p.dimension;
        k.noSky = k.dim != 0;
        k.fancy = fancy;
        int[] mm = new int[1];
        k.liquid = k.eyeY >= 0 && k.eyeY < 256 ? liquidOf(k.cell(k.eyeX, k.eyeY, k.eyeZ, mm)) : 0;
        float yaw = p.rotationYaw, pitch = p.rotationPitch;
        float f1 = MathHelper.cos(-yaw * 0.017453292F - 3.1415927F);
        float f2 = MathHelper.sin(-yaw * 0.017453292F - 3.1415927F);
        float f3 = -MathHelper.cos(-pitch * 0.017453292F);
        float f4 = MathHelper.sin(-pitch * 0.017453292F);
        k.fx = (double)(f2 * f3);
        k.fy = (double)f4;
        k.fz = (double)(f1 * f3);
        k.rx = (double)f1;
        k.ry = 0.0;
        k.rz = (double)-f2;
        k.ux = k.ry * k.fz - k.rz * k.fy;
        k.uy = k.rz * k.fx - k.rx * k.fz;
        k.uz = k.rx * k.fy - k.ry * k.fx;
        k.aspect = (double)w_ / (double)h_;

        // build_rs
        k.rd = rd;
        k.gamma = gamma;
        k.py = p.posY;
        k.yaw = yaw;
        k.pit = pitch;
        k.wt = k.w.getWorldTime();
        IntegratedServer s = mc.getIntegratedServer();
        WorldServer sw = s != null ? s.worldServerForDimension(k.dim) : null;
        if (sw != null && sw.provider.dimensionId == k.dim)
        {
            k.rain = (Float)get(prevRain, sw);
            k.thu = (Float)get(prevThunder, sw);
        }
        float floor_ = k.dim == -1 ? 0.1F : 0.0F;
        for (int i = 0; i <= 15; ++i)
        {
            float v = 1.0F - (float)i / 15.0F;
            k.lbt[i] = (1.0F - v) / (v * 3.0F + 1.0F) * (1.0F - floor_) + floor_;
        }
        int bx = (int)Math.floor(p.posX), bz = (int)Math.floor(p.posZ);
        int by = (int)Math.floor(p.posY - (double)p.yOffset + 1.8 * 0.66);
        int le = k.light(bx, by, bz);
        k.brf = (le >> 4) << 20 | (le & 15) << 4;
        int fy = (int)Math.floor(p.posY);
        float temp = t.biomeTemp[k.biome(bx, bz)];
        if (fy > 64)
        {
            NoiseGeneratorPerlin pn = (NoiseGeneratorPerlin)get(perlin, null);
            float n = (float)pn.func_151601_a((double)bx * 1.0 / 8.0, (double)bz * 1.0 / 8.0) * 4.0F;
            temp = temp - (n + (float)fy - 64.0F) * 0.05F / 30.0F;
        }
        k.skytemp = skyColorByTemp(temp);
        int lf = k.light(bx, fy, bz);
        int level = (lf >> 4) - (k.dim == -1 ? 11 : 0);
        if ((lf & 15) > level) level = lf & 15;
        float bri = k.lbt[level < 0 ? 0 : level > 15 ? 15 : level];
        float fr1 = (float)rd / 16.0F;
        k.fc1 = bri * (1.0F - fr1) + fr1;
        k.mat = k.liquid == 8 ? 1 : k.liquid == 10 ? 2 : 0;
        k.lightPort();
        if (k.liquid == 8)
        {
            float g1 = 1.0F - (MathHelper.cos(celestial(k.dim, k.wt, 1.0F) * PIF * 2.0F) * 2.0F + 0.5F);
            g1 = g1 < 0.0F ? 0.0F : g1 > 1.0F ? 1.0F : g1;
            g1 = 1.0F - g1;
            g1 = (float)((double)g1 * (1.0 - (double)(k.rainAt() * 5.0F) / 16.0));
            g1 = (float)((double)g1 * (1.0 - (double)(k.thunderAt() * 5.0F) / 16.0));
            int sub = (int)((1.0F - g1) * 11.0F);
            int skyL = k.brf >> 20 & 15, blockL = k.brf >> 4 & 15;
            int l = skyL - sub > blockL ? skyL - sub : blockL;
            float b = k.lbt[l < 0 ? 0 : l > 15 ? 15 : l];
            k.uw = true;
            for (int c = 0; c < 3; ++c) k.uwRgb[c] = t.underwater[c] * b;
        }
        k.entities(mc, w_, h_);

        // the rays, rows split among the walkers
        int nth = w_ * h_ >= 1024 ? Math.min(NTHREADS, h_) : 1;
        if (nth <= 1) k.rows(0, h_, w_, h_, rgb);
        else
        {
            if (workers == null)
                workers = java.util.concurrent.Executors.newFixedThreadPool(NTHREADS, r -> {
                    Thread th = new Thread(r, "Oracle RayCast");
                    th.setDaemon(true);
                    return th;
                });
            List<java.util.concurrent.Future<?>> fs_ = new java.util.ArrayList<java.util.concurrent.Future<?>>();
            for (int q = 0; q < nth; ++q)
            {
                final int j0 = h_ * q / nth, j1 = h_ * (q + 1) / nth;
                final RayCast c = k.fork();
                fs_.add(workers.submit(() -> c.rows(j0, j1, w_, h_, rgb)));
            }
            try { for (java.util.concurrent.Future<?> f : fs_) f.get(); }
            catch (Exception e) { throw new RuntimeException(e); }
        }
        if (k.rays != null) k.trace(w_, h_);
        return true;
    }

    /** rows j0 .. j1 - 1 of the frame: rays_sx, rays_row, rays_ents, walk, a pixel at a time in pixel order */
    void rows(int j0, int j1, int w_, int h_, byte[] rgb)
    {
        final RayCast k = this;
        double sxs = TAN_HALF * k.aspect;
        double ex0 = (double)k.eyeX, ey0 = (double)k.eyeY, ez0 = (double)k.eyeZ;
        double ex1 = (double)(k.eyeX + 1), ey1 = (double)(k.eyeY + 1), ez1 = (double)(k.eyeZ + 1);
        for (int j = j0; j < j1; ++j)
        {
            double sy = (1.0 - 2.0 * ((double)j + 0.5) / (double)h_) * TAN_HALF;
            for (int i = 0; i < w_; ++i)
            {
                double sx = (2.0 * ((double)i + 0.5) / (double)w_ - 1.0) * sxs;
                double dx = k.fx + sx * k.rx + sy * k.ux, dy = k.fy + sx * k.ry + sy * k.uy, dz = k.fz + sx * k.rz + sy * k.uz;
                double inv = 1.0 / Math.sqrt(dx * dx + dy * dy + dz * dz);
                double d0 = dx * inv, d1 = dy * inv, d2 = dz * inv;
                double fwd = d0 * k.fx + d1 * k.fy + d2 * k.fz;
                double i0 = d0 != 0.0 ? 1.0 / d0 : 0.0, i1 = d1 != 0.0 ? 1.0 / d1 : 0.0, i2 = d2 != 0.0 ? 1.0 / d2 : 0.0;
                double t0 = d0 != 0.0 ? ((d0 > 0.0 ? ex1 : ex0) - k.ox) * i0 : Double.POSITIVE_INFINITY;
                double t1 = d1 != 0.0 ? ((d1 > 0.0 ? ey1 : ey0) - k.oy) * i1 : Double.POSITIVE_INFINITY;
                double t2 = d2 != 0.0 ? ((d2 > 0.0 ? ez1 : ez0) - k.oz) * i2 : Double.POSITIVE_INFINITY;
                // the nearest entity box the pixel sees (rays_ents)
                double te = Double.POSITIVE_INFINITY;
                int ent = -1, eface = 1;
                if (k.nent > 0)
                {
                    int ssx = (d0 > 0 ? 1 : 0) - (d0 < 0 ? 1 : 0), ssy = (d1 > 0 ? 1 : 0) - (d1 < 0 ? 1 : 0), ssz = (d2 > 0 ? 1 : 0) - (d2 < 0 ? 1 : 0);
                    double[] eo = {k.ox, k.oy, k.oz}, ei = {i0, i1, i2};
                    int[] es = {ssx, ssy, ssz};
                    for (int e = 0; e < k.nent; ++e)
                    {
                        if (i < k.ei0[e] || i > k.ei1[e] || j < k.ej0[e] || j > k.ej1[e]) continue;
                        double[] box = k.ebox[e];
                        double tn = Double.NEGATIVE_INFINITY, tf = Double.POSITIVE_INFINITY;
                        int ax = -1;
                        boolean miss = false;
                        for (int a = 0; a < 3 && !miss; ++a)
                        {
                            if (es[a] == 0)
                            {
                                if (eo[a] < box[a] || eo[a] > box[a + 3]) miss = true;
                                continue;
                            }
                            double q0 = (box[a] - eo[a]) * ei[a], q1 = (box[a + 3] - eo[a]) * ei[a];
                            if (q0 > q1) { double sw_ = q0; q0 = q1; q1 = sw_; }
                            if (q0 > tn) { tn = q0; ax = a; }
                            if (q1 < tf) tf = q1;
                        }
                        if (miss || ax < 0 || tn > tf || tn <= 0.0 || tn >= te) continue;
                        te = tn;
                        ent = e;
                        eface = entryFace(ax, es[ax]);
                    }
                }
                k.pixAt = j * w_ + i;
                k.walk(d0, d1, d2, fwd, i0, i1, i2, t0, t1, t2, te, ent, eface, rgb, (j * w_ + i) * ch);
            }
        }
    }

    /** the trace's (--raycast-dump DIR:@T): each pixel's walk as it ended, and the file it goes to */
    int[][] rays;
    int pixAt;
    static File traceFile;

    int saved(net.minecraft.world.EnumSkyBlock t_, int[] c)
    {
        int y = c[1] < 0 ? 0 : c[1] > 255 ? 255 : c[1];
        Chunk ch = chunk(c[0] >> 4, c[2] >> 4);
        return ch == null ? t_.defaultLightValue : ch.getSavedLightValue(t_, c[0] & 15, y, c[2] & 15);
    }

    static String d16(double v) { return String.format("%016x", Double.doubleToRawLongBits(v)); }

    static String f8(float v) { return String.format("%08x", Float.floatToRawIntBits(v)); }

    /** pool_gate's raycast_trace form: the setup's reals as bits, the entities, the pixels' walks */
    void trace(int w_, int h_)
    {
        StringBuilder s = new StringBuilder();
        s.append("eye ").append(d16(ox)).append(' ').append(d16(oy)).append(' ').append(d16(oz))
            .append(" cell ").append(eyeX).append(' ').append(eyeY).append(' ').append(eyeZ).append('\n');
        s.append("look");
        for (double v : new double[] {fx, fy, fz, rx, rz, ux, uy, uz}) s.append(' ').append(d16(v));
        s.append('\n');
        s.append("cast liquid ").append(liquid).append(" range ").append(range).append(" dim ").append(dim).append(" no_sky ")
            .append(noSky ? 1 : 0).append('\n');
        s.append("sky ").append(f8(sky[0])).append(' ').append(f8(sky[1])).append(' ').append(f8(sky[2])).append(" fog ")
            .append(f8(fog[0])).append(' ').append(f8(fog[1])).append(' ').append(f8(fog[2])).append('\n');
        s.append("fogform exp ").append(expFog ? 1 : 0).append(" fs ").append(f8(fs)).append(" fe ").append(f8(fe)).append(" fd ")
            .append(f8(fd)).append(" sky_end ").append(f8(skyEnd)).append(" uw ").append(uw ? 1 : 0).append(' ').append(f8(uwRgb[0]))
            .append(' ').append(f8(uwRgb[1])).append(' ').append(f8(uwRgb[2])).append('\n');
        for (int i = 0; i < 256; ++i)
            s.append("lm ").append(i).append(' ').append(f8(lm[i][0])).append(' ').append(f8(lm[i][1])).append(' ').append(f8(lm[i][2])).append('\n');
        for (int e = 0; e < nent; ++e)
        {
            s.append("ent ").append(e);
            for (int a = 0; a < 6; ++a) s.append(' ').append(d16(ebox[e][a]));
            s.append(String.format(" rect %d %d %d %d rgb %s %s %s lmi %d  (%.4f %.4f %.4f)\n", ei0[e], ei1[e], ej0[e], ej1[e],
                f8(ergb[e][0]), f8(ergb[e][1]), f8(ergb[e][2]), elmi[e], ebox[e][0], ebox[e][1], ebox[e][2]));
        }
        for (int p = 0; p < w_ * h_; ++p)
        {
            int[] r = rays[p];
            s.append(String.format("px %d kind %d %d %d %d axis %d dist %d", p, r[0], r[1], r[2], r[3], r[4], r[5]));
            if (r[0] == 0)
            {
                // the client world's light (getSavedLightValue) at the cell hit and the one before it, its biome
                int[] c3 = {r[1], r[2], r[3]}, e3 = {eyeX, eyeY, eyeZ}, q3 = {r[1], r[2], r[3]};
                q3[r[4]] -= c3[r[4]] > e3[r[4]] ? 1 : c3[r[4]] < e3[r[4]] ? -1 : 0;
                s.append(String.format(" pre %d %d cell %d %d bio %d", saved(net.minecraft.world.EnumSkyBlock.Sky, q3),
                    saved(net.minecraft.world.EnumSkyBlock.Block, q3), saved(net.minecraft.world.EnumSkyBlock.Sky, c3),
                    saved(net.minecraft.world.EnumSkyBlock.Block, c3), chunk(r[1] >> 4, r[3] >> 4) != null ? biome(r[1], r[3]) : -1));
            }
            s.append('\n');
        }
        try { Files.write(traceFile.toPath(), s.toString().getBytes("UTF-8")); }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    // ------------------------------------------------------------------ the row's field

    static byte[] frame;

    /** d.rc for the row being captured: the frame's FNV-1a 64 (null when off) */
    static String rowDigest(Minecraft mc, long tick)
    {
        if (!on) return null;
        int n = specW * specH * specCh;
        if (frame == null || frame.length != n) frame = new byte[n];
        boolean at = dumpDir != null && dumpEvery > 0 && (dumpAt >= 0 ? tick == dumpAt : tick % dumpEvery == 0);
        if (at && dumpAt >= 0)
        {
            new File(dumpDir).mkdirs();
            traceFile = new File(dumpDir, String.format("r_%06d.txt", tick));
        }
        try { cast(mc, specW, specH, specCh, specFancy, specGamma, frame); }
        finally { traceFile = null; }
        long h = 0xcbf29ce484222325L;
        for (int i = 0; i < n; ++i) h = (h ^ (frame[i] & 255)) * 0x100000001b3L;
        if (at) dump(tick);
        return Rows.hex(h);
    }

    static void dump(long tick)
    {
        java.awt.image.BufferedImage im = new java.awt.image.BufferedImage(specW, specH, java.awt.image.BufferedImage.TYPE_INT_RGB);
        for (int j = 0; j < specH; ++j)
            for (int i = 0; i < specW; ++i)
            {
                int o = (j * specW + i) * specCh;
                int r = frame[o] & 255, g = specCh == 3 ? frame[o + 1] & 255 : r, b = specCh == 3 ? frame[o + 2] & 255 : r;
                im.setRGB(i, j, r << 16 | g << 8 | b);
            }
        try
        {
            new File(dumpDir).mkdirs();
            javax.imageio.ImageIO.write(im, "png", new File(dumpDir, String.format("r_%06d.png", tick)));
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }
}
