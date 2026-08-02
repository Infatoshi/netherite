/* core/config.h - the magma configuration registry: ONE source of truth for
 * every runtime knob, generated from core/config.def.
 *
 * WHY: runtime behaviour used to be steered by ~100 scattered getenv() calls.
 * An env var is invisible (no list, no doc, no dump), silently typo-tolerant,
 * and leaks into every child process. The registry replaces that with a flat
 * key=value namespace that is declared in exactly one place (config.def),
 * documented inline, dumpable (--dump-config), and settable from exactly two
 * places: a conf file and --set on the command line.
 *
 * LOAD ORDER (app/game_main.c):
 *     compiled defaults  ->  magma.conf (or --conf PATH)  ->  --set key=value
 * Last wins. Anything that differs from its default afterwards is echoed as one
 * "config: k=v ..." line on stderr, so a run's stderr always carries the
 * reproduction recipe.
 *
 * ZERO HEAP, ZERO DEPS: the CrConfig is a file-static struct, strings are fixed
 * char[CR_CFG_STR_MAX] inline storage, and the parser is the same line
 * discipline game/caps.c has always used ('#' comment, '=' normalized to
 * whitespace, "key value"). No yaml, no json, no library.
 *
 * UNKNOWN KEYS ARE FATAL. A conf file or --set naming a key that is not in the
 * registry prints one stderr line and exit(2)s. Silently ignoring them is how a
 * config rots: the knob gets renamed, the caller keeps setting the old name, and
 * nobody notices for a year.
 *
 * ADDING A KNOB: add one CFG_* line to core/config.def. That is the whole
 * change - the struct field, the default, the parser case, the --dump-config
 * row and the breadcrumb all follow from it.
 */
#ifndef MAGMA_CORE_CONFIG_H
#define MAGMA_CORE_CONFIG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inline storage for every CFG_STR value (paths, multi-spec dump lists).
 * 1024: worlddump joins several "tick,cx,cz,ncx,ncz,/tmp/.../probe_N.bin"
 * specs with ';'; 256 was too short under TMPDIR=.../.tmp paths. No heap. */
#define CR_CFG_STR_MAX 1024

typedef struct {
#define CFG_BOOL(name, def, doc) int       name;
#define CFG_INT(name, def, doc)  int       name;
#define CFG_I64(name, def, doc)  long long name;
#define CFG_F32(name, def, doc)  float     name;
#define CFG_STR(name, def, doc)  char      name[CR_CFG_STR_MAX];
#include "core/config.def"
#undef CFG_BOOL
#undef CFG_INT
#undef CFG_I64
#undef CFG_F32
#undef CFG_STR
} CrConfig;

/* Effective config singleton. On first use it lazily loads "magma.conf" from the
 * cwd (defaults if absent), exactly like cr_caps() always has. Never NULL. */
const CrConfig *cr_cfg(void);

/* (Re)load: reset every key to its compiled default, then apply `path`
 * (NULL -> "magma.conf"). A missing file is fine - it just leaves the defaults.
 * An unknown key or a malformed value inside the file is FATAL (exit 2).
 * Must run BEFORE anything caches a value off cr_cfg(). */
void cr_cfg_load(const char *path);

/* Programmatic single-key override (same key space as the conf file).
 * Returns 0 on success, -1 if `key` is not in the registry, -2 if `val` does not
 * parse for that key's type (bool takes "0" or "1" only). Callers turn a nonzero
 * return into a diagnostic + exit(2); nothing in magma tolerates a bad key. */
int cr_cfg_set(const char *key, const char *val);

/* Print every registry key, its effective value and its doc to `out`. A '*' in
 * column 0 marks a key whose value is not the compiled default. */
void cr_cfg_dump(FILE *out);

/* Reproducibility breadcrumb: one line "config: k1=v1 k2=v2 ..." listing only
 * the keys that differ from their defaults. Prints NOTHING when all-default, so
 * a normal run's stderr is unchanged. */
void cr_cfg_log_overrides(FILE *out);

/* Bumped by every cr_cfg_load and every successful cr_cfg_set. Consumers that
 * cache DERIVED state off the config (game/caps.c and its pool geometry) compare
 * this against the generation they built from and rebuild on a mismatch, so a
 * late cr_cfg_set can never leave stale derived values behind. */
unsigned cr_cfg_generation(void);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_CORE_CONFIG_H */
