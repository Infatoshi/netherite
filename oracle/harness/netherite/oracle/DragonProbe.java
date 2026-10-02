package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.boss.EntityDragonPart;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.util.DamageSource;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/** Raw End entity trace. Every floating field is written as its raw bits. */
public final class DragonProbe
{
    private DragonProbe() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try { result[0] = dump(server, cmd); }
                catch (Exception e) { error[0] = e; }
            }
        }, "Oracle DragonProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    private static String d(double v) { return Long.toHexString(Double.doubleToRawLongBits(v)); }
    private static String f(float v) { return Integer.toHexString(Float.floatToRawIntBits(v)); }

    private static void entity(PrintWriter out, int tick, EntityDragon dragon, EntityEnderCrystal crystal,
                               EntityEnderCrystal crystal2, EntityPlayer player)
    {
        StringBuilder b = new StringBuilder();
        b.append(tick).append(' ').append(d(dragon.posX)).append(' ').append(d(dragon.posY)).append(' ').append(d(dragon.posZ));
        b.append(' ').append(d(dragon.motionX)).append(' ').append(d(dragon.motionY)).append(' ').append(d(dragon.motionZ));
        b.append(' ').append(f(dragon.rotationYaw)).append(' ').append(f(dragon.prevRotationYaw));
        b.append(' ').append(f(dragon.animTime)).append(' ').append(f(dragon.prevAnimTime));
        b.append(' ').append(f(GhastProbe.floatField(dragon, "randomYawVelocity"))).append(' ').append(f(dragon.renderYawOffset));
        b.append(' ').append(d(dragon.targetX)).append(' ').append(d(dragon.targetY)).append(' ').append(d(dragon.targetZ));
        b.append(' ').append(dragon.forceNewTarget ? 1 : 0).append(' ').append(dragon.slowed ? 1 : 0);
        b.append(' ').append(dragon.ringBufferIndex).append(' ').append(dragon.ticksExisted);
        b.append(' ').append(f(dragon.getHealth())).append(' ').append(dragon.hurtTime).append(' ').append(dragon.deathTime).append(' ').append(dragon.deathTicks);
        b.append(' ').append(Det.state(dragon.getRNG()));
        b.append(' ').append(GhastProbe.field(dragon, "target") == null ? 0 : 1);
        b.append(' ').append(dragon.healingEnderCrystal == crystal && crystal != null ? 1 : 0);
        for (int i = 0; i < dragon.ringBuffer.length; ++i)
        {
            b.append(' ').append(d(dragon.ringBuffer[i][0])).append(' ').append(d(dragon.ringBuffer[i][1]));
        }
        for (EntityDragonPart p : dragon.dragonPartArray)
        {
            b.append(' ').append(d(p.posX)).append(' ').append(d(p.posY)).append(' ').append(d(p.posZ));
            b.append(' ').append(f(p.width)).append(' ').append(f(p.height));
        }
        if (crystal != null)
        {
            b.append(' ').append(d(crystal.posX)).append(' ').append(d(crystal.posY)).append(' ').append(d(crystal.posZ));
            b.append(' ').append(crystal.innerRotation).append(' ').append(crystal.health).append(' ').append(crystal.isDead ? 1 : 0);
            b.append(' ').append(Det.state((java.util.Random)GhastProbe.field(crystal, "rand")));
        }
        b.append(' ').append(d(dragon.boundingBox.minX)).append(' ').append(d(dragon.boundingBox.maxX));
        b.append(' ').append(d(dragon.boundingBox.minY));
        b.append(' ').append(d(dragon.boundingBox.minZ)).append(' ').append(d(dragon.boundingBox.maxZ));
        if (player != null)
        {
            b.append(' ').append(d(player.posX)).append(' ').append(d(player.posY)).append(' ').append(d(player.posZ));
            b.append(' ').append(d(player.motionX)).append(' ').append(d(player.motionY)).append(' ').append(d(player.motionZ));
            b.append(' ').append(f(player.getHealth())).append(' ').append(player.hurtTime).append(' ').append(player.hurtResistantTime);
            b.append(' ').append(Det.state((java.util.Random)GhastProbe.field(player, "rand")));
            b.append(' ').append(d(player.boundingBox.minY));
        }
        b.append(' ').append(dragon.hurtResistantTime);
        b.append(' ').append(f(GhastProbe.floatField(dragon, "lastDamage")));
        b.append(' ').append(f(GhastProbe.floatField(dragon, "prevHealth")));
        b.append(' ').append(GhastProbe.intField(dragon, "dead"));
        b.append(' ').append(dragon.isDead ? 1 : 0);
        b.append(' ').append(dragon.healingEnderCrystal == null ? -1 :
            (dragon.healingEnderCrystal == crystal ? 0 : (dragon.healingEnderCrystal == crystal2 ? 1 : -2)));
        out.println(b.toString());
    }

    private static void extraCrystal(PrintWriter out, int tick, EntityEnderCrystal crystal)
    {
        if (crystal == null) return;
        out.println(tick + " " + d(crystal.posX) + " " + d(crystal.posY) + " " + d(crystal.posZ)
            + " " + crystal.innerRotation + " " + crystal.health + " " + (crystal.isDead ? 1 : 0)
            + " " + Det.state((java.util.Random)GhastProbe.field(crystal, "rand")));
    }

    private static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        WorldServer ws = server.worldServers[2];
        if (ws == null) throw new IllegalStateException("no End world");
        File dir = new File(cmd.get("out").getAsString());
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IllegalStateException("cannot create " + dir);
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 400;
        boolean withCrystal = cmd.has("crystal") && cmd.get("crystal").getAsBoolean();
        boolean withPlayer = cmd.has("player") && cmd.get("player").getAsBoolean();
        String mode = cmd.has("mode") ? cmd.get("mode").getAsString() : "flight";
        Probe.rawChunks = true;
        for (int cx = -12; cx <= 12; ++cx)
            for (int cz = -12; cz <= 12; ++cz) ws.getChunkFromChunkCoords(cx, cz);

        PrintWriter shapes = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "shapes.txt")), "UTF-8"));
        if ("pillar".equals(mode))
        {
            for (int y = 80; y <= 127; ++y)
            {
                ws.setBlock(0, y, 0, Blocks.obsidian, 0, 2);
                shapes.println("0 " + y + " 0 49 0");
            }
        }
        if ("blocks".equals(mode) || "no_grief".equals(mode))
        {
            for (int y = 125; y <= 130; ++y)
            {
                ws.setBlock(0, y, 0, Blocks.cobblestone, 0, 2);
                shapes.println("0 " + y + " 0 4 0");
            }
            ws.setBlock(1, 130, 0, Blocks.obsidian, 0, 2);
            shapes.println("1 130 0 49 0");
            ws.setBlock(-1, 130, 0, Blocks.end_stone, 0, 2);
            shapes.println("-1 130 0 121 0");
        }
        shapes.close();
        if ("no_grief".equals(mode)) ws.getGameRules().setOrCreateGameRule("mobGriefing", "false");
        GhastProbe.writeDetState(new File(dir, "start.txt"), ws.getSeed(), ws);

        EntityDragon dragon = new EntityDragon(ws);
        dragon.setLocationAndAngles(0.0D, 128.0D, 0.0D, 25.0F, 0.0F);
        if (!ws.spawnEntityInWorld(dragon)) throw new IllegalStateException("dragon spawn failed");
        EntityEnderCrystal crystal = null;
        EntityEnderCrystal crystal2 = null;
        if (withCrystal)
        {
            crystal = new EntityEnderCrystal(ws, 8.0D, 128.0D, 0.0D);
            if (!ws.spawnEntityInWorld(crystal)) throw new IllegalStateException("crystal spawn failed");
        }
        if ("multi_crystal".equals(mode))
        {
            if (crystal == null) throw new IllegalStateException("multi_crystal needs crystal=true");
            crystal2 = new EntityEnderCrystal(ws, -8.0D, 128.0D, 0.0D);
            if (!ws.spawnEntityInWorld(crystal2)) throw new IllegalStateException("second crystal spawn failed");
        }
        EntityPlayer player = null;
        if (withPlayer || "pillar".equals(mode) || "parts".equals(mode))
        {
            player = new GhastProbe.ProbePlayer(ws);
            player.setLocationAndAngles(0.5D, "pillar".equals(mode) ? 128.0D : 100.0D, 0.5D, 0.0F, 0.0F);
            if (!ws.spawnEntityInWorld(player)) throw new IllegalStateException("player spawn failed");
        }
        final PrintWriter writes = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "writes.txt")), "UTF-8"));
        final PrintWriter orbOut = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "orbs.txt")), "UTF-8"));
        final PrintWriter detOut = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "det.txt")), "UTF-8"));
        final PrintWriter extraOut = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "extra.txt")), "UTF-8"));
        List<EntityXPOrb> orbs = new ArrayList<EntityXPOrb>();
        int absorbed = ws.loadedEntityList.size();
        boolean crystalRemoved = false;
        boolean crystal2Removed = false;
        final int[] currentTick = new int[] {-1};
        Rows.WriteListener previousListener = Rows.writeListener;
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                if (w == ws) writes.println(currentTick[0] + " " + x + " " + y + " " + z + " " + id + " " + meta);
            }
        };
        PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "ticks.txt")), "UTF-8"));
        entity(out, -1, dragon, crystal, crystal2, player);
        extraCrystal(extraOut, -1, crystal2);
        GhastProbe.writeDetLine(detOut, -1, ws);
        for (int i = 0; i < ticks; ++i)
        {
            currentTick[0] = i;
            if ("parts".equals(mode) && i >= 20 && i <= 260 && (i - 20) % 40 == 0)
            {
                int part = (i - 20) / 40;
                dragon.attackEntityFromPart(dragon.dragonPartArray[part], DamageSource.causePlayerDamage(player), 8.0F);
            }
            if ("parts".equals(mode) && i == 21)
                dragon.attackEntityFromPart(dragon.dragonPartBody, DamageSource.causePlayerDamage(player), 8.0F);
            if ("parts".equals(mode) && i == 22)
                dragon.attackEntityFromPart(dragon.dragonPartHead, DamageSource.causePlayerDamage(player), 12.0F);
            if ("death".equals(mode) && i >= 20 && i <= 180 && (i - 20) % 40 == 0)
                dragon.attackEntityFromPart(dragon.dragonPartHead, DamageSource.setExplosionSource((net.minecraft.world.Explosion)null), 40.0F);
            if ("healing".equals(mode) && i == 20)
                dragon.attackEntityFromPart(dragon.dragonPartHead, DamageSource.setExplosionSource((net.minecraft.world.Explosion)null), 40.0F);
            if ("crystal_kill".equals(mode) && i == 100 && crystal != null)
                crystal.attackEntityFrom(DamageSource.generic, 1.0F);
            if ("multi_crystal".equals(mode) && i == 150 && crystal2 != null)
                crystal2.attackEntityFrom(DamageSource.generic, 1.0F);
            if ("force_target".equals(mode) && (i == 50 || i == 150)) dragon.forceNewTarget = true;
            if (!dragon.isDead) GhastProbe.updateEntity(ws, dragon);
            if (crystal != null && !crystal.isDead) GhastProbe.updateEntity(ws, crystal);
            if (crystal2 != null && !crystal2.isDead) GhastProbe.updateEntity(ws, crystal2);
            if (crystal != null && crystal.isDead && !crystalRemoved)
            {
                if (crystal.addedToChunk && ws.theChunkProviderServer.chunkExists(crystal.chunkCoordX, crystal.chunkCoordZ))
                    ws.getChunkFromChunkCoords(crystal.chunkCoordX, crystal.chunkCoordZ).removeEntity(crystal);
                crystalRemoved = true;
            }
            if (crystal2 != null && crystal2.isDead && !crystal2Removed)
            {
                if (crystal2.addedToChunk && ws.theChunkProviderServer.chunkExists(crystal2.chunkCoordX, crystal2.chunkCoordZ))
                    ws.getChunkFromChunkCoords(crystal2.chunkCoordX, crystal2.chunkCoordZ).removeEntity(crystal2);
                crystal2Removed = true;
            }
            while (absorbed < ws.loadedEntityList.size())
            {
                Entity n = (Entity)ws.loadedEntityList.get(absorbed++);
                if (n instanceof EntityXPOrb) orbs.add((EntityXPOrb)n);
            }
            for (EntityXPOrb orb : orbs) if (!orb.isDead) GhastProbe.updateEntity(ws, orb);
            for (int oi = 0; oi < orbs.size(); ++oi)
            {
                EntityXPOrb orb = orbs.get(oi);
                orbOut.println(i + " " + oi + " " + orb.getEntityId() + " " + GhastProbe.intField(orb, "xpValue")
                    + " " + d(orb.posX) + " " + d(orb.posY) + " " + d(orb.posZ)
                    + " " + d(orb.motionX) + " " + d(orb.motionY) + " " + d(orb.motionZ)
                    + " " + f(orb.rotationYaw) + " " + orb.xpOrbAge + " " + GhastProbe.intField(orb, "xpOrbHealth")
                    + " " + Det.state((java.util.Random)GhastProbe.field(orb, "rand"))
                    + " " + (orb.isDead ? 1 : 0) + " " + (orb.addedToChunk ? 1 : 0));
            }
            entity(out, i, dragon, crystal, crystal2, player);
            extraCrystal(extraOut, i, crystal2);
            GhastProbe.writeDetLine(detOut, i, ws);
        }
        out.close();
        Rows.writeListener = previousListener;
        writes.close();
        orbOut.close();
        detOut.close();
        extraOut.close();
        JsonObject m = new JsonObject();
        m.addProperty("kind", "dragon");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("ticks", ticks);
        m.addProperty("crystal", withCrystal);
        m.addProperty("player", player != null);
        m.addProperty("mode", mode);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();
        return m;
    }
}
