/* The nbt tree's layout, private to nbtjson.c (which builds it) and nbtbin.c
 * (which writes it as binary NBT per entity per row and reads it directly
 * rather than through a call per field). */
#ifndef NETHERITE_NBTJSON_TREE_H
#define NETHERITE_NBTJSON_TREE_H

#include <stdint.h>

#include "nbtjson.h"

/* A compound key, interned: one per distinct string for the life of the
 * process, so two keys are equal exactly when their pointers are. It carries
 * what the binary writer needs: String.hashCode (the HashMap bucket), the
 * length, and whether every byte is ASCII and the length fits writeUTF (then
 * its bytes are the key's own). */
struct nbt_key {
    const char *s;
    size_t len;
    uint32_t hash;
    int ascii;
};

struct nbt_field {
    const struct nbt_key *key;
    nbt *val;
};

struct nbt {
    nbt_type type;
    int shared;                           /* a string whose s is an interned key's text (not freed) */
    union {
        int64_t i;                        /* byte, short, int, long */
        float f;
        double d;
        char *s;
        struct { signed char *v; int n; } bytes;
        struct { int *v; int n; } ints;
        struct { nbt **v; int n, cap; } list;
        struct { struct nbt_field *f; int n, cap; } comp;
    } u;
};

/* nbt_put with the key already interned (nbtw.h's tree output). */
void nbt_put_key(nbt *comp, const struct nbt_key *key, nbt *v);

#endif
