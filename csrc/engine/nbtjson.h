/* Canonical NBT as JSON text, the form netherite.oracle.StructuresProbe.canon
 * writes and the structure probe files use: a compound is an object whose keys
 * print in Java String.compareTo order (plain byte order for the ASCII keys
 * here), a list is an array, and a scalar is a string carrying its type,
 * "b:1" "s:1" "i:-3" "l:5" "f:"+8 hex digits of the raw float bits "d:"+16
 * "str:text" "ba:1,2" "ia:1,2" (empty "ba:" / "ia:"). No whitespace anywhere.
 * Text that is not in this form is refused by nbt_parse, so a probe file can
 * be turned back into a tree and compared key by key. */
#ifndef NETHERITE_NBTJSON_H
#define NETHERITE_NBTJSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NBT_BYTE, NBT_SHORT, NBT_INT, NBT_LONG, NBT_FLOAT, NBT_DOUBLE, NBT_STRING,
    NBT_BYTE_ARRAY, NBT_INT_ARRAY, NBT_LIST, NBT_COMPOUND
} nbt_type;

typedef struct nbt nbt;

/* An owned tree; every value is released by the nbt_free of its root. */
nbt *nbt_new_byte(int v);
nbt *nbt_new_short(int v);
nbt *nbt_new_int(int v);
nbt *nbt_new_long(int64_t v);
nbt *nbt_new_float(float v);
nbt *nbt_new_double(double v);
nbt *nbt_new_string(const char *s);
nbt *nbt_new_byte_array(const signed char *v, int n);
nbt *nbt_new_int_array(const int *v, int n);
nbt *nbt_new_list(void);
nbt *nbt_new_compound(void);

/* Append to a list, or set a key in a compound. The value is owned by the
 * container from here on. */
void nbt_list_add(nbt *list, nbt *v);
void nbt_put(nbt *comp, const char *key, nbt *v);
/* nbt_put with a key the compiler can prove is a string literal (the pointer
 * and its first character both constant): the key is interned once per call
 * site and the site's cache checked by pointer alone, which a literal's fixed
 * content makes enough. Any other key takes nbt_put's content check. */
struct nbt_put_site { const char *p; const void *k; };

/* The size classes of the trees' memory (nbtjson.c), which each environment
 * keeps its own of (env.h). */
#define NBT_POOL_CLASSES 9
void nbt_put_site(nbt *comp, struct nbt_put_site *site, const char *key, nbt *v);
#if defined(__GNUC__)
#define nbt_put(comp, key, v) \
    (__builtin_constant_p(key) && __builtin_constant_p((key)[0]) \
         ? ({ static struct nbt_put_site nbt_put_site_; nbt_put_site((comp), &nbt_put_site_, (key), (v)); }) \
         : nbt_put((comp), (key), (v)))
#endif
void nbt_list_set(nbt *list, int i, nbt *v);
void nbt_comp_set(nbt *comp, const char *key, nbt *v);
int nbt_replace(nbt *comp, const char *key, nbt *v);

/* None of these are needed to print; they read a tree back, for a caller that
 * compares two of them field by field. */
nbt_type nbt_kind(const nbt *v);
const nbt *nbt_get(const nbt *comp, const char *key);
/* The field key's value taken out of comp (the caller owns it), or NULL. */
nbt *nbt_take(nbt *comp, const char *key);
int nbt_list_size(const nbt *list);
const nbt *nbt_list_get(const nbt *list, int i);
const char *nbt_string_value(const nbt *v);

/* The value of an integer-like scalar ("b:", "s:", "i:", "l:"), decimal; 0 on
 * a non-integer node. */
long nbt_int_value(const nbt *v);

/* The fields of a compound in the order they were put, which is what a writer
 * reproducing Java's NBTTagCompound order needs: a compound's iteration order
 * is its HashMap's, and that depends on the insertion order. count 0 on a node
 * that is not a compound. */
int nbt_field_count(const nbt *comp);
const char *nbt_field_key(const nbt *comp, int i);
const nbt *nbt_field_value(const nbt *comp, int i);

/* The raw bits of a float or double node, and the elements of a byte or int
 * array (count into *n, NULL when the node is another kind). */
uint32_t nbt_float_bits(const nbt *v);
uint64_t nbt_double_bits(const nbt *v);
const signed char *nbt_byte_array(const nbt *v, int *n);
const int *nbt_int_array(const nbt *v, int *n);

/* Canonical text, malloc'd: no whitespace, keys sorted. */
char *nbt_render(const nbt *v);

/* The tree nbt_parse(nbt_render(v)) makes, without the text (a compound's
 * fields in key order); NULL where that round trip would not hold (a string
 * with a quote in it). */
nbt *nbt_copy_canonical(const nbt *v);

/* Parse canonical text into a tree, or NULL when the text is not canonical
 * NBT JSON (unknown scalar prefix, stray character, escape). */
nbt *nbt_parse(const char *text);

/* 1 when a and b differ. Writes "<path>: want <a> got <b>" into buf, path
 * starting at "(root)" and descending by key or index, e.g.
 * "Children[3].Num: want i:4 got i:5". */
int nbt_diff(const nbt *a, const nbt *b, char *buf, size_t n);

void nbt_free(nbt *v);
/* The scratch region, for trees built only to be read at once (the entity
 * digest's, one per entity per row): between begin and end every value comes
 * from one region that end rewinds as a whole, so nothing built in between
 * may outlive the end or be attached to a tree that does; nbt_free of a
 * scratch value does nothing. Each build starts at the same address on zeroed
 * memory, so nbt_scratch_bytes (the bytes the build used, or NULL when any of
 * it lies elsewhere) together with the root pointer determines the tree: its
 * nodes, arrays and copied strings are in those bytes, and every other
 * pointer in them is an interned key or string, which never changes. */
void nbt_scratch_begin(void);
void nbt_scratch_end(void);
const void *nbt_scratch_bytes(size_t *n);

#endif
