package net.minecraft.oracle;

import java.io.DataOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Exports world state to a binary format readable from C.
 *
 * File format:
 *   Header (24 bytes):
 *     bytes 0-3:   magic "NSTA" (0x4E535441)
 *     bytes 4-7:   version (u32, = 1)
 *     bytes 8-11:  tick number (i32)
 *     bytes 12-15: dimension ID (i32)
 *     bytes 16-19: chunk count (u32)
 *     bytes 20-23: entity count (u32)
 *
 *   Chunk data (repeated chunk_count times):
 *     chunkX (i32), chunkZ (i32)
 *     16 sections, each:
 *       present (u8): 0 or 1
 *       if present: blockLSB[4096] + metadata[2048]
 *
 *   Entity data (repeated entity_count times):
 *     entityTypeId (i16)
 *     posX, posY, posZ (f64 x 3)
 *     motionX, motionY, motionZ (f64 x 3)
 *     yaw, pitch (f32 x 2)
 *     health (f32)
 *
 *   Player data (appended after entities, one per online player):
 *     marker (i16 = -1, distinguishes from entities)
 *     posX, posY, posZ (f64 x 3)
 *     motionX, motionY, motionZ (f64 x 3)
 *     yaw, pitch (f32 x 2)
 *     health (f32)
 *     foodLevel (i32)
 *     saturation (f32)
 *     currentItem (i32)
 *     dimension (i32)
 *     onGround (u8)
 *     inventory: 40 slots, each:
 *       present (u8)
 *       if present: itemId (i16), stackSize (i8), damage (i16)
 */
public class OracleStateExporter
{
    private static final Logger logger = LogManager.getLogger();
    private static final int MAGIC = 0x4E535441; // "NSTA"
    private static final int VERSION = 1;

    public static void exportState(WorldServer world, String path, int tick)
    {
        try
        {
            DataOutputStream dos = new DataOutputStream(new FileOutputStream(path));

            // Sort chunks by (x, z) for deterministic output order
            ArrayList sortedChunks = new ArrayList(world.theChunkProviderServer.loadedChunks);
            Collections.sort(sortedChunks, new Comparator() {
                public int compare(Object a, Object b) {
                    Chunk ca = (Chunk) a;
                    Chunk cb = (Chunk) b;
                    if (ca.xPosition != cb.xPosition) return ca.xPosition - cb.xPosition;
                    return ca.zPosition - cb.zPosition;
                }
            });
            int chunkCount = sortedChunks.size();

            // Count non-player entities
            int entityCount = 0;
            int playerCount = 0;
            for (int i = 0; i < world.loadedEntityList.size(); i++)
            {
                Entity e = (Entity) world.loadedEntityList.get(i);
                if (e instanceof EntityPlayerMP)
                    playerCount++;
                else
                    entityCount++;
            }

            // Write header
            dos.writeInt(MAGIC);
            dos.writeInt(VERSION);
            dos.writeInt(tick);
            dos.writeInt(world.provider.dimensionId);
            dos.writeInt(chunkCount);
            dos.writeInt(entityCount + playerCount); // total entity records

            // Write chunks
            for (int i = 0; i < chunkCount; i++)
            {
                Chunk chunk = (Chunk) sortedChunks.get(i);
                dos.writeInt(chunk.xPosition);
                dos.writeInt(chunk.zPosition);

                ExtendedBlockStorage[] sections = chunk.getBlockStorageArray();
                for (int s = 0; s < 16; s++)
                {
                    if (sections[s] == null || sections[s].isEmpty())
                    {
                        dos.writeByte(0);
                    }
                    else
                    {
                        dos.writeByte(1);
                        dos.write(sections[s].getBlockLSBArray()); // 4096 bytes
                        dos.write(sections[s].getMetadataArray().data); // 2048 bytes
                    }
                }
            }

            // Write entities (non-player)
            for (int i = 0; i < world.loadedEntityList.size(); i++)
            {
                Entity e = (Entity) world.loadedEntityList.get(i);
                if (e instanceof EntityPlayerMP) continue;

                int typeId = EntityList.getEntityID(e);
                dos.writeShort(typeId);
                dos.writeDouble(e.posX);
                dos.writeDouble(e.posY);
                dos.writeDouble(e.posZ);
                dos.writeDouble(e.motionX);
                dos.writeDouble(e.motionY);
                dos.writeDouble(e.motionZ);
                dos.writeFloat(e.rotationYaw);
                dos.writeFloat(e.rotationPitch);
                float health = (e instanceof EntityLivingBase) ? ((EntityLivingBase) e).getHealth() : 0.0f;
                dos.writeFloat(health);
            }

            // Write players
            for (int i = 0; i < world.loadedEntityList.size(); i++)
            {
                Entity e = (Entity) world.loadedEntityList.get(i);
                if (!(e instanceof EntityPlayerMP)) continue;

                EntityPlayerMP player = (EntityPlayerMP) e;
                dos.writeShort(-1); // marker for player
                dos.writeDouble(player.posX);
                dos.writeDouble(player.posY);
                dos.writeDouble(player.posZ);
                dos.writeDouble(player.motionX);
                dos.writeDouble(player.motionY);
                dos.writeDouble(player.motionZ);
                dos.writeFloat(player.rotationYaw);
                dos.writeFloat(player.rotationPitch);
                dos.writeFloat(player.getHealth());
                dos.writeInt(player.getFoodStats().getFoodLevel());
                dos.writeFloat(player.getFoodStats().getSaturationLevel());
                dos.writeInt(player.inventory.currentItem);
                dos.writeInt(player.dimension);
                dos.writeByte(player.onGround ? 1 : 0);

                // Inventory: 36 main + 4 armor = 40 slots
                for (int slot = 0; slot < 36; slot++)
                {
                    writeItemStack(dos, player.inventory.mainInventory[slot]);
                }
                for (int slot = 0; slot < 4; slot++)
                {
                    writeItemStack(dos, player.inventory.armorInventory[slot]);
                }
            }

            dos.flush();
            dos.close();
            logger.info("[Oracle] State exported: tick=" + tick + ", dim=" + world.provider.dimensionId +
                ", chunks=" + chunkCount + ", entities=" + entityCount + ", players=" + playerCount + ", file=" + path);
        }
        catch (IOException e)
        {
            logger.error("[Oracle] Failed to export state", e);
        }
    }

    private static void writeItemStack(DataOutputStream dos, ItemStack stack) throws IOException
    {
        if (stack == null)
        {
            dos.writeByte(0);
        }
        else
        {
            dos.writeByte(1);
            dos.writeShort(Item.getIdFromItem(stack.getItem()));
            dos.writeByte(stack.stackSize);
            dos.writeShort(stack.getItemDamage());
        }
    }

    /**
     * Export all loaded dimensions at once.
     */
    public static void exportAllDimensions(MinecraftServer server, String baseDir, int tick)
    {
        WorldServer[] worlds = server.worldServers;
        if (worlds == null) return;

        for (WorldServer world : worlds)
        {
            if (world == null) continue;
            String path = baseDir + "/state_dim" + world.provider.dimensionId + "_tick" + tick + ".nsta";
            exportState(world, path, tick);
        }
    }
}
