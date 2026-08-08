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
int gm_nbt_blob_find_compound_int(
    const GmNbtBlob *blob, const char *compound, const char *key,
    int32_t *value_out);
/* Direct-child accessors for complete root compounds. Numeric lookup accepts
 * every NBT primitive and mirrors NBTBase.NBTPrimitive conversion to double.
 * Duplicate names use NBTTagCompound's last-value-wins semantics. */
int gm_nbt_blob_find_number(
    const GmNbtBlob *blob, const char *key, double *value_out,
    int *type_out);
int gm_nbt_blob_find_string(
    const GmNbtBlob *blob, const char *key, char *value_out,
    size_t capacity);
int gm_nbt_blob_find_numeric_list(
    const GmNbtBlob *blob, const char *key, double *values,
    size_t capacity, size_t *count_out, int *element_type_out);
int gm_nbt_blob_find_list_info(
    const GmNbtBlob *blob, const char *key, size_t *count_out,
    int *element_type_out);
int gm_nbt_blob_find_string_list_element(
    const GmNbtBlob *blob, const char *key, size_t index,
    char *value_out, size_t capacity);
int gm_nbt_blob_extract_compound(
    const GmNbtBlob *blob, const char *key, GmNbtBlob *out);
int gm_nbt_blob_extract_compound_list_element(
    const GmNbtBlob *blob, const char *key, size_t index,
    GmNbtBlob *out);
int gm_nbt_blob_wrap_named_compound(
    GmNbtBlob *out, const char *name, const GmNbtBlob *child);
/* Cold-path compound editors. They preserve every unrelated child byte for
 * byte-stable arbitrary ItemStack payload copying and replace duplicate keys
 * with one final Java-map-equivalent value. */
int gm_nbt_blob_make_empty(GmNbtBlob *out);
int gm_nbt_blob_set_byte(GmNbtBlob *blob, const char *key, int8_t value);
int gm_nbt_blob_set_int(GmNbtBlob *blob, const char *key, int32_t value);
int gm_nbt_blob_set_double(GmNbtBlob *blob, const char *key, double value);
int gm_nbt_blob_set_string(
    GmNbtBlob *blob, const char *key, const char *value);
int gm_nbt_blob_set_string_list(
    GmNbtBlob *blob, const char *key,
    const char *const *values, size_t count);
int gm_nbt_blob_set_int_array(
    GmNbtBlob *blob, const char *key,
    const int32_t *values, size_t count);
int gm_nbt_blob_set_compound(
    GmNbtBlob *blob, const char *key, const GmNbtBlob *child);
int gm_nbt_blob_append_compound_list(
    GmNbtBlob *blob, const char *key, const GmNbtBlob *element);

#endif
