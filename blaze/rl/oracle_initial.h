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
