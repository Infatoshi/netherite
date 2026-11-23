package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.List;
import java.util.Map;
import net.minecraft.entity.DataWatcher;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.ai.EntityAITasks;
import net.minecraft.item.ItemStack;
import net.minecraft.pathfinding.PathEntity;
import net.minecraft.pathfinding.PathPoint;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.Vec3;
import net.minecraft.village.Village;
import net.minecraft.village.VillageDoorInfo;

/**
 * An entity's runtime state, the part NBT does not carry: every instance
 * field of its class chain (Entity down to its own class), its DataWatcher
 * entries and their dirty flags, and for an EntityLiving the two AI task
 * lists (tickCount, the executing entries in order, each task's own fields)
 * and the look, move, jump and body helpers and the navigator. A snapshot
 * taken at any tick carries this so the replay resumes mid-AI, mid-jump,
 * mid-path (Snapshot's "rt" entity key). A living's "attrs" holds each
 * attribute's value (getAttributeValue).
 *
 * Values: the canonical scalars (d: f: i: l: s: b:, floats and doubles as raw
 * bits), an int[] as "ia:"; the rest as canonical strings ("str:" then the form): an entity reference "e:<entity id>" (e:-1 for null); a PathEntity
 * "p:<index>;x,y,z;..." ("p:" for null); a Vec3 "v:<d>,<d>,<d>"; an
 * AxisAlignedBB "bb:<minX>,<minY>,<minZ>,<maxX>,<maxY>,<maxZ>" in raw
 * double bits; ChunkCoordinates "c:x,y,z"; a Village "vi:<center x,y,z>"; a
 * VillageDoorInfo "vd:x,y,z"; an ItemStack "is:id,count,damage"; a double[][]
 * "dd:" (rows by ';', raw double bits by ','); a list of
 * entities "el:id,id"; a list of door infos "vdl:x,y,z;...". Anything else
 * (the world, Randoms, maps, attribute instances) is left out: Randoms are
 * their own keys, the rest is derived or in NBT. A field whose simple name
 * repeats up the chain is keyed name@Class.
 */
final class EntityState
{
    private EntityState() {}

    static JsonObject of(Entity e)
    {
        JsonObject o = new JsonObject();
        o.add("f", fields(e, Object.class));
        if (e instanceof EntityLiving)
        {
            // the Leash tag a load keeps until the entity's first update
            // resolves it (recreateLeash): "lh:u,<UUIDMost>,<UUIDLeast>",
            // "lh:k,<X>,<Y>,<Z>" or "lh:" for neither (absent when null)
            net.minecraft.nbt.NBTTagCompound lt = (net.minecraft.nbt.NBTTagCompound)Snapshot.objField(e, "field_110170_bx");
            if (lt != null)
            {
                String v = "lh:";
                if (lt.func_150297_b("UUIDMost", 4) && lt.func_150297_b("UUIDLeast", 4))
                    v = "lh:u," + lt.getLong("UUIDMost") + "," + lt.getLong("UUIDLeast");
                else if (lt.func_150297_b("X", 99) && lt.func_150297_b("Y", 99) && lt.func_150297_b("Z", 99))
                    v = "lh:k," + lt.getInteger("X") + "," + lt.getInteger("Y") + "," + lt.getInteger("Z");
                o.getAsJsonObject("f").addProperty("field_110170_bx", "str:" + v);
            }
        }
        o.add("dw", watcher(e.getDataWatcher()));
        if (e instanceof net.minecraft.entity.EntityLivingBase)
        {
            // each attribute's value now (the modifiers NBT leaves out, the
            // sprint boost, are in it)
            JsonObject at = new JsonObject();
            for (Object a : ((net.minecraft.entity.EntityLivingBase)e).getAttributeMap().getAllAttributes())
            {
                net.minecraft.entity.ai.attributes.IAttributeInstance ai = (net.minecraft.entity.ai.attributes.IAttributeInstance)a;
                at.addProperty(ai.getAttribute().getAttributeUnlocalizedName(), Snapshot.dhex(ai.getAttributeValue()));
            }
            o.add("attrs", at);
        }
        if (e instanceof EntityLiving)
        {
            EntityLiving l = (EntityLiving)e;
            JsonObject ai = new JsonObject();
            ai.add("tasks", tasks((EntityAITasks)Snapshot.objField(l, "tasks")));
            ai.add("target", tasks((EntityAITasks)Snapshot.objField(l, "targetTasks")));
            ai.add("look", fields(Snapshot.objField(l, "lookHelper"), Object.class));
            ai.add("move", fields(Snapshot.objField(l, "moveHelper"), Object.class));
            ai.add("jump", fields(Snapshot.objField(l, "jumpHelper"), Object.class));
            ai.add("body", fields(Snapshot.objField(l, "bodyHelper"), Object.class));
            ai.add("nav", fields(Snapshot.objField(l, "navigator"), Object.class));
            o.add("ai", ai);
        }
        return o;
    }

    static JsonObject tasks(EntityAITasks t)
    {
        JsonObject o = new JsonObject();
        o.addProperty("tc", "i:" + Snapshot.intField(t, "tickCount"));
        List entries = (List)Snapshot.objField(t, "taskEntries");
        List exec = (List)Snapshot.objField(t, "executingTaskEntries");
        StringBuilder x = new StringBuilder("ia:");
        for (int i = 0; i < exec.size(); ++i)
        {
            if (i > 0) x.append(',');
            x.append(entries.indexOf(exec.get(i)));
        }
        o.addProperty("exec", x.toString());
        JsonArray a = new JsonArray();
        for (Object en : entries)
        {
            Object action = Snapshot.objField(en, "action");
            JsonObject f = fields(action, Object.class);
            f.addProperty("cls", "str:" + action.getClass().getSimpleName());
            f.addProperty("pri", "i:" + Snapshot.intField(en, "priority"));
            a.add(f);
        }
        o.add("e", a);
        return o;
    }

    /** Every instance field from o's class up to (not including) stop. */
    static JsonObject fields(Object o, Class<?> stop)
    {
        JsonObject out = new JsonObject();
        for (Class<?> k = o.getClass(); k != null && k != stop; k = k.getSuperclass())
        {
            for (Field f : declared(k))
            {
                String v;
                try
                {
                    v = WALLCLOCK.contains(k.getName() + "." + f.getName()) ? value(f.getType(), zero(f.getType())) : value(f.getType(), f.get(o));
                }
                catch (Exception ex)
                {
                    throw new IllegalStateException("EntityState: " + k.getName() + "." + f.getName() + ": " + ex);
                }
                if (v == null) continue;
                String key = f.getName();
                if (out.has(key)) key = key + "@" + k.getSimpleName();
                out.addProperty(key, v);
            }
        }
        return out;
    }

    /* Fields whose value is the wall clock, not tick state, written as zero
     * (as worldinfo.nbt's LastPlayed is) so two runs of one script give the
     * same snapshot: EntityPlayerMP's last-action time (func_143004_u,
     * MinecraftServer.getSystemTimeMillis), the keepalive's id and send time
     * (NetHandlerPlayServer.func_147363_d, System.nanoTime in ms) and the ping
     * that keepalive's round trip averages. No native load reads them. */
    static final java.util.Set<String> WALLCLOCK = new java.util.HashSet<String>(java.util.Arrays.asList(
        "net.minecraft.entity.player.EntityPlayerMP.field_143005_bX",
        "net.minecraft.entity.player.EntityPlayerMP.ping",
        "net.minecraft.network.NetHandlerPlayServer.field_147378_h",
        "net.minecraft.network.NetHandlerPlayServer.field_147379_i"));

    static Object zero(Class<?> t)
    {
        if (t == long.class) return Long.valueOf(0L);
        if (t == int.class) return Integer.valueOf(0);
        throw new IllegalStateException("EntityState: no zero for " + t);
    }

    /* a class's own instance fields, accessible, in getDeclaredFields order
     * (which copies every Field on each call) */
    static final java.util.concurrent.ConcurrentHashMap<Class<?>, Field[]> declared =
        new java.util.concurrent.ConcurrentHashMap<Class<?>, Field[]>();

    static Field[] declared(Class<?> k)
    {
        Field[] fs = declared.get(k);
        if (fs != null) return fs;
        java.util.List<Field> l = new java.util.ArrayList<Field>();
        for (Field f : k.getDeclaredFields())
        {
            if (Modifier.isStatic(f.getModifiers())) continue;
            f.setAccessible(true);
            l.add(f);
        }
        fs = l.toArray(new Field[0]);
        declared.put(k, fs);
        return fs;
    }

    /* the typed scalars stay canonical NBT scalars; every other encoding
     * rides as a "str:" string, so the file parses as canonical NBT too */
    static String value(Class<?> t, Object v)
    {
        String r = raw(t, v);
        if (r == null || t.isPrimitive() || t == int[].class) return r;
        return "str:" + r;
    }

    static String raw(Class<?> t, Object v)
    {
        if (t == double.class) return Snapshot.dhex((Double)v);
        if (t == float.class) return Snapshot.fhex((Float)v);
        if (t == int.class) return "i:" + v;
        if (t == long.class) return "l:" + v;
        if (t == short.class) return "s:" + v;
        if (t == byte.class) return "b:" + v;
        if (t == boolean.class) return "b:" + (((Boolean)v).booleanValue() ? 1 : 0);
        if (t == int[].class)
        {
            if (v == null) return null;
            StringBuilder b = new StringBuilder("ia:");
            int[] a = (int[])v;
            for (int i = 0; i < a.length; ++i) b.append(i > 0 ? "," : "").append(a[i]);
            return b.toString();
        }
        if (t == double[][].class)
        {
            // EntityDragon's ringBuffer: rows by ';', each row's doubles as raw bits by ','
            if (v == null) return null;
            StringBuilder b = new StringBuilder("dd:");
            double[][] a = (double[][])v;
            for (int i = 0; i < a.length; ++i)
            {
                if (i > 0) b.append(';');
                for (int j = 0; j < a[i].length; ++j) b.append(j > 0 ? "," : "").append(hex(a[i][j]));
            }
            return b.toString();
        }
        if (Entity.class.isAssignableFrom(t))
        {
            if (v != null) referenced((Entity)v);
            return "e:" + (v == null ? -1 : ((Entity)v).getEntityId());
        }
        if (t == PathEntity.class) return path((PathEntity)v);
        if (t == Vec3.class)
        {
            if (v == null) return "v:";
            Vec3 p = (Vec3)v;
            return "v:" + hex(p.xCoord) + "," + hex(p.yCoord) + "," + hex(p.zCoord);
        }
        if (t == AxisAlignedBB.class)
        {
            if (v == null) return "bb:";
            AxisAlignedBB b = (AxisAlignedBB)v;
            return "bb:" + hex(b.minX) + "," + hex(b.minY) + "," + hex(b.minZ) + "," + hex(b.maxX) + "," + hex(b.maxY) + "," + hex(b.maxZ);
        }
        if (t == ChunkCoordinates.class)
        {
            if (v == null) return "c:";
            ChunkCoordinates c = (ChunkCoordinates)v;
            return "c:" + c.posX + "," + c.posY + "," + c.posZ;
        }
        if (t == Village.class)
        {
            if (v == null) return "vi:";
            ChunkCoordinates c = ((Village)v).getCenter();
            return "vi:" + c.posX + "," + c.posY + "," + c.posZ;
        }
        if (t == VillageDoorInfo.class)
        {
            if (v == null) return "vd:";
            VillageDoorInfo d = (VillageDoorInfo)v;
            return "vd:" + d.posX + "," + d.posY + "," + d.posZ;
        }
        if (t == ItemStack.class)
        {
            if (v == null) return "is:";
            ItemStack s = (ItemStack)v;
            return "is:" + net.minecraft.item.Item.getIdFromItem(s.getItem()) + "," + s.stackSize + "," + s.getItemDamage();
        }
        if (List.class.isAssignableFrom(t) && v != null)
        {
            List l = (List)v;
            if (l.isEmpty()) return null;
            Object first = l.get(0);
            if (first instanceof Entity)
            {
                StringBuilder b = new StringBuilder("el:");
                for (int i = 0; i < l.size(); ++i)
                {
                    b.append(i > 0 ? "," : "").append(((Entity)l.get(i)).getEntityId());
                    referenced((Entity)l.get(i));
                }
                return b.toString();
            }
            if (first instanceof VillageDoorInfo)
            {
                StringBuilder b = new StringBuilder("vdl:");
                for (int i = 0; i < l.size(); ++i)
                {
                    VillageDoorInfo d = (VillageDoorInfo)l.get(i);
                    b.append(i > 0 ? ";" : "").append(d.posX).append(',').append(d.posY).append(',').append(d.posZ);
                }
                return b.toString();
            }
        }
        return null;
    }

    /* the entities the values written since collect() named, in the order
     * they were first named (Snapshot.writeEntities writes the ones no world
     * lists: an entity whose chunk unloaded, or a dead one, that a listed
     * entity still holds) */
    static final ThreadLocal<java.util.List<Entity>> refs = new ThreadLocal<java.util.List<Entity>>();
    static final ThreadLocal<java.util.Set<Entity>> refSet = new ThreadLocal<java.util.Set<Entity>>();

    static void collect(boolean on)
    {
        refs.set(on ? new java.util.ArrayList<Entity>() : null);
        refSet.set(on ? java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<Entity, Boolean>()) : null);
    }

    static java.util.List<Entity> collected()
    {
        java.util.List<Entity> l = refs.get();
        return l == null ? new java.util.ArrayList<Entity>() : new java.util.ArrayList<Entity>(l);
    }

    static void referenced(Entity e)
    {
        java.util.Set<Entity> s = refSet.get();
        if (s != null && s.add(e)) refs.get().add(e);
    }

    static String path(PathEntity p)
    {
        if (p == null) return "p:";
        PathPoint[] pts = (PathPoint[])Snapshot.objField(p, "points");
        StringBuilder b = new StringBuilder("p:").append(p.getCurrentPathIndex()).append(',').append(p.getCurrentPathLength());
        for (PathPoint pt : pts) b.append(';').append(pt.xCoord).append(',').append(pt.yCoord).append(',').append(pt.zCoord);
        return b.toString();
    }

    static String hex(double d)
    {
        return Snapshot.hex(Double.doubleToRawLongBits(d), 16);
    }

    /** The watched objects by id: "<type>:<value>", and "dirty" (the ids whose watched flag is set, then objectChanged). */
    static JsonObject watcher(DataWatcher w)
    {
        JsonObject o = new JsonObject();
        Map m = (Map)Snapshot.objField(w, "watchedObjects");
        StringBuilder dirty = new StringBuilder("ia:");
        int nd = 0;
        java.util.TreeMap sorted = new java.util.TreeMap(m);
        for (Object en : sorted.entrySet())
        {
            Object wo = ((Map.Entry)en).getValue();
            int id = ((Integer)((Map.Entry)en).getKey()).intValue();
            Object v = Snapshot.objField(wo, "watchedObject");
            String s;
            if (v instanceof Byte) s = "b:" + v;
            else if (v instanceof Short) s = "s:" + v;
            else if (v instanceof Integer) s = "i:" + v;
            else if (v instanceof Float) s = Snapshot.fhex((Float)v);
            else if (v instanceof String) s = "str:sx:" + utf8hex((String)v);
            else if (v instanceof ItemStack) s = value(ItemStack.class, v);
            else if (v instanceof ChunkCoordinates) s = value(ChunkCoordinates.class, v);
            else s = "str:?";
            o.addProperty(Integer.toString(id), s);
            if (Snapshot.boolField(wo, "watched"))
            {
                if (nd++ > 0) dirty.append(',');
                dirty.append(id);
            }
        }
        o.addProperty("dirty", dirty.toString());
        o.addProperty("changed", "b:" + (Snapshot.boolField(w, "objectChanged") ? 1 : 0));
        return o;
    }

    static String utf8hex(String s)
    {
        try
        {
            byte[] b = s.getBytes("UTF-8");
            StringBuilder o = new StringBuilder();
            for (byte x : b) o.append(Snapshot.hex(x & 255, 2));
            return o.toString();
        }
        catch (java.io.UnsupportedEncodingException e)
        {
            throw new IllegalStateException(e);
        }
    }
}
