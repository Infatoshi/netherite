/* FontRenderer's text metrics; see font.h. */
#define _POSIX_C_SOURCE 200809L
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FontRenderer's glyph table, the string getCharWidth and renderStringAtPos
 * look characters up in: index i is the glyph at (i % 16, i / 16) of ascii.png. */
static const uint16_t GLYPHS[256] = {
    0x00c0, 0x00c1, 0x00c2, 0x00c8, 0x00ca, 0x00cb, 0x00cd, 0x00d3, 0x00d4, 0x00d5, 0x00da, 0x00df,
    0x00e3, 0x00f5, 0x011f, 0x0130, 0x0131, 0x0152, 0x0153, 0x015e, 0x015f, 0x0174, 0x0175, 0x017e,
    0x0207, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0020, 0x0021, 0x0022, 0x0023,
    0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b,
    0x003c, 0x003d, 0x003e, 0x003f, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f, 0x0050, 0x0051, 0x0052, 0x0053,
    0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b,
    0x006c, 0x006d, 0x006e, 0x006f, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x0000, 0x00c7, 0x00fc, 0x00e9, 0x00e2,
    0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00f8,
    0x00a3, 0x00d8, 0x00d7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x00ae, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502,
    0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566,
    0x2560, 0x2550, 0x256c, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x03b2, 0x0393, 0x03c0,
    0x03a3, 0x03c3, 0x03bc, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x2205, 0x2208, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a,
    0x207f, 0x00b2, 0x25a0, 0x0000,
};

static void *xalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "font: allocation failed\n"); exit(1); }
    return p;
}

void font_metrics_load(struct font_metrics *f, const unsigned char *rgba, int w, int h)
{
    int rows = h / 16, cols = w / 16;
    float scale = 8.0f / (float)cols;
    for (int c = 0; c < 256; ++c)
    {
        int cx = c % 16, cy = c / 16, col = cols - 1;
        for (; col >= 0; --col)
        {
            int empty = 1;
            for (int r = 0; r < rows && empty; ++r)
                if (rgba[((size_t)(cy * rows + r) * w + cx * cols + col) * 4 + 3] != 0) empty = 0;
            if (!empty) break;
        }
        ++col;
        f->cw[c] = (int)(0.5 + (double)((float)col * scale)) + 1;
    }
}

int font_char_index(uint32_t cp)
{
    for (int i = 0; i < 256; ++i)
        if (GLYPHS[i] == cp) return i;
    return -1;
}

int font_char_width(const struct font_metrics *f, uint32_t cp)
{
    if (cp == FONT_SECTION) return -1;
    if (cp == 32) return 4;
    int i = font_char_index(cp);
    if (cp > 0 && i >= 0) return f->cw[i];
    /* the unicode pages' glyph_sizes widths: no text this engine draws has
     * a character outside the table */
    return 0;
}

uint32_t *font_utf8_decode(const char *s, size_t *n)
{
    size_t len = s ? strlen(s) : 0, k = 0;
    uint32_t *out = xalloc((len + 1) * sizeof *out);
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p)
    {
        uint32_t c = *p++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        if (more) c &= 0x3f >> more;
        for (; more > 0 && (*p & 0xc0) == 0x80; --more) c = c << 6 | (*p++ & 0x3f);
        out[k++] = c;
    }
    out[k] = 0;
    *n = k;
    return out;
}

char *font_utf8_encode(const uint32_t *cp, size_t n)
{
    char *out = xalloc(n * 4 + 1), *o = out;
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t c = cp[i];
        if (c < 0x80) *o++ = (char)c;
        else if (c < 0x800) { *o++ = (char)(0xc0 | c >> 6); *o++ = (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000)
        {
            *o++ = (char)(0xe0 | c >> 12);
            *o++ = (char)(0x80 | (c >> 6 & 0x3f));
            *o++ = (char)(0x80 | (c & 0x3f));
        }
        else
        {
            *o++ = (char)(0xf0 | c >> 18);
            *o++ = (char)(0x80 | (c >> 12 & 0x3f));
            *o++ = (char)(0x80 | (c >> 6 & 0x3f));
            *o++ = (char)(0x80 | (c & 0x3f));
        }
    }
    *o = 0;
    return out;
}

static int cp_width(const struct font_metrics *f, const uint32_t *s, size_t n)
{
    int total = 0, bold = 0;
    for (size_t i = 0; i < n; ++i)
    {
        int w = font_char_width(f, s[i]);
        if (w < 0 && i < n - 1)
        {
            uint32_t c = s[++i];
            if (c == 'l' || c == 'L') bold = 1;
            else if (c == 'r' || c == 'R') bold = 0;
            w = 0;
        }
        total += w;
        if (bold && w > 0) ++total;
    }
    return total;
}

int font_string_width(const struct font_metrics *f, const char *s)
{
    if (!s) return 0;
    size_t n;
    uint32_t *cp = font_utf8_decode(s, &n);
    int w = cp_width(f, cp, n);
    free(cp);
    return w;
}

char *font_trim_to_width(const struct font_metrics *f, const char *s, int width)
{
    size_t n, k = 0;
    uint32_t *cp = font_utf8_decode(s, &n);
    uint32_t *out = xalloc((n + 1) * sizeof *out);
    int used = 0, code = 0, bold = 0;
    for (size_t i = 0; i < n && used < width; ++i)
    {
        uint32_t c = cp[i];
        int w = font_char_width(f, c);
        if (code)
        {
            code = 0;
            if (c == 'l' || c == 'L') bold = 1;
            else if (c == 'r' || c == 'R') bold = 0;
        }
        else if (w < 0) code = 1;
        else
        {
            used += w;
            if (bold) ++used;
        }
        if (used > width) break;
        out[k++] = c;
    }
    char *r = font_utf8_encode(out, k);
    free(cp);
    free(out);
    return r;
}

static int is_format_color(uint32_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_format_special(uint32_t c)
{
    return (c >= 'k' && c <= 'o') || (c >= 'K' && c <= 'O') || c == 'r' || c == 'R';
}

/* FontRenderer.sizeStringToWidth: where a line of at most width breaks. */
static size_t size_to_width(const struct font_metrics *f, const uint32_t *s, size_t n, int width)
{
    int used = 0, bold = 0;
    long space = -1;
    long i = 0;
    for (; i < (long)n; ++i)
    {
        uint32_t c = s[i];
        switch (c)
        {
        case 10:
            --i;
            break;
        case FONT_SECTION:
            if (i < (long)n - 1)
            {
                uint32_t d = s[++i];
                if (d == 'l' || d == 'L') bold = 1;
                else if (d == 'r' || d == 'R' || is_format_color(d)) bold = 0;
            }
            break;
        case 32:
            space = i;
            /* fall through */
        default:
            used += font_char_width(f, c);
            if (bold) ++used;
        }
        if (c == 10)
        {
            ++i;
            space = i;
            break;
        }
        if (used > width) break;
    }
    return i != (long)n && space != -1 && space < i ? (size_t)space : (size_t)i;
}

/* FontRenderer.getFormatFromString over s[0..n). */
static size_t format_from(const uint32_t *s, size_t n, uint32_t *out)
{
    size_t k = 0;
    for (size_t i = 0; i + 1 < n; ++i)
    {
        if (s[i] != FONT_SECTION) continue;
        uint32_t c = s[i + 1];
        if (is_format_color(c)) { k = 0; out[k++] = FONT_SECTION; out[k++] = c; }
        else if (is_format_special(c)) { out[k++] = FONT_SECTION; out[k++] = c; }
    }
    return k;
}

char **font_list_to_width(const struct font_metrics *f, const char *str, int width, int *count)
{
    /* wrapFormattedStringToWidth, iteratively: each break carries the
     * formatting so far onto the rest, then split("\n") */
    size_t n;
    uint32_t *s = font_utf8_decode(str, &n);
    int cap = 8, lines = 0;
    char **out = xalloc(cap * sizeof *out);
    for (;;)
    {
        size_t cut = size_to_width(f, s, n, width);
        if (lines + 2 > cap) { cap *= 2; out = realloc(out, cap * sizeof *out); if (!out) exit(1); }
        if (n <= cut) { out[lines++] = font_utf8_encode(s, n); break; }
        out[lines++] = font_utf8_encode(s, cut);
        int skip = s[cut] == 32 || s[cut] == 10;
        uint32_t *rest = xalloc((n * 2 + 2) * sizeof *rest);
        size_t k = format_from(s, cut, rest);
        memcpy(rest + k, s + cut + skip, (n - cut - skip) * sizeof *rest);
        size_t rn = k + n - cut - skip;
        free(s);
        s = rest;
        n = rn;
    }
    free(s);
    /* a line of the wrap can itself hold a newline sizeStringToWidth stopped
     * after; split those, then drop the trailing empty strings */
    int total = 0, tcap = lines + 4;
    char **all = xalloc(tcap * sizeof *all);
    for (int i = 0; i < lines; ++i)
    {
        char *p = out[i];
        for (;;)
        {
            char *nl = strchr(p, '\n');
            if (total + 2 > tcap) { tcap *= 2; all = realloc(all, tcap * sizeof *all); if (!all) exit(1); }
            if (!nl) { all[total++] = strdup(p); break; }
            all[total++] = strndup(p, (size_t)(nl - p));
            p = nl + 1;
        }
        free(out[i]);
    }
    free(out);
    while (total > 1 && all[total - 1][0] == 0) free(all[--total]);
    if (total == 1 && all[0][0] == 0 && strchr(str, '\n')) { free(all[0]); total = 0; }
    *count = total;
    return all;
}

void font_list_free(char **lines, int n)
{
    for (int i = 0; i < n; ++i) free(lines[i]);
    free(lines);
}
