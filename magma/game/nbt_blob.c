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

static int nbt_utf_matches(GmNbtCursor *c, const char *wanted, int *match) {
    const uint8_t *raw;
    unsigned len;
    size_t wanted_len;
    if (!c || !wanted || !match) return 0;
    wanted_len = strlen(wanted);
    if (!nbt_u16(c, &len) || !nbt_take(c, (size_t)len, &raw)
            || !nbt_modified_utf8(raw, (size_t)len))
        return 0;
    *match = wanted_len == (size_t)len
        && memcmp(raw, wanted, (size_t)len) == 0;
    return 1;
}

static int nbt_i32(GmNbtCursor *c, int32_t *out) {
    const uint8_t *raw;
    uint32_t value;
    if (!nbt_take(c, 4, &raw)) return 0;
    value = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
        | ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];
    if (out) memcpy(out, &value, sizeof value);
    return 1;
}

static int nbt_number(GmNbtCursor *c, unsigned type, double *out) {
    const uint8_t *raw;
    uint64_t bits = 0;
    if (!c || !out || type < 1 || type > 6) return 0;
    if (type == 1) {
        if (!nbt_take(c, 1, &raw)) return 0;
        *out = (double)(int8_t)raw[0];
        return 1;
    }
    if (type == 2) {
        uint16_t value;
        if (!nbt_take(c, 2, &raw)) return 0;
        value = ((uint16_t)raw[0] << 8) | (uint16_t)raw[1];
        *out = (double)(int16_t)value;
        return 1;
    }
    if (type == 3 || type == 5) {
        uint32_t value;
        if (!nbt_take(c, 4, &raw)) return 0;
        value = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
            | ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];
        if (type == 3) {
            int32_t signed_value;
            memcpy(&signed_value, &value, sizeof signed_value);
            *out = (double)signed_value;
        } else {
            float value_float;
            memcpy(&value_float, &value, sizeof value_float);
            *out = (double)value_float;
        }
        return 1;
    }
    if (!nbt_take(c, 8, &raw)) return 0;
    for (int i = 0; i < 8; ++i) bits = (bits << 8) | raw[i];
    if (type == 4) {
        int64_t signed_value;
        memcpy(&signed_value, &bits, sizeof signed_value);
        *out = (double)signed_value;
    } else {
        double value_double;
        memcpy(&value_double, &bits, sizeof value_double);
        *out = value_double;
    }
    return 1;
}

static int nbt_utf_copy(
        GmNbtCursor *c, char *out, size_t capacity, size_t *length_out) {
    const uint8_t *raw;
    unsigned len;
    if (!c || !out || capacity == 0 || !nbt_u16(c, &len)
            || (size_t)len >= capacity
            || !nbt_take(c, (size_t)len, &raw)
            || !nbt_modified_utf8(raw, (size_t)len))
        return 0;
    memcpy(out, raw, (size_t)len);
    out[len] = '\0';
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

int gm_nbt_blob_find_compound_int(
        const GmNbtBlob *blob, const char *compound, const char *key,
        int32_t *value_out) {
    GmNbtCursor cursor;
    unsigned type;
    size_t root_name_len;
    int found = 0;
    int32_t value = 0;
    if (!blob || !compound || !key || !value_out || !blob->data
            || !gm_nbt_blob_validate_root_compound(blob->data, blob->len))
        return 0;
    cursor = (GmNbtCursor){.data = blob->data, .len = blob->len};
    if (!nbt_u8(&cursor, &type) || type != 10
            || !nbt_utf(&cursor, &root_name_len) || root_name_len != 0)
        return 0;
    for (;;) {
        int compound_match;
        if (!nbt_u8(&cursor, &type)) return 0;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, compound, &compound_match)) return 0;
        if (type != 10 || !compound_match) {
            if (!nbt_payload(&cursor, type, 1)) return 0;
            continue;
        }
        /* NBTTagCompound's map overwrites duplicate names. Mirror that by
         * letting the last matching compound and last matching int win. */
        found = 0;
        for (;;) {
            int key_match;
            if (!nbt_u8(&cursor, &type)) return 0;
            if (type == 0) break;
            if (!nbt_utf_matches(&cursor, key, &key_match)) return 0;
            if (type == 3 && key_match) {
                if (!nbt_i32(&cursor, &value)) return 0;
                found = 1;
            } else if (!nbt_payload(&cursor, type, 2)) {
                return 0;
            }
        }
    }
    if (!found || cursor.at != cursor.len) return 0;
    *value_out = value;
    return 1;
}

static int nbt_root_payload_cursor(
        const GmNbtBlob *blob, GmNbtCursor *cursor) {
    unsigned type;
    size_t root_name_len;
    if (!blob || !cursor || !blob->data
            || !gm_nbt_blob_validate_root_compound(blob->data, blob->len))
        return 0;
    *cursor = (GmNbtCursor){.data = blob->data, .len = blob->len};
    return nbt_u8(cursor, &type) && type == 10
        && nbt_utf(cursor, &root_name_len) && root_name_len == 0;
}

static int nbt_compound_payload_blob(
        GmNbtBlob *out, const uint8_t *payload, size_t length) {
    uint8_t *root;
    int ok;
    if (!out || !payload || length == 0
            || length > GM_NBT_BLOB_MAX - 3u)
        return 0;
    root = (uint8_t *)malloc(length + 3u);
    if (!root) return 0;
    root[0] = 10; root[1] = 0; root[2] = 0;
    memcpy(root + 3, payload, length);
    ok = gm_nbt_blob_set(out, root, length + 3u);
    free(root);
    return ok;
}

int gm_nbt_blob_find_number(
        const GmNbtBlob *blob, const char *key, double *value_out,
        int *type_out) {
    GmNbtCursor cursor;
    unsigned type;
    double value = 0.0;
    int found = 0, found_type = 0;
    if (!key || !value_out || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) return 0;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) return 0;
        if (match && type >= 1 && type <= 6) {
            if (!nbt_number(&cursor, type, &value)) return 0;
            found = 1;
            found_type = (int)type;
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) return 0;
        }
    }
    if (!found || cursor.at != cursor.len) return 0;
    *value_out = value;
    if (type_out) *type_out = found_type;
    return 1;
}

int gm_nbt_blob_find_string(
        const GmNbtBlob *blob, const char *key, char *value_out,
        size_t capacity) {
    GmNbtCursor cursor;
    unsigned type;
    char *candidate;
    int found = 0;
    if (!key || !value_out || capacity == 0
            || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    candidate = (char *)malloc(capacity);
    if (!candidate) return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) { free(candidate); return 0; }
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) {
            free(candidate); return 0;
        }
        if (match && type == 8) {
            if (!nbt_utf_copy(&cursor, candidate, capacity, NULL)) {
                free(candidate); return 0;
            }
            found = 1;
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) {
                free(candidate); return 0;
            }
        }
    }
    if (!found || cursor.at != cursor.len) { free(candidate); return 0; }
    memcpy(value_out, candidate, strlen(candidate) + 1u);
    free(candidate);
    return 1;
}

int gm_nbt_blob_find_numeric_list(
        const GmNbtBlob *blob, const char *key, double *values,
        size_t capacity, size_t *count_out, int *element_type_out) {
    GmNbtCursor cursor;
    unsigned type;
    double *candidate;
    size_t found_count = 0;
    int found = 0, found_type = 0;
    if (!key || !values || capacity == 0 || !count_out
            || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    candidate = (double *)malloc(capacity * sizeof *candidate);
    if (!candidate) return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) { free(candidate); return 0; }
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) {
            free(candidate); return 0;
        }
        if (match && type == 9) {
            unsigned child, count;
            if (!nbt_u8(&cursor, &child)
                    || !nbt_nonnegative_i32(&cursor, &count)) {
                free(candidate); return 0;
            }
            if (child >= 1 && child <= 6 && count <= capacity) {
                for (unsigned i = 0; i < count; ++i)
                    if (!nbt_number(&cursor, child, &candidate[i])) {
                        free(candidate); return 0;
                    }
                found = 1;
                found_count = count;
                found_type = (int)child;
            } else {
                found = 0;
                if (child > 12 || (child == 0 && count != 0)) {
                    free(candidate); return 0;
                }
                for (unsigned i = 0; i < count; ++i)
                    if (!nbt_payload(&cursor, child, 2)) {
                        free(candidate); return 0;
                    }
            }
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) {
                free(candidate); return 0;
            }
        }
    }
    if (!found || cursor.at != cursor.len) { free(candidate); return 0; }
    memcpy(values, candidate, found_count * sizeof *values);
    *count_out = found_count;
    if (element_type_out) *element_type_out = found_type;
    free(candidate);
    return 1;
}

int gm_nbt_blob_find_list_info(
        const GmNbtBlob *blob, const char *key, size_t *count_out,
        int *element_type_out) {
    GmNbtCursor cursor;
    unsigned type;
    size_t found_count = 0;
    int found = 0, found_type = 0;
    if (!key || !count_out || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) return 0;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) return 0;
        if (match && type == 9) {
            unsigned child, count;
            if (!nbt_u8(&cursor, &child)
                    || !nbt_nonnegative_i32(&cursor, &count)
                    || child > 12 || (child == 0 && count != 0))
                return 0;
            found = 1;
            found_count = count;
            found_type = (int)child;
            for (unsigned i = 0; i < count; ++i)
                if (!nbt_payload(&cursor, child, 2)) return 0;
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) return 0;
        }
    }
    if (!found || cursor.at != cursor.len) return 0;
    *count_out = found_count;
    if (element_type_out) *element_type_out = found_type;
    return 1;
}

int gm_nbt_blob_find_string_list_element(
        const GmNbtBlob *blob, const char *key, size_t index,
        char *value_out, size_t capacity) {
    GmNbtCursor cursor;
    unsigned type;
    char *candidate;
    int found = 0;
    if (!key || !value_out || capacity == 0
            || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    candidate = (char *)malloc(capacity);
    if (!candidate) return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) goto invalid;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) goto invalid;
        if (match && type == 9) {
            unsigned child, count;
            found = 0;
            if (!nbt_u8(&cursor, &child)
                    || !nbt_nonnegative_i32(&cursor, &count)
                    || child > 12 || (child == 0 && count != 0))
                goto invalid;
            for (unsigned element = 0; element < count; ++element) {
                if ((size_t)element == index && child == 8) {
                    if (!nbt_utf_copy(
                            &cursor, candidate, capacity, NULL))
                        goto invalid;
                    found = 1;
                } else if (!nbt_payload(&cursor, child, 2)) {
                    goto invalid;
                }
            }
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) goto invalid;
        }
    }
    if (!found || cursor.at != cursor.len) goto invalid;
    memcpy(value_out, candidate, strlen(candidate) + 1u);
    free(candidate);
    return 1;
invalid:
    free(candidate);
    return 0;
}

int gm_nbt_blob_extract_compound(
        const GmNbtBlob *blob, const char *key, GmNbtBlob *out) {
    GmNbtCursor cursor;
    GmNbtBlob candidate = {0};
    unsigned type;
    int found = 0;
    if (!key || !out || !nbt_root_payload_cursor(blob, &cursor)) return 0;
    for (;;) {
        int match;
        size_t start;
        if (!nbt_u8(&cursor, &type)) goto invalid;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) goto invalid;
        start = cursor.at;
        if (!nbt_payload(&cursor, type, 1)) goto invalid;
        if (match) {
            gm_nbt_blob_clear(&candidate);
            found = type == 10 && nbt_compound_payload_blob(
                &candidate, cursor.data + start, cursor.at - start);
            if (type == 10 && !found) goto invalid;
        }
    }
    if (!found || cursor.at != cursor.len) goto invalid;
    gm_nbt_blob_clear(out);
    *out = candidate;
    return 1;
invalid:
    gm_nbt_blob_clear(&candidate);
    return 0;
}

int gm_nbt_blob_extract_compound_list_element(
        const GmNbtBlob *blob, const char *key, size_t index,
        GmNbtBlob *out) {
    GmNbtCursor cursor;
    GmNbtBlob candidate = {0};
    unsigned type;
    int found = 0;
    if (!key || !out || !nbt_root_payload_cursor(blob, &cursor)) return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) goto invalid;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) goto invalid;
        if (match && type == 9) {
            unsigned child, count;
            gm_nbt_blob_clear(&candidate);
            found = 0;
            if (!nbt_u8(&cursor, &child)
                    || !nbt_nonnegative_i32(&cursor, &count)
                    || child > 12 || (child == 0 && count != 0))
                goto invalid;
            for (unsigned i = 0; i < count; ++i) {
                size_t start = cursor.at;
                if (!nbt_payload(&cursor, child, 2)) goto invalid;
                if ((size_t)i == index && child == 10) {
                    if (!nbt_compound_payload_blob(
                            &candidate, cursor.data + start,
                            cursor.at - start))
                        goto invalid;
                    found = 1;
                }
            }
        } else {
            if (match) { gm_nbt_blob_clear(&candidate); found = 0; }
            if (!nbt_payload(&cursor, type, 1)) goto invalid;
        }
    }
    if (!found || cursor.at != cursor.len) goto invalid;
    gm_nbt_blob_clear(out);
    *out = candidate;
    return 1;
invalid:
    gm_nbt_blob_clear(&candidate);
    return 0;
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

int gm_nbt_blob_make_empty(GmNbtBlob *out) {
    static const uint8_t empty[] = {10, 0, 0, 0};
    return out && gm_nbt_blob_set(out, empty, sizeof empty);
}

static int nbt_ascii_key(const char *key, size_t *length_out) {
    size_t length;
    if (!key || !(length = strlen(key)) || length > 0xffffu) return 0;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)key[i] < 0x20u
                || (unsigned char)key[i] > 0x7eu)
            return 0;
    if (length_out) *length_out = length;
    return 1;
}

static int nbt_blob_set_raw(
        GmNbtBlob *blob, const char *key, unsigned new_type,
        const uint8_t *payload, size_t payload_len) {
    GmNbtCursor cursor;
    size_t key_len, kept = 0, out_len, at = 3;
    uint8_t *out;
    unsigned type;
    if (!blob || new_type < 1 || new_type > 12 || !payload
            || !nbt_ascii_key(key, &key_len)
            || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    for (;;) {
        size_t entry_start = cursor.at, entry_end;
        int match;
        if (!nbt_u8(&cursor, &type)) return 0;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)
                || !nbt_payload(&cursor, type, 1))
            return 0;
        entry_end = cursor.at;
        if (!match) kept += entry_end - entry_start;
    }
    if (cursor.at != cursor.len || kept > GM_NBT_BLOB_MAX
            || payload_len > GM_NBT_BLOB_MAX - kept - key_len - 7u)
        return 0;
    out_len = 3u + kept + 1u + 2u + key_len + payload_len + 1u;
    out = (uint8_t *)malloc(out_len);
    if (!out) return 0;
    memcpy(out, blob->data, 3);
    cursor.at = 3; cursor.nodes = 0;
    for (;;) {
        size_t entry_start = cursor.at, entry_end;
        int match;
        if (!nbt_u8(&cursor, &type)) { free(out); return 0; }
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)
                || !nbt_payload(&cursor, type, 1)) {
            free(out); return 0;
        }
        entry_end = cursor.at;
        if (!match) {
            memcpy(out + at, blob->data + entry_start,
                   entry_end - entry_start);
            at += entry_end - entry_start;
        }
    }
    out[at++] = (uint8_t)new_type;
    out[at++] = (uint8_t)(key_len >> 8);
    out[at++] = (uint8_t)key_len;
    memcpy(out + at, key, key_len); at += key_len;
    memcpy(out + at, payload, payload_len); at += payload_len;
    out[at++] = 0;
    if (at != out_len || !gm_nbt_blob_validate_root_compound(out, out_len)) {
        free(out); return 0;
    }
    gm_nbt_blob_clear(blob);
    blob->data = out; blob->len = out_len;
    return 1;
}

int gm_nbt_blob_set_byte(GmNbtBlob *blob, const char *key, int8_t value) {
    uint8_t raw = (uint8_t)value;
    return nbt_blob_set_raw(blob, key, 1, &raw, 1);
}

int gm_nbt_blob_set_int(GmNbtBlob *blob, const char *key, int32_t value) {
    uint32_t bits;
    uint8_t raw[4];
    memcpy(&bits, &value, sizeof bits);
    raw[0] = (uint8_t)(bits >> 24); raw[1] = (uint8_t)(bits >> 16);
    raw[2] = (uint8_t)(bits >> 8); raw[3] = (uint8_t)bits;
    return nbt_blob_set_raw(blob, key, 3, raw, sizeof raw);
}

int gm_nbt_blob_set_double(GmNbtBlob *blob, const char *key, double value) {
    uint64_t bits;
    uint8_t raw[8];
    memcpy(&bits, &value, sizeof bits);
    raw[0] = (uint8_t)(bits >> 56); raw[1] = (uint8_t)(bits >> 48);
    raw[2] = (uint8_t)(bits >> 40); raw[3] = (uint8_t)(bits >> 32);
    raw[4] = (uint8_t)(bits >> 24); raw[5] = (uint8_t)(bits >> 16);
    raw[6] = (uint8_t)(bits >> 8); raw[7] = (uint8_t)bits;
    return nbt_blob_set_raw(blob, key, 6, raw, sizeof raw);
}

int gm_nbt_blob_set_string(
        GmNbtBlob *blob, const char *key, const char *value) {
    size_t length;
    uint8_t *raw;
    int ok;
    if (!value || (length = strlen(value)) > 0xffffu
            || !nbt_modified_utf8((const uint8_t *)value, length))
        return 0;
    raw = (uint8_t *)malloc(length + 2u);
    if (!raw) return 0;
    raw[0] = (uint8_t)(length >> 8); raw[1] = (uint8_t)length;
    memcpy(raw + 2, value, length);
    ok = nbt_blob_set_raw(blob, key, 8, raw, length + 2u);
    free(raw);
    return ok;
}

int gm_nbt_blob_set_string_list(
        GmNbtBlob *blob, const char *key,
        const char *const *values, size_t count) {
    size_t length = 5u, at = 5u;
    uint8_t *raw;
    int ok;
    if ((!values && count) || count > INT_MAX) return 0;
    for (size_t i = 0; i < count; ++i) {
        size_t item_length;
        if (!values[i] || (item_length = strlen(values[i])) > 0xffffu
                || !nbt_modified_utf8(
                    (const uint8_t *)values[i], item_length)
                || item_length > GM_NBT_BLOB_MAX - length - 2u)
            return 0;
        length += item_length + 2u;
    }
    raw = (uint8_t *)malloc(length);
    if (!raw) return 0;
    raw[0] = 8;
    raw[1] = (uint8_t)(count >> 24); raw[2] = (uint8_t)(count >> 16);
    raw[3] = (uint8_t)(count >> 8); raw[4] = (uint8_t)count;
    for (size_t i = 0; i < count; ++i) {
        size_t item_length = strlen(values[i]);
        raw[at++] = (uint8_t)(item_length >> 8);
        raw[at++] = (uint8_t)item_length;
        memcpy(raw + at, values[i], item_length); at += item_length;
    }
    ok = at == length && nbt_blob_set_raw(blob, key, 9, raw, length);
    free(raw);
    return ok;
}

int gm_nbt_blob_set_int_array(
        GmNbtBlob *blob, const char *key,
        const int32_t *values, size_t count) {
    uint8_t *raw;
    size_t length;
    int ok;
    if ((!values && count) || count > INT_MAX
            || count > (GM_NBT_BLOB_MAX - 4u) / 4u)
        return 0;
    length = 4u + count * 4u;
    raw = (uint8_t *)malloc(length);
    if (!raw) return 0;
    raw[0] = (uint8_t)(count >> 24); raw[1] = (uint8_t)(count >> 16);
    raw[2] = (uint8_t)(count >> 8); raw[3] = (uint8_t)count;
    for (size_t i = 0; i < count; ++i) {
        uint32_t bits;
        memcpy(&bits, &values[i], sizeof bits);
        raw[4 + i * 4] = (uint8_t)(bits >> 24);
        raw[5 + i * 4] = (uint8_t)(bits >> 16);
        raw[6 + i * 4] = (uint8_t)(bits >> 8);
        raw[7 + i * 4] = (uint8_t)bits;
    }
    ok = nbt_blob_set_raw(blob, key, 11, raw, length);
    free(raw);
    return ok;
}

int gm_nbt_blob_set_compound(
        GmNbtBlob *blob, const char *key, const GmNbtBlob *child) {
    if (!child || !child->data
            || !gm_nbt_blob_validate_root_compound(child->data, child->len))
        return 0;
    return nbt_blob_set_raw(
        blob, key, 10, child->data + 3, child->len - 3);
}

int gm_nbt_blob_append_compound_list(
        GmNbtBlob *blob, const char *key, const GmNbtBlob *element) {
    GmNbtCursor cursor;
    const uint8_t *old_elements = NULL;
    size_t old_length = 0;
    unsigned old_count = 0, type;
    uint8_t *payload;
    size_t payload_len;
    int found = 0, ok;
    if (!element || !element->data
            || !gm_nbt_blob_validate_root_compound(
                element->data, element->len)
            || !nbt_root_payload_cursor(blob, &cursor))
        return 0;
    for (;;) {
        int match;
        if (!nbt_u8(&cursor, &type)) return 0;
        if (type == 0) break;
        if (!nbt_utf_matches(&cursor, key, &match)) return 0;
        if (match && type == 9) {
            unsigned child, count;
            size_t start;
            if (!nbt_u8(&cursor, &child)
                    || !nbt_nonnegative_i32(&cursor, &count))
                return 0;
            start = cursor.at;
            for (unsigned i = 0; i < count; ++i)
                if (!nbt_payload(&cursor, child, 2)) return 0;
            if (child != 10 && count != 0) return 0;
            old_elements = blob->data + start;
            old_length = cursor.at - start;
            old_count = count;
            found = 1;
        } else {
            if (match) found = 0;
            if (!nbt_payload(&cursor, type, 1)) return 0;
        }
    }
    if (!found) { old_elements = NULL; old_length = 0; old_count = 0; }
    if (old_count == INT_MAX
            || element->len > GM_NBT_BLOB_MAX - old_length - 5u)
        return 0;
    payload_len = 5u + old_length + element->len - 3u;
    payload = (uint8_t *)malloc(payload_len);
    if (!payload) return 0;
    payload[0] = 10;
    payload[1] = (uint8_t)((old_count + 1u) >> 24);
    payload[2] = (uint8_t)((old_count + 1u) >> 16);
    payload[3] = (uint8_t)((old_count + 1u) >> 8);
    payload[4] = (uint8_t)(old_count + 1u);
    if (old_length) memcpy(payload + 5, old_elements, old_length);
    memcpy(payload + 5 + old_length,
           element->data + 3, element->len - 3);
    ok = nbt_blob_set_raw(blob, key, 9, payload, payload_len);
    free(payload);
    return ok;
}
