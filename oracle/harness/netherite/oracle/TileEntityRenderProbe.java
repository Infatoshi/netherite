package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.Charset;
import net.minecraft.block.Block;
import net.minecraft.block.BlockChest;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.renderer.ActiveRenderInfo;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.tileentity.MobSpawnerBaseLogic;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityEndPortal;
import net.minecraft.tileentity.TileEntityEnderChest;
import net.minecraft.tileentity.TileEntityMobSpawner;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.tileentity.TileEntitySkull;
import net.minecraft.util.ResourceLocation;

/**
 * What RenderGlobal.renderEntities' block-entity loop draws this frame, for
 * the native tile-entity renderers (csrc/engine/raster_tileent.c). Written into
 * each RenderStateProbe row as "tes" at the fogGL hook: by then
 * updateRenderers has settled RenderGlobal.tileEntities for the frame and
 * ActiveRenderInfo holds the frame's object-space camera. Nothing here calls
 * a method that changes state: the fields are read as the renderers read them
 * (a chest's adjacent chests were set by its own updateEntity this tick).
 *
 *   p      TileEntityRendererDispatcher.staticPlayerX/Y/Z (the translation
 *          every renderer is handed) and field_147560_j/k/l (the viewer's
 *          position the distance test and RenderEndPortal read), raw bits
 *   ari    ActiveRenderInfo.objectX/Y/Z (RenderEndPortal's parallax)
 *   list   RenderGlobal.tileEntities in order: class, position, block id,
 *          metadata, getLightBrightnessForSkyBlocks(x, y, z, 0), and each
 *          renderer's own fields
 */
final class TileEntityRenderProbe
{
    static boolean textures;

    private TileEntityRenderProbe() {}

    /** The textures the tile-entity renderers bind, once per recording. */
    static void textures(Minecraft mc, File dir) throws Exception
    {
        if (textures) return;
        textures = true;
        String[][] paths = {
            {"te_chest", "textures/entity/chest/normal.png"},
            {"te_chest_double", "textures/entity/chest/normal_double.png"},
            {"te_trapped", "textures/entity/chest/trapped.png"},
            {"te_trapped_double", "textures/entity/chest/trapped_double.png"},
            {"te_ender", "textures/entity/chest/ender.png"},
            {"te_sign", "textures/entity/sign.png"},
            {"te_steve", "textures/entity/steve.png"},
            {"te_end_sky", "textures/environment/end_sky.png"},
            {"te_end_portal", "textures/entity/end_portal.png"},
            {"te_ascii", "textures/font/ascii.png"}
        };
        JsonObject info = new JsonObject();
        for (String[] pair : paths)
            info.add(pair[0], RenderStateProbe.dumpTexture(mc, pair[0], new ResourceLocation(pair[1])));
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(dir, "te.json")), Charset.forName("UTF-8")));
        w.println(info.toString());
        w.close();
    }

    static JsonArray d3(double x, double y, double z)
    {
        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(RenderStateProbe.d(x)));
        a.add(new JsonPrimitive(RenderStateProbe.d(y)));
        a.add(new JsonPrimitive(RenderStateProbe.d(z)));
        return a;
    }

    /** A chest's neighbour at (x, y, z) as TileEntityChest.func_145977_a tests it. */
    static boolean chestAt(WorldClient w, int x, int y, int z, int type)
    {
        Block b = w.getBlock(x, y, z);
        return b instanceof BlockChest && ((BlockChest)b).field_149956_a == type;
    }

    static JsonObject tes(Minecraft mc, float pt) throws Exception
    {
        WorldClient w = mc.theWorld;
        JsonObject o = new JsonObject();
        // func_147542_a's viewer position, which is also staticPlayerX/Y/Z (both
        // are set later in the frame, inside renderEntities)
        Entity v = mc.renderViewEntity;
        o.add("p", d3(v.lastTickPosX + (v.posX - v.lastTickPosX) * (double)pt,
            v.lastTickPosY + (v.posY - v.lastTickPosY) * (double)pt,
            v.lastTickPosZ + (v.posZ - v.lastTickPosZ) * (double)pt));
        JsonArray ari = new JsonArray();
        ari.add(new JsonPrimitive(RenderStateProbe.f(ActiveRenderInfo.objectX)));
        ari.add(new JsonPrimitive(RenderStateProbe.f(ActiveRenderInfo.objectY)));
        ari.add(new JsonPrimitive(RenderStateProbe.f(ActiveRenderInfo.objectZ)));
        o.add("ari", ari);
        // RenderEndPortal's scroll reads Minecraft.getSystemTime()
        o.addProperty("ms", Minecraft.getSystemTime());
        JsonArray list = new JsonArray();
        for (Object obj : mc.renderGlobal.tileEntities)
        {
            TileEntity te = (TileEntity)obj;
            int x = te.field_145851_c, y = te.field_145848_d, z = te.field_145849_e;
            JsonObject m = new JsonObject();
            m.addProperty("k", te.getClass().getSimpleName());
            m.addProperty("x", x);
            m.addProperty("y", y);
            m.addProperty("z", z);
            m.addProperty("id", Block.getIdFromBlock(w.getBlock(x, y, z)));
            m.addProperty("meta", te.blockMetadata != -1 ? te.blockMetadata : w.getBlockMetadata(x, y, z));
            m.addProperty("brf", w.getLightBrightnessForSkyBlocks(x, y, z, 0));
            if (te instanceof TileEntityChest)
            {
                TileEntityChest c = (TileEntityChest)te;
                RenderStateProbe.put(m, "lid", c.field_145989_m);
                RenderStateProbe.put(m, "plid", c.field_145986_n);
                // zNeg, xPos, xNeg, zPos. A chest the chunk rebuild made this frame has
                // not ticked: the renderer's own func_145979_i finds them then.
                Block own = w.getBlock(x, y, z);
                int type = own instanceof BlockChest ? ((BlockChest)own).field_149956_a : 0;
                boolean fresh = !c.field_145984_a;
                JsonArray adj = new JsonArray();
                adj.add(new JsonPrimitive((fresh ? chestAt(w, x, y, z - 1, type) : c.field_145992_i != null) ? 1 : 0));
                adj.add(new JsonPrimitive((fresh ? chestAt(w, x + 1, y, z, type) : c.field_145990_j != null) ? 1 : 0));
                adj.add(new JsonPrimitive((fresh ? chestAt(w, x - 1, y, z, type) : c.field_145991_k != null) ? 1 : 0));
                adj.add(new JsonPrimitive((fresh ? chestAt(w, x, y, z + 1, type) : c.field_145988_l != null) ? 1 : 0));
                m.add("adj", adj);
                m.addProperty("using", c.field_145987_o);
            }
            else if (te instanceof TileEntityEnderChest)
            {
                TileEntityEnderChest c = (TileEntityEnderChest)te;
                RenderStateProbe.put(m, "lid", c.field_145972_a);
                RenderStateProbe.put(m, "plid", c.field_145975_i);
                m.addProperty("using", c.field_145973_j);
            }
            else if (te instanceof TileEntitySign)
            {
                TileEntitySign s = (TileEntitySign)te;
                JsonArray text = new JsonArray();
                for (String line : s.field_145915_a) text.add(new JsonPrimitive(line));
                m.add("text", text);
                m.addProperty("edit", s.field_145918_i);
            }
            else if (te instanceof TileEntitySkull)
            {
                TileEntitySkull s = (TileEntitySkull)te;
                m.addProperty("type", s.func_145904_a());
                m.addProperty("rot", s.func_145906_b());
            }
            else if (te instanceof net.minecraft.tileentity.TileEntityEnchantmentTable)
            {
                // RenderEnchantmentTable's inputs: the tick count, the page flip,
                // the spread and the turn with their last values
                net.minecraft.tileentity.TileEntityEnchantmentTable b = (net.minecraft.tileentity.TileEntityEnchantmentTable)te;
                m.addProperty("ticks", b.field_145926_a);
                RenderStateProbe.put(m, "flip", b.field_145933_i);
                RenderStateProbe.put(m, "pflip", b.field_145931_j);
                RenderStateProbe.put(m, "spread", b.field_145930_m);
                RenderStateProbe.put(m, "pspread", b.field_145927_n);
                RenderStateProbe.put(m, "rot", b.field_145928_o);
                RenderStateProbe.put(m, "prot", b.field_145925_p);
            }
            else if (te instanceof TileEntityMobSpawner)
            {
                MobSpawnerBaseLogic l = ((TileEntityMobSpawner)te).func_145881_a();
                RenderStateProbe.put(m, "rot", l.field_98287_c);
                RenderStateProbe.put(m, "prot", l.field_98284_d);
                m.addProperty("delay", l.spawnDelay);
                Entity e = (Entity)RenderStateProbe.getf(l, "field_98291_j");
                m.addProperty("mob", e != null ? EntityList.getEntityString(e) : l.getEntityNameToSpawn());
                m.addProperty("made", e != null ? 1 : 0);
                // the display entity, once the renderer made it: the
                // EntityLivingBase constructor draws its head yaw from Math.random,
                // and RendererLivingEntity reads these as for any mob
                if (e instanceof net.minecraft.entity.EntityLivingBase)
                {
                    net.minecraft.entity.EntityLivingBase b = (net.minecraft.entity.EntityLivingBase)e;
                    RenderStateProbe.put(m, "byaw", RenderStateProbe.mobYaw(b.prevRenderYawOffset, b.renderYawOffset, pt));
                    RenderStateProbe.put(m, "hyaw", RenderStateProbe.mobYaw(b.prevRotationYawHead, b.rotationYawHead, pt));
                    RenderStateProbe.put(m, "pitch", b.prevRotationPitch + (b.rotationPitch - b.prevRotationPitch) * pt);
                    m.addProperty("age", b.ticksExisted);
                }
            }
            list.add(m);
        }
        o.add("list", list);
        return o;
    }
}
