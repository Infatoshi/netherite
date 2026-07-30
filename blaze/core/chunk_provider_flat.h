/* chunk_provider_flat: REAL-CHUNK flat preset pipeline = MC 1.11.2
 * ChunkProviderFlat.provideChunk (net/minecraft/world/gen/ChunkProviderFlat.java),
 * MINUS structure generators, MINUS Chunk/skylight/biome array/populate.
 *
 * Parses FlatGeneratorInfo.createFlatGeneratorFromString (FlatGeneratorInfo.java +
 * FlatLayerInfo.java) for preset layers, builds cachedBlockIDs[256] verbatim to the ctor loop,
 * then fills a ChunkPrimer column-wise. Default preset == getDefaultFlatGenerator()
 * (1 bedrock, 2 dirt, 1 grass) when settings is null/invalid; also accepts version-0 strings
 * like "1x7,2x3,1x2" and version-3 "3;1*minecraft:bedrock,2*minecraft:dirt,1*minecraft:grass;1".
 *
 * Block-state ids are the sanctioned small-int substitution (CPF_* below), unified across
 * golden + candidate, matching chunk_provider.h CB_* for the blocks flat presets use.
 *
 * Seed is ignored by provideChunk (structures/populate excluded); drivers accept seed for
 * convention parity only. */
#ifndef MC_CHUNK_PROVIDER_FLAT_H
#define MC_CHUNK_PROVIDER_FLAT_H

#include "mc.h"

enum {
    CPF_AIR = 0, CPF_STONE = 1, CPF_WATER = 2, CPF_GRASS = 3, CPF_DIRT = 4, CPF_BEDROCK = 5,
    CPF_GRAVEL = 6, CPF_SAND = 7, CPF_SANDSTONE = 8
};

#define CPF_MAX_LAYERS 64

typedef struct { u16 data[65536]; } CpfPrimer;
MC_HD static inline int  cpf_index(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD static inline void cpf_set(CpfPrimer *p, int x, int y, int z, int v) {
    p->data[cpf_index(x, y, z)] = (u16)v;
}

typedef struct {
    int count;
    int block_state;   /* CPF_* */
    int min_y;
} CpfLayer;

typedef struct {
    CpfLayer layers[CPF_MAX_LAYERS];
    int n_layers;
    int biome;         /* parsed but unused by provideChunk */
} CpfFlatInfo;

/* vanilla numeric block id (+ meta) -> CPF block-state id */
MC_HD static inline int cpf_state_from_block_id(int block_id, int meta) {
    (void)meta;
    switch (block_id) {
        case 0: return CPF_AIR;
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

MC_HD static inline int cpf_parse_int(const char *s, int def) {
    if (!s || !*s) return def;
    int sign = 1, v = 0;
    if (*s == '-') { sign = -1; ++s; }
    if (!(*s >= '0' && *s <= '9')) return def;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return sign * v;
}

/* case-insensitive prefix match */
MC_HD static inline int cpf_streq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

MC_HD static inline int cpf_state_from_name(const char *name, int meta) {
    (void)meta;
    if (!name) return CPF_AIR;
    if (cpf_streq_ci(name, "minecraft:air") || cpf_streq_ci(name, "air")) return CPF_AIR;
    if (cpf_streq_ci(name, "minecraft:stone") || cpf_streq_ci(name, "stone")) return CPF_STONE;
    if (cpf_streq_ci(name, "minecraft:grass") || cpf_streq_ci(name, "grass")) return CPF_GRASS;
    if (cpf_streq_ci(name, "minecraft:dirt") || cpf_streq_ci(name, "dirt")) return CPF_DIRT;
    if (cpf_streq_ci(name, "minecraft:bedrock") || cpf_streq_ci(name, "bedrock")) return CPF_BEDROCK;
    if (cpf_streq_ci(name, "minecraft:sand") || cpf_streq_ci(name, "sand")) return CPF_SAND;
    if (cpf_streq_ci(name, "minecraft:gravel") || cpf_streq_ci(name, "gravel")) return CPF_GRAVEL;
    if (cpf_streq_ci(name, "minecraft:sandstone") || cpf_streq_ci(name, "sandstone")) return CPF_SANDSTONE;
    return CPF_STONE;
}

MC_HD static inline void cpf_default_flat(CpfFlatInfo *info) {
    info->n_layers = 3;
    info->biome = 1;
    info->layers[0] = (CpfLayer){ 1, CPF_BEDROCK, 0 };
    info->layers[1] = (CpfLayer){ 2, CPF_DIRT, 1 };
    info->layers[2] = (CpfLayer){ 1, CPF_GRASS, 3 };
}

MC_HD static inline void cpf_update_layers(CpfFlatInfo *info) {
    int y = 0;
    for (int i = 0; i < info->n_layers; ++i) {
        info->layers[i].min_y = y;
        y += info->layers[i].count;
    }
}

/* split layer token "Nxblock" or "N*block" (version>=3 uses *). Returns 0 on failure. */
MC_HD static inline int cpf_parse_layer_token(int version, const char *token, int cur_y, CpfLayer *out) {
    char buf[128];
    int n = 0;
    while (token[n] && n < 127) { buf[n] = token[n]; ++n; }
    buf[n] = 0;

    char count_part[32] = {0};
    char block_part[96] = {0};
    char sep = (version >= 3) ? '*' : 'x';
    int ci = 0, bi = 0, found = 0;
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == sep && !found) { found = 1; continue; }
        if (!found) {
            if (ci < 31) count_part[ci++] = buf[i];
        } else {
            if (bi < 95) block_part[bi++] = buf[i];
        }
    }
    int cnt = 1;
    if (found) {
        cnt = cpf_parse_int(count_part, 1);
        if (cur_y + cnt >= 256) cnt = 256 - cur_y;
        if (cnt < 0) cnt = 0;
    } else {
        /* no separator: entire token is block spec, count=1 */
        for (int i = 0; buf[i]; ++i) block_part[i] = buf[i];
        block_part[127] = 0;
    }

    int meta = 0;
    int block_state;
    if (version < 3) {
        char id_part[32] = {0};
        char meta_part[16] = {0};
        int ii = 0, mi = 0, colon = 0;
        for (int i = 0; block_part[i]; ++i) {
            if (block_part[i] == ':') { colon = 1; continue; }
            if (!colon) { if (ii < 31) id_part[ii++] = block_part[i]; }
            else { if (mi < 15) meta_part[mi++] = block_part[i]; }
        }
        int bid = cpf_parse_int(id_part, -1);
        if (bid < 0) return 0;
        meta = cpf_parse_int(meta_part, 0);
        if (bid == 0) meta = 0;
        if (meta < 0 || meta > 15) meta = 0;
        block_state = cpf_state_from_block_id(bid, meta);
    } else {
        /* FlatGeneratorInfo.getLayerFromString v3: split(":", 3) then registry lookup. */
        char p0[48] = {0}, p1[48] = {0}, p2[16] = {0};
        int np = 0, pi = 0;
        for (int i = 0; ; ++i) {
            char c = block_part[i];
            char *dst = (np == 0) ? p0 : (np == 1) ? p1 : p2;
            int cap = (np == 2) ? 15 : 47;
            if (c == ':' || c == 0) {
                if (c == ':') { ++np; pi = 0; if (np > 2) break; }
                if (!c) break;
                continue;
            }
            if (pi < cap) dst[pi++] = c;
        }
        char full[96] = {0};
        if (np >= 1 && p1[0]) {
            int fi = 0;
            for (int i = 0; p0[i] && fi < 95; ++i) full[fi++] = p0[i];
            if (fi < 95) full[fi++] = ':';
            for (int i = 0; p1[i] && fi < 95; ++i) full[fi++] = p1[i];
            meta = cpf_parse_int(p2, 0);
            block_state = cpf_state_from_name(full, meta);
        } else {
            meta = cpf_parse_int(p1, 0);
            block_state = cpf_state_from_name(p0, meta);
        }
        if (meta < 0 || meta > 15) meta = 0;
        if (block_state == CPF_AIR && meta == 0 &&
            !cpf_streq_ci(full, "minecraft:air") && !cpf_streq_ci(p0, "air") &&
            !cpf_streq_ci(full, "air"))
            block_state = CPF_STONE;
    }

    out->count = cnt;
    out->block_state = block_state;
    out->min_y = cur_y;
    return 1;
}

/* Parse comma-separated layer list (stops at ';' if present). */
MC_HD static inline int cpf_parse_layers(int version, const char *layers_str, CpfFlatInfo *info) {
    if (!layers_str || !*layers_str) return 0;
    int cur_y = 0, n = 0;
    char token[128];
    int ti = 0;
    for (int i = 0; ; ++i) {
        char c = layers_str[i];
        if (c == ';') break;
        if (c && c != ',') {
            if (ti < 127) token[ti++] = c;
        } else {
            if (ti > 0) {
                token[ti] = 0;
                if (n >= CPF_MAX_LAYERS) return 0;
                if (!cpf_parse_layer_token(version, token, cur_y, &info->layers[n])) return 0;
                cur_y += info->layers[n].count;
                ++n;
                ti = 0;
            }
            if (!c) break;
        }
    }
    if (ti > 0) {
        token[ti] = 0;
        if (n >= CPF_MAX_LAYERS) return 0;
        if (!cpf_parse_layer_token(version, token, cur_y, &info->layers[n])) return 0;
        ++n;
    }
    if (n == 0) return 0;
    info->n_layers = n;
    return 1;
}

/* FlatGeneratorInfo.createFlatGeneratorFromString */
MC_HD static inline void cpf_parse_flat_string(const char *settings, CpfFlatInfo *info) {
    if (!settings) { cpf_default_flat(info); return; }

    /* count semicolon segments */
    int nparts = 1;
    for (const char *p = settings; *p; ++p) if (*p == ';') ++nparts;

    const char *parts[8];
    int n = 0;
    const char *start = settings;
    for (const char *p = settings; ; ++p) {
        if (*p == ';' || *p == 0) {
            parts[n++] = start;
            if (*p == 0 || n >= 8) break;
            start = p + 1;
        }
    }

    int version = (nparts == 1) ? 0 : cpf_parse_int(parts[0], 0);
    if (version < 0 || version > 3) { cpf_default_flat(info); return; }

    int lj = (nparts == 1) ? 0 : 1;
    if (!cpf_parse_layers(version, parts[lj], info)) { cpf_default_flat(info); return; }
    cpf_update_layers(info);

    info->biome = 1;
    if (version > 0 && n > lj + 1)
        info->biome = cpf_parse_int(parts[lj + 1], 1);
}

MC_HD static inline void cpf_build_cached(int cached[256], const CpfFlatInfo *info) {
    for (int i = 0; i < 256; ++i) cached[i] = -1;
    for (int l = 0; l < info->n_layers; ++l) {
        const CpfLayer *layer = &info->layers[l];
        for (int y = layer->min_y; y < layer->min_y + layer->count; ++y) {
            if (layer->block_state != CPF_AIR)
                cached[y] = layer->block_state;
        }
    }
}

/* ChunkProviderFlat.provideChunk minus structures. */
MC_HD static inline void cpf_provide_chunk(CpfPrimer *primer, const char *settings) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)CPF_AIR;

    CpfFlatInfo info;
    cpf_parse_flat_string(settings, &info);

    int cached[256];
    cpf_build_cached(cached, &info);

    for (int y = 0; y < 256; ++y) {
        if (cached[y] < 0) continue;
        int state = cached[y];
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                cpf_set(primer, x, y, z, state);
    }
}

#endif /* MC_CHUNK_PROVIDER_FLAT_H */
