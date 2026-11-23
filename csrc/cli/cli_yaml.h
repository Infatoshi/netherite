/* nether CLI: a small YAML reader and writer (csrc/cli/cli_yaml.c), no library.
 *
 * What it reads: nested maps by indentation (spaces only), "key: value" and "key:" lines, "- item" lists of
 * scalars, comments (a line's first non-space "#", or " #" after a value), values plain or in '...' / "...".
 * Every run file and config.yaml (the strict grammar in config.yaml's header) is such a file, as are the CLI's own
 * files (the gyms under configs/gyms, configs/gates.yaml, bank files). Order is kept, so a file written back out keeps its
 * keys where they were (comments are not kept: a written file is a derived one). */
#ifndef NETH_CLI_YAML_H
#define NETH_CLI_YAML_H

#include "cli.h"

struct cy {
    char *key;         /* NULL for a list item */
    char *val;         /* NULL for a map or a list */
    struct cy **kid;
    int nk, cap, line;
};

/* a file's tree (the root is an unnamed map), or NULL with the reason in err */
struct cy *cy_load(const char *path, char *err, size_t en);
struct cy *cy_parse(const char *text, const char *name, char *err, size_t en);
void cy_free(struct cy *t);

/* the node at a dotted path ("sim.client.ticks_per_step"), NULL when absent */
struct cy *cy_node(const struct cy *t, const char *path);
/* its scalar, or dflt when absent or not a scalar */
const char *cy_get(const struct cy *t, const char *path, const char *dflt);
double cy_num(const struct cy *t, const char *path, double dflt);
long cy_long(const struct cy *t, const char *path, long dflt);
/* on/yes/true/1 as 1, off/no/false/0 as 0, else dflt */
int cy_bool(const struct cy *t, const char *path, int dflt);

/* path set to val, its maps made where missing; 0, or -1 when a scalar stands where a map must be */
int cy_set(struct cy *t, const char *path, const char *val);
/* KEY=VALUE applied to a run file: KEY is a full dotted path, or a path that exists under exactly one top-level
 * part (client.cast_precision under sim:, ppo.lr under policy:). strict: VALUE must be a plain scalar of the run
 * files' grammar ([A-Za-z0-9_./+,:-]). 0, or -1 with the reason in err. */
int cy_override(struct cy *t, const char *kv, int strict, char *err, size_t en);
/* the tree as YAML, two spaces a level */
void cy_dump(const struct cy *t, struct sb *out);

/* a deep copy */
struct cy *cy_clone(const struct cy *n);
/* src merged into dst key by key: a scalar replaces, a map merges, a list replaces; 0, or -1 when a scalar and a
 * map meet at one key */
int cy_merge(struct cy *dst, const struct cy *src);
/* src (a scalar, a map or a list) put at path in t, the maps on the way made: cy_set for a scalar, cy_merge for a
 * map; 0 or -1 */
int cy_put(struct cy *t, const char *path, const struct cy *src);

#endif
