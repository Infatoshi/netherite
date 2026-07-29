// Verbatim MC 1.11.2 ChunkProviderFlat.provideChunk + FlatGeneratorInfo.createFlatGeneratorFromString
// (net/minecraft/world/gen/ChunkProviderFlat.java, FlatGeneratorInfo.java, FlatLayerInfo.java).
// Structures/Chunk/skylight/biome array excluded. Block OBJECT registry -> CPF_* integer ids
// (matching core/chunk_provider_flat.h): AIR=0, STONE=1, GRASS=3, DIRT=4, BEDROCK=5, SAND=7, etc.
// Output: ChunkPrimer char[65536] index order (x<<12|z<<8|y), %04x per line.
public class Golden {

    static final int CPF_AIR = 0, CPF_STONE = 1, CPF_GRASS = 3, CPF_DIRT = 4, CPF_BEDROCK = 5,
        CPF_SAND = 7, CPF_GRAVEL = 6, CPF_SANDSTONE = 8;

    static class FlatLayer {
        int version, layerCount, minY, blockState;
        FlatLayer(int ver, int h, int state) {
            version = ver; layerCount = h; blockState = state; minY = 0;
        }
        void setMinY(int y) { minY = y; }
        int getLayerCount() { return layerCount; }
        int getMinY() { return minY; }
        int getBlockState() { return blockState; }
        boolean isAir() { return blockState == CPF_AIR; }
    }

    static class FlatInfo {
        FlatLayer[] layers = new FlatLayer[64];
        int nLayers = 0;
        int biome = 1;
        void add(FlatLayer l) { layers[nLayers++] = l; }
        void updateLayers() {
            int y = 0;
            for (int i = 0; i < nLayers; ++i) {
                layers[i].setMinY(y);
                y += layers[i].getLayerCount();
            }
        }
    }

    static int blockIdToState(int id, int meta) {
        if (id == 0) { meta = 0; return CPF_AIR; }
        if (meta < 0 || meta > 15) meta = 0;
        switch (id) {
            case 1: return CPF_STONE;
            case 2: return CPF_GRASS;
            case 3: return CPF_DIRT;
            case 7: return CPF_BEDROCK;
            case 12: return CPF_SAND;
            case 13: return CPF_GRAVEL;
            case 24: return CPF_SANDSTONE;
            default: return CPF_STONE;
        }
    }

    static int blockNameToState(String name, int meta) {
        if (name == null) return CPF_AIR;
        String n = name.toLowerCase();
        if (n.equals("minecraft:air") || n.equals("air")) return CPF_AIR;
        if (n.equals("minecraft:stone") || n.equals("stone")) return CPF_STONE;
        if (n.equals("minecraft:grass") || n.equals("grass")) return CPF_GRASS;
        if (n.equals("minecraft:dirt") || n.equals("dirt")) return CPF_DIRT;
        if (n.equals("minecraft:bedrock") || n.equals("bedrock")) return CPF_BEDROCK;
        if (n.equals("minecraft:sand") || n.equals("sand")) return CPF_SAND;
        if (n.equals("minecraft:gravel") || n.equals("gravel")) return CPF_GRAVEL;
        if (n.equals("minecraft:sandstone") || n.equals("sandstone")) return CPF_SANDSTONE;
        return CPF_STONE;
    }

    static int getInt(String s, int def) {
        if (s == null || s.isEmpty()) return def;
        try { return Integer.parseInt(s); } catch (Throwable t) { return def; }
    }

    // FlatGeneratorInfo.getLayerFromString
    static FlatLayer getLayerFromString(int ver, String token, int curY) {
        String[] astring = ver >= 3 ? token.split("\\*", 2) : token.split("x", 2);
        int count = 1;
        if (astring.length == 2) {
            try {
                count = Integer.parseInt(astring[0]);
                if (curY + count >= 256) count = 256 - curY;
                if (count < 0) count = 0;
            } catch (Throwable t) { return null; }
        }
        try {
            String s = astring[astring.length - 1];
            int meta = 0;
            int state;
            if (ver < 3) {
                astring = s.split(":", 2);
                if (astring.length > 1) meta = Integer.parseInt(astring[1]);
                state = blockIdToState(Integer.parseInt(astring[0]), meta);
            } else {
                astring = s.split(":", 3);
                if (astring.length > 1) {
                    state = blockNameToState(astring[0] + ":" + astring[1],
                        astring.length > 2 ? Integer.parseInt(astring[2]) : 0);
                } else {
                    state = blockNameToState(astring[0],
                        astring.length > 1 ? Integer.parseInt(astring[1]) : 0);
                }
            }
            FlatLayer layer = new FlatLayer(ver, count, state);
            layer.setMinY(curY);
            return layer;
        } catch (Throwable t) {
            return null;
        }
    }

    static FlatLayer[] getLayersFromString(int ver, String layers) {
        if (layers == null || layers.length() < 1) return null;
        String[] tokens = layers.split(",");
        FlatLayer[] out = new FlatLayer[tokens.length];
        int cur = 0, n = 0;
        for (String tok : tokens) {
            FlatLayer l = getLayerFromString(ver, tok, cur);
            if (l == null) return null;
            out[n++] = l;
            cur += l.getLayerCount();
        }
        FlatLayer[] trim = new FlatLayer[n];
        System.arraycopy(out, 0, trim, 0, n);
        return trim;
    }

    static FlatInfo getDefaultFlatGenerator() {
        FlatInfo info = new FlatInfo();
        info.biome = 1;
        info.add(new FlatLayer(1, 1, CPF_BEDROCK));
        info.add(new FlatLayer(1, 2, CPF_DIRT));
        info.add(new FlatLayer(1, 1, CPF_GRASS));
        info.updateLayers();
        return info;
    }

    // FlatGeneratorInfo.createFlatGeneratorFromString
    static FlatInfo createFlatGeneratorFromString(String settings) {
        if (settings == null) return getDefaultFlatGenerator();
        String[] astring = settings.split(";", -1);
        int ver = astring.length == 1 ? 0 : getInt(astring[0], 0);
        if (ver < 0 || ver > 3) return getDefaultFlatGenerator();
        int j = astring.length == 1 ? 0 : 1;
        FlatLayer[] list = getLayersFromString(ver, astring[j++]);
        if (list == null || list.length == 0) return getDefaultFlatGenerator();
        FlatInfo info = new FlatInfo();
        for (FlatLayer l : list) info.add(l);
        info.updateLayers();
        int biome = 1;
        if (ver > 0 && astring.length > j) biome = getInt(astring[j++], biome);
        info.biome = biome;
        return info;
    }

    // ChunkProviderFlat ctor cachedBlockIDs build (sea-level j/k loop omitted - not in output).
    static int[] buildCached(FlatInfo info) {
        int[] cached = new int[256];
        for (int i = 0; i < 256; ++i) cached[i] = -1;
        for (int li = 0; li < info.nLayers; ++li) {
            FlatLayer layer = info.layers[li];
            for (int y = layer.getMinY(); y < layer.getMinY() + layer.getLayerCount(); ++y) {
                if (!layer.isAir()) cached[y] = layer.getBlockState();
            }
        }
        return cached;
    }

    // ChunkProviderFlat.provideChunk minus structures.
    static char[] provideChunk(int[] cached) {
        char[] data = new char[65536];
        for (int i = 0; i < 65536; ++i) data[i] = (char)CPF_AIR;
        for (int y = 0; y < cached.length; ++y) {
            if (cached[y] < 0) continue;
            char state = (char)cached[y];
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z)
                    data[x << 12 | z << 8 | y] = state;
        }
        return data;
    }

    public static void main(String[] args) {
        String preset = args.length > 1 ? args[1] : null;
        FlatInfo info = createFlatGeneratorFromString(preset);
        int[] cached = buildCached(info);
        char[] primer = provideChunk(cached);
        for (int i = 0; i < 65536; ++i)
            System.out.printf("%04x%n", (int)primer[i]);
    }
}
