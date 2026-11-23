/* nether CLI: a small JSON reader (csrc/cli/cli_json.c) for the trainer's episodes.jsonl, iters.jsonl and
 * score.json lines. Objects keep their keys' order. */
#ifndef NETH_CLI_JSON_H
#define NETH_CLI_JSON_H

#include "cli.h"

enum { JV_NULL, JV_BOOL, JV_NUM, JV_STR, JV_ARR, JV_OBJ };

struct jv {
    int t;
    double num;          /* JV_NUM, JV_BOOL (0 or 1) */
    char *str;           /* JV_STR */
    int n;               /* JV_ARR, JV_OBJ: members */
    char **keys;         /* JV_OBJ */
    struct jv **kids;
};

/* one JSON value from s (whitespace after it allowed), or NULL with the reason in err */
struct jv *jv_parse(const char *s, char *err, size_t en);
void jv_free(struct jv *v);
/* an object's member, NULL when absent or v is no object */
const struct jv *jv_get(const struct jv *v, const char *key);
/* a member as a number (a bool counts), dflt when absent or not a number */
double jv_num(const struct jv *v, const char *key, double dflt);
const char *jv_str(const struct jv *v, const char *key, const char *dflt);

#endif
