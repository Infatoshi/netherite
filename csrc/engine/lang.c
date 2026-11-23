#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct lang_row { const char *key, *value; };
#include "../../out/native/lang_generated.h"
const char *lang_text(const char *key)
{
    for (size_t i = 0; i < sizeof LANG_ROWS / sizeof LANG_ROWS[0]; ++i)
        if (!strcmp(key, LANG_ROWS[i].key)) return LANG_ROWS[i].value;
    fprintf(stderr, "missing generated language key: %s\n", key);
    abort();
}
