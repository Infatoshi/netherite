package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.util.Queue;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.item.Item;
import net.minecraft.network.play.server.S06PacketUpdateHealth;
import net.minecraft.network.play.server.S0BPacketAnimation;
import net.minecraft.network.play.server.S19PacketEntityStatus;
import net.minecraft.network.play.server.S1CPacketEntityMetadata;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S1EPacketRemoveEntityEffect;
import net.minecraft.network.play.server.S20PacketEntityProperties;
import net.minecraft.network.play.server.S2CPacketSpawnGlobalEntity;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;

/**
 * The client state work item C5 steps natively between frame rows: each
 * RenderStateProbe row's "cs" block. What updateRenderer's FOV step read
 * (EntityRenderer.fovMultiplierTemp) and what getFOVMultiplier reads
 * (capabilities, the movementSpeed attribute with its modifiers, the item in
 * use); EntityPlayerSP's portal timer and its inputs (inPortal,
 * timeUntilPortal, the confusion effect, the player's own Random); every
 * living entity's hurt fields; WorldClient.lastLightningBolt and the client
 * bolts (lightningState, boltLivingTime, their Randoms). "pk" lists the
 * packets the tick that led to this frame processed in updateController (read
 * off the client's receive queue at the tick's start, before anything took
 * them): S06, S19, S0B, S1C (a health), S1D, S1E, S20 and S2C; "seed0" is the CLIENT role's
 * seeder at that moment, which the bolt S2C builds constructs its Random
 * from.
 */
final class ClientStateProbe
{
    private ClientStateProbe() {}

    static JsonArray pkts = new JsonArray();
    static long seed0;
    static String prand0;

    static String f(float v) { return RenderStateProbe.f(v); }
    static String d(double v) { return RenderStateProbe.d(v); }

    /** Oracle.preTick, through RenderStateProbe.tickStart. */
    static void tickStart(Minecraft mc)
    {
        pkts = new JsonArray();
        seed0 = Det.seederState(Det.CLIENT);
        prand0 = null;
        if (mc.thePlayer != null) prand0 = Rows.hex(Det.state((java.util.Random)RenderStateProbe.getf(mc.thePlayer, "rand")));
        if (mc.getNetHandler() == null) return;
        Queue q = (Queue)RenderStateProbe.getf(mc.getNetHandler().getNetworkManager(), "receivedPacketsQueue");
        for (Object o : q)
        {
            JsonObject j = packet(o);
            if (j != null) pkts.add(j);
        }
    }

    static JsonObject packet(Object o)
    {
        JsonObject j = new JsonObject();
        if (o instanceof S06PacketUpdateHealth)
        {
            S06PacketUpdateHealth p = (S06PacketUpdateHealth)o;
            j.addProperty("p", "S06");
            j.addProperty("hp", f(p.func_149332_c()));
            j.addProperty("food", p.func_149330_d());
            j.addProperty("sat", f(p.func_149331_e()));
        }
        else if (o instanceof S19PacketEntityStatus)
        {
            S19PacketEntityStatus p = (S19PacketEntityStatus)o;
            j.addProperty("p", "S19");
            j.addProperty("id", (Integer)RenderStateProbe.getf(p, "field_149164_a"));
            j.addProperty("st", p.func_149160_c());
        }
        else if (o instanceof S0BPacketAnimation)
        {
            S0BPacketAnimation p = (S0BPacketAnimation)o;
            j.addProperty("p", "S0B");
            j.addProperty("id", p.func_148978_c());
            j.addProperty("an", p.func_148977_d());
        }
        else if (o instanceof S1CPacketEntityMetadata)
        {
            // only a living entity's health (watched object 6, a float)
            S1CPacketEntityMetadata p = (S1CPacketEntityMetadata)o;
            if (p.func_149376_c() == null) return null;
            Float hp = null;
            for (Object wo : p.func_149376_c())
            {
                net.minecraft.entity.DataWatcher.WatchableObject w = (net.minecraft.entity.DataWatcher.WatchableObject)wo;
                if (w.getDataValueId() == 6 && w.getObject() instanceof Float) hp = (Float)w.getObject();
            }
            if (hp == null) return null;
            j.addProperty("p", "S1C");
            j.addProperty("id", p.func_149375_d());
            j.addProperty("hp", f(hp.floatValue()));
        }
        else if (o instanceof S1DPacketEntityEffect)
        {
            S1DPacketEntityEffect p = (S1DPacketEntityEffect)o;
            j.addProperty("p", "S1D");
            j.addProperty("id", p.func_149426_d());
            j.addProperty("eff", p.func_149427_e());
            j.addProperty("amp", p.func_149428_f());
            j.addProperty("dur", p.func_149425_g());
        }
        else if (o instanceof S1EPacketRemoveEntityEffect)
        {
            S1EPacketRemoveEntityEffect p = (S1EPacketRemoveEntityEffect)o;
            j.addProperty("p", "S1E");
            j.addProperty("id", p.func_149076_c());
            j.addProperty("eff", p.func_149075_d());
        }
        else if (o instanceof S20PacketEntityProperties)
        {
            S20PacketEntityProperties p = (S20PacketEntityProperties)o;
            j.addProperty("p", "S20");
            j.addProperty("id", p.func_149442_c());
            JsonArray a = new JsonArray();
            for (Object so : p.func_149441_d())
            {
                S20PacketEntityProperties.Snapshot s = (S20PacketEntityProperties.Snapshot)so;
                JsonObject sj = new JsonObject();
                sj.addProperty("n", s.func_151409_a());
                sj.addProperty("base", d(s.func_151410_b()));
                sj.add("mods", mods(s.func_151408_c()));
                a.add(sj);
            }
            j.add("attrs", a);
        }
        else if (o instanceof S2CPacketSpawnGlobalEntity)
        {
            S2CPacketSpawnGlobalEntity p = (S2CPacketSpawnGlobalEntity)o;
            j.addProperty("p", "S2C");
            j.addProperty("id", p.func_149052_c());
            j.addProperty("x", p.func_149051_d());
            j.addProperty("y", p.func_149050_e());
            j.addProperty("z", p.func_149049_f());
            j.addProperty("type", p.func_149053_g());
        }
        else return null;
        return j;
    }

    /** Modifiers in the collection's own iteration order: uuid, name, operation, amount. */
    static JsonArray mods(java.util.Collection c)
    {
        JsonArray a = new JsonArray();
        for (Object mo : c)
        {
            AttributeModifier m = (AttributeModifier)mo;
            JsonArray e = new JsonArray();
            e.add(new JsonPrimitive(m.getID().toString()));
            e.add(new JsonPrimitive(m.getName()));
            e.add(new JsonPrimitive(m.getOperation()));
            e.add(new JsonPrimitive(d(m.getAmount())));
            a.add(e);
        }
        return a;
    }

    static JsonObject hurt(EntityLivingBase e)
    {
        JsonObject o = new JsonObject();
        o.addProperty("id", e.getEntityId());
        o.addProperty("hurt", e.hurtTime);
        o.addProperty("mhurt", e.maxHurtTime);
        o.addProperty("hres", e.hurtResistantTime);
        o.addProperty("mhres", e.maxHurtResistantTime);
        o.addProperty("aaty", f(e.attackedAtYaw));
        o.addProperty("limba", f(e.limbSwingAmount));
        o.addProperty("hp", f(e.getHealth()));
        o.addProperty("ldmg", f((Float)RenderStateProbe.getf(e, "lastDamage")));
        o.addProperty("death", e.deathTime);
        o.addProperty("rs", Rows.hex(Det.state((java.util.Random)RenderStateProbe.getf(e, "rand"))));
        return o;
    }

    /** RenderStateProbe.doFog: the row's "cs" block. */
    static JsonObject state(Minecraft mc, EntityClientPlayerMP p, Object er, WorldClient w)
    {
        JsonObject o = new JsonObject();
        o.addProperty("seed0", Rows.hex(seed0));
        if (prand0 != null) o.addProperty("prs0", prand0);
        o.add("pk", pkts);

        // EntityRenderer.updateFovModifierHand's input, and getFOVMultiplier's
        o.addProperty("fmt", f((Float)RenderStateProbe.getf(er, "fovMultiplierTemp")));
        o.addProperty("fovnow", f(p.getFOVMultiplier()));
        o.addProperty("fly", p.capabilities.isFlying ? 1 : 0);
        o.addProperty("walk", f(p.capabilities.getWalkSpeed()));
        IAttributeInstance ms = p.getEntityAttribute(SharedMonsterAttributes.movementSpeed);
        o.addProperty("msp", d(ms.getAttributeValue()));
        o.addProperty("msb", d(ms.getBaseValue()));
        o.add("mods", mods(ms.func_111122_c()));
        o.addProperty("sprint", p.isSprinting() ? 1 : 0);
        o.addProperty("use", p.getItemInUse() == null ? -1 : Item.getIdFromItem(p.getItemInUse().getItem()));
        o.addProperty("usec", p.getItemInUseCount());
        o.addProperty("used", p.getItemInUseDuration());
        JsonArray eff = new JsonArray();
        for (Object eo : p.getActivePotionEffects())
        {
            PotionEffect pe = (PotionEffect)eo;
            JsonArray e = new JsonArray();
            e.add(new JsonPrimitive(pe.getPotionID()));
            e.add(new JsonPrimitive(pe.getAmplifier()));
            e.add(new JsonPrimitive(pe.getDuration()));
            eff.add(e);
        }
        o.add("eff", eff);

        // EntityPlayerSP.onLivingUpdate's portal timer
        o.addProperty("tip", f(p.timeInPortal));
        o.addProperty("ptip", f(p.prevTimeInPortal));
        o.addProperty("inportal", (Boolean)RenderStateProbe.getf(p, "inPortal") ? 1 : 0);
        o.addProperty("tup", p.timeUntilPortal);
        o.addProperty("conf", p.isPotionActive(Potion.confusion) ? p.getActivePotionEffect(Potion.confusion).getDuration() : -1);
        o.addProperty("prs", Rows.hex(Det.state((java.util.Random)RenderStateProbe.getf(p, "rand"))));
        o.addProperty("pid", p.getEntityId());

        // the hurt fields, the player first
        JsonArray hurts = new JsonArray();
        hurts.add(hurt(p));
        for (Object obj : w.getLoadedEntityList())
            if (obj instanceof EntityLivingBase && obj != p) hurts.add(hurt((EntityLivingBase)obj));
        o.add("hurt", hurts);

        // WorldClient.lastLightningBolt and the weather effects
        o.addProperty("lbolt", w.lastLightningBolt);
        JsonArray bolts = new JsonArray();
        for (Object obj : w.weatherEffects)
        {
            if (!(obj instanceof EntityLightningBolt)) continue;
            EntityLightningBolt b = (EntityLightningBolt)obj;
            JsonObject bj = new JsonObject();
            bj.addProperty("id", b.getEntityId());
            bj.addProperty("ls", (Integer)RenderStateProbe.getf(b, "lightningState"));
            bj.addProperty("blt", (Integer)RenderStateProbe.getf(b, "boltLivingTime"));
            bj.addProperty("bv", Long.toString(b.boltVertex));
            bj.addProperty("rs", Rows.hex(Det.state((java.util.Random)RenderStateProbe.getf(b, "rand"))));
            bj.addProperty("dead", b.isDead ? 1 : 0);
            bj.addProperty("age", b.ticksExisted);
            bolts.add(bj);
        }
        o.add("bolts", bolts);
        return o;
    }

    // ------------------------------------------------ the inside draw

    /*
     * WorldRenderer.updateRenderer draws a render-type-0 block a second time
     * from inside when it is the render view entity's block (its floored
     * position) at the time the section is built, and a section is only built
     * when it is dirty: the geometry stays until the next rebuild, wherever
     * the camera has gone since. The two hooks below keep, per renderer, the
     * view block of its last build (the agent-mode frame rebuilds every dirty
     * renderer between them), and each row lists the blocks that are drawn
     * from inside now.
     */
    static final java.util.IdentityHashMap<Object, int[]> built = new java.util.IdentityHashMap<Object, int[]>();
    static Object[] seen;
    static int[] seenPos;
    static boolean[] seenDirty;

    /** RenderStateProbe.camera: before oracleSyncForCamera and the rebuilds. */
    static void renderersBefore(Minecraft mc)
    {
        net.minecraft.client.renderer.WorldRenderer[] wr =
            (net.minecraft.client.renderer.WorldRenderer[])RenderStateProbe.getf(mc.renderGlobal, "worldRenderers");
        if (wr == null) { seen = null; return; }
        seen = wr.clone();
        seenPos = new int[wr.length * 3];
        seenDirty = new boolean[wr.length];
        for (int i = 0; i < wr.length; ++i)
        {
            seenPos[i * 3] = wr[i].posX;
            seenPos[i * 3 + 1] = wr[i].posY;
            seenPos[i * 3 + 2] = wr[i].posZ;
            seenDirty[i] = wr[i].needsUpdate;
        }
    }

    /** RenderStateProbe.fogGL: after the rebuilds; the row's "inside" list. */
    static JsonArray inside(Minecraft mc)
    {
        JsonArray a = new JsonArray();
        net.minecraft.client.renderer.WorldRenderer[] wr =
            (net.minecraft.client.renderer.WorldRenderer[])RenderStateProbe.getf(mc.renderGlobal, "worldRenderers");
        if (wr == null || mc.renderViewEntity == null) return a;
        Entity v = mc.renderViewEntity;
        int[] cam = {net.minecraft.util.MathHelper.floor_double(v.posX),
            net.minecraft.util.MathHelper.floor_double(v.posY), net.minecraft.util.MathHelper.floor_double(v.posZ)};
        for (int i = 0; i < wr.length; ++i)
        {
            boolean same = seen != null && i < seen.length && seen[i] == wr[i];
            boolean rebuilt = !same || seenDirty[i] || seenPos[i * 3] != wr[i].posX
                || seenPos[i * 3 + 1] != wr[i].posY || seenPos[i * 3 + 2] != wr[i].posZ;
            if (rebuilt) built.put(wr[i], cam.clone());
        }
        for (net.minecraft.client.renderer.WorldRenderer r : wr)
        {
            int[] c = built.get(r);
            if (c == null || c[0] < r.posX || c[0] >= r.posX + 16 || c[1] < r.posY || c[1] >= r.posY + 16
                || c[2] < r.posZ || c[2] >= r.posZ + 16) continue;
            net.minecraft.block.Block b = mc.theWorld.getBlock(c[0], c[1], c[2]);
            if (b.getMaterial() == net.minecraft.block.material.Material.air || b.getRenderType() != 0) continue;
            JsonArray e = new JsonArray();
            e.add(new JsonPrimitive(c[0]));
            e.add(new JsonPrimitive(c[1]));
            e.add(new JsonPrimitive(c[2]));
            a.add(e);
        }
        seen = null;
        return a;
    }
}
