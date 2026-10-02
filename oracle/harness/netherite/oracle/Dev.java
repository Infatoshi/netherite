package netherite.oracle;

import com.google.gson.JsonObject;
import com.google.gson.JsonElement;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.IEntityLivingData;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.monster.EntitySpider;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.WorldServer;

/** Scripted server mutations. The lockstep hook calls this before MinecraftServer.tick. */
public final class Dev
{
    public static final boolean MUTATES = true;
    private static final class Entry
    {
        final long tick;
        final JsonObject cmd;
        Entry(long t, JsonObject c) { tick = t; cmd = c; }
    }
    private static final List<Entry> pending = new ArrayList<Entry>();

    private Dev() {}

    static void queue(long tick, JsonObject cmd)
    {
        if (!Oracle.dev || Oracle.mode != Oracle.AGENT) throw new IllegalStateException("Dev requires --dev agent mode");
        validate(cmd);
        synchronized (pending) { pending.add(new Entry(tick, Rows.parse(cmd.toString()))); }
    }

    static void validate(JsonObject c)
    {
        String op = str(c, "op");
        if (!("give".equals(op) || "slot".equals(op) || "clear".equals(op) || "tp".equals(op)
            || "state".equals(op) || "xp".equals(op) || "time".equals(op) || "weather".equals(op)
            || "gamerule".equals(op) || "setblock".equals(op) || "fill".equals(op)
            || "difficulty".equals(op)
            || "summon".equals(op) || "purge".equals(op) || "orb".equals(op) || "hurt".equals(op)
            || "hanging".equals(op) || "jockey".equals(op)
            || "lightning".equals(op) || "entity".equals(op)))
            throw new IllegalArgumentException("unknown dev op " + op);
        if ("entity".equals(op))
        {
            /* the render scenes' projectiles and block entities (lane/entrender);
             * a tape with one is Java-only: the native replay has no op for it */
            number(c, "x"); number(c, "y"); number(c, "z");
            String name = str(c, "name");
            if (!("Arrow".equals(name) || "Snowball".equals(name) || "Fireball".equals(name)
                || "SmallFireball".equals(name) || "ThrownEnderpearl".equals(name)
                || "EyeOfEnderSignal".equals(name) || "ThrownPotion".equals(name)
                || "ThrownExpBottle".equals(name) || "PrimedTnt".equals(name) || "FallingSand".equals(name)))
                throw new IllegalArgumentException("entity op cannot spawn " + name);
        }
        if ("give".equals(op) || "slot".equals(op))
        {
            number(c, "item");
            int count = c.has("count") ? number(c, "count").getAsInt() : 1;
            if (count < 1 || count > 64) throw new IllegalArgumentException(op + " count must be 1..64");
            if (c.has("meta")) number(c, "meta");
            if ("slot".equals(op) && (number(c, "slot").getAsInt() < 0 || number(c, "slot").getAsInt() >= 40))
                throw new IllegalArgumentException("slot must be 0..39");
            if (c.has("name"))
            {
                String name = str(c, "name");
                if (name.length() < 1 || name.length() > 30) throw new IllegalArgumentException("name must be 1..30 characters");
                /* the native tag store keeps a compound as canonical text,
                 * which does not escape a quote */
                if (name.indexOf('"') >= 0) throw new IllegalArgumentException("name must not hold a quote");
            }
            validateEnch(c);
            if (c.has("color"))
            {
                number(c, "color");
                Item it = Item.getItemById(number(c, "item").getAsInt());
                if (!(it instanceof net.minecraft.item.ItemArmor)
                    || ((net.minecraft.item.ItemArmor)it).getArmorMaterial() != net.minecraft.item.ItemArmor.ArmorMaterial.CLOTH)
                    throw new IllegalArgumentException("color is only supported on leather armor");
            }
        }
        if ("clear".equals(op) && c.has("item")) number(c, "item");
        if ("orb".equals(op))
        {
            number(c, "x"); number(c, "y"); number(c, "z");
            int value = number(c, "value").getAsInt();
            if (value < 1 || value > 32767) throw new IllegalArgumentException("orb value must be 1..32767");
        }
        if ("lightning".equals(op))
        {
            number(c, "x"); number(c, "y"); number(c, "z");
        }
        if ("hurt".equals(op))
        {
            if (!"dragon".equals(str(c, "target"))) throw new IllegalArgumentException("hurt target must be dragon");
            int part = c.has("part") ? number(c, "part").getAsInt() : 0;
            if (part < 0 || part > 6) throw new IllegalArgumentException("hurt part must be 0..6");
            if (number(c, "amount").getAsFloat() <= 0.0F) throw new IllegalArgumentException("hurt amount must be positive");
        }
        if ("hanging".equals(op))
        {
            number(c, "x"); number(c, "y"); number(c, "z");
            String kind = str(c, "kind");
            if (!"painting".equals(kind) && !"item_frame".equals(kind))
                throw new IllegalArgumentException("hanging kind must be painting or item_frame");
            int dir = number(c, "dir").getAsInt();
            if (dir < 0 || dir > 3) throw new IllegalArgumentException("hanging dir must be 0..3");
            if (c.has("art") && "item_frame".equals(kind))
                throw new IllegalArgumentException("art is only supported on painting");
            if (c.has("item") && "painting".equals(kind))
                throw new IllegalArgumentException("item is only supported on item_frame");
            if (c.has("rot") && "painting".equals(kind))
                throw new IllegalArgumentException("rot is only supported on item_frame");
        }
        if ("summon".equals(op) && c.has("effects"))
            for (JsonElement el : c.getAsJsonArray("effects"))
            {
                com.google.gson.JsonArray a = el.getAsJsonArray();
                if (a.size() != 3) throw new IllegalArgumentException("summon effects entries are [id, amplifier, duration]");
                int id = a.get(0).getAsInt();
                if (id < 1 || id > 23) throw new IllegalArgumentException("summon effect id must be 1..23");
            }
        if ("tp".equals(op) || "summon".equals(op))
        {
            number(c, "x"); number(c, "y"); number(c, "z");
            if (c.has("yaw")) number(c, "yaw");
            if (c.has("pitch")) number(c, "pitch");
        }
        if ("jockey".equals(op))
        {
            number(c, "x"); number(c, "y"); number(c, "z");
            String kind = str(c, "kind");
            if (!"spider_skeleton".equals(kind) && !"baby_zombie_chicken".equals(kind))
                throw new IllegalArgumentException("jockey kind must be spider_skeleton or baby_zombie_chicken");
            if (c.has("attempts")) number(c, "attempts");
        }
        if ("state".equals(op))
            for (String key : new String[] {"health", "food", "saturation", "exhaustion", "air", "fire", "fall"})
                if (c.has(key)) number(c, key);
        if ("xp".equals(op))
        {
            if (c.has("points") == c.has("levels")) throw new IllegalArgumentException("xp needs points or levels");
            if (c.has("points") && number(c, "points").getAsInt() < 0)
                throw new IllegalArgumentException("xp points must be nonnegative");
            if (c.has("levels")) number(c, "levels");
        }
        if ("time".equals(op))
        {
            if (number(c, "value").getAsInt() < 0) throw new IllegalArgumentException("time value must be nonnegative");
            String mode = c.has("mode") ? str(c, "mode") : "set";
            if (!"set".equals(mode) && !"add".equals(mode)) throw new IllegalArgumentException("unknown time mode " + mode);
        }
        if ("weather".equals(op))
        {
            String type = str(c, "type");
            if (!"clear".equals(type) && !"rain".equals(type) && !"thunder".equals(type))
                throw new IllegalArgumentException("unknown weather " + type);
            if (c.has("seconds"))
            {
                int seconds = number(c, "seconds").getAsInt();
                if (seconds < 1 || seconds > 1000000) throw new IllegalArgumentException("weather seconds must be 1..1000000");
            }
        }
        if ("setblock".equals(op) || "fill".equals(op))
        {
            int id = number(c, "id").getAsInt();
            if (Block.getBlockById(id) == null) throw new IllegalArgumentException("unknown block " + id);
            int meta = c.has("meta") ? number(c, "meta").getAsInt() : 0;
            if (meta < 0 || meta > 15) throw new IllegalArgumentException("metadata must be 0..15");
            if ("setblock".equals(op))
            {
                number(c, "x"); number(c, "y"); number(c, "z");
                if (c.has("tile") && !c.get("tile").isJsonObject())
                    throw new IllegalArgumentException("setblock tile must be an object");
                String mode = c.has("mode") ? str(c, "mode") : "replace";
                if (!"replace".equals(mode) && !"keep".equals(mode) && !"destroy".equals(mode))
                    throw new IllegalArgumentException("unknown setblock mode " + mode);
            }
            else
            {
                int x0 = number(c, "x0").getAsInt(), x1 = number(c, "x1").getAsInt();
                int y0 = number(c, "y0").getAsInt(), y1 = number(c, "y1").getAsInt();
                int z0 = number(c, "z0").getAsInt(), z1 = number(c, "z1").getAsInt();
                if (x1 < x0 || y1 < y0 || z1 < z0 || (long)(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 32768)
                    throw new IllegalArgumentException("invalid fill box");
            }
        }
        if ("summon".equals(op))
        {
            String name = str(c, "name");
            WorldConf.checkSummon(name);
            if (!("Pig".equals(name) || "Cow".equals(name) || "Sheep".equals(name)
                || "Chicken".equals(name) || "Zombie".equals(name) || "Skeleton".equals(name)
                || "Spider".equals(name) || "Creeper".equals(name) || "Enderman".equals(name)
                || "Slime".equals(name) || "Witch".equals(name) || "Bat".equals(name)
                || "Squid".equals(name) || "MushroomCow".equals(name)
                || "CaveSpider".equals(name) || "Silverfish".equals(name)
                || "IronGolem".equals(name) || "Villager".equals(name)
                || "PigZombie".equals(name) || "LavaSlime".equals(name) || "Ghast".equals(name)
                || "Blaze".equals(name)))
                throw new IllegalArgumentException("native cannot construct " + name);
            if (c.has("loot")) number(c, "loot");
            if (c.has("breakdoors")) number(c, "breakdoors");
            if (c.has("equip"))
            {
                if (!c.get("equip").isJsonArray()) throw new IllegalArgumentException("summon equip must be a list");
                for (JsonElement e : c.getAsJsonArray("equip"))
                {
                    if (!e.isJsonObject()) throw new IllegalArgumentException("summon equip entries are objects");
                    JsonObject eq = e.getAsJsonObject();
                    int slot = number(eq, "slot").getAsInt();
                    if (slot < 0 || slot > 4) throw new IllegalArgumentException("equip slot must be 0..4");
                    if (Item.getItemById(number(eq, "item").getAsInt()) == null)
                        throw new IllegalArgumentException("unknown equip item");
                    validateEnch(eq);
                    if (eq.has("drop")) number(eq, "drop");
                }
            }
        }
        if ("gamerule".equals(op))
        {
            String name = str(c, "name");
            if (!("doDaylightCycle".equals(name) || "keepInventory".equals(name)))
                throw new IllegalArgumentException("native gamerule not supported yet: " + name);
            String value = str(c, "value");
            if (!"true".equals(value) && !"false".equals(value))
                throw new IllegalArgumentException("gamerule value must be true or false");
        }
        if ("difficulty".equals(op))
        {
            int v = number(c, "value").getAsInt();
            if (v < 0 || v > 3) throw new IllegalArgumentException("difficulty must be 0..3");
        }
    }

    /** An optional "ench": [[id, lvl], ...], the stack's tag.ench list in order. */
    private static void validateEnch(JsonObject c)
    {
        if (!c.has("ench")) return;
        if (!c.get("ench").isJsonArray()) throw new IllegalArgumentException("ench must be a list");
        for (JsonElement e : c.getAsJsonArray("ench"))
        {
            if (!e.isJsonArray() || e.getAsJsonArray().size() != 2)
                throw new IllegalArgumentException("ench entries are [id, lvl]");
            int id = e.getAsJsonArray().get(0).getAsInt(), lvl = e.getAsJsonArray().get(1).getAsInt();
            if (id < 0 || id >= 256 || net.minecraft.enchantment.Enchantment.enchantmentsList[id] == null)
                throw new IllegalArgumentException("unknown enchantment " + id);
            if (lvl < 1 || lvl > 32767) throw new IllegalArgumentException("enchantment level must be 1..32767");
        }
    }

    /** new ItemStack(item, count, meta) with the "ench" list added through
     * ItemStack.addEnchantment, which appends to tag.ench in order, then an
     * optional "name" through setStackDisplayName (tag.display.Name, what an
     * anvil writes). */
    private static ItemStack stack(JsonObject c, Item item, int count)
    {
        ItemStack s = new ItemStack(item, count, opt(c, "meta", 0));
        if (c.has("ench"))
            for (JsonElement e : c.getAsJsonArray("ench"))
                s.addEnchantment(net.minecraft.enchantment.Enchantment.enchantmentsList[e.getAsJsonArray().get(0).getAsInt()],
                    e.getAsJsonArray().get(1).getAsInt());
        /* ItemArmor.func_82813_b: tag.display.color, the dye a crafted
         * leather piece carries */
        if (c.has("color")) ((net.minecraft.item.ItemArmor)item).func_82813_b(s, num(c, "color"));
        if (c.has("name")) s.setStackDisplayName(str(c, "name"));
        return s;
    }

    private static JsonElement number(JsonObject c, String key)
    {
        JsonElement value = c.get(key);
        if (value == null || !value.isJsonPrimitive() || !value.getAsJsonPrimitive().isNumber())
            throw new IllegalArgumentException(key + " must be a number");
        return value;
    }

    public static void atServerTick(MinecraftServer server)
    {
        WorldConf.atServerTick(server);
        PreloadRegion.atServerTick(server);
        if (!Oracle.dev) return;
        List<Entry> now = new ArrayList<Entry>();
        synchronized (pending)
        {
            for (int i = 0; i < pending.size();)
            {
                if (pending.get(i).tick <= Oracle.tick) now.add(pending.remove(i));
                else ++i;
            }
        }
        for (Entry e : now)
        {
            try { apply((IntegratedServer)server, e.cmd); }
            catch (Exception ex) { Oracle.threw("dev t=" + e.tick, ex); System.err.println("ORACLE DEV failed t=" + e.tick + " " + ex); Oracle.finish(3); }
        }
    }

    private static String str(JsonObject c, String k) { return c.get(k).getAsString(); }
    private static int num(JsonObject c, String k) { return c.get(k).getAsInt(); }
    private static int opt(JsonObject c, String k, int v) { return c.has(k) ? num(c, k) : v; }
    private static double dbl(JsonObject c, String k) { return c.get(k).getAsDouble(); }
    private static net.minecraft.nbt.NBTTagList doubles(com.google.gson.JsonArray a)
    {
        if (a.size() != 3) throw new IllegalArgumentException("entity motion and direction are [x, y, z]");
        net.minecraft.nbt.NBTTagList l = new net.minecraft.nbt.NBTTagList();
        for (int i = 0; i < 3; ++i) l.appendTag(new net.minecraft.nbt.NBTTagDouble(a.get(i).getAsDouble()));
        return l;
    }
    private static float flt(JsonObject c, String k) { return c.get(k).getAsFloat(); }

    static void apply(IntegratedServer server, JsonObject c) throws Exception
    {
        validate(c);
        String op = str(c, "op");
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        WorldServer w = (WorldServer)p.worldObj;
        if ("give".equals(op))
        {
            Item item = Item.getItemById(num(c, "item"));
            if (item == null) throw new IllegalArgumentException("unknown item " + num(c, "item"));
            int count = opt(c, "count", 1);
            if (count < 1 || count > 64) throw new IllegalArgumentException("give count must be 1..64");
            EntityItem dropped = p.dropPlayerItemWithRandomChoice(stack(c, item, count), false);
            dropped.delayBeforeCanPickup = 0;
            dropped.func_145797_a(p.getCommandSenderName());
        }
        else if ("slot".equals(op))
        {
            int slot = num(c, "slot"), id = num(c, "item");
            if (slot < 0 || slot >= 40) throw new IllegalArgumentException("slot must be 0..39");
            Item item = Item.getItemById(id);
            if (item == null) throw new IllegalArgumentException("unknown item " + id);
            int count = opt(c, "count", 1);
            if (count < 1 || count > 64) throw new IllegalArgumentException("slot count must be 1..64");
            p.inventory.setInventorySlotContents(slot, stack(c, item, count));
            p.inventoryContainer.detectAndSendChanges();
        }
        else if ("clear".equals(op))
        {
            Item item = c.has("item") ? Item.getItemById(num(c, "item")) : null;
            p.inventory.clearInventory(item, -1);
            p.inventoryContainer.detectAndSendChanges();
            if (!p.capabilities.isCreativeMode) p.updateHeldItem();
        }
        else if ("tp".equals(op))
        {
            p.mountEntity((Entity)null);
            p.playerNetServerHandler.setPlayerLocation(dbl(c, "x"), dbl(c, "y"), dbl(c, "z"),
                c.has("yaw") ? flt(c, "yaw") : p.rotationYaw,
                c.has("pitch") ? flt(c, "pitch") : p.rotationPitch);
        }
        else if ("state".equals(op))
        {
            if (c.has("health")) p.setHealth(flt(c, "health"));
            if (c.has("food")) p.getFoodStats().setFoodLevel(num(c, "food"));
            if (c.has("saturation")) p.getFoodStats().setFoodSaturationLevel(flt(c, "saturation"));
            if (c.has("exhaustion")) Snapshot.field(p.getFoodStats(), "foodExhaustionLevel").setFloat(p.getFoodStats(), flt(c, "exhaustion"));
            if (c.has("air")) p.setAir(num(c, "air"));
            if (c.has("fire")) Snapshot.field(p, "fire").setInt(p, num(c, "fire"));
            if (c.has("fall")) p.fallDistance = flt(c, "fall");
        }
        else if ("xp".equals(op))
        {
            if (c.has("points"))
            {
                if (num(c, "points") < 0) throw new IllegalArgumentException("xp points must be nonnegative");
                p.addExperience(num(c, "points"));
            }
            else p.addExperienceLevel(num(c, "levels"));
        }
        else if ("time".equals(op))
        {
            int value = num(c, "value");
            for (WorldServer ws : server.worldServers)
                if (ws != null) ws.setWorldTime("add".equals(c.has("mode") ? str(c, "mode") : "set")
                    ? ws.getWorldTime() + value : value);
        }
        else if ("weather".equals(op))
        {
            String type = str(c, "type");
            int duration = (300 + Det.newRandom().nextInt(600)) * 20;
            if (c.has("seconds")) duration = num(c, "seconds") * 20;
            if ("clear".equals(type))
            {
                w.getWorldInfo().setRainTime(0);
                w.getWorldInfo().setThunderTime(0);
                w.getWorldInfo().setRaining(false);
                w.getWorldInfo().setThundering(false);
            }
            else if ("rain".equals(type) || "thunder".equals(type))
            {
                w.getWorldInfo().setRainTime(duration);
                if ("thunder".equals(type)) w.getWorldInfo().setThunderTime(duration);
                w.getWorldInfo().setRaining(true);
                w.getWorldInfo().setThundering("thunder".equals(type));
            }
            else throw new IllegalArgumentException("unknown weather " + type);
        }
        else if ("gamerule".equals(op))
        {
            String name = str(c, "name");
            if (!w.getGameRules().hasRule(name)) throw new IllegalArgumentException("unknown gamerule " + name);
            w.getGameRules().setOrCreateGameRule(name, str(c, "value"));
        }
        else if ("difficulty".equals(op))
        {
            server.func_147139_a(net.minecraft.world.EnumDifficulty.getDifficultyEnum(num(c, "value")));
        }
        else if ("setblock".equals(op))
        {
            int x = num(c, "x"), y = num(c, "y"), z = num(c, "z");
            Block block = Block.getBlockById(num(c, "id"));
            String mode = c.has("mode") ? str(c, "mode") : "replace";
            if (!w.blockExists(x, y, z) || block == null) throw new IllegalArgumentException("setblock target unavailable");
            if ("keep".equals(mode) && !w.isAirBlock(x, y, z)) throw new IllegalArgumentException("setblock keep occupied");
            if ("destroy".equals(mode)) w.func_147480_a(x, y, z, true);
            if (!"replace".equals(mode) && !"keep".equals(mode) && !"destroy".equals(mode)) throw new IllegalArgumentException("unknown setblock mode " + mode);
            w.setBlock(x, y, z, block, opt(c, "meta", 0), 3);
            if (c.has("tile"))
            {
                TileEntity te = w.getTileEntity(x, y, z);
                if (te == null) throw new IllegalArgumentException("setblock has no tile entity");
                JsonObject spec = c.getAsJsonObject("tile");
                NBTTagCompound tag = new NBTTagCompound();
                te.writeToNBT(tag);
                if (spec.has("EntityId")) tag.setString("EntityId", str(spec, "EntityId"));
                for (String key : new String[] {"Delay", "MinSpawnDelay", "MaxSpawnDelay",
                    "SpawnCount", "MaxNearbyEntities", "RequiredPlayerRange", "SpawnRange",
                    "BurnTime", "CookTime"})
                    if (spec.has(key)) tag.setShort(key, (short)num(spec, key));
                /* TileEntitySign's four lines and TileEntitySkull's type and
                 * rotation, the fields their readFromNBT reads */
                for (String key : new String[] {"Text1", "Text2", "Text3", "Text4"})
                    if (spec.has(key)) tag.setString(key, str(spec, key));
                for (String key : new String[] {"SkullType", "Rot"})
                    if (spec.has(key)) tag.setByte(key, (byte)num(spec, key));
                if (spec.has("Items"))
                {
                    NBTTagList items = new NBTTagList();
                    for (JsonElement entry : spec.getAsJsonArray("Items"))
                    {
                        JsonObject row = entry.getAsJsonObject();
                        Item item = Item.getItemById(num(row, "id"));
                        if (item == null) throw new IllegalArgumentException("unknown tile item");
                        NBTTagCompound slot = new NBTTagCompound();
                        new ItemStack(item, num(row, "Count"), opt(row, "Damage", 0)).writeToNBT(slot);
                        slot.setByte("Slot", (byte)num(row, "Slot"));
                        items.appendTag(slot);
                    }
                    tag.setTag("Items", items);
                }
                te.readFromNBT(tag);
                te.onInventoryChanged();
                w.func_147471_g(x, y, z);
            }
        }
        else if ("fill".equals(op))
        {
            int x0 = num(c, "x0"), x1 = num(c, "x1"), y0 = num(c, "y0"), y1 = num(c, "y1"), z0 = num(c, "z0"), z1 = num(c, "z1");
            if (x1 < x0 || y1 < y0 || z1 < z0 || (long)(x1-x0+1)*(y1-y0+1)*(z1-z0+1) > 32768)
                throw new IllegalArgumentException("invalid fill box");
            Block block = Block.getBlockById(num(c, "id"));
            if (block == null) throw new IllegalArgumentException("unknown block");
            int meta = opt(c, "meta", 0);
            for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
                w.setBlock(x, y, z, block, meta, 2);
            for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
                w.notifyBlocksOfNeighborChange(x, y, z, block);
        }
        else if ("summon".equals(op))
        {
            String name = str(c, "name");
            double x = dbl(c, "x"), y = dbl(c, "y"), z = dbl(c, "z");
            if (!w.blockExists((int)x, (int)y, (int)z)) throw new IllegalArgumentException("summon outside loaded world");
            net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
            tag.setString("id", name.equals("IronGolem") ? "VillagerGolem" : name);
            /* The two forced spawn forms the native op mirrors, through the same
             * NBT keys the game itself reads: a child (EntityZombie's IsBaby, a
             * pigman's too, and EntityAgeable's Age) and a slime size. */
            if (c.has("child") && num(c, "child") != 0)
            {
                tag.setBoolean("IsBaby", true);
                tag.setInteger("Age", -24000);
            }
            if (c.has("size")) tag.setInteger("Size", num(c, "size") - 1);
            /* a zombie villager, and effects already running when it joins
             * (the tracker sends them with the spawn): IsVillager and
             * EntityLivingBase's ActiveEffects, [id, amplifier, duration] */
            if (c.has("villager") && num(c, "villager") != 0) tag.setBoolean("IsVillager", true);
            if (c.has("effects"))
            {
                net.minecraft.nbt.NBTTagList fx = new net.minecraft.nbt.NBTTagList();
                for (JsonElement el : c.getAsJsonArray("effects"))
                {
                    com.google.gson.JsonArray a = el.getAsJsonArray();
                    net.minecraft.nbt.NBTTagCompound pe = new net.minecraft.nbt.NBTTagCompound();
                    pe.setByte("Id", (byte)a.get(0).getAsInt());
                    pe.setByte("Amplifier", (byte)a.get(1).getAsInt());
                    pe.setInteger("Duration", a.get(2).getAsInt());
                    pe.setBoolean("Ambient", false);
                    fx.appendTag(pe);
                }
                tag.setTag("ActiveEffects", fx);
            }
            Entity e = EntityList.createEntityFromNBT(tag, w);
            if (e == null) throw new IllegalArgumentException("cannot summon " + name);
            if (c.has("child") && num(c, "child") != 0 && !(e instanceof net.minecraft.entity.monster.EntityZombie)
                && !(e instanceof net.minecraft.entity.EntityAgeable))
                throw new IllegalArgumentException("child is only supported on Zombie, PigZombie and the ageables");
            if (c.has("villager") && num(c, "villager") != 0 && !(e instanceof net.minecraft.entity.monster.EntityZombie))
                throw new IllegalArgumentException("villager is only supported on Zombie");
            e.setLocationAndAngles(x, y, z, e.rotationYaw, e.rotationPitch);
            if (e instanceof EntityLiving) ((EntityLiving)e).onSpawnWithEgg((IEntityLivingData)null);
            /* "loot": setCanPickUpLoot after the egg's roll; "breakdoors": a
             * zombie's func_146070_a (the flag and EntityAIBreakDoor) */
            if (c.has("loot"))
            {
                if (!(e instanceof EntityLiving)) throw new IllegalArgumentException("loot needs a living entity");
                ((EntityLiving)e).setCanPickUpLoot(num(c, "loot") != 0);
            }
            if (c.has("breakdoors"))
            {
                if (!(e instanceof net.minecraft.entity.monster.EntityZombie)) throw new IllegalArgumentException("breakdoors is only supported on Zombie");
                ((net.minecraft.entity.monster.EntityZombie)e).func_146070_a(num(c, "breakdoors") != 0);
            }
            if (c.has("equip"))
            {
                /* equipment slots (0 the hand, 1..4 boots to helmet), replacing
                 * whatever onSpawnWithEgg gave */
                if (!(e instanceof EntityLiving)) throw new IllegalArgumentException("equip needs a living entity");
                for (JsonElement el : c.getAsJsonArray("equip"))
                {
                    JsonObject eq = el.getAsJsonObject();
                    ((EntityLiving)e).setCurrentItemOrArmor(num(eq, "slot"), stack(eq, Item.getItemById(num(eq, "item")), 1));
                    /* an optional "drop": setEquipmentDropChance (2.0 drops
                     * the piece undamaged on any death, as a picked-up item) */
                    if (eq.has("drop")) ((EntityLiving)e).setEquipmentDropChance(num(eq, "slot"), flt(eq, "drop"));
                }
            }
            w.spawnEntityInWorld(e);
        }
        else if ("entity".equals(op))
        {
            /* EntityList.createEntityFromNBT with the entity's own NBT keys as
             * readFromNBT reads them: Motion, a fireball's direction, a primed
             * TNT's Fuse, a falling block's TileID, Data and Time, a thrown
             * potion's potionValue; then the position and an optional fire */
            String name = str(c, "name");
            double x = dbl(c, "x"), y = dbl(c, "y"), z = dbl(c, "z");
            if (!w.blockExists((int)x, (int)y, (int)z)) throw new IllegalArgumentException("entity outside loaded world");
            net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
            tag.setString("id", name);
            if (c.has("motion")) tag.setTag("Motion", doubles(c.getAsJsonArray("motion")));
            if (c.has("direction")) tag.setTag("direction", doubles(c.getAsJsonArray("direction")));
            if (c.has("fuse")) tag.setByte("Fuse", (byte)num(c, "fuse"));
            if (c.has("tile")) { tag.setInteger("TileID", num(c, "tile")); tag.setByte("Time", (byte)1); }
            if (c.has("data")) tag.setByte("Data", (byte)num(c, "data"));
            if (c.has("potion")) tag.setInteger("potionValue", num(c, "potion"));
            Entity e = EntityList.createEntityFromNBT(tag, w);
            if (e == null) throw new IllegalArgumentException("cannot create " + name);
            e.setLocationAndAngles(x, y, z, c.has("yaw") ? flt(c, "yaw") : e.rotationYaw,
                c.has("pitch") ? flt(c, "pitch") : e.rotationPitch);
            if (c.has("fire")) e.setFire(num(c, "fire"));
            w.spawnEntityInWorld(e);
        }
        else if ("jockey".equals(op))
        {
            /* The summon form of EntitySpider.onSpawnWithEgg's 1-in-100
             * spider jockey and EntityZombie.onSpawnWithEgg's chicken jockey:
             * each attempt is the constructor, setLocationAndAngles and
             * onSpawnWithEgg; a miss is dropped without joining the world
             * (its draws stay spent), the first hit is spawned. The egg path
             * has already spawned the partner (the skeleton, the chicken) and
             * mounted it. */
            String kind = str(c, "kind");
            double x = dbl(c, "x"), y = dbl(c, "y"), z = dbl(c, "z");
            int attempts = c.has("attempts") ? num(c, "attempts") : 1000;
            if (!w.blockExists((int)x, (int)y, (int)z)) throw new IllegalArgumentException("jockey outside loaded world");
            EntityLiving hit = null;
            for (int attempt = 0; attempt < attempts && hit == null; ++attempt)
            {
                EntityLiving e = "spider_skeleton".equals(kind) ? new EntitySpider(w) : new EntityZombie(w);
                e.setLocationAndAngles(x, y, z, 0.0F, 0.0F);
                e.onSpawnWithEgg((IEntityLivingData)null);
                if (e.riddenByEntity != null || e.ridingEntity != null) hit = e;
            }
            if (hit == null) throw new IllegalStateException("no " + kind + " jockey in " + attempts + " attempts");
            w.spawnEntityInWorld(hit);
        }
        else if ("hanging".equals(op))
        {
            /* EntityPainting(World,x,y,z,dir,art) / EntityItemFrame(World,x,y,z,dir),
             * the placement the item's onItemUse makes, then spawnEntityInWorld. */
            String kind = str(c, "kind");
            int x = num(c, "x"), y = num(c, "y"), z = num(c, "z"), dir = num(c, "dir");
            if (!w.blockExists(x, y, z)) throw new IllegalArgumentException("hanging outside loaded world");
            Entity e;
            if ("painting".equals(kind))
            {
                e = new EntityPainting(w, x, y, z, dir, str(c, "art"));
            }
            else
            {
                EntityItemFrame f = new EntityItemFrame(w, x, y, z, dir);
                if (c.has("item"))
                {
                    Item item = Item.getItemById(num(c, "item"));
                    if (item == null) throw new IllegalArgumentException("unknown item " + num(c, "item"));
                    f.setDisplayedItem(new ItemStack(item, 1, opt(c, "meta", 0)));
                }
                if (c.has("rot")) f.setItemRotation(num(c, "rot"));
                e = f;
            }
            w.spawnEntityInWorld(e);
        }
        else if ("orb".equals(op))
        {
            double x = dbl(c, "x"), y = dbl(c, "y"), z = dbl(c, "z");
            if (!w.blockExists((int)x, (int)y, (int)z)) throw new IllegalArgumentException("orb outside loaded world");
            w.spawnEntityInWorld(new net.minecraft.entity.item.EntityXPOrb(w, x, y, z, num(c, "value")));
        }
        else if ("lightning".equals(op))
        {
            // new EntityLightningBolt(world, x, y, z), then World.addWeatherEffect
            double x = dbl(c, "x"), y = dbl(c, "y"), z = dbl(c, "z");
            if (!w.blockExists((int)x, (int)y, (int)z)) throw new IllegalArgumentException("lightning outside loaded world");
            w.addWeatherEffect(new net.minecraft.entity.effect.EntityLightningBolt(w, x, y, z));
        }
        else if ("hurt".equals(op))
        {
            // EntityDragon.attackEntityFromPart, the entry every hit on the
            // dragon takes, with the player's damage source
            net.minecraft.entity.boss.EntityDragon d = null;
            for (Object o : w.loadedEntityList)
                if (o instanceof net.minecraft.entity.boss.EntityDragon) { d = (net.minecraft.entity.boss.EntityDragon)o; break; }
            if (d == null) throw new IllegalArgumentException("hurt: no dragon in the player's world");
            d.attackEntityFromPart(d.dragonPartArray[opt(c, "part", 0)], net.minecraft.util.DamageSource.causePlayerDamage(p), flt(c, "amount"));
        }
        else if ("purge".equals(op))
        {
            for (WorldServer ws : server.worldServers) if (ws != null) MobFree.purge(ws);
        }
    }
}
