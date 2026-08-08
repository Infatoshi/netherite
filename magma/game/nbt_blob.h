#ifndef MAGMA_NBT_BLOB_H
#define MAGMA_NBT_BLOB_H

#include <stddef.h>
#include <stdint.h>

#define GM_NBT_BLOB_MAX (1u << 20)

typedef struct {
    uint8_t *data;
    size_t len;
} GmNbtBlob;

void gm_nbt_blob_clear(GmNbtBlob *blob);
int gm_nbt_blob_validate_root_compound(const void *data, size_t len);
int gm_nbt_blob_set(GmNbtBlob *blob, const void *data, size_t len);
int gm_nbt_blob_copy(GmNbtBlob *dst, const GmNbtBlob *src);
int gm_nbt_blob_wrap_named_compound(
    GmNbtBlob *out, const char *name, const GmNbtBlob *child);

#endif
