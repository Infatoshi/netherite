#include "nbtedit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The first character past the value that starts at p, or NULL. */
static const char *value_end(const char *p)
{
    if (*p == '"')
    {
        const char *q = strchr(p + 1, '"');
        return q == NULL ? NULL : q + 1;
    }

    if (*p == '{' || *p == '[')
    {
        int depth = 0;

        for (; *p != 0; ++p)
        {
            if (*p == '"')
            {
                const char *q = strchr(p + 1, '"');
                if (q == NULL) return NULL;
                p = q;
                continue;
            }

            if (*p == '{' || *p == '[')
            {
                ++depth;
            }
            else if (*p == '}' || *p == ']')
            {
                if (--depth == 0) return p + 1;
            }
        }

        return NULL;
    }

    while (*p != 0 && *p != ',' && *p != '}') ++p;
    return p;
}

nbt *nbt_set_key(const nbt *comp, const char *key, nbt *value)
{
    if (comp == NULL || nbt_kind(comp) != NBT_COMPOUND)
    {
        nbt_free(value);
        return NULL;
    }

    char *text = nbt_render(comp);
    char *vtext = nbt_render(value);
    nbt_free(value);

    size_t cap = strlen(text) + strlen(vtext) + strlen(key) + 8;
    char *out = malloc(cap);
    if (out == NULL) abort();

    size_t n = 0;
    out[n++] = '{';

    const char *p = text + 1;
    int first = 1;

    while (*p != 0 && *p != '}')
    {
        /* one member: "key":value, comma separated */
        const char *kend = strchr(p + 1, '"');

        if (p[0] != '"' || kend == NULL || kend[1] != ':')
        {
            free(text); free(vtext); free(out);
            return NULL;
        }

        size_t klen = (size_t)(kend - (p + 1));
        const char *vend = value_end(kend + 2);

        if (vend == NULL) { free(text); free(vtext); free(out); return NULL; }

        int same = klen == strlen(key) && memcmp(p + 1, key, klen) == 0;

        if (!same)
        {
            if (!first) out[n++] = ',';
            memcpy(out + n, p, (size_t)(vend - p));
            n += (size_t)(vend - p);
            first = 0;
        }

        p = vend;

        if (*p == ',') ++p;
    }

    /* The new member goes last (or replaces the old one's place is simply
     * dropped); nbt_render sorts the keys on the way out, so the order here
     * does not reach the canonical text. */
    if (!first) out[n++] = ',';
    out[n++] = '"';
    memcpy(out + n, key, strlen(key));
    n += strlen(key);
    out[n++] = '"';
    out[n++] = ':';
    memcpy(out + n, vtext, strlen(vtext));
    n += strlen(vtext);

    out[n++] = '}';
    out[n] = 0;

    free(text);
    free(vtext);

    nbt *r = nbt_parse(out);

    /* The spliced text is canonical by construction, so a parse failure means a
     * string the format does not escape (a quote or backslash inside one), which
     * is not data this port has to carry. Refuse loudly, never drop the key. */
    if (r == NULL)
    {
        fprintf(stderr, "nbtedit: the spliced text does not parse: %s\n", out);
        abort();
    }

    free(out);
    return r;
}