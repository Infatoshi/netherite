package netherite.oracle;

import net.minecraft.util.MathHelper;

import com.google.gson.JsonObject;
import com.google.gson.JsonArray;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;

public final class SceneSetup
{
    public static final boolean MUTATES = true;
    private SceneSetup() {}

    public static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if (!Oracle.dev || Oracle.mode != Oracle.AGENT) throw new IllegalStateException("SceneSetup requires --dev agent mode");
        if (cmd.has("poses"))
        {
            Minecraft mc = Minecraft.getMinecraft();
            com.google.gson.JsonArray poses = cmd.getAsJsonArray("poses");
            int changed = 0;
            for (int i = 0; i < poses.size(); ++i)
            {
                JsonObject pose = poses.get(i).getAsJsonObject();
                String cls = pose.get("class").getAsString();
                int index = pose.has("index") ? pose.get("index").getAsInt() : 0;
                for (Object obj : mc.theWorld.getLoadedEntityList())
                {
                    if (!(obj instanceof net.minecraft.entity.EntityLivingBase)
                        || !obj.getClass().getSimpleName().equals(cls)) continue;
                    if (index-- != 0) continue;
                    net.minecraft.entity.EntityLivingBase mob = (net.minecraft.entity.EntityLivingBase)obj;
                    if (pose.has("hurt")) mob.hurtTime = pose.get("hurt").getAsInt();
                    if (pose.has("death")) mob.deathTime = pose.get("death").getAsInt();
                    if (mob instanceof net.minecraft.entity.boss.EntityDragon)
                    {
                        net.minecraft.entity.boss.EntityDragon dg =
                            (net.minecraft.entity.boss.EntityDragon)mob;
                        if (pose.has("deathTicks"))
                            dg.deathTicks = pose.get("deathTicks").getAsInt();
                        if (pose.has("anim"))
                        {
                            float a = pose.get("anim").getAsFloat();
                            dg.prevAnimTime = dg.animTime = a;
                        }
                        if (pose.has("healing") && pose.get("healing").getAsBoolean())
                        {
                            for (Object c : mc.theWorld.getLoadedEntityList())
                                if (c instanceof net.minecraft.entity.item.EntityEnderCrystal)
                                {
                                    dg.healingEnderCrystal =
                                        (net.minecraft.entity.item.EntityEnderCrystal)c;
                                    break;
                                }
                        }
                    }
                    if (mob instanceof net.minecraft.entity.monster.EntityIronGolem && pose.has("rose"))
                        Snapshot.field(mob, "holdRoseTick").setInt(mob, pose.get("rose").getAsInt());
                    if (pose.has("limb")) mob.limbSwing = pose.get("limb").getAsFloat();
                    if (pose.has("limba"))
                        mob.prevLimbSwingAmount = mob.limbSwingAmount = pose.get("limba").getAsFloat();
                    if (pose.has("yaw"))
                    {
                        float ay = pose.get("yaw").getAsFloat();
                        mob.prevRenderYawOffset = mob.renderYawOffset = ay;
                        mob.prevRotationYawHead = mob.rotationYawHead = ay;
                    }
                    if (mob instanceof net.minecraft.entity.monster.EntityGhast && pose.has("attack"))
                    {
                        net.minecraft.entity.monster.EntityGhast g = (net.minecraft.entity.monster.EntityGhast)mob;
                        g.prevAttackCounter = g.attackCounter = pose.get("attack").getAsInt();
                    }
                    if (mob instanceof net.minecraft.entity.monster.EntityGhast && pose.has("shooting"))
                        mob.getDataWatcher().updateObject(16, Byte.valueOf((byte)(pose.get("shooting").getAsBoolean() ? 1 : 0)));
                    if (mob instanceof net.minecraft.entity.monster.EntityMagmaCube && pose.has("squish"))
                    {
                        net.minecraft.entity.monster.EntityMagmaCube c = (net.minecraft.entity.monster.EntityMagmaCube)mob;
                        c.prevSquishFactor = c.squishFactor = pose.get("squish").getAsFloat();
                    }
                    if (mob instanceof net.minecraft.entity.passive.EntitySquid)
                    {
                        net.minecraft.entity.passive.EntitySquid s = (net.minecraft.entity.passive.EntitySquid)mob;
                        if (pose.has("tentacle")) s.lastTentacleAngle = s.tentacleAngle = pose.get("tentacle").getAsFloat();
                        if (pose.has("squidPitch")) s.prevSquidPitch = s.squidPitch = pose.get("squidPitch").getAsFloat();
                    }
                    if (mob instanceof net.minecraft.entity.passive.EntityBat && pose.has("hanging"))
                        ((net.minecraft.entity.passive.EntityBat)mob).setIsBatHanging(pose.get("hanging").getAsBoolean());
                    if (mob instanceof net.minecraft.entity.monster.EntityIronGolem && pose.has("attackTimer"))
                    {
                        java.lang.reflect.Field field = net.minecraft.entity.monster.EntityIronGolem.class.getDeclaredField("attackTimer");
                        field.setAccessible(true);
                        field.setInt(mob, pose.get("attackTimer").getAsInt());
                    }
                    ++changed;
                    break;
                }
            }
            JsonObject result = new JsonObject();
            result.addProperty("posed", changed);
            return result;
        }
        EntityPlayerMP player = (EntityPlayerMP) server.getConfigurationManager().playerEntityList.get(0);
        int targetDim = cmd.has("dim") ? cmd.get("dim").getAsInt() : player.dimension;
        if (player.dimension != targetDim)
        {
            server.getConfigurationManager().transferPlayerToDimension(player, targetDim);
        }

        double x = cmd.get("x").getAsDouble();
        double y = cmd.get("y").getAsDouble();
        double z = cmd.get("z").getAsDouble();
        float yaw = cmd.has("yaw") ? cmd.get("yaw").getAsFloat() : 0.0f;
        float pitch = cmd.has("pitch") ? cmd.get("pitch").getAsFloat() : 0.0f;

        player.playerNetServerHandler.setPlayerLocation(x, y, z, yaw, pitch);
        player.setPositionAndUpdate(x, y, z);
        player.rotationYaw = yaw;
        player.rotationPitch = pitch;
        player.prevRotationYaw = yaw;
        player.prevRotationPitch = pitch;

        // "flying": false lets a scene fall (a death by falling)
        player.capabilities.isFlying = !cmd.has("flying") || cmd.get("flying").getAsBoolean();
        player.capabilities.allowFlying = player.capabilities.isFlying;
        // EntityPlayer.canEat refuses while disableDamage is set: a scene that
        // eats passes "invulnerable": false
        player.capabilities.disableDamage = !cmd.has("invulnerable") || cmd.get("invulnerable").getAsBoolean();
        player.sendPlayerAbilities();

        if (cmd.has("hotbar"))
        {
            JsonArray a = cmd.getAsJsonArray("hotbar");
            for (int i = 0; i < 9; ++i)
                player.inventory.mainInventory[i] = i < a.size() && !a.get(i).isJsonNull()
                    ? stack(a.get(i).getAsJsonObject()) : null;
        }
        if (cmd.has("armor"))
        {
            JsonArray a = cmd.getAsJsonArray("armor");
            for (int i = 0; i < 4; ++i)
                player.inventory.armorInventory[i] = i < a.size() && !a.get(i).isJsonNull()
                    ? stack(a.get(i).getAsJsonObject()) : null;
        }
        if (cmd.has("selected")) player.inventory.currentItem = cmd.get("selected").getAsInt();
        if (cmd.has("health")) player.setHealth(cmd.get("health").getAsFloat());
        if (cmd.has("food")) player.getFoodStats().setFoodLevel(cmd.get("food").getAsInt());
        if (cmd.has("saturation")) player.getFoodStats().setFoodSaturationLevel(cmd.get("saturation").getAsFloat());
        if (cmd.has("air")) player.setAir(cmd.get("air").getAsInt());
        if (cmd.has("level")) player.experienceLevel = cmd.get("level").getAsInt();
        if (cmd.has("xp")) player.experience = cmd.get("xp").getAsFloat();
        if (cmd.has("level") || cmd.has("xp")) player.experienceTotal = 100;
        if (cmd.has("clearEffects") && cmd.get("clearEffects").getAsBoolean()) player.clearActivePotions();
        // "effects": [{id, dur, amp, ambient}] through addPotionEffect, so the
        // client gets them as S1D packets
        if (cmd.has("effects"))
            for (com.google.gson.JsonElement ee : cmd.getAsJsonArray("effects"))
            {
                JsonObject e = ee.getAsJsonObject();
                player.addPotionEffect(new net.minecraft.potion.PotionEffect(e.get("id").getAsInt(),
                    e.get("dur").getAsInt(), e.has("amp") ? e.get("amp").getAsInt() : 0,
                    e.has("ambient") && e.get("ambient").getAsBoolean()));
            }
        player.inventoryContainer.detectAndSendChanges();

        WorldServer ws = server.worldServerForDimension(targetDim);
        if (ws != null)
        {
            if (cmd.has("clearOthers") && cmd.get("clearOthers").getAsBoolean())
            {
                ws.getGameRules().setOrCreateGameRule("doMobSpawning", "false");
                for (Object other : new java.util.ArrayList(ws.loadedEntityList))
                    if (!(other instanceof net.minecraft.entity.player.EntityPlayer))
                        ws.removePlayerEntityDangerously((net.minecraft.entity.Entity)other);
            }
            // "spawn": true makes the scene's block the world spawn, so a
            // respawn lands back on the scene's platform
            if (cmd.has("spawn") && cmd.get("spawn").getAsBoolean())
                ws.setSpawnLocation(MathHelper.floor_double(x), MathHelper.floor_double(y), MathHelper.floor_double(z));
            ws.getWorldInfo().setRaining(false);
            ws.getWorldInfo().setThundering(false);
            if (cmd.has("rain"))
            {
                float rain = cmd.get("rain").getAsFloat();
                ws.getWorldInfo().setRaining(rain > 0.0F);
                ws.setRainStrength(rain);
            }
            if (cmd.has("time"))
            {
                ws.setWorldTime(cmd.get("time").getAsLong());
            }

            if (cmd.has("blocks"))
            {
                com.google.gson.JsonArray blocks = cmd.getAsJsonArray("blocks");
                for (int i = 0; i < blocks.size(); ++i)
                {
                    JsonObject b = blocks.get(i).getAsJsonObject();
                    int bx = b.get("x").getAsInt();
                    int by = b.get("y").getAsInt();
                    int bz = b.get("z").getAsInt();
                    int bid = b.get("id").getAsInt();
                    int bmeta = b.has("meta") ? b.get("meta").getAsInt() : 0;
                    ws.setBlock(bx, by, bz, net.minecraft.block.Block.getBlockById(bid), bmeta, 3);
                }
            }

            if (cmd.has("platform"))
            {
                JsonObject p = cmd.getAsJsonObject("platform");
                int cx = p.get("x").getAsInt(), cy = p.get("y").getAsInt();
                int cz = p.get("z").getAsInt(), radius = p.get("radius").getAsInt();
                net.minecraft.block.Block block = net.minecraft.block.Block.getBlockById(p.get("id").getAsInt());
                for (int dx = -radius; dx <= radius; ++dx)
                    for (int dz = -radius; dz <= radius; ++dz)
                        ws.setBlock(cx + dx, cy, cz + dz, block, 0, 3);
            }

            // "exitPortal": {"x", "z"}: the End's exit fountain, built by
            // EntityDragon.createEnderPortal itself on a dragon never spawned
            if (cmd.has("exitPortal"))
            {
                JsonObject ep = cmd.getAsJsonObject("exitPortal");
                net.minecraft.entity.boss.EntityDragon dg = new net.minecraft.entity.boss.EntityDragon(ws);
                java.lang.reflect.Method m = net.minecraft.entity.boss.EntityDragon.class
                    .getDeclaredMethod("createEnderPortal", int.class, int.class);
                m.setAccessible(true);
                m.invoke(dg, ep.get("x").getAsInt(), ep.get("z").getAsInt());
            }

            if (cmd.has("entities"))
            {
                com.google.gson.JsonArray entities = cmd.getAsJsonArray("entities");
                for (int i = 0; i < entities.size(); ++i)
                {
                    JsonObject e = entities.get(i).getAsJsonObject();
                    String type = e.get("type").getAsString();
                    if ("painting".equals(type))
                    {
                        int px = e.get("x").getAsInt();
                        int py = e.get("y").getAsInt();
                        int pz = e.get("z").getAsInt();
                        int dir = e.get("dir").getAsInt();
                        String art = e.has("art") ? e.get("art").getAsString() : "Kebab";
                        net.minecraft.entity.item.EntityPainting p = new net.minecraft.entity.item.EntityPainting(ws, px, py, pz, dir, art);
                        ws.spawnEntityInWorld(p);
                    }
                    else if ("item_frame".equals(type))
                    {
                        int px = e.get("x").getAsInt();
                        int py = e.get("y").getAsInt();
                        int pz = e.get("z").getAsInt();
                        int dir = e.get("dir").getAsInt();
                        net.minecraft.entity.item.EntityItemFrame f = new net.minecraft.entity.item.EntityItemFrame(ws, px, py, pz, dir);
                        if (e.has("item")) f.setDisplayedItem(stack(e.getAsJsonObject("item")));
                        if (e.has("rot")) f.setItemRotation(e.get("rot").getAsInt());
                        ws.spawnEntityInWorld(f);
                    }
                    else if ("squid".equals(type))
                    {
                        double sx = e.get("x").getAsDouble();
                        double sy = e.get("y").getAsDouble();
                        double sz = e.get("z").getAsDouble();
                        float syaw = e.has("yaw") ? e.get("yaw").getAsFloat() : 0.0f;
                        float spit = e.has("pitch") ? e.get("pitch").getAsFloat() : 0.0f;
                        net.minecraft.entity.passive.EntitySquid sq = new net.minecraft.entity.passive.EntitySquid(ws);
                        sq.setLocationAndAngles(sx, sy, sz, syaw, spit);
                        ws.spawnEntityInWorld(sq);
                    }
                    else if ("item".equals(type))
                    {
                        net.minecraft.entity.item.EntityItem drop = new net.minecraft.entity.item.EntityItem(
                            ws, e.get("x").getAsDouble(), e.get("y").getAsDouble(),
                            e.get("z").getAsDouble(), stack(e));
                        drop.motionX = drop.motionY = drop.motionZ = 0.0D;
                        drop.hoverStart = e.has("hover") ? e.get("hover").getAsFloat() : 0.0F;
                        /* "pickup": the ticks before a player can collect it
                         * (lane/entrender's pickup scene), never otherwise */
                        drop.delayBeforeCanPickup = e.has("pickup") ? e.get("pickup").getAsInt() : 32767;
                        ws.spawnEntityInWorld(drop);
                    }
                    else if ("dragon".equals(type))
                    {
                        net.minecraft.entity.boss.EntityDragon dg =
                            new net.minecraft.entity.boss.EntityDragon(ws);
                        double dx = e.get("x").getAsDouble();
                        double dy = e.get("y").getAsDouble();
                        double dz = e.get("z").getAsDouble();
                        float dyaw = e.has("yaw") ? e.get("yaw").getAsFloat() : 0.0f;
                        dg.setLocationAndAngles(dx, dy, dz, dyaw, 0.0f);
                        dg.prevRotationYaw = dg.rotationYaw = dyaw;
                        dg.prevRenderYawOffset = dg.renderYawOffset = dyaw;
                        dg.prevRotationYawHead = dg.rotationYawHead = dyaw;
                        if (e.has("motionY")) dg.motionY = e.get("motionY").getAsDouble();
                        if (e.has("health")) dg.setHealth(e.get("health").getAsFloat());
                        if (e.has("deathTicks")) dg.deathTicks = e.get("deathTicks").getAsInt();
                        if (e.has("anim"))
                        {
                            dg.prevAnimTime = dg.animTime = e.get("anim").getAsFloat();
                        }
                        ws.spawnEntityInWorld(dg);
                    }
                    else if ("ender_crystal".equals(type))
                    {
                        net.minecraft.entity.item.EntityEnderCrystal c =
                            new net.minecraft.entity.item.EntityEnderCrystal(ws,
                                e.get("x").getAsDouble(), e.get("y").getAsDouble(),
                                e.get("z").getAsDouble());
                        if (e.has("innerRotation"))
                            c.innerRotation = e.get("innerRotation").getAsInt();
                        ws.spawnEntityInWorld(c);
                    }
                    else if ("pig".equals(type) || "cow".equals(type) || "sheep".equals(type)
                        || "chicken".equals(type) || "zombie".equals(type) || "pigman".equals(type)
                        || "ghast".equals(type) || "blaze".equals(type) || "magma_cube".equals(type)
                        || "villager".equals(type) || "iron_golem".equals(type) || "bat".equals(type)
                        || "mooshroom".equals(type)
                        || "skeleton".equals(type) || "wither_skeleton".equals(type)
                        || "creeper".equals(type) || "spider".equals(type)
                        || "cave_spider".equals(type) || "enderman".equals(type)
                        || "witch".equals(type) || "slime".equals(type)
                        || "silverfish".equals(type))
                    {
                        net.minecraft.entity.EntityLivingBase mob;
                        if ("pig".equals(type)) mob = new net.minecraft.entity.passive.EntityPig(ws);
                        else if ("cow".equals(type)) mob = new net.minecraft.entity.passive.EntityCow(ws);
                        else if ("sheep".equals(type)) mob = new net.minecraft.entity.passive.EntitySheep(ws);
                        else if ("chicken".equals(type)) mob = new net.minecraft.entity.passive.EntityChicken(ws);
                        else if ("zombie".equals(type)) mob = new net.minecraft.entity.monster.EntityZombie(ws);
                        else if ("pigman".equals(type)) mob = new net.minecraft.entity.monster.EntityPigZombie(ws);
                        else if ("ghast".equals(type)) mob = new net.minecraft.entity.monster.EntityGhast(ws);
                        else if ("blaze".equals(type)) mob = new net.minecraft.entity.monster.EntityBlaze(ws);
                        else if ("magma_cube".equals(type)) mob = new net.minecraft.entity.monster.EntityMagmaCube(ws);
                        else if ("villager".equals(type)) mob = new net.minecraft.entity.passive.EntityVillager(ws);
                        else if ("iron_golem".equals(type)) mob = new net.minecraft.entity.monster.EntityIronGolem(ws);
                        else if ("bat".equals(type)) mob = new net.minecraft.entity.passive.EntityBat(ws);
                        else if ("mooshroom".equals(type)) mob = new net.minecraft.entity.passive.EntityMooshroom(ws);
                        else if ("skeleton".equals(type) || "wither_skeleton".equals(type))
                        {
                            net.minecraft.entity.monster.EntitySkeleton s = new net.minecraft.entity.monster.EntitySkeleton(ws);
                            if ("wither_skeleton".equals(type)) s.setSkeletonType(1);
                            mob = s;
                        }
                        else if ("creeper".equals(type)) mob = new net.minecraft.entity.monster.EntityCreeper(ws);
                        else if ("spider".equals(type)) mob = new net.minecraft.entity.monster.EntitySpider(ws);
                        else if ("cave_spider".equals(type)) mob = new net.minecraft.entity.monster.EntityCaveSpider(ws);
                        else if ("enderman".equals(type)) mob = new net.minecraft.entity.monster.EntityEnderman(ws);
                        else if ("witch".equals(type)) mob = new net.minecraft.entity.monster.EntityWitch(ws);
                        else if ("slime".equals(type)) mob = new net.minecraft.entity.monster.EntitySlime(ws);
                        else mob = new net.minecraft.entity.monster.EntitySilverfish(ws);
                        float myaw = e.has("yaw") ? e.get("yaw").getAsFloat() : 0.0f;
                        mob.setLocationAndAngles(e.get("x").getAsDouble(), e.get("y").getAsDouble(),
                            e.get("z").getAsDouble(), myaw, 0.0f);
                        mob.prevRotationYaw = mob.rotationYaw = myaw;
                        if (e.has("pitch")) mob.prevRotationPitch = mob.rotationPitch = e.get("pitch").getAsFloat();
                        mob.prevRotationYawHead = mob.rotationYawHead = myaw;
                        mob.prevRenderYawOffset = mob.renderYawOffset = myaw;
                        if (e.has("limb")) mob.limbSwing = e.get("limb").getAsFloat();
                        if (e.has("limba"))
                            mob.prevLimbSwingAmount = mob.limbSwingAmount = e.get("limba").getAsFloat();
                        if (e.has("vx")) mob.motionX = e.get("vx").getAsDouble();
                        if (e.has("vz")) mob.motionZ = e.get("vz").getAsDouble();
                        if (mob instanceof net.minecraft.entity.passive.EntityPig && e.has("saddle"))
                            ((net.minecraft.entity.passive.EntityPig)mob).setSaddled(e.get("saddle").getAsBoolean());
                        if (e.has("baby") && e.get("baby").getAsBoolean())
                        {
                            if (mob instanceof net.minecraft.entity.EntityAgeable)
                                ((net.minecraft.entity.EntityAgeable)mob).setGrowingAge(-24000);
                            else if (mob instanceof net.minecraft.entity.monster.EntityZombie)
                                ((net.minecraft.entity.monster.EntityZombie)mob).setChild(true);
                        }
                        if (mob instanceof net.minecraft.entity.passive.EntitySheep)
                        {
                            net.minecraft.entity.passive.EntitySheep sh = (net.minecraft.entity.passive.EntitySheep)mob;
                            if (e.has("color")) sh.setFleeceColor(e.get("color").getAsInt());
                            if (e.has("sheared")) sh.setSheared(e.get("sheared").getAsBoolean());
                        }
                        if (mob instanceof net.minecraft.entity.passive.EntityVillager && e.has("profession"))
                            ((net.minecraft.entity.passive.EntityVillager)mob).setProfession(e.get("profession").getAsInt());
                        if (mob instanceof net.minecraft.entity.passive.EntityBat && e.has("hanging"))
                            ((net.minecraft.entity.passive.EntityBat)mob).setIsBatHanging(e.get("hanging").getAsBoolean());
                        if (mob instanceof net.minecraft.entity.monster.EntityGhast && e.has("attack"))
                        {
                            net.minecraft.entity.monster.EntityGhast g = (net.minecraft.entity.monster.EntityGhast)mob;
                            g.prevAttackCounter = g.attackCounter = e.get("attack").getAsInt();
                        }
                        if (e.has("hurt")) mob.hurtTime = e.get("hurt").getAsInt();
                        if (e.has("death")) mob.deathTime = e.get("death").getAsInt();
                        if (mob instanceof net.minecraft.entity.monster.EntityCreeper)
                        {
                            net.minecraft.entity.monster.EntityCreeper c = (net.minecraft.entity.monster.EntityCreeper)mob;
                            if (e.has("charged") && e.get("charged").getAsBoolean())
                                c.getDataWatcher().updateObject(17, Byte.valueOf((byte)1));
                            if (e.has("swell") && e.get("swell").getAsBoolean()) c.setCreeperState(1);
                        }
                        if (mob instanceof net.minecraft.entity.monster.EntityEnderman)
                        {
                            net.minecraft.entity.monster.EntityEnderman n = (net.minecraft.entity.monster.EntityEnderman)mob;
                            if (e.has("carried")) n.func_146081_a(net.minecraft.block.Block.getBlockById(e.get("carried").getAsInt()));
                            if (e.has("carriedMeta")) n.setCarryingData(e.get("carriedMeta").getAsInt());
                            if (e.has("screaming")) n.setScreaming(e.get("screaming").getAsBoolean());
                        }
                        if (mob instanceof net.minecraft.entity.monster.EntitySlime && e.has("size"))
                        {
                            net.minecraft.nbt.NBTTagCompound nbt = new net.minecraft.nbt.NBTTagCompound();
                            mob.writeEntityToNBT(nbt);
                            nbt.setInteger("Size", e.get("size").getAsInt() - 1);
                            mob.readEntityFromNBT(nbt);
                        }
                        /* lane/entrender: equipment (0 the hand, 1..4 boots to
                         * helmet; "ench" [[id, level]...], leather "color"), a
                         * burning mob, a zombie villager (the golem's rose is a
                         * client pose: its S19 would reach no tracker yet) */
                        if (e.has("equip"))
                            for (com.google.gson.JsonElement el : e.getAsJsonArray("equip"))
                            {
                                JsonObject q = el.getAsJsonObject();
                                ItemStack st = stack(q);
                                if (q.has("ench"))
                                    for (com.google.gson.JsonElement en : q.getAsJsonArray("ench"))
                                        st.addEnchantment(net.minecraft.enchantment.Enchantment.enchantmentsList[
                                            en.getAsJsonArray().get(0).getAsInt()], en.getAsJsonArray().get(1).getAsInt());
                                if (q.has("color"))
                                    ((net.minecraft.item.ItemArmor)st.getItem()).func_82813_b(st, q.get("color").getAsInt());
                                mob.setCurrentItemOrArmor(q.get("slot").getAsInt(), st);
                            }
                        if (e.has("fire")) mob.setFire(e.get("fire").getAsInt());
                        if (mob instanceof net.minecraft.entity.monster.EntityZombie && e.has("villager"))
                            ((net.minecraft.entity.monster.EntityZombie)mob).setVillager(e.get("villager").getAsBoolean());
                        ws.spawnEntityInWorld(mob);
                    }
                }
            }
        }

        Minecraft mc = Minecraft.getMinecraft();
        if (mc.thePlayer != null)
        {
            mc.thePlayer.setLocationAndAngles(x, y, z, yaw, pitch);
            mc.thePlayer.rotationYaw = yaw;
            mc.thePlayer.rotationPitch = pitch;
            mc.thePlayer.prevRotationYaw = yaw;
            mc.thePlayer.prevRotationPitch = pitch;
            mc.thePlayer.capabilities.isFlying = true;
            if (cmd.has("air")) mc.thePlayer.setAir(cmd.get("air").getAsInt());
            if (cmd.has("burning") && cmd.get("burning").getAsBoolean())
            {
                // On the client Entity.isBurning() reads the synced on-fire
                // flag, so the fire has to start on the server player; and
                // EntityPlayer.onUpdate extinguishes a burning player who
                // cannot be damaged, so damage has to be allowed for the scene.
                player.capabilities.disableDamage = false;
                player.sendPlayerAbilities();
                player.setFire(30);
                mc.thePlayer.setFire(30);
            }
            if (cmd.has("hurt"))
            {
                mc.thePlayer.hurtTime = cmd.get("hurt").getAsInt();
                mc.thePlayer.maxHurtTime = 10;
                mc.thePlayer.attackedAtYaw = cmd.has("hurtYaw") ? cmd.get("hurtYaw").getAsFloat() : 0.0F;
            }
            if (cmd.has("portal"))
            {
                mc.thePlayer.timeInPortal = cmd.get("portal").getAsFloat();
                mc.thePlayer.prevTimeInPortal = mc.thePlayer.timeInPortal;
            }
        }
        if (mc.theWorld != null && cmd.has("entities"))
        {
            JsonArray entities = cmd.getAsJsonArray("entities");
            for (int i = 0; i < entities.size(); ++i)
            {
                JsonObject e = entities.get(i).getAsJsonObject();
                if (!"orb".equals(e.get("type").getAsString())) continue;
                net.minecraft.entity.item.EntityXPOrb orb = new net.minecraft.entity.item.EntityXPOrb(
                    mc.theWorld, e.get("x").getAsDouble(), e.get("y").getAsDouble(),
                    e.get("z").getAsDouble(), e.get("value").getAsInt());
                orb.motionX = orb.motionY = orb.motionZ = 0.0D;
                if (e.has("color")) orb.xpColor = e.get("color").getAsInt();
                orb.setEntityId(-10000 - i);
                mc.theWorld.spawnEntityInWorld(orb);
            }
        }
        if (cmd.has("rain") && mc.theWorld != null)
            mc.theWorld.setRainStrength(cmd.get("rain").getAsFloat());

        JsonObject r = new JsonObject();
        r.addProperty("dim", targetDim);
        r.addProperty("x", x);
        r.addProperty("y", y);
        r.addProperty("z", z);
        r.addProperty("yaw", yaw);
        r.addProperty("pitch", pitch);
        return r;
    }

    private static ItemStack stack(JsonObject o)
    {
        Item item = Item.getItemById(o.get("id").getAsInt());
        if (item == null) throw new IllegalArgumentException("SceneSetup: unknown item id " + o.get("id"));
        return new ItemStack(item, o.has("count") ? o.get("count").getAsInt() : 1,
            o.has("meta") ? o.get("meta").getAsInt() : 0);
    }
}
