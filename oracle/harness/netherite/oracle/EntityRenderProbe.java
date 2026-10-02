package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.Charset;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.entity.item.EntityFallingBlock;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.entity.item.EntityEnderPearl;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemBlock;
import net.minecraft.item.ItemPotion;
import net.minecraft.item.ItemStack;
import net.minecraft.potion.PotionHelper;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;

/**
 * The frame inputs of the entity renders that are not living models, and the
 * living models' equipment, for RenderStateProbe's row (lane/entrender).
 * Everything is read at the fog hook, before the frame draws, the way each
 * Render class reads it in doRender; floats and doubles go out as raw bits.
 *
 *  "ents": RenderGlobal.renderEntities' two lists in its order (the weather
 *   effects, then the loaded entities) reduced to the kinds with a native
 *   render: arrows, fireballs, the RenderSnowball sprites, primed TNT, falling
 *   blocks and lightning bolts; each with RenderManager.func_147936_a's
 *   position, yaw and lightmap, isInRangeToRender3d and the blockExists gate.
 *  equipment(): RenderBiped's, RenderWitch's and RenderIronGolem's inputs.
 *  pickup(): EntityPickupFX's own fields and the entity it draws.
 */
public final class EntityRenderProbe
{
    private EntityRenderProbe() {}

    static String f(float v) { return RenderStateProbe.f(v); }
    static String d(double v) { return RenderStateProbe.d(v); }
    static void put(JsonObject o, String k, float v) { o.addProperty(k, f(v)); }
    static void put(JsonObject o, String k, double v) { o.addProperty(k, d(v)); }

    static boolean textures;

    /** The entity textures the passes bind, once per recording. */
    static void textures(Minecraft mc) throws Exception
    {
        if (textures) return;
        textures = true;
        JsonObject info = new JsonObject();
        String[][] paths = {
            {"arrow", "textures/entity/arrow.png"},
            {"zombie_villager", "textures/entity/zombie/zombie_villager.png"},
            {"painting", "textures/painting/paintings_kristoffer_zetterstrand.png"},
        };
        for (String[] p : paths) info.add(p[0], RenderStateProbe.dumpTexture(mc, p[0], new ResourceLocation(p[1])));
        String[] mats = {"leather", "chainmail", "iron", "diamond", "gold"};
        for (String m : mats)
            for (int layer = 1; layer <= 2; ++layer)
            {
                String n = "armor_" + m + "_" + layer;
                info.add(n, RenderStateProbe.dumpTexture(mc, n,
                    new ResourceLocation("textures/models/armor/" + m + "_layer_" + layer + ".png")));
                if (m.equals("leather"))
                    info.add(n + "_overlay", RenderStateProbe.dumpTexture(mc, n + "_overlay",
                        new ResourceLocation("textures/models/armor/" + m + "_layer_" + layer + "_overlay.png")));
            }
        PrintWriter w = new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(new File(RenderStateProbe.dir, "entities.json")), Charset.forName("UTF-8")));
        w.println(info.toString());
        w.close();
    }

    static void icon(JsonObject o, IIcon ic)
    {
        if (ic == null) return;
        o.addProperty("icon", ic.getIconName());
        put(o, "minU", ic.getMinU());
        put(o, "maxU", ic.getMaxU());
        put(o, "minV", ic.getMinV());
        put(o, "maxV", ic.getMaxV());
        o.addProperty("iw", ic.getIconWidth());
        o.addProperty("ih", ic.getIconHeight());
    }

    /** A block's bounds as they stand (renderBlockAsItem's setRenderBoundsFromBlock reads them). */
    static JsonArray bounds(Block b)
    {
        JsonArray a = new JsonArray();
        a.add(new JsonPrimitive(d(b.getBlockBoundsMinX())));
        a.add(new JsonPrimitive(d(b.getBlockBoundsMinY())));
        a.add(new JsonPrimitive(d(b.getBlockBoundsMinZ())));
        a.add(new JsonPrimitive(d(b.getBlockBoundsMaxX())));
        a.add(new JsonPrimitive(d(b.getBlockBoundsMaxY())));
        a.add(new JsonPrimitive(d(b.getBlockBoundsMaxZ())));
        return a;
    }

    /**
     * One stack as ItemRenderer.renderItem(entity, stack, pass) and RenderBiped
     * read it: the block path's gate and the block's bounds, the item paths'
     * flags, and each pass's tint and icon as the entity's getItemIcon picks it.
     */
    static JsonObject stack(EntityLivingBase e, ItemStack s)
    {
        if (s == null || s.getItem() == null) return null;
        Item it = s.getItem();
        JsonObject o = new JsonObject();
        o.addProperty("id", Item.getIdFromItem(it));
        o.addProperty("dmg", s.getItemDamage());
        o.addProperty("sprite", s.getItemSpriteNumber());
        Block b = Block.getBlockFromItem(it);
        if (it instanceof ItemBlock && b != null && b != Blocks.air)
        {
            o.addProperty("rt", b.getRenderType());
            o.addProperty("b3d", s.getItemSpriteNumber() == 0 && RenderBlocks.renderItemIn3d(b.getRenderType()) ? 1 : 0);
            o.addProperty("ib3d", RenderBlocks.renderItemIn3d(b.getRenderType()) ? 1 : 0);
            o.addProperty("bpass", b.getRenderBlockPass());
            o.addProperty("rc", b.getRenderColor(s.getItemDamage()));
            o.add("bounds", bounds(b));
        }
        o.addProperty("full3d", it.isFull3D() ? 1 : 0);
        o.addProperty("rot", it.shouldRotateAroundWhenRendering() ? 1 : 0);
        o.addProperty("multi", it.requiresMultipleRenderPasses() ? 1 : 0);
        o.addProperty("bow", it == Items.bow ? 1 : 0);
        o.addProperty("eff", s.hasEffect() ? 1 : 0);
        JsonArray passes = new JsonArray();
        for (int pass = 0; pass < (it.requiresMultipleRenderPasses() ? 2 : 1); ++pass)
        {
            JsonObject q = new JsonObject();
            q.addProperty("tint", it.getColorFromItemStack(s, pass));
            icon(q, e != null ? e.getItemIcon(s, pass) : s.getIconIndex());
            passes.add(q);
        }
        o.add("passes", passes);
        if (it instanceof ItemArmor)
        {
            ItemArmor a = (ItemArmor)it;
            o.addProperty("ari", a.renderIndex);
            o.addProperty("cloth", a.getArmorMaterial() == ItemArmor.ArmorMaterial.CLOTH ? 1 : 0);
            if (a.getArmorMaterial() == ItemArmor.ArmorMaterial.CLOTH) o.addProperty("color", a.getColor(s));
        }
        return o;
    }

    /** The living model extras: equipment slots 0 (held) to 4 (helmet) and the model switches. */
    static void living(JsonObject m, EntityLivingBase e)
    {
        JsonArray eq = new JsonArray();
        for (int i = 0; i < 5; ++i) eq.add(stack(e, e.getEquipmentInSlot(i)));
        m.add("equip", eq);
        m.addProperty("sneak", e.isSneaking() ? 1 : 0);
        m.addProperty("riding", e.isRiding() ? 1 : 0);
        put(m, "yoff", e.yOffset);
        put(m, "fdy", (float)(e.posY - e.boundingBox.minY));
        if (e instanceof net.minecraft.entity.monster.EntityZombie)
        {
            net.minecraft.entity.monster.EntityZombie z = (net.minecraft.entity.monster.EntityZombie)e;
            m.addProperty("zvil", z.isVillager() ? 1 : 0);
            m.addProperty("conv", z.isConverting() ? 1 : 0);
        }
    }

    static JsonObject base(Entity e, float pt, boolean weather)
    {
        JsonObject o = new JsonObject();
        o.addProperty("id", e.getEntityId());
        o.addProperty("class", e.getClass().getSimpleName());
        if (weather) o.addProperty("weather", 1);
        boolean fresh = e.ticksExisted == 0;
        put(o, "x", fresh ? e.posX : e.lastTickPosX + (e.posX - e.lastTickPosX) * (double)pt);
        put(o, "y", fresh ? e.posY : e.lastTickPosY + (e.posY - e.lastTickPosY) * (double)pt);
        put(o, "z", fresh ? e.posZ : e.lastTickPosZ + (e.posZ - e.lastTickPosZ) * (double)pt);
        put(o, "yaw", e.rotationYaw);
        put(o, "pyaw", e.prevRotationYaw);
        put(o, "pitch", e.rotationPitch);
        put(o, "ppitch", e.prevRotationPitch);
        o.addProperty("age", e.ticksExisted);
        o.addProperty("burning", e.isBurning() ? 1 : 0);
        o.addProperty("brf", e.isBurning() ? 15728880 : e.getBrightnessForRender(pt));
        put(o, "bright", e.getBrightness(pt));
        put(o, "width", e.width);
        put(o, "height", e.height);
        put(o, "fdy", (float)(e.posY - e.boundingBox.minY));
        return o;
    }

    static IIcon sprite(Entity e)
    {
        if (e instanceof EntitySnowball) return Items.snowball.getIconFromDamage(0);
        if (e instanceof EntityEnderPearl) return Items.ender_pearl.getIconFromDamage(0);
        if (e instanceof EntityEnderEye) return Items.ender_eye.getIconFromDamage(0);
        if (e instanceof EntityEgg) return Items.egg.getIconFromDamage(0);
        if (e instanceof EntityPotion) return Items.potionitem.getIconFromDamage(16384);
        if (e instanceof EntityExpBottle) return Items.experience_bottle.getIconFromDamage(0);
        return null;
    }

    static JsonObject thing(Minecraft mc, WorldClient w, Entity e, float pt, boolean weather,
                            double cx, double cy, double cz)
    {
        String kind = null;
        if (e instanceof EntityLightningBolt) kind = "bolt";
        else if (e instanceof EntityArrow) kind = "arrow";
        else if (e instanceof EntityLargeFireball) kind = "fireball";
        else if (e instanceof EntitySmallFireball) kind = "smallfireball";
        else if (sprite(e) != null) kind = "sprite";
        else if (e instanceof EntityTNTPrimed) kind = "tnt";
        else if (e instanceof EntityFallingBlock) kind = "falling";
        else if (e instanceof EntityItem) kind = "item";
        else if (e instanceof EntityXPOrb) kind = "orb";
        if (kind == null) return null;
        JsonObject o = base(e, pt, weather);
        o.addProperty("k", kind);
        boolean vis = e.isInRangeToRender3d(cx, cy, cz)
            && (weather || w.blockExists(MathHelper.floor_double(e.posX), 0, MathHelper.floor_double(e.posZ)));
        o.addProperty("vis", vis ? 1 : 0);
        if (e instanceof EntityLightningBolt)
            o.addProperty("bolt", Long.toString(((EntityLightningBolt)e).boltVertex));
        else if (e instanceof EntityArrow)
            o.addProperty("shake", ((EntityArrow)e).arrowShake);
        else if (kind.endsWith("fireball"))
            icon(o, Items.fire_charge.getIconFromDamage(0));
        else if (kind.equals("sprite"))
        {
            IIcon ic = sprite(e);
            icon(o, ic);
            if (ic == ItemPotion.func_94589_d("bottle_splash"))
            {
                JsonObject ov = new JsonObject();
                icon(ov, ItemPotion.func_94589_d("overlay"));
                o.add("overlay", ov);
                o.addProperty("color", PotionHelper.func_77915_a(((EntityPotion)e).getPotionDamage(), false));
            }
        }
        else if (e instanceof EntityTNTPrimed)
            o.addProperty("fuse", ((EntityTNTPrimed)e).fuse);
        else if (e instanceof EntityItem)
        {
            /* MeshProbe's render_items fields, per frame */
            EntityItem ei = (EntityItem)e;
            ItemStack s = ei.getEntityItem();
            if (s == null) return null;
            o.addProperty("id", Item.getIdFromItem(s.getItem()));
            o.addProperty("meta", s.getItemDamage());
            o.addProperty("count", s.stackSize);
            o.addProperty("age", ei.age);
            o.addProperty("hover", Float.floatToRawIntBits(ei.hoverStart));
            o.addProperty("light", e.getBrightnessForRender(pt));
            o.add("sprite", RenderStateProbe.item(s));
        }
        else if (e instanceof EntityXPOrb)
        {
            EntityXPOrb xo = (EntityXPOrb)e;
            o.addProperty("value", xo.getXpValue());
            o.addProperty("color", xo.xpColor);
            o.addProperty("light", e.getBrightnessForRender(pt));
        }
        else if (e instanceof EntityFallingBlock)
        {
            EntityFallingBlock fb = (EntityFallingBlock)e;
            Block b = fb.func_145805_f();
            o.addProperty("block", Block.getIdFromBlock(b));
            o.addProperty("meta", fb.field_145814_a);
            boolean draws = b != null && b != w.getBlock(MathHelper.floor_double(e.posX),
                MathHelper.floor_double(e.posY), MathHelper.floor_double(e.posZ));
            o.addProperty("draws", draws ? 1 : 0);
            o.addProperty("bx", MathHelper.floor_double(e.posX));
            o.addProperty("by", MathHelper.floor_double(e.posY));
            o.addProperty("bz", MathHelper.floor_double(e.posZ));
            if (b != null)
            {
                o.add("bounds", bounds(b));
                /* renderBlockSandFalling's Tessellator.setBrightness */
                o.addProperty("bbr", b.getBlockBrightness(w, MathHelper.floor_double(e.posX),
                    MathHelper.floor_double(e.posY), MathHelper.floor_double(e.posZ)));
            }
        }
        return o;
    }

    static JsonArray ents(Minecraft mc, WorldClient w, float pt)
    {
        EntityLivingBase v = mc.renderViewEntity;
        double cx = v.prevPosX + (v.posX - v.prevPosX) * (double)pt;
        double cy = v.prevPosY + (v.posY - v.prevPosY) * (double)pt;
        double cz = v.prevPosZ + (v.posZ - v.prevPosZ) * (double)pt;
        JsonArray a = new JsonArray();
        for (Object obj : w.weatherEffects)
        {
            JsonObject o = thing(mc, w, (Entity)obj, pt, true, cx, cy, cz);
            if (o != null) a.add(o);
        }
        for (Object obj : w.getLoadedEntityList())
        {
            if (obj instanceof EntityLivingBase) continue;
            JsonObject o = thing(mc, w, (Entity)obj, pt, false, cx, cy, cz);
            if (o != null) a.add(o);
        }
        return a;
    }

    /**
     * EntityPickupFX.renderParticle's inputs: its age over maxAge, yOffs, the
     * picking-up entity's interpolated position and the picked-up entity as
     * RenderItem or RenderXPOrb draws it.
     */
    static void pickup(JsonObject m, Object fx, float pt) throws Exception
    {
        Entity item = (Entity)RenderStateProbe.getf(fx, "entityToPickUp");
        Entity by = (Entity)RenderStateProbe.getf(fx, "entityPickingUp");
        m.addProperty("page", ((Integer)RenderStateProbe.getf(fx, "age")).intValue());
        m.addProperty("pmax", ((Integer)RenderStateProbe.getf(fx, "maxAge")).intValue());
        put(m, "yoffs", ((Float)RenderStateProbe.getf(fx, "yOffs")).floatValue());
        put(m, "bx", by.lastTickPosX + (by.posX - by.lastTickPosX) * (double)pt);
        put(m, "by", by.lastTickPosY + (by.posY - by.lastTickPosY) * (double)pt);
        put(m, "bz", by.lastTickPosZ + (by.posZ - by.lastTickPosZ) * (double)pt);
        put(m, "ix", item.posX);
        put(m, "iy", item.posY);
        put(m, "iz", item.posZ);
        put(m, "iyaw", item.rotationYaw);
        /* the entity as RenderItem / RenderXPOrb read it, in render_items'
         * and render_orbs' fields; RenderItem draws under the lightmap the
         * particle set (its own getBrightnessForRender), RenderXPOrb sets the
         * orb's */
        JsonObject o = new JsonObject();
        if (item instanceof EntityItem)
        {
            EntityItem ei = (EntityItem)item;
            ItemStack s = ei.getEntityItem();
            o.addProperty("k", "item");
            o.addProperty("id", Item.getIdFromItem(s.getItem()));
            o.addProperty("meta", s.getItemDamage());
            o.addProperty("count", s.stackSize);
            o.addProperty("age", ei.age);
            o.addProperty("hover", Float.floatToRawIntBits(ei.hoverStart));
            o.addProperty("light", ((net.minecraft.client.particle.EntityFX)fx).getBrightnessForRender(pt));
            o.add("sprite", RenderStateProbe.item(s));
        }
        else if (item instanceof EntityXPOrb)
        {
            EntityXPOrb xo = (EntityXPOrb)item;
            o.addProperty("k", "orb");
            o.addProperty("value", xo.getXpValue());
            o.addProperty("color", xo.xpColor);
            o.addProperty("light", xo.getBrightnessForRender(pt));
        }
        m.add("ent", o);
    }
}
