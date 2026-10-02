/* Matched tracepoints for the native port: the same call site as
 * netherite.oracle.Trace.t in the Java oracle writes the same line, so a diff of
 * the two files (csrc/tests/trace_diff.c) names the first step where the two
 * part. A no-op when no file is open. */
#ifndef NETHERITE_TRACE_H
#define NETHERITE_TRACE_H

/* Appends to path (creating its directory); does nothing else until close. */
void trace_open(const char *path);

/* Flushes and closes. Safe to call with no trace open. */
void trace_close(void);

/* One char of types per value: 'd' double, 'f' float (arrives as double through
 * varargs; converted back to float before taking the bits), 'i' int, 'l'
 * int64_t. Writes "label v1 v2 ...": doubles as d: + 16 hex digits of the raw
 * bits, floats as f: + 8, ints and int64s decimal. */
void trace(const char *label, const char *types, ...);

#endif