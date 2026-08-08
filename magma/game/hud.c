/* game/hud.c - Minecraft 1.11.2 survival HUD as a 2D overlay.
 *
 * Draws onto the FINISHED framebuffer, positioned like vanilla MC:
 *   - hotbar strip bottom-center (+ selection box around the active slot)
 *   - hearts above-left, hunger haunches above-right (mirrored)
 *   - green XP bar under the hotbar (+ tiny hardcoded digit level number)
 *   - crosshair at the exact center
 *
 * Sprites are the real MC gui textures (widgets.png / icons.png) extracted by
 * assets/build_hud_atlas.py into assets/hud_atlas.h. Blits are nearest-neighbour
 * integer-scaled and alpha-composited (skip a=0; textured src-over uses separate
 * round of src*a and dst*ia; solid fills keep fused (s*a+d*ia+127)/255).
 * gm_hud_draw NEVER touches fb->depth and clips every write to fb bounds.
 *
 * NOTES on stretch/optional items:
 *   - Item icons: block items are isometric mini-cubes (gm_item_draw_block_icon);
 *     flat 2D items (tools, coal, sticks) use gui_atlas 16x16 tiles; unknown
 *     ids keep a colored pip so occupancy is visible.
 *   - Crosshair: the plain white MC crosshair sprite blitted src-over (NOT the
 *     vanilla inverted-color blend), which reads clearly on any background.
 *   - XP level number: drawn with a hardcoded 3x5 pixel digit font.
 */
#include "game/hud.h"
#include "game/item_render.h"
#include "assets/hud_atlas.h"
#include "assets/gui_atlas.h"

#include <math.h>
#include <stdint.h>

_Static_assert(GM_GUI_INV_PANEL == GUI_INV_PANEL &&
               GM_GUI_TABLE_PANEL == GUI_TABLE_PANEL &&
               GM_GUI_FURNACE_PANEL == GUI_FURNACE_PANEL &&
               GM_GUI_FURNACE_FLAME == GUI_FURNACE_FLAME &&
               GM_GUI_FURNACE_ARROW == GUI_FURNACE_ARROW &&
               GM_GUI_FONT == GUI_FONT &&
               GM_GUI_CHEST_PANEL == GUI_CHEST_PANEL &&
               GM_GUI_BREWING_PANEL == GUI_BREWING_PANEL &&
               GM_GUI_BREWING_PROGRESS == GUI_BREWING_PROGRESS &&
               GM_GUI_BREWING_FUEL == GUI_BREWING_FUEL &&
               GM_GUI_BREWING_BUBBLES == GUI_BREWING_BUBBLES &&
               GM_GUI_ENCHANTING_PANEL == GUI_ENCHANTING_PANEL,
               "hud.h GM_GUI_* ids must match generated gui_atlas.h order");

/* ------------------------------------------------------------------ sprites */

static const HudSprite *hud_sprite(int idx) {
    if (idx < 0 || idx >= HUD_SPRITE_COUNT) return 0;
    return &HUD_SPRITES[idx];
}

/* ------------------------------------------------------------------ blitting */

/* One texel of sprite `idx` at local (sx,sy), as CrRgba. */
static CrRgba hud_texel(int idx, int sx, int sy) {
    const HudSprite *s = &HUD_SPRITES[idx];
    const unsigned char *p = &HUD_RGBA[s->off + (sy * s->w + sx) * 4];
    CrRgba c = { p[0], p[1], p[2], p[3] };
    return c;
}

/* Alpha-composite src over the framebuffer pixel at (x,y). Clips to bounds.
 * Fused (src*a + dst*ia + 127)/255, used for solid fills including the death
 * gradient, pips, and digit rects. Textured sprite blits use the separate-round
 * path below; inventory tooltip gradients use their scoped path in screen.c. */
static void hud_blend_px(CrFramebuffer *fb, int x, int y, CrRgba src) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    if (src.a == 0) return;
    CrRgba *d = &fb->color[y * fb->w + x];
    if (src.a == 255) { *d = src; return; }
    int a = src.a, ia = 255 - a;
    d->r = (u8)((src.r * a + d->r * ia + 127) / 255);
    d->g = (u8)((src.g * a + d->g * ia + 127) / 255);
    d->b = (u8)((src.b * a + d->b * ia + 127) / 255);
    d->a = (u8)(a + (d->a * ia + 127) / 255);
}

/* Textured GUI blit src-over. Separate round of src*a and dst*ia, then add
 * (clamp). Matches Java widgets/icons over the dark inside-block underlay:
 * fused (s*a+d*ia+127)/255 is systematically 1 LSB off on hotbar a=186
 * texels (stone/grass hard_px was HUD-band only after body bit-exact). Solid
 * fills keep hud_blend_px so death/durability numerical rows stay exact. */
static void hud_blend_px_tex(CrFramebuffer *fb, int x, int y, CrRgba src) {
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    if (src.a == 0) return;
    CrRgba *d = &fb->color[y * fb->w + x];
    if (src.a == 255) { *d = src; return; }
    int a = src.a, ia = 255 - a;
    int r = (src.r * a + 127) / 255 + (d->r * ia + 127) / 255;
    int g = (src.g * a + 127) / 255 + (d->g * ia + 127) / 255;
    int b = (src.b * a + 127) / 255 + (d->b * ia + 127) / 255;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    d->r = (u8)r;
    d->g = (u8)g;
    d->b = (u8)b;
    d->a = (u8)(a + (d->a * ia + 127) / 255);
}

/* Blit sprite `idx` at framebuffer top-left (dx,dy), integer-scaled by `scale`
 * (nearest-neighbour). Alpha-composited, clipped to fb bounds. */
static void hud_blit(CrFramebuffer *fb, int idx, int dx, int dy, int scale) {
    const HudSprite *s = hud_sprite(idx);
    if (!s || scale < 1) return;
    for (int sy = 0; sy < s->h; sy++) {
        for (int sx = 0; sx < s->w; sx++) {
            CrRgba t = hud_texel(idx, sx, sy);
            if (t.a == 0) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++)
                    hud_blend_px_tex(fb, px0 + xx, py0 + yy, t);
        }
    }
}

static void hud_blit_sub_alpha(CrFramebuffer *fb, int idx, int sx0, int sy0,
                               int sw, int sh, int dx, int dy, int scale,
                               float alpha) {
    const HudSprite *s = hud_sprite(idx);
    if (!fb || !fb->color || !s || scale < 1 || sw <= 0 || sh <= 0) return;
    if (sx0 < 0 || sy0 < 0 || sx0 + sw > s->w || sy0 + sh > s->h) return;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    for (int sy = 0; sy < sh; ++sy)
        for (int sx = 0; sx < sw; ++sx) {
            CrRgba t = hud_texel(idx, sx0 + sx, sy0 + sy);
            t.a = (uint8_t)((float)t.a * alpha);
            if (!t.a) continue;
            for (int yy = 0; yy < scale; ++yy)
                for (int xx = 0; xx < scale; ++xx)
                    hud_blend_px_tex(fb, dx + sx * scale + xx,
                                     dy + sy * scale + yy, t);
        }
}

/* Fill a solid scaled rect (used for hotbar item pips). */
/* hud_blit clipped to the first `rows` sprite rows (vanilla partial blits). */
static void hud_blit_rows(CrFramebuffer *fb, int idx, int dx, int dy, int scale,
                          int rows) {
    const HudSprite *s = hud_sprite(idx);
    if (!s || scale < 1) return;
    int h = rows < s->h ? rows : s->h;
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < s->w; sx++) {
            CrRgba t = hud_texel(idx, sx, sy);
            if (t.a == 0) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++)
                    hud_blend_px_tex(fb, px0 + xx, py0 + yy, t);
        }
    }
}

/* hud_blit clipped to the first `cols` sprite columns (boss bar progress:
 * vanilla blits (int)(percent * 183) px of the 182px FULL strip). */
static void hud_blit_cols(CrFramebuffer *fb, int idx, int dx, int dy, int scale,
                          int cols) {
    const HudSprite *s = hud_sprite(idx);
    if (!s || scale < 1) return;
    int w = cols < s->w ? cols : s->w;
    for (int sy = 0; sy < s->h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            CrRgba t = hud_texel(idx, sx, sy);
            if (t.a == 0) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++)
                    hud_blend_px_tex(fb, px0 + xx, py0 + yy, t);
        }
    }
}

/* GL blendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR) + alpha test:
 * out = src*(1-dst) + dst*(1-src) per channel; texels with a<26 (GL_GREATER
 * 0.1) discard. This is how vanilla draws the crosshair. */
static void hud_blit_invert(CrFramebuffer *fb, int idx, int dx, int dy, int scale) {
    const HudSprite *s = hud_sprite(idx);
    if (!s || scale < 1) return;
    for (int sy = 0; sy < s->h; sy++) {
        for (int sx = 0; sx < s->w; sx++) {
            CrRgba t = hud_texel(idx, sx, sy);
            if (t.a < 26) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++) {
                    int x = px0 + xx, y = py0 + yy;
                    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) continue;
                    CrRgba *d = &fb->color[y * fb->w + x];
                    int r = (t.r * (255 - d->r) + d->r * (255 - t.r) + 127) / 255;
                    int g = (t.g * (255 - d->g) + d->g * (255 - t.g) + 127) / 255;
                    int b = (t.b * (255 - d->b) + d->b * (255 - t.b) + 127) / 255;
                    d->r = (u8)r; d->g = (u8)g; d->b = (u8)b;
                }
        }
    }
}

static void hud_fill(CrFramebuffer *fb, int dx, int dy, int w, int h, CrRgba c) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            hud_blend_px(fb, dx + x, dy + y, c);
}

/* ------------------------------------------------------------- tiny 3x5 font */
/* Digits 0-9, 3 wide x 5 tall, one bit per pixel, MSB = leftmost, top row first. */
static const uint8_t HUD_DIGITS[10][5] = {
    {0x7,0x5,0x5,0x5,0x7}, /* 0 */
    {0x2,0x6,0x2,0x2,0x7}, /* 1 */
    {0x7,0x1,0x7,0x4,0x7}, /* 2 */
    {0x7,0x1,0x7,0x1,0x7}, /* 3 */
    {0x5,0x5,0x7,0x1,0x1}, /* 4 */
    {0x7,0x4,0x7,0x1,0x7}, /* 5 */
    {0x7,0x4,0x7,0x5,0x7}, /* 6 */
    {0x7,0x1,0x1,0x2,0x2}, /* 7 */
    {0x7,0x5,0x7,0x5,0x7}, /* 8 */
    {0x7,0x5,0x7,0x1,0x7}, /* 9 */
};

static void hud_digit(CrFramebuffer *fb, int d, int dx, int dy, int scale, CrRgba c) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (HUD_DIGITS[d][row] & (1 << (2 - col)))
                hud_fill(fb, dx + col * scale, dy + row * scale, scale, scale, c);
}

/* Draw a non-negative integer left-aligned at (dx,dy). Returns width in px. */
static int hud_number(CrFramebuffer *fb, int n, int dx, int dy, int scale, CrRgba c) {
    int digits[10], nd = 0;
    if (n <= 0) { digits[nd++] = 0; }
    else { while (n > 0 && nd < 10) { digits[nd++] = n % 10; n /= 10; } }
    int x = dx;
    for (int i = nd - 1; i >= 0; i--) {
        hud_digit(fb, digits[i], x, dy, scale, c);
        x += 4 * scale; /* 3px glyph + 1px gap */
    }
    return x - dx;
}

/* ------------------------------------------------------------------- pip hue */
/* Deterministic distinct-ish color from a block id (for the item-slot markers). */
static CrRgba hud_pip_color(int id) {
    uint32_t h = (uint32_t)id * 2654435761u;
    CrRgba c;
    c.r = (u8)(80 + (h & 0x7F));
    c.g = (u8)(80 + ((h >> 8) & 0x7F));
    c.b = (u8)(80 + ((h >> 16) & 0x7F));
    c.a = 255;
    return c;
}

/* Public wrappers shared with the container screen (see hud.h). */
void gm_hud_fill(CrFramebuffer *fb, int dx, int dy, int w, int h, CrRgba c) {
    hud_fill(fb, dx, dy, w, h, c);
}
int gm_hud_number(CrFramebuffer *fb, int n, int dx, int dy, int scale, CrRgba c) {
    return hud_number(fb, n, dx, dy, scale, c);
}
CrRgba gm_hud_pip_color(int id) { return hud_pip_color(id); }

/* ------------------------------------------------------- container GUI art */
/* Sprites live in assets/gui_atlas.h (real MC panels, ascii.png font sheet,
 * flat 16x16 item icons). Same nearest-neighbour alpha blit as the HUD. */

static void gui_blit_sub(CrFramebuffer *fb, int idx, int sx0, int sy0, int sw, int sh,
                         int dx, int dy, int scale) {
    if (idx < 0 || idx >= GUI_SPRITE_COUNT || scale < 1) return;
    const GuiSprite *s = &GUI_SPRITES[idx];
    if (sx0 < 0) sx0 = 0;
    if (sy0 < 0) sy0 = 0;
    if (sx0 + sw > s->w) sw = s->w - sx0;
    if (sy0 + sh > s->h) sh = s->h - sy0;
    for (int sy = 0; sy < sh; sy++) {
        for (int sx = 0; sx < sw; sx++) {
            const unsigned char *p =
                &GUI_RGBA[s->off + ((sy0 + sy) * s->w + (sx0 + sx)) * 4];
            CrRgba t = { p[0], p[1], p[2], p[3] };
            if (t.a == 0) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; yy++)
                for (int xx = 0; xx < scale; xx++)
                    hud_blend_px_tex(fb, px0 + xx, py0 + yy, t);
        }
    }
}

void gm_gui_blit(CrFramebuffer *fb, int idx, int dx, int dy, int scale) {
    if (idx < 0 || idx >= GUI_SPRITE_COUNT) return;
    gui_blit_sub(fb, idx, 0, 0, GUI_SPRITES[idx].w, GUI_SPRITES[idx].h, dx, dy, scale);
}

void gm_gui_blit_sub(CrFramebuffer *fb, int idx, int sx, int sy, int sw, int sh,
                     int dx, int dy, int scale) {
    gui_blit_sub(fb, idx, sx, sy, sw, sh, dx, dy, scale);
}

int gm_gui_item_icon(CrFramebuffer *fb, int item_id, int item_meta,
                     int dx, int dy, int scale) {
    /* Block items: vanilla GUI isometric mini-cube (terrain faces or single-
     * texture stand-in). Flat 2D items fall through to the gui_atlas tiles. */
    if (gm_item_draw_block_icon(fb, item_id, item_meta, dx, dy, scale)) return 1;
    int lo = 0, hi = GUI_ITEM_ICON_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (GUI_ITEM_ICONS[mid].item == item_id) {
            if (fb) gm_gui_blit(fb, GUI_ITEM_ICONS[mid].sprite, dx, dy, scale);
            return 1;
        }
        if (GUI_ITEM_ICONS[mid].item < item_id) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* Item.getMaxDamage for damageable items (ToolMaterial + swords/armor/etc). */
static int hud_item_max_damage(int item_id) {
    if (item_id >= 256 && item_id <= 258) return 250; /* iron shovel/pick/axe */
    if (item_id == 259) return 64;                    /* flint and steel */
    if (item_id == 261) return 384;                   /* bow */
    if (item_id == 267) return 250;                   /* iron sword */
    if (item_id >= 268 && item_id <= 271) return 59;  /* wood sword+tools */
    if (item_id >= 272 && item_id <= 275) return 131; /* stone sword+tools */
    if (item_id >= 276 && item_id <= 279) return 1561;/* diamond sword+tools */
    if (item_id >= 283 && item_id <= 286) return 32;  /* gold sword+tools */
    if (item_id == 290) return 59;                    /* wood hoe */
    if (item_id == 291) return 131;                   /* stone hoe */
    if (item_id == 292) return 250;                   /* iron hoe */
    if (item_id == 293) return 1561;                  /* diamond hoe */
    if (item_id == 294) return 32;                    /* gold hoe */
    if (item_id == 298) return 55;                    /* leather helmet */
    if (item_id == 299) return 80;                    /* leather chest */
    if (item_id == 300) return 75;                    /* leather legs */
    if (item_id == 301) return 65;                    /* leather boots */
    if (item_id == 302) return 165;                   /* chain helmet */
    if (item_id == 303) return 240;                   /* chain chest */
    if (item_id == 304) return 225;                   /* chain legs */
    if (item_id == 305) return 195;                   /* chain boots */
    if (item_id == 306) return 165;                   /* iron helmet */
    if (item_id == 307) return 240;                   /* iron chest */
    if (item_id == 308) return 225;                   /* iron legs */
    if (item_id == 309) return 195;                   /* iron boots */
    if (item_id == 310) return 363;                   /* diamond helmet */
    if (item_id == 311) return 528;                   /* diamond chest */
    if (item_id == 312) return 495;                   /* diamond legs */
    if (item_id == 313) return 429;                   /* diamond boots */
    if (item_id == 314) return 77;                    /* gold helmet */
    if (item_id == 315) return 112;                   /* gold chest */
    if (item_id == 316) return 105;                   /* gold legs */
    if (item_id == 317) return 91;                    /* gold boots */
    if (item_id == 346) return 64;                    /* fishing rod */
    if (item_id == 359) return 238;                   /* shears */
    if (item_id == 398) return 25;                    /* carrot on a stick */
    if (item_id == 442) return 336;                   /* shield */
    if (item_id == 443) return 432;                   /* elytra */
    return 0;
}

/* MathHelper.hsvToRGB(hue, 1, 1) used by Item.getRGBDurabilityForDisplay.
 * hue = max(0, 1 - damage/maxDamage) / 3. Channels: clamp((int)(f*255),0,255). */
static CrRgba hud_durability_color(int damage, int max_damage) {
    float dur = (float)((double)damage / (double)max_damage);
    float hue = (1.0f - dur);
    if (hue < 0.0f) hue = 0.0f;
    hue /= 3.0f;
    int i = (int)(hue * 6.0f) % 6;
    float f = hue * 6.0f - (float)i;
    float f1 = 0.0f;                         /* value*(1-sat) with V=S=1 */
    float f2 = 1.0f - f;                     /* value*(1-f*sat) */
    float f3 = f;                            /* value*(1-(1-f)*sat) */
    float r, g, b;
    switch (i) {
        case 0: r = 1.0f; g = f3;   b = f1; break;
        case 1: r = f2;   g = 1.0f; b = f1; break;
        case 2: r = f1;   g = 1.0f; b = f3; break;
        case 3: r = f1;   g = f2;   b = 1.0f; break;
        case 4: r = f3;   g = f1;   b = 1.0f; break;
        default: r = 1.0f; g = f1;  b = f2; break;
    }
    int ri = (int)(r * 255.0f); if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    int gi = (int)(g * 255.0f); if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    int bi = (int)(b * 255.0f); if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    return (CrRgba){(u8)ri, (u8)gi, (u8)bi, 255};
}

/* Vanilla GuiIngame.renderExpBar: fill = (int)(experience * 183.0F). */
int gm_hud_xp_fill_cols(float frac) {
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    int cols = (int)(frac * 183.0f);
    if (cols > 182) cols = 182;
    return cols;
}

int gm_hud_durability_width(int item_id, int damage) {
    /* RenderItem: i = Math.round(13.0F - (float)health * 13.0F) with
     * health = getDurabilityForDisplay = damage/maxDamage. */
    int max_damage = hud_item_max_damage(item_id);
    if (max_damage <= 0 || damage <= 0) return 0;
    float health = (float)((double)damage / (double)max_damage);
    int width = (int)floorf(13.0f - health * 13.0f + 0.5f);
    if (width < 0) width = 0;
    if (width > 13) width = 13;
    return width;
}

void gm_hud_durability_rgb(int item_id, int damage, unsigned char *r,
                           unsigned char *g, unsigned char *b) {
    int max_damage = hud_item_max_damage(item_id);
    CrRgba c = {0, 0, 0, 255};
    if (max_damage > 0 && damage > 0)
        c = hud_durability_color(damage, max_damage);
    if (r) *r = c.r;
    if (g) *g = c.g;
    if (b) *b = c.b;
}

/* RenderItem.renderItemOverlayIntoGUI durability strip: 13x2 black backing,
 * one-pixel colored fill at x+2,y+13 in 16x16 item coordinates. */
static void hud_item_durability(CrFramebuffer *fb, int item_id, int damage,
                                int x, int y, int scale) {
    int max_damage = hud_item_max_damage(item_id);
    if (max_damage <= 0 || damage <= 0) return;
    int width = gm_hud_durability_width(item_id, damage);
    hud_fill(fb, x + 2 * scale, y + 13 * scale,
             13 * scale, 2 * scale, (CrRgba){0, 0, 0, 255});
    if (width > 0)
        hud_fill(fb, x + 2 * scale, y + 13 * scale,
                 width * scale, scale,
                 hud_durability_color(damage, max_damage));
}

/* ------------------------------------------------------------------ MC font */
/* PORT: FontRenderer.readFontTexture / renderDefaultChar (ascii.png only).
 * charWidth[c]: 4 for space, else (rightmost non-empty column of the 8x8
 * cell + 1) + 1. A glyph draws charWidth-1 columns and advances charWidth. */

static uint8_t g_font_width[256];
static int g_font_ready = 0;

static void font_init(void) {
    const GuiSprite *fs = &GUI_SPRITES[GUI_FONT];
    for (int c = 0; c < 256; c++) {
        if (c == 32) { g_font_width[c] = 4; continue; }
        int cx = (c % 16) * 8, cy = (c / 16) * 8;
        int col = 7;
        for (; col >= 0; --col) {
            int hit = 0;
            for (int row = 0; row < 8 && !hit; row++)
                if (GUI_RGBA[fs->off + ((cy + row) * fs->w + cx + col) * 4 + 3] != 0)
                    hit = 1;
            if (hit) break;
        }
        g_font_width[c] = (uint8_t)(col + 2);
    }
    g_font_ready = 1;
}

int gm_font_width(const char *s) {
    if (!g_font_ready) font_init();
    int w = 0;
    for (; s && *s; s++) w += g_font_width[(unsigned char)*s];
    return w;
}

static void font_draw_pass(CrFramebuffer *fb, const char *s, int dx, int dy,
                           int scale, unsigned rgb, int alpha) {
    const GuiSprite *fs = &GUI_SPRITES[GUI_FONT];
    CrRgba c = { (u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, (u8)alpha };
    int x = dx;
    for (; s && *s; s++) {
        int ch = (unsigned char)*s;
        int w = g_font_width[ch];
        int cx = (ch % 16) * 8, cy = (ch / 16) * 8;
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < w - 1 && col < 8; col++) {
                if (GUI_RGBA[fs->off + ((cy + row) * fs->w + cx + col) * 4 + 3] == 0)
                    continue;
                for (int yy = 0; yy < scale; yy++)
                    for (int xx = 0; xx < scale; xx++)
                        hud_blend_px(fb, x + col * scale + xx,
                                     dy + row * scale + yy, c);
            }
        x += w * scale;
    }
}

void gm_font_draw(CrFramebuffer *fb, const char *s, int dx, int dy, int scale,
                  unsigned rgb, int shadow) {
    if (!fb || !fb->color || scale < 1) return;
    if (!g_font_ready) font_init();
    if (shadow)
        font_draw_pass(fb, s, dx + scale, dy + scale, scale,
                       (rgb & 0xFCFCFCu) >> 2, 255);
    font_draw_pass(fb, s, dx, dy, scale, rgb, 255);
}

static void hud_draw_mount_message(CrFramebuffer *fb,
                                   const GmPlayerView *pv, int scale,
                                   int sw_s, int sh_s) {
    static const char text[] = "Press LSHIFT to dismount";
    if (!pv->mount_message_ticks) return;
    float remaining = (float)pv->mount_message_ticks - 1.0f;
    int alpha = (int)(remaining * 255.0f / 20.0f);
    if (alpha > 255) alpha = 255;
    if (alpha <= 8) return;
    font_draw_pass(fb, text,
                   ((sw_s - gm_font_width(text)) / 2) * scale,
                   (sh_s - 72) * scale, scale, 0xFFFFFFu, alpha);
}

/* --------------------------------------------------------------------- init */

static int g_hud_ready = 0;

int gm_hud_init(void) {
    /* Sprites are compiled-in (assets/hud_atlas.h). Sanity-check the table. */
    if (HUD_SPRITE_COUNT < 11) return 1;
    for (int i = 0; i < HUD_SPRITE_COUNT; i++) {
        const HudSprite *s = &HUD_SPRITES[i];
        if (s->w <= 0 || s->h <= 0 || s->off < 0) return 2;
    }
    g_hud_ready = 1;
    return 0;
}

/* --------------------------------------------------------------------- draw */

static void hud_draw_crosshair(CrFramebuffer *fb) {
    if (!fb || !fb->color || !g_hud_ready) return;
    const int scale = (fb->h / 240) > 1 ? (fb->h / 240) : 1;
    /* vanilla: (scaledWidth/2 - 7, scaledHeight/2 - 7) in ScaledResolution
     * coords (integer center of the CEILED scaled size), then scaled up. */
    const int sw_s = (fb->w + scale - 1) / scale;
    const int sh_s = (fb->h + scale - 1) / scale;
    hud_blit_invert(fb, HUD_CROSSHAIR,
                    (sw_s / 2 - 7) * scale, (sh_s / 2 - 7) * scale, scale);
}

static int potion_active(const GmPlayerView *pv, int id) {
    for (int i = 0; i < pv->potion_count; ++i)
        if (pv->potions[i].id == id) return 1;
    return 0;
}

static int potion_icon_index(int id) {
    static const signed char icons[28] = {
        -1, 0, 1, 2, 3, 4, -1, -1, 10, 11, 7, 14, 15, 16,
        8, 13, 12, 9, 5, 6, 17, 23, 18, -1, 20, 19, 21, 22
    };
    return id >= 0 && id < (int)(sizeof icons / sizeof icons[0])
        ? icons[id] : -1;
}

static int potion_beneficial(int id) {
    switch (id) {
        case 1: case 3: case 5: case 6: case 7: case 8: case 10:
        case 11: case 12: case 13: case 14: case 16: case 21: case 22:
        case 23: case 26: return 1;
        default: return 0;
    }
}

/* GuiIngame.renderPotionEffects: `if (potioneffect.doesShowParticles())` gates
 * the whole icon, so a hidden-particle effect (`/effect ... <amp> true`, e.g.
 * the dragon_kill scenario's Resistance V) draws NOTHING even though the
 * effect is active. Magma drew a phantom shield icon top-right for exactly
 * that reason. hide_particles is set only by a tape row that carries the
 * recorded flag; everything else keeps vanilla's shown default. */
static void hud_draw_potion_effects(CrFramebuffer *fb,
                                    const GmPlayerView *pv, int scale,
                                    int sw_s) {
    int good = 0, bad = 0;
    int order[GM_MAX_POTION_EFFECTS];
    for (int n = 0; n < pv->potion_count; ++n) {
        int at = n;
        while (at > 0 &&
               pv->potions[order[at - 1]].duration < pv->potions[n].duration) {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = n;
    }
    for (int n = 0; n < pv->potion_count; ++n) {
        const GmPotionEffectView *p = &pv->potions[order[n]];
        int icon = potion_icon_index(p->id);
        if (icon < 0 || p->hide_particles) continue;
        int x = sw_s - 25 * (potion_beneficial(p->id) ? ++good : ++bad);
        int y = potion_beneficial(p->id) ? 1 : 27;
        hud_blit_sub_alpha(fb, HUD_POTION_BG, 0, 0, 24, 24,
                           x * scale, y * scale, scale, 1.0f);
        float alpha = 1.0f;
        if (p->duration <= 200) {
            int j = 10 - p->duration / 20;
            float pulse = (float)p->duration / 50.0f * 0.5f;
            if (pulse < 0.0f) pulse = 0.0f;
            if (pulse > 0.5f) pulse = 0.5f;
            float amp = (float)j / 10.0f * 0.25f;
            if (amp < 0.0f) amp = 0.0f;
            if (amp > 0.25f) amp = 0.25f;
            alpha = pulse + cosf((float)p->duration * 3.14159265358979323846f / 5.0f) * amp;
        }
        hud_blit_sub_alpha(fb, HUD_POTION_ICONS,
                           icon % 8 * 18, icon / 8 * 18, 18, 18,
                           (x + 3) * scale, (y + 3) * scale, scale, alpha);
    }
}

/* GuiBossOverlay state: the frame assembler flags a boss (ender dragon view
 * present this frame) and its health fraction before gm_hud_draw. */
static int   g_boss_show = 0;
static float g_boss_frac = 1.0f;
static const char *g_boss_name = "Ender Dragon";

void gm_hud_set_boss(int show, float frac) {
    g_boss_show = show;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    g_boss_frac = frac;
}

void gm_hud_state_step(GmHudState *s, GmPlayerView *pv,
                       long long update_counter) {
    if (!s || !pv) return;
    int health = (int)ceilf(pv->health);
    if (health < 0) health = 0;
    if (!s->initialized) {
        s->initialized = 1;
        s->player_health = health;
        s->last_player_health = health;
        s->last_sync_counter = update_counter;
    }

    /* GuiIngame.renderPlayerStats computes the blink from the previous
     * healthUpdateCounter, then refreshes the counter on a health transition. */
    pv->hud_flash = s->health_update_counter > update_counter &&
        ((s->health_update_counter - update_counter) / 3LL) % 2LL == 1LL;
    long long transition_counter = update_counter - pv->hud_transition_lead;
    int hurt_resistant = pv->hurt_time > 0 ||
        (pv->hud_transition_lead && s->previous_hurt_time > 0);
    if (health < s->player_health && hurt_resistant) {
        s->last_sync_counter = transition_counter;
        s->health_update_counter = transition_counter + 20;
    } else if (health > s->player_health && hurt_resistant) {
        s->last_sync_counter = transition_counter;
        s->health_update_counter = transition_counter + 10;
    }
    /* Vanilla uses a 1000 ms wall-clock guard. Tape replay is a fixed 20 Hz
     * client tick stream, so strictly-more-than-one-second is 21 ticks. */
    if (update_counter - s->last_sync_counter > 20) {
        s->player_health = health;
        s->last_player_health = health;
        s->last_sync_counter = update_counter;
    }
    s->player_health = health;
    pv->hud_health = health;
    pv->hud_last_health = s->last_player_health;
    pv->hud_state_valid = 1;
    s->previous_hurt_time = pv->hurt_time;
}

/* Pointer for GuiGameOver hover (framebuffer pixels). */
static int g_ptr_x = -1, g_ptr_y = -1;

void gm_hud_set_pointer(int mx, int my) {
    g_ptr_x = mx;
    g_ptr_y = my;
}

int gm_hud_death_buttons_enabled(int death_ticks) {
    return death_ticks >= 20;
}

void gm_hud_death_layout(int fb_w, int fb_h,
                         int *btn0_x, int *btn0_y, int *btn1_x, int *btn1_y,
                         int *btn_w, int *btn_h) {
    const int scale = (fb_h / 240) > 1 ? (fb_h / 240) : 1;
    const int sw_s = (fb_w + scale - 1) / scale;
    const int sh_s = (fb_h + scale - 1) / scale;
    /* GuiGameOver.initGui: width/2-100, height/4+72 / +96, 200x20 */
    int x = (sw_s / 2 - 100) * scale;
    int y0 = (sh_s / 4 + 72) * scale;
    int y1 = (sh_s / 4 + 96) * scale;
    int w = 200 * scale, h = 20 * scale;
    if (btn0_x) *btn0_x = x;
    if (btn0_y) *btn0_y = y0;
    if (btn1_x) *btn1_x = x;
    if (btn1_y) *btn1_y = y1;
    if (btn_w) *btn_w = w;
    if (btn_h) *btn_h = h;
}

int gm_hud_death_button_at(int fb_w, int fb_h, int mx, int my,
                           int buttons_enabled) {
    if (!buttons_enabled) return -1;
    int x0, y0, x1, y1, w, h;
    gm_hud_death_layout(fb_w, fb_h, &x0, &y0, &x1, &y1, &w, &h);
    if (mx >= x0 && my >= y0 && mx < x0 + w && my < y0 + h) return 0;
    if (mx >= x1 && my >= y1 && mx < x1 + w && my < y1 + h) return 1;
    return -1;
}

/* Gui.drawGradientRect over full frame: top 0x60500000, bottom 0xA0803030. */
static void hud_death_gradient(CrFramebuffer *fb, int scale) {
    const int sw = (fb->w + scale - 1) / scale;
    const int sh = (fb->h + scale - 1) / scale;
    const unsigned top = 0x60500000u;
    const unsigned bot = 0xA0803030u;
    int fy0 = 0, fy1 = sh * scale;
    int h = fy1 - fy0;
    if (h < 1) h = 1;
    for (int y = 0; y < h; ++y) {
        int den = h > 1 ? h - 1 : 1;
        int ta = (top >> 24) & 255, tr = (top >> 16) & 255;
        int tg = (top >> 8) & 255, tb = top & 255;
        int ba = (bot >> 24) & 255, br = (bot >> 16) & 255;
        int bg = (bot >> 8) & 255, bb = bot & 255;
        CrRgba c = {
            (u8)((tr * (den - y) + br * y + den / 2) / den),
            (u8)((tg * (den - y) + bg * y + den / 2) / den),
            (u8)((tb * (den - y) + bb * y + den / 2) / den),
            (u8)((ta * (den - y) + ba * y + den / 2) / den)
        };
        hud_fill(fb, 0, fy0 + y, sw * scale, 1, c);
    }
}

/* GuiButton.drawButton: full 200-wide strip (left+right halves of widgets.png). */
static void hud_draw_button(CrFramebuffer *fb, int idx, int dx, int dy, int scale) {
    hud_blit(fb, idx, dx, dy, scale);
}

static void hud_death_draw(CrFramebuffer *fb, const GmPlayerView *pv, int scale) {
    const int sw_s = (fb->w + scale - 1) / scale;
    const int sh_s = (fb->h + scale - 1) / scale;
    const int cx_s = sw_s / 2;

    /* Background tint (GuiGameOver.drawScreen drawGradientRect). */
    hud_death_gradient(fb, scale);

    /* Title: GlStateManager.scale(2,2); drawCenteredString(..., width/2/2, 30).
     * Integer path: x_pre = (sw/2)/2 - tw/2; after *2 matrix gui_x = 2*x_pre
     * (NOT sw/2 - tw — that is 1 GUI unit / 2 fb px too far right at sw=427).
     * Glyph scale is 2*guiScale; baseline GUI y = 2*30 = 60. */
    {
        const char *title = "You died!";
        int ts = scale * 2;
        int tw = gm_font_width(title);
        int x_pre = (sw_s / 2) / 2 - tw / 2;
        int tx = (2 * x_pre) * scale;
        int ty = 60 * scale;
        gm_font_draw(fb, title, tx, ty, ts, 0xFFFFFFu, 1);
    }

    /* Score line at GUI y=100: "Score: " white + yellow number (TextFormatting.YELLOW). */
    {
        char num[16];
        int score = pv->score;
        if (score < 0) score = 0;
        {
            int n = score, len = 0, tmp = n;
            if (tmp == 0) { num[0] = '0'; len = 1; }
            else {
                while (tmp > 0 && len < 15) { len++; tmp /= 10; }
                num[len] = 0;
                for (int d = len - 1; d >= 0; --d) {
                    num[d] = (char)('0' + n % 10);
                    n /= 10;
                }
            }
            num[len] = 0;
        }
        const char *prefix = "Score: ";
        int pw = gm_font_width(prefix);
        int nw = gm_font_width(num);
        int total = pw + nw;
        int sx = (cx_s - total / 2) * scale;
        int sy = 100 * scale;
        gm_font_draw(fb, prefix, sx, sy, scale, 0xFFFFFFu, 1);
        gm_font_draw(fb, num, sx + pw * scale, sy, scale, 0xFFFF55u, 1);
    }

    /* Buttons: disabled until enableButtonsTimer >= 20. */
    {
        int bx, by0, by1, bw, bh;
        gm_hud_death_layout(fb->w, fb->h, &bx, &by0, &bx, &by1, &bw, &bh);
        (void)bw; (void)bh;
        int enabled = gm_hud_death_buttons_enabled(pv->death_ticks);
        int gmx = g_ptr_x >= 0 ? g_ptr_x / scale : -9999;
        int gmy = g_ptr_y >= 0 ? g_ptr_y / scale : -9999;
        int bx_g = sw_s / 2 - 100;
        int by0_g = sh_s / 4 + 72;
        int by1_g = sh_s / 4 + 96;
        const char *labels[2] = { "Respawn", "Title screen" };
        int ys[2] = { by0, by1 };
        int ysg[2] = { by0_g, by1_g };
        for (int i = 0; i < 2; ++i) {
            int hovered = enabled &&
                gmx >= bx_g && gmy >= ysg[i] &&
                gmx < bx_g + 200 && gmy < ysg[i] + 20;
            int spr = !enabled ? HUD_BUTTON_DISABLED
                    : (hovered ? HUD_BUTTON_HOVER : HUD_BUTTON_ENABLED);
            hud_draw_button(fb, spr, bx, ys[i], scale);
            unsigned col = !enabled ? 0xA0A0A0u
                         : (hovered ? 0xFFFFA0u : 0xE0E0E0u);
            int lw = gm_font_width(labels[i]);
            int lx = (bx_g + 100 - lw / 2) * scale;
            int ly = (ysg[i] + (20 - 8) / 2) * scale;
            gm_font_draw(fb, labels[i], lx, ly, scale, col, 1);
        }
    }
}

void gm_hud_draw(CrFramebuffer *fb, const GmPlayerView *pv) {
    if (!fb || !fb->color || !pv || !g_hud_ready) return;

    const int scale = (fb->h / 240) > 1 ? (fb->h / 240) : 1; /* MC-like gui scale */

    /* ---- GuiGameOver (1.11.2): replaces the survival HUD while dead. ---- */
    if (pv->dead) {
        hud_death_draw(fb, pv, scale);
        return;
    }

    /* Vanilla lays the HUD out in ScaledResolution coords (scaledWidth =
     * ceil(w/scale)) and the GL scale maps them back: with an odd scaled
     * width the center (scaledWidth/2, integer) is NOT fb->w/2 - anchoring
     * in screen pixels shifted the whole HUD 1px right. Compute anchors in
     * scaled coords, multiply at the end. */
    const int sw_s = (fb->w + scale - 1) / scale;   /* scaledWidth */
    const int sh_s = (fb->h + scale - 1) / scale;   /* scaledHeight */
    const int cx_s = sw_s / 2;

    /* ---- boss bar (GuiBossOverlay): PINK strip at (cx-91, 12), name with
     * shadow centered at y-9; progress width = (int)(percent * 183). ---- */
    if (g_boss_show) {
        const int bb_x = (cx_s - 91) * scale, bb_y = 12 * scale;
        hud_blit(fb, HUD_BOSS_PINK_BG, bb_x, bb_y, scale);
        int cols = (int)(g_boss_frac * 183.0f);
        if (cols > 0)
            hud_blit_cols(fb, HUD_BOSS_PINK_FULL, bb_x, bb_y, scale, cols);
        gm_font_draw(fb, g_boss_name,
                     (cx_s - gm_font_width(g_boss_name) / 2) * scale,
                     (12 - 9) * scale, scale, 0xFFFFFFu, 1);
    }

    /* ---- hotbar (bottom-center, ~1px above the bottom) ---- */
    const HudSprite *hb = &HUD_SPRITES[HUD_HOTBAR];
    const int hb_w = hb->w * scale;
    const int hb_x = (cx_s - 91) * scale;
    /* vanilla GuiIngame: drawTexturedModalRect(i - 91, sh - 22, ...) */
    const int hb_y = (sh_s - 22) * scale;
    hud_blit(fb, HUD_HOTBAR, hb_x, hb_y, scale);

    /* selection highlight box around the active slot (slots are 20px pitch,
     * first slot starts 3px into the strip; box sprite is 24px, offset -1). */
    int sel = pv->hotbar_sel;
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    const int sel_x = hb_x + (sel * 20 - 1) * scale;
    const int sel_y = hb_y - 1 * scale;
    /* vanilla blits only 24x22 of the 24x24 widget (0,22,24,22) */
    hud_blit_rows(fb, HUD_SELECT, sel_x, sel_y, scale, 22);

    /* item icons (iso cubes for blocks, flat tiles for items; vanilla pos =
     * strip + 3 + i*20). Ids without an icon keep the colored pip. */
    for (int i = 0; i < 9; i++) {
        if (pv->hotbar_counts[i] <= 0 && pv->hotbar_ids[i] <= 0) continue;
        const int ix = hb_x + (3 + i * 20) * scale;
        const int iy = hb_y + 3 * scale;
        if (!gm_gui_item_icon(fb, pv->hotbar_ids[i], pv->hotbar_meta[i],
                              ix, iy, scale)) {
            const int pip = 5 * scale;
            hud_fill(fb, ix + 8 * scale - pip / 2, iy + 8 * scale - pip / 2,
                     pip, pip, hud_pip_color(pv->hotbar_ids[i]));
        }
        if (pv->hotbar_counts[i] > 1) {
            char buf[8];
            int n = pv->hotbar_counts[i], len = 0, tmp = n;
            while (tmp > 0 && len < 7) { len++; tmp /= 10; }
            buf[len] = 0;
            for (int d = len - 1; d >= 0; d--) { buf[d] = (char)('0' + n % 10); n /= 10; }
            /* vanilla renderItemOverlayIntoGUI: x+17-width, y+9, white, shadow */
            gm_font_draw(fb, buf, ix + (17 - gm_font_width(buf)) * scale,
                         iy + 9 * scale, scale, 0xFFFFFFu, 1);
        }
        hud_item_durability(fb, pv->hotbar_ids[i], pv->hotbar_meta[i],
                            ix, iy, scale);
    }

    /* Creative keeps the hotbar and crosshair but omits the survival-only
     * experience, health, armor, hunger, and air rows. Potion effects remain
     * visible in the top-right just as they do in survival. */
    if (pv->creative) {
        hud_draw_crosshair(fb);
        hud_draw_potion_effects(fb, pv, scale, sw_s);
        hud_draw_mount_message(fb, pv, scale, sw_s, sh_s);
        return;
    }

    /* ---- XP bar (directly under the hotbar strip position, MC draws it just
     * above the hotbar; place it spanning the hotbar width just above it) ---- */
    const int xp_x = (cx_s - 91) * scale;
    /* vanilla renderExpBar: l = sh - 32 + 3 */
    const int xp_y = (sh_s - 29) * scale;
    hud_blit(fb, HUD_XP_EMPTY, xp_x, xp_y, scale);
    /* filled portion: vanilla (int)(experience * 183.0F) columns of 182px. */
    {
        int fill_cols = gm_hud_xp_fill_cols(pv->xp_frac);
        const HudSprite *xpf = &HUD_SPRITES[HUD_XP_FULL];
        if (fill_cols > xpf->w) fill_cols = xpf->w;
        for (int sy = 0; sy < xpf->h; sy++)
            for (int sx = 0; sx < fill_cols; sx++) {
                CrRgba t = hud_texel(HUD_XP_FULL, sx, sy);
                if (t.a == 0) continue;
                int px0 = xp_x + sx * scale, py0 = xp_y + sy * scale;
                for (int yy = 0; yy < scale; yy++)
                    for (int xx = 0; xx < scale; xx++)
                        hud_blend_px_tex(fb, px0 + xx, py0 + yy, t);
            }
    }
    /* XP level: FontRenderer outline (4 black offsets) + 0x80FF20 centre.
     * y = sh - 31 - 4 = sh - 35 (= xp_y - 6 at scale 1). */
    if (pv->xp_level > 0) {
        char buf[16];
        int n = pv->xp_level, len = 0, tmp = n;
        if (tmp <= 0) { buf[0] = '0'; len = 1; }
        else {
            while (tmp > 0 && len < 15) { len++; tmp /= 10; }
            for (int d = len - 1; d >= 0; d--) {
                buf[d] = (char)('0' + n % 10); n /= 10;
            }
        }
        buf[len] = 0;
        /* FontRenderer: i1 = (scaledWidth - getStringWidth(s)) / 2
         * (not cx - width/2; integer division differs when both are odd). */
        int lx = ((sw_s - gm_font_width(buf)) / 2) * scale;
        int ly = (sh_s - 35) * scale;
        gm_font_draw(fb, buf, lx + scale, ly, scale, 0x000000u, 0);
        gm_font_draw(fb, buf, lx - scale, ly, scale, 0x000000u, 0);
        gm_font_draw(fb, buf, lx, ly + scale, scale, 0x000000u, 0);
        gm_font_draw(fb, buf, lx, ly - scale, scale, 0x000000u, 0);
        gm_font_draw(fb, buf, lx, ly, scale, 0x80FF20u, 0);
    }

    /* ---- hearts + armor (GuiIngame.renderPlayerStats) ---- */
    /* j1 = height - 39; l1 = ceil((maxHealth + ceil(absorption)) / 2 / 10);
     * i2 = max(10-(l1-2),3); armor j2 = j1 - (l1-1)*i2 - 10.
     * Draw order matches vanilla: armor section first, then health (high j5
     * down to 0). Absorption gold fills remaining slots from the top icon
     * downward (full k5+144 / half k5+153 at icons.png U=160/169 when k5=16).
     * Hardcore uses y*=5 (i5=5) on the same U; survival oracle is i5=0. */
    const int j1_s = sh_s - 39;
    float abs_amt = pv->absorption;
    if (abs_amt < 0.f) abs_amt = 0.f;
    int abs_ceil = (int)ceilf(abs_amt); /* MathHelper.ceil(getAbsorptionAmount()) */
    /* Heart icon count is ceil((maxHealth + ceil(absorption)) / 2), same as
     * the Java loop bound; rows/gap also drive armor y. */
    int heart_icons = (int)ceilf((pv->max_health + (float)abs_ceil) / 2.0f);
    if (heart_icons < 1) heart_icons = 1;
    int heart_rows = (int)ceilf((float)heart_icons / 10.0f);
    if (heart_rows < 1) heart_rows = 1;
    int row_gap = 10 - (heart_rows - 2);
    if (row_gap < 3) row_gap = 3;
    const int stat_y = j1_s * scale; /* first heart row fb y (for hunger) */
    /* GuiIngame's <=4-health one-pixel jitter is seeded from the absolute
     * updateCounter (updateCounter*312871), which current tapes do not record.
     * Keep the stable baseline rather than inventing a phase; health count and
     * damage-flash state still use the recorded health/hurt fields. */
    const int half_hearts = pv->hud_state_valid ? pv->hud_health
                                                 : (int)ceilf(pv->health);
    const int old_half_hearts = pv->hud_state_valid ? pv->hud_last_health
                                                     : half_hearts;
    int heart_full = HUD_HEART_FULL, heart_half = HUD_HEART_HALF;
    int heart_flash_full = HUD_HEART_FLASH_FULL;
    int heart_flash_half = HUD_HEART_FLASH_HALF;
    if (potion_active(pv, 19)) {
        heart_full = HUD_HEART_POISON_FULL;
        heart_half = HUD_HEART_POISON_HALF;
        heart_flash_full = HUD_HEART_POISON_FLASH_FULL;
        heart_flash_half = HUD_HEART_POISON_FLASH_HALF;
    } else if (potion_active(pv, 20)) {
        heart_full = HUD_HEART_WITHER_FULL;
        heart_half = HUD_HEART_WITHER_HALF;
        heart_flash_full = HUD_HEART_WITHER_FLASH_FULL;
        heart_flash_half = HUD_HEART_WITHER_FLASH_HALF;
    }

    /* Armor first (profiler "armor" before "health"). j2 = j1-(l1-1)*i2-10. */
    {
        int armor_pts = pv->armor_points;
        if (armor_pts < 0) armor_pts = 0;
        if (armor_pts > 20) armor_pts = 20;
        if (armor_pts > 0) {
            const int armor_y = (j1_s - (heart_rows - 1) * row_gap - 10) * scale;
            for (int k = 0; k < 10; ++k) {
                int ax = hb_x + (k * 8) * scale;
                int bit = k * 2 + 1;
                int spr = HUD_ARMOR_EMPTY;
                if (bit < armor_pts) spr = HUD_ARMOR_FULL;
                else if (bit == armor_pts) spr = HUD_ARMOR_HALF;
                hud_blit(fb, spr, ax, armor_y, scale);
            }
        }
    }

    /* Health + absorption: j5 from ceil((max+abs)/2)-1 down to 0 so the
     * 1px icon overlap matches vanilla (left icons win). l2 starts at
     * ceil(absorption) and consumes gold slots from the high end. */
    {
        int l2 = abs_ceil;
        for (int j5 = heart_icons - 1; j5 >= 0; --j5) {
            int row = (int)ceilf((float)(j5 + 1) / 10.0f) - 1;
            int hx = hb_x + ((j5 % 10) * 8) * scale;
            int hy = (j1_s - row * row_gap) * scale;
            hud_blit(fb, pv->hud_flash ? HUD_HEART_BG_FLASH : HUD_HEART_BG,
                     hx, hy, scale);
            if (pv->hud_flash) {
                /* lastPlayerHealth white overlay under current/gold fill */
                if (j5 * 2 + 1 < old_half_hearts)
                    hud_blit(fb, heart_flash_full, hx, hy, scale);
                if (j5 * 2 + 1 == old_half_hearts)
                    hud_blit(fb, heart_flash_half, hx, hy, scale);
            }
            if (l2 > 0) {
                /* Odd total absorption: first (highest) gold slot is half. */
                if (l2 == abs_ceil && (abs_ceil % 2) == 1) {
                    hud_blit(fb, HUD_HEART_ABSORB_HALF, hx, hy, scale);
                    --l2;
                } else {
                    hud_blit(fb, HUD_HEART_ABSORB_FULL, hx, hy, scale);
                    l2 -= 2;
                }
            } else {
                if (j5 * 2 + 1 < half_hearts)
                    hud_blit(fb, heart_full, hx, hy, scale);
                if (j5 * 2 + 1 == half_hearts)
                    hud_blit(fb, heart_half, hx, hy, scale);
            }
        }
    }

    /* ---- hunger haunches (above-right of the hotbar, mirrored) ---- */
    const int max_hunger = (int)(pv->max_food / 2.f + 0.5f);
    const int half_food = (int)(pv->food + 0.5f);
    const int hunger_right = hb_x + hb_w; /* right edge of the hotbar */
    /* MobEffects.HUNGER = 17 swaps the haunch sprites (Forge renderFood). */
    int hunger_bg = HUD_HUNGER_BG, hunger_full = HUD_HUNGER_FULL,
        hunger_half = HUD_HUNGER_HALF;
    if (potion_active(pv, 17)) {
        hunger_bg = HUD_HUNGER_POISON_BG;
        hunger_full = HUD_HUNGER_POISON_FULL;
        hunger_half = HUD_HUNGER_POISON_HALF;
    }
    for (int i = 0; i < max_hunger; i++) {
        /* i=0 is the rightmost haunch */
        int hx = hunger_right - (i * 8 + 9) * scale;  /* vanilla: left - i*8 - 9 */
        int hy = stat_y;
        hud_blit(fb, hunger_bg, hx, hy, scale);
        int hv = half_food - i * 2;
        if (hv >= 2)      hud_blit(fb, hunger_full, hx, hy, scale);
        else if (hv == 1) hud_blit(fb, hunger_half, hx, hy, scale);
    }

    /* ---- air bubbles: Forge GuiIngameForge.renderAir, one row above food. */
    if (pv->air >= 0 && pv->air < 300) {
        int full = (int)ceil(((double)pv->air - 2.0) * 10.0 / 300.0);
        int total = (int)ceil((double)pv->air * 10.0 / 300.0);
        if (full < 0) full = 0;
        if (total < 0) total = 0;
        if (total > 10) total = 10;
        const int air_right = (cx_s + 91) * scale;
        const int air_y = (sh_s - 49) * scale;
        for (int i = 0; i < total; ++i)
            hud_blit(fb, i < full ? HUD_AIR_FULL : HUD_AIR_PARTIAL,
                     air_right - (i * 8 + 9) * scale, air_y, scale);
    }

    /* ---- crosshair: vanilla draws the icons.png 16x16 tile at (w/2-7, h/2-7)
     * with blendFunc(ONE_MINUS_DST_COLOR, ONE_MINUS_SRC_COLOR) + alpha test,
     * i.e. opaque texels INVERT against the scene, transparent ones discard. */
    hud_draw_crosshair(fb);

    /* Vanilla draws the potion HUD after the XP bar and selected-item text. */
    hud_draw_potion_effects(fb, pv, scale, sw_s);
    hud_draw_mount_message(fb, pv, scale, sw_s, sh_s);
}
