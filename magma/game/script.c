#include "game/script.h"
#include "game/runtime.h"
#include "game/native_save.h"
/* after runtime.h: entity_render.h's guarded GmEntityView redecl must see
 * game.h's full definition (MAGMA_GAME_H) or the types conflict. */
#include "core/config.h"
#include "game/entity_render.h"
#include "game/frame_capture.h"
#include "game/hand.h"
#include "game/particles_live.h"
#include "game/screen.h"
#include "game/sel_box.h"
#include "game/window_compose.h"
#include "container_click.h"
#include "tile_entity_brewing.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JL_FIELDS 96
#define JL_KEY 32
#define JL_VALUE 128

typedef struct { char key[JL_KEY]; char value[JL_VALUE]; int string; } JlField;
typedef struct { JlField f[JL_FIELDS]; int n; } JlObject;

static void write_hex(FILE *out, const uint8_t *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        fputc(digits[data[i] >> 4], out);
        fputc(digits[data[i] & 15], out);
    }
}

static void write_json_bytes(
        FILE *out, const unsigned char *text, size_t length) {
    fputc('"', out);
    for (size_t index = 0; index < length; ++index) {
        unsigned char c = text[index];
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc(c, out);
        } else if (c == '\b') fputs("\\b", out);
        else if (c == '\f') fputs("\\f", out);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 32) fprintf(out, "\\u%04x", (unsigned)c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static void write_json_string(FILE *out, const char *text) {
    write_json_bytes(
        out, (const unsigned char *)text, text ? strlen(text) : 0);
}

static const char *json_floating(
        char buffer[static 32], double value, int precision) {
    if (value == 0.0 && signbit(value)) {
        memcpy(buffer, "-0.0", 5);
        return buffer;
    }
    snprintf(buffer, 32, "%.*g", precision, value);
    return buffer;
}

static void write_stack_payload(
        FILE *out, const GmRuntime *runtime, const ICStack *stack) {
    const GmNbtBlob *tag = gm_runtime_stack_tag(runtime, stack->tag_id);
    if (!tag || !tag->data || tag->len == 0) return;
    fputs(",\"stack_payload\":{\"kind\":\"item_tag\",\"nbt\":\"", out);
    write_hex(out, tag->data, tag->len);
    fputs("\"}", out);
}

static void write_stack_extra(
        FILE *out, const GmRuntime *runtime, const ICStack *stack) {
    const char *name;
    if (stack->repair_cost != 0)
        fprintf(out, ",\"repair_cost\":%d", stack->repair_cost);
    name = gm_runtime_item_name(runtime, stack->custom_name);
    if (name) {
        fputs(",\"custom_name\":", out);
        write_json_string(out, name);
    }
    write_stack_payload(out, runtime, stack);
}

static void write_merchant_stack(
        FILE *out, const GmRuntime *runtime, const ICStack *stack) {
    const char *name = gm_runtime_item_name(runtime, stack->custom_name);
    fprintf(out,
            "{\"id\":%d,\"count\":%d,\"meta\":%d,"
            "\"repair_cost\":%d,\"custom_name\":",
            stack->item, stack->count, stack->meta, stack->repair_cost);
    write_json_string(out, name ? name : "");
    fputs(",\"nbt_subset_exact\":true,\"enchants\":[", out);
    for (int i = 0; i < stack->n_enchants; ++i)
        fprintf(out, "%s[%d,%d]", i ? "," : "",
                stack->enchants[i].id, stack->enchants[i].level);
    fputc(']', out);
    write_stack_payload(out, runtime, stack);
    fputc('}', out);
}

static void write_slotted_stack(
        FILE *out, const GmRuntime *runtime, int slot,
        const ICStack *stack) {
    const char *name = gm_runtime_item_name(runtime, stack->custom_name);
    fprintf(out,
            "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d,"
            "\"repair_cost\":%d,\"custom_name\":",
            slot, stack->item, stack->count, stack->meta,
            stack->repair_cost);
    write_json_string(out, name ? name : "");
    fputs(",\"nbt_subset_exact\":true,\"enchants\":[", out);
    for (int i = 0; i < stack->n_enchants; ++i)
        fprintf(out, "%s[%d,%d]", i ? "," : "",
                stack->enchants[i].id, stack->enchants[i].level);
    fputc(']', out);
    write_stack_payload(out, runtime, stack);
    fputc('}', out);
}

static int read_capsule_bytes(
        const char *name, size_t min_len, size_t max_len,
        uint8_t **data_out, size_t *len_out) {
    const char *root = getenv("MAGMA_CAPSULE_DIR");
    char path[PATH_MAX];
    FILE *stream;
    long length;
    uint8_t *data;
    size_t name_len;
    if (!root || !*root || !name || !data_out || !len_out)
        return 0;
    name_len = strlen(name);
    if (name_len == 0 || name_len > 64 || name[0] == '.'
            || strstr(name, ".."))
        return 0;
    for (size_t i = 0; i < name_len; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_'
                && name[i] != '-' && name[i] != '.')
            return 0;
    if (snprintf(path, sizeof path, "%s/%s", root, name)
            >= (int)sizeof path)
        return 0;
    stream = fopen(path, "rb");
    if (!stream) return 0;
    if (fseek(stream, 0, SEEK_END) != 0
            || (length = ftell(stream)) < 0
            || (unsigned long)length < min_len
            || (unsigned long)length > max_len
            || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return 0;
    }
    data = (uint8_t *)malloc((size_t)length);
    if (!data) {
        fclose(stream);
        return 0;
    }
    if (fread(data, 1, (size_t)length, stream) != (size_t)length
            || fclose(stream) != 0) {
        free(data);
        return 0;
    }
    *data_out = data;
    *len_out = (size_t)length;
    return 1;
}

static int read_capsule_nbt(
        const char *name, uint8_t **data_out, size_t *len_out) {
    return read_capsule_bytes(
        name, 4, GM_NBT_BLOB_MAX, data_out, len_out);
}

static int named_payload_path(
        const char *root, const char *name, char path[PATH_MAX]) {
    size_t name_len;
    if (!root || !*root || !name || !path) return 0;
    name_len = strlen(name);
    if (name_len == 0 || name_len > 64 || name[0] == '.' || strstr(name, ".."))
        return 0;
    for (size_t i = 0; i < name_len; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_'
                && name[i] != '-' && name[i] != '.')
            return 0;
    if (snprintf(path, PATH_MAX, "%s/%s", root, name) >= PATH_MAX)
        return 0;
    return 1;
}

static int capsule_payload_path(
        const char *name, char path[PATH_MAX]) {
    return named_payload_path(getenv("MAGMA_CAPSULE_DIR"), name, path);
}

static int native_save_payload_path(
        const char *name, char path[PATH_MAX]) {
    return named_payload_path(getenv("MAGMA_NATIVE_SAVE_DIR"), name, path);
}

static FILE *open_capsule_payload(const char *name) {
    char path[PATH_MAX];
    if (!capsule_payload_path(name, path)) return NULL;
    return fopen(path, "rb");
}

static int read_u32le(FILE *stream, uint32_t *value) {
    unsigned char raw[4];
    if (fread(raw, 1, sizeof raw, stream) != sizeof raw) return 0;
    *value = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8)
        | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    return 1;
}

static int read_i32le(FILE *stream, int32_t *value) {
    uint32_t raw;
    if (!read_u32le(stream, &raw)) return 0;
    *value = (int32_t)raw;
    return 1;
}

#define SNAPSHOT_CHUNK_CELLS (16u * 16u * 256u)
#define SNAPSHOT_CHUNK_BLOCK_BYTES (SNAPSHOT_CHUNK_CELLS * 2u)
#define SNAPSHOT_CHUNK_LIGHT_BYTES SNAPSHOT_CHUNK_CELLS
#define SNAPSHOT_CHUNK_RECORD_BYTES \
    (20u + SNAPSHOT_CHUNK_BLOCK_BYTES + 2u * SNAPSHOT_CHUNK_LIGHT_BYTES)

static int load_snapshot_chunk_bundle(
        GmRuntime *runtime, const char *name, int expected_dimension,
        int expected_cx, int expected_cz, int expected_radius) {
    static const unsigned char magic[8] = {
        'N', 'T', 'H', 'C', 'H', 'N', '0', '1'
    };
    FILE *stream = NULL;
    unsigned char actual_magic[8];
    unsigned char *blocks = NULL, *sky = NULL, *block_light = NULL;
    int32_t dimension, center_cx, center_cz;
    uint32_t version, count, ticking_count, loaded_count, pending_count;
    long records_offset;
    int ok = 0;
    if (!runtime || expected_dimension < -1 || expected_dimension > 1
            || expected_radius < 0 || expected_radius > 32)
        return 0;
    stream = open_capsule_payload(name);
    if (!stream || fread(actual_magic, 1, 8, stream) != 8
            || memcmp(actual_magic, magic, 8) != 0
            || !read_u32le(stream, &version)
            || !read_i32le(stream, &dimension)
            || !read_u32le(stream, &count)
            || !read_i32le(stream, &center_cx)
            || !read_i32le(stream, &center_cz)
            || !read_u32le(stream, &ticking_count)
            || !read_u32le(stream, &loaded_count)
            || !read_u32le(stream, &pending_count)
            || version != 4 || dimension != expected_dimension
            || center_cx != expected_cx || center_cz != expected_cz
            || count == 0 || count > 4225u || ticking_count > count
            || loaded_count == 0 || loaded_count > 16384u
            || pending_count > 16384u
            || !gm_runtime_ticking_chunks_begin(
                runtime, (int)ticking_count)
            || !gm_runtime_loaded_chunks_begin(
                runtime, (int)loaded_count))
        goto done;
    for (uint32_t index = 0; index < loaded_count; ++index) {
        int32_t cx, cz;
        if (!read_i32le(stream, &cx) || !read_i32le(stream, &cz)
                || !gm_runtime_loaded_chunk_set(
                    runtime, (int)index, cx, cz))
            goto done;
    }
    if (!gm_runtime_loaded_chunks_finalize(runtime))
        goto done;
    if (!gm_runtime_pending_chunk_unloads_begin(
            runtime, (int)pending_count))
        goto done;
    for (uint32_t index = 0; index < pending_count; ++index) {
        int32_t cx, cz;
        uint32_t flags;
        if (!read_i32le(stream, &cx) || !read_i32le(stream, &cz)
                || !read_u32le(stream, &flags) || flags > 3u
                || !gm_runtime_pending_chunk_unload_set(
                    runtime, (int)index, cx, cz, flags == 3u))
            goto done;
    }
    if (!gm_runtime_pending_chunk_unloads_finalize(runtime))
        goto done;
    records_offset = ftell(stream);
    if (records_offset < 0
            || !gm_runtime_snapshot_region_dim(
                runtime, dimension, center_cx, center_cz, expected_radius))
        goto done;
    blocks = (unsigned char *)malloc(SNAPSHOT_CHUNK_BLOCK_BYTES);
    sky = (unsigned char *)malloc(SNAPSHOT_CHUNK_LIGHT_BYTES);
    block_light = (unsigned char *)malloc(SNAPSHOT_CHUNK_LIGHT_BYTES);
    if (!blocks || !sky || !block_light) goto done;
    for (uint32_t chunk = 0; chunk < count; ++chunk) {
        int32_t cx, cz, tick_order;
        uint32_t flags, random_tick_mask;
        if (!read_i32le(stream, &cx) || !read_i32le(stream, &cz)
                || !read_u32le(stream, &flags)
                || !read_i32le(stream, &tick_order)
                || !read_u32le(stream, &random_tick_mask)
                || flags > 7u
                || tick_order < -1
                || tick_order >= (int32_t)ticking_count
                || random_tick_mask > 0xFFFFu
                || (tick_order < 0 && random_tick_mask != 0)
                || cx < -134217728 || cx > 134217727
                || cz < -134217728 || cz > 134217727
                || llabs((long long)cx - center_cx) > expected_radius
                || llabs((long long)cz - center_cz) > expected_radius
                || fread(blocks, 1, SNAPSHOT_CHUNK_BLOCK_BYTES, stream)
                    != SNAPSHOT_CHUNK_BLOCK_BYTES
                || fseek(stream, 2L * SNAPSHOT_CHUNK_LIGHT_BYTES, SEEK_CUR) != 0
                || (tick_order >= 0 && !gm_runtime_ticking_chunk_set(
                    runtime, (int)tick_order, cx, cz,
                    random_tick_mask)))
            goto done;
        for (int y = 0; y < 256; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    size_t index = (size_t)(y << 8 | z << 4 | x);
                    uint16_t state = (uint16_t)blocks[index * 2]
                        | (uint16_t)((uint16_t)blocks[index * 2 + 1] << 8);
                    if ((state >> 4) > 4095
                            || !gm_runtime_load_raw_block_dim(
                                runtime, dimension, cx * 16 + x, y,
                                cz * 16 + z, state >> 4, state & 15))
                        goto done;
                }
    }
    if (!gm_runtime_ticking_chunks_finalize(runtime)
            || !gm_runtime_finalize_block_snapshot_dim(
            runtime, dimension, center_cx, center_cz, expected_radius)
            || fseek(stream, records_offset, SEEK_SET) != 0)
        goto done;
    for (uint32_t chunk = 0; chunk < count; ++chunk) {
        int32_t cx, cz, tick_order;
        uint32_t flags, random_tick_mask;
        if (!read_i32le(stream, &cx) || !read_i32le(stream, &cz)
                || !read_u32le(stream, &flags)
                || !read_i32le(stream, &tick_order)
                || !read_u32le(stream, &random_tick_mask)
                || fseek(stream, SNAPSHOT_CHUNK_BLOCK_BYTES, SEEK_CUR) != 0
                || fread(sky, 1, SNAPSHOT_CHUNK_LIGHT_BYTES, stream)
                    != SNAPSHOT_CHUNK_LIGHT_BYTES
                || fread(block_light, 1, SNAPSHOT_CHUNK_LIGHT_BYTES, stream)
                    != SNAPSHOT_CHUNK_LIGHT_BYTES)
            goto done;
        for (int y = 0; y < 256; ++y)
            for (int z = 0; z < 16; ++z)
                for (int x = 0; x < 16; ++x) {
                    size_t index = (size_t)(y << 8 | z << 4 | x);
                    if (sky[index] > 15 || block_light[index] > 15
                            || !gm_runtime_load_sky_light_dim(
                                runtime, dimension, cx * 16 + x, y,
                                cz * 16 + z, sky[index])
                            || !gm_runtime_load_block_light_dim(
                                runtime, dimension, cx * 16 + x, y,
                                cz * 16 + z, block_light[index]))
                        goto done;
                }
    }
    if (!gm_runtime_finalize_sky_light_snapshot_dim(runtime, dimension)
            || !gm_runtime_finalize_block_light_snapshot_dim(runtime, dimension))
        goto done;
    if (fgetc(stream) != EOF) goto done;
    ok = 1;
done:
    free(block_light);
    free(sky);
    free(blocks);
    if (stream) fclose(stream);
    return ok;
}

static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }

static int parse_string(const char **pp, char *out, int cap) {
    const char *p = *pp; int n = 0;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c < 32 || n + 1 >= cap) return 0;
        if (c == '\\') {
            c = (unsigned char)*p++;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            default: return 0;
            }
        }
        out[n++] = (char)c;
    }
    if (*p++ != '"') return 0;
    out[n] = 0; *pp = p; return 1;
}

static int parse_object(const char *line, JlObject *o, char *err, int cap) {
    const char *p = skip_ws(line); o->n = 0;
    if (*p++ != '{') { snprintf(err, cap, "expected JSON object"); return 0; }
    p = skip_ws(p);
    if (*p == '}') p++;
    else for (;;) {
        if (o->n >= JL_FIELDS) { snprintf(err, cap, "too many fields"); return 0; }
        JlField *f = &o->f[o->n++];
        if (!parse_string(&p, f->key, sizeof f->key)) { snprintf(err, cap, "invalid key"); return 0; }
        p = skip_ws(p);
        if (*p++ != ':') { snprintf(err, cap, "expected colon"); return 0; }
        p = skip_ws(p); f->string = *p == '"';
        if (f->string) {
            if (!parse_string(&p, f->value, sizeof f->value)) { snprintf(err, cap, "invalid string"); return 0; }
        } else {
            int n = 0;
            while (*p && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
                if (n + 1 >= (int)sizeof f->value) { snprintf(err, cap, "value too long"); return 0; }
                f->value[n++] = *p++;
            }
            f->value[n] = 0;
            if (!n) { snprintf(err, cap, "missing value"); return 0; }
        }
        p = skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p++ != ',') { snprintf(err, cap, "expected comma"); return 0; }
        p = skip_ws(p);
    }
    p = skip_ws(p);
    if (*p) { snprintf(err, cap, "trailing JSON data"); return 0; }
    for (int i = 0; i < o->n; ++i)
        for (int j = i + 1; j < o->n; ++j)
            if (!strcmp(o->f[i].key, o->f[j].key)) {
                snprintf(err, cap, "duplicate field: %s", o->f[i].key); return 0;
            }
    return 1;
}

static const JlField *field(const JlObject *o, const char *key) {
    for (int i = 0; i < o->n; ++i) if (!strcmp(o->f[i].key, key)) return &o->f[i];
    return NULL;
}

/* Cold trace-harness export. Packed order matches qrl getblocks and the state
 * capsule: y, then z, then x, little-endian (block_id << 4 | meta). */
static int write_blocks_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_BLOCKS_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path && !box) return 1;
    if (!path || !box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "blocks-out: invalid MAGMA_BLOCKS_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("blocks-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned value =
                    (unsigned)(gm_world_block(r->world, x, y, z) << 4)
                    | (unsigned)gm_world_meta(r->world, x, y, z);
                unsigned char packed[2] = {
                    (unsigned char)(value & 255u),
                    (unsigned char)((value >> 8) & 255u),
                };
                if (fwrite(packed, 1, sizeof packed, stream) != sizeof packed) {
                    fprintf(stderr, "blocks-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("blocks-out");
        return 0;
    }
    return 1;
}

/* Cold parity-harness export. One raw block-light nibble per cell, promoted
 * to a byte, in the same y/z/x order as write_blocks_out. */
static int write_block_light_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_BLOCK_LIGHT_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "block-light-out: invalid MAGMA_BLOCK_LIGHT_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("block-light-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned char value =
                    (unsigned char)gm_world_block_light(r->world, x, y, z);
                if (fwrite(&value, 1, 1, stream) != 1) {
                    fprintf(stderr, "block-light-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("block-light-out");
        return 0;
    }
    return 1;
}

/* Cold parity-harness skylight export, with the same one-byte y/z/x wire
 * format as write_block_light_out. */
static int write_sky_light_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_SKY_LIGHT_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "sky-light-out: invalid MAGMA_SKY_LIGHT_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("sky-light-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                unsigned char value =
                    (unsigned char)gm_world_sky_light(r->world, x, y, z);
                if (fwrite(&value, 1, 1, stream) != 1) {
                    fprintf(stderr, "sky-light-out: short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("sky-light-out");
        return 0;
    }
    return 1;
}

/* Cold parity-harness biome export. The biome column value is repeated for
 * each y in the requested y/z/x cuboid so every raw world surface shares one
 * coordinate traversal and first-difference index. */
static int write_biomes_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_BIOMES_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "biomes-out: invalid MAGMA_BIOMES_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("biomes-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                int biome = gm_world_biome(r->world, x, z);
                unsigned char value = (unsigned char)biome;
                if (biome < 0 || biome > 255
                        || fwrite(&value, 1, 1, stream) != 1) {
                    fprintf(stderr, "biomes-out: invalid value or short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("biomes-out");
        return 0;
    }
    return 1;
}

/* Exact Chunk.heightMap export as little-endian u16, repeated across y like
 * the biome export so it shares the cuboid comparator coordinate index. */
static int write_heights_out(const GmRuntime *r) {
    const char *path = getenv("MAGMA_HEIGHTS_OUT");
    const char *box = getenv("MAGMA_BLOCKS_BOX");
    int x0, y0, z0, x1, y1, z1;
    if (!path) return 1;
    if (!box
            || sscanf(box, "%d,%d,%d,%d,%d,%d",
                      &x0, &y0, &z0, &x1, &y1, &z1) != 6
            || x1 < x0 || y0 < 0 || y1 < y0 || y1 > 255 || z1 < z0) {
        fprintf(stderr, "heights-out: invalid MAGMA_HEIGHTS_OUT/BOX\n");
        return 0;
    }
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("heights-out");
        return 0;
    }
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                int height = gm_world_height(r->world, x, z);
                unsigned char raw[2] = {
                    (unsigned char)height, (unsigned char)(height >> 8),
                };
                if (height < 0 || height > 256
                        || fwrite(raw, 1, sizeof raw, stream) != sizeof raw) {
                    fprintf(stderr, "heights-out: invalid value or short write\n");
                    fclose(stream);
                    return 0;
                }
            }
    if (fclose(stream) != 0) {
        perror("heights-out");
        return 0;
    }
    return 1;
}

static int keys_only(const JlObject *o, const char *const *keys, int n,
                     char *err, int cap) {
    for (int i = 0; i < o->n; ++i) {
        int ok = 0;
        for (int j = 0; j < n; ++j) if (!strcmp(o->f[i].key,keys[j])) { ok=1; break; }
        if (!ok) { snprintf(err,cap,"unknown or forbidden field: %s",o->f[i].key); return 0; }
    }
    return 1;
}

static int as_i64(const JlField *f, long long *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    long long x = strtoll(f->value, &e, 10);
    if (errno || e == f->value || *e) return 0;
    *v = x; return 1;
}
static int as_double(const JlField *f, double *v) {
    char *e = NULL; errno = 0;
    if (!f || f->string || !f->value[0]) return 0;
    double x = strtod(f->value, &e);
    if (errno || e == f->value || *e || !isfinite(x)) return 0;
    *v = x; return 1;
}
static int as_string(const JlField *f, const char **v) {
    if (!f || !f->string) return 0;
    *v = f->value;
    return 1;
}

static int as_rule_bool(const JlField *f, int *v) {
    if (!f) return 1;
    if (!strcmp(f->value,"true")) { *v=1; return 1; }
    if (!strcmp(f->value,"false")) { *v=0; return 1; }
    return 0;
}

static int known_action_key(const char *k) {
    static const char *keys[] = {"tick","type","forward","strafe","dyaw","dpitch",
        "jump","sneak","sprint","attack","use","do_break","do_place","hotbar",
        "close_container","inv_slot","inv_button","inv_type","death_click",
        "death_button","server_only"};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) if (!strcmp(k, keys[i])) return 1;
    return 0;
}

static int parse_action(const JlObject *o, GmAction *a, char *err, int cap) {
    memset(a, 0, sizeof *a); a->hotbar_sel = -1;
    for (int i = 0; i < o->n; ++i)
        if (!known_action_key(o->f[i].key)) {
            snprintf(err, cap, "unknown or forbidden action field: %s", o->f[i].key); return 0;
        }
    double d; long long n;
#define NUM(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_double(q,&d)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(float)d;} }while(0)
#define INT(K, DST) do { const JlField *q=field(o,K); if(q){if(!as_i64(q,&n)){snprintf(err,cap,"invalid %s",K);return 0;} DST=(int)n;} }while(0)
    NUM("forward", a->forward); NUM("strafe", a->strafe); NUM("dyaw", a->dyaw); NUM("dpitch", a->dpitch);
    INT("jump", a->jump); INT("sneak", a->sneak); INT("sprint", a->sprint);
    INT("attack", a->attack); INT("use", a->use); INT("do_break", a->do_break);
    INT("do_place", a->do_place);
    INT("close_container", a->close_container);
    INT("hotbar", a->hotbar_sel);
    INT("death_click", a->death_click);
    INT("death_button", a->death_button);
    INT("server_only", a->server_only);
#undef NUM
#undef INT
    /* Container.slotClick as a SURVIVAL action: inv_slot present -> one click of
     * (inv_slot, inv_button, inv_type) through gm_container_click this tick. */
    { const JlField *sf = field(o, "inv_slot");
      if (sf) {
          long long slot, button = 0, ctype = 0;
          if (!as_i64(sf, &slot) ||
              !(slot == GMC_OUTSIDE || (slot >= 0 && slot < GMC_SLOT_COUNT))) {
              snprintf(err, cap, "invalid inv_slot"); return 0;
          }
          const JlField *bf = field(o, "inv_button");
          const JlField *tf = field(o, "inv_type");
          if ((bf && (!as_i64(bf, &button) || button < 0 || button > 10)) ||
              (tf && (!as_i64(tf, &ctype) || ctype < 0 || ctype > 6))) {
              snprintf(err, cap, "invalid inv_button/inv_type"); return 0;
          }
          if ((ctype == CC_CLICK_SWAP && button > 8)
                  || (ctype == CC_CLICK_CLONE && button > 2)
                  || (ctype == CC_CLICK_QUICK_CRAFT
                      && ((button & 3) == 3))
                  || ((ctype == CC_CLICK_PICKUP
                       || ctype == CC_CLICK_QUICK_MOVE
                       || ctype == CC_CLICK_THROW
                       || ctype == CC_CLICK_PICKUP_ALL)
                      && button > 1)) {
              snprintf(err, cap, "invalid inv_button/inv_type"); return 0;
          }
          a->inv_click = 1; a->inv_slot = (int)slot;
          a->inv_button = (int)button; a->inv_type = (int)ctype;
      } else if (field(o, "inv_button") || field(o, "inv_type")) {
          snprintf(err, cap, "inv_button/inv_type require inv_slot"); return 0;
      } }
    if (a->forward < -1 || a->forward > 1 || a->strafe < -1 || a->strafe > 1 ||
        a->hotbar_sel < -1 || a->hotbar_sel > 8
        || (a->death_click != 0 && a->death_click != 1)
        || (a->death_button != 0 && a->death_button != 1)
        || (a->server_only != 0 && a->server_only != 1)) {
        snprintf(err, cap, "action value out of range"); return 0;
    }
    return 1;
}

static int parse_craft(const JlObject *o, int *width, int slots[9], char *err, int cap) {
    long long n;
    const JlField *wf = field(o, "width");
    if (!as_i64(wf, &n) || (n != 2 && n != 3)) { snprintf(err,cap,"craft width must be 2 or 3"); return 0; }
    *width = (int)n;
    for (int i = 0; i < 9; ++i) slots[i] = -1;
    for (int i = 0; i < o->n; ++i) {
        const char *k = o->f[i].key;
        if (!strcmp(k,"tick") || !strcmp(k,"type") || !strcmp(k,"width")) continue;
        if (strncmp(k,"grid",4) || strlen(k) != 5 || k[4] < '0' || k[4] > '8') {
            snprintf(err,cap,"unknown or forbidden craft field: %s",k); return 0;
        }
        if (!as_i64(&o->f[i],&n) || n < -1 || n >= ISR_MAIN_SLOTS) {
            snprintf(err,cap,"invalid %s inventory slot",k); return 0;
        }
        slots[k[4]-'0'] = (int)n;
    }
    return 1;
}

/* FNV-1a over the 9x9x9 id/meta volume around the player. Anchored at the
 * double-precision sim feet position (not the float render view): the Java
 * recorder computes the identical digest from floor(posX/Y/Z), and a float
 * round-trip can flip floor() at block boundaries. Java mirror:
 * Recorder.recordTick "wfnv". Iteration order and value packing must stay
 * bit-equal on both sides.
 * The basis below is NOT standard FNV-1a (last digit of ...6037 dropped,
 * historic); it only has to keep matching the Java mirror. */
static unsigned long long nearby_hash(const GmRuntime *r, int anchor[3]) {
    unsigned long long h = 1469598103934665603ULL;
    int cx = (int)floor(r->player.ent.posX + (double)r->ox);
    int cy = (int)floor(r->player.ent.posY);
    int cz = (int)floor(r->player.ent.posZ + (double)r->oz);
    anchor[0] = cx; anchor[1] = cy; anchor[2] = cz;
    for (int z = cz - 4; z <= cz + 4; ++z)
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                unsigned s = (unsigned)(gm_world_block(r->world,x,y,z) << 4 |
                                        gm_world_meta(r->world,x,y,z));
                h ^= s; h *= 1099511628211ULL;
            }
    return h;
}

static int nearby_blocks_every(void) {
    static int initialized;
    static int every;
    if (!initialized) {
        const char *value = getenv("MAGMA_STATE_NEARBY_BLOCKS_EVERY");
        long parsed = value && *value ? strtol(value, NULL, 10) : 0;
        every = parsed > 0 && parsed <= INT_MAX ? (int)parsed : 0;
        initialized = 1;
    }
    return every;
}

static long long nearby_blocks_offset(void) {
    static int initialized;
    static long long offset;
    if (!initialized) {
        const char *value = getenv("MAGMA_STATE_NEARBY_BLOCKS_OFFSET");
        offset = value && *value ? strtoll(value, NULL, 10) : 0;
        initialized = 1;
    }
    return offset;
}

static void write_nearby_blocks(
        FILE *out, const GmRuntime *r, const GmPlayerView *v) {
    int cx = (int)floor(v->x), cy = (int)floor(v->y), cz = (int)floor(v->z);
    int first = 1;
    fputs(",\"nearby_blocks\":[", out);
    for (int z = cz - 4; z <= cz + 4; ++z)
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                unsigned state = (unsigned)(gm_world_block(r->world, x, y, z) << 4 |
                                            gm_world_meta(r->world, x, y, z));
                fprintf(out, "%s%u", first ? "" : ",", state);
                first = 0;
            }
    fputc(']', out);
}

static void write_state(FILE *out, const GmRuntime *r) {
    GmPlayerView v; gm_runtime_view(r, &v);
    int spawn_x = r->world_spawn_x;
    int spawn_y = r->world_spawn_y;
    int spawn_z = r->world_spawn_z;
    double border_min_x = r->border_center_x - r->border_diameter * 0.5;
    double border_max_x = r->border_center_x + r->border_diameter * 0.5;
    double border_min_z = r->border_center_z - r->border_diameter * 0.5;
    double border_max_z = r->border_center_z + r->border_diameter * 0.5;
    if (!((double)(spawn_x + 1) > border_min_x
            && (double)spawn_x < border_max_x
            && (double)(spawn_z + 1) > border_min_z
            && (double)spawn_z < border_max_z)) {
        int height;
        spawn_x = (int)floor(r->border_center_x);
        spawn_z = (int)floor(r->border_center_z);
        height = gm_world_height(r->world, spawn_x, spawn_z);
        if (height >= 0) spawn_y = height;
    }
    fprintf(out, "{\"version\":1,\"tick\":%lld,\"dim\":%d,\"world_time\":%lld,"
                 "\"total_time\":%lld,\"do_entity_drops\":%s,"
                 "\"do_mob_spawning\":%s,"
                 "\"do_mob_loot\":%s,"
                 "\"random_tick_speed\":%d,"
                 "\"world_spawn_x\":%d,\"world_spawn_y\":%d,"
                 "\"world_spawn_z\":%d,"
                 "\"default_game_mode\":%d,"
                 "\"difficulty\":%d,"
                 "\"border_center_x\":%.17g,\"border_center_z\":%.17g,"
                 "\"border_diameter\":%.17g,"
                 "\"border_target_diameter\":%.17g,"
                 "\"border_time_until_target\":%lld,"
                 "\"border_damage_amount\":%.17g,"
                 "\"border_damage_buffer\":%.17g,"
                 "\"border_warning_time\":%d,"
                 "\"border_warning_distance\":%d,"
                 "\"entity_id_cursor\":%d,"
                 "\"player_entity_id\":%d,"
                 "\"player_riding_eid\":%d,"
                 "\"world_rand_seed48\":%llu,\"math_rand_seed48\":%llu,"
                 "\"collections_rand_seed48\":%llu,"
                 "\"server_uuid_seed48\":%llu,"
                 "\"entity_seed_generator_seed48\":%llu,"
                 "\"block_rand_seed48\":%llu,"
                 "\"inventory_helper_rand_seed48\":%llu,"
                 "\"inventory_helper_rand_have_gaussian\":%s,"
                 "\"inventory_helper_rand_gaussian\":%.17g,"
                 "\"dispenser_rand_seed48\":%llu,"
                 "\"world_rand_have_gaussian\":%s,"
                 "\"world_rand_gaussian\":%.17g,"
                 "\"world_update_lcg\":%d,"
                 "\"dragon_fight_present\":%d,"
                 "\"dragon_respawn_state\":%d,"
                 "\"dragon_respawn_ticks\":%d,",
            r->tick,r->dimension,(long long)r->clock.world_time,
            (long long)r->clock.total_time,
            r->do_entity_drops ? "true" : "false",
            r->gamerules.doMobSpawning ? "true" : "false",
            r->do_mob_loot ? "true" : "false",
            r->gamerules.randomTickSpeed,
            spawn_x, spawn_y, spawn_z,
            r->default_game_mode,
            r->difficulty,
            r->border_center_x, r->border_center_z,
            r->border_diameter, r->border_target_diameter,
            r->border_time_until_target,
            r->border_damage_amount, r->border_damage_buffer,
            r->border_warning_time, r->border_warning_distance,
            r->next_entity_id,
            r->player_entity_id,
            r->minecart_ride_eid,
            (unsigned long long)r->world_random_seed48,
            (unsigned long long)r->math_random_seed48,
            (unsigned long long)r->collections_random_seed48,
            (unsigned long long)r->server_uuid_random_seed48,
            (unsigned long long)r->entity_seed_generator_seed48,
            (unsigned long long)r->block_random_seed48,
            (unsigned long long)r->inventory_helper_random_seed48,
            r->inventory_helper_random_have_gaussian ? "true" : "false",
            r->inventory_helper_random_gaussian,
            (unsigned long long)r->dispenser_random_seed48,
            r->world_random_have_gaussian ? "true" : "false",
            r->world_random_gaussian,
            (int)r->world_update_lcg,
            r->dimension == 1,
            r->dragon_respawn_state == GM_DRAGON_RESPAWN_NONE
                ? -1 : r->dragon_respawn_state - 1,
            r->dragon_respawn_ticks);
    if (r->controlled_input_valid
            && r->controlled_input_tick == r->tick) {
        fprintf(
            out,
            "\"controlled_input\":{\"before\":{"
            "\"world_rand_seed48\":%llu,\"math_rand_seed48\":%llu,"
            "\"block_rand_seed48\":%llu,"
            "\"inventory_helper_rand_seed48\":%llu,"
            "\"inventory_helper_rand_have_gaussian\":%s,"
            "\"inventory_helper_rand_gaussian\":%.17g,"
            "\"world_update_lcg\":%d,"
            "\"next_entity_id\":%d},\"world_rand_seed48\":%llu,"
            "\"math_rand_seed48\":%llu,\"block_rand_seed48\":%llu,"
            "\"inventory_helper_rand_seed48\":%llu,"
            "\"inventory_helper_rand_have_gaussian\":%s,"
            "\"inventory_helper_rand_gaussian\":%.17g,"
            "\"world_update_lcg\":%d,\"next_entity_id\":%d},",
            (unsigned long long)r->controlled_input_before_world_seed48,
            (unsigned long long)r->controlled_input_before_math_seed48,
            (unsigned long long)r->controlled_input_before_block_seed48,
            (unsigned long long)
                r->controlled_input_before_inventory_helper_seed48,
            r->controlled_input_before_inventory_helper_have_gaussian
                ? "true" : "false",
            r->controlled_input_before_inventory_helper_gaussian,
            (int)r->controlled_input_before_update_lcg,
            r->controlled_input_before_entity_id,
            (unsigned long long)r->controlled_input_world_seed48,
            (unsigned long long)r->controlled_input_math_seed48,
            (unsigned long long)r->controlled_input_block_seed48,
            (unsigned long long)
                r->controlled_input_inventory_helper_seed48,
            r->controlled_input_inventory_helper_have_gaussian
                ? "true" : "false",
            r->controlled_input_inventory_helper_gaussian,
            (int)r->controlled_input_update_lcg,
            r->controlled_input_entity_id);
    } else {
        fprintf(out, "\"controlled_input\":null,");
    }
    fprintf(out, "\"weather\":{\"raining\":%d,\"thundering\":%d,"
                 "\"rain_time\":%d,\"thunder_time\":%d,"
                 "\"clean_weather_time\":%d,\"weather_cycle\":%d,"
                 "\"daylight_cycle\":%d,\"prev_rain_strength\":%.9g,"
                 "\"rain_strength\":%.9g,\"prev_thunder_strength\":%.9g,"
                 "\"thunder_strength\":%.9g},",
            r->clock.raining,r->clock.thundering,
            r->clock.rain_time,r->clock.thunder_time,
            r->clock.clean_weather_time,r->clock.weather_cycle,
            !r->clock.freeze_daylight,
            (double)r->clock.prev_rain_strength,
            (double)r->clock.rain_strength,
            (double)r->clock.prev_thunder_strength,
            (double)r->clock.thunder_strength);
    fprintf(out, "\"weather_effects\":[");
    {
        int first_weather = 1;
        for (int i = 0; i < r->lightning_cap; ++i) {
            const GmRuntimeLightning *bolt = &r->lightning[i];
            if (!bolt->active || bolt->dimension != r->dimension) continue;
            fprintf(out,
                "%s{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"ticks_existed\":%d,\"lightning_state\":%d,"
                "\"living_time\":%d,\"effect_only\":%s,"
                "\"bolt_vertex\":%lld,\"entity_seed48\":%llu}",
                first_weather ? "" : ",", bolt->eid,
                bolt->x, bolt->y, bolt->z, bolt->ticks_existed,
                bolt->lightning_state, bolt->living_time,
                bolt->effect_only ? "true" : "false",
                bolt->bolt_vertex,
                (unsigned long long)bolt->random_seed48);
            first_weather = 0;
        }
    }
    fprintf(out, "],");
    fprintf(out, "\"scheduled_ticks\":[");
    for (int i = 0; i < gm_runtime_scheduled_tick_count(r); ++i) {
        GmRuntimeScheduledTick entry;
        if (!gm_runtime_scheduled_tick_get(r, i, &entry)) continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"block\":%d,"
            "\"time\":%lld,\"priority\":%d,\"order\":%lld}",
            entry.x, entry.y, entry.z, entry.block, entry.time,
            /* Java's authoritative surface exposes TreeSet rank, not the
             * private tickEntryID. Internal order still controls dispatch;
             * rank is the save/reload-stable representation. */
            entry.priority, (long long)i);
    }
    fprintf(out, "],");
    fprintf(out, "\"moving_pistons\":[");
    int piston_written = 0;
    for (int i = 0; i < gm_runtime_moving_piston_count(r); ++i) {
        GmRuntimePiston piston;
        union { float f; unsigned u; } progress,last_progress;
        if (!gm_runtime_moving_piston_get(r, i, &piston)
                || piston.dimension != r->dimension)
            continue;
        if (piston_written++) fputc(',', out);
        progress.f = piston.progress;
        last_progress.f = piston.last_progress;
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"moved_block\":%d,\"moved_meta\":%d,\"facing\":%d,"
            "\"extending\":%s,\"source\":%s,"
            "\"progress_bits\":%u,\"last_progress_bits\":%u}",
            piston.x, piston.y, piston.z,
            piston.moved_block, piston.moved_meta, piston.facing,
            piston.extending ? "true" : "false",
            piston.source ? "true" : "false",
            progress.u, last_progress.u);
    }
    fprintf(out, "],");
    fprintf(out, "\"comparators\":[");
    int comparator_written = 0;
    for (int i = 0; i < gm_runtime_comparator_count(r); ++i) {
        GmRuntimeComparator comparator;
        if (!gm_runtime_comparator_get(r, i, &comparator)
                || comparator.dimension != r->dimension)
            continue;
        if (comparator_written++) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"output_signal\":%d}",
            comparator.x, comparator.y, comparator.z,
            comparator.output_signal);
    }
    fprintf(out, "],");
    fprintf(out, "\"containers\":[");
    int container_written = 0;
    {
        int total = gm_runtime_chest_count(r);
        int have_previous = 0;
        int previous_x = 0, previous_y = 0, previous_z = 0;
        for (int written = 0; written < total; ++written) {
            GmRuntimeChest selected;
            int found = 0;
            for (int i = 0; i < total; ++i) {
                GmRuntimeChest candidate;
                if (!gm_runtime_chest_get(r, i, &candidate))
                    continue;
                if (have_previous
                        && (candidate.wx < previous_x
                            || (candidate.wx == previous_x
                                && candidate.wy < previous_y)
                            || (candidate.wx == previous_x
                                && candidate.wy == previous_y
                                && candidate.wz <= previous_z)))
                    continue;
                if (!found
                        || candidate.wx < selected.wx
                        || (candidate.wx == selected.wx
                            && candidate.wy < selected.wy)
                        || (candidate.wx == selected.wx
                            && candidate.wy == selected.wy
                            && candidate.wz < selected.wz)) {
                    selected = candidate;
                    found = 1;
                }
            }
            if (!found) break;
            if (container_written++) fputc(',', out);
            GmRuntimeChest pair;
            int pair_found = 0;
            int selected_block = gm_world_block(
                r->world, selected.wx, selected.wy, selected.wz);
            int loaded_order = gm_runtime_loaded_tile_rank(
                r, selected.wx, selected.wy, selected.wz);
            for (int i = 0; i < total; ++i) {
                GmRuntimeChest candidate;
                if (!gm_runtime_chest_get(r, i, &candidate)
                        || candidate.wy != selected.wy)
                    continue;
                if (gm_world_block(
                        r->world, candidate.wx, candidate.wy,
                        candidate.wz) != selected_block)
                    continue;
                int distance =
                    abs(candidate.wx - selected.wx)
                    + abs(candidate.wz - selected.wz);
                if (distance == 1) {
                    pair = candidate;
                    pair_found = 1;
                    break;
                }
            }
            if (selected_block == 146 && pair_found) {
                fprintf(
                    out,
                    "{\"type\":\"double_trapped_chest_half\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,"
                    "\"pair_x\":%d,\"pair_y\":%d,\"pair_z\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS,
                    pair.wx, pair.wy, pair.wz);
            } else if (selected_block == 146) {
                fprintf(
                    out,
                    "{\"type\":\"single_trapped_chest\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS);
            } else if (pair_found) {
                fprintf(
                    out,
                    "{\"type\":\"double_chest_half\","
                    "\"x\":%d,\"y\":%d,\"z\":%d,\"size\":%d,"
                    "\"pair_x\":%d,\"pair_y\":%d,\"pair_z\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS,
                    pair.wx, pair.wy, pair.wz);
            } else {
                fprintf(
                    out,
                    "{\"type\":\"single_chest\",\"x\":%d,\"y\":%d,"
                    "\"z\":%d,\"size\":%d,",
                    selected.wx, selected.wy, selected.wz,
                    CHEST_LIVE_SLOTS);
            }
            if (loaded_order >= 0)
                fprintf(out, "\"loaded_order\":%d,", loaded_order);
            {
                union { float f; unsigned u; } lid, prev_lid;
                lid.f = selected.state.te.lid_angle;
                prev_lid.f = selected.state.te.prev_lid_angle;
                fprintf(
                    out,
                    "\"num_players_using\":%d,"
                    "\"lid_angle_bits\":%u,"
                    "\"prev_lid_angle_bits\":%u,"
                    "\"ticks_since_sync\":%d,\"items\":[",
                    selected.state.te.num_players_using,
                    lid.u, prev_lid.u,
                    selected.state.te.ticks_since_sync);
            }
            int item_written = 0;
            for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot) {
                ICStack stack = chest_live_get(&selected.state, slot);
                if (stack.item <= 0 || stack.count <= 0)
                    continue;
                if (item_written++) fputc(',', out);
                fprintf(
                    out,
                    "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d",
                    slot, stack.item, stack.count, stack.meta);
                write_stack_payload(out, r, &stack);
                fputc('}', out);
            }
            fprintf(out, "]}");
            previous_x = selected.wx;
            previous_y = selected.wy;
            previous_z = selected.wz;
            have_previous = 1;
        }
    }
    for (int i = 0; i < gm_runtime_furnace_count(r); ++i) {
        GmRuntimeFurnace furnace;
        int loaded_order;
        if (!gm_runtime_furnace_get(r, i, &furnace))
            continue;
        loaded_order = gm_runtime_loaded_tile_rank(
            r, furnace.wx, furnace.wy, furnace.wz);
        if (container_written++) fputc(',', out);
        const char *custom_name = gm_runtime_item_name(
            r, furnace.custom_name);
        fputc('{', out);
        if (loaded_order >= 0)
            fprintf(out, "\"loaded_order\":%d,", loaded_order);
        fprintf(
            out,
            "\"type\":\"furnace\",\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"size\":%d,\"burn_time\":%d,\"current_burn_time\":%d,"
            "\"cook_time\":%d,\"total_cook_time\":%d,\"custom_name\":",
            furnace.wx, furnace.wy, furnace.wz,
            FURNACE_LIVE_SLOT_COUNT,
            furnace.state.burn_time, furnace.state.current_burn_time,
            furnace.state.cook_time, furnace.state.total_cook);
        write_json_string(out, custom_name ? custom_name : "");
        fputs(",\"items\":[", out);
        int item_written = 0;
        for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot) {
            ICStack stack = furnace_live_get_ic(&furnace.state, slot);
            if (isr_is_empty(&stack)) continue;
            if (item_written++) fputc(',', out);
            fprintf(
                out,
                "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d",
                slot, stack.item, stack.count, stack.meta);
            write_stack_payload(out, r, &stack);
            fputc('}', out);
        }
        fprintf(out, "]}");
    }
    for (int i = 0; i < gm_runtime_static_container_count(r); ++i) {
        GmRuntimeStaticContainer container;
        int loaded_order;
        if (!gm_runtime_static_container_get(r, i, &container))
            continue;
        loaded_order = gm_runtime_loaded_tile_rank(
            r, container.wx, container.wy, container.wz);
        if (container_written++) fputc(',', out);
        if (container.block >= 219 && container.block <= 234) {
            union { float f; unsigned u; } progress, progress_old;
            progress.f = container.shulker_progress;
            progress_old.f = container.shulker_progress_old;
            fprintf(
                out,
                "{\"type\":\"shulker_box\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"block\":%d,"
                "\"facing\":%d,\"open_count\":%d,"
                "\"animation_status\":%d,\"progress_bits\":%u,"
                "\"progress_old_bits\":%u,",
                container.wx, container.wy, container.wz, container.size,
                container.block,
                gm_world_meta(
                    r->world, container.wx, container.wy, container.wz),
                container.shulker_open_count,
                container.shulker_animation_status,
                progress.u, progress_old.u);
            if (loaded_order >= 0)
                fprintf(out, "\"loaded_order\":%d,", loaded_order);
            if (container.item_tag.data && container.item_tag.len > 0) {
                fprintf(out, "\"item_tag_nbt\":\"");
                write_hex(
                    out, container.item_tag.data, container.item_tag.len);
                fprintf(out, "\",");
            }
            fprintf(out, "\"items\":[");
        } else if (container.block == 117) {
            fprintf(
                out,
                "{\"type\":\"brewing_stand\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"brew_time\":%d,"
                "\"fuel\":%d,\"ingredient_id\":%d,",
                container.wx, container.wy, container.wz,
                container.size, container.brewing.brew_time,
                container.brewing.fuel,
                container.brewing.ingredient_id);
            if (loaded_order >= 0)
                fprintf(out, "\"loaded_order\":%d,", loaded_order);
            fprintf(out, "\"items\":[");
        } else if (container.block == 154) {
            fprintf(
                out,
                "{\"type\":\"hopper\",\"x\":%d,\"y\":%d,"
                "\"z\":%d,\"size\":%d,\"transfer_cooldown\":%d,"
                "\"ticked_game_time\":%lld,",
                container.wx, container.wy, container.wz,
                container.size, container.transfer_cooldown,
                container.ticked_game_time);
            if (loaded_order >= 0)
                fprintf(out, "\"loaded_order\":%d,", loaded_order);
            fprintf(out, "\"items\":[");
        } else {
            fprintf(
                out,
                "{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"z\":%d,"
                "\"size\":%d,",
                container.block == 23 ? "dispenser"
                    : container.block == 158 ? "dropper"
                    : container.block == 154 ? "hopper" : "jukebox",
                container.wx, container.wy, container.wz,
                container.size);
            if (loaded_order >= 0)
                fprintf(out, "\"loaded_order\":%d,", loaded_order);
            fprintf(out, "\"items\":[");
        }
        int item_written = 0;
        for (int slot = 0; slot < container.size; ++slot) {
            ICStack stack = container.slots[slot];
            if (stack.item <= 0 || stack.count <= 0)
                continue;
            if (item_written++) fputc(',', out);
            fprintf(
                out,
                "{\"slot\":%d,\"id\":%d,\"count\":%d,\"meta\":%d",
                slot, stack.item, stack.count, stack.meta);
            write_stack_payload(out, r, &stack);
            fputc('}', out);
        }
        fprintf(out, "]}");
    }
    for (int i = 0; i < gm_runtime_command_block_count(r); ++i) {
        GmRuntimeCommandBlock command;
        const unsigned char *command_bytes = NULL, *output_bytes = NULL;
        size_t command_length = 0, output_length = 0;
        char command_text[256] = "";
        char output_text[768] = "";
        if (!gm_runtime_command_block_get(r, i, &command))
            continue;
        if (command.command_tag_id
                && gm_runtime_armor_stand_string(
                    r, command.command_tag_id,
                    &command_bytes, &command_length)
                && command_length < sizeof command_text) {
            memcpy(command_text, command_bytes, command_length);
            command_text[command_length] = '\0';
        }
        if (command.last_output_tag_id
                && gm_runtime_armor_stand_string(
                    r, command.last_output_tag_id,
                    &output_bytes, &output_length)
                && output_length < sizeof output_text) {
            memcpy(output_text, output_bytes, output_length);
            output_text[output_length] = '\0';
        }
        if (container_written++) fputc(',', out);
        int loaded_order = gm_runtime_loaded_tile_rank(
            r, command.wx, command.wy, command.wz);
        fprintf(
            out,
            "{\"type\":\"%s\",\"x\":%d,\"y\":%d,\"z\":%d,"
            "\"size\":0,",
            command.block == 210 ? "repeating_command_block"
                : command.block == 211 ? "chain_command_block"
                : "command_block",
            command.wx, command.wy, command.wz);
        if (loaded_order >= 0)
            fprintf(out, "\"loaded_order\":%d,", loaded_order);
        fprintf(
            out, "\"success_count\":%d,\"command\":",
            command.success_count);
        write_json_string(out, command_text);
        fprintf(out, ",\"last_output\":");
        write_json_string(out, output_text);
        fprintf(out,
            ",\"powered\":%s,\"automatic\":%s,"
            "\"condition_met\":%s,\"items\":[]}",
            command.powered ? "true" : "false",
            command.automatic ? "true" : "false",
            command.condition_met ? "true" : "false");
    }
    fprintf(out, "],");
    fprintf(out, "\"flower_pots\":[");
    for (int i = 0; i < gm_runtime_flower_pot_count(r); ++i) {
        GmRuntimeFlowerPot pot;
        if (!gm_runtime_flower_pot_get(r, i, &pot))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"item\":%d,\"meta\":%d}",
            pot.wx, pot.wy, pot.wz, pot.item, pot.meta);
    }
    fprintf(out, "],");
    fprintf(out, "\"note_blocks\":[");
    for (int i = 0; i < gm_runtime_note_block_count(r); ++i) {
        GmRuntimeNoteBlock note;
        if (!gm_runtime_note_block_get(r, i, &note))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"note\":%d,"
            "\"powered\":%s}",
            note.wx, note.wy, note.wz, note.note,
            note.powered ? "true" : "false");
    }
    fprintf(out, "],");
    fprintf(out, "\"skulls\":[");
    for (int i = 0; i < gm_runtime_skull_count(r); ++i) {
        GmRuntimeSkull skull;
        if (!gm_runtime_skull_get(r, i, &skull))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"type\":%d,"
            "\"rotation\":%d,\"has_owner\":%s",
            skull.wx, skull.wy, skull.wz, skull.type, skull.rotation,
            skull.owner_profile.data ? "true" : "false");
        if (skull.owner_profile.data) {
            fprintf(out, ",\"owner_nbt\":\"");
            write_hex(
                out, skull.owner_profile.data, skull.owner_profile.len);
            fputc('"', out);
        }
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"decorative_tiles\":[");
    for (int i = 0; i < gm_runtime_decorative_tile_count(r); ++i) {
        GmRuntimeDecorativeTile tile;
        const GmNbtBlob *tile_tag;
        const GmNbtBlob *drop_tag;
        if (!gm_runtime_decorative_tile_get(r, i, &tile)) continue;
        tile_tag = gm_runtime_stack_tag(r, tile.tile_tag_id);
        drop_tag = gm_runtime_stack_tag(r, tile.drop_tag_id);
        if (i) fputc(',', out);
        fprintf(out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"block\":%d,"
            "\"tile_nbt\":\"",
            tile.wx, tile.wy, tile.wz, tile.block);
        if (tile_tag) write_hex(out, tile_tag->data, tile_tag->len);
        fprintf(out,
            "\",\"drop_item\":%d,\"drop_meta\":%d",
            tile.drop_item, tile.drop_meta);
        if (drop_tag) {
            fputs(",\"drop_nbt\":\"", out);
            write_hex(out, drop_tag->data, drop_tag->len);
            fputc('\"', out);
        }
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"item_frames\":[");
    for (int i = 0; i < gm_runtime_item_frame_count(r); ++i) {
        GmRuntimeItemFrame frame;
        if (!gm_runtime_item_frame_get(r, i, &frame))
            continue;
        if (i) fputc(',', out);
        int loaded_order = -1;
        for (int order = 0; order < r->loaded_entity_order_count; ++order)
            if (r->loaded_entity_order[order] == frame.eid) {
                loaded_order = order;
                break;
            }
        fprintf(
            out,
            "{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
            "\"hanging_x\":%d,\"hanging_y\":%d,\"hanging_z\":%d,"
            "\"facing\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,"
            "\"rotation\":%d,\"tick_counter\":%d,"
            "\"item_drop_chance\":%.9g,\"entity_seed48\":%llu,"
            "\"entity_have_gaussian\":%s,\"entity_gaussian\":%.17g,"
            "\"loaded_order\":%d,\"tracker_update_counter\":%d,"
            "\"map_data_present\":%s,\"map_dimension\":%d,"
            "\"map_x_center\":%d,\"map_z_center\":%d,\"map_scale\":%d,"
            "\"map_tracking_position\":%s,"
            "\"map_unlimited_tracking\":%s,"
            "\"map_decoration_present\":%s,"
            "\"map_decoration_type\":%d,\"map_decoration_x\":%d,"
            "\"map_decoration_z\":%d,\"map_decoration_rotation\":%d,"
            "\"repair_cost\":%d,\"custom_name\":",
            frame.eid, frame.x, frame.y, frame.z,
            frame.hanging_x, frame.hanging_y, frame.hanging_z,
            frame.facing, frame.item, frame.count, frame.meta,
            frame.rotation, frame.tick_counter,
            (double)frame.item_drop_chance, frame.random_seed48,
            frame.random_have_gaussian ? "true" : "false",
            frame.random_gaussian, loaded_order,
            frame.tracker_update_counter,
            frame.map_data_present ? "true" : "false",
            frame.map_dimension, frame.map_x_center, frame.map_z_center,
            frame.map_scale,
            frame.map_tracking_position ? "true" : "false",
            frame.map_unlimited_tracking ? "true" : "false",
            frame.map_decoration_present ? "true" : "false",
            frame.map_decoration_type, frame.map_decoration_x,
            frame.map_decoration_z, frame.map_decoration_rotation,
            frame.repair_cost);
        {
            const char *name = gm_runtime_item_name(r, frame.custom_name);
            write_json_string(out, name ? name : "");
        }
        fputs(",\"enchants\":[", out);
        for (int enchant = 0; enchant < frame.n_enchants; ++enchant) {
            if (enchant) fputc(',', out);
            fprintf(out, "[%d,%d]",
                (int)frame.enchants[enchant].id,
                (int)frame.enchants[enchant].level);
        }
        fputc(']', out);
        if (!frame.map_colors_present)
            fputs(",\"map_colors_b64\":\"\"", out);
        if (frame.tag_id > 0) {
            const GmNbtBlob *tag = gm_runtime_stack_tag(r, frame.tag_id);
            if (tag && tag->data && tag->len > 0) {
                fputs(",\"stack_payload\":{\"kind\":\"item_tag\","
                      "\"nbt\":\"", out);
                write_hex(out, tag->data, tag->len);
                fputs("\"}", out);
            }
        }
        if (frame.uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                frame.uuid_most, frame.uuid_least);
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"paintings\":[");
    for (int i = 0; i < gm_runtime_painting_count(r); ++i) {
        GmRuntimePainting painting;
        int loaded_order = -1;
        if (!gm_runtime_painting_get(r, i, &painting))
            continue;
        for (int order = 0; order < r->loaded_entity_order_count; ++order)
            if (r->loaded_entity_order[order] == painting.eid) {
                loaded_order = order;
                break;
            }
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
            "\"hanging_x\":%d,\"hanging_y\":%d,\"hanging_z\":%d,"
            "\"facing\":%d,\"art\":%d,\"tick_counter\":%d,"
            "\"loaded_order\":%d",
            painting.eid, painting.x, painting.y, painting.z,
            painting.hanging_x, painting.hanging_y, painting.hanging_z,
            painting.facing, painting.art, painting.tick_counter,
            loaded_order);
        if (painting.uuid_present)
            fprintf(out,
                ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                painting.uuid_most, painting.uuid_least);
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"leash_knots\":[");
    for (int i = 0; i < gm_runtime_leash_knot_count(r); ++i) {
        GmRuntimeLeashKnot knot;
        int loaded_order = -1;
        if (!gm_runtime_leash_knot_get(r, i, &knot))
            continue;
        for (int order = 0; order < r->loaded_entity_order_count; ++order)
            if (r->loaded_entity_order[order] == knot.eid) {
                loaded_order = order;
                break;
            }
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
            "\"hanging_x\":%d,\"hanging_y\":%d,\"hanging_z\":%d,"
            "\"tick_counter\":%d,\"loaded_order\":%d",
            knot.eid, knot.x, knot.y, knot.z,
            knot.hanging_x, knot.hanging_y, knot.hanging_z,
            knot.tick_counter, loaded_order);
        if (knot.uuid_present)
            fprintf(out,
                ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                knot.uuid_most, knot.uuid_least);
        fputc('}', out);
    }
    fprintf(out, "],");
    fprintf(out, "\"living_leashes\":[");
    {
        GmMobLive *mutable_mobs = (GmMobLive *)&r->mobs;
        const EwStore *mobs;
        int written = 0;
        int cursor = 0;
        int slot;
        while ((slot = gm_mobs_living_next_slot(
                    mutable_mobs, &cursor)) > 0) {
            int holder_kind, holder_eid, holder_slot;
            int entity_eid, entity_type, uuid_present, leashed, pending;
            int pending_x, pending_y, pending_z, wolf_angry;
            long long entity_most, entity_least;
            long long holder_most = 0, holder_least = 0;
            mobs = r->mobs.current ? &r->mobs.b : &r->mobs.a;
            if (!mobs->alive[slot]
                    || r->mobs.entity_dimension[slot] != r->dimension
                    || !gm_mobs_vanilla_leashable_type(mobs->type[slot])
                    || (!r->mobs.llama_leashed[slot]
                        && !r->mobs.llama_leash_pending[slot]
                        && !(mobs->type[slot] == EW_TYPE_WOLF
                            && r->mobs.wolf_angry[slot])))
                continue;
            entity_eid = mobs->id[slot];
            entity_type = mobs->type[slot];
            uuid_present = r->mobs.entity_uuid_present[slot];
            entity_most = (long long)r->mobs.entity_uuid_most[slot];
            entity_least = (long long)r->mobs.entity_uuid_least[slot];
            leashed = r->mobs.llama_leashed[slot];
            pending = r->mobs.llama_leash_pending[slot];
            pending_x = r->mobs.llama_leash_pending_x[slot];
            pending_y = r->mobs.llama_leash_pending_y[slot];
            pending_z = r->mobs.llama_leash_pending_z[slot];
            wolf_angry = entity_type == EW_TYPE_WOLF
                && r->mobs.wolf_angry[slot];
            holder_kind = r->mobs.llama_leash_holder_kind[slot];
            holder_eid = r->mobs.llama_leash_holder_eid[slot];
            holder_slot = holder_kind == 2
                ? gm_mobs_find_slot_by_eid(&r->mobs, holder_eid) : -1;
            if (holder_kind == 1) {
                holder_most = (long long)r->player_uuid_most;
                holder_least = (long long)r->player_uuid_least;
            } else if (holder_kind == 2 && holder_slot > 0
                    && r->mobs.entity_uuid_present[holder_slot]) {
                holder_most = (long long)
                    r->mobs.entity_uuid_most[holder_slot];
                holder_least = (long long)
                    r->mobs.entity_uuid_least[holder_slot];
            } else if (holder_kind == 3) {
                for (int index = 0; index < r->leash_knots_cap; ++index) {
                    const GmRuntimeLeashKnot *knot =
                        &r->leash_knots[index];
                    if (!knot->active || knot->eid != holder_eid) continue;
                    holder_most = knot->uuid_most;
                    holder_least = knot->uuid_least;
                    break;
                }
            }
            fprintf(out,
                "%s{\"eid\":%d,\"uuid_most\":%lld,"
                "\"uuid_least\":%lld,\"leashed\":%s,"
                "\"holder_kind\":%d,\"holder_eid\":%d,"
                "\"holder_uuid_most\":%lld,"
                "\"holder_uuid_least\":%lld,\"pending\":%s,"
                "\"pending_x\":%d,\"pending_y\":%d,"
                "\"pending_z\":%d,\"wolf_angry\":%s}",
                written++ ? "," : "", entity_eid,
                uuid_present ? entity_most : 0LL,
                uuid_present ? entity_least : 0LL,
                leashed ? "true" : "false",
                holder_kind, holder_eid, holder_most, holder_least,
                pending ? "true" : "false",
                pending_x, pending_y, pending_z,
                wolf_angry ? "true" : "false");
        }
        gm_mobs_living_cold_flush(mutable_mobs);
    }
    fprintf(out, "],");
    fprintf(out, "\"redstone_torch_toggles\":[");
    for (int i = 0; i < gm_runtime_redstone_torch_toggle_count(r); ++i) {
        GmRuntimeRedstoneTorchToggle toggle;
        if (!gm_runtime_redstone_torch_toggle_get(r, i, &toggle))
            continue;
        if (i) fputc(',', out);
        fprintf(
            out,
            "{\"x\":%d,\"y\":%d,\"z\":%d,\"time\":%lld}",
            toggle.x, toggle.y, toggle.z, toggle.time);
    }
    fprintf(out, "],");
    /* Position from the double-precision sim state, NOT the float render view:
     * MC positions are doubles and the tape differ works at 1e-9, so the float
     * round-trip alone showed up as a fake tick-0 divergence (3e-6). */
    fprintf(out, "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,",
            r->player.ent.posX + (double)r->ox,
            r->player.ent.posY,
            r->player.ent.posZ + (double)r->oz);
    fprintf(out, "\"player_name\":\"%s\",", r->player_name);
    fprintf(out,
            "\"player_spawn_present\":%s,\"player_spawn_x\":%d,"
            "\"player_spawn_y\":%d,\"player_spawn_z\":%d,"
            "\"player_spawn_forced\":%s,",
            r->player_spawn_present ? "true" : "false",
            r->player_spawn_x, r->player_spawn_y, r->player_spawn_z,
            r->player_spawn_forced ? "true" : "false");
    fprintf(out,
            "\"trigger_qrl_present\":%s,\"trigger_qrl_score\":%d,"
            "\"trigger_qrl_locked\":%s,",
            r->trigger_qrl_present ? "true" : "false",
            r->trigger_qrl_score,
            r->trigger_qrl_locked ? "true" : "false");
    fprintf(out, "\"yaw\":%.9g,\"pitch\":%.9g,", (double)v.yaw,(double)v.pitch);
    fprintf(out, "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,",
            r->player.ent.motionX,r->player.ent.motionY,r->player.ent.motionZ);
    fprintf(out,
            "\"server_x\":%.17g,\"server_y\":%.17g,\"server_z\":%.17g,"
            "\"server_vx\":%.17g,\"server_vy\":%.17g,"
            "\"server_vz\":%.17g,",
            r->server_player.ent.posX + (double)r->ox,
            r->server_player.ent.posY,
            r->server_player.ent.posZ + (double)r->oz,
            r->server_player.ent.motionX,
            r->server_player.ent.motionY,
            r->server_player.ent.motionZ);
    fprintf(out, "\"on_ground\":%d,\"health\":%.9g,"
                 "\"max_health\":%.9g,\"absorption\":%.9g,\"food\":%.9g,"
                 "\"saturation\":%.9g,\"food_exhaustion\":%.9g,"
                 "\"food_timer\":%d,\"air\":%d,\"fire\":%d,"
                 "\"creative\":%d,\"game_mode\":%d,"
                 "\"xp_level\":%d,\"xp_frac\":%.9g,\"fall_distance\":%.9g,"
                 "\"sprinting\":%d,\"sneaking\":%d,\"jumping\":%d,",
            v.on_ground,(double)v.health,(double)v.max_health,
            (double)v.absorption,(double)v.food,
            (double)r->vitals.saturation,
            (double)r->vitals.exhaustion, r->vitals.foodTimer,
            r->player_air,
            r->player_fire_ticks, v.creative, r->tape_game_mode,
            v.xp_level,(double)v.xp_frac,(double)r->player.fall_distance,
            r->player.sprinting,r->player.prev_sneak,r->player.prev_jump);
    {
        int sel = r->player.inv.current_item;
        ICStack held;
        if (sel < 0) sel = 0;
        if (sel > 8) sel = 8;
        held = isr_get_stack(&r->player.inv, sel);
        fprintf(out, "\"held_slot\":%d,\"held_id\":%d,\"held_count\":%d,"
                     "\"held_meta\":%d,\"attack_cooldown\":%.9g,"
                     "\"attack_ticks\":%d,\"hurt_time\":%d,"
                     "\"hurt_resistant_time\":%d,\"death_time\":%d,"
                     "\"xp_total\":%d,\"potions\":[",
                sel, held.item, held.count, held.meta,
                (double)v.attack_cooldown,
                r->mobs.player_ticks_since_last_swing,
                v.hurt_time, r->mobs.player_hurt_resistant,
                r->player_death_time,
                r->player_xp_level >= 0
                    ? r->player_xp_total : r->mobs.xp_total);
        for (int i = 0; i < v.potion_count; ++i) {
            if (i) fputc(',', out);
            fprintf(out, "{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                         "\"ambient\":%s,\"show_particles\":%s}",
                    v.potions[i].id, v.potions[i].amplifier,
                    v.potions[i].duration,
                    v.potions[i].ambient ? "true" : "false",
                    v.potions[i].hide_particles ? "false" : "true");
        }
        fprintf(out, "],");
        {
            int active = r->bow_drawing || v.use_action != 0;
            int remaining = r->bow_drawing
                ? (72000 - r->bow_ticks) : v.use_remaining;
            int elapsed = r->bow_drawing
                ? r->bow_ticks
                : (v.use_max > v.use_remaining
                    ? v.use_max - v.use_remaining : 0);
            int use_action = r->bow_drawing ? 4 : v.use_action;
            fprintf(out,
                    "\"hand_active\":%s,\"active_hand\":%d,"
                    "\"active_use_remaining\":%d,"
                    "\"active_use_elapsed\":%d,"
                    "\"active_use_action\":%d,",
                    active ? "true" : "false", active ? 0 : -1,
                    active ? remaining : 0,
                    active ? elapsed : 0,
                    active ? use_action : 0);
        }
    }
    { int hx,hy,hz,ax,ay,az;
      int hit=gm_raycast_sel(r->window,&r->sin_table,&r->player,
                             &hx,&hy,&hz,&ax,&ay,&az);
      if(hit>=0) fprintf(out,"\"look\":{\"x\":%d,\"y\":%d,\"z\":%d,\"id\":%d},",
          hx+r->ox,hy,hz+r->oz,gm_world_block(r->world,hx+r->ox,hy,hz+r->oz));
      else fprintf(out,"\"look\":null,"); }
    fprintf(out, "\"dead\":%d,\"deaths\":%d,\"won\":%s,\"credits\":%d,\"container\":%d,",
            v.dead,v.deaths,r->won?"true":"false",r->credits,r->container);
    fprintf(out,
            "\"statistics_present\":%d,\"stat_play_one_minute\":%lld,"
            "\"stat_time_since_death\":%lld,"
            "\"statistics_bytes\":%zu,\"statistics_fnv64\":\"%016llx\",",
            r->player_statistics_present,
            r->stat_play_one_minute, r->stat_time_since_death,
            r->player_statistics_len,
            (unsigned long long)r->player_statistics_fnv64);
    fprintf(out,
            "\"stat_achievement_open_inventory\":%d,"
            "\"stat_achievement_build_furnace\":%d,"
            "\"stat_achievement_acquire_iron\":%d,"
            "\"stat_achievement_cook_fish\":%d,"
            "\"stat_achievement_blaze_rod\":%d,"
            "\"stat_achievement_potion\":%d,",
            r->stat_achievement_open_inventory,
            r->stat_achievement_build_furnace,
            r->stat_achievement_acquire_iron,
            r->stat_achievement_cook_fish,
            r->stat_achievement_blaze_rod,
            r->stat_achievement_potion);
    fputs("\"stat_craft_items\":[", out);
    {
        int first_stat = 1;
        for (int item = 0; item < GM_RUNTIME_ITEM_STAT_LIMIT; ++item) {
            if (!r->stat_craft_item_present[item]) continue;
            fprintf(out, "%s{\"item\":%d,\"value\":%d}",
                    first_stat ? "" : ",", item,
                    r->stat_craft_item[item]);
            first_stat = 0;
        }
    }
    fputs("],\"brew_events\":[", out);
    for (int index = 0; index < r->brew_event_count; ++index) {
        GmRuntimeBrewEvent event;
        if (!gm_runtime_brew_event_get(r, index, &event)) continue;
        fprintf(out,
                "%s{\"seq\":%llu,\"achievement_awarded\":%s,"
                "\"stack\":",
                index ? "," : "",
                (unsigned long long)event.seq,
                event.achievement_awarded ? "true" : "false");
        write_merchant_stack(out, r, &event.stack);
        fputc('}', out);
    }
    fputs("],\"smelt_events\":[", out);
    for (int index = 0; index < r->smelt_event_count; ++index) {
        GmRuntimeSmeltEvent event;
        if (!gm_runtime_smelt_event_get(r, index, &event)) continue;
        fprintf(out,
                "%s{\"seq\":%llu,\"xp\":%d,"
                "\"achievement\":%d,\"achievement_awarded\":%s,"
                "\"stack\":",
                index ? "," : "",
                (unsigned long long)event.seq, event.xp,
                event.achievement,
                event.achievement_awarded ? "true" : "false");
        write_merchant_stack(out, r, &event.stack);
        fputc('}', out);
    }
    fputs("],", out);
    fprintf(out, "\"inventory\":[");
    {
        int first_inv = 1;
        for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", i, s.item, s.count, s.meta);
            for (int j = 0; j < s.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        s.enchants[j].id, s.enchants[j].level);
            fputc(']', out);
            write_stack_extra(out, r, &s);
            fputc('}', out);
            first_inv = 0;
        }
        for (int i = 0; i < ISR_ARMOR_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, ISR_ARMOR0 + i);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", ISR_ARMOR0 + i, s.item, s.count, s.meta);
            for (int j = 0; j < s.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        s.enchants[j].id, s.enchants[j].level);
            fputc(']', out);
            write_stack_extra(out, r, &s);
            fputc('}', out);
            first_inv = 0;
        }
        {
            ICStack oh = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
            fprintf(out, "%s{\"slot\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,\"enchants\":[",
                    first_inv ? "" : ",", ISR_OFFHAND_SLOT, oh.item, oh.count, oh.meta);
            for (int j = 0; j < oh.n_enchants; ++j)
                fprintf(out, "%s[%d,%d]", j ? "," : "",
                        oh.enchants[j].id, oh.enchants[j].level);
            fputc(']', out);
            write_stack_extra(out, r, &oh);
            fputc('}', out);
        }
    }
    fprintf(out, "],");
    fprintf(out, "\"ender_inventory\":[");
    {
        int first_ender = 1;
        for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot) {
            ICStack stack = gm_runtime_ender_chest_get_slot(r, slot);
            if (isr_is_empty(&stack)) continue;
            if (!first_ender) fputc(',', out);
            write_slotted_stack(out, r, slot, &stack);
            first_ender = 0;
        }
    }
    fprintf(out, "],");
    { ICStack c = gm_player_cursor();
      fprintf(out, "\"cursor\":[%d,%d,%d],", c.item, c.count, c.meta);
      fprintf(out,
              "\"cursor_stack\":{\"item\":%d,\"count\":%d,\"meta\":%d,"
              "\"enchants\":[",
              c.item, c.count, c.meta);
      for (int j = 0; j < c.n_enchants; ++j)
          fprintf(out, "%s[%d,%d]", j ? "," : "",
                  c.enchants[j].id, c.enchants[j].level);
      fputc(']', out);
      write_stack_extra(out, r, &c);
      fprintf(out, "},\"container_drag\":{\"event\":%d,\"mode\":%d,"
                   "\"slots\":[",
              r->container_drag_event, r->container_drag_mode);
      { int first_drag = 1;
        for (int slot = 0; slot < GMC_SLOT_COUNT; ++slot)
            if (r->container_drag_slots[slot]) {
                fprintf(out, "%s%d", first_drag ? "" : ",", slot);
                first_drag = 0;
            }
      }
      fprintf(out, "]},\"grid\":[");
      for (int i = 0; i < 9; ++i)
          fprintf(out, "%s[%d,%d,%d]", i ? "," : "",
                  r->craft_grid[i].item, r->craft_grid[i].count, r->craft_grid[i].meta);
      ICStack res = gm_container_result(r);
      fprintf(out, "],\"craft_result\":[%d,%d,%d],", res.item, res.count, res.meta); }
    fprintf(out, "\"entities\":[");
    int first = 1;
    for (int i = 0;
            i < gm_live_entity_slot_count(&r->entities); ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(&r->entities, i);
        char x[32], y[32], z[32], vx[32], vy[32], vz[32];
        char yaw[32], hover_start[32];
        if (!e || !e->active) continue;
        fprintf(out, "%s{\"kind\":\"item\",\"eid\":%d,\"type\":%d,"
                     "\"x\":%s,\"y\":%s,\"z\":%s,"
                     "\"vx\":%s,\"vy\":%s,\"vz\":%s,"
                     "\"yaw\":%s,\"pitch\":0,\"health\":%d,"
                     "\"item\":%d,\"count\":%d,\"meta\":%d,"
                     "\"age\":%d,\"ticks_existed\":%d,"
                     "\"pickup_delay\":%d,"
                     "\"lifespan\":%d,\"hover_start\":%s,"
                     "\"on_ground\":%s,\"no_gravity\":%s,\"no_clip\":%s,"
                     "\"fire\":%d,\"in_water\":%s,"
                     "\"first_update\":%s,\"entity_seed48\":%llu",
                first ? "" : ",", e->eid >= 0 ? e->eid : 1000000 + i,
                e->type,
                json_floating(x, e->x, 17),
                json_floating(y, e->y, 17),
                json_floating(z, e->z, 17),
                json_floating(vx, e->mx, 17),
                json_floating(vy, e->my, 17),
                json_floating(vz, e->mz, 17),
                json_floating(yaw, (double)e->yaw, 9), e->health,
                e->item,e->count,e->meta,e->age,e->ticks_existed,
                e->pickup_delay,
                e->lifespan,
                json_floating(hover_start, (double)e->hover_start, 9),
                e->on_ground ? "true" : "false",
                e->no_gravity ? "true" : "false",
                e->no_clip ? "true" : "false", e->fire,
                e->in_water ? "true" : "false",
                e->first_update ? "true" : "false",
                (unsigned long long)e->random_seed48);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    e->uuid_most, e->uuid_least);
        {
            ICStack stack = ic_mk(e->item, e->count, e->meta);
            stack.repair_cost = e->repair_cost;
            stack.custom_name = e->custom_name;
            stack.tag_id = e->tag_id;
            write_stack_extra(out, r, &stack);
        }
        {
            GmRuntimeTaggedItem tagged;
            if (e->tag_id == 0 && gm_runtime_tagged_item_get_by_eid(
                    r, e->eid, &tagged)) {
                if (tagged.tag.data && tagged.tag.len > 0) {
                    fprintf(
                        out,
                        ",\"stack_payload\":{\"kind\":\"item_tag\","
                        "\"nbt\":\"");
                    write_hex(out, tagged.tag.data, tagged.tag.len);
                    fprintf(out, "\"}");
                } else {
                    fprintf(
                        out,
                        ",\"stack_payload\":{\"kind\":\"shulker_box\","
                        "\"items\":[");
                    int item_written = 0;
                    for (int slot = 0; slot < tagged.size; ++slot) {
                        ICStack stack = tagged.slots[slot];
                        if (stack.item <= 0 || stack.count <= 0)
                            continue;
                        if (item_written++) fputc(',', out);
                        fprintf(
                            out,
                            "{\"slot\":%d,\"id\":%d,\"count\":%d,"
                            "\"meta\":%d}",
                            slot, stack.item, stack.count, stack.meta);
                    }
                    fprintf(out, "]}");
                }
            }
        }
        fputc('}', out);
        first = 0;
    }
    GmEntityView bosses[ED_NUM_CRYSTALS+1];
    int nboss=gm_dragon_fill_views(&r->dragon,bosses,ED_NUM_CRYSTALS+1);
    for(int i=0;i<nboss;++i){
        const GmEntityView *e=&bosses[i];
        fprintf(out,"%s{\"kind\":\"boss\",\"eid\":%d,\"type\":%d,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":0,\"vy\":0,\"vz\":0,\"yaw\":%.9g,\"pitch\":0,"
                    "\"health\":%.9g}",
                first?"":",",2000000+i,e->type,(double)e->x,(double)e->y,
                (double)e->z,(double)e->yaw,(double)e->health);
        first=0;
    }
    {
        GmMobLive *mutable_mobs = (GmMobLive *)&r->mobs;
        const EwStore *mobs;
        int cursor = 0;
        int i;
        while ((i = gm_mobs_living_next_slot(
                    mutable_mobs, &cursor)) > 0) {
            mobs = r->mobs.current ? &r->mobs.b : &r->mobs.a;
            if (!mobs->alive[i] ||
                r->mobs.entity_dimension[i] != r->mobs.active_dimension)
                continue;
            fprintf(out,"%s{\"kind\":\"mob\",\"eid\":%d,\"type\":%d,"
                        "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                        "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                        "\"yaw\":%.9g,\"pitch\":0,\"health\":%.9g",
                    first?"":",",mobs->id[i],mobs->type[i],
                    mobs->x[i],mobs->y[i],mobs->z[i],
                    mobs->vx[i],mobs->vy[i],mobs->vz[i],
                    (double)mobs->yaw[i],
                    mobs->type[i]==EW_TYPE_BOAT?-1.0:(double)mobs->health[i]);
                if (mobs->type[i] == EW_TYPE_BOAT) {
                fputs(",\"hurt_time\":null,\"death_time\":null,"
                      "\"hurt_resistant_time\":null}", out);
                } else {
                if (r->mobs.entity_uuid_present[i])
                    fprintf(out,
                            ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                            (long long)r->mobs.entity_uuid_most[i],
                            (long long)r->mobs.entity_uuid_least[i]);
                fprintf(out,",\"max_health\":%.9g,\"absorption\":%.9g,"
                            "\"air\":%d,"
                            "\"hurt_time\":%d,\"death_time\":%d,"
                            "\"hurt_resistant_time\":%d,\"potions\":[",
                        (double)gm_mobs_max_health(&r->mobs, i),
                        (double)gm_mobs_absorption(&r->mobs, i),
                        gm_mobs_air(&r->mobs, i),
                        r->mobs.entity_hurt_time[i],
                        r->mobs.entity_death_time[i],
                        r->mobs.entity_hurt_resistant[i]);
                for (int effect_index = 0;
                        effect_index < gm_mobs_potion_effect_count(
                            &r->mobs, i); ++effect_index) {
                    PtMobEffect effect;
                    int ambient = 0, show_particles = 1;
                    if (!gm_mobs_potion_effect_get(
                            &r->mobs, i, effect_index, &effect))
                        continue;
                    (void)gm_mobs_potion_effect_flags(
                        &r->mobs, i, effect_index,
                        &ambient, &show_particles);
                    fprintf(out,
                            "%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                            "\"ambient\":%s,\"show_particles\":%s}",
                            effect_index ? "," : "", effect.id,
                            effect.amplifier, effect.duration,
                            ambient ? "true" : "false",
                            show_particles ? "true" : "false");
                }
                fprintf(out, "],\"no_ai\":%s",
                        r->mobs.controlled_no_ai[i] ? "true" : "false");
                if (r->mobs.controlled_no_ai[i]
                        || mobs->type[i] == EW_TYPE_VILLAGER
                        || mobs->type[i] == EW_TYPE_BAT
                        || mobs->type[i] == EW_TYPE_SQUID) {
                    const JavaGaussianRandom *base_random =
                        &r->mobs.entity_random[i];
                    fprintf(out,
                            ",\"living_base_exact\":true,"
                            "\"no_ai_base_exact\":%s,"
                            "\"fire\":%d,\"on_ground\":%s,"
                            "\"fall_distance\":%.9g,\"in_water\":%s,"
                            "\"ticks_existed\":%d,"
                            "\"base_living_sound_time\":%d,"
                            "\"base_last_damage\":%.9g,"
                            "\"base_entity_seed48\":%llu,"
                            "\"base_entity_have_gaussian\":%s,"
                            "\"base_entity_gaussian\":%.17g,"
                            "\"base_box_min_x\":%.17g,"
                            "\"base_box_min_y\":%.17g,"
                            "\"base_box_min_z\":%.17g,"
                            "\"base_box_max_x\":%.17g,"
                            "\"base_box_max_y\":%.17g,"
                            "\"base_box_max_z\":%.17g",
                            r->mobs.controlled_no_ai[i] ? "true" : "false",
                            r->mobs.fire_ticks[i],
                            mobs->on_ground[i] ? "true" : "false",
                            (double)r->mobs.entity_fall_distance[i],
                            r->mobs.entity_in_water[i] ? "true" : "false",
                            r->mobs.entity_ticks_existed[i],
                            r->mobs.entity_living_sound_time[i],
                            (double)r->mobs.entity_last_damage[i],
                            (unsigned long long)base_random->random.seed,
                            base_random->have_next_next_gaussian
                                ? "true" : "false",
                            base_random->next_next_gaussian,
                            r->mobs.entity_box_min_x[i],
                            r->mobs.entity_box_min_y[i],
                            r->mobs.entity_box_min_z[i],
                            r->mobs.entity_box_max_x[i],
                            r->mobs.entity_box_max_y[i],
                            r->mobs.entity_box_max_z[i]);
                }
                if (gm_mobs_horse_type(mobs->type[i])) {
                    const char *subtype = mobs->type[i] == EW_TYPE_HORSE
                        ? "horse" : mobs->type[i] == EW_TYPE_DONKEY
                        ? "donkey" : mobs->type[i] == EW_TYPE_MULE
                        ? "mule" : mobs->type[i] == EW_TYPE_SKELETON_HORSE
                        ? "skeleton" : mobs->type[i] == EW_TYPE_LLAMA
                        ? "llama" : "zombie";
                    unsigned char status = r->mobs.horse_status[i];
                    fprintf(out,
                            ",\"horse_exact\":%s,\"horse_subtype\":",
                            r->mobs.controlled_no_ai[i] ? "true" : "false");
                    write_json_string(out, subtype);
                    fprintf(out,
                            ",\"horse_growing_age\":%d,"
                            "\"horse_forced_age\":%d,"
                            "\"horse_forced_age_timer\":%d,"
                            "\"horse_in_love\":%d,"
                            "\"horse_tame\":%s,\"horse_saddled\":%s,"
                            "\"horse_bred\":%s,\"horse_eating\":%s,"
                            "\"horse_rearing\":%s,"
                            "\"horse_mouth_open\":%s,"
                            "\"horse_temper\":%d,\"horse_variant\":%d,"
                            "\"horse_owner_present\":%s,"
                            "\"horse_owner_uuid_most\":%lld,"
                            "\"horse_owner_uuid_least\":%lld,"
                            "\"horse_armor\":%d,\"horse_chested\":%s,"
                            "\"horse_trap\":%s,\"horse_trap_time\":%d,"
                            "\"horse_max_health_base\":%.17g,"
                            "\"horse_movement_speed_base\":%.17g,"
                            "\"horse_jump_strength\":%.17g,"
                            "\"horse_eating_counter\":%d,"
                            "\"horse_open_mouth_counter\":%d,"
                            "\"horse_jump_rearing_counter\":%d,"
                            "\"horse_tail_counter\":%d,"
                            "\"horse_sprint_counter\":%d,"
                            "\"horse_gallop_time\":%d,"
                            "\"horse_jumping\":%s,"
                            "\"horse_allow_stand_sliding\":%s,"
                            "\"horse_jump_power\":%.9g,"
                            "\"horse_head_lean\":%.9g,"
                            "\"horse_prev_head_lean\":%.9g,"
                            "\"horse_rearing_amount\":%.9g,"
                            "\"horse_prev_rearing_amount\":%.9g,"
                            "\"horse_mouth_openness\":%.9g,"
                            "\"horse_prev_mouth_openness\":%.9g,"
                            "\"horse_prev_limb_amount\":%.9g,"
                            "\"horse_limb_amount\":%.9g,"
                            "\"horse_limb_swing\":%.9g,"
                            "\"horse_inventory\":[",
                            r->mobs.growing_age[i],
                            r->mobs.sheep_forced_age[i],
                            r->mobs.sheep_forced_age_timer[i],
                            r->mobs.sheep_in_love[i],
                            status & GM_HORSE_TAME ? "true" : "false",
                            status & GM_HORSE_SADDLED ? "true" : "false",
                            status & GM_HORSE_BRED ? "true" : "false",
                            status & GM_HORSE_EATING ? "true" : "false",
                            status & GM_HORSE_REARING ? "true" : "false",
                            status & GM_HORSE_MOUTH_OPEN ? "true" : "false",
                            r->mobs.horse_temper[i],
                            mobs->type[i] == EW_TYPE_LLAMA
                                ? r->mobs.llama_variant[i]
                                : r->mobs.horse_variant[i],
                            r->mobs.horse_owner_present[i]
                                ? "true" : "false",
                            (long long)r->mobs.horse_owner_uuid_most[i],
                            (long long)r->mobs.horse_owner_uuid_least[i],
                            (int)r->mobs.horse_armor[i],
                            r->mobs.horse_chested[i] ? "true" : "false",
                            r->mobs.horse_trap[i] ? "true" : "false",
                            r->mobs.horse_trap_time[i],
                            r->mobs.horse_max_health[i],
                            r->mobs.horse_movement_speed[i],
                            r->mobs.horse_jump_strength[i],
                            r->mobs.horse_eating_counter[i],
                            r->mobs.horse_open_mouth_counter[i],
                            r->mobs.horse_jump_rearing_counter[i],
                            r->mobs.horse_tail_counter[i],
                            r->mobs.horse_sprint_counter[i],
                            r->mobs.horse_gallop_time[i],
                            r->mobs.horse_jumping[i] ? "true" : "false",
                            r->mobs.horse_allow_stand_sliding[i]
                                ? "true" : "false",
                            (double)r->mobs.horse_jump_power[i],
                            (double)r->mobs.horse_head_lean[i],
                            (double)r->mobs.horse_prev_head_lean[i],
                            (double)r->mobs.horse_rearing_amount[i],
                            (double)r->mobs.horse_prev_rearing_amount[i],
                            (double)r->mobs.horse_mouth_openness[i],
                            (double)r->mobs.horse_prev_mouth_openness[i],
                            (double)r->mobs.horse_prev_limb_amount[i],
                            (double)r->mobs.horse_limb_amount[i],
                            (double)r->mobs.horse_limb_swing[i]);
                    int horse_stack_written = 0;
                    int horse_inventory_size =
                        r->mobs.horse_chested[i] ? 17 : 2;
                    for (int inventory_slot = 0;
                            inventory_slot < horse_inventory_size;
                            ++inventory_slot) {
                        const ICStack *stack =
                            &r->mobs.horse_inventory[i][inventory_slot];
                        if (stack->item <= 0 || stack->count <= 0) continue;
                        if (horse_stack_written++) fputc(',', out);
                        write_slotted_stack(
                            out, r, inventory_slot, stack);
                    }
                    fputc(']', out);
                    if (mobs->type[i] == EW_TYPE_LLAMA) {
                        int holder_kind =
                            r->mobs.llama_leash_holder_kind[i];
                        int holder_eid =
                            r->mobs.llama_leash_holder_eid[i];
                        long long holder_most = 0, holder_least = 0;
                        int holder_slot = holder_kind == 2
                            ? gm_mobs_find_slot_by_eid(
                                &r->mobs, holder_eid) : -1;
                        if (holder_kind == 1) {
                            holder_most = (long long)r->player_uuid_most;
                            holder_least = (long long)r->player_uuid_least;
                        } else if (holder_kind == 3) {
                            for (int knot_index = 0;
                                    knot_index < r->leash_knots_cap;
                                    ++knot_index) {
                                const GmRuntimeLeashKnot *knot =
                                    &r->leash_knots[knot_index];
                                if (!knot->active
                                        || knot->eid != holder_eid)
                                    continue;
                                holder_most = knot->uuid_most;
                                holder_least = knot->uuid_least;
                                break;
                            }
                        } else if (holder_slot > 0
                                && r->mobs.entity_uuid_present[holder_slot]) {
                            holder_most = (long long)
                                r->mobs.entity_uuid_most[holder_slot];
                            holder_least = (long long)
                                r->mobs.entity_uuid_least[holder_slot];
                        }
                        fprintf(out,
                            ",\"llama_strength\":%d,"
                            "\"llama_decor\":%d,"
                            "\"llama_did_spit\":%s,"
                            "\"llama_leashed\":%s,"
                            "\"llama_leash_holder_kind\":%d,"
                            "\"llama_leash_holder_eid\":%d,"
                            "\"llama_leash_holder_uuid_most\":%lld,"
                            "\"llama_leash_holder_uuid_least\":%lld,"
                            "\"llama_leash_pending\":%s,"
                            "\"llama_leash_pending_x\":%d,"
                            "\"llama_leash_pending_y\":%d,"
                            "\"llama_leash_pending_z\":%d,"
                            "\"llama_caravan_head_eid\":%d,"
                            "\"llama_caravan_tail_eid\":%d,"
                            "\"llama_caravan_speed\":%.17g,"
                            "\"llama_caravan_dist_counter\":%d",
                            (int)r->mobs.llama_strength[i],
                            (int)r->mobs.llama_decor[i],
                            r->mobs.llama_did_spit[i] ? "true" : "false",
                            r->mobs.llama_leashed[i] ? "true" : "false",
                            holder_kind, holder_eid,
                            holder_most, holder_least,
                            r->mobs.llama_leash_pending[i]
                                ? "true" : "false",
                            r->mobs.llama_leash_pending_x[i],
                            r->mobs.llama_leash_pending_y[i],
                            r->mobs.llama_leash_pending_z[i],
                            r->mobs.llama_caravan_head_eid[i],
                            r->mobs.llama_caravan_tail_eid[i],
                            r->mobs.llama_caravan_speed[i],
                            r->mobs.llama_caravan_dist_counter[i]);
                    }
                } else if (mobs->type[i] == EW_TYPE_SHEEP) {
                    fprintf(out,
                            ",\"sheep_fleece_color\":%d,"
                            "\"sheep_sheared\":%s",
                            (int)(r->mobs.sheep_data[i] & 15),
                            (r->mobs.sheep_data[i] & 16)
                                ? "true" : "false");
                } else if (mobs->type[i] == EW_TYPE_CHICKEN) {
                    fprintf(out,
                            ",\"chicken_egg_time\":%d,"
                            "\"chicken_wing_rotation\":%.9g,"
                            "\"chicken_dest_pos\":%.9g,"
                            "\"chicken_old_flap_speed\":%.9g,"
                            "\"chicken_old_flap\":%.9g,"
                            "\"chicken_wing_rot_delta\":%.9g,"
                            "\"chicken_jockey\":%s",
                            r->mobs.chicken_time_until_next_egg[i],
                            (double)r->mobs.chicken_wing_rotation[i],
                            (double)r->mobs.chicken_dest_pos[i],
                            (double)r->mobs.chicken_old_flap_speed[i],
                            (double)r->mobs.chicken_old_flap[i],
                            (double)r->mobs.chicken_wing_rot_delta[i],
                            r->mobs.chicken_jockey[i] ? "true" : "false");
                } else if (mobs->type[i] == EW_TYPE_RABBIT) {
                    fprintf(out,
                            ",\"rabbit_type\":%d,\"growing_age\":%d,"
                            "\"entity_seed48\":%llu,"
                            "\"entity_have_gaussian\":%s,"
                            "\"entity_gaussian\":%.17g",
                            (int)r->mobs.rabbit_type[i],
                            r->mobs.growing_age[i],
                            (unsigned long long)
                                r->mobs.entity_random[i].random.seed,
                            r->mobs.entity_random[i]
                                    .have_next_next_gaussian
                                ? "true" : "false",
                            r->mobs.entity_random[i]
                                .next_next_gaussian);
                } else if (mobs->type[i] == EW_TYPE_SLIME
                        || mobs->type[i] == EW_TYPE_MAGMA) {
                    fprintf(out,
                            ",\"slime_size\":%d,"
                            "\"slime_squish_amount\":%.9g,"
                            "\"slime_squish_factor\":%.9g,"
                            "\"slime_prev_squish_factor\":%.9g,"
                            "\"slime_was_on_ground\":%s",
                            (int)r->mobs.size[i],
                            (double)r->mobs.squish_amount[i],
                            (double)r->mobs.squish_factor[i],
                            (double)r->mobs.prev_squish_factor[i],
                            r->mobs.was_on_ground[i] ? "true" : "false");
                } else if (mobs->type[i] == EW_TYPE_IRON_GOLEM) {
                    fprintf(out,
                            ",\"golem_player_created\":%s,"
                            "\"golem_home_timer\":%d,"
                            "\"golem_attack_timer\":%d,"
                            "\"golem_rose_timer\":%d",
                            r->mobs.golem_player_created[i]
                                ? "true" : "false",
                            r->mobs.golem_home_check_timer[i],
                            r->mobs.golem_attack_timer[i],
                            r->mobs.golem_hold_rose_tick[i]);
                } else if (mobs->type[i] == EW_TYPE_BAT) {
                    fprintf(out,
                            ",\"bat_hanging\":%s,"
                            "\"bat_spawn_valid\":%s,"
                            "\"bat_spawn_x\":%d,\"bat_spawn_y\":%d,"
                            "\"bat_spawn_z\":%d,"
                            "\"bat_head_yaw\":%.9g,"
                            "\"bat_render_yaw_offset\":%.9g,"
                            "\"bat_body_rotation_tick_counter\":%d,"
                            "\"bat_body_prev_head_yaw\":%.9g,"
                            "\"bat_move_forward\":%.9g,"
                            "\"bat_move_strafing\":%.9g,"
                            "\"bat_entity_age\":%d,"
                            "\"bat_persistence_required\":%s,"
                            "\"bat_active_exact\":%s",
                            r->mobs.bat_hanging[i] ? "true" : "false",
                            r->mobs.bat_spawn_position_valid[i]
                                ? "true" : "false",
                            r->mobs.bat_spawn_x[i],
                            r->mobs.bat_spawn_y[i],
                            r->mobs.bat_spawn_z[i],
                            (double)r->mobs.passive_head_yaw[i],
                            (double)r->mobs.squid_render_yaw_offset[i],
                            r->mobs.body_rotation_tick_counter[i],
                            (double)r->mobs.body_prev_head_yaw[i],
                            (double)r->mobs.passive_move_forward[i],
                            (double)r->mobs.passive_move_strafe[i],
                            r->mobs.entity_age[i],
                            r->mobs.persistence_required[i]
                                ? "true" : "false",
                            r->mobs.controlled_no_ai[i]
                                ? "false" : "true");
                } else if (mobs->type[i] == EW_TYPE_SNOWMAN) {
                    fprintf(out, ",\"snowman_pumpkin\":%s",
                            r->mobs.snowman_pumpkin[i]
                                ? "true" : "false");
                } else if (mobs->type[i] == EW_TYPE_ENDERMITE) {
                    fprintf(out,
                            ",\"endermite_lifetime\":%d,"
                            "\"endermite_player_spawned\":%s,"
                            "\"endermite_persistence_required\":%s",
                            r->mobs.endermite_lifetime[i],
                            r->mobs.endermite_player_spawned[i]
                                ? "true" : "false",
                            r->mobs.persistence_required[i]
                                ? "true" : "false");
                } else if (mobs->type[i] == EW_TYPE_SQUID) {
                    fprintf(out,
                            ",\"squid_pitch\":%.9g,"
                            "\"squid_prev_pitch\":%.9g,"
                            "\"squid_yaw\":%.9g,"
                            "\"squid_prev_yaw\":%.9g,"
                            "\"squid_rotation\":%.9g,"
                            "\"squid_prev_rotation\":%.9g,"
                            "\"squid_tentacle_angle\":%.9g,"
                            "\"squid_last_tentacle_angle\":%.9g,"
                            "\"squid_random_motion_speed\":%.9g,"
                            "\"squid_rotation_velocity\":%.9g,"
                            "\"squid_rotate_speed\":%.9g,"
                            "\"squid_random_motion_x\":%.9g,"
                            "\"squid_random_motion_y\":%.9g,"
                            "\"squid_random_motion_z\":%.9g,"
                            "\"squid_render_yaw_offset\":%.9g,"
                            "\"squid_head_yaw\":%.9g,"
                            "\"squid_body_rotation_tick_counter\":%d,"
                            "\"squid_body_prev_head_yaw\":%.9g,"
                            "\"squid_entity_age\":%d,"
                            "\"squid_persistence_required\":%s,"
                            "\"squid_active_exact\":%s",
                            (double)r->mobs.squid_pitch[i],
                            (double)r->mobs.squid_prev_pitch[i],
                            (double)r->mobs.squid_yaw[i],
                            (double)r->mobs.squid_prev_yaw[i],
                            (double)r->mobs.squid_rotation[i],
                            (double)r->mobs.squid_prev_rotation[i],
                            (double)r->mobs.squid_tentacle_angle[i],
                            (double)r->mobs.squid_last_tentacle_angle[i],
                            (double)r->mobs.squid_random_motion_speed[i],
                            (double)r->mobs.squid_rotation_velocity[i],
                            (double)r->mobs.squid_rotate_speed[i],
                            (double)r->mobs.squid_random_motion_x[i],
                            (double)r->mobs.squid_random_motion_y[i],
                            (double)r->mobs.squid_random_motion_z[i],
                            (double)r->mobs.squid_render_yaw_offset[i],
                            (double)r->mobs.passive_head_yaw[i],
                            r->mobs.body_rotation_tick_counter[i],
                            (double)r->mobs.body_prev_head_yaw[i],
                            r->mobs.entity_age[i],
                            r->mobs.persistence_required[i]
                                ? "true" : "false",
                            r->mobs.controlled_no_ai[i]
                                ? "false" : "true");
                }
                if (mobs->type[i] == EW_TYPE_VILLAGER) {
                    const GmRuntimeVillageResident *resident = NULL;
                    for (int resident_index = 0;
                            resident_index < r->village_resident_count;
                            ++resident_index)
                        if (r->village_residents[resident_index].eid
                                == mobs->id[i]) {
                            resident = &r->village_residents[resident_index];
                            break;
                        }
                    const JavaGaussianRandom *random =
                        &r->mobs.entity_random[i];
                    fprintf(
                        out,
                        ",\"profession\":%d,\"growing_age\":%d,"
                        "\"career\":%d,\"career_level\":%d,"
                        "\"living_sound_time\":%d,"
                        "\"offers_initialized\":%s,"
                        "\"entity_seed48\":%llu,"
                        "\"entity_have_gaussian\":%s,"
                        "\"entity_gaussian\":%.17g",
                        (int)r->mobs.villager_profession[i],
                        r->mobs.growing_age[i],
                        resident && resident->trade.initialized
                            ? resident->trade.career : 0,
                        resident && resident->trade.initialized
                            ? resident->trade.career_level : 0,
                        r->mobs.entity_living_sound_time[i],
                        resident && resident->trade.initialized
                            ? "true" : "false",
                        (unsigned long long)random->random.seed,
                        random->have_next_next_gaussian
                            ? "true" : "false",
                        random->next_next_gaussian);
                    fprintf(out,
                            ",\"wealth\":%d,\"willing\":%s,"
                            "\"villager_inventory_empty\":true,"
                            "\"offers\":[",
                            resident && resident->trade.initialized
                                ? resident->trade.wealth : 0,
                            resident && resident->trade.initialized
                                    && resident->trade.willing_to_mate
                                ? "true" : "false");
                    if (resident && resident->trade.initialized) {
                        for (int offer_index = 0;
                                offer_index < resident->trade.offer_count;
                                ++offer_index) {
                            const GmVillagerOffer *offer =
                                &resident->trade.offers[offer_index];
                            fprintf(out,
                                    "%s{\"uses\":%d,\"max_uses\":%d,"
                                    "\"rewards_exp\":%s,\"buy_a\":",
                                    offer_index ? "," : "", offer->uses,
                                    offer->max_uses,
                                    offer->rewards_exp ? "true" : "false");
                            write_merchant_stack(out, r, &offer->buy_a);
                            fputs(",\"buy_b\":", out);
                            write_merchant_stack(out, r, &offer->buy_b);
                            fputs(",\"sell\":", out);
                            write_merchant_stack(out, r, &offer->sell);
                            fputc('}', out);
                        }
                    }
                    fputc(']', out);
                } else if (mobs->type[i] == EW_TYPE_WOLF
                        || mobs->type[i] == EW_TYPE_OCELOT) {
                    const JavaGaussianRandom *random =
                        &r->mobs.entity_random[i];
                    fprintf(out,
                            ",\"tameable_exact\":%s,\"tamed\":%s,"
                            "\"sitting\":%s,\"player_owner\":%s,"
                            "\"variant\":%d,\"growing_age\":%d,"
                            "\"living_sound_time\":%d,"
                            "\"entity_seed48\":%llu,"
                            "\"entity_have_gaussian\":%s,"
                            "\"entity_gaussian\":%.17g",
                            r->mobs.controlled_no_ai[i]
                                ? "true" : "false",
                            r->mobs.tameable_tamed[i] ? "true" : "false",
                            r->mobs.tameable_sitting[i] ? "true" : "false",
                            r->mobs.tameable_owner[i] ? "true" : "false",
                            (int)r->mobs.tameable_variant[i],
                            r->mobs.growing_age[i],
                            r->mobs.entity_living_sound_time[i],
                            (unsigned long long)random->random.seed,
                            random->have_next_next_gaussian
                                ? "true" : "false",
                            random->next_next_gaussian);
                }
                fputc('}', out);
            }
            first=0;
        }
        gm_mobs_living_cold_flush(mutable_mobs);
    }
    for (int i = 0; i < r->withers_cap; ++i) {
        const GmRuntimeWither *e = &r->withers[i];
        double half_width = (double)(0.9F / 2.0F);
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"wither\",\"eid\":%d,\"type\":66,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"health\":%.9g,"
                "\"hurt_time\":%d,\"death_time\":%d,"
                "\"hurt_resistant_time\":%d,\"no_ai\":%s,"
                "\"air\":%d,\"fire\":%d,\"on_ground\":%s,"
                "\"fall_distance\":%.9g,\"in_water\":%s,"
                "\"ticks_existed\":%d,\"base_living_sound_time\":%d,"
                "\"base_last_damage\":%.9g,"
                "\"base_entity_seed48\":%llu,"
                "\"base_entity_have_gaussian\":%s,"
                "\"base_entity_gaussian\":%.17g,"
                "\"base_box_min_x\":%.17g,\"base_box_min_y\":%.17g,"
                "\"base_box_min_z\":%.17g,\"base_box_max_x\":%.17g,"
                "\"base_box_max_y\":%.17g,\"base_box_max_z\":%.17g,"
                "\"max_health\":300,\"absorption\":0,"
                "\"no_gravity\":%s,\"render_yaw_offset\":%.9g,"
                "\"prev_render_yaw_offset\":%.9g,"
                "\"rotation_yaw_head\":%.9g,"
                "\"prev_rotation_yaw_head\":%.9g,"
                "\"body_rotation_tick_counter\":%d,"
                "\"body_prev_render_yaw_head\":%.9g,"
                "\"invul_time\":%d,\"block_break_counter\":%d,"
                "\"recently_hit\":%d,\"attacking_player\":%s,"
                "\"attack_target_eid\":%d,"
                "\"attack_target_is_player\":%s,"
                "\"revenge_eid\":%d,\"revenge_is_player\":%s,"
                "\"revenge_timer\":%d,"
                "\"hurt_target_task_active\":%s,"
                "\"hurt_target_eid\":%d,"
                "\"hurt_target_is_player\":%s,"
                "\"hurt_revenge_timer_old\":%d,"
                "\"hurt_target_unseen_ticks\":%d,"
                "\"nearest_target_task_active\":%s,"
                "\"goal_task_tick\":%d,"
                "\"target_task_tick\":%d,\"invul_task_active\":%s,"
                "\"ranged_task_active\":%s,"
                "\"ranged_attack_time\":%d,\"ranged_see_time\":%d",
                first ? "" : ",", e->eid,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, (double)e->health,
                e->hurt_time, e->death_time, e->hurt_resistant_time,
                e->no_ai ? "true" : "false", e->air, e->fire,
                e->on_ground ? "true" : "false",
                (double)e->fall_distance,
                e->in_water ? "true" : "false", e->ticks_existed,
                e->living_sound_time, (double)e->last_damage,
                (unsigned long long)e->random_seed48,
                e->random_have_gaussian ? "true" : "false",
                e->random_gaussian,
                e->x - half_width, e->y, e->z - half_width,
                e->x + half_width, e->y + (double)3.5F,
                e->z + half_width,
                e->no_gravity ? "true" : "false",
                (double)e->render_yaw_offset,
                (double)e->prev_render_yaw_offset,
                (double)e->rotation_yaw_head,
                (double)e->prev_rotation_yaw_head,
                e->body_rotation_tick_counter,
                (double)e->body_prev_render_yaw_head, e->invul_time,
                e->block_break_counter, e->recently_hit,
                e->attacking_player ? "true" : "false",
                e->target_eid,
                e->target_is_player ? "true" : "false",
                e->revenge_eid,
                e->revenge_is_player ? "true" : "false",
                e->revenge_timer,
                e->hurt_target_task_active ? "true" : "false",
                e->hurt_target_eid,
                e->hurt_target_is_player ? "true" : "false",
                e->hurt_revenge_timer_old,
                e->hurt_target_unseen_ticks,
                e->nearest_target_task_active ? "true" : "false",
                e->goal_task_tick, e->target_task_tick,
                e->invul_task_active ? "true" : "false",
                e->ranged_task_active ? "true" : "false",
                e->ranged_attack_time, e->ranged_see_time);
        for (int head = 0; head < 3; ++head) {
            fprintf(out,
                    ",\"head_target_%d\":%d,"
                    "\"head_target_player_%d\":%s",
                    head, e->watched_target[head], head,
                    e->watched_target_is_player[head] ? "true" : "false");
            if (head > 0) {
                int side = head - 1;
                fprintf(out,
                        ",\"head_next_%d\":%d,\"head_idle_%d\":%d,"
                        "\"head_yaw_%d\":%.9g,\"head_pitch_%d\":%.9g,"
                        "\"head_prev_yaw_%d\":%.9g,"
                        "\"head_prev_pitch_%d\":%.9g",
                        side, e->next_head_update[side],
                        side, e->idle_head_updates[side],
                        side, (double)e->head_y_rotation[side],
                        side, (double)e->head_x_rotation[side],
                        side, (double)e->head_y_rotation_prev[side],
                        side, (double)e->head_x_rotation_prev[side]);
            }
        }
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    e->uuid_most, e->uuid_least);
        fputc('}', out);
        first = 0;
    }
    for (int i = 0; i < r->armor_stands_cap; ++i) {
        const GmRuntimeArmorStand *e = &r->armor_stands[i];
        int equipment_written = 0;
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"armor_stand\",\"eid\":%d,\"type\":34,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"health\":%.9g,"
                "\"hurt_time\":%d,\"death_time\":%d,"
                "\"hurt_resistant_time\":%d,"
                "\"armor_stand_exact\":true,"
                "\"armor_stand_small\":%s,"
                "\"armor_stand_show_arms\":%s,"
                "\"armor_stand_no_base_plate\":%s,"
                "\"armor_stand_marker\":%s,"
                "\"armor_stand_no_gravity\":%s,"
                "\"armor_stand_invisible\":%s,"
                "\"armor_stand_disabled_slots\":%d,"
                "\"armor_stand_air\":%d,"
                "\"armor_stand_in_water\":%s,"
                "\"armor_stand_on_ground\":%s,"
                "\"armor_stand_fall_distance\":%.9g,"
                "\"armor_stand_fire\":%d,"
                "\"armor_stand_ticks_existed\":%d,"
                "\"armor_stand_punch_cooldown\":%lld,"
                "\"armor_stand_last_damage\":%.9g,"
                "\"armor_stand_entity_seed48\":%llu,"
                "\"armor_stand_entity_have_gaussian\":%s,"
                "\"armor_stand_entity_gaussian\":%.17g",
                first ? "" : ",", e->eid,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, (double)e->health,
                e->hurt_time, e->death_time, e->hurt_resistant_time,
                e->status & GM_ARMOR_STAND_SMALL ? "true" : "false",
                e->status & GM_ARMOR_STAND_SHOW_ARMS ? "true" : "false",
                e->status & GM_ARMOR_STAND_NO_BASE_PLATE
                    ? "true" : "false",
                e->status & GM_ARMOR_STAND_MARKER ? "true" : "false",
                e->no_gravity ? "true" : "false",
                e->invisible ? "true" : "false",
                e->disabled_slots, e->air,
                e->in_water ? "true" : "false",
                e->on_ground ? "true" : "false",
                (double)e->fall_distance, e->fire_ticks,
                e->ticks_existed, e->punch_cooldown,
                (double)e->last_damage,
                (unsigned long long)e->random_seed48,
                e->random_have_gaussian ? "true" : "false",
                e->random_gaussian);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most, (long long)e->uuid_least);
        fprintf(out,
                ",\"armor_stand_absorption\":%.9g,"
                "\"armor_stand_max_health\":%.9g,"
                "\"armor_stand_max_health_base\":%.9g,"
                "\"armor_stand_revenge_timer\":%d,"
                "\"armor_stand_portal_cooldown\":%d,"
                "\"armor_stand_custom_name_visible\":%s,"
                "\"armor_stand_silent\":%s,"
                "\"armor_stand_glowing\":%s,"
                "\"armor_stand_invulnerable\":%s,"
                "\"armor_stand_update_blocked\":%s,"
                "\"armor_stand_fall_flying\":%s,"
                "\"armor_stand_vehicle_eid\":%d,"
                "\"armor_stand_custom_name\":",
                (double)e->absorption, (double)e->max_health,
                (double)e->max_health_base,
                e->revenge_timer, e->portal_cooldown,
                e->custom_name_visible ? "true" : "false",
                e->silent ? "true" : "false",
                e->glowing ? "true" : "false",
                e->invulnerable ? "true" : "false",
                e->update_blocked ? "true" : "false",
                e->fall_flying ? "true" : "false", e->vehicle_eid);
        {
            const unsigned char *name = NULL;
            size_t name_length = 0;
            (void)gm_runtime_armor_stand_string(
                r, e->custom_name_tag_id, &name, &name_length);
            write_json_bytes(out,
                name ? name : (const unsigned char *)"", name_length);
        }
        fputs(",\"armor_stand_tags\":[", out);
        for (int tag = 0; tag < e->tag_count; ++tag) {
            const unsigned char *value = NULL;
            size_t value_length = 0;
            (void)gm_runtime_armor_stand_string(
                r, e->tag_ids[tag], &value, &value_length);
            if (tag) fputc(',', out);
            write_json_bytes(out,
                value ? value : (const unsigned char *)"", value_length);
        }
        fputs("],\"armor_stand_effects\":[", out);
        for (int effect = 0; effect < e->effect_count; ++effect) {
            const GmRuntimeArmorStandEffect *active = &e->effects[effect];
            fprintf(out,
                    "%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                    "\"ambient\":%s,\"show_particles\":%s}",
                    effect ? "," : "", active->id,
                    active->amplifier, active->duration,
                    active->ambient ? "true" : "false",
                    active->show_particles ? "true" : "false");
        }
        fputc(']', out);
        fputs(",\"armor_stand_equipment\":[", out);
        for (int slot = 0; slot < GM_ARMOR_STAND_SLOTS; ++slot) {
            const ICStack *stack = &e->equipment[slot];
            if (isr_is_empty(stack)) continue;
            if (equipment_written++) fputc(',', out);
            write_slotted_stack(out, r, slot, stack);
        }
        fputs("],\"armor_stand_pose\":[", out);
        for (int part = 0; part < GM_ARMOR_STAND_POSE_PARTS; ++part)
            fprintf(out, "%s[%.9g,%.9g,%.9g]", part ? "," : "",
                    (double)e->pose[part].x,
                    (double)e->pose[part].y,
                    (double)e->pose[part].z);
        fputs("]}", out);
        first = 0;
    }
    for (int i = 0; i < gm_mobs_xp_slot_count(&r->mobs); ++i) {
        const McOrb *e = gm_mobs_xp_orb_ref(&r->mobs, i);
        if (e->dead || e->xpValue <= 0 ||
            gm_mobs_xp_slot_dimension(&r->mobs, i)
                != r->mobs.active_dimension)
            continue;
        fprintf(out,"%s{\"kind\":\"xp_orb\",\"eid\":%d,\"type\":21,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                    "\"yaw\":%.9g,\"pitch\":0,\"health\":%d,\"value\":%d,"
                    "\"age\":%d,\"pickup_delay\":%d,\"color\":%d,"
                    "\"target_color\":%d",
                first?"":",",e->eid,e->posX,e->posY,e->posZ,
                e->motionX,e->motionY,e->motionZ,(double)e->yaw,
                e->health,e->xpValue,
                e->xpOrbAge,e->delayBeforeCanPickup,e->xpColor,
                e->xpTargetColor);
        {
            int64_t uuid_most, uuid_least;
            if (gm_mobs_xp_slot_uuid(
                    &r->mobs, i, &uuid_most, &uuid_least))
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)uuid_most, (long long)uuid_least);
        }
        fputc('}', out);
        first=0;
    }
    if (r->fish_hook.active && r->fish_hook.dimension == r->dimension) {
        const GmRuntimeFishHook *e = &r->fish_hook;
        int state = e->state == GM_FISH_STATE_HOOKED ? 1
            : e->state == GM_FISH_STATE_BOBBING ? 2 : 0;
        fprintf(out,
                "%s{\"kind\":\"fish_hook\",\"eid\":%d,\"type\":90,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"health\":-1,"
                "\"fish_state\":%d,\"in_ground\":%d,"
                "\"ticks_in_ground\":%d,\"ticks_in_air\":%d,"
                "\"ticks_catchable\":%d,\"ticks_caught_delay\":%d,"
                "\"ticks_catchable_delay\":%d,"
                "\"fish_approach_angle\":%.9g,\"lure\":%d,"
                "\"luck\":%d,\"caught_eid\":%d,"
                "\"entity_seed48\":%llu,"
                "\"entity_have_gaussian\":%d,"
                "\"entity_gaussian\":%.17g}",
                first ? "" : ",", e->eid,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, state, e->in_ground,
                e->ticks_in_ground, e->ticks_in_air,
                e->catch_state.ticks_catchable,
                e->catch_state.ticks_caught_delay,
                e->catch_state.ticks_catchable_delay,
                (double)e->catch_state.approach_angle,
                e->catch_state.lure, e->catch_state.luck, e->caught_eid,
                (unsigned long long)e->random.random.seed,
                e->random.have_next_next_gaussian,
                e->random.next_next_gaussian);
        first = 0;
    }
    for (int i = 0; i < r->fireworks_cap; ++i) {
        const GmRuntimeFirework *e = &r->fireworks[i];
        ICStack stack;
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"firework\",\"eid\":%d,\"type\":22,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"health\":-1,"
                "\"firework_exact\":%s,\"firework_age\":%d,"
                "\"lifetime\":%d,\"ticks_existed\":%d,"
                "\"prev_yaw\":%.9g,\"prev_pitch\":%.9g,"
                "\"attached_player\":%s,\"flight\":%d,"
                "\"explosion_count\":%d,\"large_blast\":%s,"
                "\"twinkle\":%s,\"firework_item_present\":%s,"
                "\"firework_item\":%d,\"firework_count\":%d,"
                "\"firework_meta\":%d,\"entity_seed48\":%llu,"
                "\"entity_have_gaussian\":%s,"
                "\"entity_gaussian\":%.17g",
                first ? "" : ",", e->eid,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch,
                e->state_exact && e->uuid_present ? "true" : "false",
                e->age, e->lifetime, e->ticks_existed,
                (double)e->prev_yaw, (double)e->prev_pitch,
                e->attached_player ? "true" : "false",
                e->flight, e->explosion_count,
                e->large_blast ? "true" : "false",
                e->twinkle ? "true" : "false",
                e->firework_item_present ? "true" : "false",
                e->firework_item, e->firework_count, e->firework_meta,
                (unsigned long long)e->random_seed48,
                e->random_have_gaussian ? "true" : "false",
                e->random_gaussian);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most,
                    (long long)e->uuid_least);
        if (e->firework_item_present && e->stack_tag_id != 0) {
            stack = ic_mk(
                e->firework_item, e->firework_count, e->firework_meta);
            stack.tag_id = e->stack_tag_id;
            write_stack_payload(out, r, &stack);
        }
        fputc('}', out);
        first = 0;
    }
    for(int i=0;i<r->projectiles_cap;++i){
        const GmRuntimeProjectile *p = &r->projectiles[i];
        double yaw, pitch, horiz;
        if (!p->active || (p->dimension != r->dimension && p->type == 10))
            continue;
        if (p->type == 1 || p->type == 3 || p->type == 10
                || (p->type >= 6 && p->type <= 9) || p->type == 12) {
            yaw = p->yaw;
            pitch = p->pitch;
        } else if (p->controlled_stationary) {
            /* The saved arrow updates before a same-tick explosion, so its
             * fresh external motion has not changed rotation yet. */
            yaw = pitch = 0.0;
        } else {
            horiz = sqrt(p->vx*p->vx + p->vz*p->vz);
            yaw = atan2(p->vx,p->vz) * 180.0 / MC_PI;
            pitch = atan2(p->vy,horiz) * 180.0 / MC_PI;
        }
        fprintf(out,"%s{\"kind\":\"projectile\",\"eid\":%d,\"type\":%d,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g",
                first?"":",",
                p->type == 10 ? p->eid
                    : (p->eid > 0 ? p->eid : 3000000+i),
                p->type,p->x,p->y,p->z,
                p->vx,p->vy,p->vz);
        if (p->type == 3 || p->type == 10)
            fprintf(out,",\"ax\":%.17g,\"ay\":%.17g,\"az\":%.17g",
                    p->ax,p->ay,p->az);
        if (p->type == 10) {
            fprintf(out,
                    ",\"shooter_eid\":%d,\"invulnerable\":%s,"
                    "\"ticks_in_air\":%d,\"life\":%d",
                    p->wither_shooter_eid,
                    p->wither_invulnerable ? "true" : "false",
                    p->wither_ticks_in_air, p->wither_life);
            if (p->uuid_present)
                fprintf(out,
                        ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                        p->uuid_most, p->uuid_least);
        }
        if ((p->type >= 6 && p->type <= 9) || p->type == 12) {
            fprintf(out,
                    ",\"throwable_exact\":%s,\"age\":%d,"
                    "\"ticks_in_air\":%d,\"prev_yaw\":%.9g,"
                    "\"prev_pitch\":%.9g,"
                    "\"player_thrower\":%d,"
                    "\"thrower_player_pending\":%d,"
                    "\"ignore_player\":%d,"
                    "\"ignore_player_time\":%d,"
                    "\"pearl_private_thrower\":%d,"
                    "\"throwable_shake\":%d,\"in_ground\":%d,"
                    "\"ticks_in_ground\":%d,"
                    "\"tile_x\":%d,\"tile_y\":%d,\"tile_z\":%d,"
                    "\"tile_block\":%d,\"portal_counter\":%d,"
                    "\"in_portal\":%d,"
                    "\"portal_cooldown\":%d,"
                    "\"last_portal_pos_valid\":%d,"
                    "\"last_portal_x\":%d,\"last_portal_y\":%d,"
                    "\"last_portal_z\":%d,"
                    "\"last_portal_vec_x\":%.17g,"
                    "\"last_portal_vec_y\":%.17g,"
                    "\"teleport_direction\":%d,"
                    "\"client_random_valid\":%d,"
                    "\"client_entity_seed48\":%llu,"
                    "\"entity_seed48\":%llu,"
                    "\"entity_have_gaussian\":%d,"
                    "\"entity_gaussian\":%.17g",
                    p->uuid_present && !p->in_portal
                        ? "true" : "false",
                    p->age,p->throwable_ticks_in_air,
                    (double)p->prev_yaw,(double)p->prev_pitch,
                    p->player_thrower,p->thrower_player_pending,
                    p->ignore_player,
                    p->ignore_player_time,p->pearl_private_thrower,
                    p->throwable_shake,p->throwable_in_ground,
                    p->throwable_ticks_in_ground,
                    p->throwable_tile_x,p->throwable_tile_y,
                    p->throwable_tile_z,p->throwable_tile_block,
                    p->portal_counter,p->in_portal,
                    p->portal_cooldown,
                    p->last_portal_pos_valid,
                    p->last_portal_x,p->last_portal_y,p->last_portal_z,
                    p->last_portal_vec_x,p->last_portal_vec_y,
                    p->teleport_direction,
                    p->client_random_valid,
                    (unsigned long long)p->client_random_seed48,
                    (unsigned long long)p->random_seed48,
                    p->random_have_gaussian,p->random_next_gaussian);
            if (p->uuid_present)
                fprintf(out,
                        ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                        p->uuid_most,p->uuid_least);
        }
        if (p->type == 1 || p->type == 2) {
            ICStack pickup = ic_mk(
                p->arrow_pickup_item,1,p->arrow_pickup_meta);
            pickup.tag_id = p->arrow_pickup_tag_id;
            fprintf(out,
                    ",\"arrow_exact\":true,\"ticks_in_air\":%d,"
                    "\"fire_ticks\":%d,\"damage\":%.17g,"
                    "\"knockback\":%d,\"critical\":%s,"
                    "\"pickup_status\":%d,\"in_ground\":%s,"
                    "\"shake\":%d,\"ticks_in_ground\":%d,"
                    "\"time_in_ground\":%d,"
                    "\"tile_x\":%d,\"tile_y\":%d,\"tile_z\":%d,"
                    "\"tile_block\":%d,\"tile_meta\":%d,"
                    "\"entity_seed48\":%llu,"
                    "\"entity_have_gaussian\":%s,"
                    "\"entity_gaussian\":%.17g,"
                    "\"arrow_kind\":%d,\"potion_type\":%d,"
                    "\"spectral_duration\":%d,\"arrow_color\":%d,"
                    "\"arrow_custom_color\":%s,"
                    "\"pickup_item\":%d,\"pickup_meta\":%d,"
                    "\"arrow_effects\":[",
                    p->age,p->fire_ticks,p->arrow_damage,
                    p->arrow_knockback,
                    p->arrow_critical ? "true" : "false",
                    p->arrow_pickup_status,
                    p->arrow_in_ground ? "true" : "false",
                    p->arrow_shake,p->arrow_ticks_in_ground,
                    p->arrow_time_in_ground,
                    p->arrow_tile_x,p->arrow_tile_y,p->arrow_tile_z,
                    p->arrow_tile_block,p->arrow_tile_meta,
                    (unsigned long long)p->random_seed48,
                    p->random_have_gaussian ? "true" : "false",
                    p->random_next_gaussian,
                    p->arrow_kind,p->arrow_potion_type,
                    p->arrow_spectral_duration,p->arrow_color,
                    p->arrow_custom_color ? "true" : "false",
                    p->arrow_pickup_item,p->arrow_pickup_meta);
            for (int effect_index=0;
                    effect_index<p->arrow_effect_count;++effect_index)
                fprintf(out,
                        "%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                        "\"flags\":%u}",
                        effect_index ? "," : "",
                        p->arrow_effects[effect_index].id,
                        p->arrow_effects[effect_index].amplifier,
                        p->arrow_effects[effect_index].duration,
                        (unsigned)p->arrow_effect_flags[effect_index]);
            fputc(']',out);
            if (p->uuid_present)
                fprintf(out,
                        ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                        p->uuid_most,p->uuid_least);
            write_stack_payload(out,r,&pickup);
        }
        if (p->type == 6) {
            ICStack potion = ic_mk(
                p->potion_item, 1, p->potion_type);
            potion.tag_id = p->potion_tag_id;
            fprintf(out,
                    ",\"potion_exact\":true,\"potion_item\":%d,"
                    "\"potion_type\":%d,\"potion_color\":%d,"
                    "\"potion_custom_color\":%s,\"potion_effects\":[",
                    p->potion_item,p->potion_type,p->potion_color,
                    p->potion_custom_color ? "true" : "false");
            for (int effect_index=0;
                    effect_index<p->potion_effect_count;++effect_index)
                fprintf(out,
                        "%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                        "\"flags\":%u}",
                        effect_index ? "," : "",
                        p->potion_effects[effect_index].id,
                        p->potion_effects[effect_index].amplifier,
                        p->potion_effects[effect_index].duration,
                        (unsigned)p->potion_effect_flags[effect_index]);
            fputc(']',out);
            write_stack_payload(out,r,&potion);
        }
        fprintf(out,",\"yaw\":%.9g,\"pitch\":%.9g,\"health\":-1}",
                yaw,pitch);
        first=0;
    }
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        const GmRuntimeMinecart *e = &r->minecarts[i];
        int size;
        int item_written = 0;
        if (!e->active || e->dimension != r->dimension) continue;
        size = e->kind == GM_MINECART_CHEST ? 27
            : e->kind == GM_MINECART_HOPPER ? 5 : 0;
        fprintf(out,
                "%s{\"kind\":\"minecart\",\"eid\":%d,\"type\":28,"
                "\"minecart_kind\":%d,\"x\":%.17g,\"y\":%.17g,"
                "\"z\":%.17g,\"vx\":%.17g,\"vy\":%.17g,"
                "\"vz\":%.17g,\"yaw\":%.9g,\"pitch\":%.9g,"
                "\"health\":-1,\"reverse\":%d,"
                "\"rolling_amplitude\":%d,\"rolling_direction\":%d,"
                "\"damage\":%.9g,"
                "\"fuel\":%d,\"push_x\":%.17g,\"push_z\":%.17g,"
                "\"tnt_fuse\":%d,\"hopper_enabled\":%d,"
                "\"transfer_cooldown\":%d,\"entity_seed48\":%llu,"
                "\"entity_have_gaussian\":%d,"
                "\"entity_gaussian\":%.17g",
                first ? "" : ",", e->eid, e->kind,
                e->x, e->y, e->z, e->vx, e->vy, e->vz,
                (double)e->yaw, (double)e->pitch, e->reverse,
                e->rolling_amplitude, e->rolling_direction,
                (double)e->damage,
                e->fuel, e->push_x, e->push_z, e->tnt_fuse,
                e->hopper_enabled, e->transfer_cooldown,
                (unsigned long long)e->random_seed48,
                e->random_have_gaussian, e->random_gaussian);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most, (long long)e->uuid_least);
        if (e->kind == GM_MINECART_SPAWNER) {
            const GmNbtBlob *spawn_data = gm_runtime_stack_tag(
                r, e->spawner_nbt_tag_id);
            fprintf(out,
                    ",\"spawner_entity\":%d,\"spawner_delay\":%d,"
                    "\"spawner_min_delay\":%d,\"spawner_max_delay\":%d,"
                    "\"spawner_spawn_count\":%d,"
                    "\"spawner_max_nearby\":%d,"
                    "\"spawner_activate_range\":%d,"
                    "\"spawner_spawn_range\":%d,"
                    "\"spawner_spawn_data_nbt\":",
                    e->spawner_entity_type, e->spawner_delay,
                    e->spawner_min_delay, e->spawner_max_delay,
                    e->spawner_spawn_count, e->spawner_max_nearby,
                    e->spawner_activate_range, e->spawner_spawn_range);
            if (spawn_data && spawn_data->data) {
                fputc('\"', out);
                write_hex(out, spawn_data->data, spawn_data->len);
                fputc('\"', out);
            } else {
                fputs("null", out);
            }
            fprintf(out,
                    ",\"spawner_default_entity_nbt\":%s,"
                    "\"spawner_potentials\":[",
                    e->spawner_default_entity_nbt ? "true" : "false");
            for (int potential = 0;
                    potential < e->spawner_potential_count; ++potential) {
                const GmNbtBlob *entity_nbt = gm_runtime_stack_tag(
                    r, e->spawner_potentials[potential].nbt_tag_id);
                fprintf(out,
                        "%s{\"entity\":%d,\"weight\":%d,"
                        "\"entity_nbt\":",
                        potential ? "," : "",
                        e->spawner_potentials[potential].type,
                        e->spawner_potentials[potential].weight);
                if (entity_nbt && entity_nbt->data) {
                    fputc('\"', out);
                    write_hex(out, entity_nbt->data, entity_nbt->len);
                    fputc('\"', out);
                } else {
                    fputs("null", out);
                }
                fprintf(out, ",\"default_entity_nbt\":%s}",
                        e->spawner_potentials[potential].default_entity_nbt
                            ? "true" : "false");
            }
            fputc(']', out);
        }
        fputs(",\"items\":[", out);
        for (int slot = 0; slot < size; ++slot) {
            ICStack stack = e->slots[slot];
            if (isr_is_empty(&stack)) continue;
            fprintf(out,
                    "%s{\"slot\":%d,\"id\":%d,\"count\":%d,"
                    "\"meta\":%d",
                    item_written++ ? "," : "", slot,
                    stack.item, stack.count, stack.meta);
            write_stack_payload(out, r, &stack);
            fputc('}', out);
        }
        fputs("]}", out);
        first = 0;
    }
    for (int i = 0; i < r->area_effect_clouds_cap; ++i) {
        const GmRuntimeAreaEffectCloud *e = &r->area_effect_clouds[i];
        if (!e->state.active) continue;
        fprintf(out,
                "%s{\"kind\":\"area_effect_cloud\",\"eid\":%d,"
                "\"type\":41,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,"
                "\"prev_yaw\":%.9g,\"prev_pitch\":%.9g,"
                "\"health\":-1,\"potion_type\":%d,\"age\":%d,"
                "\"duration\":%d,\"duration_on_use\":%d,"
                "\"wait_time\":%d,"
                "\"reapplication_delay\":%d,\"radius\":%.9g,"
                "\"radius_on_use\":%.9g,\"radius_per_tick\":%.9g,"
                "\"next_application\":%d,\"player_owner\":%d,"
                "\"uuid_most\":%lld,\"uuid_least\":%lld,"
                "\"owner_present\":%s,\"owner_eid\":%d,"
                "\"owner_uuid_most\":%lld,\"owner_uuid_least\":%lld,"
                "\"ignore_radius\":%s,\"particle\":%d,"
                "\"particle_param1\":%d,\"particle_param2\":%d,"
                "\"dimension\":%d,\"air\":%d,\"fire\":%d,"
                "\"portal_cooldown\":%d,\"on_ground\":%s,"
                "\"no_gravity\":%s,\"invulnerable\":%s,"
                "\"silent\":%s,\"glowing\":%s,"
                "\"update_blocked\":%s,\"in_water\":%s,"
                "\"first_update\":%s,\"fall_distance\":%.9g,"
                "\"prev_x\":%.17g,\"prev_y\":%.17g,"
                "\"prev_z\":%.17g,\"last_tick_x\":%.17g,"
                "\"last_tick_y\":%.17g,\"last_tick_z\":%.17g,"
                "\"server_entity_seed48\":%llu,"
                "\"server_entity_have_gaussian\":%s,"
                "\"server_entity_gaussian\":%.17g,"
                "\"entity_seed48\":%llu,\"potion_color\":%d,"
                "\"potion_custom_color\":%s,\"potion_effects\":[",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->vx, e->vy, e->vz, (double)e->yaw, (double)e->pitch,
                (double)e->prev_yaw, (double)e->prev_pitch,
                e->potion_type, e->state.age, e->state.duration,
                e->state.duration_on_use, e->state.wait_time,
                e->state.reapplication_delay,
                (double)e->state.radius, (double)e->state.radius_on_use,
                (double)e->state.radius_per_tick,
                e->state.next_application, e->player_owner,
                e->uuid_most, e->uuid_least,
                e->owner_present ? "true" : "false", e->owner_eid,
                e->owner_uuid_most, e->owner_uuid_least,
                e->ignore_radius ? "true" : "false", e->particle,
                e->particle_param1, e->particle_param2,
                e->dimension, e->air, e->fire_ticks,
                e->portal_cooldown,
                e->on_ground ? "true" : "false",
                e->no_gravity ? "true" : "false",
                e->invulnerable ? "true" : "false",
                e->silent ? "true" : "false",
                e->glowing ? "true" : "false",
                e->update_blocked ? "true" : "false",
                e->in_water ? "true" : "false",
                e->first_update ? "true" : "false",
                (double)e->fall_distance,
                e->prev_x, e->prev_y, e->prev_z,
                e->last_tick_x, e->last_tick_y, e->last_tick_z,
                (unsigned long long)e->server_random_seed48,
                e->server_random_have_gaussian ? "true" : "false",
                e->server_random_gaussian,
                (unsigned long long)e->random_seed48,e->potion_color,
                e->potion_custom_color ? "true" : "false");
        for (int effect_index=0;
                effect_index<e->potion_effect_count;++effect_index)
            fprintf(out,
                    "%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
                    "\"flags\":%u}",
                    effect_index ? "," : "",
                    e->potion_effects[effect_index].id,
                    e->potion_effects[effect_index].amplifier,
                    e->potion_effects[effect_index].duration,
                    (unsigned)e->potion_effect_flags[effect_index]);
        fputs("],\"reapplication_deadlines\":[",out);
        {
            int capacity = r->area_effect_cooldown_count + 1;
            int *target_eid = (int *)malloc(
                (size_t)capacity * sizeof *target_eid);
            int *deadline = (int *)malloc(
                (size_t)capacity * sizeof *deadline);
            int count = 0;
            if (target_eid && deadline && e->state.next_application > 0) {
                target_eid[count] = r->player_entity_id;
                deadline[count++] = e->state.next_application;
            }
            if (target_eid && deadline)
                for (int slot = 0;
                        slot < r->area_effect_cooldown_count; ++slot) {
                    const GmRuntimeAreaEffectCooldown *entry =
                        &r->area_effect_cooldowns[slot];
                    if (entry->cloud_eid != e->eid
                            || entry->deadline <= 0)
                        continue;
                    target_eid[count] = entry->target_eid;
                    deadline[count++] = entry->deadline;
                }
            for (int right = 1; right < count; ++right) {
                int sorted_eid = target_eid[right];
                int sorted_deadline = deadline[right];
                int left = right;
                while (left > 0 && target_eid[left - 1] > sorted_eid) {
                    target_eid[left] = target_eid[left - 1];
                    deadline[left] = deadline[left - 1];
                    --left;
                }
                target_eid[left] = sorted_eid;
                deadline[left] = sorted_deadline;
            }
            for (int target = 0; target < count; ++target)
                fprintf(out, "%s{\"eid\":%d,\"deadline\":%d}",
                        target ? "," : "", target_eid[target],
                        deadline[target]);
            free(target_eid);
            free(deadline);
        }
        fputs("]}",out);
        first = 0;
    }
    for(int i=0;i<r->falling_blocks_cap;++i){
        const GmRuntimeFallingBlock *e = &r->falling_blocks[i];
        if (!e->active) continue;
        fprintf(out,"%s{\"kind\":\"falling_block\",\"eid\":%d,\"type\":38,"
                    "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                    "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                    "\"yaw\":0,\"pitch\":0,\"health\":-1,"
                    "\"block\":%d,\"meta\":%d,\"fall_time\":%d,"
                    "\"origin_x\":%d,\"origin_y\":%d,\"origin_z\":%d",
                first?"":",",e->eid,e->x,e->y,e->z,e->vx,e->vy,e->vz,
                e->block,e->meta,e->fall_time,
                e->origin_x,e->origin_y,e->origin_z);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most, (long long)e->uuid_least);
        fputc('}', out);
        first=0;
    }
    for (int i = 0; i < r->primed_tnt_cap; ++i) {
        const GmRuntimePrimedTnt *e = &r->primed_tnt[i];
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"primed_tnt\",\"eid\":%d,\"type\":39,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"yaw\":0,\"pitch\":0,\"health\":-1,\"fuse\":%d",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->vx, e->vy, e->vz, e->fuse);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most, (long long)e->uuid_least);
        fputc('}', out);
        first = 0;
    }
    for (int i = 0; i < r->end_crystals_cap; ++i) {
        const GmRuntimeEndCrystal *e = &r->end_crystals[i];
        if (!e->active || e->dimension != r->dimension) continue;
        fprintf(out,
                "%s{\"kind\":\"end_crystal\",\"eid\":%d,\"type\":40,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"vx\":0,\"vy\":0,\"vz\":0,"
                "\"yaw\":0,\"pitch\":0,\"health\":-1,"
                "\"inner_rotation\":%d,\"show_bottom\":%d,"
                "\"has_beam\":%d,\"beam_x\":%d,\"beam_y\":%d,"
                "\"beam_z\":%d",
                first ? "" : ",", e->eid, e->x, e->y, e->z,
                e->inner_rotation, e->show_bottom, e->has_beam,
                e->beam_x, e->beam_y, e->beam_z);
        if (e->uuid_present)
            fprintf(out, ",\"uuid_most\":%lld,\"uuid_least\":%lld",
                    (long long)e->uuid_most, (long long)e->uuid_least);
        fputc('}', out);
        first = 0;
    }
    fprintf(out, "],\"village_collection\":{\"tick\":%d,\"villages\":[",
            r->village_collection_tick);
    for (int i = 0; i < r->village_state_count; ++i) {
        const GmVillageState *village = &r->village_states[i];
        fprintf(out,
                "%s{\"population\":%d,\"radius\":%d,\"golems\":%d,"
                "\"stable\":%d,\"state_tick\":%d,\"mating_tick\":%d,"
                "\"center_x\":%d,\"center_y\":%d,\"center_z\":%d,"
                "\"helper_x\":%d,\"helper_y\":%d,\"helper_z\":%d,"
                "\"doors\":[",
                i ? "," : "", village->num_villagers, village->radius,
                village->num_golems, village->last_add_door_timestamp,
                village->tick_counter, village->no_breed_ticks,
                village->center_x, village->center_y, village->center_z,
                village->helper_x, village->helper_y, village->helper_z);
        for (int door_index = 0;
                door_index < village->door_count; ++door_index) {
            const GmVillageDoorState *door = &village->doors[door_index];
            fprintf(out,
                    "%s{\"x\":%d,\"y\":%d,\"z\":%d,"
                    "\"inside_dx\":%d,\"inside_dz\":%d,"
                    "\"timestamp\":%d}",
                    door_index ? "," : "", door->x, door->y, door->z,
                    door->inside_dx, door->inside_dz, door->timestamp);
        }
        fputs("],\"reputations\":[", out);
        for (int reputation_index = 0;
                reputation_index < village->reputation_count;
                ++reputation_index) {
            const GmVillageReputationState *reputation =
                &village->reputations[reputation_index];
            fprintf(out,
                    "%s{\"uuid_most_hex\":\"%016llx\","
                    "\"uuid_least_hex\":\"%016llx\",\"score\":%d}",
                    reputation_index ? "," : "",
                    (unsigned long long)reputation->uuid_most,
                    (unsigned long long)reputation->uuid_least,
                    reputation->score);
        }
        fputs("]}", out);
    }
    fprintf(out, "]},\"mob_update_order\":[");
    for (int i = 0; i < r->mobs.tick_update_order_count; ++i) {
        int eid = 0;
        if (!gm_mobs_tick_update_order_get(&r->mobs, i, &eid)) break;
        fprintf(out, "%s%d", i ? "," : "", eid);
    }
    fprintf(out, "],\"loaded_entity_order\":[");
    for (int i = 0; i < r->loaded_entity_order_count; ++i)
        fprintf(out, "%s%d", i ? "," : "",
                r->loaded_entity_order[i]);
    fprintf(out, "],\"loaded_tile_order\":[");
    for (int i = 0; i < r->loaded_tile_order_count; ++i) {
        const GmRuntimeLoadedTile *tile = &r->loaded_tile_order[i];
        fprintf(out, "%s[%d,%d,%d]", i ? "," : "",
                tile->x, tile->y, tile->z);
    }
    fprintf(out, "],\"tickable_tile_order\":[");
    for (int i = 0; i < r->tickable_tile_order_count; ++i) {
        const GmRuntimeLoadedTile *tile = &r->tickable_tile_order[i];
        fprintf(out, "%s[%d,%d,%d]", i ? "," : "",
                tile->x, tile->y, tile->z);
    }
    fprintf(out, "],\"spawners\":[");
    {
        int written = 0;
        for (int i = 0; i < r->mobs.spawners_cap; ++i) {
            const GmSpawnerTE *spawner = &r->mobs.spawners[i];
            if (!spawner->active || spawner->dimension != r->dimension)
                continue;
            fprintf(out,
                    "%s{\"x\":%d,\"y\":%d,\"z\":%d,"
                    "\"entity\":%d,\"delay\":%d,"
                    "\"min_delay\":%d,\"max_delay\":%d,"
                    "\"spawn_count\":%d,\"max_nearby\":%d,"
                    "\"activate_range\":%d,\"spawn_range\":%d,",
                    written++ ? "," : "", spawner->x, spawner->y,
                    spawner->z, spawner->entity_type, spawner->delay,
                    spawner->min_delay, spawner->max_delay,
                    spawner->spawn_count, spawner->max_nearby,
                    spawner->activate_range, spawner->spawn_range);
            {
                const GmNbtBlob *spawn_data = gm_runtime_stack_tag(
                    r, spawner->entity_nbt_tag_id);
                fputs("\"spawn_data_nbt\":", out);
                if (spawn_data && spawn_data->data) {
                    fputc('\"', out);
                    write_hex(out, spawn_data->data, spawn_data->len);
                    fputc('\"', out);
                } else {
                    fputs("null", out);
                }
                fprintf(out, ",\"default_entity_nbt\":%s,\"potentials\":[",
                        spawner->default_entity_nbt ? "true" : "false");
            }
            for (int potential = 0;
                    potential < spawner->potential_count; ++potential) {
                const GmNbtBlob *entity_nbt = gm_runtime_stack_tag(
                    r, spawner->potentials[potential].nbt_tag_id);
                fprintf(out,
                        "%s{\"entity\":%d,\"weight\":%d,"
                        "\"entity_nbt\":",
                        potential ? "," : "",
                        spawner->potentials[potential].type,
                        spawner->potentials[potential].weight);
                if (entity_nbt && entity_nbt->data) {
                    fputc('\"', out);
                    write_hex(out, entity_nbt->data, entity_nbt->len);
                    fputc('\"', out);
                } else {
                    fputs("null", out);
                }
                fprintf(out, ",\"default_entity_nbt\":%s}",
                        spawner->potentials[potential].default_entity_nbt
                            ? "true" : "false");
            }
            fputs("]}", out);
        }
    }
    fprintf(out, "],\"loaded_chunk_order\":[");
    {
        int written = 0;
        for (int i = 0; i < r->loaded_chunks_cap; ++i) {
            const GmRuntimeLoadedChunk *chunk = &r->loaded_chunks[i];
            if (!chunk->present) continue;
            fprintf(out, "%s[%d,%d]", written++ ? "," : "",
                    chunk->chunk_x, chunk->chunk_z);
        }
    }
    fprintf(out, "],\"furnace\":");
    if (r->active_furnace >= 0) {
        const FurnaceLive *f=&r->furnaces[r->active_furnace].state;
        fprintf(out,"{\"input\":[%d,%d,%d],\"fuel\":[%d,%d,%d],"
                    "\"output\":[%d,%d,%d],\"burn\":%d,\"cook\":%d,\"cook_total\":%d}",
                f->input.item,f->input.count,f->input.meta,
                f->fuel.item,f->fuel.count,f->fuel.meta,
                f->output.item,f->output.count,f->output.meta,
                f->burn_time,f->cook_time,f->total_cook);
    } else fprintf(out,"null");
    /* Tape-driven render ghosts, exactly as ingested: the Java-truth entity
     * rows come back out so replay can assert the tape->magma pipeline did
     * not drop, cap, or corrupt them (positions are float32 of the taped
     * doubles). */
    fprintf(out, ",\"ghost_views\":[");
    {
        GmEntityView ghosts[GM_RUNTIME_GHOST_VIEWS];
        int ng = gm_runtime_ghost_views(r, ghosts, GM_RUNTIME_GHOST_VIEWS);
        for (int i = 0; i < ng; ++i)
            fprintf(out, "%s{\"type\":%d,\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}",
                    i ? "," : "", ghosts[i].type, (double)ghosts[i].x,
                    (double)ghosts[i].y, (double)ghosts[i].z);
    }
    fprintf(out, "]");
    fprintf(out, ",\"particle_events\":[");
    for (int i = 0; i < gm_runtime_particle_event_count(r); ++i) {
        GmRuntimeParticleEvent event;
        if (!gm_runtime_particle_event_get(r, i, &event)) continue;
        fprintf(out,
                "%s{\"kind\":%d,\"count\":%d,\"entity_eid\":%d,"
                "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"motion_x\":%.17g,\"motion_y\":%.17g,"
                "\"motion_z\":%.17g,\"parameter_count\":%d,"
                "\"parameters\":[%d,%d]}",
                i ? "," : "", event.kind, event.count, event.entity_eid,
                event.x, event.y, event.z,
                event.motion_x, event.motion_y, event.motion_z,
                event.parameter_count, event.parameters[0],
                event.parameters[1]);
    }
    fprintf(out, "]");
    int anchor[3];
    unsigned long long nh = nearby_hash(r, anchor);
    fprintf(out, ",\"nearby_anchor\":[%d,%d,%d]", anchor[0],anchor[1],anchor[2]);
    {
        int every = nearby_blocks_every();
        long long offset = nearby_blocks_offset();
        if (every > 0 && r->tick >= offset
                && (r->tick - offset) % every == 0)
            write_nearby_blocks(out, r, &v);
    }
    fprintf(out, ",\"nearby_hash\":\"%016llx\",\"terminal\":%s}\n",
            nh, v.dead ? "\"death\"" : (r->won?"\"won\"":"null"));
}

/* MAGMA_STATE_PROF=1: batched-env sizing census, printed to stderr at run
 * end. Distinct packed states (id<<4|meta, air included) per chunk and per
 * 3x3-chunk window bound the u8-palette budget; non-air 16^3 sections per
 * chunk bound section elision. Scans the 9x9 chunks around the player (must
 * be inside the generated radius). */
static void prof_scan(GmRuntime *r) {
    int pcx = (int)floor((r->player.ent.posX + (double)r->ox) / 16.0);
    int pcz = (int)floor((r->player.ent.posZ + (double)r->oz) / 16.0);
    /* render-off runs only generate the physics window; a census over
     * ungenerated (all-air) chunks would undercount everything. */
    gm_world_ensure(r->world, pcx, pcz, 5);
    static unsigned char seen[65536];
    int worst_chunk = 0, sec_tot = 0, sec_max = 0, nchunks = 0;
    for (int dz = -4; dz <= 4; ++dz) for (int dx = -4; dx <= 4; ++dx) {
        int cx = pcx + dx, cz = pcz + dz, nsec = 0, ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int s = 0; s < 16; ++s) {
            int has = 0;
            for (int y = s * 16; y < s * 16 + 16; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        int wx = cx * 16 + lx, wz = cz * 16 + lz;
                        unsigned st = (unsigned)((gm_world_block(r->world, wx, y, wz) << 4) |
                                                 gm_world_meta(r->world, wx, y, wz));
                        if (st) has = 1;
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
            nsec += has;
        }
        nchunks++; sec_tot += nsec;
        if (nsec > sec_max) sec_max = nsec;
        if (ndist > worst_chunk) worst_chunk = ndist;
    }
    int worst_win = 0;
    for (int wz = -1; wz <= 1; ++wz) for (int wx = -1; wx <= 1; ++wx) {
        int ndist = 0;
        memset(seen, 0, sizeof seen);
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            int cx = pcx + wx + dx, cz = pcz + wz + dz;
            for (int y = 0; y < 256; ++y)
                for (int lz = 0; lz < 16; ++lz)
                    for (int lx = 0; lx < 16; ++lx) {
                        unsigned st = (unsigned)((gm_world_block(r->world, cx * 16 + lx, y, cz * 16 + lz) << 4) |
                                                 gm_world_meta(r->world, cx * 16 + lx, y, cz * 16 + lz));
                        if (!seen[st]) { seen[st] = 1; ndist++; }
                    }
        }
        if (ndist > worst_win) worst_win = ndist;
    }
    fprintf(stderr, "[state_prof] census 9x9@(%d,%d): distinct states max %d/chunk, "
            "max %d/3x3-window; non-air sections mean %.1f max %d of 16\n",
            pcx, pcz, worst_chunk, worst_win, (double)sec_tot / nchunks, sec_max);
}

int gm_script_run(const GmConfig *cfg) {
    FILE *in = NULL, *out = stdout;
    if (cfg->script_path) { in = fopen(cfg->script_path, "r"); if (!in) { perror("script"); return 1; } }
    if (cfg->state_out_path) { out = fopen(cfg->state_out_path, "w"); if (!out) { perror("state-out"); if(in)fclose(in); return 1; } }
    GmRuntime r; char err[256];
    if (!gm_runtime_init(&r, cfg, err, sizeof err)) { fprintf(stderr,"runtime: %s\n",err); return 1; }
    /* Tape replay: never run live random-tick engine (oracle world RNG is
     * unseedable; terrain evolution is carried by snapshots, not re-simulated). */
    r.randtick_enabled = 0;
    GmFrameCapture *frames=NULL;
    GmWindowCompose *window_frames=NULL;
    GmParticlesLive replay_particles;
    uint64_t replay_particle_seed =
        (uint64_t)cfg->seed ^ UINT64_C(0x7061727469636c65);
    gm_particles_live_init(&replay_particles, replay_particle_seed);
    if(cfg->frames_out_dir){
        /* The oracle's frames carry the survival HUD (hearts/hunger/hotbar/
         * crosshair); headless frames must draw it too or every whole-frame
         * diff eats the missing overlay. gm_hud_draw silently no-ops until
         * gm_hud_init has run - the interactive path inits it, this script
         * path never did (that WAS the largest pixel cluster on the 12k-tape
         * poses: 2.1/ch of pose A's 3.42, 3.8/ch of pose B's 9.01). */
        gm_hud_init();
        if(cfg->compose==GM_COMPOSE_WINDOW){
            window_frames=gm_window_compose_open(cfg,err,sizeof err);
            if(window_frames)
                gm_window_compose_bind(window_frames,&r,&replay_particles);
        }else{
            frames=gm_frame_capture_open(cfg,err,sizeof err);
            if(frames)gm_frame_capture_bind_particles(frames,&replay_particles);
        }
        if(!frames&&!window_frames){fprintf(stderr,"frames-out: %s\n",err);gm_runtime_destroy(&r);if(in)fclose(in);if(out!=stdout)fclose(out);return 1;}
    }
    char line[2048] = {0}; long line_no = 0; JlObject pending; int have = 0;
    long long pending_tick = -1;
    /* Saturated FoodStats regeneration is server-side, but tape rows are
     * client ticks. Preserve an early local heal's hidden exhaustion/timer
     * effects while deferring its visible health until the recorded packet. */
    float held_regen = 0.0f;
    int continue_after_death = 0;
    /* MAGMA_STATE_PROF: per-tick world-edit rate. gm_world_block_gen counts
     * every block edit (set_block_meta + populate gen events), so its per-tick
     * delta = journal entries/tick for a dirty-edit journal. Baseline taken
     * here so worldgen's one-shot fill is excluded. */
    int prof_on = getenv("MAGMA_STATE_PROF") != NULL;
    int restore_only = getenv("MAGMA_RESTORE_ONLY") != NULL;
    long long prof_last = 0, prof_tot = 0, prof_max = 0, prof_maxt = -1, prof_nz = 0;
    long long prof_h[5] = {0};   /* buckets: 0, 1-8, 9-64, 65-512, 513+ */
    if (prof_on) prof_last = gm_world_block_gen(r.world);
    /* A tape can contain GuiGameOver followed by SPacketRespawn on the next
     * row. Keep consuming scripted events while dead; gm_runtime_tick itself
     * remains inert until an authoritative positive set_vitals revives it. */
    for (int tick = 0; tick < cfg->ticks && !r.won &&
         (!r.dead || continue_after_death); ++tick) {
        /* renderable ghost entities are per-tick state: last tick's recorded
         * entities must not linger into a tick whose tape row has none. */
        gm_runtime_ent_views_clear(&r);
        /* same for open GUI screen views (divergence #9). */
        gm_runtime_gui_view_clear(&r);
        GmAction action; memset(&action,0,sizeof action); action.hotbar_sel=-1;
        int have_look = 0; double look_yaw = 0.0, look_pitch = 0.0;
        int have_vitals_post = 0; double vitals_health = 20.0; long long vitals_food = 20;
        int have_regen_post = 0; double regen_health = 20.0, regen_exhaustion = 0.0;
        long long regen_food = 20;
        int have_hold_regen_post = 0;
        int clear_hurt_velocity_post = 0;
        int hold_fall_damage_post = 0;
        int have_food_stats_post = 0;
        double food_stats_saturation = 5.0, food_stats_exhaustion = 0.0;
        int have_pose_post = 0, pose_on_ground = 0;
        double pose_x = 0.0, pose_y = 0.0, pose_z = 0.0;
        double pose_yaw = 0.0, pose_pitch = 0.0;
        double pose_vx = 0.0, pose_vy = 0.0, pose_vz = 0.0, pose_fall = 0.0;
        for (;;) {
            if (!have && in && fgets(line,sizeof line,in)) {
                line_no++;
                err[0] = 0;
                if (!strchr(line,'\n') && !feof(in)) { fprintf(stderr,"script:%ld: line too long\n",line_no); goto bad; }
                char *nl=strchr(line,'\n'); if(nl)*nl=0;
                if (!parse_object(line,&pending,err,sizeof err) ||
                    !as_i64(field(&pending,"tick"),&pending_tick) || pending_tick < 0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid tick"); goto bad;
                }
                have=1;
            }
            if (!have || pending_tick > tick) break;
            if (pending_tick < tick) { fprintf(stderr,"script:%ld: events must be tick-sorted\n",line_no); goto bad; }
            const char *type;
            if (!as_string(field(&pending,"type"),&type)) { fprintf(stderr,"script:%ld: missing string type\n",line_no); goto bad; }
            if (!strcmp(type,"continue_after_death")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid continue_after_death\n",line_no);
                    goto bad;
                }
                continue_after_death=1;
            } else if (!strcmp(type,"action")) {
                if (!parse_action(&pending,&action,err,sizeof err)) { fprintf(stderr,"script:%ld: %s\n",line_no,err); goto bad; }
            } else if (!strcmp(type,"set_pose")) {
                double x,y,z,yaw,pitch;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch"};
                if (!keys_only(&pending,keys,7,err,sizeof err)||
                    !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                    !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"invalid set_pose"); goto bad;
                }
                gm_runtime_set_pose(&r,x,y,z,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_pose_state")) {
                double x,y,z,yaw,pitch,vx,vy,vz,fall;
                long long on_ground;
                static const char *const keys[]={"tick","type","x","y","z","yaw",
                    "pitch","vx","vy","vz","on_ground","fall"};
                if (!keys_only(&pending,keys,12,err,sizeof err)||
                    !as_double(field(&pending,"x"),&x)||
                    !as_double(field(&pending,"y"),&y)||
                    !as_double(field(&pending,"z"),&z)||
                    !as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)||
                    !as_double(field(&pending,"vx"),&vx)||
                    !as_double(field(&pending,"vy"),&vy)||
                    !as_double(field(&pending,"vz"),&vz)||
                    !as_i64(field(&pending,"on_ground"),&on_ground)||
                    (on_ground!=0&&on_ground!=1)||
                    !as_double(field(&pending,"fall"),&fall)||fall<0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,
                            err[0]?err:"invalid set_pose_state");
                    goto bad;
                }
                gm_runtime_set_pose_state(&r,x,y,z,(float)yaw,(float)pitch,
                                          vx,vy,vz,(int)on_ground,(float)fall);
            } else if (!strcmp(type,"set_look")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"yaw"),&yaw)||
                    !as_double(field(&pending,"pitch"),&pitch)) {
                    fprintf(stderr,"script:%ld: invalid set_look\n",line_no); goto bad;
                }
                /* DEFERRED to after gm_runtime_tick: the tape records yaw/pitch
                 * POST-tick (the qrl bridge applies the quantized turn after the
                 * tick's physics, before recordTick; mouse look likewise lands
                 * between ticks). Tick t's move must run with the PREVIOUS look;
                 * the new look takes effect for state/frame capture at t and for
                 * tick t+1's physics. Found at t371 of the fresh-world tape: a
                 * mid-walk 15-degree turn accelerated magma along the new yaw
                 * one tick early (accel fit: oracle 0.070711 = yaw 0, magma
                 * 0.086603 = yaw -15). */
                have_look = 1; look_yaw = yaw; look_pitch = pitch;
            } else if (!strcmp(type,"set_look_pre")) {
                double yaw,pitch;
                static const char *const keys[]={"tick","type","yaw","pitch"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)){
                    fprintf(stderr,"script:%ld: invalid set_look_pre\n",line_no);goto bad;
                }
                gm_runtime_set_look(&r,(float)yaw,(float)pitch);
            } else if (!strcmp(type,"set_pose_post")) {
                long long og;
                static const char *const keys[]={"tick","type","x","y","z","yaw","pitch",
                    "vx","vy","vz","on_ground","fall"};
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_double(field(&pending,"x"),&pose_x)||
                   !as_double(field(&pending,"y"),&pose_y)||
                   !as_double(field(&pending,"z"),&pose_z)||
                   !as_double(field(&pending,"yaw"),&pose_yaw)||
                   !as_double(field(&pending,"pitch"),&pose_pitch)||
                   !as_double(field(&pending,"vx"),&pose_vx)||
                   !as_double(field(&pending,"vy"),&pose_vy)||
                   !as_double(field(&pending,"vz"),&pose_vz)||
                   !as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)||
                   !as_double(field(&pending,"fall"),&pose_fall)||pose_fall<0){
                    fprintf(stderr,"script:%ld: invalid set_pose_post\n",line_no);goto bad;
                }
                pose_on_ground=(int)og;have_pose_post=1;
            } else if (!strcmp(type,"set_vitals")) {
                double health; long long food;
                static const char *const keys[]={"tick","type","health","food"};
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_double(field(&pending,"health"),&health)||
                    !as_i64(field(&pending,"food"),&food)||
                    health<0||health>1024||food<0||food>20
                    || health>(double)r.vitals.maxHealth+1e-6) {
                    fprintf(stderr,"script:%ld: invalid set_vitals\n",line_no); goto bad;
                }
                gm_runtime_set_vitals(&r,(float)health,(int)food);
                held_regen=0.0f;
            } else if (!strcmp(type,"set_vitals_post")) {
                static const char *const keys[]={"tick","type","health","food"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"health"),&vitals_health)||
                   !as_i64(field(&pending,"food"),&vitals_food)||
                   vitals_health<0||vitals_health>20||vitals_food<0||vitals_food>20){
                    fprintf(stderr,"script:%ld: invalid set_vitals_post\n",line_no);goto bad;
                }
                have_vitals_post=1;
            } else if (!strcmp(type,"set_regen_post")) {
                static const char *const keys[]={"tick","type","health","food","exhaustion"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"health"),&regen_health)||
                   !as_i64(field(&pending,"food"),&regen_food)||
                   !as_double(field(&pending,"exhaustion"),&regen_exhaustion)||
                   regen_health<0||regen_health>20||regen_food<0||regen_food>20||
                   regen_exhaustion<0||regen_exhaustion>6){
                    fprintf(stderr,"script:%ld: invalid set_regen_post\n",line_no);goto bad;
                }
                have_regen_post=1;
            } else if (!strcmp(type,"hold_regen_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_regen_post\n",line_no);goto bad;
                }
                have_hold_regen_post=1;
            } else if (!strcmp(type,"clear_hurt_velocity_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid clear_hurt_velocity_post\n",line_no);goto bad;
                }
                clear_hurt_velocity_post=1;
            } else if (!strcmp(type,"hold_fall_damage_post")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid hold_fall_damage_post\n",line_no);goto bad;
                }
                hold_fall_damage_post=1;
            } else if (!strcmp(type,"set_food_stats_post")) {
                static const char *const keys[]={"tick","type","saturation","exhaustion"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"saturation"),&food_stats_saturation)||
                   !as_double(field(&pending,"exhaustion"),&food_stats_exhaustion)||
                   food_stats_saturation<0||food_stats_saturation>20||
                   food_stats_exhaustion<0||food_stats_exhaustion>40){
                    fprintf(stderr,"script:%ld: invalid set_food_stats_post\n",line_no);goto bad;
                }
                have_food_stats_post=1;
            } else if (!strcmp(type,"set_food_stats")) {
                double saturation,exhaustion;
                static const char *const keys[]={"tick","type","saturation","exhaustion"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_double(field(&pending,"saturation"),&saturation)||
                   !as_double(field(&pending,"exhaustion"),&exhaustion)||
                   saturation<0||saturation>20||exhaustion<0||exhaustion>40){
                    fprintf(stderr,"script:%ld: invalid set_food_stats\n",line_no);goto bad;
                }
                gm_runtime_set_food_stats(&r,(float)saturation,(float)exhaustion);
            } else if (!strcmp(type,"set_food_timer")) {
                long long timer;
                static const char *const keys[]={"tick","type","timer"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"timer"),&timer)||
                   !gm_runtime_set_food_timer(&r,(int)timer)) {
                    fprintf(stderr,"script:%ld: invalid set_food_timer\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_xp")) {
                long long level,total; double fraction;
                static const char *const keys[]={
                    "tick","type","level","fraction","total"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"level"),&level)||
                   !as_double(field(&pending,"fraction"),&fraction)||
                   !as_i64(field(&pending,"total"),&total)||
                   level>INT_MAX||total>INT_MAX||
                   !gm_runtime_set_player_xp(
                       &r,(int)level,(float)fraction,(int)total)) {
                    fprintf(stderr,"script:%ld: invalid set_player_xp\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_combat")) {
                long long attack,hurt,resistant,death,dead,deaths;
                static const char *const keys[]={
                    "tick","type","attack_ticks","hurt_time",
                    "hurt_resistant_time","death_time","dead","deaths"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"attack_ticks"),&attack)||
                   !as_i64(field(&pending,"hurt_time"),&hurt)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&resistant)||
                   !as_i64(field(&pending,"death_time"),&death)||
                   !as_i64(field(&pending,"dead"),&dead)||
                   !as_i64(field(&pending,"deaths"),&deaths)||
                   !gm_runtime_set_player_combat(
                       &r,(int)attack,(int)hurt,(int)resistant,
                       (int)death,(int)dead,(int)deaths)) {
                    fprintf(stderr,"script:%ld: invalid set_player_combat\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_absorption")) {
                double value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_double(field(&pending,"value"),&value)||
                   !gm_runtime_set_player_absorption(&r,(float)value)) {
                    fprintf(stderr,"script:%ld: invalid set_player_absorption\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_position_update_ticks")) {
                long long value,queued;
                static const char *const keys[]={
                    "tick","type","value","pending"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   !as_i64(field(&pending,"pending"),&queued)||
                   !gm_runtime_set_position_update_ticks(
                       &r,(int)value,(int)queued)) {
                    fprintf(stderr,
                            "script:%ld: invalid set_position_update_ticks\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_dimension")) {
                long long dimension;
                static const char *const keys[]={"tick","type","dimension"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !gm_runtime_set_dimension(&r,(int)dimension)){
                    fprintf(stderr,"script:%ld: invalid set_dimension\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_velocity")) {
                /* optional on_ground: a mid-session tape can start with
                 * residual motion while standing - first-tick friction is
                 * 0.546 on ground vs 0.91 airborne, and a fresh player
                 * defaults to airborne, so tick 0 diverges in vx without it. */
                double x,y,z; long long og=-1;
                static const char *const keys[]={"tick","type","x","y","z","on_ground"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   (field(&pending,"on_ground")&&
                    (!as_i64(field(&pending,"on_ground"),&og)||(og!=0&&og!=1)))){
                    fprintf(stderr,"script:%ld: invalid set_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_velocity(&r,x,y,z);
                if(og>=0)r.player.ent.onGround=(int)og;
            } else if (!strcmp(type,"set_packet_velocity")) {
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid set_packet_velocity\n",line_no);goto bad;
                }
                gm_runtime_set_packet_velocity(&r,x,y,z);
            } else if (!strcmp(type,"add_velocity")) {
                /* SPacketExplosion knockback: handleExplosion ADDS the
                 * packet motion to the local player, unlike pvel. */
                double x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)){
                    fprintf(stderr,"script:%ld: invalid add_velocity\n",line_no);goto bad;
                }
                gm_runtime_add_velocity(&r,x,y,z);
            } else if (!strcmp(type,"ent_box")) {
                /* Tape replay ghost pusher: recorded oracle entity box (world
                 * coords, feet y, width, height) applied as a vanilla
                 * applyEntityCollision player push during this tick. */
                double x,y,z,w,h;
                static const char *const keys[]={"tick","type","x","y","z","w","h"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"w"),&w)||
                   !as_double(field(&pending,"h"),&h)||w<=0||h<=0){
                    fprintf(stderr,"script:%ld: invalid ent_box\n",line_no);goto bad;
                }
                gm_runtime_ent_box(&r,x,y,z,w,h);
            } else if (!strcmp(type,"dragon_contact")) {
                /* Recorded EntityDragon part query. Wing boxes carry 5 damage
                 * (collideWithEntities); head/neck boxes carry 10
                 * (attackEntitiesInList). Damage lands before FoodStats.onUpdate. */
                double x0,y0,z0,x1,y1,z1,damage;
                static const char *const keys[]={"tick","type","min_x","min_y","min_z",
                    "max_x","max_y","max_z","damage"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_double(field(&pending,"min_x"),&x0)||
                   !as_double(field(&pending,"min_y"),&y0)||
                   !as_double(field(&pending,"min_z"),&z0)||
                   !as_double(field(&pending,"max_x"),&x1)||
                   !as_double(field(&pending,"max_y"),&y1)||
                   !as_double(field(&pending,"max_z"),&z1)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid dragon_contact\n",line_no);goto bad;
                }
                (void)gm_runtime_dragon_contact(&r,x0,y0,z0,x1,y1,z1,(float)damage);
            } else if (!strcmp(type,"mob_damage")) {
                double damage;
                static const char *const keys[]={"tick","type","damage"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_double(field(&pending,"damage"),&damage)||damage<=0){
                    fprintf(stderr,"script:%ld: invalid mob_damage\n",line_no);goto bad;
                }
                (void)gm_mobs_attack_player(&r.mobs,
                    (struct PvStats *)&r.vitals, &r.player.inv,
                    (float)damage, 0);
                r.player.health=r.vitals.health;
            } else if (!strcmp(type,"ent_view")) {
                /* Tape replay renderable ghost entity (divergence #10): the
                 * recorded oracle entity is drawn by frame capture through
                 * the live-entity model path. Render-only; the physics-push
                 * ghost stays the separate ent_box event above. Types with
                 * no magma model are skipped (logged once per type). */
                const char *ent;double x,y,z,yaw,hp=-1.0,d;
                long long eid=-1,n;
                static const char *const keys[]={"tick","type","ent","x","y","z","yaw","hp","id",
                    "tape_pose","head_yaw","pitch","swing","hurt","death","body_yaw","flags",
                    "sheared","fleece","graze_y","graze_x","item","item_meta","count","age",
                    "hover","has_hover","crystal_rot","show_bottom","has_beam","beam_x","beam_y","beam_z",
                    "anim_time","death_ticks","phase_id","stationary",
                    "ticks_existed","has_heal_beam","heal_x","heal_y","heal_z","heal_crystal_ticks",
                    /* EntityXPOrb: item=xpValue, item_meta=xpColor, age=xpOrbAge */
                    "xp_value","xp_color",
                    /* EntityArmorStand equipment + saved display flags. */
                    "armor_feet","armor_legs","armor_chest","armor_head",
                    "armor_feet_meta","armor_legs_meta","armor_chest_meta",
                    "armor_head_meta",
                    "armor_color_valid","armor_color_0","armor_color_1",
                    "armor_color_2","armor_color_3",
                    "stand_mainhand","stand_offhand","stand_mainhand_meta",
                    "stand_offhand_meta","stand_flags",
                    "stand_pose_valid","stand_punch_time",
                    "stand_pose_0_x","stand_pose_0_y","stand_pose_0_z",
                    "stand_pose_1_x","stand_pose_1_y","stand_pose_1_z",
                    "stand_pose_2_x","stand_pose_2_y","stand_pose_2_z",
                    "stand_pose_3_x","stand_pose_3_y","stand_pose_3_z",
                    "stand_pose_4_x","stand_pose_4_y","stand_pose_4_z",
                    "stand_pose_5_x","stand_pose_5_y","stand_pose_5_z",
                    "wither_invul","wither_head_yaw_0","wither_head_pitch_0",
                    "wither_head_yaw_1","wither_head_pitch_1",
                    "wither_skull_invulnerable"};
                if(!keys_only(&pending,keys,
                              (int)(sizeof keys / sizeof keys[0]),err,sizeof err)||
                   !as_string(field(&pending,"ent"),&ent)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||!as_double(field(&pending,"yaw"),&yaw)||
                   (field(&pending,"hp")&&!as_double(field(&pending,"hp"),&hp))||
                   (field(&pending,"id")&&!as_i64(field(&pending,"id"),&eid))){
                    fprintf(stderr,"script:%ld: invalid ent_view\n",line_no);goto bad;
                }
                GmEntityView view;memset(&view,0,sizeof view);
                view.type=!strcmp(ent,"EntityItem")&&field(&pending,"item")
                    ?GM_VIEW_ITEM:gm_entity_type_for_name(ent);
                /* EntityFallingBlock: fallTile id/meta travel as item/item_meta
                 * (RenderFallingBlock). Legacy 7-field rows have no state; NBT
                 * load default is sand (EntityFallingBlock.java:328). */
                if(view.type==GM_VIEW_FALLING_BLOCK&&!field(&pending,"item")){
                    view.item_id=12; /* Blocks.SAND */
                    view.item_meta=0;
                }
                view.skin=gm_entity_skin_for_name(ent);
                if(view.type==GM_VIEW_BILLBOARD||
                   view.type==GM_VIEW_DRAGON_FIREBALL)
                    view.item_id=gm_entity_billboard_item(ent);
                if(!strcmp(ent,"EntityLargeFireball"))
                    view.item_meta=2; /* RenderFireball scale 2 + large fire layers */
                view.x=(float)x;view.y=(float)y;view.z=(float)z;view.yaw=(float)yaw;
                view.health=(float)hp;view.ent_id=(int)eid;
                if (view.type == EW_TYPE_BOAT)
                    gm_runtime_tape_boat_view(&r, (int)eid, x, y, z, yaw);
#define OPT_I64(K,DST) do{if(field(&pending,K)){if(!as_i64(field(&pending,K),&n)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(int)n;}}while(0)
#define OPT_DBL(K,DST) do{if(field(&pending,K)){if(!as_double(field(&pending,K),&d)){fprintf(stderr,"script:%ld: invalid ent_view %s\n",line_no,K);goto bad;}DST=(float)d;}}while(0)
                OPT_I64("tape_pose",view.tape_pose);OPT_DBL("head_yaw",view.head_yaw);
                OPT_DBL("pitch",view.pitch);OPT_DBL("swing",view.swing_progress);
                OPT_I64("hurt",view.hurt_time);OPT_I64("death",view.death_time);
                OPT_DBL("body_yaw",view.yaw);OPT_I64("flags",view.flags);
                OPT_I64("sheared",view.sheared);OPT_I64("fleece",view.fleece_color);
                OPT_DBL("graze_y",view.graze_y);OPT_DBL("graze_x",view.graze_x);
                OPT_I64("item",view.item_id);OPT_I64("item_meta",view.item_meta);
                OPT_I64("count",view.item_count);OPT_I64("age",view.age);
                /* Explicit XP orb aliases (same fields as item/item_meta). */
                OPT_I64("xp_value",view.item_id);OPT_I64("xp_color",view.item_meta);
                OPT_DBL("hover",view.hover_start);OPT_I64("has_hover",view.has_hover_start);
                view.beam_x=view.beam_y=view.beam_z=-1;
                OPT_DBL("crystal_rot",view.crystal_rot);OPT_I64("show_bottom",view.show_bottom);
                OPT_I64("beam_x",view.beam_x);OPT_I64("beam_y",view.beam_y);
                OPT_I64("beam_z",view.beam_z);
                view.has_beam=!(view.beam_x==-1&&view.beam_y==-1&&view.beam_z==-1);
                OPT_I64("has_beam",view.has_beam);
                OPT_DBL("anim_time",view.anim_time);OPT_I64("death_ticks",view.death_ticks);
                OPT_I64("phase_id",view.phase_id);OPT_I64("stationary",view.stationary);
                OPT_I64("ticks_existed",view.ticks_existed);
                OPT_I64("armor_feet",view.armor_feet);OPT_I64("armor_legs",view.armor_legs);
                OPT_I64("armor_chest",view.armor_chest);OPT_I64("armor_head",view.armor_head);
                OPT_I64("armor_feet_meta",view.armor_feet_meta);
                OPT_I64("armor_legs_meta",view.armor_legs_meta);
                OPT_I64("armor_chest_meta",view.armor_chest_meta);
                OPT_I64("armor_head_meta",view.armor_head_meta);
                OPT_I64("armor_color_valid",view.armor_color_valid);
                OPT_I64("armor_color_0",view.armor_color[0]);
                OPT_I64("armor_color_1",view.armor_color[1]);
                OPT_I64("armor_color_2",view.armor_color[2]);
                OPT_I64("armor_color_3",view.armor_color[3]);
                OPT_I64("stand_mainhand",view.stand_mainhand);
                OPT_I64("stand_offhand",view.stand_offhand);
                OPT_I64("stand_mainhand_meta",view.stand_mainhand_meta);
                OPT_I64("stand_offhand_meta",view.stand_offhand_meta);
                OPT_I64("stand_flags",view.stand_flags);
                OPT_I64("stand_pose_valid",view.stand_pose_valid);
                if(field(&pending,"stand_punch_time"))
                    view.stand_punch_time_valid=1;
                OPT_DBL("stand_punch_time",view.stand_punch_time);
                OPT_DBL("stand_pose_0_x",view.stand_pose[0][0]);
                OPT_DBL("stand_pose_0_y",view.stand_pose[0][1]);
                OPT_DBL("stand_pose_0_z",view.stand_pose[0][2]);
                OPT_DBL("stand_pose_1_x",view.stand_pose[1][0]);
                OPT_DBL("stand_pose_1_y",view.stand_pose[1][1]);
                OPT_DBL("stand_pose_1_z",view.stand_pose[1][2]);
                OPT_DBL("stand_pose_2_x",view.stand_pose[2][0]);
                OPT_DBL("stand_pose_2_y",view.stand_pose[2][1]);
                OPT_DBL("stand_pose_2_z",view.stand_pose[2][2]);
                OPT_DBL("stand_pose_3_x",view.stand_pose[3][0]);
                OPT_DBL("stand_pose_3_y",view.stand_pose[3][1]);
                OPT_DBL("stand_pose_3_z",view.stand_pose[3][2]);
                OPT_DBL("stand_pose_4_x",view.stand_pose[4][0]);
                OPT_DBL("stand_pose_4_y",view.stand_pose[4][1]);
                OPT_DBL("stand_pose_4_z",view.stand_pose[4][2]);
                OPT_DBL("stand_pose_5_x",view.stand_pose[5][0]);
                OPT_DBL("stand_pose_5_y",view.stand_pose[5][1]);
                OPT_DBL("stand_pose_5_z",view.stand_pose[5][2]);
                OPT_I64("wither_invul",view.wither_invul_time);
                OPT_DBL("wither_head_yaw_0",view.wither_head_yaw[0]);
                OPT_DBL("wither_head_pitch_0",view.wither_head_pitch[0]);
                OPT_DBL("wither_head_yaw_1",view.wither_head_yaw[1]);
                OPT_DBL("wither_head_pitch_1",view.wither_head_pitch[1]);
                OPT_I64("wither_skull_invulnerable",
                        view.wither_skull_invulnerable);
                OPT_I64("has_heal_beam",view.has_heal_beam);
                OPT_DBL("heal_x",view.heal_x);OPT_DBL("heal_y",view.heal_y);
                OPT_DBL("heal_z",view.heal_z);
                OPT_I64("heal_crystal_ticks",view.heal_crystal_ticks);
#undef OPT_I64
#undef OPT_DBL
                int vt=view.type;
                if(vt<0){
                    static char warned[16][JL_VALUE];static int nwarned=0;
                    int seen=0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],ent)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",ent);
                        fprintf(stderr,"script: ent_view %s: no model, skipped\n",ent);
                    }
                }else if(view.item_id<0||
                         (view.item_id>4095&&
                          !(view.type==GM_VIEW_DRAGON_FIREBALL&&
                            view.item_id==9003))||view.item_meta<0||
                         view.item_meta>32767||view.item_count<0||view.item_count>64||
                         view.fleece_color<0||view.fleece_color>15||
                         (view.flags&~((view.type==EW_TYPE_LLAMA
                            ? (15|8192) : 15)|GM_ENTITY_FLAG_GLOWING))||
                         view.hurt_time<0||view.death_time<0){
                    fprintf(stderr,"script:%ld: invalid ent_view state\n",line_no);goto bad;
                }else if(view.armor_feet<0||view.armor_feet>4095||
                         view.armor_legs<0||view.armor_legs>4095||
                         view.armor_chest<0||view.armor_chest>4095||
                         view.armor_head<0||view.armor_head>4095||
                         view.stand_mainhand<0||view.stand_mainhand>4095||
                         view.stand_offhand<0||view.stand_offhand>4095||
                         view.armor_feet_meta<0||view.armor_feet_meta>32767||
                         view.armor_legs_meta<0||view.armor_legs_meta>32767||
                         view.armor_chest_meta<0||view.armor_chest_meta>32767||
                         view.armor_head_meta<0||view.armor_head_meta>32767||
                         view.stand_mainhand_meta<0||
                            view.stand_mainhand_meta>32767||
                         view.stand_offhand_meta<0||
                            view.stand_offhand_meta>32767||
                         (view.armor_color_valid&~15)||
                         view.armor_color[0]<0||view.armor_color[0]>0xffffff||
                         view.armor_color[1]<0||view.armor_color[1]>0xffffff||
                         view.armor_color[2]<0||view.armor_color[2]>0xffffff||
                         view.armor_color[3]<0||view.armor_color[3]>0xffffff||
                         (view.stand_flags&~7)||
                         (view.stand_pose_valid!=0&&
                          view.stand_pose_valid!=1)){
                    fprintf(stderr,"script:%ld: invalid armor stand state\n",line_no);goto bad;
                }else if(view.wither_invul_time<0||
                         (view.wither_skull_invulnerable!=0&&
                          view.wither_skull_invulnerable!=1)){
                    fprintf(stderr,"script:%ld: invalid Wither render state\n",line_no);goto bad;
                }else gm_runtime_ent_view(&r,&view);
            } else if (!strcmp(type,"gui_view")) {
                /* Tape replay open container GUI (divergence #9): draw-only.
                 * Maps vanilla GuiScreen simple name -> container kind; mx/my
                 * are ScaledResolution coords (converted to fb px at draw).
                 * Unmapped screens (pause, chat, ...) are logged once + skipped. */
                const char *gui; long long mx = -1, my = -1;
                static const char *const keys[]={"tick","type","gui","mx","my"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_string(field(&pending,"gui"),&gui)||
                   (field(&pending,"mx")&&!as_i64(field(&pending,"mx"),&mx))||
                   (field(&pending,"my")&&!as_i64(field(&pending,"my"),&my))){
                    fprintf(stderr,"script:%ld: invalid gui_view\n",line_no);goto bad;
                }
                int kind = gm_screen_kind_for_gui(gui);
                if(kind < 0){
                    static char warned[16][JL_VALUE]; static int nwarned = 0;
                    int seen = 0;
                    for(int i=0;i<nwarned;++i)if(!strcmp(warned[i],gui)){seen=1;break;}
                    if(!seen&&nwarned<16){
                        snprintf(warned[nwarned++],JL_VALUE,"%s",gui);
                        fprintf(stderr,"script: gui_view %s: no container screen, skipped\n",gui);
                    }
                }else{
                    /* default mouse to gui-space center when gmx/gmy absent */
                    if(mx < 0 || my < 0){
                        int s = gm_screen_gui_scale(cfg->height > 0 ? cfg->height : 480);
                        int gw = ((cfg->width > 0 ? cfg->width : 854) + s - 1) / s;
                        int gh = ((cfg->height > 0 ? cfg->height : 480) + s - 1) / s;
                        if(mx < 0) mx = gw / 2;
                        if(my < 0) my = gh / 2;
                    }
                    gm_runtime_gui_view(&r, kind, (int)mx, (int)my);
                }
            } else if (!strcmp(type,"gui_slot_view") || !strcmp(type,"gui_cursor_view")) {
                /* Optional StoredEnchantments subset: n_ench + e0..e7 packed as
                 * (id<<16)|level. Absent => n_enchants=0 (backward compatible). */
                long long slot=0,item,count,meta,n_ench=0;
                int is_slot = !strcmp(type,"gui_slot_view");
                static const char *const keys_slot[]={
                    "tick","type","slot","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                static const char *const keys_cur[]={
                    "tick","type","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7"
                };
                ICStack st;
                if (is_slot) {
                    if(!keys_only(&pending,keys_slot,15,err,sizeof err)||
                       !as_i64(field(&pending,"slot"),&slot)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else {
                    if(!keys_only(&pending,keys_cur,14,err,sizeof err)||
                       !as_i64(field(&pending,"item"),&item)||
                       !as_i64(field(&pending,"count"),&count)||
                       !as_i64(field(&pending,"meta"),&meta)){
                        fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                    }
                }
                st = count == 0 ? ic_empty() : ic_mk((i32)item,(i32)count,(i32)meta);
                if (field(&pending,"n_ench")) {
                    char ek[4];
                    int ei;
                    if (!as_i64(field(&pending,"n_ench"),&n_ench) ||
                        n_ench < 0 || n_ench > IC_MAX_ENCHANTS) {
                        fprintf(stderr,"script:%ld: invalid n_ench\n",line_no);goto bad;
                    }
                    st.n_enchants = (i32)n_ench;
                    for (ei = 0; ei < (int)n_ench; ++ei) {
                        long long packed = 0;
                        snprintf(ek, sizeof ek, "e%d", ei);
                        if (!as_i64(field(&pending, ek), &packed)) {
                            fprintf(stderr,"script:%ld: missing %s\n",line_no,ek);goto bad;
                        }
                        st.enchants[ei].id = (i16)((packed >> 16) & 0xffff);
                        st.enchants[ei].level = (i16)(packed & 0xffff);
                    }
                }
                if (is_slot) {
                    if (!gm_runtime_tape_gui_slot_stack(&r,(int)slot,st)) {
                        fprintf(stderr,"script:%ld: invalid gui_slot_view\n",line_no);goto bad;
                    }
                } else if (!gm_runtime_tape_gui_cursor_stack(&r,st)) {
                    fprintf(stderr,"script:%ld: invalid gui_cursor_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_furnace_view")) {
                long long burn,current,cook,total;
                static const char *const keys[]={"tick","type","burn","current_burn","cook","total_cook"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"burn"),&burn)||
                   !as_i64(field(&pending,"current_burn"),&current)||
                   !as_i64(field(&pending,"cook"),&cook)||
                   !as_i64(field(&pending,"total_cook"),&total)||
                   burn>2147483647LL||current>2147483647LL||
                   cook>2147483647LL||total>2147483647LL||
                   !gm_runtime_tape_furnace(&r,(int)burn,(int)current,(int)cook,(int)total)){
                    fprintf(stderr,"script:%ld: invalid gui_furnace_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_brewing_view")) {
                long long brew,fuel;
                static const char *const keys[]={
                    "tick","type","brew","fuel"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"brew"),&brew)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   !gm_runtime_tape_brewing(
                       &r,(int)brew,(int)fuel)){
                    fprintf(
                        stderr,"script:%ld: invalid gui_brewing_view\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"gui_merchant_view")) {
                long long selected,offers,disabled;
                long long ai,ac,am,bi,bc,bm,si,sc,sm;
                static const char *const keys[]={
                    "tick","type","selected","offers","disabled",
                    "a_item","a_count","a_meta","b_item","b_count",
                    "b_meta","sell_item","sell_count","sell_meta"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"selected"),&selected)||
                   !as_i64(field(&pending,"offers"),&offers)||
                   !as_i64(field(&pending,"disabled"),&disabled)||
                   !as_i64(field(&pending,"a_item"),&ai)||
                   !as_i64(field(&pending,"a_count"),&ac)||
                   !as_i64(field(&pending,"a_meta"),&am)||
                   !as_i64(field(&pending,"b_item"),&bi)||
                   !as_i64(field(&pending,"b_count"),&bc)||
                   !as_i64(field(&pending,"b_meta"),&bm)||
                   !as_i64(field(&pending,"sell_item"),&si)||
                   !as_i64(field(&pending,"sell_count"),&sc)||
                   !as_i64(field(&pending,"sell_meta"),&sm)||
                   selected<0||selected>INT_MAX||offers<1||offers>INT_MAX||
                   disabled<0||disabled>1||
                   ai<0||ai>4095||ac<0||ac>64||am<0||am>32767||
                   bi<0||bi>4095||bc<0||bc>64||bm<0||bm>32767||
                   si<0||si>4095||sc<1||sc>64||sm<0||sm>32767||
                   !gm_runtime_tape_merchant(
                       &r,(int)selected,(int)offers,(int)disabled,
                       ac ? ic_mk((int)ai,(int)ac,(int)am) : ic_empty(),
                       bc ? ic_mk((int)bi,(int)bc,(int)bm) : ic_empty(),
                       sc ? ic_mk((int)si,(int)sc,(int)sm) : ic_empty())){
                    fprintf(stderr,
                        "script:%ld: invalid gui_merchant_view\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"set_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_time\n",line_no);goto bad;
                }
                gm_runtime_set_time(&r,value);
            } else if (!strcmp(type,"set_total_time")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||value<0){
                    fprintf(stderr,"script:%ld: invalid set_total_time\n",line_no);goto bad;
                }
                gm_runtime_set_total_time(&r,value);
            } else if (!strcmp(type,"set_world_spawn")) {
                long long x,y,z;
                static const char *const keys[]={
                    "tick","type","x","y","z"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX){
                    fprintf(stderr,"script:%ld: invalid set_world_spawn\n",
                            line_no);goto bad;
                }
                r.world_spawn_x=(int)x;
                r.world_spawn_y=(int)y;
                r.world_spawn_z=(int)z;
            } else if (!strcmp(type,"restore_lightning")) {
                long long dimension,eid,ticks,state,living,effect,vertex,seed;
                double x,y,z;
                static const char *const keys[]={
                    "tick","type","dimension","eid","x","y","z",
                    "ticks_existed","lightning_state","living_time",
                    "effect_only","bolt_vertex","entity_seed48"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks)||
                   !as_i64(field(&pending,"lightning_state"),&state)||
                   !as_i64(field(&pending,"living_time"),&living)||
                   !as_i64(field(&pending,"effect_only"),&effect)||
                   !as_i64(field(&pending,"bolt_vertex"),&vertex)||
                   !as_i64(field(&pending,"entity_seed48"),&seed)||
                   !gm_runtime_restore_lightning(
                       &r,(int)dimension,(int)eid,(int)ticks,(int)state,
                       (int)living,(int)effect,vertex,(uint64_t)seed,x,y,z)){
                    fprintf(stderr,"script:%ld: invalid restore_lightning\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"redstone_torch_toggle")) {
                long long x,y,z,time;
                static const char *const keys[]={
                    "tick","type","x","y","z","time"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"time"),&time)||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   !gm_runtime_redstone_torch_toggle_add(
                       &r,(int)x,(int)y,(int)z,time)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid redstone_torch_toggle\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_comparator_output")) {
                long long dimension,x,y,z,output;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","output_signal"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"output_signal"),&output)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||output<0||output>15||
                   !gm_runtime_comparator_set_output(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)output)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_comparator_output\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"load_moving_piston")) {
                long long dimension,x,y,z,moved_block,moved_meta,facing;
                long long extending,source,progress_bits,last_progress_bits;
                union { unsigned u; float f; } progress,last_progress;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "moved_block","moved_meta","facing",
                    "extending","source","progress_bits",
                    "last_progress_bits"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"moved_block"),&moved_block)||
                   !as_i64(field(&pending,"moved_meta"),&moved_meta)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"extending"),&extending)||
                   !as_i64(field(&pending,"source"),&source)||
                   !as_i64(field(&pending,"progress_bits"),&progress_bits)||
                   !as_i64(field(&pending,"last_progress_bits"),
                           &last_progress_bits)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   moved_block<1||moved_block>4095||
                   moved_meta<0||moved_meta>15||facing<0||facing>5||
                   extending<0||extending>1||source<0||source>1||
                   progress_bits<0||progress_bits>0xffffffffLL||
                   last_progress_bits<0||last_progress_bits>0xffffffffLL){
                    fprintf(stderr,
                            "script:%ld: invalid load_moving_piston\n",
                            line_no);goto bad;
                }
                progress.u=(unsigned)progress_bits;
                last_progress.u=(unsigned)last_progress_bits;
                if(!gm_runtime_moving_piston_load(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)moved_block,(int)moved_meta,(int)facing,
                       (int)extending,(int)source,
                       progress.f,last_progress.f)){
                    fprintf(stderr,
                            "script:%ld: invalid load_moving_piston\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_chest_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                const JlField *stack_nbt_file=field(&pending,"stack_nbt_file");
                uint8_t *stack_tag_nbt=NULL;
                size_t stack_tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta","stack_nbt_file"
                };
                if(!keys_only(&pending,keys,11,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=CHEST_LIVE_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   (stack_nbt_file&&(!stack_nbt_file->string||count==0||
                       !read_capsule_nbt(stack_nbt_file->value,
                           &stack_tag_nbt,&stack_tag_nbt_len)))||
                   !gm_runtime_chest_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta)||
                   (stack_nbt_file&&!gm_runtime_container_set_stack_tag(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)slot,
                       stack_tag_nbt,stack_tag_nbt_len))){
                    free(stack_tag_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_chest_slot\n",
                        line_no);goto bad;
                }
                free(stack_tag_nbt);
            } else if (!strcmp(type,"restore_chest_transient")) {
                long long dimension,x,y,z,viewers,lid_bits,prev_bits,sync;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "num_players_using","lid_angle_bits",
                    "prev_lid_angle_bits","ticks_since_sync"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"num_players_using"),&viewers)||
                   !as_i64(field(&pending,"lid_angle_bits"),&lid_bits)||
                   !as_i64(field(&pending,"prev_lid_angle_bits"),&prev_bits)||
                   !as_i64(field(&pending,"ticks_since_sync"),&sync)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||viewers<0||viewers>INT_MAX||
                   lid_bits<0||lid_bits>0xffffffffLL||
                   prev_bits<0||prev_bits>0xffffffffLL||
                   sync<INT_MIN||sync>INT_MAX||
                   !gm_runtime_chest_set_transient(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)viewers,(uint32_t)lid_bits,
                       (uint32_t)prev_bits,(int)sync)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_chest_transient\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_furnace_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                long long burn,current_burn,cook,total_cook;
                const char *custom_name=NULL;
                const JlField *stack_nbt_file=field(&pending,"stack_nbt_file");
                uint8_t *stack_tag_nbt=NULL;
                size_t stack_tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta",
                    "burn_time","current_burn_time",
                    "cook_time","total_cook_time","custom_name",
                    "stack_nbt_file"
                };
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"burn_time"),&burn)||
                   !as_i64(
                       field(&pending,"current_burn_time"),&current_burn)||
                   !as_i64(field(&pending,"cook_time"),&cook)||
                   !as_i64(field(&pending,"total_cook_time"),&total_cook)||
                   (field(&pending,"custom_name")&&
                    !as_string(field(&pending,"custom_name"),&custom_name))||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=FURNACE_LIVE_SLOT_COUNT||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   burn<0||burn>32767||
                   current_burn<0||current_burn>32767||
                   cook<0||cook>32767||
                   total_cook<0||total_cook>32767||
                   (stack_nbt_file&&(!stack_nbt_file->string||count==0||
                       !read_capsule_nbt(stack_nbt_file->value,
                           &stack_tag_nbt,&stack_tag_nbt_len)))||
                   !gm_runtime_furnace_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta,
                       (int)burn,(int)current_burn,
                       (int)cook,(int)total_cook)||
                   (custom_name&&!gm_runtime_furnace_set_custom_name(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       custom_name))||
                   (stack_nbt_file&&!gm_runtime_container_set_stack_tag(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)slot,
                       stack_tag_nbt,stack_tag_nbt_len))){
                    free(stack_tag_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_furnace_slot\n",
                        line_no);goto bad;
                }
                free(stack_tag_nbt);
            } else if (!strcmp(type,"set_brewing_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                long long brew_time,fuel;
                const JlField *stack_nbt_file=field(&pending,"stack_nbt_file");
                uint8_t *stack_tag_nbt=NULL;
                size_t stack_tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta","brew_time","fuel",
                    "stack_nbt_file"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"brew_time"),&brew_time)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=BREWING_LIVE_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   brew_time<0||brew_time>TB_BREW_TICKS||
                   fuel<0||fuel>TB_FUEL_CHARGE||
                   (stack_nbt_file&&(!stack_nbt_file->string||count==0||
                       !read_capsule_nbt(stack_nbt_file->value,
                           &stack_tag_nbt,&stack_tag_nbt_len)))||
                   !gm_runtime_brewing_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta,
                       (int)brew_time,(int)fuel)||
                   (stack_nbt_file&&!gm_runtime_container_set_stack_tag(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)slot,
                       stack_tag_nbt,stack_tag_nbt_len))){
                    free(stack_tag_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_brewing_slot\n",
                        line_no);goto bad;
                }
                free(stack_tag_nbt);
            } else if (!strcmp(type,"set_static_container_slot")) {
                long long dimension,x,y,z,slot,item,count,meta;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *item_tag_nbt=NULL;
                size_t item_tag_nbt_len=0;
                const JlField *stack_nbt_file=
                    field(&pending,"stack_nbt_file");
                uint8_t *stack_tag_nbt=NULL;
                size_t stack_tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "slot","item","count","meta","nbt_file",
                    "stack_nbt_file"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   slot<0||slot>=GM_RUNTIME_STATIC_CONTAINER_SLOTS||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||
                   (nbt_file&&(!nbt_file->string||
                       !read_capsule_nbt(
                           nbt_file->value,
                           &item_tag_nbt,&item_tag_nbt_len)))||
                   (stack_nbt_file&&(!stack_nbt_file->string||count==0||
                       !read_capsule_nbt(stack_nbt_file->value,
                           &stack_tag_nbt,&stack_tag_nbt_len)))||
                   !gm_runtime_static_container_set_slot(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)slot,(int)item,(int)count,(int)meta)||
                   (nbt_file&&!gm_runtime_shulker_set_item_tag_nbt(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       item_tag_nbt,item_tag_nbt_len))||
                   (stack_nbt_file&&!gm_runtime_container_set_stack_tag(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)slot,
                       stack_tag_nbt,stack_tag_nbt_len))){
                    free(item_tag_nbt);
                    free(stack_tag_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_static_container_slot\n",
                        line_no);goto bad;
                }
                free(item_tag_nbt);
                free(stack_tag_nbt);
            } else if (!strcmp(type,"restore_shulker_transient")) {
                long long dimension,x,y,z,open_count,animation_status;
                long long progress_bits,progress_old_bits;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","open_count",
                    "animation_status","progress_bits","progress_old_bits"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"open_count"),&open_count)||
                   !as_i64(field(&pending,"animation_status"),
                           &animation_status)||
                   !as_i64(field(&pending,"progress_bits"),&progress_bits)||
                   !as_i64(field(&pending,"progress_old_bits"),
                           &progress_old_bits)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   open_count<INT_MIN||open_count>INT_MAX||
                   animation_status<GM_SHULKER_BOX_CLOSED||
                   animation_status>GM_SHULKER_BOX_CLOSING||
                   progress_bits<0||progress_bits>UINT32_MAX||
                   progress_old_bits<0||progress_old_bits>UINT32_MAX||
                   !gm_runtime_shulker_set_transient(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)open_count,(int)animation_status,
                       (uint32_t)progress_bits,
                       (uint32_t)progress_old_bits)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "restore_shulker_transient\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"restore_brewing_ingredient")) {
                long long dimension,x,y,z,ingredient_id;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","ingredient_id"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"ingredient_id"),&ingredient_id)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   ingredient_id<0||ingredient_id>4095||
                   !gm_runtime_brewing_set_ingredient(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)ingredient_id)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "restore_brewing_ingredient\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_hopper_transfer_state")) {
                long long dimension,x,y,z,cooldown,ticked_time;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "transfer_cooldown","ticked_game_time"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"transfer_cooldown"),&cooldown)||
                   !as_i64(field(&pending,"ticked_game_time"),&ticked_time)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   cooldown<INT_MIN||cooldown>INT_MAX||
                   !gm_runtime_hopper_set_transfer_state(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)cooldown,ticked_time)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_hopper_transfer_state\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_flower_pot")) {
                long long dimension,x,y,z,item,meta;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","item","meta"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   item<0||item>4095||meta<0||meta>32767||
                   (item==0&&meta!=0)||
                   !gm_runtime_flower_pot_set(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)item,(int)meta)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_flower_pot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_note_block")) {
                long long dimension,x,y,z,note,powered;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","note","powered"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"note"),&note)||
                   !as_i64(field(&pending,"powered"),&powered)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||note<0||note>24||
                   (powered!=0&&powered!=1)||
                   !gm_runtime_note_block_set(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)note,(int)powered)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_note_block\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_skull")) {
                long long dimension,x,y,z,skull_type,rotation;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *profile_nbt=NULL;
                size_t profile_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "skull_type","rotation","nbt_file"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"skull_type"),&skull_type)||
                   !as_i64(field(&pending,"rotation"),&rotation)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   skull_type<0||skull_type>5||
                   rotation<0||rotation>15||
                   (nbt_file&&(!nbt_file->string||skull_type!=3||
                       !read_capsule_nbt(
                           nbt_file->value,&profile_nbt,&profile_nbt_len)))||
                   !(nbt_file
                       ? gm_runtime_skull_set_profile_nbt(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)skull_type,(int)rotation,
                           profile_nbt,profile_nbt_len)
                       : gm_runtime_skull_set(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)skull_type,(int)rotation))){
                    free(profile_nbt);
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_skull\n",
                        line_no);goto bad;
                }
                free(profile_nbt);
            } else if (!strcmp(type,"set_decorative_tile")) {
                long long dimension,x,y,z,drop_item,drop_meta;
                const JlField *tile_file=field(&pending,"tile_nbt_file");
                const JlField *drop_file=field(&pending,"drop_nbt_file");
                uint8_t *tile_nbt=NULL,*drop_nbt=NULL;
                size_t tile_nbt_len=0,drop_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "tile_nbt_file","drop_item","drop_meta",
                    "drop_nbt_file"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"drop_item"),&drop_item)||
                   !as_i64(field(&pending,"drop_meta"),&drop_meta)||
                   !tile_file||!tile_file->string||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   drop_item<=0||drop_item>4095||
                   drop_meta<0||drop_meta>32767||
                   !read_capsule_nbt(tile_file->value,
                       &tile_nbt,&tile_nbt_len)||
                   (drop_file&&(!drop_file->string||
                       !read_capsule_nbt(drop_file->value,
                           &drop_nbt,&drop_nbt_len)))||
                   !gm_runtime_decorative_tile_set_nbt(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       tile_nbt,tile_nbt_len,
                       (int)drop_item,(int)drop_meta,
                       drop_nbt,drop_nbt_len)){
                    free(tile_nbt);free(drop_nbt);
                    fprintf(stderr,
                        "script:%ld: invalid set_decorative_tile\n",
                        line_no);goto bad;
                }
                free(tile_nbt);free(drop_nbt);
            } else if (!strcmp(type,"set_command_block_success")) {
                long long dimension,x,y,z,success;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","success_count"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"success_count"),&success)||
                   dimension<-1||dimension>1||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   success<0||success>15||
                   !gm_runtime_command_block_set_success(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       (int)success)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_command_block_success\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_command_block_state")) {
                long long dimension,x,y,z,success;
                int powered,automatic,condition_met;
                const JlField *command=field(&pending,"command");
                const JlField *output=field(&pending,"last_output");
                static const char *const keys[]={
                    "tick","type","dim","x","y","z",
                    "success_count","command","last_output",
                    "powered","automatic","condition_met"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"success_count"),&success)||
                   !as_rule_bool(field(&pending,"powered"),&powered)||
                   !as_rule_bool(field(&pending,"automatic"),&automatic)||
                   !as_rule_bool(
                       field(&pending,"condition_met"),&condition_met)||
                   !command||!command->string||!output||!output->string||
                   dimension<-1||dimension>1||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX||
                   success<0||success>15||
                   !gm_runtime_command_block_set_state(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       command->value,output->value,(int)success)||
                   !gm_runtime_command_block_set_execution_state(
                       &r,(int)dimension,(int)x,(int)y,(int)z,
                       powered,automatic,condition_met)){
                    fprintf(stderr,
                        "script:%ld: invalid set_command_block_state\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"trigger_command_block")) {
                long long dimension,x,y,z,hour=0,minute=0,second=0;
                long long weather_duration=0;
                long long teleport_delay=0;
                long long teleport_pre_tick=0;
                int has_clock=field(&pending,"hour")!=NULL;
                int has_weather_duration=
                    field(&pending,"weather_duration_ticks")!=NULL;
                int has_teleport_delay=
                    field(&pending,"teleport_client_delay_ticks")!=NULL;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z"
                };
                static const char *const clock_keys[]={
                    "tick","type","dim","x","y","z",
                    "hour","minute","second"
                };
                static const char *const context_keys[]={
                    "tick","type","dim","x","y","z",
                    "hour","minute","second","weather_duration_ticks"
                };
                static const char *const teleport_keys[]={
                    "tick","type","dim","x","y","z",
                    "hour","minute","second","teleport_client_delay_ticks",
                    "teleport_client_pre_tick"
                };
                if(!(has_teleport_delay
                        ? keys_only(&pending,teleport_keys,11,err,sizeof err)
                        : has_weather_duration
                        ? keys_only(&pending,context_keys,10,err,sizeof err)
                        : has_clock
                        ? keys_only(&pending,clock_keys,9,err,sizeof err)
                        : keys_only(&pending,keys,6,err,sizeof err))||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   (has_clock&&(
                       !as_i64(field(&pending,"hour"),&hour)||
                       !as_i64(field(&pending,"minute"),&minute)||
                       !as_i64(field(&pending,"second"),&second)||
                       hour<0||hour>23||minute<0||minute>59||
                       second<0||second>59))||
                   (has_weather_duration&&(
                       !has_clock||
                       !as_i64(field(&pending,"weather_duration_ticks"),
                           &weather_duration)||
                       weather_duration<6000||weather_duration>17980||
                       weather_duration%20))||
                   (has_teleport_delay&&(
                       !has_clock||
                       !as_i64(field(&pending,"teleport_client_delay_ticks"),
                           &teleport_delay)||
                       !as_i64(field(&pending,"teleport_client_pre_tick"),
                           &teleport_pre_tick)||
                       teleport_delay<1||teleport_delay>100||
                       teleport_pre_tick<0||teleport_pre_tick>1))||
                   dimension<-1||dimension>1||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX||
                   !(has_weather_duration
                       ? gm_runtime_command_block_trigger_at_context(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)hour,(int)minute,(int)second,
                           (int)weather_duration)
                       : has_clock
                       ? gm_runtime_command_block_trigger_at_clock(
                           &r,(int)dimension,(int)x,(int)y,(int)z,
                           (int)hour,(int)minute,(int)second)
                       : gm_runtime_command_block_trigger(
                           &r,(int)dimension,(int)x,(int)y,(int)z))){
                    fprintf(stderr,
                        "script:%ld: invalid trigger_command_block\n",
                        line_no);goto bad;
                }
                if(has_teleport_delay)
                    r.command_position_correction_ticks=(int)teleport_delay;
                if(has_teleport_delay)
                    r.command_position_correction_pre_tick=
                        (int)teleport_pre_tick;
            } else if (!strcmp(type,"set_item_frame_source")) {
                long long dimension,eid,hanging_x,hanging_y,hanging_z;
                long long facing,item,count,meta,rotation;
                double x,y,z;
                static const char *const keys[]={
                    "tick","type","dim","eid","x","y","z",
                    "hanging_x","hanging_y","hanging_z",
                    "facing","item","count","meta","rotation"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"hanging_x"),&hanging_x)||
                   !as_i64(field(&pending,"hanging_y"),&hanging_y)||
                   !as_i64(field(&pending,"hanging_z"),&hanging_z)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"rotation"),&rotation)||
                   dimension<-1||dimension>1||
                   eid<0||eid>INT_MAX||
                   hanging_x<INT_MIN||hanging_x>INT_MAX||
                   hanging_y<0||hanging_y>255||
                   hanging_z<INT_MIN||hanging_z>INT_MAX||
                   facing<2||facing>5||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767||rotation<0||rotation>7||
                   !gm_runtime_item_frame_set(
                       &r,(int)dimension,(int)eid,x,y,z,
                       (int)hanging_x,(int)hanging_y,(int)hanging_z,
                       (int)facing,(int)item,(int)count,(int)meta,
                       (int)rotation)){
                    fprintf(
                        stderr,
                        "script:%ld: invalid set_item_frame_source\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_item_frame")) {
                long long dimension,eid,hanging_x,hanging_y,hanging_z;
                long long facing,item,count,meta,rotation,tick_counter;
                long long entity_seed48,entity_have_gaussian,most,least;
                long long n_ench=0,repair_cost=0;
                long long tracker_update_counter,map_data_present;
                long long map_dimension,map_x_center,map_z_center,map_scale;
                long long map_tracking_position,map_unlimited_tracking;
                long long map_decoration_present,map_decoration_type;
                long long map_decoration_x,map_decoration_z;
                long long map_decoration_rotation;
                double item_drop_chance,entity_gaussian;
                const char *custom_name=NULL;
                const JlField *nbt_file=field(&pending,"nbt_file");
                const JlField *map_colors_file=
                    field(&pending,"map_colors_file");
                uint8_t *tag_nbt=NULL;size_t tag_nbt_len=0;ICStack st;
                static const char *const keys[]={
                    "tick","type","dim","eid",
                    "hanging_x","hanging_y","hanging_z","facing",
                    "item","count","meta","rotation","tick_counter",
                    "item_drop_chance","entity_seed48",
                    "entity_have_gaussian","entity_gaussian",
                    "most","least","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7",
                    "repair_cost","custom_name","nbt_file",
                    "tracker_update_counter","map_data_present",
                    "map_dimension","map_x_center","map_z_center",
                    "map_scale","map_tracking_position",
                    "map_unlimited_tracking","map_decoration_present",
                    "map_decoration_type","map_decoration_x",
                    "map_decoration_z","map_decoration_rotation",
                    "map_colors_file"
                };
                if(!keys_only(&pending,keys,45,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"hanging_x"),&hanging_x)||
                   !as_i64(field(&pending,"hanging_y"),&hanging_y)||
                   !as_i64(field(&pending,"hanging_z"),&hanging_z)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"rotation"),&rotation)||
                   !as_i64(field(&pending,"tick_counter"),&tick_counter)||
                   !as_double(field(&pending,"item_drop_chance"),
                              &item_drop_chance)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                              &entity_gaussian)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   !as_i64(field(&pending,"tracker_update_counter"),
                           &tracker_update_counter)||
                   !as_i64(field(&pending,"map_data_present"),
                           &map_data_present)||
                   !as_i64(field(&pending,"map_dimension"),&map_dimension)||
                   !as_i64(field(&pending,"map_x_center"),&map_x_center)||
                   !as_i64(field(&pending,"map_z_center"),&map_z_center)||
                   !as_i64(field(&pending,"map_scale"),&map_scale)||
                   !as_i64(field(&pending,"map_tracking_position"),
                           &map_tracking_position)||
                   !as_i64(field(&pending,"map_unlimited_tracking"),
                           &map_unlimited_tracking)||
                   !as_i64(field(&pending,"map_decoration_present"),
                           &map_decoration_present)||
                   !as_i64(field(&pending,"map_decoration_type"),
                           &map_decoration_type)||
                   !as_i64(field(&pending,"map_decoration_x"),
                           &map_decoration_x)||
                   !as_i64(field(&pending,"map_decoration_z"),
                           &map_decoration_z)||
                   !as_i64(field(&pending,"map_decoration_rotation"),
                           &map_decoration_rotation)||
                   (field(&pending,"repair_cost")&&
                    !as_i64(field(&pending,"repair_cost"),&repair_cost))||
                   (field(&pending,"custom_name")&&
                    !as_string(field(&pending,"custom_name"),&custom_name))||
                   dimension<-1||dimension>1||eid<0||eid>INT_MAX||
                   hanging_x<INT_MIN||hanging_x>INT_MAX||
                   hanging_y<0||hanging_y>255||
                   hanging_z<INT_MIN||hanging_z>INT_MAX||
                   facing<2||facing>5||item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767||
                   rotation<0||rotation>7||tick_counter<0||
                   tick_counter>100||entity_seed48<0||
                   entity_seed48>((1LL<<48)-1)||
                   (entity_have_gaussian!=0&&entity_have_gaussian!=1)||
                   tracker_update_counter<0||tracker_update_counter>INT_MAX||
                   (map_data_present!=0&&map_data_present!=1)||
                   map_dimension<INT_MIN||map_dimension>INT_MAX||
                   map_x_center<INT_MIN||map_x_center>INT_MAX||
                   map_z_center<INT_MIN||map_z_center>INT_MAX||
                   map_scale<0||map_scale>4||
                   (map_tracking_position!=0&&map_tracking_position!=1)||
                   (map_unlimited_tracking!=0&&map_unlimited_tracking!=1)||
                   (map_decoration_present!=0&&
                    map_decoration_present!=1)||
                   map_decoration_type<0||map_decoration_type>9||
                   map_decoration_x<-128||map_decoration_x>127||
                   map_decoration_z<-128||map_decoration_z>127||
                   map_decoration_rotation<-128||
                   map_decoration_rotation>127||
                   (map_colors_file&&(!map_colors_file->string||
                    !map_data_present||item!=358))||
                   repair_cost<0||repair_cost>INT_MAX||
                   !isfinite(item_drop_chance)||!isfinite(entity_gaussian)){
                    fprintf(stderr,"script:%ld: invalid set_item_frame\n",
                            line_no);goto bad;
                }
                st=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                st.repair_cost=(i32)repair_cost;
                if(nbt_file){
                    if(!nbt_file->string||count==0||
                       !read_capsule_nbt(nbt_file->value,
                                         &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                                "script:%ld: invalid item frame NBT\n",
                                line_no);goto bad;
                    }
                    st.tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(st.tag_id==0){
                        fprintf(stderr,
                                "script:%ld: invalid item frame tag\n",
                                line_no);goto bad;
                    }
                }
                if(custom_name&&custom_name[0]){
                    st.custom_name=gm_runtime_item_name_intern(
                        &r,custom_name);
                    if(st.custom_name==0){
                        fprintf(stderr,
                                "script:%ld: invalid item frame name\n",
                                line_no);goto bad;
                    }
                }
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,
                                "script:%ld: invalid item frame enchants\n",
                                line_no);goto bad;
                    }
                    st.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char ek[4];long long packed;
                        snprintf(ek,sizeof ek,"e%d",ei);
                        if(!as_i64(field(&pending,ek),&packed)){
                            fprintf(stderr,
                                    "script:%ld: missing item frame %s\n",
                                    line_no,ek);goto bad;
                        }
                        st.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        st.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!gm_runtime_item_frame_set_full(
                       &r,(int)dimension,(int)eid,
                       (int)hanging_x,(int)hanging_y,(int)hanging_z,
                       (int)facing,st,(int)rotation,(int)tick_counter,
                       (float)item_drop_chance,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian,most,least)||
                   !gm_runtime_item_frame_set_map_state(
                       &r,(int)eid,(int)tracker_update_counter,
                       (int)map_data_present,(int)map_dimension,
                       (int)map_x_center,(int)map_z_center,(int)map_scale,
                       (int)map_tracking_position,
                       (int)map_unlimited_tracking,
                       (int)map_decoration_present,
                       (int)map_decoration_type,(int)map_decoration_x,
                       (int)map_decoration_z,
                       (int)map_decoration_rotation)){
                    fprintf(stderr,"script:%ld: invalid set_item_frame\n",
                            line_no);goto bad;
                }
                if(map_colors_file){
                    uint8_t *colors=NULL;size_t colors_len=0;
                    if(!read_capsule_bytes(map_colors_file->value,
                                          128u*128u,128u*128u,
                                          &colors,&colors_len)||
                       colors_len!=128u*128u||
                       !gm_runtime_item_frame_set_map_colors(
                           &r,(int)eid,colors)){
                        free(colors);
                        fprintf(stderr,
                                "script:%ld: invalid item frame map colors\n",
                                line_no);goto bad;
                    }
                    free(colors);
                }
            } else if (!strcmp(type,"set_painting")) {
                long long dimension,eid,hanging_x,hanging_y,hanging_z;
                long long facing,art,tick_counter,most,least;
                static const char *const keys[]={
                    "tick","type","dim","eid",
                    "hanging_x","hanging_y","hanging_z",
                    "facing","art","tick_counter","most","least"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"hanging_x"),&hanging_x)||
                   !as_i64(field(&pending,"hanging_y"),&hanging_y)||
                   !as_i64(field(&pending,"hanging_z"),&hanging_z)||
                   !as_i64(field(&pending,"facing"),&facing)||
                   !as_i64(field(&pending,"art"),&art)||
                   !as_i64(field(&pending,"tick_counter"),&tick_counter)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   dimension<-1||dimension>1||eid<0||eid>INT_MAX||
                   hanging_x<INT_MIN||hanging_x>INT_MAX||
                   hanging_y<0||hanging_y>255||
                   hanging_z<INT_MIN||hanging_z>INT_MAX||
                   facing<2||facing>5||art<0||art>=26||
                   tick_counter<0||tick_counter>100||
                   !gm_runtime_painting_set(
                       &r,(int)dimension,(int)eid,
                       (int)hanging_x,(int)hanging_y,(int)hanging_z,
                       (int)facing,(int)art,(int)tick_counter)||
                   !gm_runtime_painting_set_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid set_painting\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_leash_knot")) {
                long long dimension,eid,x,y,z,tick_counter,most,least;
                static const char *const keys[]={
                    "tick","type","dim","eid","x","y","z",
                    "tick_counter","most","least"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"tick_counter"),&tick_counter)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   dimension<-1||dimension>1||eid<0||eid>INT_MAX||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   tick_counter<0||tick_counter>100||
                   !gm_runtime_leash_knot_set(
                       &r,(int)dimension,(int)eid,
                       (int)x,(int)y,(int)z,(int)tick_counter)||
                   !gm_runtime_leash_knot_set_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid set_leash_knot\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_living_leash")) {
                long long eid,holder_kind,holder_eid;
                static const char *const keys[]={
                    "tick","type","eid","holder_kind","holder_eid"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"holder_kind"),&holder_kind)||
                   !as_i64(field(&pending,"holder_eid"),&holder_eid)||
                   eid<0||eid>INT_MAX||holder_kind<0||holder_kind>2||
                   holder_eid<-1||holder_eid>INT_MAX||
                   (holder_kind==0
                       ? (holder_eid!=-1||!gm_mobs_living_clear_leash(
                           &r.mobs,(int)eid))
                       : holder_kind==1
                       ? !gm_mobs_living_set_leash_player(
                           &r.mobs,(int)eid,(int)holder_eid)
                       : !gm_mobs_living_set_leash_living(
                           &r.mobs,(int)eid,(int)holder_eid))){
                    fprintf(stderr,
                            "script:%ld: invalid restore_living_leash\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_living_leash_knot")) {
                long long eid,knot_eid;
                static const char *const keys[]={
                    "tick","type","eid","knot_eid"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"knot_eid"),&knot_eid)||
                   eid<0||eid>INT_MAX||knot_eid<0||knot_eid>INT_MAX||
                   !gm_runtime_restore_living_leash_knot(
                       &r,(int)eid,(int)knot_eid)){
                    fprintf(stderr,
                        "script:%ld: invalid restore_living_leash_knot\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_living_leash_pending")) {
                long long eid,x,y,z;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   eid<0||eid>INT_MAX||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX||
                   !gm_runtime_restore_living_leash_pending(
                       &r,(int)eid,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                        "script:%ld: invalid restore_living_leash_pending\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"set_wolf_angry")) {
                long long eid,angry;
                static const char *const keys[]={
                    "tick","type","eid","angry"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"angry"),&angry)||
                   eid<0||eid>INT_MAX||(angry!=0&&angry!=1)||
                   !gm_mobs_set_wolf_angry(&r.mobs,(int)eid,(int)angry)){
                    fprintf(stderr,"script:%ld: invalid set_wolf_angry\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_llama_leash_knot")) {
                long long llama_eid,knot_eid;
                static const char *const keys[]={
                    "tick","type","llama_eid","knot_eid"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"llama_eid"),&llama_eid)||
                   !as_i64(field(&pending,"knot_eid"),&knot_eid)||
                   llama_eid<0||llama_eid>INT_MAX||
                   knot_eid<0||knot_eid>INT_MAX||
                   !gm_runtime_restore_llama_leash_knot(
                       &r,(int)llama_eid,(int)knot_eid)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_llama_leash_knot\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_llama_leash_pending")) {
                long long llama_eid,x,y,z;
                static const char *const keys[]={
                    "tick","type","llama_eid","x","y","z"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"llama_eid"),&llama_eid)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   llama_eid<0||llama_eid>INT_MAX||
                   x<INT_MIN||x>INT_MAX||y<0||y>255||
                   z<INT_MIN||z>INT_MAX||
                   !gm_runtime_restore_llama_leash_pending(
                       &r,(int)llama_eid,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_llama_leash_pending\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_entity_id_cursor")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>2147483647LL||
                   !gm_runtime_set_entity_id_cursor(&r,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_entity_id_cursor\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_player_entity_id")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>2147483647LL||
                   !gm_runtime_set_player_entity_id(&r,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_player_entity_id\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_world_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_math_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_math_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_math_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_collections_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_collections_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "set_collections_random_seed\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_server_uuid_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_server_uuid_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_server_uuid_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_entity_seed_generator_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_entity_seed_generator_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "set_entity_seed_generator_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_random_gaussian")) {
                long long have_next;
                double next;
                static const char *const keys[]={
                    "tick","type","have_next","next"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"have_next"),&have_next)||
                   !as_double(field(&pending,"next"),&next)||
                   (have_next!=0&&have_next!=1)||!isfinite(next)||
                   !gm_runtime_set_world_random_gaussian(
                       &r,(int)have_next,next)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_random_gaussian\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_block_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_block_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_block_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_inventory_helper_random")) {
                long long value, have_next;
                double next;
                static const char *const keys[]={
                    "tick","type","value","have_next","next"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   !as_i64(field(&pending,"have_next"),&have_next)||
                   !as_double(field(&pending,"next"),&next)||
                   value<0||value>((1LL<<48)-1)||
                   (have_next!=0&&have_next!=1)||!isfinite(next)||
                   !gm_runtime_set_inventory_helper_random_state(
                       &r,(uint64_t)value,(int)have_next,next)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "set_inventory_helper_random\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_dispenser_random_seed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>((1LL<<48)-1)||
                   !gm_runtime_set_dispenser_random_seed48(
                       &r,(uint64_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_dispenser_random_seed\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_world_update_lcg")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<(-2147483647LL-1)||value>2147483647LL||
                   !gm_runtime_set_world_update_lcg(
                       &r,(int32_t)value)){
                    fprintf(stderr,
                            "script:%ld: invalid set_world_update_lcg\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"ticking_chunks_begin")) {
                long long count;
                static const char *const keys[]={"tick","type","count"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"count"),&count)||
                   count<0||count>4225||
                   !gm_runtime_ticking_chunks_begin(&r,(int)count)){
                    fprintf(stderr,
                            "script:%ld: invalid ticking_chunks_begin\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"ticking_chunk_set")) {
                long long order,x,z,mask;
                static const char *const keys[]={
                    "tick","type","order","x","z","random_tick_mask"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"order"),&order)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"random_tick_mask"),&mask)||
                   order<0||order>4224||x<-134217728||x>134217727||
                   z<-134217728||z>134217727||mask<0||mask>65535||
                   !gm_runtime_ticking_chunk_set(
                       &r,(int)order,(int)x,(int)z,(unsigned)mask)){
                    fprintf(stderr,
                            "script:%ld: invalid ticking_chunk_set\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"ticking_chunks_finalize")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)||
                   !gm_runtime_ticking_chunks_finalize(&r)){
                    fprintf(stderr,
                            "script:%ld: invalid ticking_chunks_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"random_tick_block")) {
                long long x,y,z,block;
                static const char *const keys[]={
                    "tick","type","x","y","z","block"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||block<1||block>4095||
                   !gm_runtime_random_tick_block(
                       &r,(int)x,(int)y,(int)z,(int)block)){
                    fprintf(stderr,
                            "script:%ld: invalid random_tick_block\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"random_tick_selection")) {
                long long x,y,z,block,advances;
                static const char *const keys[]={
                    "tick","type","x","y","z","block",
                    "lcg_advances_before"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(
                       field(&pending,"lcg_advances_before"),&advances)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||block<1||block>4095||
                   advances<0||advances>1000000||
                   !gm_runtime_random_tick_selection(
                       &r,(int)x,(int)y,(int)z,(int)block,
                       (int)advances)){
                    fprintf(stderr,
                            "script:%ld: invalid random_tick_selection\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"schedule_tick")) {
                long long x,y,z,block,time,priority,order;
                static const char *const keys[]={
                    "tick","type","x","y","z","block","time","priority","order"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(field(&pending,"time"),&time)||
                   !as_i64(field(&pending,"priority"),&priority)||
                   !as_i64(field(&pending,"order"),&order)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   !gm_runtime_schedule_tick(
                       &r,(int)x,(int)y,(int)z,(int)block,time,
                       (int)priority,order)){
                    fprintf(stderr,"script:%ld: invalid schedule_tick\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_scheduled_tick")) {
                long long dimension,x,y,z,block,time,priority,order;
                static const char *const keys[]={
                    "tick","type","dimension","x","y","z","block",
                    "time","priority","order"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(field(&pending,"time"),&time)||
                   !as_i64(field(&pending,"priority"),&priority)||
                   !as_i64(field(&pending,"order"),&order)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   !gm_runtime_restore_scheduled_tick(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)block,
                       time,(int)priority,order)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_scheduled_tick\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_elytra")) {
                long long equipped;
                static const char *const keys[]={"tick","type","equipped"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"equipped"),&equipped)||
                   (equipped!=0&&equipped!=1)){
                    fprintf(stderr,"script:%ld: invalid set_elytra\n",line_no);goto bad;
                }
                gm_runtime_set_elytra(&r,(int)equipped);
            } else if (!strcmp(type,"set_elytra_flag7")) {
                long long flying;
                static const char *const keys[]={"tick","type","flying"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"flying"),&flying)||
                   (flying!=0&&flying!=1)){
                    fprintf(stderr,"script:%ld: invalid set_elytra_flag7\n",line_no);goto bad;
                }
                gm_runtime_set_elytra_flag7(&r,(int)flying);
            } else if (!strcmp(type,"set_skin")) {
                /* first-person arm variant: offline players get steve or alex
                 * by username-UUID hash; the tape header records which. */
                const char *skin;
                static const char *const keys[]={"tick","type","skin"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_string(field(&pending,"skin"),&skin)||
                   (strcmp(skin,"default")&&strcmp(skin,"slim"))){
                    fprintf(stderr,"script:%ld: invalid set_skin\n",line_no);goto bad;
                }
                gm_hand_set_skin(!strcmp(skin,"slim"));
            } else if (!strcmp(type,"set_weather")) {
                long long raining,thundering,rain_time,thunder_time;
                long long clean_weather_time=0,weather_cycle=1;
                double prev_rain=0.0,rain=0.0,prev_thunder=0.0,thunder=0.0;
                static const char *const keys[]={
                    "tick","type","raining","thundering",
                    "rain_time","thunder_time","clean_weather_time",
                    "weather_cycle","prev_rain_strength","rain_strength",
                    "prev_thunder_strength","thunder_strength"};
                int full=field(&pending,"clean_weather_time")!=NULL||
                    field(&pending,"weather_cycle")!=NULL||
                    field(&pending,"prev_rain_strength")!=NULL||
                    field(&pending,"rain_strength")!=NULL||
                    field(&pending,"prev_thunder_strength")!=NULL||
                    field(&pending,"thunder_strength")!=NULL;
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"raining"),&raining)||
                   !as_i64(field(&pending,"thundering"),&thundering)||
                   !as_i64(field(&pending,"rain_time"),&rain_time)||
                   !as_i64(field(&pending,"thunder_time"),&thunder_time)||
                   (raining!=0&&raining!=1)||(thundering!=0&&thundering!=1)||
                   rain_time<0||rain_time>2147483647LL||
                   thunder_time<0||thunder_time>2147483647LL) {
                    fprintf(stderr,"script:%ld: invalid set_weather\n",line_no);goto bad;
                }
                if(full){
                    if(!as_i64(field(&pending,"clean_weather_time"),&clean_weather_time)||
                       !as_i64(field(&pending,"weather_cycle"),&weather_cycle)||
                       !as_double(field(&pending,"prev_rain_strength"),&prev_rain)||
                       !as_double(field(&pending,"rain_strength"),&rain)||
                       !as_double(field(&pending,"prev_thunder_strength"),&prev_thunder)||
                       !as_double(field(&pending,"thunder_strength"),&thunder)||
                       clean_weather_time<0||clean_weather_time>2147483647LL||
                       (weather_cycle!=0&&weather_cycle!=1)||
                       prev_rain<0.0||prev_rain>1.0||rain<0.0||rain>1.0||
                       prev_thunder<0.0||prev_thunder>1.0||
                       thunder<0.0||thunder>1.0){
                        fprintf(stderr,"script:%ld: invalid full set_weather\n",line_no);goto bad;
                    }
                    gm_runtime_set_weather_full(
                        &r,(int)raining,(int)thundering,
                        (int)rain_time,(int)thunder_time,
                        (int)clean_weather_time,(int)weather_cycle,
                        (float)prev_rain,(float)rain,
                        (float)prev_thunder,(float)thunder);
                }else{
                    gm_runtime_set_weather(&r,(int)raining,(int)thundering,
                                           (int)rain_time,(int)thunder_time);
                }
            } else if (!strcmp(type,"set_gamerules")) {
                McGameRules gamerules=r.gamerules;
                int do_mob_loot=r.do_mob_loot;
                /* The recorder emits all string-backed rules. These three
                 * currently have magma runtime mechanics; every other string
                 * field is intentionally consumed without effect. */
                for(int i=0;i<pending.n;++i){
                    const JlField *rf=&pending.f[i];
                    if(!strcmp(rf->key,"tick")||!strcmp(rf->key,"type"))continue;
                    if(!rf->string){
                        fprintf(stderr,"script:%ld: gamerule %s must be a string\n",
                                line_no,rf->key);goto bad;
                    }
                }
                if(!as_rule_bool(field(&pending,"naturalRegeneration"),
                                 &gamerules.naturalRegeneration)||
                   !as_rule_bool(field(&pending,"doMobSpawning"),
                                 &gamerules.doMobSpawning)||
                   !as_rule_bool(field(&pending,"doMobLoot"),
                                 &do_mob_loot)||
                   !as_rule_bool(field(&pending,"doDaylightCycle"),
                                 &gamerules.doDaylightCycle)||
                   !as_rule_bool(field(&pending,"doWeatherCycle"),
                                 &gamerules.doWeatherCycle)){
                    fprintf(stderr,"script:%ld: invalid honored gamerule\n",line_no);goto bad;
                }
                gm_runtime_set_gamerules(&r,&gamerules);
                gm_runtime_set_do_mob_loot(&r,do_mob_loot);
            } else if (!strcmp(type,"set_random_tick_speed")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<(-2147483647LL-1)||value>2147483647LL){
                    fprintf(stderr,
                            "script:%ld: invalid set_random_tick_speed\n",
                            line_no);goto bad;
                }
                r.gamerules.randomTickSpeed=(int)value;
            } else if (!strcmp(type,"set_daylight_cycle")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   (enabled!=0&&enabled!=1)){
                    fprintf(stderr,"script:%ld: invalid set_daylight_cycle\n",line_no);goto bad;
                }
                gm_runtime_set_daylight_cycle(&r,(int)enabled);
            } else if (!strcmp(type,"set_fire_rain_context")) {
                long long x,y,z,can_die,raining_at_east;
                long long can_die_west_candidate;
                static const char *const keys[]={
                    "tick","type","x","y","z","can_die",
                    "raining_at_east","can_die_west_candidate"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"can_die"),&can_die)||
                   !as_i64(field(&pending,"raining_at_east"),
                           &raining_at_east)||
                   !as_i64(field(&pending,"can_die_west_candidate"),
                           &can_die_west_candidate)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||(can_die!=0&&can_die!=1)||
                   (raining_at_east!=0&&raining_at_east!=1)||
                   (can_die_west_candidate!=0&&
                    can_die_west_candidate!=1)||
                   !gm_runtime_set_fire_rain_context(
                       &r,(int)x,(int)y,(int)z,(int)can_die,
                       (int)raining_at_east,
                       (int)can_die_west_candidate)){
                    fprintf(stderr,
                            "script:%ld: invalid set_fire_rain_context\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_fire_humidity_context")) {
                long long x,y,z;
                static const char *const keys[]={
                    "tick","type","x","y","z"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||y<0||y>255||
                   !gm_runtime_set_fire_humidity_context(
                       &r,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid set_fire_humidity_context\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_block")) {
                long long x,y,z,id,meta;
                static const char *const keys[]={"tick","type","x","y","z","id","meta"};
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_set_block(&r,(int)x,(int)y,(int)z,(int)id,(int)meta)) {
                    fprintf(stderr,"script:%ld: invalid set_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"harvest_block")) {
                long long x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||y<0||y>255||
                   !gm_runtime_harvest_block(&r,(int)x,(int)y,(int)z)) {
                    fprintf(stderr,"script:%ld: invalid harvest_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"begin_controlled_input")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid begin_controlled_input\n",
                            line_no);goto bad;
                }
                gm_runtime_begin_controlled_input(&r);
            } else if (!strcmp(type,"capture_controlled_input")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid capture_controlled_input\n",
                            line_no);goto bad;
                }
                gm_runtime_capture_controlled_input(&r);
            } else if (!strcmp(type,"restore_loaded_entity_order")) {
                long long order,eid;
                static const char *const keys[]={
                    "tick","type","order","eid"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"order"),&order)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   order<0||order>=GM_RUNTIME_LOADED_ENTITY_ORDER||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_restore_loaded_entity_order(
                       &r,(int)order,(int)eid)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_loaded_entity_order\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_item_entity_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_set_item_entity_uuid(
                       &r,(int)eid,most,least)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_item_entity_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_minecart_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_minecart_set_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_minecart_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_transient_entity_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_set_transient_entity_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "restore_transient_entity_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_mob_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_set_mob_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_mob_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_falling_origin")) {
                long long eid,x,y,z;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   eid<0||eid>INT_MAX||
                   x<INT_MIN||x>INT_MAX||y<INT_MIN||y>INT_MAX||
                   z<INT_MIN||z>INT_MAX||
                   !gm_runtime_set_falling_origin(
                       &r,(int)eid,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_falling_origin\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_player_riding")) {
                long long eid;
                static const char *const keys[]={"tick","type","eid"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_minecart_mount(&r,(int)eid)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_riding\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_loaded_tile_order")) {
                long long order,x,y,z;
                static const char *const keys[]={
                    "tick","type","order","x","y","z"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"order"),&order)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   order<0||order>=GM_RUNTIME_LOADED_TILE_ORDER||
                   x<INT_MIN||x>INT_MAX||z<INT_MIN||z>INT_MAX||
                   y<0||y>255||
                   !gm_runtime_restore_loaded_tile_order(
                       &r,(int)order,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_loaded_tile_order\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_tickable_tile_order")) {
                long long order,x,y,z;
                static const char *const keys[]={
                    "tick","type","order","x","y","z"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"order"),&order)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   order<0||order>=GM_RUNTIME_LOADED_TILE_ORDER||
                   x<INT_MIN||x>INT_MAX||z<INT_MIN||z>INT_MAX||
                   y<0||y>255||
                   !gm_runtime_restore_tickable_tile_order(
                       &r,(int)order,(int)x,(int)y,(int)z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_tickable_tile_order\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_spawner_state")) {
                long long dim,x,y,z,entity,delay,min_delay,max_delay;
                long long spawn_count,max_nearby,activate_range,spawn_range;
                int default_entity_nbt=0;
                const JlField *spawn_nbt_file=
                    field(&pending,"spawn_nbt_file");
                const JlField *default_field=
                    field(&pending,"default_entity_nbt");
                uint8_t *spawn_nbt=NULL;
                size_t spawn_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","entity","delay",
                    "min_delay","max_delay","spawn_count","max_nearby",
                    "activate_range","spawn_range","spawn_nbt_file",
                    "default_entity_nbt"
                };
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dim)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"delay"),&delay)||
                   !as_i64(field(&pending,"min_delay"),&min_delay)||
                   !as_i64(field(&pending,"max_delay"),&max_delay)||
                   !as_i64(field(&pending,"spawn_count"),&spawn_count)||
                   !as_i64(field(&pending,"max_nearby"),&max_nearby)||
                   !as_i64(field(&pending,"activate_range"),&activate_range)||
                   !as_i64(field(&pending,"spawn_range"),&spawn_range)||
                   dim!=r.dimension||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX||
                   entity<=0||entity>255||delay< -1||delay>32767||
                   min_delay<0||min_delay>32767||
                   max_delay<0||max_delay>32767||
                   spawn_count<0||spawn_count>32767||
                   max_nearby<0||max_nearby>32767||
                   activate_range<0||activate_range>32767||
                   spawn_range<0||spawn_range>32767||
                   !spawn_nbt_file||!spawn_nbt_file->string||
                   !default_field||
                   !as_rule_bool(default_field,&default_entity_nbt)||
                   !read_capsule_nbt(spawn_nbt_file->value,
                       &spawn_nbt,&spawn_nbt_len)||
                   !gm_runtime_spawner_set_state(
                       &r,(int)x,(int)y,(int)z,(int)entity,
                       (int)delay,(int)min_delay,(int)max_delay,
                       (int)spawn_count,(int)max_nearby,
                       (int)activate_range,(int)spawn_range,
                       spawn_nbt,spawn_nbt_len,default_entity_nbt)){
                    free(spawn_nbt);
                    fprintf(stderr,
                            "script:%ld: invalid set_spawner_state\n",
                            line_no);goto bad;
                }
                free(spawn_nbt);
            } else if (!strcmp(type,"add_spawner_potential")) {
                long long dim,x,y,z,entity,weight;
                int default_entity_nbt=0;
                const JlField *entity_nbt_file=
                    field(&pending,"entity_nbt_file");
                const JlField *default_field=
                    field(&pending,"default_entity_nbt");
                uint8_t *entity_nbt=NULL;
                size_t entity_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","dim","x","y","z","entity","weight",
                    "entity_nbt_file","default_entity_nbt"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"dim"),&dim)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"weight"),&weight)||
                   dim!=r.dimension||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX||
                   entity<=0||entity>255||weight<=0||weight>INT_MAX||
                   !entity_nbt_file||!entity_nbt_file->string||
                   !default_field||
                   !as_rule_bool(default_field,&default_entity_nbt)||
                   !read_capsule_nbt(entity_nbt_file->value,
                       &entity_nbt,&entity_nbt_len)||
                   !gm_runtime_spawner_add_potential(
                       &r,(int)x,(int)y,(int)z,
                       (int)entity,(int)weight,
                       entity_nbt,entity_nbt_len,default_entity_nbt)){
                    free(entity_nbt);
                    fprintf(stderr,
                            "script:%ld: invalid add_spawner_potential\n",
                            line_no);goto bad;
                }
                free(entity_nbt);
            } else if (!strcmp(type,"attach_chunk_store")) {
                long long dimension;
                const JlField *file=field(&pending,"file");
                char path[PATH_MAX];
                static const char *const keys[]={
                    "tick","type","file","dim"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !file||!file->string||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   dimension < -1||dimension > 1||
                   !capsule_payload_path(file->value,path)||
                   !gm_runtime_attach_chunk_store_dim(
                       &r,(int)dimension,path)){
                    fprintf(stderr,
                            "script:%ld: invalid attach_chunk_store\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_player_statistics")) {
                long long play,time_since;
                const JlField *file=field(&pending,"file");
                uint8_t *json=NULL;
                size_t json_len=0;
                static const char *const keys[]={
                    "tick","type","file","play_one_minute",
                    "time_since_death"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !file||!file->string||
                   !as_i64(field(&pending,"play_one_minute"),&play)||
                   !as_i64(field(&pending,"time_since_death"),&time_since)||
                   play<0||time_since<0||
                   !read_capsule_bytes(
                       file->value,2,GM_RUNTIME_STATISTICS_MAX,
                       &json,&json_len)||
                   !gm_runtime_restore_player_statistics(
                       &r,json,json_len,play,time_since)){
                    free(json);
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_statistics\n",
                            line_no);goto bad;
                }
                free(json);
            } else if (!strcmp(type,"write_chunk_store")) {
                long long dimension;
                const JlField *file=field(&pending,"file");
                char path[PATH_MAX];
                static const char *const keys[]={
                    "tick","type","file","dim"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !file||!file->string||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   dimension < -1||dimension > 1||
                   !native_save_payload_path(file->value,path)||
                   !gm_runtime_write_chunk_store_dim(
                       &r,(int)dimension,path)){
                    fprintf(stderr,
                            "script:%ld: invalid write_chunk_store\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"write_runtime_checkpoint")) {
                const JlField *file=field(&pending,"file");
                char path[PATH_MAX];
                static const char *const keys[]={"tick","type","file"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !file||!file->string||
                   !native_save_payload_path(file->value,path)||
                   !gm_runtime_write_checkpoint(&r,path)){
                    fprintf(stderr,
                            "script:%ld: invalid write_runtime_checkpoint\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"write_player_statistics")) {
                const JlField *file=field(&pending,"file");
                char path[PATH_MAX];
                static const char *const keys[]={"tick","type","file"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !file||!file->string||
                   !native_save_payload_path(file->value,path)||
                   !gm_runtime_write_player_statistics(&r,path)){
                    fprintf(stderr,
                            "script:%ld: invalid write_player_statistics\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"write_native_save")) {
                const JlField *slot=field(&pending,"slot");
                const char *root=getenv("MAGMA_NATIVE_WORLD_ROOT");
                static const char *const keys[]={"tick","type","slot"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !slot||!slot->string||!root||!*root||
                   !gm_native_save_write(
                       &r,root,slot->value,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid write_native_save: %s\n",
                            line_no,err);goto bad;
                }
            } else if (!strcmp(type,"load_native_save")) {
                const JlField *slot=field(&pending,"slot");
                const char *root=getenv("MAGMA_NATIVE_WORLD_ROOT");
                static const char *const keys[]={"tick","type","slot"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !slot||!slot->string||!root||!*root||
                   !gm_native_save_load(
                       &r,cfg,root,slot->value,err,sizeof err)){
                    fprintf(stderr,
                            "script:%ld: invalid load_native_save: %s\n",
                            line_no,err);goto bad;
                }
            } else if (!strcmp(type,"load_runtime_checkpoint")) {
                const JlField *file=field(&pending,"file");
                char path[PATH_MAX];
                static const char *const keys[]={"tick","type","file"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !file||!file->string||
                   !native_save_payload_path(file->value,path)||
                   !gm_runtime_load_checkpoint(&r,path)){
                    fprintf(stderr,
                            "script:%ld: invalid load_runtime_checkpoint\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_chunk_bundle")) {
                long long cx,cz,radius,dimension;
                const JlField *file=field(&pending,"file");
                static const char *const keys[]={
                    "tick","type","file","dim","cx","cz","radius"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !file||!file->string||
                   !as_i64(field(&pending,"dim"),&dimension)||
                   !as_i64(field(&pending,"cx"),&cx)||
                   !as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   dimension < -1||dimension > 1||radius<0||radius>32||
                   cx<-134217728LL||cx>134217727LL||
                   cz<-134217728LL||cz>134217727LL||
                   !load_snapshot_chunk_bundle(
                       &r,file->value,(int)dimension,(int)cx,(int)cz,
                       (int)radius)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_chunk_bundle\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_block")) {
                long long x,y,z,id,meta,dimension=0;
                static const char *const keys[]={"tick","type","x","y","z","id","meta","dim"};
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||!as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||id<0||id>4095||meta<0||meta>15||
                   !gm_runtime_load_block_dim(&r,(int)dimension,(int)x,(int)y,(int)z,
                                              (int)id,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid snapshot_block\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_blocks_finalize")) {
                long long cx,cz,radius,dimension=0;
                static const char *const keys[]={
                    "tick","type","cx","cz","radius","dim"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"cx"),&cx)||
                   !as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   cx<-134217728LL||cx>134217727LL||
                   cz<-134217728LL||cz>134217727LL||
                   !gm_runtime_finalize_block_snapshot_dim(
                       &r,(int)dimension,(int)cx,(int)cz,(int)radius)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_blocks_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_height")) {
                long long x,z,value,dimension=0;
                static const char *const keys[]={
                    "tick","type","x","z","value","dim"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"value"),&value)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   value<0||value>256||
                   !gm_runtime_load_height_dim(
                       &r,(int)dimension,(int)x,(int)z,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_height\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_sky_light")) {
                long long x,y,z,value,dimension=0;
                static const char *const keys[]={
                    "tick","type","x","y","z","value","dim"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"value"),&value)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||value<0||value>15||
                   !gm_runtime_load_sky_light_dim(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_sky_light\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_sky_light_finalize")) {
                long long dimension=0;
                static const char *const keys[]={"tick","type","dim"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   !gm_runtime_finalize_sky_light_snapshot_dim(
                       &r,(int)dimension)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_sky_light_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_block_light")) {
                long long x,y,z,value,dimension=0;
                static const char *const keys[]={
                    "tick","type","x","y","z","value","dim"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"value"),&value)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   x<-2147483647LL-1||x>2147483647LL||
                   z<-2147483647LL-1||z>2147483647LL||
                   y<0||y>255||value<0||value>15||
                   !gm_runtime_load_block_light_dim(
                       &r,(int)dimension,(int)x,(int)y,(int)z,(int)value)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_block_light\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_block_light_finalize")) {
                long long dimension=0;
                static const char *const keys[]={"tick","type","dim"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   (field(&pending,"dim")&&
                    !as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   !gm_runtime_finalize_block_light_snapshot_dim(
                       &r,(int)dimension)){
                    fprintf(stderr,
                            "script:%ld: invalid snapshot_block_light_finalize\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"snapshot_region")) {
                long long cx,cz,radius,dimension=0;
                static const char *const keys[]={"tick","type","cx","cz","radius","dim"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"cx"),&cx)||!as_i64(field(&pending,"cz"),&cz)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   (field(&pending,"dim")&&!as_i64(field(&pending,"dim"),&dimension))||
                   dimension < -1||dimension > 1||
                   cx<-134217728LL||cx>134217727LL||cz<-134217728LL||cz>134217727LL||
                   !gm_runtime_snapshot_region_dim(&r,(int)dimension,(int)cx,(int)cz,
                                                  (int)radius)){
                    fprintf(stderr,"script:%ld: invalid snapshot_region\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_inventory")
                    || !strcmp(type,"set_ender_inventory")) {
                long long slot,item,count,meta,n_ench=0,repair_cost=0;
                int ender_inventory=!strcmp(type,"set_ender_inventory");
                const char *custom_name = NULL;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","slot","item","count","meta","n_ench",
                    "e0","e1","e2","e3","e4","e5","e6","e7",
                    "repair_cost","custom_name","nbt_file"};
                ICStack st;
                if(!keys_only(&pending,keys,18,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"repair_cost")&&
                    !as_i64(field(&pending,"repair_cost"),&repair_cost))||
                   (field(&pending,"custom_name")&&
                    !as_string(field(&pending,"custom_name"),&custom_name))||
                   ((!ender_inventory&&
                     !((slot>=0&&slot<ISR_MAIN_SLOTS)||
                       (slot>=ISR_ARMOR0&&slot<ISR_ARMOR0+ISR_ARMOR_SLOTS)||
                       slot==ISR_OFFHAND_SLOT))||
                    (ender_inventory&&
                     (slot<0||slot>=CHEST_LIVE_SLOTS)))||
                   item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767||
                   repair_cost<0||repair_cost>2147483647LL) {
                    fprintf(stderr,"script:%ld: invalid set_inventory\n",line_no);goto bad;
                }
                st=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                st.repair_cost=(i32)repair_cost;
                if(nbt_file){
                    if(!nbt_file->string||count==0||
                       !read_capsule_nbt(nbt_file->value,&tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,"script:%ld: invalid set_inventory nbt_file\n",line_no);goto bad;
                    }
                    st.tag_id=gm_runtime_stack_tag_intern(&r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(st.tag_id==0){
                        fprintf(stderr,"script:%ld: invalid set_inventory NBT\n",line_no);goto bad;
                    }
                }
                if(custom_name&&custom_name[0]){
                    st.custom_name=gm_runtime_item_name_intern(&r,custom_name);
                    if(st.custom_name==0){
                        fprintf(stderr,"script:%ld: invalid set_inventory custom_name\n",line_no);goto bad;
                    }
                }
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,"script:%ld: invalid set_inventory n_ench\n",line_no);goto bad;
                    }
                    st.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char ek[4];long long packed;
                        snprintf(ek,sizeof ek,"e%d",ei);
                        if(!as_i64(field(&pending,ek),&packed)){
                            fprintf(stderr,"script:%ld: missing set_inventory %s\n",line_no,ek);goto bad;
                        }
                        st.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        st.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!(ender_inventory
                        ? gm_runtime_ender_chest_set_slot(&r,(int)slot,st)
                        : gm_runtime_set_inventory_stack(&r,(int)slot,st))){
                    fprintf(stderr,"script:%ld: invalid set_inventory stack\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_selected_slot")) {
                long long slot;
                static const char *const keys[]={"tick","type","slot"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !gm_runtime_set_selected_slot(&r,(int)slot)) {
                    fprintf(stderr,"script:%ld: invalid set_selected_slot\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_air")) {
                long long air;
                static const char *const keys[]={"tick","type","air"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"air"),&air)||
                   !gm_runtime_set_air(&r,(int)air)) {
                    fprintf(stderr,"script:%ld: invalid set_air\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_fire")) {
                long long fire;
                static const char *const keys[]={"tick","type","fire"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !gm_runtime_set_fire(&r,(int)fire)) {
                    fprintf(stderr,"script:%ld: invalid set_fire\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_do_fire_tick")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   !gm_runtime_set_do_fire_tick(&r,(int)enabled)) {
                    fprintf(stderr,"script:%ld: invalid set_do_fire_tick\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"set_do_entity_drops")) {
                long long enabled;
                static const char *const keys[]={"tick","type","enabled"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"enabled"),&enabled)||
                   !gm_runtime_set_do_entity_drops(&r,(int)enabled)) {
                    fprintf(stderr,"script:%ld: invalid set_do_entity_drops\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"inv_view")) {
                long long slot,item,count,meta;
                static const char *const keys[]={"tick","type","slot","item","count","meta"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !gm_runtime_tape_inventory(&r,(int)slot,(int)item,(int)count,(int)meta)){
                    fprintf(stderr,"script:%ld: invalid inv_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"player_view")) {
                long long xp,air,portal_frame=-1,portal_phase=0,loading=0,pinned=0;
                long long fire=0,creative=0,hurt=0,max_hurt=10;
                double frac,portal=0.0,hurt_yaw=0.0,attack_cooldown=1.0;
                static const char *const keys[]={"tick","type","xp_level","xp_frac","air",
                    "portal","portal_frame","portal_phase","loading","texture_animations_pinned",
                    "fire","creative","hurt","max_hurt","hurt_yaw","attack_cooldown"};
                if(!keys_only(&pending,keys,16,err,sizeof err)||
                   !as_i64(field(&pending,"xp_level"),&xp)||
                   !as_double(field(&pending,"xp_frac"),&frac)||
                   !as_i64(field(&pending,"air"),&air)||
                   (field(&pending,"portal")&&!as_double(field(&pending,"portal"),&portal))||
                   (field(&pending,"portal_frame")&&!as_i64(field(&pending,"portal_frame"),&portal_frame))||
                   (field(&pending,"portal_phase")&&!as_i64(field(&pending,"portal_phase"),&portal_phase))||
                   (field(&pending,"loading")&&!as_i64(field(&pending,"loading"),&loading))||
                   (field(&pending,"texture_animations_pinned")&&
                    !as_i64(field(&pending,"texture_animations_pinned"),&pinned))||
                   (field(&pending,"fire")&&!as_i64(field(&pending,"fire"),&fire))||
                   (field(&pending,"creative")&&!as_i64(field(&pending,"creative"),&creative))||
                   (field(&pending,"hurt")&&!as_i64(field(&pending,"hurt"),&hurt))||
                   (field(&pending,"max_hurt")&&!as_i64(field(&pending,"max_hurt"),&max_hurt))||
                   (field(&pending,"hurt_yaw")&&!as_double(field(&pending,"hurt_yaw"),&hurt_yaw))||
                   (field(&pending,"attack_cooldown")&&
                    !as_double(field(&pending,"attack_cooldown"),&attack_cooldown))||
                   /* vanilla drowning runs air down to -20 (damage pulse then
                    * resets it to 0), so negative values are legitimate tape data */
                   xp<0||xp>21863||frac<0||frac>1||air<-20||air>300||
                   portal<0||portal>1||portal_frame < -1||
                   portal_phase<0||loading<0||loading>2||pinned<0||pinned>1||
                   fire<0||fire>1||creative<0||creative>1||hurt<0||hurt>20||
                   max_hurt<0||max_hurt>20||attack_cooldown<0||attack_cooldown>1){
                    fprintf(stderr,"script:%ld: invalid player_view\n",line_no);goto bad;
                }
                gm_runtime_tape_player_view(&r,(int)xp,(float)frac,(int)air,
                    (float)portal,(int)portal_frame,(int)portal_phase,(int)loading,
                    (int)pinned,(int)fire,(int)creative,(int)hurt,
                    (int)max_hurt,(float)hurt_yaw,(float)attack_cooldown);
            } else if (!strcmp(type,"potion_fixture")) {
                long long id=0,amplifier=0,duration=0;
                const JlField *clear=field(&pending,"clear");
                static const char *const keys[]={
                    "tick","type","id","amplifier","duration","clear"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !clear||clear->string||strcmp(clear->value,"true")||
                   id<1||id>255||amplifier<0||amplifier>255||
                   duration<=0||duration>INT_MAX){
                    fprintf(stderr,"script:%ld: invalid potion_fixture\n",line_no);goto bad;
                }
                gm_runtime_potions_clear(&r);
                if(!gm_runtime_potion_add(&r,(int)id,(int)amplifier,(int)duration)){
                    fprintf(stderr,"script:%ld: potion_fixture capacity exceeded\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"player_potions_clear")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid player_potions_clear\n",line_no);goto bad;
                }
                gm_runtime_potions_clear(&r);
            } else if (!strcmp(type,"player_potion_add")) {
                long long id,amplifier,duration,ambient,show_particles;
                static const char *const keys[]={
                    "tick","type","id","amplifier","duration",
                    "ambient","show_particles"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !as_i64(field(&pending,"ambient"),&ambient)||
                   !as_i64(field(&pending,"show_particles"),&show_particles)||
                   !gm_runtime_potion_add_flags(
                       &r,(int)id,(int)amplifier,(int)duration,
                       (int)ambient,(int)show_particles)){
                    fprintf(stderr,"script:%ld: invalid player_potion_add\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"potion_clear")) {
                static const char *const keys[]={"tick","type"};
                if(!keys_only(&pending,keys,2,err,sizeof err)){
                    fprintf(stderr,"script:%ld: invalid potion_clear\n",line_no);goto bad;
                }
                gm_runtime_tape_potions_clear(&r);
            } else if (!strcmp(type,"potion_view")) {
                long long id,amplifier,duration,show=1;
                /* show_particles is optional: tapes recorded before the flag
                 * existed carry visible effects only by ASSUMPTION, so the
                 * legacy default stays 1 (the PotionEffect ctor default). */
                static const char *const keys[]={"tick","type","id","amplifier",
                                                 "duration","show_particles"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   (field(&pending,"show_particles")&&
                    !as_i64(field(&pending,"show_particles"),&show))||
                   !gm_runtime_tape_potion(&r,(int)id,(int)amplifier,(int)duration,
                                           (int)show)){
                    fprintf(stderr,"script:%ld: invalid potion_view\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"armor_view")) {
                /* Recorded ForgeHooks.getTotalArmorValue; -1 clears. */
                long long points;
                static const char *const keys[]={"tick","type","points"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"points"),&points)||points<-1||points>20){
                    fprintf(stderr,"script:%ld: invalid armor_view\n",line_no);goto bad;
                }
                gm_runtime_tape_armor(&r,(int)points);
            } else if (!strcmp(type,"spawn_entity")) {
                long long entity;double x,y,z;
                static const char *const keys[]={"tick","type","entity","x","y","z"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_double(field(&pending,"x"),&x)||!as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   gm_mobs_spawn(&r.mobs,(int)entity,x,y,z)<0){
                    fprintf(stderr,"script:%ld: invalid spawn_entity\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_particle")) {
                long long id;double x,y,z,vx,vy,vz;
                static const char *const keys[]={"tick","type","id","x","y","z",
                                                 "vx","vy","vz"};
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||id<0||id>2||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !gm_particles_live_spawn_recorded(&replay_particles,(int)id,
                       x,y,z,vx,vy,vz,
                       gm_world_sky_light(r.world,(int)floor(x),(int)floor(y),
                                          (int)floor(z)),
                       gm_world_block_light(r.world,(int)floor(x),(int)floor(y),
                                            (int)floor(z)))){
                    fprintf(stderr,"script:%ld: invalid spawn_particle\n",line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_particle_state")) {
                long long id,age,max_age,on_ground;
                long long texture_index,texture_base;
                double prev_x,prev_y,prev_z,x,y,z,vx,vy,vz,scale;
                double color_r,color_g,color_b;
                static const char *const keys[]={
                    "tick","type","id","prev_x","prev_y","prev_z",
                    "x","y","z","vx","vy","vz","age","max_age",
                    "on_ground","scale","color_r","color_g","color_b",
                    "tex","tex_base"};
                if(!keys_only(&pending,keys,21,err,sizeof err)||
                   !as_i64(field(&pending,"id"),&id)||
                   (id!=0&&id!=1&&id!=2&&id!=11&&id!=15&&id!=34&&id!=48)||
                   !as_double(field(&pending,"prev_x"),&prev_x)||
                   !as_double(field(&pending,"prev_y"),&prev_y)||
                   !as_double(field(&pending,"prev_z"),&prev_z)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"max_age"),&max_age)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_double(field(&pending,"scale"),&scale)||
                   !as_double(field(&pending,"color_r"),&color_r)||
                   !as_double(field(&pending,"color_g"),&color_g)||
                   !as_double(field(&pending,"color_b"),&color_b)||
                   !as_i64(field(&pending,"tex"),&texture_index)||
                   !as_i64(field(&pending,"tex_base"),&texture_base)||
                   (on_ground!=0&&on_ground!=1)||
                   !gm_particles_live_spawn_recorded_state(
                       &replay_particles,(int)id,
                       prev_x,prev_y,prev_z,x,y,z,vx,vy,vz,
                       (int)age,(int)max_age,(int)on_ground,
                       (float)scale,(float)color_r,(float)color_g,
                       (float)color_b,(int)texture_index,(int)texture_base,
                       gm_world_sky_light(r.world,(int)floor(x),(int)floor(y),
                                          (int)floor(z)),
                       gm_world_block_light(r.world,(int)floor(x),(int)floor(y),
                                            (int)floor(z)))){
                    fprintf(stderr,"script:%ld: invalid spawn_particle_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_xp_fixture")) {
                double x,y,z,vx,vy,vz;
                long long value,eid,age,delay,color,target_color;
                static const char *const keys[]={
                    "tick","type","x","y","z","vx","vy","vz",
                    "value","eid","age","pickup_delay","color","target_color"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"value"),&value)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"pickup_delay"),&delay)||
                   !as_i64(field(&pending,"color"),&color)||
                   !as_i64(field(&pending,"target_color"),&target_color)||
                   !gm_runtime_spawn_xp_fixture(
                       &r,x,y,z,vx,vy,vz,(int)value,(int)eid,
                       (int)age,(int)delay,(int)color,(int)target_color)){
                    fprintf(stderr,"script:%ld: invalid spawn_xp_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_xp_orb_box")) {
                double min_x,min_y,min_z,max_x,max_y,max_z;
                long long eid;
                static const char *const keys[]={
                    "tick","type","eid","min_x","min_y","min_z",
                    "max_x","max_y","max_z"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"min_x"),&min_x)||
                   !as_double(field(&pending,"min_y"),&min_y)||
                   !as_double(field(&pending,"min_z"),&min_z)||
                   !as_double(field(&pending,"max_x"),&max_x)||
                   !as_double(field(&pending,"max_y"),&max_y)||
                   !as_double(field(&pending,"max_z"),&max_z)||
                   !gm_runtime_restore_xp_orb_box(
                       &r,(int)eid,min_x,min_y,min_z,max_x,max_y,max_z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_xp_orb_box\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_item_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,item,count,meta,age,delay,stationary;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "item","count","meta","age","pickup_delay",
                    "controlled_stationary"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"pickup_delay"),&delay)||
                   !as_i64(field(&pending,"controlled_stationary"),&stationary)||
                   !gm_runtime_spawn_item_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)item,(int)count,
                       (int)meta,(int)age,(int)delay,(int)stationary)){
                    fprintf(stderr,"script:%ld: invalid spawn_item_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_firework_state_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch,prev_yaw,prev_pitch;
                double entity_gaussian;
                long long eid,age,lifetime,ticks_existed,attached_player;
                long long flight,explosion_count,large_blast,twinkle;
                long long item_present,item,count,meta,entity_seed48;
                long long entity_have_gaussian;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                int tag_id=0;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","prev_yaw","prev_pitch","age",
                    "lifetime","ticks_existed","attached_player","flight",
                    "explosion_count","large_blast","twinkle",
                    "firework_item_present","firework_item",
                    "firework_count","firework_meta","entity_seed48",
                    "entity_have_gaussian","entity_gaussian","nbt_file"
                };
                if(nbt_file){
                    if(!nbt_file->string||
                       !read_capsule_nbt(nbt_file->value,
                           &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                            "script:%ld: invalid firework nbt_file\n",
                            line_no);goto bad;
                    }
                    tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(tag_id==0){
                        fprintf(stderr,
                            "script:%ld: invalid firework NBT\n",
                            line_no);goto bad;
                    }
                }
                if(!keys_only(&pending,keys,29,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_double(field(&pending,"prev_yaw"),&prev_yaw)||
                   !as_double(field(&pending,"prev_pitch"),&prev_pitch)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"lifetime"),&lifetime)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_i64(field(&pending,"attached_player"),&attached_player)||
                   !as_i64(field(&pending,"flight"),&flight)||
                   !as_i64(field(&pending,"explosion_count"),&explosion_count)||
                   !as_i64(field(&pending,"large_blast"),&large_blast)||
                   !as_i64(field(&pending,"twinkle"),&twinkle)||
                   !as_i64(field(&pending,"firework_item_present"),&item_present)||
                   !as_i64(field(&pending,"firework_item"),&item)||
                   !as_i64(field(&pending,"firework_count"),&count)||
                   !as_i64(field(&pending,"firework_meta"),&meta)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                       &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                       &entity_gaussian)||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   !gm_runtime_spawn_firework_state_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,
                       (float)yaw,(float)pitch,
                       (float)prev_yaw,(float)prev_pitch,
                       (int)age,(int)lifetime,(int)ticks_existed,
                       (int)attached_player,(int)flight,
                       (int)explosion_count,(int)large_blast,(int)twinkle,
                       (int)item_present,(int)item,(int)count,(int)meta,
                       tag_id,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "spawn_firework_state_fixture\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"spawn_item_state_fixture")) {
                double x,y,z,vx,vy,vz,yaw,hover_start;
                long long eid,item,count,meta,age,delay,health,lifespan;
                long long on_ground,no_gravity,ticks_existed;
                long long fire,in_water,first_update,entity_seed48;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                int tag_id=0;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","hover_start","item","count","meta","age",
                    "ticks_existed","pickup_delay","health","lifespan","on_ground",
                    "no_gravity","fire","in_water","first_update", "entity_seed48",
                    "nbt_file"
                };
                if(nbt_file){
                    if(!nbt_file->string||
                       !read_capsule_nbt(nbt_file->value,&tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,"script:%ld: invalid spawn item nbt_file\n",line_no);goto bad;
                    }
                    tag_id=gm_runtime_stack_tag_intern(&r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(tag_id==0){
                        fprintf(stderr,"script:%ld: invalid spawn item NBT\n",line_no);goto bad;
                    }
                }
                if(!keys_only(&pending,keys,26,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"hover_start"),&hover_start)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_i64(field(&pending,"pickup_delay"),&delay)||
                   !as_i64(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"lifespan"),&lifespan)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !as_i64(field(&pending,"in_water"),&in_water)||
                   !as_i64(field(&pending,"first_update"),&first_update)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   !gm_runtime_spawn_item_state_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,
                       (float)hover_start,(int)item,(int)count,(int)meta,
                       (int)age,(int)delay,(int)health,(int)lifespan,
                       (int)on_ground,(int)no_gravity,
                       (int)ticks_existed,(int)fire,(int)in_water,
                       (int)first_update,(uint64_t)entity_seed48)||
                   (tag_id&&!gm_runtime_set_entity_item_stack_tag(
                       &r,(int)eid,tag_id))){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_item_state_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_falling_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,block,meta,fall_time,no_gravity,no_ground;
                static const char *const keys[]={
                    "tick","type","eid","block","meta","fall_time",
                    "x","y","z","vx","vy","vz",
                    "no_gravity","no_ground"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"block"),&block)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   !as_i64(field(&pending,"fall_time"),&fall_time)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"no_ground"),&no_ground)||
                   !gm_runtime_spawn_falling_fixture(
                       &r,(int)eid,(int)block,(int)meta,(int)fall_time,
                       x,y,z,vx,vy,vz,(int)no_gravity,(int)no_ground)){
                    fprintf(stderr,"script:%ld: invalid spawn_falling_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_arrow_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch;
                long long eid,stationary,fire_ticks=0;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","controlled_stationary","fire_ticks"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"controlled_stationary"),
                           &stationary)||
                   (field(&pending,"fire_ticks")&&
                    !as_i64(field(&pending,"fire_ticks"),&fire_ticks))||
                   yaw != 0.0||pitch != 0.0||
                   !gm_runtime_spawn_arrow_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)stationary,
                       (int)fire_ticks)){
                    fprintf(stderr,"script:%ld: invalid spawn_arrow_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_player_arrow_state_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch,damage,next_gaussian;
                long long eid,ticks_in_air,fire_ticks,knockback,critical;
                long long pickup_status,in_ground,shake,ticks_in_ground;
                long long tile_x,tile_y,tile_z,tile_block,tile_meta;
                long long random_seed48,random_have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","ticks_in_air","fire_ticks","damage",
                    "knockback","critical","pickup_status","in_ground",
                    "shake","ticks_in_ground","tile_x","tile_y","tile_z",
                    "tile_block","tile_meta","random_seed48",
                    "random_have_gaussian","random_next_gaussian"
                };
                if(!keys_only(&pending,keys,28,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"ticks_in_air"),&ticks_in_air)||
                   !as_i64(field(&pending,"fire_ticks"),&fire_ticks)||
                   !as_double(field(&pending,"damage"),&damage)||
                   !as_i64(field(&pending,"knockback"),&knockback)||
                   !as_i64(field(&pending,"critical"),&critical)||
                   !as_i64(field(&pending,"pickup_status"),&pickup_status)||
                   !as_i64(field(&pending,"in_ground"),&in_ground)||
                   !as_i64(field(&pending,"shake"),&shake)||
                   !as_i64(field(&pending,"ticks_in_ground"),
                           &ticks_in_ground)||
                   !as_i64(field(&pending,"tile_x"),&tile_x)||
                   !as_i64(field(&pending,"tile_y"),&tile_y)||
                   !as_i64(field(&pending,"tile_z"),&tile_z)||
                   !as_i64(field(&pending,"tile_block"),&tile_block)||
                   !as_i64(field(&pending,"tile_meta"),&tile_meta)||
                   !as_i64(field(&pending,"random_seed48"),&random_seed48)||
                   !as_i64(field(&pending,"random_have_gaussian"),
                           &random_have_gaussian)||
                   !as_double(field(&pending,"random_next_gaussian"),
                              &next_gaussian)||
                   random_seed48<0||
                   !gm_runtime_spawn_player_arrow_state_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)pitch,
                       (int)ticks_in_air,(int)fire_ticks,damage,
                       (int)knockback,(int)critical,(int)pickup_status,
                       (int)in_ground,(int)shake,(int)ticks_in_ground,
                       (int)tile_x,(int)tile_y,(int)tile_z,
                       (int)tile_block,(int)tile_meta,
                       (uint64_t)random_seed48,(int)random_have_gaussian,
                       next_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "spawn_player_arrow_state_fixture\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"set_arrow_payload_fixture")) {
                long long eid,arrow_kind,potion_type,spectral_duration;
                long long color,custom_color,pickup_item,pickup_meta;
                long long effect_count,time_in_ground;
                PtMobEffect effects[GM_RUNTIME_ARROW_EFFECTS];
                unsigned char effect_flags[GM_RUNTIME_ARROW_EFFECTS];
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                int tag_id=0;
                int fields_valid=1;
                static const char *const keys[]={
                    "tick","type","eid","arrow_kind","potion_type",
                    "spectral_duration","color","custom_color",
                    "pickup_item","pickup_meta","effect_count",
                    "time_in_ground","nbt_file",
                    "e0_id","e0_amp","e0_dur","e0_flags",
                    "e1_id","e1_amp","e1_dur","e1_flags",
                    "e2_id","e2_amp","e2_dur","e2_flags",
                    "e3_id","e3_amp","e3_dur","e3_flags",
                    "e4_id","e4_amp","e4_dur","e4_flags",
                    "e5_id","e5_amp","e5_dur","e5_flags",
                    "e6_id","e6_amp","e6_dur","e6_flags",
                    "e7_id","e7_amp","e7_dur","e7_flags",
                    "e8_id","e8_amp","e8_dur","e8_flags",
                    "e9_id","e9_amp","e9_dur","e9_flags",
                    "e10_id","e10_amp","e10_dur","e10_flags",
                    "e11_id","e11_amp","e11_dur","e11_flags",
                    "e12_id","e12_amp","e12_dur","e12_flags",
                    "e13_id","e13_amp","e13_dur","e13_flags",
                    "e14_id","e14_amp","e14_dur","e14_flags",
                    "e15_id","e15_amp","e15_dur","e15_flags"
                };
                memset(effects,0,sizeof effects);
                memset(effect_flags,0,sizeof effect_flags);
                for(int effect_index=0;
                        effect_index<GM_RUNTIME_ARROW_EFFECTS;
                        ++effect_index){
                    char id_key[16],amp_key[16],dur_key[16],flags_key[16];
                    long long id,amp,dur,flags;
                    snprintf(id_key,sizeof id_key,"e%d_id",effect_index);
                    snprintf(amp_key,sizeof amp_key,"e%d_amp",effect_index);
                    snprintf(dur_key,sizeof dur_key,"e%d_dur",effect_index);
                    snprintf(flags_key,sizeof flags_key,
                             "e%d_flags",effect_index);
                    if(!as_i64(field(&pending,id_key),&id)||
                       !as_i64(field(&pending,amp_key),&amp)||
                       !as_i64(field(&pending,dur_key),&dur)||
                       !as_i64(field(&pending,flags_key),&flags)){
                        fprintf(stderr,
                            "script:%ld: malformed arrow effect fields %d\n",
                            line_no,effect_index);
                        fields_valid=0;
                        break;
                    }
                    effects[effect_index]=(PtMobEffect){
                        (int)id,(int)dur,(int)amp};
                    effect_flags[effect_index]=(unsigned char)flags;
                }
                if(nbt_file){
                    if(!nbt_file->string||
                       !read_capsule_nbt(nbt_file->value,
                           &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                            "script:%ld: invalid arrow payload nbt_file\n",
                            line_no);goto bad;
                    }
                    tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(tag_id==0){
                        fprintf(stderr,
                            "script:%ld: arrow payload tag capacity exceeded\n",
                            line_no);goto bad;
                    }
                }
                if(!keys_only(&pending,keys,77,err,sizeof err)){
                    fprintf(stderr,"script:%ld: %s\n",line_no,err);
                    goto bad;
                }
                if(!fields_valid||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"arrow_kind"),&arrow_kind)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_i64(field(&pending,"spectral_duration"),
                           &spectral_duration)||
                   !as_i64(field(&pending,"color"),&color)||
                   !as_i64(field(&pending,"custom_color"),&custom_color)||
                   !as_i64(field(&pending,"pickup_item"),&pickup_item)||
                   !as_i64(field(&pending,"pickup_meta"),&pickup_meta)||
                   !as_i64(field(&pending,"effect_count"),&effect_count)||
                   !as_i64(field(&pending,"time_in_ground"),
                           &time_in_ground)){
                    fprintf(stderr,
                        "script:%ld: malformed set_arrow_payload_fixture\n",
                        line_no);goto bad;
                }
                if(!gm_runtime_set_arrow_payload(
                        &r,(int)eid,(int)arrow_kind,(int)potion_type,
                        (int)spectral_duration,(int)color,(int)custom_color,
                        effects,(int)effect_count,effect_flags,
                        (int)pickup_item,(int)pickup_meta,tag_id)){
                    fprintf(stderr,
                        "script:%ld: rejected arrow payload eid=%lld kind=%lld "
                        "potion=%lld effects=%lld pickup=%lld:%lld tag=%d\n",
                        line_no,eid,arrow_kind,potion_type,effect_count,
                        pickup_item,pickup_meta,tag_id);goto bad;
                }
                if(!gm_runtime_set_arrow_time_in_ground(
                        &r,(int)eid,(int)time_in_ground)){
                    fprintf(stderr,
                        "script:%ld: rejected arrow time eid=%lld time=%lld\n",
                        line_no,eid,time_in_ground);goto bad;
                }
            } else if (!strcmp(type,"set_potion_payload_fixture")) {
                long long eid,cloud,color,custom_color,effect_count;
                PtMobEffect effects[GM_RUNTIME_POTION_EFFECTS];
                unsigned char effect_flags[GM_RUNTIME_POTION_EFFECTS];
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                int tag_id=0;
                int fields_valid=1;
                static const char *const keys[]={
                    "tick","type","eid","cloud","color",
                    "custom_color","effect_count","nbt_file",
                    "e0_id","e0_amp","e0_dur","e0_flags",
                    "e1_id","e1_amp","e1_dur","e1_flags",
                    "e2_id","e2_amp","e2_dur","e2_flags",
                    "e3_id","e3_amp","e3_dur","e3_flags",
                    "e4_id","e4_amp","e4_dur","e4_flags",
                    "e5_id","e5_amp","e5_dur","e5_flags",
                    "e6_id","e6_amp","e6_dur","e6_flags",
                    "e7_id","e7_amp","e7_dur","e7_flags",
                    "e8_id","e8_amp","e8_dur","e8_flags",
                    "e9_id","e9_amp","e9_dur","e9_flags",
                    "e10_id","e10_amp","e10_dur","e10_flags",
                    "e11_id","e11_amp","e11_dur","e11_flags",
                    "e12_id","e12_amp","e12_dur","e12_flags",
                    "e13_id","e13_amp","e13_dur","e13_flags",
                    "e14_id","e14_amp","e14_dur","e14_flags",
                    "e15_id","e15_amp","e15_dur","e15_flags"
                };
                memset(effects,0,sizeof effects);
                memset(effect_flags,0,sizeof effect_flags);
                for(int effect_index=0;
                        effect_index<GM_RUNTIME_POTION_EFFECTS;
                        ++effect_index){
                    char id_key[16],amp_key[16],dur_key[16],flags_key[16];
                    long long id,amp,dur,flags;
                    snprintf(id_key,sizeof id_key,"e%d_id",effect_index);
                    snprintf(amp_key,sizeof amp_key,"e%d_amp",effect_index);
                    snprintf(dur_key,sizeof dur_key,"e%d_dur",effect_index);
                    snprintf(flags_key,sizeof flags_key,
                             "e%d_flags",effect_index);
                    if(!as_i64(field(&pending,id_key),&id)||
                       !as_i64(field(&pending,amp_key),&amp)||
                       !as_i64(field(&pending,dur_key),&dur)||
                       !as_i64(field(&pending,flags_key),&flags)){
                        fprintf(stderr,
                            "script:%ld: malformed potion effect fields %d\n",
                            line_no,effect_index);
                        fields_valid=0;
                        break;
                    }
                    effects[effect_index]=(PtMobEffect){
                        (int)id,(int)dur,(int)amp};
                    effect_flags[effect_index]=(unsigned char)flags;
                }
                if(nbt_file){
                    if(!nbt_file->string||
                       !read_capsule_nbt(nbt_file->value,
                           &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                            "script:%ld: invalid potion payload nbt_file\n",
                            line_no);goto bad;
                    }
                    tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(tag_id==0){
                        fprintf(stderr,
                            "script:%ld: potion payload tag capacity exceeded\n",
                            line_no);goto bad;
                    }
                }
                if(!keys_only(&pending,keys,72,err,sizeof err)||
                   !fields_valid||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"cloud"),&cloud)||
                   !as_i64(field(&pending,"color"),&color)||
                   !as_i64(field(&pending,"custom_color"),&custom_color)||
                   !as_i64(field(&pending,"effect_count"),&effect_count)||
                   (cloud!=0&&cloud!=1)||
                   (cloud&&tag_id!=0)||
                   (cloud
                       ? !gm_runtime_set_area_effect_cloud_payload(
                           &r,(int)eid,(int)color,(int)custom_color,
                           effects,(int)effect_count,effect_flags)
                       : !gm_runtime_set_potion_payload(
                           &r,(int)eid,(int)color,(int)custom_color,
                           effects,(int)effect_count,effect_flags,tag_id))){
                    fprintf(stderr,
                        "script:%ld: invalid set_potion_payload_fixture\n",
                        line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_primed_tnt_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,fuse;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "fuse"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"fuse"),&fuse)||
                   !gm_runtime_spawn_primed_tnt_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(int)fuse)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_primed_tnt_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_end_crystal_fixture")) {
                double x,y,z;
                long long eid,inner_rotation,show_bottom,has_beam;
                long long beam_x,beam_y,beam_z;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","inner_rotation",
                    "show_bottom","has_beam","beam_x","beam_y","beam_z"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"inner_rotation"),&inner_rotation)||
                   !as_i64(field(&pending,"show_bottom"),&show_bottom)||
                   !as_i64(field(&pending,"has_beam"),&has_beam)||
                   !as_i64(field(&pending,"beam_x"),&beam_x)||
                   !as_i64(field(&pending,"beam_y"),&beam_y)||
                   !as_i64(field(&pending,"beam_z"),&beam_z)||
                   !gm_runtime_spawn_end_crystal_fixture(
                       &r,(int)eid,x,y,z,(int)inner_rotation,
                       (int)show_bottom,(int)has_beam,
                       (int)beam_x,(int)beam_y,(int)beam_z)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_end_crystal_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_small_fireball_fixture")) {
                double x,y,z,vx,vy,vz,ax,ay,az;
                long long eid;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "ax","ay","az"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"ax"),&ax)||
                   !as_double(field(&pending,"ay"),&ay)||
                   !as_double(field(&pending,"az"),&az)||
                   !gm_runtime_spawn_small_fireball_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,ax,ay,az)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_small_fireball_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_wither_fixture")) {
                long long eid,invul,ticks,hurt,death,resistant,break_counter;
                long long seed,have_gaussian;
                double x,y,z,vx,vy,vz,yaw,pitch,render_yaw,health,gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","render_yaw_offset","health",
                    "invul_time","ticks_existed","hurt_time","death_time",
                    "hurt_resistant_time","block_break_counter",
                    "entity_seed48","entity_have_gaussian",
                    "entity_gaussian"
                };
                if(!keys_only(&pending,keys,22,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_double(field(&pending,"render_yaw_offset"),&render_yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"invul_time"),&invul)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks)||
                   !as_i64(field(&pending,"hurt_time"),&hurt)||
                   !as_i64(field(&pending,"death_time"),&death)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&resistant)||
                   !as_i64(field(&pending,"block_break_counter"),
                           &break_counter)||
                   !as_i64(field(&pending,"entity_seed48"),&seed)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   eid<0||eid>INT_MAX||invul<0||invul>INT_MAX||
                   ticks<0||ticks>INT_MAX||hurt<0||hurt>INT_MAX||
                   death<0||death>INT_MAX||resistant<0||resistant>INT_MAX||
                   break_counter<0||break_counter>INT_MAX||seed<0||
                   seed>((1LL<<48)-1)||have_gaussian<0||have_gaussian>1||
                   !gm_runtime_spawn_wither_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)pitch,
                       (float)render_yaw,(float)health,(int)invul,(int)ticks,
                       (int)hurt,(int)death,(int)resistant,
                       (int)break_counter,(uint64_t)seed,
                       (int)have_gaussian,gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_wither_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_wither_base_state")) {
                long long eid,no_ai,no_gravity,air,fire,on_ground,in_water;
                long long sound,recently_hit,attacking_player;
                double fall_distance,last_damage;
                static const char *const keys[]={
                    "tick","type","eid","no_ai","no_gravity","air",
                    "fire","on_ground","fall_distance","in_water",
                    "living_sound_time","last_damage","recently_hit",
                    "attacking_player"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"air"),&air)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_double(field(&pending,"fall_distance"),&fall_distance)||
                   !as_i64(field(&pending,"in_water"),&in_water)||
                   !as_i64(field(&pending,"living_sound_time"),&sound)||
                   !as_double(field(&pending,"last_damage"),&last_damage)||
                   !as_i64(field(&pending,"recently_hit"),&recently_hit)||
                   !as_i64(field(&pending,"attacking_player"),
                           &attacking_player)||eid<0||eid>INT_MAX||
                   !gm_runtime_restore_wither_base_state(
                       &r,(int)eid,(int)no_ai,(int)no_gravity,(int)air,
                       (int)fire,(int)on_ground,(float)fall_distance,
                       (int)in_water,(int)sound,(float)last_damage,
                       (int)recently_hit,(int)attacking_player)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_wither_base_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_wither_head_state")) {
                long long eid,head,target,target_player,next,idle;
                double yaw,pitch,prev_yaw,prev_pitch;
                static const char *const keys[]={
                    "tick","type","eid","head","target_eid",
                    "target_is_player","next_update","idle_updates",
                    "yaw","pitch","prev_yaw","prev_pitch"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"head"),&head)||
                   !as_i64(field(&pending,"target_eid"),&target)||
                   !as_i64(field(&pending,"target_is_player"),&target_player)||
                   !as_i64(field(&pending,"next_update"),&next)||
                   !as_i64(field(&pending,"idle_updates"),&idle)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_double(field(&pending,"prev_yaw"),&prev_yaw)||
                   !as_double(field(&pending,"prev_pitch"),&prev_pitch)||
                   !gm_runtime_set_wither_head_state(
                       &r,(int)eid,(int)head,(int)target,(int)target_player,
                       (int)next,(int)idle,(float)yaw,(float)pitch,
                       (float)prev_yaw,(float)prev_pitch)){
                    fprintf(stderr,
                            "script:%ld: invalid set_wither_head_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_wither_ai_state")) {
                long long eid,target,target_player,revenge,revenge_player;
                long long revenge_timer,target_tick,goal_tick,invul_active;
                long long hurt_active,hurt_target,hurt_player,hurt_old;
                long long hurt_unseen,nearest_active;
                long long ranged_active;
                long long attack_time,see_time;
                static const char *const keys[]={
                    "tick","type","eid","target_eid","target_is_player",
                    "revenge_eid","revenge_is_player","revenge_timer",
                    "hurt_target_task_active","hurt_target_eid",
                    "hurt_target_is_player","hurt_revenge_timer_old",
                    "hurt_target_unseen_ticks",
                    "nearest_target_task_active",
                    "target_task_tick","goal_task_tick",
                    "invul_task_active","ranged_task_active","ranged_attack_time",
                    "ranged_see_time"
                };
                if(!keys_only(&pending,keys,20,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"target_eid"),&target)||
                   !as_i64(field(&pending,"target_is_player"),&target_player)||
                   !as_i64(field(&pending,"revenge_eid"),&revenge)||
                   !as_i64(field(&pending,"revenge_is_player"),
                           &revenge_player)||
                   !as_i64(field(&pending,"revenge_timer"),&revenge_timer)||
                   !as_i64(field(&pending,"hurt_target_task_active"),
                           &hurt_active)||
                   !as_i64(field(&pending,"hurt_target_eid"),&hurt_target)||
                   !as_i64(field(&pending,"hurt_target_is_player"),
                           &hurt_player)||
                   !as_i64(field(&pending,"hurt_revenge_timer_old"),
                           &hurt_old)||
                   !as_i64(field(&pending,"hurt_target_unseen_ticks"),
                           &hurt_unseen)||
                   !as_i64(field(&pending,"nearest_target_task_active"),
                           &nearest_active)||
                   !as_i64(field(&pending,"target_task_tick"),&target_tick)||
                   !as_i64(field(&pending,"goal_task_tick"),&goal_tick)||
                   !as_i64(field(&pending,"invul_task_active"),
                           &invul_active)||
                   !as_i64(field(&pending,"ranged_task_active"),
                           &ranged_active)||
                   !as_i64(field(&pending,"ranged_attack_time"),&attack_time)||
                   !as_i64(field(&pending,"ranged_see_time"),&see_time)||
                   !gm_runtime_restore_wither_ai_state(
                       &r,(int)eid,(int)target,(int)target_player,
                       (int)revenge,(int)revenge_player,(int)revenge_timer,
                       (int)hurt_active,(int)hurt_target,(int)hurt_player,
                       (int)hurt_old,(int)hurt_unseen,(int)nearest_active,
                       (int)target_tick,(int)goal_tick,(int)invul_active,
                       (int)ranged_active,
                       (int)attack_time,(int)see_time)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_wither_ai_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_wither_rotation_state")) {
                long long eid,body_tick;
                double head_yaw,prev_head_yaw,prev_body_yaw,body_head_yaw;
                static const char *const keys[]={
                    "tick","type","eid","rotation_yaw_head",
                    "prev_rotation_yaw_head","prev_render_yaw_offset",
                    "body_rotation_tick_counter",
                    "body_prev_render_yaw_head"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"rotation_yaw_head"),
                              &head_yaw)||
                   !as_double(field(&pending,"prev_rotation_yaw_head"),
                              &prev_head_yaw)||
                   !as_double(field(&pending,"prev_render_yaw_offset"),
                              &prev_body_yaw)||
                   !as_i64(field(&pending,"body_rotation_tick_counter"),
                           &body_tick)||
                   !as_double(field(&pending,"body_prev_render_yaw_head"),
                              &body_head_yaw)||
                   eid<0||eid>INT_MAX||body_tick<0||body_tick>INT_MAX||
                   !gm_runtime_restore_wither_rotation_state(
                       &r,(int)eid,(float)head_yaw,(float)prev_head_yaw,
                       (float)prev_body_yaw,(int)body_tick,
                       (float)body_head_yaw)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_wither_rotation_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_wither_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   !gm_runtime_set_wither_uuid(&r,(int)eid,most,least)){
                    fprintf(stderr,"script:%ld: invalid set_wither_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_wither_skull_fixture")) {
                long long eid,shooter,invulnerable,ticks_in_air,life;
                double x,y,z,vx,vy,vz,ax,ay,az,yaw,pitch;
                static const char *const keys[]={
                    "tick","type","eid","shooter_eid","x","y","z",
                    "vx","vy","vz","ax","ay","az","yaw","pitch",
                    "invulnerable","ticks_in_air","life"
                };
                if(!keys_only(&pending,keys,18,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"shooter_eid"),&shooter)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"ax"),&ax)||
                   !as_double(field(&pending,"ay"),&ay)||
                   !as_double(field(&pending,"az"),&az)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"invulnerable"),&invulnerable)||
                   !as_i64(field(&pending,"ticks_in_air"),&ticks_in_air)||
                   !as_i64(field(&pending,"life"),&life)||
                   !gm_runtime_spawn_wither_skull_fixture(
                       &r,(int)eid,(int)shooter,x,y,z,vx,vy,vz,ax,ay,az,
                       (float)yaw,(float)pitch,(int)invulnerable,
                       (int)ticks_in_air,(int)life)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_wither_skull_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_llama_spit_fixture")) {
                long long eid,owner_eid,owner_uuid_present;
                long long owner_uuid_most,owner_uuid_least;
                long long ticks_existed,no_gravity;
                double x,y,z,vx,vy,vz,yaw,pitch;
                static const char *const keys[]={
                    "tick","type","eid","owner_eid",
                    "owner_uuid_present","owner_uuid_most",
                    "owner_uuid_least","x","y","z","vx","vy","vz",
                    "yaw","pitch","ticks_existed","no_gravity"
                };
                if(!keys_only(&pending,keys,17,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"owner_eid"),&owner_eid)||
                   !as_i64(field(&pending,"owner_uuid_present"),
                           &owner_uuid_present)||
                   !as_i64(field(&pending,"owner_uuid_most"),
                           &owner_uuid_most)||
                   !as_i64(field(&pending,"owner_uuid_least"),
                           &owner_uuid_least)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"ticks_existed"),
                           &ticks_existed)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !gm_runtime_spawn_llama_spit_fixture(
                       &r,(int)eid,(int)owner_eid,
                       (int)owner_uuid_present,owner_uuid_most,
                       owner_uuid_least,x,y,z,vx,vy,vz,(float)yaw,
                       (float)pitch,(int)ticks_existed,(int)no_gravity)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_llama_spit_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_throwable_state_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch,prev_yaw,prev_pitch;
                double random_next_gaussian;
                long long eid,projectile_type,potion_item,potion_type;
                long long age,ticks_in_air,player_thrower;
                long long thrower_player_pending,ignore_player;
                long long ignore_player_time,pearl_private_thrower;
                long long throwable_shake,in_ground,ticks_in_ground;
                long long tile_x,tile_y,tile_z,tile_block;
                long long portal_counter,in_portal;
                long long portal_cooldown;
                long long last_portal_pos_valid;
                long long last_portal_x,last_portal_y,last_portal_z;
                double last_portal_vec_x,last_portal_vec_y;
                long long teleport_direction;
                long long client_random_valid,client_entity_seed48;
                long long random_seed48,random_have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","projectile_type",
                    "potion_item","potion_type","x","y","z",
                    "vx","vy","vz","yaw","pitch","prev_yaw",
                    "prev_pitch","age","ticks_in_air",
                    "player_thrower","thrower_player_pending",
                    "ignore_player",
                    "ignore_player_time","pearl_private_thrower",
                    "throwable_shake","in_ground","ticks_in_ground",
                    "tile_x","tile_y","tile_z","tile_block",
                    "portal_counter","in_portal",
                    "portal_cooldown",
                    "last_portal_pos_valid","last_portal_x",
                    "last_portal_y","last_portal_z",
                    "last_portal_vec_x","last_portal_vec_y",
                    "teleport_direction",
                    "client_random_valid","client_entity_seed48",
                    "random_seed48","random_have_gaussian",
                    "random_next_gaussian"
                };
                if(!keys_only(&pending,keys,45,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"projectile_type"),
                           &projectile_type)||
                   !as_i64(field(&pending,"potion_item"),&potion_item)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_double(field(&pending,"prev_yaw"),&prev_yaw)||
                   !as_double(field(&pending,"prev_pitch"),&prev_pitch)||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"ticks_in_air"),&ticks_in_air)||
                   !as_i64(field(&pending,"player_thrower"),
                           &player_thrower)||
                   !as_i64(field(&pending,"thrower_player_pending"),
                           &thrower_player_pending)||
                   !as_i64(field(&pending,"ignore_player"),&ignore_player)||
                   !as_i64(field(&pending,"ignore_player_time"),
                           &ignore_player_time)||
                   !as_i64(field(&pending,"pearl_private_thrower"),
                           &pearl_private_thrower)||
                   !as_i64(field(&pending,"throwable_shake"),
                           &throwable_shake)||
                   !as_i64(field(&pending,"in_ground"),&in_ground)||
                   !as_i64(field(&pending,"ticks_in_ground"),
                           &ticks_in_ground)||
                   !as_i64(field(&pending,"tile_x"),&tile_x)||
                   !as_i64(field(&pending,"tile_y"),&tile_y)||
                   !as_i64(field(&pending,"tile_z"),&tile_z)||
                   !as_i64(field(&pending,"tile_block"),&tile_block)||
                   !as_i64(field(&pending,"portal_counter"),
                           &portal_counter)||
                   !as_i64(field(&pending,"in_portal"),&in_portal)||
                   !as_i64(field(&pending,"portal_cooldown"),
                           &portal_cooldown)||
                   !as_i64(field(&pending,"last_portal_pos_valid"),
                           &last_portal_pos_valid)||
                   !as_i64(field(&pending,"last_portal_x"),
                           &last_portal_x)||
                   !as_i64(field(&pending,"last_portal_y"),
                           &last_portal_y)||
                   !as_i64(field(&pending,"last_portal_z"),
                           &last_portal_z)||
                   !as_double(field(&pending,"last_portal_vec_x"),
                              &last_portal_vec_x)||
                   !as_double(field(&pending,"last_portal_vec_y"),
                              &last_portal_vec_y)||
                   !as_i64(field(&pending,"teleport_direction"),
                           &teleport_direction)||
                   !as_i64(field(&pending,"client_random_valid"),
                           &client_random_valid)||
                   !as_i64(field(&pending,"client_entity_seed48"),
                           &client_entity_seed48)||
                   !as_i64(field(&pending,"random_seed48"),&random_seed48)||
                   !as_i64(field(&pending,"random_have_gaussian"),
                           &random_have_gaussian)||
                   !as_double(field(&pending,"random_next_gaussian"),
                              &random_next_gaussian)||
                   !gm_runtime_spawn_throwable_state_fixture(
                       &r,(int)eid,(int)projectile_type,(int)potion_item,
                       (int)potion_type,x,y,z,vx,vy,vz,(float)yaw,
                       (float)pitch,(float)prev_yaw,(float)prev_pitch,
                       (int)age,(int)ticks_in_air,(int)player_thrower,
                       (int)thrower_player_pending,
                       (int)ignore_player,(int)ignore_player_time,
                       (int)pearl_private_thrower,
                       (int)throwable_shake,(int)in_ground,
                       (int)ticks_in_ground,(int)tile_x,(int)tile_y,
                       (int)tile_z,(int)tile_block,(int)portal_counter,
                       (int)in_portal,
                       (int)portal_cooldown,
                       (int)last_portal_pos_valid,
                       (int)last_portal_x,(int)last_portal_y,
                       (int)last_portal_z,last_portal_vec_x,
                       last_portal_vec_y,(int)teleport_direction,
                       (int)client_random_valid,
                       (uint64_t)client_entity_seed48,
                       (uint64_t)random_seed48,
                       (int)random_have_gaussian,
                       random_next_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "spawn_throwable_state_fixture\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"spawn_potion_fixture")) {
                double x,y,z,vx,vy,vz;
                long long eid,potion_item,potion_type,age;
                long long player_thrower=1,ignore_player=0;
                long long ignore_player_time=0;
                static const char *const keys[]={
                    "tick","type","eid","potion_item","potion_type",
                    "x","y","z","vx","vy","vz","age",
                    "player_thrower","ignore_player","ignore_player_time"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"potion_item"),&potion_item)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_i64(field(&pending,"age"),&age)||
                   (field(&pending,"player_thrower")&&
                    !as_i64(field(&pending,"player_thrower"),
                            &player_thrower))||
                   (field(&pending,"ignore_player")&&
                    !as_i64(field(&pending,"ignore_player"),
                            &ignore_player))||
                   (field(&pending,"ignore_player_time")&&
                    !as_i64(field(&pending,"ignore_player_time"),
                            &ignore_player_time))||
                   !gm_runtime_spawn_potion_state_fixture(
                       &r,(int)eid,(int)potion_item,(int)potion_type,
                       x,y,z,vx,vy,vz,(int)age,(int)player_thrower,
                       (int)ignore_player,(int)ignore_player_time)){
                    fprintf(stderr,"script:%ld: invalid spawn_potion_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_area_effect_cloud_fixture")) {
                double x,y,z,vx=0.0,vy=0.0,vz=0.0;
                double yaw=0.0,pitch=0.0,prev_yaw=0.0,prev_pitch=0.0;
                double radius,radius_on_use,radius_per_tick;
                long long eid,potion_type,age,duration,wait_time;
                long long reapplication_delay,next_application;
                long long player_owner=0;
                long long entity_seed48=-1;
                long long duration_on_use=0,ignore_radius=-1;
                long long particle=GM_PARTICLE_SPELL_MOB;
                long long particle_param1=0,particle_param2=0;
                static const char *const keys[]={
                    "tick","type","eid","potion_type","x","y","z",
                    "age","duration","wait_time","reapplication_delay",
                    "radius","radius_on_use","radius_per_tick",
                    "next_application","player_owner","entity_seed48",
                    "duration_on_use","ignore_radius","particle",
                    "particle_param1","particle_param2",
                    "vx","vy","vz","yaw","pitch","prev_yaw",
                    "prev_pitch"
                };
                if(!keys_only(&pending,keys,29,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"potion_type"),&potion_type)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   (field(&pending,"vx")&&
                    !as_double(field(&pending,"vx"),&vx))||
                   (field(&pending,"vy")&&
                    !as_double(field(&pending,"vy"),&vy))||
                   (field(&pending,"vz")&&
                    !as_double(field(&pending,"vz"),&vz))||
                   (field(&pending,"yaw")&&
                    !as_double(field(&pending,"yaw"),&yaw))||
                   (field(&pending,"pitch")&&
                    !as_double(field(&pending,"pitch"),&pitch))||
                   (field(&pending,"prev_yaw")&&
                    !as_double(field(&pending,"prev_yaw"),&prev_yaw))||
                   (field(&pending,"prev_pitch")&&
                    !as_double(field(&pending,"prev_pitch"),&prev_pitch))||
                   !as_i64(field(&pending,"age"),&age)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !as_i64(field(&pending,"wait_time"),&wait_time)||
                   !as_i64(field(&pending,"reapplication_delay"),
                           &reapplication_delay)||
                   !as_double(field(&pending,"radius"),&radius)||
                   !as_double(field(&pending,"radius_on_use"),&radius_on_use)||
                   !as_double(field(&pending,"radius_per_tick"),
                              &radius_per_tick)||
                   !as_i64(field(&pending,"next_application"),
                           &next_application)||
                   (field(&pending,"player_owner")&&
                    !as_i64(field(&pending,"player_owner"),&player_owner))||
                   (field(&pending,"entity_seed48")&&
                    !as_i64(field(&pending,"entity_seed48"),
                            &entity_seed48))||
                   (field(&pending,"duration_on_use")&&
                    !as_i64(field(&pending,"duration_on_use"),
                            &duration_on_use))||
                   (field(&pending,"ignore_radius")&&
                    !as_i64(field(&pending,"ignore_radius"),
                            &ignore_radius))||
                   (field(&pending,"particle")&&
                    !as_i64(field(&pending,"particle"),&particle))||
                   (field(&pending,"particle_param1")&&
                    !as_i64(field(&pending,"particle_param1"),
                            &particle_param1))||
                   (field(&pending,"particle_param2")&&
                    !as_i64(field(&pending,"particle_param2"),
                            &particle_param2))||
                   !gm_runtime_spawn_area_effect_cloud_state_fixture(
                       &r,(int)eid,(int)potion_type,x,y,z,
                       (int)age,(int)duration,(int)wait_time,
                       (int)reapplication_delay,(float)radius,
                       (float)radius_on_use,(float)radius_per_tick,
                       (int)next_application,(int)player_owner)||
                   !gm_runtime_set_area_effect_cloud_extended_state(
                       &r,(int)eid,(int)duration_on_use,
                       ignore_radius>=0 ? (int)ignore_radius
                           : age<wait_time,
                       (int)particle,(int)particle_param1,
                       (int)particle_param2)||
                   !gm_runtime_set_area_effect_cloud_kinematics(
                       &r,(int)eid,vx,vy,vz,(float)yaw,(float)pitch,
                       (float)prev_yaw,(float)prev_pitch)||
                   (entity_seed48>=0&&
                    !gm_runtime_set_area_effect_cloud_random_seed48(
                        &r,(int)eid,(uint64_t)entity_seed48))){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_area_effect_cloud_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(
                    type,"set_area_effect_cloud_common_state")) {
                long long cloud_eid,dimension,air,fire,portal_cooldown;
                long long on_ground,no_gravity,invulnerable,silent,glowing;
                long long update_blocked,in_water,first_update;
                long long server_entity_seed48;
                long long server_entity_have_gaussian;
                double fall_distance,prev_x,prev_y,prev_z;
                double last_tick_x,last_tick_y,last_tick_z;
                double server_entity_gaussian;
                static const char *const keys[]={
                    "tick","type","cloud_eid","dimension","air","fire",
                    "portal_cooldown","on_ground","no_gravity",
                    "invulnerable","silent","glowing","update_blocked",
                    "in_water","first_update","fall_distance","prev_x",
                    "prev_y","prev_z","last_tick_x","last_tick_y",
                    "last_tick_z","server_entity_seed48",
                    "server_entity_have_gaussian",
                    "server_entity_gaussian"
                };
                if(!keys_only(&pending,keys,25,err,sizeof err)||
                   !as_i64(field(&pending,"cloud_eid"),&cloud_eid)||
                   !as_i64(field(&pending,"dimension"),&dimension)||
                   !as_i64(field(&pending,"air"),&air)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !as_i64(field(&pending,"portal_cooldown"),
                           &portal_cooldown)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"invulnerable"),&invulnerable)||
                   !as_i64(field(&pending,"silent"),&silent)||
                   !as_i64(field(&pending,"glowing"),&glowing)||
                   !as_i64(field(&pending,"update_blocked"),
                           &update_blocked)||
                   !as_i64(field(&pending,"in_water"),&in_water)||
                   !as_i64(field(&pending,"first_update"),&first_update)||
                   !as_double(field(&pending,"fall_distance"),
                              &fall_distance)||
                   !as_double(field(&pending,"prev_x"),&prev_x)||
                   !as_double(field(&pending,"prev_y"),&prev_y)||
                   !as_double(field(&pending,"prev_z"),&prev_z)||
                   !as_double(field(&pending,"last_tick_x"),
                              &last_tick_x)||
                   !as_double(field(&pending,"last_tick_y"),
                              &last_tick_y)||
                   !as_double(field(&pending,"last_tick_z"),
                              &last_tick_z)||
                   !as_i64(field(&pending,"server_entity_seed48"),
                           &server_entity_seed48)||
                   !as_i64(field(&pending,"server_entity_have_gaussian"),
                           &server_entity_have_gaussian)||
                   !as_double(field(&pending,"server_entity_gaussian"),
                              &server_entity_gaussian)||
                   cloud_eid<=0||cloud_eid>INT_MAX||
                   server_entity_seed48<0||
                   !gm_runtime_set_area_effect_cloud_common_state(
                       &r,(int)cloud_eid,(int)dimension,(int)air,(int)fire,
                       (int)portal_cooldown,(int)on_ground,(int)no_gravity,
                       (int)invulnerable,(int)silent,(int)glowing,
                       (int)update_blocked,(int)in_water,(int)first_update,
                       (float)fall_distance,prev_x,prev_y,prev_z,
                       last_tick_x,last_tick_y,last_tick_z,
                       (uint64_t)server_entity_seed48,
                       (int)server_entity_have_gaussian,
                       server_entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid area-effect-cloud common state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_area_effect_cloud_identity")) {
                long long cloud_eid,uuid_most,uuid_least;
                long long owner_present,owner_eid;
                long long owner_uuid_most,owner_uuid_least;
                static const char *const keys[]={
                    "tick","type","cloud_eid","uuid_most","uuid_least",
                    "owner_present","owner_eid","owner_uuid_most",
                    "owner_uuid_least"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"cloud_eid"),&cloud_eid)||
                   !as_i64(field(&pending,"uuid_most"),&uuid_most)||
                   !as_i64(field(&pending,"uuid_least"),&uuid_least)||
                   !as_i64(field(&pending,"owner_present"),&owner_present)||
                   !as_i64(field(&pending,"owner_eid"),&owner_eid)||
                   !as_i64(field(&pending,"owner_uuid_most"),
                           &owner_uuid_most)||
                   !as_i64(field(&pending,"owner_uuid_least"),
                           &owner_uuid_least)||
                   cloud_eid<=0||cloud_eid>INT_MAX||
                   owner_eid < -1||owner_eid>INT_MAX||
                   !gm_runtime_set_area_effect_cloud_identity(
                       &r,(int)cloud_eid,uuid_most,uuid_least,
                       (int)owner_present,(int)owner_eid,
                       owner_uuid_most,owner_uuid_least)){
                    fprintf(stderr,
                            "script:%ld: invalid area-effect-cloud identity\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_area_effect_cloud_deadline")) {
                long long cloud_eid,target_eid,deadline;
                static const char *const keys[]={
                    "tick","type","cloud_eid","target_eid","deadline"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"cloud_eid"),&cloud_eid)||
                   !as_i64(field(&pending,"target_eid"),&target_eid)||
                   !as_i64(field(&pending,"deadline"),&deadline)||
                   cloud_eid<=0||cloud_eid>INT_MAX||
                   target_eid<0||target_eid>INT_MAX||
                   deadline<=0||deadline>INT_MAX||
                   !gm_runtime_set_area_effect_cloud_deadline(
                       &r,(int)cloud_eid,(int)target_eid,(int)deadline)){
                    fprintf(stderr,
                            "script:%ld: invalid area-effect-cloud deadline\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_fish_hook_fixture")) {
                long long eid,state,in_ground,ticks_in_ground,ticks_in_air;
                long long ticks_catchable,ticks_caught_delay;
                long long ticks_catchable_delay,lure,luck,caught_eid;
                long long entity_seed48,entity_have_gaussian;
                double x,y,z,vx,vy,vz,yaw,pitch,approach_angle;
                double entity_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","fish_state","in_ground",
                    "ticks_in_ground","ticks_in_air","ticks_catchable",
                    "ticks_caught_delay","ticks_catchable_delay",
                    "fish_approach_angle","lure","luck","caught_eid",
                    "entity_seed48","entity_have_gaussian",
                    "entity_gaussian"
                };
                if(!keys_only(&pending,keys,25,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"fish_state"),&state)||
                   !as_i64(field(&pending,"in_ground"),&in_ground)||
                   !as_i64(field(&pending,"ticks_in_ground"),
                           &ticks_in_ground)||
                   !as_i64(field(&pending,"ticks_in_air"),&ticks_in_air)||
                   !as_i64(field(&pending,"ticks_catchable"),
                           &ticks_catchable)||
                   !as_i64(field(&pending,"ticks_caught_delay"),
                           &ticks_caught_delay)||
                   !as_i64(field(&pending,"ticks_catchable_delay"),
                           &ticks_catchable_delay)||
                   !as_double(field(&pending,"fish_approach_angle"),
                              &approach_angle)||
                   !as_i64(field(&pending,"lure"),&lure)||
                   !as_i64(field(&pending,"luck"),&luck)||
                   !as_i64(field(&pending,"caught_eid"),&caught_eid)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                              &entity_gaussian)||
                   eid<=0||eid>INT_MAX||state<0||state>2||
                   in_ground<0||in_ground>1||ticks_in_ground<0||
                   ticks_in_ground>INT_MAX||ticks_in_air<0||
                   ticks_in_air>INT_MAX||ticks_catchable<0||
                   ticks_catchable>INT_MAX||
                   ticks_caught_delay<INT_MIN||ticks_caught_delay>INT_MAX||
                   ticks_catchable_delay<INT_MIN||
                   ticks_catchable_delay>INT_MAX||lure<0||lure>3||
                   luck<0||luck>3||caught_eid<0||caught_eid>INT_MAX||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   entity_have_gaussian<0||entity_have_gaussian>1||
                   !isfinite(x)||!isfinite(y)||!isfinite(z)||
                   !isfinite(vx)||!isfinite(vy)||!isfinite(vz)||
                   !isfinite(yaw)||!isfinite(pitch)||
                   !isfinite(approach_angle)||!isfinite(entity_gaussian)||
                   !gm_runtime_spawn_fish_hook_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)pitch,
                       (int)state,(int)in_ground,(int)ticks_in_ground,
                       (int)ticks_in_air,(int)ticks_catchable,
                       (int)ticks_caught_delay,(int)ticks_catchable_delay,
                       (float)approach_angle,(int)lure,(int)luck,
                       (int)caught_eid,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_fish_hook_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_minecart_fixture")) {
                long long kind,eid,reverse,rolling_amplitude;
                long long rolling_direction,fuel,tnt_fuse;
                long long hopper_enabled,transfer_cooldown;
                long long entity_seed48,entity_have_gaussian;
                double x,y,z,vx,vy,vz,yaw,pitch,damage,push_x,push_z;
                double entity_gaussian;
                static const char *const keys[]={
                    "tick","type","kind","eid","x","y","z",
                    "vx","vy","vz","yaw","pitch","reverse",
                    "rolling_amplitude","rolling_direction","damage",
                    "fuel","push_x","push_z","tnt_fuse",
                    "hopper_enabled","transfer_cooldown","entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,25,err,sizeof err)||
                   !as_i64(field(&pending,"kind"),&kind)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"reverse"),&reverse)||
                   !as_i64(field(&pending,"rolling_amplitude"),
                           &rolling_amplitude)||
                   !as_i64(field(&pending,"rolling_direction"),
                           &rolling_direction)||
                   !as_double(field(&pending,"damage"),&damage)||
                   !as_i64(field(&pending,"fuel"),&fuel)||
                   !as_double(field(&pending,"push_x"),&push_x)||
                   !as_double(field(&pending,"push_z"),&push_z)||
                   !as_i64(field(&pending,"tnt_fuse"),&tnt_fuse)||
                   !as_i64(field(&pending,"hopper_enabled"),&hopper_enabled)||
                   !as_i64(field(&pending,"transfer_cooldown"),
                           &transfer_cooldown)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &entity_have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),
                              &entity_gaussian)||
                   kind<0||kind>6||kind==6||eid<0||eid>INT_MAX||
                   reverse<0||reverse>1||rolling_amplitude<0||
                   rolling_amplitude>INT_MAX||rolling_direction==0||
                   rolling_direction<INT_MIN||rolling_direction>INT_MAX||
                   fuel<-32768||fuel>32767||tnt_fuse<-1||
                   tnt_fuse>INT_MAX||hopper_enabled<0||hopper_enabled>1||
                   transfer_cooldown<INT_MIN||transfer_cooldown>INT_MAX||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   entity_have_gaussian<0||entity_have_gaussian>1||
                   !isfinite(x)||!isfinite(y)||!isfinite(z)||
                   !isfinite(vx)||!isfinite(vy)||!isfinite(vz)||
                   !isfinite(yaw)||!isfinite(pitch)||!isfinite(damage)||
                   !isfinite(push_x)||!isfinite(push_z)||
                   !isfinite(entity_gaussian)||
                   !gm_runtime_spawn_minecart_fixture(
                       &r,(int)kind,(int)eid,x,y,z,vx,vy,vz,(float)yaw)||
                   !gm_runtime_minecart_set_base_state(
                       &r,(int)eid,(int)reverse,(int)rolling_amplitude,
                       (int)rolling_direction,(float)damage,(float)pitch)||
                   !gm_runtime_minecart_set_state(
                       &r,(int)eid,(int)fuel,push_x,push_z,(int)tnt_fuse,
                       (int)hopper_enabled,(int)transfer_cooldown)||
                   !gm_runtime_minecart_set_random_state(
                       &r,(int)eid,(uint64_t)entity_seed48,
                       (int)entity_have_gaussian,entity_gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_minecart_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_minecart_spawner_state")) {
                long long eid,entity,delay,min_delay,max_delay,spawn_count;
                long long max_nearby,activate_range,spawn_range;
                int default_entity_nbt=0;
                const JlField *spawn_nbt_file=
                    field(&pending,"spawn_nbt_file");
                const JlField *default_field=
                    field(&pending,"default_entity_nbt");
                uint8_t *spawn_nbt=NULL;
                size_t spawn_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","eid","entity","delay","min_delay",
                    "max_delay","spawn_count","max_nearby",
                    "activate_range","spawn_range","spawn_nbt_file",
                    "default_entity_nbt"
                };
                if(!keys_only(&pending,keys,13,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"delay"),&delay)||
                   !as_i64(field(&pending,"min_delay"),&min_delay)||
                   !as_i64(field(&pending,"max_delay"),&max_delay)||
                   !as_i64(field(&pending,"spawn_count"),&spawn_count)||
                   !as_i64(field(&pending,"max_nearby"),&max_nearby)||
                   !as_i64(field(&pending,"activate_range"),&activate_range)||
                   !as_i64(field(&pending,"spawn_range"),&spawn_range)||
                   eid<0||eid>INT_MAX||entity<=0||entity>255||
                   delay<-1||delay>32767||min_delay<0||min_delay>32767||
                   max_delay<0||max_delay>32767||spawn_count<0||
                   spawn_count>32767||max_nearby<0||max_nearby>32767||
                   activate_range<0||activate_range>32767||spawn_range<0||
                   spawn_range>32767||!spawn_nbt_file||
                   !spawn_nbt_file->string||!default_field||
                   !as_rule_bool(default_field,&default_entity_nbt)||
                   !read_capsule_nbt(spawn_nbt_file->value,
                       &spawn_nbt,&spawn_nbt_len)||
                   !gm_runtime_minecart_set_spawner_nbt_state(
                       &r,(int)eid,(int)entity,(int)delay,
                       (int)min_delay,(int)max_delay,(int)spawn_count,
                       (int)max_nearby,(int)spawn_range,
                       (int)activate_range,spawn_nbt,spawn_nbt_len,
                       default_entity_nbt)){
                    free(spawn_nbt);
                    fprintf(stderr,"script:%ld: invalid "
                            "set_minecart_spawner_state\n",line_no);
                    goto bad;
                }
                free(spawn_nbt);
            } else if (!strcmp(type,"add_minecart_spawner_potential")) {
                long long eid,entity,weight;
                int default_entity_nbt=0;
                const JlField *entity_nbt_file=
                    field(&pending,"entity_nbt_file");
                const JlField *default_field=
                    field(&pending,"default_entity_nbt");
                uint8_t *entity_nbt=NULL;
                size_t entity_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","eid","entity","weight",
                    "entity_nbt_file","default_entity_nbt"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"weight"),&weight)||
                   eid<0||eid>INT_MAX||entity<=0||entity>255||
                   weight<=0||weight>INT_MAX||!entity_nbt_file||
                   !entity_nbt_file->string||!default_field||
                   !as_rule_bool(default_field,&default_entity_nbt)||
                   !read_capsule_nbt(entity_nbt_file->value,
                       &entity_nbt,&entity_nbt_len)||
                   !gm_runtime_minecart_add_spawner_potential(
                       &r,(int)eid,(int)entity,(int)weight,
                       entity_nbt,entity_nbt_len,default_entity_nbt)){
                    free(entity_nbt);
                    fprintf(stderr,"script:%ld: invalid "
                            "add_minecart_spawner_potential\n",line_no);
                    goto bad;
                }
                free(entity_nbt);
            } else if (!strcmp(type,"spawn_shulker_state_fixture")) {
                long long eid,attach_x,attach_y,attach_z,face,no_ai;
                long long peek_tick,peek_time,attack_time;
                long long has_player_target,watch_time,idle_look_time;
                long long living_sound_time,ticks_existed,hurt_time;
                long long hurt_resistant_time,death_time,entity_seed48;
                double health,last_damage,prev_peek_amount,peek_amount;
                double head_yaw,head_pitch;
                static const char *const keys[]={
                    "tick","type","eid","attach_x","attach_y",
                    "attach_z","face","no_ai","peek_tick","peek_time",
                    "attack_time","has_player_target","watch_time",
                    "idle_look_time","living_sound_time","ticks_existed",
                    "hurt_time","hurt_resistant_time","death_time",
                    "health","last_damage","prev_peek_amount",
                    "peek_amount","head_yaw","head_pitch","entity_seed48"
                };
                if(!keys_only(&pending,keys,26,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"attach_x"),&attach_x)||
                   !as_i64(field(&pending,"attach_y"),&attach_y)||
                   !as_i64(field(&pending,"attach_z"),&attach_z)||
                   !as_i64(field(&pending,"face"),&face)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_i64(field(&pending,"peek_tick"),&peek_tick)||
                   !as_i64(field(&pending,"peek_time"),&peek_time)||
                   !as_i64(field(&pending,"attack_time"),&attack_time)||
                   !as_i64(field(&pending,"has_player_target"),
                           &has_player_target)||
                   !as_i64(field(&pending,"watch_time"),&watch_time)||
                   !as_i64(field(&pending,"idle_look_time"),
                           &idle_look_time)||
                   !as_i64(field(&pending,"living_sound_time"),
                           &living_sound_time)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),
                           &hurt_resistant_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_double(field(&pending,"last_damage"),&last_damage)||
                   !as_double(field(&pending,"prev_peek_amount"),
                              &prev_peek_amount)||
                   !as_double(field(&pending,"peek_amount"),&peek_amount)||
                   !as_double(field(&pending,"head_yaw"),&head_yaw)||
                   !as_double(field(&pending,"head_pitch"),&head_pitch)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   eid<=0||eid>INT_MAX||attach_x<INT_MIN||attach_x>INT_MAX||
                   attach_y<INT_MIN||attach_y>INT_MAX||
                   attach_z<INT_MIN||attach_z>INT_MAX||
                   face<0||face>5||no_ai<0||no_ai>1||
                   peek_tick<0||peek_tick>100||peek_time<0||
                   peek_time>INT_MAX||attack_time<0||attack_time>INT_MAX||
                   has_player_target<0||has_player_target>1||
                   watch_time<0||watch_time>INT_MAX||idle_look_time<0||
                   idle_look_time>INT_MAX||living_sound_time<INT_MIN||
                   living_sound_time>INT_MAX||ticks_existed<0||
                   ticks_existed>INT_MAX||hurt_time<0||hurt_time>10||
                   hurt_resistant_time<0||hurt_resistant_time>20||
                   death_time<0||death_time>19||entity_seed48<0||
                   entity_seed48>((1LL<<48)-1)||!isfinite(health)||
                   !isfinite(last_damage)||!isfinite(prev_peek_amount)||
                   !isfinite(peek_amount)||!isfinite(head_yaw)||
                   !isfinite(head_pitch)||
                   !gm_runtime_spawn_shulker_state_fixture(
                       &r,(int)eid,(int)attach_x,(int)attach_y,
                       (int)attach_z,(int)face,(int)no_ai,(int)peek_tick,
                       (int)peek_time,(int)attack_time,
                       (int)has_player_target,(int)watch_time,
                       (int)idle_look_time,(int)living_sound_time,
                       (int)ticks_existed,(int)hurt_time,
                       (int)hurt_resistant_time,(int)death_time,
                       (float)health,(float)last_damage,
                       (float)prev_peek_amount,(float)peek_amount,
                       (float)head_yaw,(float)head_pitch,
                       (uint64_t)entity_seed48)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_shulker_state_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_shulker_bullet_state_fixture")) {
                long long eid,owner_eid,direction,steps,ticks_existed;
                long long entity_seed48;
                double x,y,z,vx,vy,vz,target_dx,target_dy,target_dz;
                double yaw,pitch;
                static const char *const keys[]={
                    "tick","type","eid","owner_eid","direction","steps",
                    "ticks_existed","x","y","z","vx","vy","vz",
                    "target_dx","target_dy","target_dz","yaw","pitch",
                    "entity_seed48"
                };
                if(!keys_only(&pending,keys,19,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"owner_eid"),&owner_eid)||
                   !as_i64(field(&pending,"direction"),&direction)||
                   !as_i64(field(&pending,"steps"),&steps)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"target_dx"),&target_dx)||
                   !as_double(field(&pending,"target_dy"),&target_dy)||
                   !as_double(field(&pending,"target_dz"),&target_dz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"entity_seed48"),&entity_seed48)||
                   eid<=0||eid>INT_MAX||owner_eid<0||owner_eid>INT_MAX||
                   direction<-1||direction>5||steps<0||steps>INT_MAX||
                   ticks_existed<0||ticks_existed>INT_MAX||
                   entity_seed48<0||entity_seed48>((1LL<<48)-1)||
                   !isfinite(x)||!isfinite(y)||!isfinite(z)||
                   !isfinite(vx)||!isfinite(vy)||!isfinite(vz)||
                   !isfinite(target_dx)||!isfinite(target_dy)||
                   !isfinite(target_dz)||!isfinite(yaw)||!isfinite(pitch)||
                   !gm_runtime_spawn_shulker_bullet_state_fixture(
                       &r,(int)eid,(int)owner_eid,(int)direction,(int)steps,
                       (int)ticks_existed,x,y,z,vx,vy,vz,target_dx,target_dy,
                       target_dz,(float)yaw,(float)pitch,
                       (uint64_t)entity_seed48)){
                    fprintf(stderr,
                            "script:%ld: invalid "
                            "spawn_shulker_bullet_state_fixture\n",line_no);
                    goto bad;
                }
            } else if (!strcmp(type,"set_minecart_slot")) {
                long long eid,slot,item,count,meta;
                const JlField *stack_nbt_file=field(&pending,"stack_nbt_file");
                uint8_t *stack_tag_nbt=NULL;
                size_t stack_tag_nbt_len=0;
                ICStack stack;
                static const char *const keys[]={
                    "tick","type","eid","slot","item","count","meta",
                    "stack_nbt_file"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   eid<=0||eid>INT_MAX||slot<0||slot>=27||
                   item<1||item>4095||count<1||count>64||
                   meta<0||meta>32767||
                   (stack_nbt_file&&(!stack_nbt_file->string||
                       !read_capsule_nbt(stack_nbt_file->value,
                           &stack_tag_nbt,&stack_tag_nbt_len)))){
                    free(stack_tag_nbt);
                    fprintf(stderr,
                            "script:%ld: invalid set_minecart_slot\n",
                            line_no);goto bad;
                }
                stack=ic_mk((int)item,(int)count,(int)meta);
                if(stack_nbt_file){
                    stack.tag_id=gm_runtime_stack_tag_intern(
                        &r,stack_tag_nbt,stack_tag_nbt_len);
                    free(stack_tag_nbt);
                    if(stack.tag_id==0){
                        fprintf(stderr,
                            "script:%ld: invalid set_minecart_slot NBT\n",
                            line_no);goto bad;
                    }
                }
                if(!gm_runtime_minecart_set_slot_stack(
                        &r,(int)eid,(int)slot,stack)){
                    fprintf(stderr,
                            "script:%ld: invalid set_minecart_slot\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_boat_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch;
                long long eid,stationary;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","controlled_stationary"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_i64(field(&pending,"controlled_stationary"),
                           &stationary)||
                   vx!=0.0||vy!=0.0||vz!=0.0||pitch!=0.0||stationary!=1||
                   !gm_runtime_spawn_boat_fixture(
                       &r,(int)eid,x,y,z,(float)yaw)){
                    fprintf(stderr,"script:%ld: invalid spawn_boat_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_village_collection")) {
                long long collection_tick,count;
                static const char *const keys[]={
                    "tick","type","collection_tick","count"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"collection_tick"),
                           &collection_tick)||
                   !as_i64(field(&pending,"count"),&count)||
                   collection_tick<INT_MIN||collection_tick>INT_MAX||
                   count<0||count>GM_RUNTIME_VILLAGE_STATES_MAX||
                   !gm_runtime_village_collection_begin(
                       &r,(int)collection_tick,(int)count)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_village_collection\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_village_state")) {
                long long index,population,radius,golems,stable,state_tick;
                long long mating,cx,cy,cz,acx,acy,acz;
                static const char *const keys[]={
                    "tick","type","index","population","radius",
                    "golems","stable","state_tick","mating_tick",
                    "center_x","center_y","center_z",
                    "helper_x","helper_y","helper_z"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"index"),&index)||
                   !as_i64(field(&pending,"population"),&population)||
                   !as_i64(field(&pending,"radius"),&radius)||
                   !as_i64(field(&pending,"golems"),&golems)||
                   !as_i64(field(&pending,"stable"),&stable)||
                   !as_i64(field(&pending,"state_tick"),&state_tick)||
                   !as_i64(field(&pending,"mating_tick"),&mating)||
                   !as_i64(field(&pending,"center_x"),&cx)||
                   !as_i64(field(&pending,"center_y"),&cy)||
                   !as_i64(field(&pending,"center_z"),&cz)||
                   !as_i64(field(&pending,"helper_x"),&acx)||
                   !as_i64(field(&pending,"helper_y"),&acy)||
                   !as_i64(field(&pending,"helper_z"),&acz)||
                   index<0||index>=GM_RUNTIME_VILLAGE_STATES_MAX||
                   population<0||population>INT_MAX||
                   radius<0||radius>INT_MAX||golems<0||golems>INT_MAX||
                   stable<INT_MIN||stable>INT_MAX||
                   state_tick<INT_MIN||state_tick>INT_MAX||
                   mating<INT_MIN||mating>INT_MAX||
                   cx<INT_MIN||cx>INT_MAX||cy<INT_MIN||cy>INT_MAX||
                   cz<INT_MIN||cz>INT_MAX||acx<INT_MIN||acx>INT_MAX||
                   acy<INT_MIN||acy>INT_MAX||acz<INT_MIN||acz>INT_MAX||
                   !gm_runtime_village_state_restore(
                       &r,(int)index,(int)population,(int)radius,
                       (int)golems,(int)stable,(int)state_tick,(int)mating,
                       (int)cx,(int)cy,(int)cz,
                       (int)acx,(int)acy,(int)acz)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_village_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_village_door")) {
                long long index,x,y,z,inside_dx,inside_dz,timestamp;
                static const char *const keys[]={
                    "tick","type","index","x","y","z",
                    "inside_dx","inside_dz","timestamp"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"index"),&index)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"inside_dx"),&inside_dx)||
                   !as_i64(field(&pending,"inside_dz"),&inside_dz)||
                   !as_i64(field(&pending,"timestamp"),&timestamp)||
                   index<0||index>=GM_RUNTIME_VILLAGE_STATES_MAX||
                   x<INT_MIN||x>INT_MAX||y<INT_MIN||y>INT_MAX||
                   z<INT_MIN||z>INT_MAX||timestamp<INT_MIN||
                   timestamp>INT_MAX||
                   !gm_runtime_village_door_restore(
                       &r,(int)index,(int)x,(int)y,(int)z,
                       (int)inside_dx,(int)inside_dz,(int)timestamp)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_village_door\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_village_reputation")) {
                long long index,most_hi,most_lo,least_hi,least_lo,score;
                static const char *const keys[]={
                    "tick","type","index","most_hi","most_lo",
                    "least_hi","least_lo","score"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"index"),&index)||
                   !as_i64(field(&pending,"most_hi"),&most_hi)||
                   !as_i64(field(&pending,"most_lo"),&most_lo)||
                   !as_i64(field(&pending,"least_hi"),&least_hi)||
                   !as_i64(field(&pending,"least_lo"),&least_lo)||
                   !as_i64(field(&pending,"score"),&score)||
                   index<0||index>=GM_RUNTIME_VILLAGE_STATES_MAX||
                   most_hi<0||(unsigned long long)most_hi>UINT32_MAX||
                   most_lo<0||(unsigned long long)most_lo>UINT32_MAX||
                   least_hi<0||(unsigned long long)least_hi>UINT32_MAX||
                   least_lo<0||(unsigned long long)least_lo>UINT32_MAX||
                   score<INT_MIN||score>INT_MAX||
                   !gm_runtime_village_reputation_restore(
                       &r,(int)index,
                       ((uint64_t)(uint32_t)most_hi<<32)
                           |(uint32_t)most_lo,
                       ((uint64_t)(uint32_t)least_hi<<32)
                           |(uint32_t)least_lo,(int)score)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_village_reputation\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_player_uuid")) {
                long long most_hi,most_lo,least_hi,least_lo;
                static const char *const keys[]={
                    "tick","type","most_hi","most_lo",
                    "least_hi","least_lo"};
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"most_hi"),&most_hi)||
                   !as_i64(field(&pending,"most_lo"),&most_lo)||
                   !as_i64(field(&pending,"least_hi"),&least_hi)||
                   !as_i64(field(&pending,"least_lo"),&least_lo)||
                   most_hi<0||(unsigned long long)most_hi>UINT32_MAX||
                   most_lo<0||(unsigned long long)most_lo>UINT32_MAX||
                   least_hi<0||(unsigned long long)least_hi>UINT32_MAX||
                   least_lo<0||(unsigned long long)least_lo>UINT32_MAX||
                   !gm_runtime_player_uuid_restore(
                       &r,
                       ((uint64_t)(uint32_t)most_hi<<32)
                           |(uint32_t)most_lo,
                       ((uint64_t)(uint32_t)least_hi<<32)
                           |(uint32_t)least_lo)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_player_name")) {
                const char *name;
                static const char *const keys[]={"tick","type","name"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_string(field(&pending,"name"),&name)||
                   !gm_runtime_player_name_restore(&r,name)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_name\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_player_spawn")) {
                long long present,x,y,z,forced;
                static const char *const keys[]={
                    "tick","type","present","x","y","z","forced"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"present"),&present)||
                   !as_i64(field(&pending,"x"),&x)||
                   !as_i64(field(&pending,"y"),&y)||
                   !as_i64(field(&pending,"z"),&z)||
                   !as_i64(field(&pending,"forced"),&forced)||
                   (present!=0&&present!=1)||(forced!=0&&forced!=1)||
                   (!present&&forced)||x<INT_MIN||x>INT_MAX||
                   y<0||y>255||z<INT_MIN||z>INT_MAX){
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_spawn\n",
                            line_no);goto bad;
                }
                r.player_spawn_present=(int)present;
                r.player_spawn_x=(int)x;
                r.player_spawn_y=(int)y;
                r.player_spawn_z=(int)z;
                r.player_spawn_forced=(int)forced;
            } else if (!strcmp(type,"restore_trigger_qrl_score")) {
                long long present,score,locked;
                static const char *const keys[]={
                    "tick","type","present","score","locked"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"present"),&present)||
                   !as_i64(field(&pending,"score"),&score)||
                   !as_i64(field(&pending,"locked"),&locked)||
                   (present!=0&&present!=1)||(locked!=0&&locked!=1)||
                   score<INT_MIN||score>INT_MAX||
                   (!present&&(score!=0||locked))){
                    fprintf(stderr,
                            "script:%ld: invalid restore_trigger_qrl_score\n",
                            line_no);goto bad;
                }
                r.trigger_qrl_present=(int)present;
                r.trigger_qrl_score=(int)score;
                r.trigger_qrl_locked=(int)locked;
            } else if (!strcmp(type,"restore_achievement_open_inventory")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   (value!=0&&value!=1)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_achievement_open_inventory\n",
                            line_no);goto bad;
                }
                r.stat_achievement_open_inventory=(int)value;
                r.stat_achievement_open_inventory_present=1;
            } else if (!strcmp(type,"restore_player_game_mode")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>3){
                    fprintf(stderr,
                            "script:%ld: invalid restore_player_game_mode\n",
                            line_no);goto bad;
                }
                r.tape_game_mode=(int)value;
                r.tape_creative=value==1;
                gm_mobs_set_player_creative(&r.mobs,value==1);
            } else if (!strcmp(type,"restore_default_game_mode")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>3){
                    fprintf(stderr,
                            "script:%ld: invalid restore_default_game_mode\n",
                            line_no);goto bad;
                }
                r.default_game_mode=(int)value;
            } else if (!strcmp(type,"restore_difficulty")) {
                long long value;
                static const char *const keys[]={"tick","type","value"};
                if(!keys_only(&pending,keys,3,err,sizeof err)||
                   !as_i64(field(&pending,"value"),&value)||
                   value<0||value>3){
                    fprintf(stderr,
                            "script:%ld: invalid restore_difficulty\n",
                            line_no);goto bad;
                }
                r.difficulty=(int)value;
            } else if (!strcmp(type,"restore_world_border")) {
                double center_x,center_z,diameter,target_diameter;
                double damage_amount,damage_buffer;
                long long time_until_target,warning_time,warning_distance;
                static const char *const keys[]={
                    "tick","type","center_x","center_z","diameter",
                    "target_diameter","time_until_target","damage_amount",
                    "damage_buffer","warning_time","warning_distance"
                };
                if(!keys_only(&pending,keys,11,err,sizeof err)||
                   !as_double(field(&pending,"center_x"),&center_x)||
                   !as_double(field(&pending,"center_z"),&center_z)||
                   !as_double(field(&pending,"diameter"),&diameter)||
                   !as_double(field(&pending,"target_diameter"),
                              &target_diameter)||
                   !as_i64(field(&pending,"time_until_target"),
                           &time_until_target)||
                   !as_double(field(&pending,"damage_amount"),&damage_amount)||
                   !as_double(field(&pending,"damage_buffer"),&damage_buffer)||
                   !as_i64(field(&pending,"warning_time"),&warning_time)||
                   !as_i64(field(&pending,"warning_distance"),
                           &warning_distance)||
                   !isfinite(center_x)||!isfinite(center_z)||
                   !isfinite(diameter)||!isfinite(target_diameter)||
                   !isfinite(damage_amount)||!isfinite(damage_buffer)||
                   diameter<1.0||target_diameter<1.0||
                   time_until_target<0||damage_amount<0.0||
                   damage_buffer<0.0||warning_time<0||warning_distance<0||
                   warning_time>INT_MAX||warning_distance>INT_MAX){
                    fprintf(stderr,
                            "script:%ld: invalid restore_world_border\n",
                            line_no);goto bad;
                }
                r.border_center_x=center_x;
                r.border_center_z=center_z;
                r.border_diameter=diameter;
                r.border_target_diameter=target_diameter;
                r.border_time_until_target=time_until_target;
                r.border_damage_amount=damage_amount;
                r.border_damage_buffer=damage_buffer;
                r.border_warning_time=(int)warning_time;
                r.border_warning_distance=(int)warning_distance;
            } else if (!strcmp(type,"spawn_mob_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health;
                long long entity,eid,no_ai,hurt_time,death_time,hurt_resistant;
                long long size=1;
                static const char *const keys[]={
                    "tick","type","entity","eid","x","y","z","vx","vy","vz",
                    "yaw","health","no_ai","hurt_time","death_time",
                    "hurt_resistant_time","size"
                };
                if(!keys_only(&pending,keys,17,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&hurt_resistant)||
                   (field(&pending,"size")&&
                    !as_i64(field(&pending,"size"),&size))||
                   !gm_runtime_spawn_sized_mob_fixture(
                       &r,(int)entity,(int)eid,x,y,z,vx,vy,vz,
                       (float)yaw,(float)health,(int)size,(int)no_ai,(int)hurt_time,
                       (int)death_time,(int)hurt_resistant)){
                    fprintf(stderr,"script:%ld: invalid spawn_mob_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_horse_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health,max_health;
                double movement_speed,jump_strength;
                long long entity,eid,no_ai,growing_age,status,temper;
                long long variant,armor,chested,trap,trap_time;
                long long hurt_time,death_time,hurt_resistant_time;
                static const char *const keys[]={
                    "tick","type","entity","eid","x","y","z",
                    "vx","vy","vz","yaw","health","no_ai",
                    "max_health","movement_speed","jump_strength",
                    "growing_age","status","temper","variant","armor",
                    "chested","trap","trap_time","hurt_time",
                    "death_time","hurt_resistant_time"
                };
                if(!keys_only(&pending,keys,27,err,sizeof err)||
                   !as_i64(field(&pending,"entity"),&entity)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_double(field(&pending,"max_health"),&max_health)||
                   !as_double(field(&pending,"movement_speed"),
                              &movement_speed)||
                   !as_double(field(&pending,"jump_strength"),
                              &jump_strength)||
                   !as_i64(field(&pending,"growing_age"),&growing_age)||
                   !as_i64(field(&pending,"status"),&status)||
                   !as_i64(field(&pending,"temper"),&temper)||
                   !as_i64(field(&pending,"variant"),&variant)||
                   !as_i64(field(&pending,"armor"),&armor)||
                   !as_i64(field(&pending,"chested"),&chested)||
                   !as_i64(field(&pending,"trap"),&trap)||
                   !as_i64(field(&pending,"trap_time"),&trap_time)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),
                           &hurt_resistant_time)||
                   !gm_runtime_spawn_horse_fixture(
                       &r,(int)entity,(int)eid,x,y,z,vx,vy,vz,
                       (float)yaw,(float)health,(int)no_ai,max_health,
                       movement_speed,jump_strength,(int)growing_age,
                       (int)status,(int)temper,(int)variant,(int)armor,
                       (int)chested,(int)trap,(int)trap_time,
                       (int)hurt_time,(int)death_time,
                       (int)hurt_resistant_time)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_horse_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_llama_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health,max_health;
                double movement_speed,jump_strength;
                long long eid,no_ai,growing_age,status,temper,variant;
                long long strength,decor,chested,did_spit,leashed;
                long long hurt_time,death_time,hurt_resistant_time;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","health","no_ai","max_health","movement_speed",
                    "jump_strength","growing_age","status","temper",
                    "variant","strength","decor","chested","did_spit",
                    "leashed","hurt_time","death_time",
                    "hurt_resistant_time"
                };
                if(!keys_only(&pending,keys,27,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   !as_double(field(&pending,"max_health"),&max_health)||
                   !as_double(field(&pending,"movement_speed"),
                              &movement_speed)||
                   !as_double(field(&pending,"jump_strength"),
                              &jump_strength)||
                   !as_i64(field(&pending,"growing_age"),&growing_age)||
                   !as_i64(field(&pending,"status"),&status)||
                   !as_i64(field(&pending,"temper"),&temper)||
                   !as_i64(field(&pending,"variant"),&variant)||
                   !as_i64(field(&pending,"strength"),&strength)||
                   !as_i64(field(&pending,"decor"),&decor)||
                   !as_i64(field(&pending,"chested"),&chested)||
                   !as_i64(field(&pending,"did_spit"),&did_spit)||
                   !as_i64(field(&pending,"leashed"),&leashed)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),
                           &hurt_resistant_time)||
                   !gm_runtime_spawn_llama_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,
                       (float)health,(int)no_ai,max_health,movement_speed,
                       jump_strength,(int)growing_age,(int)status,
                       (int)temper,(int)variant,(int)strength,(int)decor,
                       (int)chested,(int)did_spit,(int)leashed,
                       (int)hurt_time,(int)death_time,
                       (int)hurt_resistant_time)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_llama_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_armor_stand_fixture")) {
                double x,y,z,vx,vy,vz,yaw,pitch,health;
                long long eid,on_ground,no_gravity,invisible,status;
                long long disabled_slots,ticks_existed,fire,punch_cooldown;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","pitch","health","on_ground","no_gravity",
                    "invisible","status","disabled_slots","ticks_existed",
                    "fire","punch_cooldown"
                };
                if(!keys_only(&pending,keys,20,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"pitch"),&pitch)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_i64(field(&pending,"no_gravity"),&no_gravity)||
                   !as_i64(field(&pending,"invisible"),&invisible)||
                   !as_i64(field(&pending,"status"),&status)||
                   !as_i64(field(&pending,"disabled_slots"),&disabled_slots)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !as_i64(field(&pending,"punch_cooldown"),&punch_cooldown)||
                   eid<0||eid>INT_MAX||disabled_slots<INT_MIN||
                   disabled_slots>INT_MAX||ticks_existed<0||
                   ticks_existed>INT_MAX||fire<-20||fire>32767||
                   punch_cooldown<0||
                   !gm_runtime_spawn_armor_stand_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)pitch,
                       (float)health,(int)on_ground,(int)no_gravity,
                       (int)invisible,(int)status,(int)disabled_slots,
                       (int)ticks_existed,(int)fire,punch_cooldown)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_armor_stand_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_uuid")) {
                long long eid,most,least;
                static const char *const keys[]={
                    "tick","type","eid","most","least"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_armor_stand_set_uuid(
                       &r,(int)eid,(int64_t)most,(int64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_uuid\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_random_state")) {
                long long eid,seed48,have_gaussian;
                double gaussian;
                static const char *const keys[]={
                    "tick","type","eid","entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"entity_seed48"),&seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   eid<0||eid>INT_MAX||seed48<0||
                   seed48>((1LL<<48)-1)||have_gaussian<0||
                   have_gaussian>1||
                   !gm_runtime_armor_stand_set_random_state(
                       &r,(int)eid,(uint64_t)seed48,
                       (int)have_gaussian,gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_random_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_living_state")) {
                long long eid,air,in_water,hurt,death,resistant;
                double fall_distance,last_damage;
                static const char *const keys[]={
                    "tick","type","eid","air","in_water",
                    "fall_distance","hurt_time","death_time",
                    "hurt_resistant_time","last_damage"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"air"),&air)||
                   !as_i64(field(&pending,"in_water"),&in_water)||
                   !as_double(field(&pending,"fall_distance"),&fall_distance)||
                   !as_i64(field(&pending,"hurt_time"),&hurt)||
                   !as_i64(field(&pending,"death_time"),&death)||
                   !as_i64(field(&pending,"hurt_resistant_time"),&resistant)||
                   !as_double(field(&pending,"last_damage"),&last_damage)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_armor_stand_set_living_state(
                       &r,(int)eid,(int)air,(int)in_water,
                       (float)fall_distance,(int)hurt,(int)death,
                       (int)resistant,(float)last_damage)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_living_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_generic_state")) {
                long long eid,revenge,portal,name_visible,silent,glowing;
                long long invulnerable,update_blocked,fall_flying,vehicle;
                double absorption,max_health,max_health_base;
                static const char *const keys[]={
                    "tick","type","eid","absorption","max_health",
                    "max_health_base",
                    "revenge_timer","portal_cooldown","name_visible",
                    "silent","glowing","invulnerable","update_blocked",
                    "fall_flying","vehicle_eid"
                };
                if(!keys_only(&pending,keys,15,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"absorption"),&absorption)||
                   !as_double(field(&pending,"max_health"),&max_health)||
                   !as_double(field(&pending,"max_health_base"),
                       &max_health_base)||
                   !as_i64(field(&pending,"revenge_timer"),&revenge)||
                   !as_i64(field(&pending,"portal_cooldown"),&portal)||
                   !as_i64(field(&pending,"name_visible"),&name_visible)||
                   !as_i64(field(&pending,"silent"),&silent)||
                   !as_i64(field(&pending,"glowing"),&glowing)||
                   !as_i64(field(&pending,"invulnerable"),&invulnerable)||
                   !as_i64(field(&pending,"update_blocked"),&update_blocked)||
                   !as_i64(field(&pending,"fall_flying"),&fall_flying)||
                   !as_i64(field(&pending,"vehicle_eid"),&vehicle)||
                   eid<0||eid>INT_MAX||revenge<0||revenge>INT_MAX||
                   portal<0||portal>INT_MAX||vehicle<-1||vehicle>INT_MAX||
                   !gm_runtime_armor_stand_set_generic_state(
                       &r,(int)eid,(float)absorption,(float)max_health,
                       (float)max_health_base,
                       (int)revenge,(int)portal,(int)name_visible,
                       (int)silent,(int)glowing,(int)invulnerable,
                       (int)update_blocked,(int)fall_flying,(int)vehicle)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_generic_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_custom_name")) {
                long long eid;const char *name;
                static const char *const keys[]={
                    "tick","type","eid","name"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_string(field(&pending,"name"),&name)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_armor_stand_set_custom_name(
                       &r,(int)eid,name)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_custom_name\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"add_armor_stand_tag")) {
                long long eid;const char *tag;
                static const char *const keys[]={
                    "tick","type","eid","tag"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_string(field(&pending,"tag"),&tag)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_armor_stand_add_tag(&r,(int)eid,tag)){
                    fprintf(stderr,
                            "script:%ld: invalid add_armor_stand_tag\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"add_armor_stand_effect")) {
                long long eid,id,amplifier,duration,ambient,show_particles;
                static const char *const keys[]={
                    "tick","type","eid","id","amplifier","duration",
                    "ambient","show_particles"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !as_i64(field(&pending,"ambient"),&ambient)||
                   !as_i64(field(&pending,"show_particles"),&show_particles)||
                   eid<0||eid>INT_MAX||duration>INT_MAX||
                   !gm_runtime_armor_stand_add_effect(
                       &r,(int)eid,(int)id,(int)amplifier,(int)duration,
                       (int)ambient,(int)show_particles)){
                    fprintf(stderr,
                            "script:%ld: invalid add_armor_stand_effect\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_pose")) {
                long long eid,part;double x,y,z;
                static const char *const keys[]={
                    "tick","type","eid","part","x","y","z"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"part"),&part)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_armor_stand_set_pose(
                       &r,(int)eid,(int)part,(float)x,(float)y,(float)z)){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_pose\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_armor_stand_equipment")) {
                long long eid,slot,item,count,meta,n_ench=0,repair_cost=0;
                const char *custom_name=NULL;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;size_t tag_nbt_len=0;ICStack st;
                static const char *const keys[]={
                    "tick","type","eid","slot","item","count","meta",
                    "n_ench","e0","e1","e2","e3","e4","e5","e6",
                    "e7","repair_cost","custom_name","nbt_file"
                };
                if(!keys_only(&pending,keys,19,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"repair_cost")&&
                    !as_i64(field(&pending,"repair_cost"),&repair_cost))||
                   (field(&pending,"custom_name")&&
                    !as_string(field(&pending,"custom_name"),&custom_name))||
                   eid<0||eid>INT_MAX||slot<0||
                   slot>=GM_ARMOR_STAND_SLOTS||item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767||
                   repair_cost<0||repair_cost>INT_MAX){
                    fprintf(stderr,
                            "script:%ld: invalid set_armor_stand_equipment\n",
                            line_no);goto bad;
                }
                st=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                st.repair_cost=(i32)repair_cost;
                if(nbt_file){
                    if(!nbt_file->string||count==0||
                       !read_capsule_nbt(nbt_file->value,
                                         &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                                "script:%ld: invalid armor stand equipment NBT\n",
                                line_no);goto bad;
                    }
                    st.tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(st.tag_id==0){
                        fprintf(stderr,
                                "script:%ld: invalid armor stand equipment tag\n",
                                line_no);goto bad;
                    }
                }
                if(custom_name&&custom_name[0]){
                    st.custom_name=gm_runtime_item_name_intern(&r,custom_name);
                    if(st.custom_name==0){
                        fprintf(stderr,
                                "script:%ld: invalid armor stand equipment name\n",
                                line_no);goto bad;
                    }
                }
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,
                                "script:%ld: invalid armor stand enchant count\n",
                                line_no);goto bad;
                    }
                    st.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char ek[4];long long packed;
                        snprintf(ek,sizeof ek,"e%d",ei);
                        if(!as_i64(field(&pending,ek),&packed)){
                            fprintf(stderr,
                                    "script:%ld: missing armor stand enchant %s\n",
                                    line_no,ek);goto bad;
                        }
                        st.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        st.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!gm_runtime_armor_stand_set_equipment(
                        &r,(int)eid,(int)slot,st)){
                    fprintf(stderr,
                            "script:%ld: invalid armor stand equipment stack\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_llama_links")) {
                double caravan_speed;
                long long eid,holder_kind,holder_eid,head_eid,tail_eid;
                long long dist_counter;
                static const char *const keys[]={
                    "tick","type","eid","leash_holder_kind",
                    "leash_holder_eid","caravan_head_eid",
                    "caravan_tail_eid","caravan_speed",
                    "caravan_dist_counter"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"leash_holder_kind"),
                           &holder_kind)||
                   !as_i64(field(&pending,"leash_holder_eid"),
                           &holder_eid)||
                   !as_i64(field(&pending,"caravan_head_eid"),&head_eid)||
                   !as_i64(field(&pending,"caravan_tail_eid"),&tail_eid)||
                   !as_double(field(&pending,"caravan_speed"),
                              &caravan_speed)||
                   !as_i64(field(&pending,"caravan_dist_counter"),
                           &dist_counter)||
                   !gm_runtime_restore_llama_links(
                       &r,(int)eid,(int)holder_kind,(int)holder_eid,
                       (int)head_eid,(int)tail_eid,caravan_speed,
                       (int)dist_counter)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_llama_links\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_horse_lifecycle")) {
                long long eid,in_love,forced_age,forced_age_timer;
                long long eating_counter,open_mouth_counter;
                long long jump_rearing_counter,tail_counter,sprint_counter;
                long long gallop_time,horse_jumping,allow_stand_sliding;
                double jump_power,head_lean,prev_head_lean,rearing_amount;
                double prev_rearing_amount,mouth_openness;
                double prev_mouth_openness,prev_limb_amount,limb_amount;
                double limb_swing;
                static const char *const keys[]={
                    "tick","type","eid","in_love","forced_age",
                    "forced_age_timer","eating_counter",
                    "open_mouth_counter","jump_rearing_counter",
                    "tail_counter","sprint_counter","gallop_time",
                    "horse_jumping","allow_stand_sliding","jump_power",
                    "head_lean","prev_head_lean","rearing_amount",
                    "prev_rearing_amount","mouth_openness",
                    "prev_mouth_openness","prev_limb_amount",
                    "limb_amount","limb_swing"
                };
                if(!keys_only(&pending,keys,24,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"in_love"),&in_love)||
                   !as_i64(field(&pending,"forced_age"),&forced_age)||
                   !as_i64(field(&pending,"forced_age_timer"),
                           &forced_age_timer)||
                   !as_i64(field(&pending,"eating_counter"),
                           &eating_counter)||
                   !as_i64(field(&pending,"open_mouth_counter"),
                           &open_mouth_counter)||
                   !as_i64(field(&pending,"jump_rearing_counter"),
                           &jump_rearing_counter)||
                   !as_i64(field(&pending,"tail_counter"),&tail_counter)||
                   !as_i64(field(&pending,"sprint_counter"),
                           &sprint_counter)||
                   !as_i64(field(&pending,"gallop_time"),&gallop_time)||
                   !as_i64(field(&pending,"horse_jumping"),
                           &horse_jumping)||
                   !as_i64(field(&pending,"allow_stand_sliding"),
                           &allow_stand_sliding)||
                   !as_double(field(&pending,"jump_power"),&jump_power)||
                   !as_double(field(&pending,"head_lean"),&head_lean)||
                   !as_double(field(&pending,"prev_head_lean"),
                              &prev_head_lean)||
                   !as_double(field(&pending,"rearing_amount"),
                              &rearing_amount)||
                   !as_double(field(&pending,"prev_rearing_amount"),
                              &prev_rearing_amount)||
                   !as_double(field(&pending,"mouth_openness"),
                              &mouth_openness)||
                   !as_double(field(&pending,"prev_mouth_openness"),
                              &prev_mouth_openness)||
                   !as_double(field(&pending,"prev_limb_amount"),
                              &prev_limb_amount)||
                   !as_double(field(&pending,"limb_amount"),&limb_amount)||
                   !as_double(field(&pending,"limb_swing"),&limb_swing)||
                   !gm_runtime_restore_horse_lifecycle(
                       &r,(int)eid,(int)in_love,(int)forced_age,
                       (int)forced_age_timer,(int)eating_counter,
                       (int)open_mouth_counter,(int)jump_rearing_counter,
                       (int)tail_counter,(int)sprint_counter,
                       (int)gallop_time,(int)horse_jumping,
                       (int)allow_stand_sliding,(float)jump_power,
                       (float)head_lean,(float)prev_head_lean,
                       (float)rearing_amount,(float)prev_rearing_amount,
                       (float)mouth_openness,(float)prev_mouth_openness,
                       (float)prev_limb_amount,(float)limb_amount,
                       (float)limb_swing)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_horse_lifecycle\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_horse_owner")) {
                long long eid,present,most,least;
                static const char *const keys[]={
                    "tick","type","eid","present","most","least"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"present"),&present)||
                   !as_i64(field(&pending,"most"),&most)||
                   !as_i64(field(&pending,"least"),&least)||
                   !gm_runtime_restore_horse_owner(
                       &r,(int)eid,(int)present,
                       (uint64_t)most,(uint64_t)least)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_horse_owner\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_horse_inventory")) {
                long long eid,slot,item,count,meta,n_ench=0,repair_cost=0;
                const char *custom_name=NULL;
                const JlField *nbt_file=field(&pending,"nbt_file");
                uint8_t *tag_nbt=NULL;
                size_t tag_nbt_len=0;
                static const char *const keys[]={
                    "tick","type","eid","slot","item","count","meta",
                    "n_ench","e0","e1","e2","e3","e4","e5","e6",
                    "e7","repair_cost","custom_name","nbt_file"
                };
                ICStack st;
                if(!keys_only(&pending,keys,19,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"slot"),&slot)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   (field(&pending,"repair_cost")&&
                    !as_i64(field(&pending,"repair_cost"),&repair_cost))||
                   (field(&pending,"custom_name")&&
                    !as_string(field(&pending,"custom_name"),&custom_name))||
                   eid<=0||eid>INT_MAX||slot<0||slot>=17||item<0||item>4095||
                   count<0||count>64||meta<0||meta>32767||
                   repair_cost<0||repair_cost>INT_MAX){
                    fprintf(stderr,
                            "script:%ld: invalid restore_horse_inventory\n",
                            line_no);goto bad;
                }
                st=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                st.repair_cost=(i32)repair_cost;
                if(nbt_file){
                    if(!nbt_file->string||count==0||
                       !read_capsule_nbt(nbt_file->value,
                                         &tag_nbt,&tag_nbt_len)){
                        free(tag_nbt);
                        fprintf(stderr,
                                "script:%ld: invalid horse inventory NBT\n",
                                line_no);goto bad;
                    }
                    st.tag_id=gm_runtime_stack_tag_intern(
                        &r,tag_nbt,tag_nbt_len);
                    free(tag_nbt);
                    if(st.tag_id==0){
                        fprintf(stderr,
                                "script:%ld: invalid horse inventory tag\n",
                                line_no);goto bad;
                    }
                }
                if(custom_name&&custom_name[0]){
                    st.custom_name=gm_runtime_item_name_intern(
                        &r,custom_name);
                    if(st.custom_name==0){
                        fprintf(stderr,
                                "script:%ld: invalid horse inventory name\n",
                                line_no);goto bad;
                    }
                }
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,
                                "script:%ld: invalid horse enchant count\n",
                                line_no);goto bad;
                    }
                    st.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char ek[4];long long packed;
                        snprintf(ek,sizeof ek,"e%d",ei);
                        if(!as_i64(field(&pending,ek),&packed)){
                            fprintf(stderr,
                                    "script:%ld: missing horse enchant %s\n",
                                    line_no,ek);goto bad;
                        }
                        st.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        st.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!gm_runtime_set_horse_inventory(
                        &r,(int)eid,(int)slot,st)){
                    fprintf(stderr,
                            "script:%ld: invalid horse inventory stack\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_no_ai_mob_state")) {
                double fall_distance,last_damage,gaussian;
                long long eid,air,fire,on_ground,in_water,ticks_existed;
                long long living_sound_time,seed48,have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","air","fire","on_ground",
                    "fall_distance","in_water","ticks_existed",
                    "living_sound_time","last_damage","entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"air"),&air)||
                   !as_i64(field(&pending,"fire"),&fire)||
                   !as_i64(field(&pending,"on_ground"),&on_ground)||
                   !as_double(field(&pending,"fall_distance"),
                              &fall_distance)||
                   !as_i64(field(&pending,"in_water"),&in_water)||
                   !as_i64(field(&pending,"ticks_existed"),&ticks_existed)||
                   !as_i64(field(&pending,"living_sound_time"),
                           &living_sound_time)||
                   !as_double(field(&pending,"last_damage"),&last_damage)||
                   !as_i64(field(&pending,"entity_seed48"),&seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   eid<0||eid>INT_MAX||air < -20||air > 300||
                   fire < -20||fire > 32767||
                   (on_ground != 0 && on_ground != 1)||
                   (in_water != 0 && in_water != 1)||
                   ticks_existed < 0||ticks_existed > INT_MAX||
                   living_sound_time < -1000000||
                   living_sound_time > 1000000||
                   (have_gaussian != 0 && have_gaussian != 1)||
                   seed48<0||
                   (unsigned long long)seed48>=(UINT64_C(1)<<48)||
                   !gm_runtime_restore_no_ai_mob_state(
                       &r,(int)eid,(int)air,(int)fire,(int)on_ground,
                       (float)fall_distance,(int)in_water,
                       (int)ticks_existed,(int)living_sound_time,
                       (float)last_damage,(uint64_t)seed48,
                       (int)have_gaussian,gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_no_ai_mob_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_mob_effect")) {
                long long eid,id,amplifier,duration,ambient,show_particles;
                static const char *const keys[]={
                    "tick","type","eid","id","amplifier","duration",
                    "ambient","show_particles"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"id"),&id)||
                   !as_i64(field(&pending,"amplifier"),&amplifier)||
                   !as_i64(field(&pending,"duration"),&duration)||
                   !as_i64(field(&pending,"ambient"),&ambient)||
                   !as_i64(field(&pending,"show_particles"),&show_particles)||
                   !gm_runtime_restore_mob_effect(
                       &r,(int)eid,(int)id,(int)amplifier,(int)duration,
                       (int)ambient,(int)show_particles)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_mob_effect\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_mob_health_absorption")) {
                long long eid;
                double health,absorption;
                static const char *const keys[]={
                    "tick","type","eid","health","absorption"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_double(field(&pending,"absorption"),&absorption)||
                   !gm_runtime_restore_mob_health_absorption(
                       &r,(int)eid,(float)health,(float)absorption)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_mob_health_absorption\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_sheep_state")) {
                long long eid,fleece_color,sheared;
                static const char *const keys[]={
                    "tick","type","eid","fleece_color","sheared"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"fleece_color"),&fleece_color)||
                   !as_i64(field(&pending,"sheared"),&sheared)||
                   eid<=0||eid>INT_MAX||fleece_color<0||fleece_color>15||
                   (sheared!=0&&sheared!=1)||
                   !gm_runtime_set_sheep_state(
                       &r,(int)eid,(int)fleece_color,(int)sheared)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_sheep_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_iron_golem_state")) {
                long long eid,player_created,home_timer,attack_timer;
                long long rose_timer;
                static const char *const keys[]={
                    "tick","type","eid","player_created","home_timer",
                    "attack_timer","rose_timer"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"player_created"),&player_created)||
                   !as_i64(field(&pending,"home_timer"),&home_timer)||
                   !as_i64(field(&pending,"attack_timer"),&attack_timer)||
                   !as_i64(field(&pending,"rose_timer"),&rose_timer)||
                   eid<0||eid>INT_MAX||
                   (player_created!=0&&player_created!=1)||
                   home_timer<INT_MIN||home_timer>INT_MAX||
                   attack_timer<0||attack_timer>10||
                   rose_timer<0||rose_timer>400||
                   !gm_runtime_restore_iron_golem_state(
                       &r,(int)eid,(int)player_created,(int)home_timer,
                       (int)attack_timer,(int)rose_timer)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_iron_golem_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_no_ai_mob_box")) {
                double min_x,min_y,min_z,max_x,max_y,max_z;
                long long eid;
                static const char *const keys[]={
                    "tick","type","eid","min_x","min_y","min_z",
                    "max_x","max_y","max_z"
                };
                if(!keys_only(&pending,keys,9,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"min_x"),&min_x)||
                   !as_double(field(&pending,"min_y"),&min_y)||
                   !as_double(field(&pending,"min_z"),&min_z)||
                   !as_double(field(&pending,"max_x"),&max_x)||
                   !as_double(field(&pending,"max_y"),&max_y)||
                   !as_double(field(&pending,"max_z"),&max_z)||
                   eid<0||eid>INT_MAX||
                   !gm_runtime_restore_no_ai_mob_box(
                       &r,(int)eid,min_x,min_y,min_z,max_x,max_y,max_z)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_no_ai_mob_box\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_bat_state")) {
                long long eid,hanging;
                static const char *const keys[]={
                    "tick","type","eid","hanging"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"hanging"),&hanging)||
                   eid<=0||eid>INT_MAX||
                   (hanging!=0&&hanging!=1)||
                   !gm_runtime_restore_bat_state(
                       &r,(int)eid,(int)hanging)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_bat_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_bat_ai_state")) {
                long long eid,hanging,spawn_valid,spawn_x,spawn_y,spawn_z;
                long long body_tick,entity_age=0,persistence_required=0;
                double head_yaw,render_yaw,body_prev_head_yaw;
                const JlField *age_field=field(&pending,"entity_age");
                const JlField *persistence_field=
                    field(&pending,"persistence_required");
                static const char *const keys[]={
                    "tick","type","eid","hanging","spawn_valid",
                    "spawn_x","spawn_y","spawn_z","head_yaw",
                    "render_yaw","body_tick","body_prev_head_yaw",
                    "entity_age","persistence_required"
                };
                if(!keys_only(&pending,keys,14,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"hanging"),&hanging)||
                   !as_i64(field(&pending,"spawn_valid"),&spawn_valid)||
                   !as_i64(field(&pending,"spawn_x"),&spawn_x)||
                   !as_i64(field(&pending,"spawn_y"),&spawn_y)||
                   !as_i64(field(&pending,"spawn_z"),&spawn_z)||
                   !as_double(field(&pending,"head_yaw"),&head_yaw)||
                   !as_double(field(&pending,"render_yaw"),&render_yaw)||
                   !as_i64(field(&pending,"body_tick"),&body_tick)||
                   !as_double(field(&pending,"body_prev_head_yaw"),
                              &body_prev_head_yaw)||
                   (age_field&&!as_i64(age_field,&entity_age))||
                   (persistence_field&&!as_i64(
                       persistence_field,&persistence_required))||
                   eid<=0||eid>INT_MAX||
                   (hanging!=0&&hanging!=1)||
                   (spawn_valid!=0&&spawn_valid!=1)||
                   spawn_x<INT_MIN||spawn_x>INT_MAX||
                   spawn_y<INT_MIN||spawn_y>INT_MAX||
                   spawn_z<INT_MIN||spawn_z>INT_MAX||
                   body_tick<0||body_tick>INT_MAX||
                   entity_age<INT_MIN||entity_age>INT_MAX||
                   (persistence_required!=0&&persistence_required!=1)||
                   !gm_runtime_set_bat_ai_state(
                       &r,(int)eid,(int)hanging,(int)spawn_valid,
                       (int)spawn_x,(int)spawn_y,(int)spawn_z,
                       (float)head_yaw,(float)render_yaw,(int)body_tick,
                       (float)body_prev_head_yaw,(int)entity_age,
                       (int)persistence_required)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_bat_ai_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_snowman_state")) {
                long long eid,pumpkin;
                static const char *const keys[]={
                    "tick","type","eid","pumpkin"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"pumpkin"),&pumpkin)||
                   eid<=0||eid>INT_MAX||
                   (pumpkin!=0&&pumpkin!=1)||
                   !gm_runtime_restore_snowman_state(
                       &r,(int)eid,(int)pumpkin)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_snowman_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_endermite_state")) {
                long long eid,lifetime,player_spawned,persistence_required;
                static const char *const keys[]={
                    "tick","type","eid","lifetime","player_spawned",
                    "persistence_required"
                };
                if(!keys_only(&pending,keys,6,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"lifetime"),&lifetime)||
                   !as_i64(field(&pending,"player_spawned"),
                           &player_spawned)||
                   !as_i64(field(&pending,"persistence_required"),
                           &persistence_required)||
                   eid<=0||eid>INT_MAX||lifetime<0||lifetime>=2400||
                   (player_spawned!=0&&player_spawned!=1)||
                   (persistence_required!=0&&persistence_required!=1)||
                   !gm_runtime_restore_endermite_state(
                       &r,(int)eid,(int)lifetime,(int)player_spawned,
                       (int)persistence_required)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_endermite_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_squid_state")) {
                long long eid;
                double squid_pitch,prev_squid_pitch,squid_yaw,prev_squid_yaw;
                double squid_rotation,prev_squid_rotation,tentacle_angle;
                double last_tentacle_angle,random_motion_speed;
                double rotation_velocity,rotate_speed,random_motion_x;
                double random_motion_y,random_motion_z,render_yaw_offset;
                double head_yaw,body_prev_head_yaw;
                long long body_tick;
                static const char *const keys[]={
                    "tick","type","eid","squid_pitch",
                    "prev_squid_pitch","squid_yaw","prev_squid_yaw",
                    "squid_rotation","prev_squid_rotation",
                    "tentacle_angle","last_tentacle_angle",
                    "random_motion_speed","rotation_velocity",
                    "rotate_speed","random_motion_x","random_motion_y",
                    "random_motion_z","render_yaw_offset","head_yaw",
                    "body_tick","body_prev_head_yaw"
                };
                if(!keys_only(&pending,keys,21,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"squid_pitch"),&squid_pitch)||
                   !as_double(field(&pending,"prev_squid_pitch"),
                              &prev_squid_pitch)||
                   !as_double(field(&pending,"squid_yaw"),&squid_yaw)||
                   !as_double(field(&pending,"prev_squid_yaw"),
                              &prev_squid_yaw)||
                   !as_double(field(&pending,"squid_rotation"),
                              &squid_rotation)||
                   !as_double(field(&pending,"prev_squid_rotation"),
                              &prev_squid_rotation)||
                   !as_double(field(&pending,"tentacle_angle"),
                              &tentacle_angle)||
                   !as_double(field(&pending,"last_tentacle_angle"),
                              &last_tentacle_angle)||
                   !as_double(field(&pending,"random_motion_speed"),
                              &random_motion_speed)||
                   !as_double(field(&pending,"rotation_velocity"),
                              &rotation_velocity)||
                   !as_double(field(&pending,"rotate_speed"),&rotate_speed)||
                   !as_double(field(&pending,"random_motion_x"),
                              &random_motion_x)||
                   !as_double(field(&pending,"random_motion_y"),
                              &random_motion_y)||
                   !as_double(field(&pending,"random_motion_z"),
                              &random_motion_z)||
                   !as_double(field(&pending,"render_yaw_offset"),
                              &render_yaw_offset)||
                   !as_double(field(&pending,"head_yaw"),&head_yaw)||
                   !as_i64(field(&pending,"body_tick"),&body_tick)||
                   !as_double(field(&pending,"body_prev_head_yaw"),
                              &body_prev_head_yaw)||
                   eid<=0||eid>INT_MAX||
                   body_tick<0||body_tick>INT_MAX||
                   !gm_runtime_restore_squid_state(
                       &r,(int)eid,(float)squid_pitch,
                       (float)prev_squid_pitch,(float)squid_yaw,
                       (float)prev_squid_yaw,(float)squid_rotation,
                       (float)prev_squid_rotation,(float)tentacle_angle,
                       (float)last_tentacle_angle,(float)random_motion_speed,
                       (float)rotation_velocity,(float)rotate_speed,
                       (float)random_motion_x,(float)random_motion_y,
                       (float)random_motion_z,(float)render_yaw_offset,
                       (float)head_yaw,(int)body_tick,
                       (float)body_prev_head_yaw)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_squid_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_squid_ai_state")) {
                long long eid,entity_age,persistence_required;
                static const char *const keys[]={
                    "tick","type","eid","entity_age",
                    "persistence_required"
                };
                if(!keys_only(&pending,keys,5,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"entity_age"),&entity_age)||
                   !as_i64(field(&pending,"persistence_required"),
                           &persistence_required)||
                   eid<=0||eid>INT_MAX||
                   entity_age<INT_MIN||entity_age>INT_MAX||
                   (persistence_required!=0&&persistence_required!=1)||
                   !gm_runtime_set_squid_ai_state(
                       &r,(int)eid,(int)entity_age,
                       (int)persistence_required)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_squid_ai_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_chicken_state")) {
                double wing_rotation,dest_pos,old_flap_speed,old_flap;
                double wing_rot_delta;
                long long eid,time_until_next_egg,chicken_jockey;
                static const char *const keys[]={
                    "tick","type","eid","time_until_next_egg",
                    "wing_rotation","dest_pos","old_flap_speed",
                    "old_flap","wing_rot_delta","chicken_jockey"
                };
                if(!keys_only(&pending,keys,10,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"time_until_next_egg"),
                           &time_until_next_egg)||
                   !as_double(field(&pending,"wing_rotation"),
                              &wing_rotation)||
                   !as_double(field(&pending,"dest_pos"),&dest_pos)||
                   !as_double(field(&pending,"old_flap_speed"),
                              &old_flap_speed)||
                   !as_double(field(&pending,"old_flap"),&old_flap)||
                   !as_double(field(&pending,"wing_rot_delta"),
                              &wing_rot_delta)||
                   !as_i64(field(&pending,"chicken_jockey"),
                           &chicken_jockey)||
                   eid<=0||eid>INT_MAX||time_until_next_egg<=0||
                   time_until_next_egg>INT_MAX||
                   (chicken_jockey!=0&&chicken_jockey!=1)||
                   !gm_mobs_set_chicken_state(
                       &r.mobs,(int)eid,(int)time_until_next_egg,
                       (float)wing_rotation,(float)dest_pos,
                       (float)old_flap_speed,(float)old_flap,
                       (float)wing_rot_delta,(int)chicken_jockey)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_chicken_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_slime_state")) {
                double squish_amount,squish_factor,prev_squish_factor;
                long long eid,was_on_ground;
                static const char *const keys[]={
                    "tick","type","eid","squish_amount",
                    "squish_factor","prev_squish_factor",
                    "was_on_ground"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"squish_amount"),
                              &squish_amount)||
                   !as_double(field(&pending,"squish_factor"),
                              &squish_factor)||
                   !as_double(field(&pending,"prev_squish_factor"),
                              &prev_squish_factor)||
                   !as_i64(field(&pending,"was_on_ground"),
                           &was_on_ground)||
                   eid<=0||eid>INT_MAX||
                   (was_on_ground!=0&&was_on_ground!=1)||
                   !gm_mobs_set_slime_state(
                       &r.mobs,(int)eid,(float)squish_amount,
                       (float)squish_factor,(float)prev_squish_factor,
                       (int)was_on_ground)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_slime_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_tameable_state")) {
                double gaussian;
                long long eid,tamed,sitting,owner,variant,growing_age;
                long long living_sound_time,seed48,have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","tamed","sitting",
                    "player_owner","variant","growing_age",
                    "living_sound_time","entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,12,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"tamed"),&tamed)||
                   !as_i64(field(&pending,"sitting"),&sitting)||
                   !as_i64(field(&pending,"player_owner"),&owner)||
                   !as_i64(field(&pending,"variant"),&variant)||
                   !as_i64(field(&pending,"growing_age"),&growing_age)||
                   !as_i64(field(&pending,"living_sound_time"),
                           &living_sound_time)||
                   !as_i64(field(&pending,"entity_seed48"),&seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   eid<=0||eid>INT_MAX||seed48<0||
                   (unsigned long long)seed48>=(UINT64_C(1)<<48)||
                   !gm_mobs_restore_tameable_state(
                       &r.mobs,(int)eid,(int)tamed,(int)sitting,(int)owner,
                       (int)variant,(int)growing_age,(int)living_sound_time,
                       (uint64_t)seed48,(int)have_gaussian,gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_tameable_state\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"spawn_villager_fixture")) {
                double x,y,z,vx,vy,vz,yaw,health,gaussian;
                long long eid,hurt_time,death_time,hurt_resistant;
                long long profession,living_sound_time,seed48,have_gaussian;
                static const char *const keys[]={
                    "tick","type","eid","x","y","z","vx","vy","vz",
                    "yaw","health","hurt_time","death_time",
                    "hurt_resistant_time","profession","living_sound_time",
                    "entity_seed48",
                    "entity_have_gaussian","entity_gaussian"
                };
                if(!keys_only(&pending,keys,19,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_double(field(&pending,"x"),&x)||
                   !as_double(field(&pending,"y"),&y)||
                   !as_double(field(&pending,"z"),&z)||
                   !as_double(field(&pending,"vx"),&vx)||
                   !as_double(field(&pending,"vy"),&vy)||
                   !as_double(field(&pending,"vz"),&vz)||
                   !as_double(field(&pending,"yaw"),&yaw)||
                   !as_double(field(&pending,"health"),&health)||
                   !as_i64(field(&pending,"hurt_time"),&hurt_time)||
                   !as_i64(field(&pending,"death_time"),&death_time)||
                   !as_i64(field(&pending,"hurt_resistant_time"),
                           &hurt_resistant)||
                   !as_i64(field(&pending,"profession"),&profession)||
                   !as_i64(field(&pending,"living_sound_time"),
                           &living_sound_time)||
                   !as_i64(field(&pending,"entity_seed48"),&seed48)||
                   !as_i64(field(&pending,"entity_have_gaussian"),
                           &have_gaussian)||
                   !as_double(field(&pending,"entity_gaussian"),&gaussian)||
                   !gm_runtime_spawn_villager_fixture(
                       &r,(int)eid,x,y,z,vx,vy,vz,(float)yaw,(float)health,
                       (int)hurt_time,(int)death_time,(int)hurt_resistant,
                       (int)profession,(int)living_sound_time,
                       (uint64_t)seed48,(int)have_gaussian,
                       gaussian)){
                    fprintf(stderr,
                            "script:%ld: invalid spawn_villager_fixture\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_villager_trade")) {
                long long eid,career,career_level,wealth,willing,offer_count;
                static const char *const keys[]={
                    "tick","type","eid","career","career_level",
                    "wealth","willing","offer_count"
                };
                if(!keys_only(&pending,keys,8,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"career"),&career)||
                   !as_i64(field(&pending,"career_level"),&career_level)||
                   !as_i64(field(&pending,"wealth"),&wealth)||
                   !as_i64(field(&pending,"willing"),&willing)||
                   !as_i64(field(&pending,"offer_count"),&offer_count)||
                   !gm_runtime_restore_villager_trade(
                       &r,(int)eid,(int)career,(int)career_level,
                       (int)wealth,(int)willing,(int)offer_count)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_villager_trade\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_villager_offer")) {
                long long eid,index,uses,max_uses,rewards_exp;
                static const char *const keys[]={
                    "tick","type","eid","index","uses","max_uses",
                    "rewards_exp"
                };
                if(!keys_only(&pending,keys,7,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"index"),&index)||
                   !as_i64(field(&pending,"uses"),&uses)||
                   !as_i64(field(&pending,"max_uses"),&max_uses)||
                   !as_i64(field(&pending,"rewards_exp"),&rewards_exp)||
                   !gm_runtime_restore_villager_offer(
                       &r,(int)eid,(int)index,(int)uses,(int)max_uses,
                       (int)rewards_exp)){
                    fprintf(stderr,
                            "script:%ld: invalid restore_villager_offer\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"restore_villager_offer_stack")) {
                long long eid,index,part,item,count,meta,n_ench=0;
                static const char *const keys[]={
                    "tick","type","eid","index","part","item","count",
                    "meta","n_ench","e0","e1","e2","e3","e4","e5",
                    "e6","e7"
                };
                ICStack stack;
                if(!keys_only(&pending,keys,17,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"index"),&index)||
                   !as_i64(field(&pending,"part"),&part)||
                   !as_i64(field(&pending,"item"),&item)||
                   !as_i64(field(&pending,"count"),&count)||
                   !as_i64(field(&pending,"meta"),&meta)||
                   item<0||item>4095||count<0||count>64||
                   meta<0||meta>32767){
                    fprintf(stderr,
                            "script:%ld: invalid restore_villager_offer_stack\n",
                            line_no);goto bad;
                }
                stack=count==0?ic_empty():ic_mk((int)item,(int)count,(int)meta);
                if(field(&pending,"n_ench")){
                    if(!as_i64(field(&pending,"n_ench"),&n_ench)||
                       n_ench<0||n_ench>IC_MAX_ENCHANTS){
                        fprintf(stderr,
                                "script:%ld: invalid villager stack n_ench\n",
                                line_no);goto bad;
                    }
                    stack.n_enchants=(int)n_ench;
                    for(int ei=0;ei<(int)n_ench;++ei){
                        char key[4];long long packed;
                        snprintf(key,sizeof key,"e%d",ei);
                        if(!as_i64(field(&pending,key),&packed)){
                            fprintf(stderr,
                                    "script:%ld: missing villager stack %s\n",
                                    line_no,key);goto bad;
                        }
                        stack.enchants[ei].id=(i16)((packed>>16)&0xffff);
                        stack.enchants[ei].level=(i16)(packed&0xffff);
                    }
                }
                if(!gm_runtime_restore_villager_offer_stack(
                       &r,(int)eid,(int)index,(int)part,stack)){
                    fprintf(stderr,
                            "script:%ld: rejected villager offer stack\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_mob_air")) {
                long long eid,air;
                static const char *const keys[]={"tick","type","eid","air"};
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"air"),&air)||
                   !gm_runtime_set_mob_air(&r,(int)eid,(int)air)){
                    fprintf(stderr,"script:%ld: invalid set_mob_air\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"set_mob_no_ai")) {
                long long eid,no_ai;
                static const char *const keys[]={
                    "tick","type","eid","no_ai"
                };
                if(!keys_only(&pending,keys,4,err,sizeof err)||
                   !as_i64(field(&pending,"eid"),&eid)||
                   !as_i64(field(&pending,"no_ai"),&no_ai)||
                   eid<0||eid>INT_MAX||(no_ai!=0&&no_ai!=1)||
                   !gm_runtime_set_mob_no_ai(&r,(int)eid,(int)no_ai)){
                    fprintf(stderr,"script:%ld: invalid set_mob_no_ai\n",
                            line_no);goto bad;
                }
            } else if (!strcmp(type,"craft")) {
                int width, slots[9];
                if (!parse_craft(&pending,&width,slots,err,sizeof err) ||
                    !gm_runtime_craft(&r,width,slots)) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"craft failed"); goto bad;
                }
            } else if (!strcmp(type,"use_block")) {
                long long x,y,z;
                static const char *const keys[]={"tick","type","x","y","z"};
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"x"),&x)||!as_i64(field(&pending,"y"),&y)||
                    !as_i64(field(&pending,"z"),&z)||
                    !gm_runtime_use_block(&r,(int)x,(int)y,(int)z)) {
                    fprintf(stderr,"script:%ld: use_block failed\n",line_no); goto bad;
                }
            } else if (!strcmp(type,"furnace_insert")) {
                static const char *const keys[]={"tick","type","slot","inventory","count"};
                long long slot,inv,count;
                if (!keys_only(&pending,keys,5,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"inventory"),&inv)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_insert(&r,(int)slot,(int)inv,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace insert failed"); goto bad;
                }
            } else if (!strcmp(type,"furnace_extract")) {
                static const char *const keys[]={"tick","type","slot","count"};
                long long slot,count;
                if (!keys_only(&pending,keys,4,err,sizeof err)||
                    !as_i64(field(&pending,"slot"),&slot)||
                    !as_i64(field(&pending,"count"),&count)||
                    gm_runtime_furnace_extract(&r,(int)slot,(int)count)<=0) {
                    fprintf(stderr,"script:%ld: %s\n",line_no,err[0]?err:"furnace extract failed"); goto bad;
                }
            } else { fprintf(stderr,"script:%ld: unknown or forbidden type: %s\n",line_no,type); goto bad; }
            have=0;
        }
        /* Save-fork t=0 probe: apply the complete tick-zero restore stream,
         * then export raw blocks/light below without advancing simulation. */
        if (restore_only) {
            write_state(out, &r);
            break;
        }
        float health_before_tick=r.vitals.health;
        int food_before_tick=r.vitals.foodLevel;
        gm_runtime_tick(&r,action);
        /* The llama spawn packet expands into seven id-48 client particles.
         * Runtime exposes the exact packet arguments; instantiate their
         * constructor-private visuals once, on the packet's own tick. */
        for (int particle_index = 0;
                particle_index < gm_runtime_particle_event_count(&r);
                ++particle_index) {
            GmRuntimeParticleEvent particle_event;
            if (!gm_runtime_particle_event_get(
                    &r, particle_index, &particle_event)
                    || particle_event.kind != GM_PARTICLE_SPIT)
                continue;
            int particle_x = (int)floor(particle_event.x);
            int particle_y = (int)floor(particle_event.y);
            int particle_z = (int)floor(particle_event.z);
            (void)gm_particles_live_spawn_spit(
                &replay_particles,
                particle_event.x, particle_event.y, particle_event.z,
                particle_event.motion_x, particle_event.motion_y,
                particle_event.motion_z,
                gm_world_sky_light(
                    r.world, particle_x, particle_y, particle_z),
                gm_world_block_light(
                    r.world, particle_x, particle_y, particle_z));
        }
        if(clear_hurt_velocity_post)gm_player_clear_inferred_hurt_velocity();
        if (prof_on) {
            long long bg = gm_world_block_gen(r.world), d = bg - prof_last;
            if (d < 0) d = 0;   /* dimension switch swapped in a fresh world */
            prof_last = bg; prof_tot += d;
            if (d > prof_max) { prof_max = d; prof_maxt = tick; }
            if (d) prof_nz++;
            prof_h[d == 0 ? 0 : d <= 8 ? 1 : d <= 64 ? 2 : d <= 512 ? 3 : 4]++;
        }
        if (have_pose_post)
            gm_runtime_set_pose_state(&r,pose_x,pose_y,pose_z,
                (float)pose_yaw,(float)pose_pitch,pose_vx,pose_vy,pose_vz,
                pose_on_ground,(float)pose_fall);
        if (have_look) gm_runtime_set_look(&r,(float)look_yaw,(float)look_pitch);
        if(hold_fall_damage_post &&
           r.vitals.health < health_before_tick-1e-6f)
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        if (have_hold_regen_post &&
            r.vitals.health > health_before_tick + 1e-6f) {
            held_regen += r.vitals.health-health_before_tick;
            gm_runtime_set_vitals(&r,health_before_tick,r.vitals.foodLevel);
        }
        if(have_hold_regen_post&&held_regen>0.0f&&
           r.vitals.foodLevel<food_before_tick)
            gm_runtime_set_vitals(&r,r.vitals.health,food_before_tick);
        if (have_vitals_post) {
            if(r.vitals.health+1e-6f<(float)vitals_health&&held_regen>0.0f){
                float visible=(float)vitals_health-r.vitals.health;
                held_regen=held_regen>visible?held_regen-visible:0.0f;
            }
            gm_runtime_set_vitals(&r,(float)vitals_health,(int)vitals_food);
        }
        if (have_regen_post && r.gamerules.naturalRegeneration) {
            if (r.vitals.health + 1e-6f < (float)regen_health) {
                float visible=(float)regen_health-r.vitals.health;
                if (held_regen + 1e-6f >= visible) {
                    held_regen-=visible;
                    if(held_regen<1e-6f)held_regen=0.0f;
                } else {
                    held_regen=0.0f;
                    pv_add_exhaustion(&r.vitals,(float)regen_exhaustion);
                    r.vitals.foodTimer=0;
                }
            }
            gm_runtime_set_vitals(&r,(float)regen_health,(int)regen_food);
        }
        if (have_food_stats_post) {
            r.vitals.saturation=(float)food_stats_saturation;
            r.vitals.exhaustion=(float)food_stats_exhaustion;
        }
        /* Flywheel probe: dump="tick,x0,x1,y0,y1,z0,z1" dumps id/meta of
         * a world region to stderr at that tick - the way to see magma's LIVE
         * world state mid-replay (fluid CA etc.), which no state-out field has. */
        {
            const char *dbg = cr_cfg()->dump;
            if (dbg[0]) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    for (int y=dy1;y>=dy0;--y){
                        fprintf(stderr,"[dump t%d y=%d]",dt,y);
                        for (int z=dz0;z<=dz1;++z){
                            fprintf(stderr," z%d:",z);
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr," %d/%d",
                                    gm_world_block(r.world,x,y,z),
                                    gm_world_meta(r.world,x,y,z));
                        }
                        fprintf(stderr,"\n");
                    }
                }
            }
        }
        /* Flywheel probe: worlddump="tick,cx0,cz0,ncx,ncz,path[;...]" writes
         * the LIVE world's canonical vanilla states for a chunk range in exactly
         * the trace/world_dump --states "CRWS" layout, so snapshot_patch.py can
         * diff the save against THE GAME'S OWN generation instead of world_dump's.
         * The two do not agree by construction: populate windows seed each other
         * with their neighbours' out-of-bounds spill (world/populate_mc.c
         * build_window), so decoration depends on the order windows were built,
         * and the game builds them around a walking player while world_dump
         * sweeps. Semicolon-separated specs let one run dump several ticks - the
         * dimension is whichever one the replay is in at that tick, and a range
         * wider than the resident pool needs several player-centred rectangles.
         * Non-resident chunks dump as all-zero and are counted on stderr: a
         * silent all-zero tile would read as "the game generated air here" and
         * would patch a whole real chunk away. */
        {
            const char *dbg = cr_cfg()->worlddump;
            for (const char *spec = dbg; spec && *spec; ) {
                int dt,cx0,cz0,ncx,ncz; char path[512];
                const char *next = strchr(spec, ';');
                if (sscanf(spec,"%d,%d,%d,%d,%d,%511[^;]",&dt,&cx0,&cz0,&ncx,&ncz,path)==6 &&
                    (long long)dt==r.tick && ncx>0 && ncz>0) {
                    FILE *wf=fopen(path,"wb");
                    if (!wf) { fprintf(stderr,"worlddump: cannot open %s\n",path); }
                    else {
                        long long zero=0; int32_t hdr[4]={cx0,cz0,ncx,ncz};
                        fwrite("CRWS",1,4,wf); fwrite(&zero,8,1,wf);
                        fwrite(hdr,sizeof(int32_t),4,wf);
                        static unsigned short blk[16*256*16];
                        static int32_t bio[16*16];
                        int missing=0;
                        for (int ix=0;ix<ncx;++ix) for (int iz=0;iz<ncz;++iz) {
                            int cx=cx0+ix, cz=cz0+iz, any=0;
                            for (int lx=0;lx<16;++lx) for (int lz=0;lz<16;++lz) {
                                bio[lx*16+lz]=gm_world_biome(r.world,cx*16+lx,cz*16+lz);
                                for (int y=0;y<256;++y) {
                                    int id=gm_world_block(r.world,cx*16+lx,y,cz*16+lz);
                                    int mt=gm_world_meta(r.world,cx*16+lx,y,cz*16+lz);
                                    blk[lx*4096+lz*256+y]=(unsigned short)((id<<4)|(mt&15));
                                    if (id) any=1;
                                }
                            }
                            if (!any) ++missing;
                            fwrite(blk,sizeof(unsigned short),16*256*16,wf);
                            fwrite(bio,sizeof(int32_t),16*16,wf);
                        }
                        fclose(wf);
                        fprintf(stderr,"[worlddump t%d] %d chunks (%d,%d)+%dx%d -> %s "
                                "(%d empty/non-resident)\n",
                                dt,ncx*ncz,cx0,cz0,ncx,ncz,path,missing);
                    }
                }
                spec = next ? next + 1 : NULL;
            }
        }
        /* Same, for light: dump_light="tick,x0,x1,y0,y1,z0,z1" dumps
         * "wx wy wz sky blk" lines (matches the qrl sample_light CSV columns)
         * so live-game light can be diffed cell-for-cell against magma's. */
        {
            const char *dbg = cr_cfg()->dump_light;
            if (dbg[0]) {
                int dt,dx0,dx1,dy0,dy1,dz0,dz1;
                if (sscanf(dbg,"%d,%d,%d,%d,%d,%d,%d",&dt,&dx0,&dx1,&dy0,&dy1,&dz0,&dz1)==7 &&
                    (long long)dt==r.tick) {
                    fprintf(stderr,"[dumplight t%d] wx wy wz sky blk\n",dt);
                    for (int y=dy0;y<=dy1;++y)
                        for (int z=dz0;z<=dz1;++z)
                            for (int x=dx0;x<=dx1;++x)
                                fprintf(stderr,"%d %d %d %d %d\n",x,y,z,
                                    gm_world_sky_light(r.world,x,y,z),
                                    gm_world_block_light(r.world,x,y,z));
                }
            }
        }
        write_state(out,&r);
        gm_particles_live_tick(&replay_particles,r.window,r.ox,r.oz);
        if(window_frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            GmPlayerView view;
            gm_runtime_view(&r,&view);
            gm_runtime_apply_tape_view(&r,&view);
            gm_window_compose_advance(window_frames,&view,&action,1);
            if(render){
                GmWindowComposeFrame wf={
                    .view=&view,.camera_view=&view,.partial_ticks=1.0f,
                    .interactive=1,.screen_open=0,
                    .mouse_x=cfg->width/2,.mouse_y=cfg->height/2,.stamp=NULL
                };
                if(!gm_window_compose_draw(window_frames,&wf,NULL,err,sizeof err)||
                   !gm_window_compose_emit_frame(window_frames,tick,err,sizeof err)){
                    fprintf(stderr,"frames-out: %s\n",err);goto bad;
                }
            }
        }else if(frames){
            int render = tick >= cfg->frame_offset &&
                         (tick - cfg->frame_offset) % cfg->frame_every == 0;
            if(!gm_frame_capture_write(frames,&r,&action,render,err,sizeof err)){
                fprintf(stderr,"frames-out: %s\n",err);goto bad;
            }
        }
    }
    if (have || (in && fgets(line,sizeof line,in))) { fprintf(stderr,"script: event lies beyond --ticks\n"); goto bad; }
    if (prof_on && r.tick > 0) {
        fprintf(stderr, "[state_prof] edits: total %lld over %lld ticks (mean %.2f/tick), "
                "max %lld @t%lld, nonzero ticks %lld (%.1f%%), "
                "hist 0|1-8|9-64|65-512|513+ = %lld|%lld|%lld|%lld|%lld\n",
                prof_tot, r.tick, (double)prof_tot / (double)r.tick,
                prof_max, prof_maxt, prof_nz, 100.0 * (double)prof_nz / (double)r.tick,
                prof_h[0], prof_h[1], prof_h[2], prof_h[3], prof_h[4]);
        prof_scan(&r);
    }
    if (!write_blocks_out(&r)) goto bad;
    if (!write_block_light_out(&r)) goto bad;
    if (!write_sky_light_out(&r)) goto bad;
    if (!write_biomes_out(&r)) goto bad;
    if (!write_heights_out(&r)) goto bad;
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 0;
bad:
    gm_frame_capture_close(frames);gm_window_compose_close(window_frames);gm_runtime_destroy(&r); if(in)fclose(in); if(out!=stdout)fclose(out); return 2;
}
