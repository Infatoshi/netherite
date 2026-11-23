package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.texture.TextureAtlasSprite;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.ChunkCache;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;

/**
 * The chunk meshes the 1.7.10 client builds, the reference a native renderer is
 * checked against vertex by vertex instead of pixel by pixel.
 *
 * For every loaded chunk section within `radius` chunks of the camera block and
 * both render passes, this builds the section exactly as
 * WorldRenderer.updateRenderer does - the same ChunkCache over the section plus
 * one block of margin, the same RenderBlocks over it, the same startDrawingQuads
 * and setTranslation(-section origin), the same pass test and y/z/x block order,
 * the same double draw of the one block the renderer sits inside - but without
 * drawing: it reads the Tessellator's raw ints back out and resets instead of
 * calling draw(). The Tessellator state touched is saved and restored.
 *
 * The mesh is a function of world state alone. Nothing here depends on the
 * frustum, the frame count or which section the camera is standing in; the
 * camera position only decides where the extra inside-the-block draw happens.
 *
 * Runs on the client thread, which Oracle.ticksThisFrame has parked between
 * frames, so the server is parked in Lockstep.pair: no RNG stream moves and no
 * world state changes underneath.
 *
 * Output DIR/manifest.json, DIR/sections.bin, DIR/atlas.json. Both layouts are
 * described in the manifest.
 *
 * With "allowEmpty": true a chunk the client has not received (its blank chunk)
 * is meshed as empty and recorded as air instead of failing the probe, so a
 * scene can be meshed out to the full render distance; manifest chunks_unloaded
 * counts them. Without it the probe still refuses an unloaded world.
 */
final class MeshProbe
{
    static Field fRawBuffer, fVertexCount, fRawBufferIndex, fDrawMode, fIsDrawing, fIsColorDisabled,
        fHasTexture, fHasColor, fHasBrightness, fHasNormals,
        fXOffset, fYOffset, fZOffset, fTextureMap, fRotated, fMip, fAniso, fPartialRenderBounds;
    private static Method mReset;

    private MeshProbe() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        Minecraft mc = Minecraft.getMinecraft();
        RenderBlocks.fancyGrass = mc.gameSettings.fancyGraphics;
        World world = mc.theWorld;
        EntityLivingBase view = mc.renderViewEntity;
        if (world == null || view == null) throw new IllegalStateException("no client world or camera");

        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 4;
        // A scene whose render distance exceeds the loaded chunk disc cannot be
        // meshed unless the chunks the client never received are recorded as the
        // empty chunks the client itself renders there. The caller opts in, so a
        // scene that simply ran too few ticks still fails loudly by default.
        boolean allowEmpty = cmd.has("allowEmpty") && cmd.get("allowEmpty").getAsBoolean();
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        reflect();

        // The chunk of the camera block, the way WorldRenderer floors the entity
        // position. Entity.chunkCoordX is not maintained on the client player, so
        // it must not be used here (it reads 0 at spawn).
        int px = MathHelper.floor_double(view.posX);
        int py = MathHelper.floor_double(view.posY);
        int pz = MathHelper.floor_double(view.posZ);
        int cx = px >> 4, cz = pz >> 4;

        Chunk[][] cs = new Chunk[2 * radius + 1][2 * radius + 1];
        int unloaded = 0;
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                Chunk c = world.getChunkFromChunkCoords(cx + dx, cz + dz);
                if (c == null || c.isEmpty())
                {
                    if (!allowEmpty)
                        throw new IllegalStateException("chunk (" + (cx + dx) + "," + (cz + dz) + ") is not loaded yet; the client fills missed chunks with an EmptyChunk, so let it run some ticks first");
                    cs[dx + radius][dz + radius] = null;
                    ++unloaded;
                    continue;
                }
                cs[dx + radius][dz + radius] = c;
            }
        }

        Tessellator t = Tessellator.instance;
        if ((Boolean)fIsDrawing.get(t)) throw new IllegalStateException("the Tessellator is mid-draw; MeshProbe must run between frames");
        long[] saved = save(t);

        Map<Long, ChunkCache> caches = new HashMap<Long, ChunkCache>();
        Mesh m = new Mesh();
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "sections.bin")), 1 << 16));

        // chunk order +x then +z, section 0..15, pass 0 then 1. Each record's
        // address is (cx, cz, s, pass), so the file sorts back into that order.
        for (int dx = -radius; dx <= radius; ++dx)
        {
            for (int dz = -radius; dz <= radius; ++dz)
            {
                Chunk c = cs[dx + radius][dz + radius];
                if (c == null) continue;
                for (int s = 0; s < 16; ++s)
                {
                    meshSection(world, c, s, px, py, pz, caches, m, out);
                }
            }
        }
        out.close();

        restore(t, saved);
        // digest() resets the MessageDigest, so take each digest exactly once.
        String shaAll = hex(m.md.digest());
        String sha0 = hex(m.pass[0].digest());
        String sha1 = hex(m.pass[1].digest());
        int sprites = atlas(mc, dir, m);
        int verticesTotal = m.vertices[0] + m.vertices[1];
        // The world the meshes above were built from, and the renderer's lookup
        // tables, so the native mesher needs nothing but this directory.
        writeChunks(world, cx, cz, radius, allowEmpty, dir);
        JsonObject table = RenderTable.write(mc, dir);

        JsonObject r = new JsonObject();
        r.addProperty("dir", dir.getPath());
        r.addProperty("radius", radius);
        r.addProperty("seed", Oracle.seed);
        r.addProperty("player_chunk", cx + "," + cz);
        r.addProperty("player_block", px + "," + py + "," + pz);
        r.addProperty("sections_total", (2 * radius + 1) * (2 * radius + 1) * 16);
        r.addProperty("sections_nonempty", m.sections);
        r.addProperty("sections_pass0", m.nonEmpty[0]);
        r.addProperty("sections_pass1", m.nonEmpty[1]);
        r.addProperty("records", m.records);
        r.addProperty("vertices_pass0", m.vertices[0]);
        r.addProperty("vertices_pass1", m.vertices[1]);
        r.addProperty("raw_ints", m.ints);
        r.add("render_types", toJson(m.renderTypes));
        r.add("features", toJson(m.features));
        r.addProperty("has_texture_records", m.hasTexture);
        r.addProperty("has_color_records", m.hasColor);
        r.addProperty("has_brightness_records", m.hasBrightness);
        r.addProperty("has_normals_records", m.hasNormals);
        r.addProperty("sprites", sprites);
        r.addProperty("inside_draws", m.insideDraws);
        r.addProperty("inside_vertices", m.insideVerts);
        r.addProperty("sha1_meshes", shaAll);
        r.addProperty("sha1_pass0", sha0);
        r.addProperty("sha1_pass1", sha1);

        JsonObject man = new JsonObject();
        man.addProperty("seed", Oracle.seed);
        man.addProperty("world_seed", world.getSeed()); // the client WorldInfo does not carry the server seed
        man.addProperty("mc", "1.7.10");
        man.addProperty("player_chunk", cx + "," + cz);
        man.addProperty("player_block", px + "," + py + "," + pz);
        man.addProperty("radius_chunks", radius);
        man.addProperty("chunks_unloaded", unloaded);
        man.addProperty("sections_total", (2 * radius + 1) * (2 * radius + 1) * 16);
        man.addProperty("sections_nonempty", m.sections);
        man.addProperty("sections_pass0", m.nonEmpty[0]);
        man.addProperty("sections_pass1", m.nonEmpty[1]);
        man.addProperty("records", m.records);
        man.addProperty("vertices_pass0", m.vertices[0]);
        man.addProperty("vertices_pass1", m.vertices[1]);
        man.addProperty("raw_ints", m.ints);
        man.add("render_types", toJson(m.renderTypes));
        man.add("features", toJson(m.features));
        man.addProperty("has_texture_records", m.hasTexture);
        man.addProperty("has_color_records", m.hasColor);
        man.addProperty("has_brightness_records", m.hasBrightness);
        man.addProperty("has_normals_records", m.hasNormals);
        man.addProperty("sprites", sprites);
        man.addProperty("inside_draws", m.insideDraws);
        man.addProperty("inside_vertices", m.insideVerts);
        man.addProperty("inside_note", "updateRenderer draws the block the renderer sits inside a second time (renderFromInside + renderAllFaces) when its render type is 0; this counts that draw. It only fires when a full cube occupies the camera block, so it is 0 whenever the camera stands in air.");
        man.addProperty("atlas_json", "{\"atlas_width\":w,\"atlas_height\":h,\"mip_levels\":n,\"aniso\":n,\"count\":n,\"sprites\":[{\"name\":str,\"x\":pixels,\"y\":pixels,\"w\":pixels,\"h\":pixels,\"minU\":rawbits,\"maxU\":rawbits,\"minV\":rawbits,\"maxV\":rawbits,\"rotated\":bool}]}");
        man.addProperty("vertex_layout", "each vertex is 8 int32: x, y, z, u, v, color, normal, brightness. x/y/z, u/v are float bits; color is RGBA8 packed little endian; normal is 3 signed bytes (x, y, z) normalized by 127 packed as byte0 | byte1 << 8 | byte2 << 16; brightness is sky << 16 | block. Coordinates are relative to the section origin (renderer translation is -origin).");
        man.addProperty("sections_bin_layout", "little endian records in order cx asc, cz asc, section asc, pass asc: int32 cx | int32 cz | int32 section (0..15) | int32 pass (0 or 1) | int32 vertexCount | int32 hasFlags (1 texture, 2 color, 4 brightness, 8 normals) | vertexCount * 8 int32 raw vertex data. A record exists only when vertexCount > 0.");
        man.addProperty("sha1_layout", "sha1_meshes is SHA-1 over the records in sections.bin order: for each record, 16 bytes little-endian (cx, cz, section, pass) then the vertexCount * 8 raw int32 as they appear. sha1_passN is the same over the pass N records only.");
        man.addProperty("camera", "pass 1 (translucent) quads are sorted back to front before they are drawn: at the end of updateRenderer's pass 1, Tessellator.getVertexState reorders the raw buffer in quad order by QuadComparator about the camera, and updateRendererSort replays it. sections.bin holds the unsorted build order; a renderer must reproduce the sort from the camera position recorded here.");
        man.addProperty("passes", "pass 0 is opaque, pass 1 is translucent; RenderBlocks sees both through one ChunkCache per section, exactly as updateRenderer builds them.");
        man.addProperty("graphics", "fancy:" + (mc.gameSettings.fancyGraphics ? 1 : 0)
            + " ao:" + mc.gameSettings.ambientOcclusion
            + " mipmap:" + mc.gameSettings.mipmapLevels
            + " aniso:" + mc.gameSettings.anisotropicFiltering);
        man.addProperty("sha1_meshes", shaAll);
        man.addProperty("sha1_pass0", sha0);
        man.addProperty("sha1_pass1", sha1);
        man.addProperty("vertices_total", verticesTotal);
        man.addProperty("chunks_bin_layout", "for every chunk within radius + 1 of the camera chunk, cx-major (dx outer ascending, dz inner ascending from -radius-1 to radius+1): int32 cx LE, int32 cz LE, the chunk bytes of Probe's chunk_bytes layout, then the chunk's 256 biome ids (getBiomeArray(), index z << 4 | x, the raw byte read as id & 255). This is the world the oracle meshed: a ChunkCache over a section reads blocks up to one block outside it and the biome one block out, both inside radius + 1.");
        man.add("table", table);
        man.add("census", censusJson(m));
        man.addProperty("census_layout", "how many times each (block id : meta : render type : path) was passed to renderBlockByRenderType, the extra inside-the-block draw excluded. path is ao / ao-partial / colormult / colormult-partial for render type 0 (ambient occlusion enabled and light value 0 picks ao; a partial bounding box picks -partial) and rtN otherwise.");
        man.add("block_classes", classJson(m));
        PrintWriter pw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), Charset.forName("UTF-8")));
        pw.println(man.toString());
        pw.close();
        Snapshot.writeEntities(server, dir, new int[3]);
        PrintWriter items = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "render_items.jsonl")), Charset.forName("UTF-8")));
        for (Object obj : world.loadedEntityList)
        {
            if (!(obj instanceof net.minecraft.entity.item.EntityItem)) continue;
            net.minecraft.entity.item.EntityItem e = (net.minecraft.entity.item.EntityItem)obj;
            net.minecraft.item.ItemStack s = e.getEntityItem();
            if (s == null) continue;
            JsonObject o = new JsonObject();
            o.addProperty("id", net.minecraft.item.Item.getIdFromItem(s.getItem()));
            o.addProperty("meta", s.getItemDamage());
            o.addProperty("count", s.stackSize);
            o.addProperty("x", e.posX);
            o.addProperty("y", e.posY);
            o.addProperty("z", e.posZ);
            o.addProperty("age", e.age);
            o.addProperty("hover", Float.floatToRawIntBits(e.hoverStart));
            o.addProperty("light", e.getBrightnessForRender(1.0F));
            o.add("sprite", RenderStateProbe.item(s));
            items.println(o.toString());
        }
        items.close();
        PrintWriter orbs = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "render_orbs.jsonl")), Charset.forName("UTF-8")));
        for (Object obj : world.loadedEntityList)
        {
            if (!(obj instanceof net.minecraft.entity.item.EntityXPOrb)) continue;
            net.minecraft.entity.item.EntityXPOrb e = (net.minecraft.entity.item.EntityXPOrb)obj;
            JsonObject o = new JsonObject();
            o.addProperty("x", e.posX);
            o.addProperty("y", e.posY);
            o.addProperty("z", e.posZ);
            o.addProperty("value", e.getXpValue());
            o.addProperty("color", e.xpColor);
            o.addProperty("light", e.getBrightnessForRender(1.0F));
            orbs.println(o.toString());
        }
        orbs.close();
        return r;
    }

    // ------------------------------------------------------------------ meshing

    /**
     * One section, both passes, exactly as WorldRenderer.updateRenderer builds
     * it: one ChunkCache (section + one block, radius 1) and one RenderBlocks per
     * section, pass by pass, block order y/z/x, then the extra inside-the-block
     * draw for the block the camera sits inside.
     */
    static void meshSection(World world, Chunk c, int s, int px, int py, int pz,
                                    Map<Long, ChunkCache> caches, Mesh m, DataOutputStream out) throws Exception
    {
        Tessellator t = Tessellator.instance;
        int baseX = c.xPosition << 4, baseZ = c.zPosition << 4;
        int y0 = s << 4;
        ChunkCache cc = cache(world, c, caches);
        RenderBlocks rb = new RenderBlocks(cc);
        boolean any = false;

        for (int pass = 0; pass < 2; ++pass)
        {
            t.startDrawingQuads();
            t.setTranslation((double)(-baseX), (double)(-y0), (double)(-baseZ));

            for (int y = y0; y < y0 + 16; ++y)
            {
                for (int z = baseZ; z < baseZ + 16; ++z)
                {
                    for (int x = baseX; x < baseX + 16; ++x)
                    {
                        Block b = cc.getBlock(x, y, z);
                        if (b.getMaterial() == Material.air) continue;
                        if (b.getRenderBlockPass() != pass) continue;

                        rb.renderBlockByRenderType(b, x, y, z);
                        inc(m.renderTypes, b.getRenderType());
                        census(m, b, x, y, z, cc, rb);

                        if (b.getRenderType() == 0 && x == px && y == py && z == pz)
                        {
                            int before = (Integer)fVertexCount.get(t);
                            rb.setRenderFromInside(true);
                            rb.setRenderAllFaces(true);
                            rb.renderBlockByRenderType(b, x, y, z);
                            rb.setRenderFromInside(false);
                            rb.setRenderAllFaces(false);
                            ++m.insideDraws;
                            m.insideVerts += (Integer)fVertexCount.get(t) - before;
                        }
                    }
                }
            }

            int vc = (Integer)fVertexCount.get(t);
            int n = fRawBufferIndex.getInt(t);
            int flags = flags(t);
            if (vc > 0)
            {
                ++m.nonEmpty[pass];
                any = true;
                int[] raw = (int[])fRawBuffer.get(t);
                m.vertices[pass] += vc;
                m.ints += n;
                ++m.records;
                writeRecord(out, c.xPosition, c.zPosition, s, pass, vc, flags, raw, n);
                digest(m, raw, n, c.xPosition, c.zPosition, s, pass);
                if ((flags & 1) != 0) m.hasTexture = true;
                if ((flags & 2) != 0) m.hasColor = true;
                if ((flags & 4) != 0) m.hasBrightness = true;
                if ((flags & 8) != 0) m.hasNormals = true;
                inc(m.features, flags);
            }
            // draw() would clear isDrawing along with the counts; we only read the
            // buffer, so clear it ourselves before the next startDrawingQuads().
            reset(t);
            fIsDrawing.setBoolean(t, false);
        }
        if (any) ++m.sections;
    }

    /** The ChunkCache WorldRenderer builds for a section: bounds +-1 block, chunk radius 1, shared per chunk. */
    static ChunkCache cache(World world, Chunk c, Map<Long, ChunkCache> caches)
    {
        long k = ((long)c.xPosition << 32) ^ (c.zPosition & 0xffffffffL);
        ChunkCache cc = caches.get(k);
        if (cc == null)
        {
            int x0 = c.xPosition << 4, z0 = c.zPosition << 4;
            cc = new ChunkCache(world, x0 - 1, 0, z0 - 1, x0 + 17, 256, z0 + 17, 1);
            caches.put(k, cc);
        }
        return cc;
    }

    // ------------------------------------------------------------------ output

    /**
     * One count per (id, meta, render type, path the standard-block renderer
     * took). Only the main draw is counted, not the extra inside-the-block one.
     */
    static void census(Mesh m, Block b, int x, int y, int z, ChunkCache cc, RenderBlocks rb) throws Exception
    {
        int id = Block.getIdFromBlock(b);
        int meta = cc.getBlockMetadata(x, y, z);
        int rt = b.getRenderType();
        String path;

        if (rt == 0)
        {
            boolean ao = Minecraft.isAmbientOcclusionEnabled() && b.getLightValue() == 0;
            boolean partial = fPartialRenderBounds.getBoolean(rb);
            path = ao ? (partial ? "ao-partial" : "ao") : (partial ? "colormult-partial" : "colormult");
        }
        else
        {
            path = "rt" + rt;
        }

        inc(m.census, id + ":" + meta + ":" + rt + ":" + path);
        if (!m.classes.containsKey(Integer.valueOf(id))) m.classes.put(Integer.valueOf(id), b.getClass().getSimpleName());
    }

    /**
     * The chunks the mesher above read from, so the native mesher can rebuild
     * the world exactly: every chunk within radius + 1 of the camera chunk,
     * each in Probe's chunk_bytes layout plus its biome array.
     */
    static void writeChunks(World world, int cx, int cz, int radius, boolean allowEmpty, File dir) throws Exception
    {
        int margin = radius + 1;
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "chunks.bin")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];

        for (int dx = -margin; dx <= margin; ++dx)
        {
            for (int dz = -margin; dz <= margin; ++dz)
            {
                Chunk c = world.getChunkFromChunkCoords(cx + dx, cz + dz);
                if (c == null) throw new IllegalStateException("chunk (" + (cx + dx) + "," + (cz + dz) + ") is not loaded");
                writeIntLE(out, cx + dx);
                writeIntLE(out, cz + dz);
                if (c.isEmpty() && allowEmpty)
                {
                    // Not a chunk the client ever received: the world there is
                    // air (the client's own blank chunk), so record it as air.
                    java.util.Arrays.fill(buf, (byte)0);
                    out.write(buf);
                    out.write(new byte[256]);
                }
                else
                {
                    Probe.fillChunkBytes(c, buf);
                    out.write(buf);
                    out.write(c.getBiomeArray());
                }
            }
        }
        out.close();
    }

    static JsonArray censusJson(Mesh m)
    {
        JsonArray a = new JsonArray();
        for (Map.Entry<String, Integer> e : m.census.entrySet())
        {
            JsonArray p = new JsonArray();
            p.add(new JsonPrimitive(e.getKey()));
            p.add(new JsonPrimitive(e.getValue()));
            a.add(p);
        }
        return a;
    }

    static JsonObject classJson(Mesh m)
    {
        JsonObject o = new JsonObject();
        for (Map.Entry<Integer, String> e : m.classes.entrySet()) o.addProperty(String.valueOf(e.getKey()), e.getValue());
        return o;
    }

    private static void writeRecord(DataOutputStream out, int cx, int cz, int s, int pass, int vc,
                                    int flags, int[] raw, int n) throws Exception
    {
        writeIntLE(out, cx);
        writeIntLE(out, cz);
        writeIntLE(out, s);
        writeIntLE(out, pass);
        writeIntLE(out, vc);
        writeIntLE(out, flags);
        for (int i = 0; i < n; ++i) writeIntLE(out, raw[i]);
    }

    private static void writeIntLE(DataOutputStream out, int v) throws Exception
    {
        out.write(v & 0xff);
        out.write(v >> 8 & 0xff);
        out.write(v >> 16 & 0xff);
        out.write(v >> 24 & 0xff);
    }

    static void digest(Mesh m, int[] raw, int n, int cx, int cz, int s, int pass) throws Exception
    {
        byte[] hdr = new byte[16];
        le32(hdr, 0, cx);
        le32(hdr, 4, cz);
        le32(hdr, 8, s);
        le32(hdr, 12, pass);
        m.md.update(hdr);
        m.pass[pass].update(hdr);
        byte[] b = new byte[n * 4];
        for (int i = 0; i < n; ++i) le32(b, i * 4, raw[i]);
        m.md.update(b);
        m.pass[pass].update(b);
    }

    static int atlas(Minecraft mc, File dir, Mesh m) throws Exception
    {
        TextureMap map = mc.getTextureMapBlocks();
        Map uploaded = (Map)fTextureMap.get(map);
        int aniso = fAniso.getInt(map);
        TreeSet<String> names = new TreeSet<String>();
        for (Object o : uploaded.keySet()) names.add((String)o);
        JsonArray arr = new JsonArray();
        for (String name : names)
        {
            TextureAtlasSprite sp = (TextureAtlasSprite)uploaded.get(name);
            JsonObject o = new JsonObject();
            o.addProperty("name", name);
            o.addProperty("x", sp.getOriginX());
            o.addProperty("y", sp.getOriginY());
            o.addProperty("w", sp.getIconWidth());
            o.addProperty("h", sp.getIconHeight());
            o.addProperty("minU", bits(sp.getMinU()));
            o.addProperty("maxU", bits(sp.getMaxU()));
            o.addProperty("minV", bits(sp.getMinV()));
            o.addProperty("maxV", bits(sp.getMaxV()));
            o.addProperty("rotated", fRotated.getBoolean(sp));
            arr.add(o);
        }
        // initSprite writes minU = x / W + 0.01 / W and maxU = (x + width) / W - 0.01 / W,
        // so maxU - minU = (width - 0.02) / W. With anisotropic filtering the sprite is padded
        // by 16 pixels on each side and the same 8 / W is trimmed from both u and v.
        int pad = aniso > 1 ? 16 : 0;
        int w = atlasDim(names, uploaded, true, pad);
        int h = atlasDim(names, uploaded, false, pad);
        int oldTexture = GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, map.getGlTextureId());
        int minFilter = GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER);
        int magFilter = GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER);
        int mipLevels = fMip.getInt(map);
        for (int level = 0; level <= mipLevels; ++level)
        {
            int width = w >> level;
            int height = h >> level;
            ByteBuffer pixels = BufferUtils.createByteBuffer(width * height * 4);
            GL11.glGetTexImage(GL11.GL_TEXTURE_2D, level, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, pixels);
            FileOutputStream image = new FileOutputStream(new File(dir, level == 0 ? "atlas.rgba" : "atlas_" + level + ".rgba"));
            byte[] row = new byte[width * 4];
            pixels.position(0);
            for (int y = 0; y < height; ++y)
            {
                pixels.get(row);
                image.write(row);
            }
            image.close();
        }
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, oldTexture);
        JsonObject root = new JsonObject();
        root.addProperty("atlas_width", w);
        root.addProperty("atlas_height", h);
        root.addProperty("min_filter", minFilter);
        root.addProperty("mag_filter", magFilter);
        root.addProperty("mip_levels", mipLevels);
        root.addProperty("aniso", aniso);
        root.addProperty("count", arr.size());
        // the animated sprites' state now, so the dump can be rebuilt from anim.rgba
        root.add("anim", RenderStateProbe.animState(mc));
        root.add("sprites", arr);
        PrintWriter pw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "atlas.json")), Charset.forName("UTF-8")));
        pw.println(root.toString());
        pw.close();
        m.sprites = arr.size();
        return arr.size();
    }

    /**
     * Recover one atlas dimension W from the sprite uv range. The largest sprite
     * gives the most precise answer; round it and require the fraction to be within
     * half a pixel, so a wrong formula cannot pass silently.
     */
    private static int atlasDim(TreeSet<String> names, Map uploaded, boolean u, int pad)
    {
        double best = 0;
        String bestName = null;
        for (String name : names)
        {
            TextureAtlasSprite sp = (TextureAtlasSprite)uploaded.get(name);
            double span = u ? (double)sp.getMaxU() - (double)sp.getMinU() : (double)sp.getMaxV() - (double)sp.getMinV();
            double px = u ? sp.getIconWidth() : sp.getIconHeight();
            if (span <= 0) continue;
            double size = (px - pad - 0.02D) / span;
            if (size > best)
            {
                best = size;
                bestName = name;
            }
        }
        int rounded = (int)Math.round(best);
        if (bestName == null || Math.abs(best - rounded) > 0.5D)
        {
            throw new IllegalStateException("atlas " + (u ? "width" : "height") + " is not recoverable: best " + best + " from " + bestName);
        }
        return Math.max(rounded, 1);
    }

    // ------------------------------------------------------------------ state

    static void reflect() throws Exception
    {
        if (fRawBuffer != null) return;
        fRawBuffer = field(Tessellator.class, "rawBuffer");
        fVertexCount = field(Tessellator.class, "vertexCount");
        fRawBufferIndex = field(Tessellator.class, "rawBufferIndex");
        fDrawMode = field(Tessellator.class, "drawMode");
        fIsDrawing = field(Tessellator.class, "isDrawing");
        fIsColorDisabled = field(Tessellator.class, "isColorDisabled");
        fHasTexture = field(Tessellator.class, "hasTexture");
        fHasColor = field(Tessellator.class, "hasColor");
        fHasBrightness = field(Tessellator.class, "hasBrightness");
        fHasNormals = field(Tessellator.class, "hasNormals");
        fXOffset = field(Tessellator.class, "xOffset");
        fYOffset = field(Tessellator.class, "yOffset");
        fZOffset = field(Tessellator.class, "zOffset");
        mReset = Tessellator.class.getDeclaredMethod("reset");
        mReset.setAccessible(true);
        fTextureMap = field(TextureMap.class, "mapUploadedSprites");
        fRotated = field(TextureAtlasSprite.class, "rotated");
        fMip = field(TextureMap.class, "field_147636_j");
        fAniso = field(TextureMap.class, "field_147637_k");
        fPartialRenderBounds = field(RenderBlocks.class, "partialRenderBounds");
    }

    private static Field field(Class<?> c, String name)
    {
        try
        {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f;
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** isDrawing, drawMode, isColorDisabled, the four has* flags, then the three offsets as raw double bits. */
    static long[] save(Tessellator t) throws Exception
    {
        return new long[] {
            fIsDrawing.getBoolean(t) ? 1 : 0,
            fDrawMode.getInt(t),
            fIsColorDisabled.getBoolean(t) ? 1 : 0,
            fHasTexture.getBoolean(t) ? 1 : 0,
            fHasColor.getBoolean(t) ? 1 : 0,
            fHasBrightness.getBoolean(t) ? 1 : 0,
            fHasNormals.getBoolean(t) ? 1 : 0,
            Double.doubleToRawLongBits(((Double)fXOffset.get(t)).doubleValue()),
            Double.doubleToRawLongBits(((Double)fYOffset.get(t)).doubleValue()),
            Double.doubleToRawLongBits(((Double)fZOffset.get(t)).doubleValue())
        };
    }

    static void restore(Tessellator t, long[] s) throws Exception
    {
        reset(t);
        fIsDrawing.setBoolean(t, s[0] != 0);
        fDrawMode.setInt(t, (int)s[1]);
        fIsColorDisabled.setBoolean(t, s[2] != 0);
        fHasTexture.setBoolean(t, s[3] != 0);
        fHasColor.setBoolean(t, s[4] != 0);
        fHasBrightness.setBoolean(t, s[5] != 0);
        fHasNormals.setBoolean(t, s[6] != 0);
        fXOffset.set(t, Double.valueOf(Double.longBitsToDouble(s[7])));
        fYOffset.set(t, Double.valueOf(Double.longBitsToDouble(s[8])));
        fZOffset.set(t, Double.valueOf(Double.longBitsToDouble(s[9])));
    }

    /** Tessellator.reset() is private; it only clears vertexCount, the byte buffer and the indices. */
    static void reset(Tessellator t)
    {
        try
        {
            mReset.invoke(t);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    /** hasTexture/hasColor/hasBrightness/hasNormals as a 4-bit mask, from the live Tessellator. */
    private static int flags(Tessellator t)
    {
        try
        {
            int f = 0;
            if (fHasTexture.getBoolean(t)) f |= 1;
            if (fHasColor.getBoolean(t)) f |= 2;
            if (fHasBrightness.getBoolean(t)) f |= 4;
            if (fHasNormals.getBoolean(t)) f |= 8;
            return f;
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    private static void le32(byte[] a, int k, int v)
    {
        a[k] = (byte)v;
        a[k + 1] = (byte)(v >> 8);
        a[k + 2] = (byte)(v >> 16);
        a[k + 3] = (byte)(v >> 24);
    }

    private static void inc(Map<Integer, Integer> map, int key)
    {
        Integer n = map.get(Integer.valueOf(key));
        map.put(Integer.valueOf(key), Integer.valueOf(n == null ? 1 : n.intValue() + 1));
    }

    private static void inc(Map<String, Integer> map, String key)
    {
        Integer n = map.get(key);
        map.put(key, Integer.valueOf(n == null ? 1 : n.intValue() + 1));
    }

    static JsonArray toJson(Map<Integer, Integer> map)
    {
        JsonArray a = new JsonArray();
        for (Map.Entry<Integer, Integer> e : map.entrySet())
        {
            JsonArray p = new JsonArray();
            p.add(new JsonPrimitive(e.getKey()));
            p.add(new JsonPrimitive(e.getValue()));
            a.add(p);
        }
        return a;
    }

    static String hex(byte[] d)
    {
        StringBuilder b = new StringBuilder();
        for (byte x : d) b.append(String.format("%02x", x & 0xff));
        return b.toString();
    }

    private static int bits(float f1)
    {
        return Float.floatToRawIntBits(f1);
    }

    static final class Mesh
    {
        MessageDigest md = sha1();
        MessageDigest[] pass = {sha1(), sha1()};
        Map<Integer, Integer> renderTypes = new TreeMap<Integer, Integer>();
        Map<Integer, Integer> features = new TreeMap<Integer, Integer>();
        Map<String, Integer> census = new TreeMap<String, Integer>();
        Map<Integer, String> classes = new TreeMap<Integer, String>();
        int[] vertices = new int[2];
        int[] nonEmpty = new int[2];
        int sections, records, ints, sprites, insideDraws, insideVerts;
        boolean hasTexture, hasColor, hasBrightness, hasNormals;
    }

    private static MessageDigest sha1()
    {
        try
        {
            return MessageDigest.getInstance("SHA-1");
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }
}
