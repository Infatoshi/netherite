package netherite.oracle;

import com.google.gson.*;
import java.io.*;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;
import java.util.zip.GZIPOutputStream;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.*;
import net.minecraft.server.integrated.IntegratedServer;

public final class ClientWorldProbe
{
    private static Field recvQueueField;
    private static Field lcgField;
    private static Field newPosIncField;
    private static Field newPosXField;
    private static Field newPosYField;
    private static Field newPosZField;
    private static Field newRotYawField;
    private static Field newRotPitchField;
    private static Field s14IdField;
    private static Field s19IdField;

    static
    {
        try
        {
            recvQueueField = net.minecraft.network.NetworkManager.class.getDeclaredField("receivedPacketsQueue");
            recvQueueField.setAccessible(true);
            lcgField = net.minecraft.world.World.class.getDeclaredField("updateLCG");
            lcgField.setAccessible(true);

            newPosIncField = EntityLivingBase.class.getDeclaredField("newPosRotationIncrements");
            newPosIncField.setAccessible(true);
            newPosXField = EntityLivingBase.class.getDeclaredField("newPosX");
            newPosXField.setAccessible(true);
            newPosYField = EntityLivingBase.class.getDeclaredField("newPosY");
            newPosYField.setAccessible(true);
            newPosZField = EntityLivingBase.class.getDeclaredField("newPosZ");
            newPosZField.setAccessible(true);
            newRotYawField = EntityLivingBase.class.getDeclaredField("newRotationYaw");
            newRotYawField.setAccessible(true);
            newRotPitchField = EntityLivingBase.class.getDeclaredField("newRotationPitch");
            newRotPitchField.setAccessible(true);

            s14IdField = S14PacketEntity.class.getDeclaredField("field_149074_a");
            s14IdField.setAccessible(true);
            s19IdField = S19PacketEntityHeadLook.class.getDeclaredField("field_149384_a");
            s19IdField.setAccessible(true);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static JsonObject serializePacket(Packet p)
    {
        JsonObject j = new JsonObject();
        if (p instanceof S0FPacketSpawnMob)
        {
            S0FPacketSpawnMob m = (S0FPacketSpawnMob)p;
            j.addProperty("pkt", "S0F");
            j.addProperty("id", m.func_149024_d());
            j.addProperty("type", m.func_149025_e());
            j.addProperty("x", m.func_149023_f());
            j.addProperty("y", m.func_149034_g());
            j.addProperty("z", m.func_149029_h());
            j.addProperty("yaw", m.func_149028_l());
            j.addProperty("pitch", m.func_149030_m());
            j.addProperty("head_yaw", m.func_149032_n());
            j.addProperty("mx", m.func_149026_i());
            j.addProperty("my", m.func_149033_j());
            j.addProperty("mz", m.func_149031_k());
        }
        else if (p instanceof S0EPacketSpawnObject)
        {
            S0EPacketSpawnObject o = (S0EPacketSpawnObject)p;
            j.addProperty("pkt", "S0E");
            j.addProperty("id", o.func_149001_c());
            j.addProperty("type", o.func_148993_l());
            j.addProperty("data", o.func_149009_m());
            j.addProperty("x", o.func_148997_d());
            j.addProperty("y", o.func_148998_e());
            j.addProperty("z", o.func_148994_f());
            j.addProperty("pitch", o.func_149008_j());
            j.addProperty("yaw", o.func_149006_k());
            j.addProperty("mx", o.func_149010_g());
            j.addProperty("my", o.func_149004_h());
            j.addProperty("mz", o.func_148999_i());
        }
        else if (p instanceof S11PacketSpawnExperienceOrb)
        {
            S11PacketSpawnExperienceOrb xp = (S11PacketSpawnExperienceOrb)p;
            j.addProperty("pkt", "S11");
            j.addProperty("id", xp.func_148985_c());
            j.addProperty("x", xp.func_148984_d());
            j.addProperty("y", xp.func_148983_e());
            j.addProperty("z", xp.func_148982_f());
            j.addProperty("xp", xp.func_148986_g());
        }
        else if (p instanceof S14PacketEntity.S17PacketEntityLookMove)
        {
            S14PacketEntity.S17PacketEntityLookMove lm = (S14PacketEntity.S17PacketEntityLookMove)p;
            j.addProperty("pkt", "S17");
            try { j.addProperty("id", s14IdField.getInt(lm)); } catch (Exception e) {}
            j.addProperty("dx", lm.func_149062_c());
            j.addProperty("dy", lm.func_149061_d());
            j.addProperty("dz", lm.func_149064_e());
            j.addProperty("yaw", lm.func_149066_f());
            j.addProperty("pitch", lm.func_149063_g());
        }
        else if (p instanceof S14PacketEntity.S15PacketEntityRelMove)
        {
            S14PacketEntity.S15PacketEntityRelMove rm = (S14PacketEntity.S15PacketEntityRelMove)p;
            j.addProperty("pkt", "S15");
            try { j.addProperty("id", s14IdField.getInt(rm)); } catch (Exception e) {}
            j.addProperty("dx", rm.func_149062_c());
            j.addProperty("dy", rm.func_149061_d());
            j.addProperty("dz", rm.func_149064_e());
        }
        else if (p instanceof S14PacketEntity.S16PacketEntityLook)
        {
            S14PacketEntity.S16PacketEntityLook l = (S14PacketEntity.S16PacketEntityLook)p;
            j.addProperty("pkt", "S16");
            try { j.addProperty("id", s14IdField.getInt(l)); } catch (Exception e) {}
            j.addProperty("yaw", l.func_149066_f());
            j.addProperty("pitch", l.func_149063_g());
        }
        else if (p instanceof S18PacketEntityTeleport)
        {
            S18PacketEntityTeleport tp = (S18PacketEntityTeleport)p;
            j.addProperty("pkt", "S18");
            j.addProperty("id", tp.func_149451_c());
            j.addProperty("x", tp.func_149449_d());
            j.addProperty("y", tp.func_149448_e());
            j.addProperty("z", tp.func_149446_f());
            j.addProperty("yaw", tp.func_149450_g());
            j.addProperty("pitch", tp.func_149447_h());
        }
        else if (p instanceof S12PacketEntityVelocity)
        {
            S12PacketEntityVelocity v = (S12PacketEntityVelocity)p;
            j.addProperty("pkt", "S12");
            j.addProperty("id", v.func_149412_c());
            j.addProperty("mx", v.func_149411_d());
            j.addProperty("my", v.func_149410_e());
            j.addProperty("mz", v.func_149409_f());
        }
        else if (p instanceof S19PacketEntityHeadLook)
        {
            S19PacketEntityHeadLook hl = (S19PacketEntityHeadLook)p;
            j.addProperty("pkt", "S19");
            try { j.addProperty("id", s19IdField.getInt(hl)); } catch (Exception e) {}
            j.addProperty("head_yaw", hl.func_149380_c());
        }
        else if (p instanceof S1CPacketEntityMetadata)
        {
            S1CPacketEntityMetadata meta = (S1CPacketEntityMetadata)p;
            j.addProperty("pkt", "S1C");
            j.addProperty("id", meta.func_149375_d());
        }
        else if (p instanceof S13PacketDestroyEntities)
        {
            S13PacketDestroyEntities d = (S13PacketDestroyEntities)p;
            j.addProperty("pkt", "S13");
            JsonArray arr = new JsonArray();
            for (int id : d.func_149098_c()) arr.add(new JsonPrimitive(id));
            j.add("ids", arr);
        }
        else if (p instanceof S1BPacketEntityAttach)
        {
            S1BPacketEntityAttach a = (S1BPacketEntityAttach)p;
            j.addProperty("pkt", "S1B");
            j.addProperty("leash", a.func_149404_c());
            j.addProperty("id", a.func_149403_d());
            j.addProperty("vehicle_id", a.func_149402_e());
        }
        else if (p instanceof S0DPacketCollectItem)
        {
            S0DPacketCollectItem c = (S0DPacketCollectItem)p;
            j.addProperty("pkt", "S0D");
            j.addProperty("id", c.func_149354_c());
            j.addProperty("collector_id", c.func_149353_d());
        }
        else if (p instanceof S04PacketEntityEquipment)
        {
            S04PacketEntityEquipment eq = (S04PacketEntityEquipment)p;
            j.addProperty("pkt", "S04");
            j.addProperty("id", eq.func_149389_d());
            j.addProperty("slot", eq.func_149388_e());
        }
        else if (p instanceof S20PacketEntityProperties)
        {
            S20PacketEntityProperties prop = (S20PacketEntityProperties)p;
            j.addProperty("pkt", "S20");
            j.addProperty("id", prop.func_149442_c());
        }
        else if (p instanceof S23PacketBlockChange)
        {
            S23PacketBlockChange bc = (S23PacketBlockChange)p;
            j.addProperty("pkt", "S23");
            j.addProperty("x", bc.func_148879_d());
            j.addProperty("y", bc.func_148878_e());
            j.addProperty("z", bc.func_148877_f());
            j.addProperty("block", net.minecraft.block.Block.getIdFromBlock(bc.func_148880_c()));
            j.addProperty("meta", bc.func_148881_g());
        }
        else if (p instanceof S22PacketMultiBlockChange)
        {
            S22PacketMultiBlockChange mb = (S22PacketMultiBlockChange)p;
            j.addProperty("pkt", "S22");
            int bx = mb.func_148920_c().chunkXPos * 16;
            int bz = mb.func_148920_c().chunkZPos * 16;
            JsonArray records = new JsonArray();
            if (mb.func_148921_d() != null)
            {
                DataInputStream dis = new DataInputStream(new ByteArrayInputStream(mb.func_148921_d()));
                try {
                    for (int i = 0; i < mb.func_148922_e(); ++i)
                    {
                        short s1 = dis.readShort();
                        short s2 = dis.readShort();
                        int blockId = s2 >> 4 & 4095;
                        int meta = s2 & 15;
                        int lx = s1 >> 12 & 15;
                        int lz = s1 >> 8 & 15;
                        int ly = s1 & 255;
                        JsonObject r = new JsonObject();
                        r.addProperty("x", bx + lx);
                        r.addProperty("y", ly);
                        r.addProperty("z", bz + lz);
                        r.addProperty("block", blockId);
                        r.addProperty("meta", meta);
                        records.add(r);
                    }
                } catch (Exception e) {}
            }
            j.add("records", records);
        }
        else if (p instanceof S26PacketMapChunkBulk)
        {
            S26PacketMapChunkBulk b = (S26PacketMapChunkBulk)p;
            j.addProperty("pkt", "S26PacketMapChunkBulk");
            JsonArray chunks = new JsonArray();
            for (int i = 0; i < b.func_149254_d(); ++i)
            {
                JsonArray c = new JsonArray();
                c.add(new JsonPrimitive(b.func_149255_a(i)));
                c.add(new JsonPrimitive(b.func_149253_b(i)));
                chunks.add(c);
            }
            j.add("chunks", chunks);
        }
        else if (p instanceof S21PacketChunkData)
        {
            S21PacketChunkData cd = (S21PacketChunkData)p;
            j.addProperty("pkt", "S21PacketChunkData");
            j.addProperty("x", cd.func_149273_e());
            j.addProperty("z", cd.func_149271_f());
        }
        else if (p instanceof S28PacketEffect)
        {
            S28PacketEffect eff = (S28PacketEffect)p;
            j.addProperty("pkt", "S28PacketEffect");
            j.addProperty("effect", eff.func_149242_d());
            j.addProperty("x", eff.func_149240_f());
            j.addProperty("y", eff.func_149243_g());
            j.addProperty("z", eff.func_149239_h());
            j.addProperty("data", eff.func_149241_e());
        }
        else
        {
            j.addProperty("pkt", p.getClass().getSimpleName());
        }
        return j;
    }

    public static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        Minecraft mc = Minecraft.getMinecraft();
        int seed = cmd.get("seed").getAsInt();
        String tapePath = cmd.get("tape").getAsString();
        String snapPath = cmd.get("snap").getAsString();
        String outDir = cmd.get("out").getAsString();
        new File(outDir).mkdirs();

        // Symlink snapshot files
        String[] snapFiles = new String[]{
            "chunks.bin.gz", "chunkstate.jsonl", "entities.jsonl", "worldinfo.nbt", "worldstate.nbt",
            "det.nbt", "player_client.nbt", "player_server.nbt", "tape.jsonl"
        };
        for (String f : snapFiles)
        {
            File target = new File(snapPath, f);
            File link = new File(outDir, f);
            if (!link.exists())
            {
                try { Files.createSymbolicLink(link.toPath(), target.toPath()); }
                catch (Exception e) {}
            }
        }

        BufferedReader br = new BufferedReader(new FileReader(tapePath));
        String line;
        Map<Integer, JsonObject> rowsByT = new HashMap<Integer, JsonObject>();
        while ((line = br.readLine()) != null)
        {
            if (line.trim().isEmpty()) continue;
            JsonObject obj = new JsonParser().parse(line).getAsJsonObject();
            if (obj.has("t"))
            {
                rowsByT.put(obj.get("t").getAsInt(), obj);
            }
        }
        br.close();

        Queue queue = (Queue)recvQueueField.get(mc.getNetHandler().getNetworkManager());

        // Initial state at tick 2
        JsonObject initObj = new JsonObject();
        initObj.addProperty("tick", 2);
        long initRand = Det.state(mc.theWorld.rand);
        int initLcg = (Integer)lcgField.get(mc.theWorld);
        initObj.addProperty("cw_rand", Rows.hex(initRand));
        initObj.addProperty("cw_update_lcg", initLcg);

        JsonArray initPkts = new JsonArray();
        for (Object p : queue)
        {
            initPkts.add(serializePacket((Packet)p));
        }
        initObj.add("initial_packets", initPkts);

        PrintWriter initPw = new PrintWriter(new FileWriter(new File(outDir, "initial.json")));
        initPw.println(new GsonBuilder().setPrettyPrinting().create().toJson(initObj));
        initPw.close();

        // Gzip output for rows
        JsonObject res = new JsonObject();
        PrintWriter rowPw = new PrintWriter(new OutputStreamWriter(new GZIPOutputStream(new FileOutputStream(new File(outDir, "clientrows.jsonl.gz")))));

        int maxT = 0;
        for (int t : rowsByT.keySet()) if (t > maxT) maxT = t;

        Oracle.detail = true;
        for (int t = 2; t <= maxT; ++t)
        {
            JsonObject expRow = rowsByT.get(t);
            long cs_before = Det.seederState(Det.CLIENT);
            Act cur = Act.parseTape(expRow.has("act") ? expRow.getAsJsonObject("act") : null);
            Oracle.cur = cur;
            Oracle.preTick(mc);
            long cs_mid0 = Det.seederState(Det.CLIENT);
            
boolean sprintBefore = mc.thePlayer.isSprinting();
            boolean ogBefore = mc.thePlayer.onGround;
            Det.clientNews.clear();
            long s_pre = Det.seederState(Det.CLIENT);
            long m_pre = Det.mathState(Det.CLIENT);
            mc.runTick();
            long s_post = Det.seederState(Det.CLIENT);
            long m_post = Det.mathState(Det.CLIENT);

            int digCount = 0;
            for (String cn : Det.clientNews)
            {
                if (cn.contains("EffectRenderer")) digCount++;
            }
            Oracle.postTick(mc);

            JsonObject actD = Rows.lastRow.getAsJsonObject("d");
            JsonObject rowOut = new JsonObject();
            rowOut.addProperty("t", t);
            rowOut.add("d", actD);
            rowOut.addProperty("sprint", sprintBefore);
            rowOut.addProperty("og", ogBefore);
            rowOut.addProperty("dig", digCount);

            long curRand = Det.state(mc.theWorld.rand);
            int curLcg = (Integer)lcgField.get(mc.theWorld);
            rowOut.addProperty("cw_rand", Rows.hex(curRand));
            rowOut.addProperty("cw_update_lcg", curLcg);

            // Client entity list
            JsonArray entsArr = new JsonArray();
            for (Object o : mc.theWorld.loadedEntityList)
            {
                Entity e = (Entity)o;
                JsonObject entJ = new JsonObject();
                entJ.addProperty("id", e.getEntityId());
                entJ.addProperty("cls", e.getClass().getSimpleName());
                entJ.addProperty("x", e.posX);
                entJ.addProperty("y", e.posY);
                entJ.addProperty("z", e.posZ);
                entJ.addProperty("mx", e.motionX);
                entJ.addProperty("my", e.motionY);
                entJ.addProperty("mz", e.motionZ);
                entJ.addProperty("yaw", e.rotationYaw);
                entJ.addProperty("pitch", e.rotationPitch);
                entJ.addProperty("sx", e.serverPosX);
                entJ.addProperty("sy", e.serverPosY);
                entJ.addProperty("sz", e.serverPosZ);

                if (e instanceof EntityLivingBase)
                {
                    entJ.addProperty("inc", (Integer)newPosIncField.get(e));
                    entJ.addProperty("nx", (Double)newPosXField.get(e));
                    entJ.addProperty("ny", (Double)newPosYField.get(e));
                    entJ.addProperty("nz", (Double)newPosZField.get(e));
                    entJ.addProperty("nyaw", (Double)newRotYawField.get(e));
                    entJ.addProperty("npitch", (Double)newRotPitchField.get(e));
                }
                else
                {
                    entJ.addProperty("inc", 0);
                }
                entsArr.add(entJ);
            }
            rowOut.add("ents", entsArr);

            // Packets in queue from server tick t
            JsonArray pktsArr = new JsonArray();
            for (Object p : queue)
            {
                pktsArr.add(serializePacket((Packet)p));
            }
            rowOut.add("packets", pktsArr);

            rowPw.println(rowOut.toString());
        }
                rowPw.close();

        // Manifest
        JsonObject manifest = new JsonObject();
        manifest.addProperty("kind", "netherite-clientworld");
        manifest.addProperty("v", 1);
        manifest.addProperty("seed", seed);
        manifest.addProperty("tape", tapePath);
        manifest.addProperty("snapshot", snapPath);
        PrintWriter manPw = new PrintWriter(new FileWriter(new File(outDir, "manifest.json")));
        manPw.println(new GsonBuilder().setPrettyPrinting().create().toJson(manifest));
        manPw.close();

        res.addProperty("ok", true);
        res.addProperty("ticksRecorded", maxT - 1);
        return res;
    }

    }
