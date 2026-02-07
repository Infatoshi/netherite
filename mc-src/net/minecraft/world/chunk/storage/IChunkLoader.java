package net.minecraft.world.chunk.storage;

import java.io.IOException;
import net.minecraft.world.MinecraftException;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;

public interface IChunkLoader
{
    /**
     * Loads the specified(XZ) chunk into the specified world.
     */
    Chunk loadChunk(World p_75815_1_, int p_75815_2_, int p_75815_3_) throws IOException;

    void saveChunk(World p_75816_1_, Chunk p_75816_2_) throws MinecraftException, IOException;

    /**
     * Save extra data associated with this Chunk not normally saved during autosave, only during chunk unload.
     * Currently unused.
     */
    void saveExtraChunkData(World p_75819_1_, Chunk p_75819_2_);

    /**
     * Called every World.tick()
     */
    void chunkTick();

    /**
     * Save extra data not associated with any Chunk.  Not saved during autosave, only during world unload.  Currently
     * unused.
     */
    void saveExtraData();
}