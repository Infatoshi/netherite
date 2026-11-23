/* A JSON reader for the oracle's tapes and snapshots, and the tape row loop.
 *
 * Small and dependency-free: the oracle writes JSON with no whitespace beyond
 * the newline between rows, so the reader only has to handle what Gson emits.
 * Every value keeps the raw text it occupied (raw/rawlen), which is what makes
 * the canonical NBT members ("nbt":{...}) directly parseable by nbt_parse: the
 * canonical form is valid JSON with typed string scalars, so its raw slice is
 * exactly the text nbtjson reads.
 *
 * A parsed value owns the line it came from: json_parse takes the buffer over
 * and json_free releases it, so raw slices stay valid for the value's life.
 */
#ifndef NETHERITE_TAPE_H
#define NETHERITE_TAPE_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

struct jfield {
    char *key;
    struct jval *val;
};

struct jval {
    int kind;
    const char *raw;   /* the value's own text, into the buffer json_parse took */
    size_t rawlen;
    char *str;         /* J_STR: unescaped, owned */
    int64_t num;       /* J_NUM: integral when has_num */
    double dbl;
    int has_num, has_dbl;
    int boolean;       /* J_BOOL */
    struct jfield *fields;
    int nfields;
    struct jval **items;
    int nitems;
    char *owned;       /* the line this value was parsed from, freed with the root */
};

/* Parse one value; takes ownership of text (which must be malloc'd and NUL
 * terminated) and frees it in json_free. NULL on a syntax error. */
struct jval *json_parse(char *text);
void json_free(struct jval *v);

/* A member of an object, or NULL. */
const struct jval *json_get(const struct jval *o, const char *key);
const struct jval *json_at(const struct jval *a, int i);
int json_len(const struct jval *a);
const char *json_str(const struct jval *v);

/* Numbers, whether written as a JSON number or as the canonical NBT scalar
 * strings "i:" "l:" "s:" "b:" (integral) and "f:<8 hex>" "d:<16 hex>" (raw
 * bits). 1 on success. */
int json_int(const struct jval *v, int64_t *out);
int json_double(const struct jval *v, uint64_t *bits);
int json_float(const struct jval *v, uint32_t *bits);

/* The value's raw text, NUL terminated, malloc'd. */
char *json_raw(const struct jval *v);

/* ------------------------------------------------------------------- lines */

/* A line source over a plain FILE or a gzFile, one growing buffer. */
struct lines {
    void *f;      /* FILE * */
    void *g;      /* gzFile, when f == NULL */
    void *z;      /* struct gunzip, when f and g are NULL */
    char *buf;
    size_t cap, len, pos;
    int eof, failed;
};

void lines_init(struct lines *l);
void lines_file(struct lines *l, FILE *f);
void lines_gz(struct lines *l, void *gz);
void lines_gunzip(struct lines *l, void *z);
/* The next line without its newline, or NULL at the end. The buffer is reused
 * by the next call. */
const char *lines_next(struct lines *l);
void lines_free(struct lines *l);

/* -------------------------------------------------------------------- tape */

/* A tape: the header line, then one row per line. */
struct jarena;
struct tape {
    struct lines in;
    char *header;
    struct jval *hdr;
    struct jval *row;        /* the current row, in arena (tape.c) */
    struct jarena *arena;
    int64_t next_t;
    int failed;
};

int tape_open(struct tape *t, const char *path);
/* 1 with *row set, 0 at the end, -1 after a malformed row or header. The row
 * (and every value inside it) is freed by the next call and by tape_close. */
int tape_next(struct tape *t, const struct jval **row);
void tape_close(struct tape *t);

#endif