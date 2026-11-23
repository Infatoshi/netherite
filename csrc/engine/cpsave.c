/* A checkpoint start's save directory: the save itself, or a refusal
 * (cpsave.h). */
#include "cpsave.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int cs_fail(char *err, size_t en, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (err && en) vsnprintf(err, en, fmt, ap);
    va_end(ap);
    return -1;
}

int cpsave_resolve(const char *cpdir, char *out, size_t n, char *err, size_t en)
{
    char p[4200];
    snprintf(p, sizeof p, "%.4000s/save", cpdir);
    struct stat sb;
    if (stat(p, &sb) == 0 && S_ISDIR(sb.st_mode))
    {
        if (strlen(p) >= n) return cs_fail(err, en, "checkpoint save path %.300s too long", p);
        snprintf(out, n, "%s", p);
        return 0;
    }
    char arch[4200];
    snprintf(arch, sizeof arch, "%.4000s/save.tar.zst", cpdir);
    const char *id = strrchr(cpdir, '/');
    char idb[256] = "";
    if (id && id > cpdir && strcmp(id, "/cp") == 0)
    {
        /* a library entry's checkpoint is LIB/ID/cp: the ID for the message */
        const char *s = id - 1;
        while (s > cpdir && s[-1] != '/') --s;
        snprintf(idb, sizeof idb, "%.*s", (int)(id - s) < 200 ? (int)(id - s) : 200, s);
    }
    if (stat(arch, &sb) == 0 && S_ISREG(sb.st_mode))
        return cs_fail(err, en, "%.300s/save missing (packed: run harvest.sh -l LIB --unpack %s)", cpdir,
                       idb[0] ? idb : "ID");
    return cs_fail(err, en, "%.300s/save missing", cpdir);
}
