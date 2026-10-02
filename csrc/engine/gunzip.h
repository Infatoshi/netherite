/* A whole gzip file read into memory by gunzip.c's own inflater (the same
 * bytes zlib gives, checked against the trailer's CRC-32 and length). */
#ifndef NETHERITE_GUNZIP_H
#define NETHERITE_GUNZIP_H

#include <stddef.h>
#include <stdint.h>

/* The single-member gzip file at path, inflated into a malloc'd buffer (*out,
 * *len bytes, 8 bytes of slack after it). 1 on success; 0 when the file is
 * missing or anything about it is not a single well-formed member, and the
 * caller then reads it with zlib, which reports the reason. */
int gunzip_file(const char *path, uint8_t **out, size_t *len);
/* check_crc 0: the trailer's CRC-32 is neither computed nor checked (its
 * length still is), for a reader whose input another check verifies (a
 * snapshot's replay: its load check reads the same files with it). */
int gunzip_file_crc(const char *path, uint8_t **out, size_t *len, int check_crc);

/* The same file read in pieces, for a stream too large to hold inflated: its
 * own inflater when the file is a single well-formed member, else zlib's
 * gzread. gunzip_read gives n bytes, or fewer at the end of the stream, or -1
 * on a bad stream (a CRC-32 or length that does not match the trailer is
 * one, reported once the stream's end is reached). NULL from gunzip_open
 * when the file cannot be opened at all. */
struct gunzip;
struct gunzip *gunzip_open(const char *path);
struct gunzip *gunzip_open_crc(const char *path, int check_crc);
long gunzip_read(struct gunzip *z, void *dst, size_t n);
/* gzgets: up to len - 1 bytes through the next newline (kept), NUL
 * terminated; NULL when nothing was read (the end, or a bad stream). */
char *gunzip_gets(struct gunzip *z, char *dst, int len);
void gunzip_close(struct gunzip *z);

#endif
