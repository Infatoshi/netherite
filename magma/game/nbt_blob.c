#include "game/nbt_blob.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GM_NBT_DEPTH_MAX 64
#define GM_NBT_NODES_MAX 65536u

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t at;
    unsigned nodes;
} GmNbtCursor;

static int nbt_take(GmNbtCursor *c, size_t len, const uint8_t **out) {
    if (!c || len > c->len - c->at) return 0;
    if (out) *out = c->data + c->at;
    c->at += len;
    return 1;
}

static int nbt_u8(GmNbtCursor *c, unsigned *out) {
    const uint8_t *raw;
    if (!nbt_take(c, 1, &raw)) return 0;
    if (out) *out = raw[0];
    return 1;
}

static int nbt_u16(GmNbtCursor *c, unsigned *out) {
    const uint8_t *raw;
    if (!nbt_take(c, 2, &raw)) return 0;
    if (out) *out = ((unsigned)raw[0] << 8) | (unsigned)raw[1];
    return 1;
}

static int nbt_nonnegative_i32(GmNbtCursor *c, unsigned *out) {
    const uint8_t *raw;
    unsigned value;
    if (!nbt_take(c, 4, &raw)) return 0;
    value = ((unsigned)raw[0] << 24) | ((unsigned)raw[1] << 16)
        | ((unsigned)raw[2] << 8) | (unsigned)raw[3];
    if (value > INT_MAX) return 0;
    if (out) *out = value;
    return 1;
}

/* Match DataInputStream.readUTF's one-, two-, and three-byte code-unit wire
 * grammar. Java modified UTF-8 encodes supplementary characters as two
 * three-byte UTF-16 surrogate code units and never uses four-byte UTF-8. */
static int nbt_modified_utf8(const uint8_t *raw, size_t len) {
    size_t at = 0;
    while (at < len) {
        unsigned first = raw[at];
        if ((first >> 4) <= 7) {
            at++;
        } else if ((first >> 4) == 12 || (first >> 4) == 13) {
            if (at + 1 >= len || (raw[at + 1] & 0xc0u) != 0x80u)
                return 0;
            at += 2;
        } else if ((first >> 4) == 14) {
            if (at + 2 >= len || (raw[at + 1] & 0xc0u) != 0x80u
                    || (raw[at + 2] & 0xc0u) != 0x80u)
                return 0;
            at += 3;
        } else {
            return 0;
        }
    }
    return 1;
}

static int nbt_utf(GmNbtCursor *c, size_t *length_out) {
    const uint8_t *raw;
    unsigned len;
    if (!nbt_u16(c, &len) || !nbt_take(c, (size_t)len, &raw)
            || !nbt_modified_utf8(raw, (size_t)len))
        return 0;
    if (length_out) *length_out = (size_t)len;
    return 1;
}

static int nbt_payload(GmNbtCursor *c, unsigned type, int depth) {
    unsigned count;
    unsigned child;
    size_t width;
    if (!c || type < 1 || type > 12 || depth > GM_NBT_DEPTH_MAX
            || ++c->nodes > GM_NBT_NODES_MAX)
        return 0;
    switch (type) {
        case 1: return nbt_take(c, 1, NULL);
        case 2: return nbt_take(c, 2, NULL);
        case 3: case 5: return nbt_take(c, 4, NULL);
        case 4: case 6: return nbt_take(c, 8, NULL);
        case 7: case 11: case 12:
            if (!nbt_nonnegative_i32(c, &count)) return 0;
            width = type == 7 ? 1u : type == 11 ? 4u : 8u;
            if ((size_t)count > (c->len - c->at) / width) return 0;
            return nbt_take(c, (size_t)count * width, NULL);
        case 8:
            return nbt_utf(c, NULL);
        case 9:
            if (!nbt_u8(c, &child) || child > 12
                    || !nbt_nonnegative_i32(c, &count)
                    || (child == 0 && count != 0)
                    || count > GM_NBT_NODES_MAX - c->nodes)
                return 0;
            for (unsigned i = 0; i < count; ++i)
                if (!nbt_payload(c, child, depth + 1)) return 0;
            return 1;
        case 10:
            for (;;) {
                if (!nbt_u8(c, &child)) return 0;
                if (child == 0) return 1;
                if (child > 12 || !nbt_utf(c, NULL)
                        || !nbt_payload(c, child, depth + 1))
                    return 0;
            }
        default:
            return 0;
    }
}

void gm_nbt_blob_clear(GmNbtBlob *blob) {
    if (!blob) return;
    free(blob->data);
    blob->data = NULL;
    blob->len = 0;
}

int gm_nbt_blob_validate_root_compound(const void *data, size_t len) {
    GmNbtCursor cursor;
    unsigned type;
    size_t root_name_len;
    if (!data || len < 4 || len > GM_NBT_BLOB_MAX) return 0;
    cursor = (GmNbtCursor){
        .data = (const uint8_t *)data,
        .len = len,
    };
    if (!nbt_u8(&cursor, &type) || type != 10
            || !nbt_utf(&cursor, &root_name_len) || root_name_len != 0
            || !nbt_payload(&cursor, type, 0))
        return 0;
    return cursor.at == cursor.len;
}

int gm_nbt_blob_set(GmNbtBlob *blob, const void *data, size_t len) {
    uint8_t *copy;
    if (!blob || !gm_nbt_blob_validate_root_compound(data, len)) return 0;
    copy = (uint8_t *)malloc(len);
    if (!copy) return 0;
    memcpy(copy, data, len);
    gm_nbt_blob_clear(blob);
    blob->data = copy;
    blob->len = len;
    return 1;
}

int gm_nbt_blob_copy(GmNbtBlob *dst, const GmNbtBlob *src) {
    if (!dst || !src || !src->data || src->len == 0) return 0;
    return gm_nbt_blob_set(dst, src->data, src->len);
}

int gm_nbt_blob_wrap_named_compound(
        GmNbtBlob *out, const char *name, const GmNbtBlob *child) {
    size_t name_len;
    size_t len;
    uint8_t *data;
    size_t at = 0;
    if (!out || !name || !child || !child->data
            || !gm_nbt_blob_validate_root_compound(
                child->data, child->len))
        return 0;
    name_len = strlen(name);
    if (name_len > 0xffffu
            || child->len > GM_NBT_BLOB_MAX - name_len - 4u)
        return 0;
    for (size_t i = 0; i < name_len; ++i)
        if ((unsigned char)name[i] < 0x20u
                || (unsigned char)name[i] > 0x7eu)
            return 0;
    /* Root header + named compound header + the child's compound payload
     * (including its TAG_End) + the outer compound's TAG_End. */
    len = child->len + name_len + 4u;
    data = (uint8_t *)malloc(len);
    if (!data) return 0;
    data[at++] = 10;
    data[at++] = 0;
    data[at++] = 0;
    data[at++] = 10;
    data[at++] = (uint8_t)(name_len >> 8);
    data[at++] = (uint8_t)name_len;
    memcpy(data + at, name, name_len);
    at += name_len;
    memcpy(data + at, child->data + 3, child->len - 3);
    at += child->len - 3;
    data[at++] = 0;
    if (at != len || !gm_nbt_blob_validate_root_compound(data, len)) {
        free(data);
        return 0;
    }
    gm_nbt_blob_clear(out);
    out->data = data;
    out->len = len;
    return 1;
}
