package net.minecraft.world.chunk;

import java.util.List;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.util.IProgressUpdate;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.World;

public interface IChunkProvider
{
    /**
     * Checks to see if a chunk exists at x, y
     */
    boolean chunkExists(int p_73149_1_, int p_73149_2_);

    /**
     * Will return back a chunk, if it doesn't exist and its not a MP client it will generates all the blocks for the
     * specified chunk from the map seed and chunk seed
     */
    Chunk provideChunk(int p_73154_1_, int p_73154_2_);

    /**
     * loads or generates the chunk at the chunk location specified
     */
    Chunk loadChunk(int p_73158_1_, int p_73158_2_);

    /**
     * Populates chunk with ores etc etc
     */
    void populate(IChunkProvider p_73153_1_, int p_73153_2_, int p_73153_3_);

    /**
     * Two modes of operation: if passed true, save all Chunks in one go.  If passed false, save up to two chunks.
     * Return true if all chunks have been saved.
     */
    boolean saveChunks(boolean p_73151_1_, IProgressUpdate p_73151_2_);

    /**
     * Unloads chunks that are marked to be unloaded. This is not guaranteed to unload every such chunk.
     */
    boolean unloadQueuedChunks();

    /**
     * Returns if the IChunkProvider supports saving.
     */
    boolean canSave();

    /**
     * Converts the instance data to a readable string.
     */
    String makeString();

    /**
     * Returns a list of creatures of the specified type that can spawn at the given location.
     */
    List getPossibleCreatures(EnumCreatureType p_73155_1_, int p_73155_2_, int p_73155_3_, int p_73155_4_);

    ChunkPosition func_147416_a(World p_147416_1_, String p_147416_2_, int p_147416_3_, int p_147416_4_, int p_147416_5_);

    int getLoadedChunkCount();

    void recreateStructures(int p_82695_1_, int p_82695_2_);

    /**
     * Save extra data not associated with any Chunk.  Not saved during autosave, only during world unload.  Currently
     * unimplemented.
     */
    void saveExtraData();
}