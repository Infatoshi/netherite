/* The byte codec of the parked chunks and the spill file (lzpack.h). */
#include "lzpack.h"

#include <stddef.h>
#include <string.h>

#define LZP_MIN 4

static inline uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static inline uint64_t rd64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static inline void cp8(uint8_t *d, const uint8_t *s)
{
    memcpy(d, s, 8);
}

static inline void cp16(uint8_t *d, const uint8_t *s)
{
    memcpy(d, s, 16);
}

/* the low five bytes of v */
static inline uint32_t hash5(uint64_t v)
{
    return (uint32_t)(((v << 24) * 889523592379ull) >> (64 - LZP_HASH_LOG));
}

static inline uint8_t *put_len(uint8_t *o, uint32_t n)
{
    while (n >= 255)
    {
        *o++ = 255;
        n -= 255;
    }
    *o++ = (uint8_t)n;
    return o;
}

uint32_t lzp_compress(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t *table)
{
    uint8_t *o = out;
    const uint8_t *anchor = in;
    const uint8_t *const end = in + n;

    /* a match starts at most 12 bytes before the end and stops 5 before it
     * (the eight-byte reads stay inside the input) */
    if (n >= 16)
    {
        const uint8_t *const mflimit = end - 12, *const mlimit = end - 5;
        const uint8_t *ip = in + 1;
        uint8_t *tok;
        const uint8_t *ref;

        memset(table, 0, LZP_TABLE_BYTES);
        table[hash5(rd64(in))] = 0;
        for (;;)
        {
            /* the next match: the step grows past 64 misses in a row */
            {
                uint32_t step = 1 << 6;
                const uint8_t *fwd = ip;
                uint32_t h = hash5(rd64(ip));

                do
                {
                    ip = fwd;
                    fwd += step++ >> 6;
                    if (fwd > mflimit) goto last;
                    ref = in + table[h];
                    uint32_t hn = hash5(rd64(fwd));
                    table[h] = (uint32_t)(ip - in);
                    h = hn;
                } while (ip - ref > 65535 || ref == ip || rd32(ref) != rd32(ip));
            }
            while (ip > anchor && ref > in && ip[-1] == ref[-1])
            {
                --ip;
                --ref;
            }

            /* the literals, eight bytes at a time (out has the room) */
            uint32_t lit = (uint32_t)(ip - anchor);
            tok = o++;
            if (lit >= 15)
            {
                *tok = 15 << 4;
                o = put_len(o, lit - 15);
            }
            else *tok = (uint8_t)(lit << 4);
            {
                uint8_t *e = o + lit;
                const uint8_t *s = anchor;
                do
                {
                    cp8(o, s);
                    o += 8;
                    s += 8;
                } while (o < e);
                o = e;
            }

        match:;
            uint16_t off = (uint16_t)(ip - ref);
            *o++ = (uint8_t)off;
            *o++ = (uint8_t)(off >> 8);
            {
                const uint8_t *p = ip + LZP_MIN, *q = ref + LZP_MIN;
                while (p + 8 <= mlimit)
                {
                    uint64_t x = rd64(p) ^ rd64(q);
                    if (x)
                    {
                        p += __builtin_ctzll(x) >> 3;
                        goto counted;
                    }
                    p += 8;
                    q += 8;
                }
                while (p < mlimit && *p == *q)
                {
                    ++p;
                    ++q;
                }
            counted:;
                uint32_t ml = (uint32_t)(p - ip) - LZP_MIN;
                if (ml >= 15)
                {
                    *tok |= 15;
                    o = put_len(o, ml - 15);
                }
                else *tok |= (uint8_t)ml;
                ip = p;
            }
            anchor = ip;
            if (ip > mflimit) break;
            table[hash5(rd64(ip - 2))] = (uint32_t)(ip - 2 - in);

            /* a match right here: a sequence without literals */
            {
                uint32_t h = hash5(rd64(ip));
                ref = in + table[h];
                table[h] = (uint32_t)(ip - in);
                if (ref != ip && ip - ref <= 65535 && rd32(ref) == rd32(ip))
                {
                    tok = o++;
                    *tok = 0;
                    goto match;
                }
            }
            ++ip;
        }
    }

last:;
    uint32_t lit = (uint32_t)(end - anchor);
    if (lit >= 15)
    {
        *o++ = 15 << 4;
        o = put_len(o, lit - 15);
    }
    else *o++ = (uint8_t)(lit << 4);
    memcpy(o, anchor, lit);
    o += lit;
    return (uint32_t)(o - out);
}

int64_t lzp_decompress(const uint8_t *in, uint32_t zn, uint8_t *out, uint32_t cap)
{
    /* an overlapping match (offset below 8, LZ4's way): four bytes one by
     * one, four more from a point that makes the distance at least 8 */
    static const uint8_t inc[8] = {0, 1, 2, 1, 0, 4, 4, 4};
    static const int8_t dec[8] = {0, 0, 0, -1, -4, 1, 2, 3};
    const uint8_t *ip = in, *const iend = in + zn;
    uint8_t *o = out, *const oend = out + cap;

    for (;;)
    {
        if (ip >= iend) return -1;
        uint32_t t = *ip++;
        uint32_t lit = t >> 4, off, ml;
        const uint8_t *r;
        uint8_t *e;

        /* a short sequence far from both ends: fixed-size copies */
        if (lit < 15 && (t & 15) < 15 && iend - ip >= 32 && oend - o >= 32)
        {
            cp16(o, ip);
            o += lit;
            ip += lit;
            off = (uint32_t)ip[0] | (uint32_t)ip[1] << 8;
            ip += 2;
            ml = (t & 15) + LZP_MIN;
            if (off == 0 || (size_t)(o - out) < off) return -1;
            r = o - off;
            if (off >= 8)
            {
                /* ml is at most 18: 24 bytes stay inside the slack */
                cp8(o, r);
                cp8(o + 8, r + 8);
                cp8(o + 16, r + 16);
                o += ml;
                continue;
            }
            goto overlap;
        }

        if (lit == 15)
        {
            uint32_t b;
            do
            {
                if (ip >= iend) return -1;
                b = *ip++;
                lit += b;
            } while (b == 255);
        }
        if ((size_t)(iend - ip) < lit || (size_t)(oend - o) < lit) return -1;
        if (iend - ip >= (ptrdiff_t)lit + 16)
        {
            e = o + lit;
            const uint8_t *s = ip;
            do
            {
                cp16(o, s);
                o += 16;
                s += 16;
            } while (o < e);
            o = e;
        }
        else
        {
            memcpy(o, ip, lit);
            o += lit;
        }
        ip += lit;
        if (ip == iend) return o - out;

        if (iend - ip < 2) return -1;
        off = (uint32_t)ip[0] | (uint32_t)ip[1] << 8;
        ip += 2;
        ml = t & 15;
        if (ml == 15)
        {
            uint32_t b;
            do
            {
                if (ip >= iend) return -1;
                b = *ip++;
                ml += b;
            } while (b == 255);
        }
        ml += LZP_MIN;
        if (off == 0 || (size_t)(o - out) < off || (size_t)(oend - o) < ml) return -1;
        r = o - off;
        if (off < 8) goto overlap;
        e = o + ml;
        if (off >= 16)
            do
            {
                cp16(o, r);
                o += 16;
                r += 16;
            } while (o < e);
        else
            do
            {
                cp8(o, r);
                o += 8;
                r += 8;
            } while (o < e);
        o = e;
        continue;

    overlap:
        if ((size_t)(oend - o) < ml) return -1;
        e = o + ml;
        o[0] = r[0];
        o[1] = r[1];
        o[2] = r[2];
        o[3] = r[3];
        r += inc[off];
        memcpy(o + 4, r, 4);
        r -= dec[off];
        o += 8;
        while (o < e)
        {
            cp8(o, r);
            o += 8;
            r += 8;
        }
        o = e;
    }
}
