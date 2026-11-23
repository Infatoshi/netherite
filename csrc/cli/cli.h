/* nether CLI: what every module shares (csrc/cli/cli_util.c): the checkout, growable strings and string lists,
 * files, time, a shell-free word splitter, JSON string escaping and SHA-256.
 *
 * Nothing here knows a host, a user, a seed or an experiment: those live in YAML and TSV files under configs/. */
#ifndef NETH_CLI_H
#define NETH_CLI_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* the checkout (nether.c's find_root fills it) */
extern char cli_root[PATH_MAX];

/* a growable string, always NUL-terminated once anything was put in it */
struct sb {
    char *s;
    size_t n, cap;
};
void sb_putn(struct sb *b, const char *s, size_t n);
void sb_puts(struct sb *b, const char *s);
void sb_putc(struct sb *b, char c);
void sb_printf(struct sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *sb_str(struct sb *b);
void sb_free(struct sb *b);

/* a growable list of owned strings, NULL-terminated (an argv) */
struct sv {
    char **v;
    int n, cap;
};
void sv_push(struct sv *l, const char *s);
void sv_pushf(struct sv *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sv_free(struct sv *l);

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t k);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* snprintf into a buffer that must hold it all: a truncated path is an error, never a silently different file
 * (exits 2 with a message) */
void cli_fmt(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* a file's bytes (NUL-terminated; *len without it), NULL when unreadable */
char *cli_read_file(const char *path, size_t *len);
/* path written whole or not at all: a temporary beside it, then rename; 0 or -1 */
int cli_write_file_atomic(const char *path, const char *data, size_t len);
/* data appended under an exclusive flock of the file itself (rows of stacked agents never interleave); 0 or -1 */
int cli_append_locked(const char *path, const char *header, const char *row);
int cli_mkdirs(const char *path);
int cli_exists(const char *path);
int cli_is_dir(const char *path);
/* p as an absolute path (from the current directory), in buf (PATH_MAX) */
const char *cli_abs(const char *p, char *buf);
/* p as a path from the checkout when it is not absolute and not found from here */
const char *cli_resolve(const char *p, char *buf);

double cli_now(void);          /* monotonic seconds */

/* --profile (nether --profile VERB, or NETHER_PROFILE=1): each phase's CLOCK_MONOTONIC seconds, summed over the run
 * and printed by main at the end in milliseconds. Phases do not overlap: a spawn inside the lock and GPU guard counts
 * as the guard's. */
enum { CLI_P_STARTUP, CLI_P_YAML, CLI_P_JSON, CLI_P_LOCK, CLI_P_SPAWN, CLI_P_STATS, CLI_P_N };
extern int cli_prof_on, cli_prof_nest;
extern double cli_prof_s[CLI_P_N];
extern const char *const cli_prof_name[CLI_P_N];
/* the seconds since t0 added to phase p (outside another timed phase) */
void cli_prof_add(int p, double t0);
double cli_wall(void);         /* seconds since the epoch */
void cli_stamp(char *buf, size_t n, const char *fmt); /* local time, strftime FMT */

/* s split into words as a shell would split a simple command, without running one: spaces separate, '...'
 * keeps everything, "..." keeps everything but \" and \\, a backslash escapes one character. No expansions,
 * globs, pipes or redirections: a word that would have been one (an unquoted | ; & < > ` $( ) is refused, so a
 * command never silently means something else. 0, or -1 with the reason in err. */
int cli_split_words(const char *s, struct sv *out, char *err, size_t en);
/* every {KEY} in tmpl replaced by its value from keys/vals (n pairs); an unknown {KEY} stays as it is */
char *cli_subst(const char *tmpl, const char *const *keys, const char *const *vals, int n);

/* s as a JSON string (quoted, escaped) appended to b */
void cli_json_str(struct sb *b, const char *s);
/* a JSON number (null for NaN and the infinities) */
void cli_json_num(struct sb *b, double x);

/* SHA-256 (FIPS 180-4), pure C */
void cli_sha256(const void *data, size_t n, uint8_t out[32]);
/* a file's SHA-256 as 64 hex digits; 0, or -1 when unreadable */
int cli_sha256_file(const char *path, char hex[65]);
void cli_hex(const uint8_t *b, size_t n, char *out);

/* the checkout's HEAD commit (12 hex digits, "nogit" without git), with "+dirty" when the tree has edits */
void cli_git_sha(char *buf, size_t n);

/* a "--flag value" pair at argv[*i]: the value, advancing *i, or exits 2 when it is missing */
const char *cli_arg(int argc, char **argv, int *i, const char *verb);
/* strtod/strtol that refuse junk: 0, or -1 */
int cli_num(const char *s, double *out);
int cli_long(const char *s, long *out);

#endif
