/* FontRenderer's text metrics: the glyph table, the per-glyph widths read
 * from ascii.png, and the width, trim and wrap rules the HUD, the chat, the
 * screens and the credits lay text out with. Strings are UTF-8; every index
 * the Java code counts in chars is a code point here (all of the font's
 * glyphs are in the BMP). Drawing is raster_hud.c's. */
#ifndef NETHERITE_FONT_H
#define NETHERITE_FONT_H

#include <stddef.h>
#include <stdint.h>

#define FONT_SECTION 167 /* the formatting code prefix, U+00A7 */

struct font_metrics {
    int cw[256]; /* FontRenderer.charWidth */
};

/* FontRenderer.readFontTexture over the texture's RGBA pixels. */
void font_metrics_load(struct font_metrics *f, const unsigned char *rgba, int w, int h);

/* The glyph index of a code point in ascii.png's table, -1 when it has none. */
int font_char_index(uint32_t cp);

/* FontRenderer.getCharWidth: -1 for the section sign, 4 for a space, the
 * glyph's width, 0 for anything outside the table (the unicode pages). */
int font_char_width(const struct font_metrics *f, uint32_t cp);

/* FontRenderer.getStringWidth. */
int font_string_width(const struct font_metrics *f, const char *s);

/* FontRenderer.trimStringToWidth(s, width, false), malloc'd. */
char *font_trim_to_width(const struct font_metrics *f, const char *s, int width);

/* FontRenderer.listFormattedStringToWidth: the lines, malloc'd, *n of them
 * (String.split's rule: trailing empty lines are dropped). */
char **font_list_to_width(const struct font_metrics *f, const char *s, int width, int *n);
void font_list_free(char **lines, int n);

/* UTF-8 helpers: decode s into a malloc'd code point array (*n long), and
 * encode n code points back into a malloc'd string. */
uint32_t *font_utf8_decode(const char *s, size_t *n);
char *font_utf8_encode(const uint32_t *cp, size_t n);

#endif
