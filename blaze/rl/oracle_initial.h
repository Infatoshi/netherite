#pragma once
#include "blaze_snapshot.h"
#include <stddef.h>
#include <stdint.h>
typedef struct {
  RlSnapHead head;
  uint16_t *blocks;
  size_t cells;
  uint64_t mismatches;
  int compared, first_x, first_y, first_z;
  unsigned first_snapshot, first_oracle;
} OracleInitial;
int oracle_initial_load(OracleInitial *s, const char *path, int64_t seed,
                        int require_empty, char *err, size_t cap);
void oracle_initial_free(OracleInitial *s);
/* Java file is y,z,x u16le; snapshot is x,y,z u16le. Exact size required. */
int oracle_initial_compare(OracleInitial *s, const char *java_blocks, char *err,
                           size_t cap);
/* Derive a NEW v2 fixture from an empty v2 template plus Java y,z,x block
 * and packed-light files. Writes OUT and OUT.provenance.json exclusively.
 * Does not import world clocks, RNG, entities, biomes or scheduled updates. */
int oracle_fixture_write(const char *template_path, const char *blocks_path,
                         const char *light_path, const char *output_path,
                         char *err, size_t cap);
