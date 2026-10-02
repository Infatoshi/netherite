/* Matched tracepoints for the native port, the counterpart of
 * oracle/harness/netherite/oracle/Trace.java. Same line format; see trace.h. */
#include "trace.h"
#include "env.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define g_trace (nw_env->trace.out)

/* mkdir -p for the directory part of path, so a trace into a fresh directory
 * does not depend on the caller creating it first. */
static void mkdir_parent(const char *path)
{
    char buf[4096];
    size_t n = strlen(path);
    if (n >= sizeof buf) return;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; ++i)
        if (buf[i] == '/')
        {
            buf[i] = 0;
            mkdir(buf, 0777);
            buf[i] = '/';
        }
}

void trace_open(const char *path)
{
    if (!path) return;
    mkdir_parent(path);
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        perror(path);
        return;
    }
    g_trace = f;
}

void trace_close(void)
{
    if (g_trace)
    {
        fclose(g_trace);
        g_trace = NULL;
    }
}

void trace(const char *label, const char *types, ...)
{
    if (!g_trace) return;
    va_list ap;
    va_start(ap, types);
    fputs(label, g_trace);
    for (const char *t = types; *t; ++t)
    {
        fputc(' ', g_trace);
        if (*t == 'd')
        {
            double v = va_arg(ap, double);
            uint64_t bits;
            memcpy(&bits, &v, sizeof bits);
            fprintf(g_trace, "d:%016llx", (unsigned long long)bits);
        }
        else if (*t == 'f')
        {
            /* a float arrives promoted to double; convert back, then take the bits */
            float v = (float)va_arg(ap, double);
            uint32_t bits;
            memcpy(&bits, &v, sizeof bits);
            fprintf(g_trace, "f:%08x", (unsigned)bits);
        }
        else if (*t == 'i')
        {
            fprintf(g_trace, "%d", va_arg(ap, int));
        }
        else if (*t == 'l')
        {
            fprintf(g_trace, "%lld", (long long)va_arg(ap, int64_t));
        }
    }
    fputc('\n', g_trace);
    va_end(ap);
}