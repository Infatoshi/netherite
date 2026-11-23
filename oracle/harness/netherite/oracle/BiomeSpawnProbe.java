package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.File;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.structure.MapGenScatteredFeature;

/**
 * Every biome's spawn lists, the reference for the native
 * spawning_list_for (csrc/engine/spawning.c, checked by
 * csrc/tests/test_biome_spawn.c): for each registered biome id and each
 * EnumCreatureType, BiomeGenBase.getSpawnableList (what
 * ChunkProviderGenerate.getPossibleCreatures returns outside a swamp hut),
 * after WorldConf.pruneSpawns, in list order. The swamp hut's own list
 * (MapGenScatteredFeature.getScatteredFeatureSpawnList, which
 * getPossibleCreatures returns for monsters inside a hut) rides along.
 *
 * Run: make run SEED=1 CLASS=BiomeSpawnProbe NAME=bs-1 CMD='{"out":"/abs/out/java/biome_spawn/bs-1"}'
 *
 * manifest.json: "biomes", one object per registered slot (slot, id, name;
 * slot 161 holds a second object whose biomeID is 160, and the chunk biome
 * array carries the id, so the native test looks lists up by id; and the
 * four lists monster, creature, ambient, water as rows of class, weight,
 * min, max), and "witch", the hut's rows.
 */
final class BiomeSpawnProbe
{
    private BiomeSpawnProbe() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File d = new File(cmd.get("out").getAsString());
        d.mkdirs();

        WorldServer ws = server.worldServers[0];
        ChunkProviderGenerate gen = (ChunkProviderGenerate)Snapshot.objField(ws.theChunkProviderServer, "currentChunkProvider");
        MapGenScatteredFeature huts = (MapGenScatteredFeature)Snapshot.objField(gen, "scatteredFeatureGenerator");

        JsonArray biomes = new JsonArray();
        int n = 0;
        BiomeGenBase[] all = BiomeGenBase.getBiomeGenArray();
        for (int slot = 0; slot < all.length; ++slot)
        {
            BiomeGenBase b = all[slot];
            if (b == null) continue;
            JsonObject o = new JsonObject();
            o.addProperty("slot", slot);
            o.addProperty("id", b.biomeID);
            o.addProperty("name", b.biomeName);
            o.add("monster", FortressSpawnProbe.rows(b.getSpawnableList(EnumCreatureType.monster)));
            o.add("creature", FortressSpawnProbe.rows(b.getSpawnableList(EnumCreatureType.creature)));
            o.add("ambient", FortressSpawnProbe.rows(b.getSpawnableList(EnumCreatureType.ambient)));
            o.add("water", FortressSpawnProbe.rows(b.getSpawnableList(EnumCreatureType.waterCreature)));
            biomes.add(o);
            ++n;
        }

        JsonObject m = new JsonObject();
        m.addProperty("kind", "biome_spawn");
        m.addProperty("seed", ws.getSeed());
        m.add("biomes", biomes);
        m.add("witch", FortressSpawnProbe.rows(huts.getScatteredFeatureSpawnList()));
        Snapshot.writeFile(new File(d, "manifest.json"), m.toString());

        JsonObject r = new JsonObject();
        r.addProperty("biomes", n);
        return r;
    }
}
