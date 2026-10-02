/* The chat screen's client half (gui_chat.h): GuiChat, GuiSleepMP and their
 * GuiTextField, one method each in Java's order. */
#include "gui_chat.h"

#include <string.h>

#include "font.h"
#include "font_cw.h"
#include "player.h"

/* ---------------------------------------------------------------- text */

/* ChatAllowedCharacters.isAllowedCharacter */
static int allowed(uint16_t c)
{
    return c != 167 && c >= 32 && c != 127;
}

/* FontRenderer.getCharWidth with the default font (unicodeFlag off): the
 * section sign -1, a space 4, a glyph of ascii.png its width; the unicode
 * pages' glyph_sizes widths are not carried (font.c's rule) */
static int char_width(uint16_t c)
{
    if (c == 167) return -1;
    if (c == 32) return 4;
    int i = font_char_index(c);
    return c > 0 && i >= 0 ? FONT_CW[i] : 0;
}

/* FontRenderer.trimStringToWidth(s, width, reverse): how many chars of s
 * (from its end when reverse) it keeps */
static int trim_count(const uint16_t *s, int n, int width, int reverse)
{
    int w = 0, k = 0, fmt = 0, bold = 0;

    for (int i = reverse ? n - 1 : 0; i >= 0 && i < n && w < width; i += reverse ? -1 : 1)
    {
        uint16_t c = s[i];
        int x = char_width(c);
        if (fmt)
        {
            fmt = 0;
            if (c == 'l' || c == 'L') bold = 1;
            else if (c == 'r' || c == 'R') bold = 0;
        }
        else if (x < 0) fmt = 1;
        else
        {
            w += x;
            if (bold) ++w;
        }
        if (w > width) break;
        ++k;
    }
    return k;
}

int gui_chat_width(const uint16_t *s, int n)
{
    int w = 0, bold = 0;

    for (int i = 0; i < n; ++i)
    {
        int x = char_width(s[i]);
        if (x < 0 && i < n - 1)
        {
            uint16_t c = s[++i];
            if (c == 'l' || c == 'L') bold = 1;
            else if (c == 'r' || c == 'R') bold = 0;
            x = 0;
        }
        w += x;
        if (bold && x > 0) ++w;
    }
    return w;
}

int gui_chat_utf16(const char *s, uint16_t *out, int cap)
{
    const unsigned char *u = (const unsigned char *)s;
    int n = 0;

    while (*u && n < cap)
    {
        uint32_t c;
        if (*u < 0x80) c = *u++;
        else if ((*u & 0xe0) == 0xc0 && u[1]) { c = (uint32_t)(*u & 0x1f) << 6 | (u[1] & 0x3f); u += 2; }
        else if ((*u & 0xf0) == 0xe0 && u[1] && u[2])
        {
            c = (uint32_t)(*u & 0x0f) << 12 | (uint32_t)(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
            u += 3;
        }
        else if ((*u & 0xf8) == 0xf0 && u[1] && u[2] && u[3])
        {
            c = (uint32_t)(*u & 0x07) << 18 | (uint32_t)(u[1] & 0x3f) << 12 | (uint32_t)(u[2] & 0x3f) << 6 | (u[3] & 0x3f);
            u += 4;
            if (n + 1 >= cap) break;
            c -= 0x10000;
            out[n++] = (uint16_t)(0xd800 + (c >> 10));
            out[n++] = (uint16_t)(0xdc00 + (c & 0x3ff));
            continue;
        }
        else { c = 0xfffd; ++u; }
        out[n++] = (uint16_t)c;
    }
    return n;
}

int gui_chat_utf16_allowed(const char *s, uint16_t *out, int cap)
{
    int n = 0;
    const char *p = s;

    while (*p && n < cap)
    {
        /* one code point at a time: its length from the lead byte */
        unsigned char b = (unsigned char)*p;
        int len = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : (b & 0xf8) == 0xf0 ? 4 : 1;
        char one[5] = {0};
        for (int i = 0; i < len && p[i]; ++i) one[i] = p[i];
        uint16_t u[2];
        int m = gui_chat_utf16(one, u, 2);
        for (int i = 0; i < m && n < cap; ++i)
            if (allowed(u[i])) out[n++] = u[i];
        for (int i = 0; i < len && *p; ++i) ++p;
    }
    return n;
}

int gui_chat_utf8(const uint16_t *s, int n, char *out)
{
    int k = 0;

    for (int i = 0; i < n; ++i)
    {
        uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
        {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            ++i;
            out[k++] = (char)(0xf0 | (c >> 18));
            out[k++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[k++] = (char)(0x80 | (c & 0x3f));
        }
        else if (c < 0x80) out[k++] = (char)c;
        else if (c < 0x800)
        {
            out[k++] = (char)(0xc0 | (c >> 6));
            out[k++] = (char)(0x80 | (c & 0x3f));
        }
        else
        {
            /* a lone surrogate as Java's String.getBytes(UTF-8): '?' */
            if (c >= 0xd800 && c < 0xe000) { out[k++] = '?'; continue; }
            out[k++] = (char)(0xe0 | (c >> 12));
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[k++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[k] = 0;
    return k;
}

/* String.trim: the chars up to a space off both ends, as [*from, *to) */
static void trim_bounds(const uint16_t *s, int n, int *from, int *to)
{
    int a = 0, b = n;
    while (a < b && s[a] <= 32) ++a;
    while (b > a && s[b - 1] <= 32) --b;
    *from = a;
    *to = b;
}

/* --------------------------------------------------------- GuiTextField */

/* func_146199_i: the selection's end, and the scroll that keeps it drawn */
static void tf_set_sel(struct gui_text_field *f, int pos)
{
    int n = f->len;

    if (pos > n) pos = n;
    if (pos < 0) pos = 0;
    f->sel = pos;
    if (f->scroll > n) f->scroll = n;
    int w = f->w;   /* func_146200_o: no background */
    int end = trim_count(f->text + f->scroll, n - f->scroll, w, 0) + f->scroll;
    if (pos == f->scroll) f->scroll -= trim_count(f->text, n, w, 1);
    if (pos > end) f->scroll += pos - end;
    else if (pos <= f->scroll) f->scroll -= f->scroll - pos;
    if (f->scroll < 0) f->scroll = 0;
    if (f->scroll > n) f->scroll = n;
}

/* func_146190_e: the cursor, the selection with it */
static void tf_set_cursor(struct gui_text_field *f, int pos)
{
    f->cursor = pos;
    if (f->cursor < 0) f->cursor = 0;
    if (f->cursor > f->len) f->cursor = f->len;
    tf_set_sel(f, f->cursor);
}

/* func_146182_d: from the selection's end */
static void tf_move(struct gui_text_field *f, int d)
{
    tf_set_cursor(f, f->sel + d);
}

/* setText */
static void tf_set_text(struct gui_text_field *f, const uint16_t *s, int n)
{
    if (n > f->max) n = f->max;
    memmove(f->text, s, (size_t)n * sizeof f->text[0]);
    f->len = n;
    tf_set_cursor(f, f->len);   /* func_146202_e */
}

/* func_146191_b: s (filtered to the allowed chars) over the selection */
static void tf_write(struct gui_text_field *f, const uint16_t *s, int n)
{
    uint16_t t[GC_MAX], out[GC_MAX];
    int tn = 0, k = 0;

    for (int i = 0; i < n && tn < GC_MAX; ++i)
        if (allowed(s[i])) t[tn++] = s[i];
    int lo = f->cursor < f->sel ? f->cursor : f->sel;
    int hi = f->cursor < f->sel ? f->sel : f->cursor;
    int room = f->max - f->len - (lo - f->sel);
    int add;

    if (f->len > 0) { memcpy(out, f->text, (size_t)lo * sizeof out[0]); k = lo; }
    add = room < tn ? room : tn;
    if (add < 0) add = 0;
    memcpy(out + k, t, (size_t)add * sizeof out[0]);
    k += add;
    if (f->len > 0 && hi < f->len)
    {
        memcpy(out + k, f->text + hi, (size_t)(f->len - hi) * sizeof out[0]);
        k += f->len - hi;
    }
    memcpy(f->text, out, (size_t)k * sizeof out[0]);
    f->len = k;
    tf_move(f, lo - f->sel + add);
}

/* func_146197_a: n words from pos (back when negative), skipping runs of
 * spaces when skip */
static int tf_word(const struct gui_text_field *f, int n, int pos, int skip)
{
    int p = pos, back = n < 0, m = n < 0 ? -n : n;

    for (int i = 0; i < m; ++i)
    {
        if (back)
        {
            while (skip && p > 0 && f->text[p - 1] == 32) --p;
            while (p > 0 && f->text[p - 1] != 32) --p;
        }
        else
        {
            int len = f->len, q = -1;
            for (int j = p < 0 ? 0 : p; j < len; ++j)
                if (f->text[j] == 32) { q = j; break; }
            p = q;
            if (p == -1) p = len;
            else
                while (skip && p < len && f->text[p] == 32) ++p;
        }
    }
    return p;
}

/* func_146187_c, and func_146183_a (which reads the cursor, not its
 * second argument) */
static int tf_word_from_cursor(const struct gui_text_field *f, int n)
{
    return tf_word(f, n, f->cursor, 1);
}

/* func_146175_b: n chars from the cursor, or the selection */
static void tf_delete(struct gui_text_field *f, int n)
{
    if (f->len == 0) return;
    if (f->sel != f->cursor)
    {
        tf_write(f, NULL, 0);
        return;
    }
    int back = n < 0;
    int a = back ? f->cursor + n : f->cursor;
    int b = back ? f->cursor : f->cursor + n;
    uint16_t out[GC_MAX];
    int k = 0;

    if (a >= 0) { memcpy(out, f->text, (size_t)a * sizeof out[0]); k = a; }
    if (b < f->len)
    {
        memcpy(out + k, f->text + b, (size_t)(f->len - b) * sizeof out[0]);
        k += f->len - b;
    }
    memcpy(f->text, out, (size_t)k * sizeof out[0]);
    f->len = k;
    if (back) tf_move(f, n);
}

/* func_146177_a: n words, or the selection */
static void tf_delete_words(struct gui_text_field *f, int n)
{
    if (f->len == 0) return;
    if (f->sel != f->cursor) tf_write(f, NULL, 0);
    else tf_delete(f, tf_word_from_cursor(f, n) - f->cursor);
}

/* setFocused */
static void tf_focus(struct gui_text_field *f, int on)
{
    if (on && !f->focused) f->counter = 0;
    f->focused = on;
}

/* textboxKeyTyped (the clipboard is the client's) */
struct tf_ctx { struct gui_text_field *f; struct chat_clip *clip; };
static void tf_key(struct tf_ctx *g, uint16_t c, int code, int shift, int ctrl)
{
    struct gui_text_field *f = g->f;

    if (!f->focused) return;
    switch (c)
    {
    case 1:
        tf_set_cursor(f, f->len);
        tf_set_sel(f, 0);
        return;
    case 3:
    case 24:
    {
        int lo = f->cursor < f->sel ? f->cursor : f->sel, hi = f->cursor < f->sel ? f->sel : f->cursor;
        memcpy(g->clip->s, f->text + lo, (size_t)(hi - lo) * sizeof g->clip->s[0]);
        g->clip->n = hi - lo;
        if (c == 24) tf_write(f, NULL, 0);
        return;
    }
    case 22:
        tf_write(f, g->clip->s, g->clip->n);
        return;
    default:
        break;
    }
    switch (code)
    {
    case 14:
        if (ctrl) tf_delete_words(f, -1);
        else tf_delete(f, -1);
        return;
    case 199:
        if (shift) tf_set_sel(f, 0);
        else tf_set_cursor(f, 0);
        return;
    case 203:
        if (shift) tf_set_sel(f, ctrl ? tf_word_from_cursor(f, -1) : f->sel - 1);
        else if (ctrl) tf_set_cursor(f, tf_word_from_cursor(f, -1));
        else tf_move(f, -1);
        return;
    case 205:
        if (shift) tf_set_sel(f, ctrl ? tf_word_from_cursor(f, 1) : f->sel + 1);
        else if (ctrl) tf_set_cursor(f, tf_word_from_cursor(f, 1));
        else tf_move(f, 1);
        return;
    case 207:
        if (shift) tf_set_sel(f, f->len);
        else tf_set_cursor(f, f->len);
        return;
    case 211:
        if (ctrl) tf_delete_words(f, 1);
        else tf_delete(f, 1);
        return;
    default:
        if (allowed(c)) tf_write(f, &c, 1);
        return;
    }
}

/* mouseClicked: the cursor where the press falls in the drawn text */
static void tf_click(struct gui_text_field *f, int x, int y, int button)
{
    (void)y;
    /* field_146212_n is off (GuiChat.initGui's func_146205_d(false)): the
     * focus stays */
    if (f->focused && button == 0)
    {
        int d = x - f->x;
        int vis = trim_count(f->text + f->scroll, f->len - f->scroll, f->w, 0);
        tf_set_cursor(f, trim_count(f->text + f->scroll, vis, d, 0) + f->scroll);
    }
}

/* -------------------------------------------------------------- GuiChat */

static void out_add(struct client_player *p, int kind, const uint16_t *s, int n)
{
    struct chat_out *o = &p->chat_out;
    if (o->n >= GC_OUT) return;
    o->kind[o->n] = kind;
    gui_chat_utf8(s, n, o->text[o->n]);
    ++o->n;
}

static void fx_scroll(struct client_player *p, int32_t d)
{
    struct chat_fx *x = &p->chat_fx;
    if (x->nscroll < CHAT_FX_MAX) x->scroll[x->nscroll++] = d;
}

/* GuiNewChat.func_146239_a: a message not equal to the last one */
static void sent_add(struct chat_sent *h, const uint16_t *s, int n)
{
    if (h->n > 0)
    {
        int last = (h->n - 1) % GC_HIST;
        if (h->len[last] == n && !memcmp(h->s[last], s, (size_t)n * sizeof s[0])) return;
    }
    int k = h->n % GC_HIST;
    memcpy(h->s[k], s, (size_t)n * sizeof s[0]);
    h->len[k] = (uint8_t)n;
    ++h->n;
}

/* func_146403_a: the history, then EntityClientPlayerMP.sendChatMessage */
static void chat_send(struct client_player *p, const uint16_t *s, int n)
{
    sent_add(&p->chat_sent, s, n);
    out_add(p, 0, s, n);
}

/* func_146402_a: the up and down keys over the sent history */
static void chat_history(struct client_player *p, int d)
{
    struct gui_chat *g = &p->chat;
    struct chat_sent *h = &p->chat_sent;
    int want = g->hist_idx + d, size = h->n;

    if (want < 0) want = 0;
    if (want > size) want = size;
    if (want == g->hist_idx) return;
    if (want == size)
    {
        g->hist_idx = size;
        tf_set_text(&g->f, g->saved, g->saved_len);
        return;
    }
    if (g->hist_idx == size)
    {
        memcpy(g->saved, g->f.text, (size_t)g->f.len * sizeof g->saved[0]);
        g->saved_len = g->f.len;
    }
    if (want < size - GC_HIST)
    {
        /* the kept history is the newest GC_HIST: an older one is not
         * modelled (vanilla's list is unbounded) */
        g->hist_idx = want;
        tf_set_text(&g->f, NULL, 0);
        p->chat_unmodelled = 1;
        return;
    }
    tf_set_text(&g->f, h->s[want % GC_HIST], h->len[want % GC_HIST]);
    g->hist_idx = want;
}

/* func_146234_a(new ChatComponentText(list), 1): the completions joined
 * by ", " as the chat's id-1 line (the drawn chat takes it with the S02s) */
static void chat_tab_line(struct client_player *p)
{
    struct gui_chat *g = &p->chat;
    if (p->s02_n >= (int)(sizeof p->s02_nparts / sizeof p->s02_nparts[0])) return;
    int n = p->s02_n++;
    char *t = p->s02_text[n];
    size_t cap = sizeof p->s02_text[n], k = 0;

    t[k++] = 0;   /* the part's formatting code: none */
    for (int i = 0; i < g->ntab; ++i)
    {
        char u[GC_TAB_LEN * 3 + 1];
        int m = gui_chat_utf8(g->tab[i], g->tab_len[i], u);
        if (k + (size_t)m + 5 >= cap) break;
        if (i > 0) { t[k++] = ','; t[k++] = ' '; }
        memcpy(t + k, u, (size_t)m);
        k += (size_t)m;
    }
    t[k++] = 0;
    t[k++] = 0;   /* no click */
    t[k] = 0;     /* no hover */
    p->s02_nparts[n] = 1;
    p->s02_id[n] = 1;
}

/* func_146404_p_: Tab */
static void chat_tab(struct client_player *p)
{
    struct gui_chat *g = &p->chat;
    struct gui_text_field *f = &g->f;

    if (g->completing)
    {
        tf_delete(f, tf_word(f, -1, f->cursor, 0) - f->cursor);
        if (g->tab_idx >= g->ntab) g->tab_idx = 0;
    }
    else
    {
        g->ntab = 0;
        g->tab_idx = 0;
        /* func_146405_a: the text up to the cursor, when there is any */
        if (f->cursor >= 1)
        {
            out_add(p, 1, f->text, f->cursor);
            g->waiting = 1;
        }
        return;   /* the list was just cleared */
    }
    if (g->ntab > 1) chat_tab_line(p);
    int i = g->tab_idx++;
    tf_write(f, g->tab[i], g->tab_len[i]);
}

/* GuiChat.keyTyped, and GuiSleepMP's over it */
static void chat_key(struct client_player *p, uint16_t c, int code, int shift, int ctrl, int *close, int *wake)
{
    struct gui_chat *g = &p->chat;

    if (g->sleep)
    {
        if (code == 1)
        {
            *wake = 1;
            return;
        }
        if (code == 28 || code == 156)
        {
            int a, b;
            trim_bounds(g->f.text, g->f.len, &a, &b);
            /* sendChatMessage without the history */
            if (b > a) out_add(p, 0, g->f.text + a, b - a);
            tf_set_text(&g->f, NULL, 0);
            fx_scroll(p, CHAT_FX_RESET);
            return;
        }
    }
    g->waiting = 0;
    if (code == 15) chat_tab(p);
    else g->completing = 0;

    if (code == 1)
    {
        *close = 1;
        return;
    }
    if (code == 28 || code == 156)
    {
        int a, b;
        trim_bounds(g->f.text, g->f.len, &a, &b);
        if (b > a)
        {
            uint16_t s[GC_MAX];
            memcpy(s, g->f.text + a, (size_t)(b - a) * sizeof s[0]);
            chat_send(p, s, b - a);
        }
        *close = 1;
        return;
    }
    /* func_146232_i with the chat open: chatHeightFocused 1.0, 180 / 9 */
    const int lines = 20;
    if (code == 200) chat_history(p, -1);
    else if (code == 208) chat_history(p, 1);
    else if (code == 201) fx_scroll(p, lines - 1);
    else if (code == 209) fx_scroll(p, -lines + 1);
    else
    {
        struct tf_ctx t = {&g->f, &p->chat_clip};
        tf_key(&t, c, code, shift, ctrl);
    }
}

void gui_chat_open(struct client_player *p, const char *preset, int sleep, int width, int height)
{
    struct gui_chat *g = &p->chat;
    uint16_t s[GC_MAX];
    int n = gui_chat_utf16(preset, s, GC_MAX);

    memset(g, 0, sizeof *g);
    g->up = 1;
    g->sleep = sleep;
    g->hist_idx = p->chat_sent.n;
    struct gui_text_field *f = &g->f;
    f->x = 4;
    f->y = height - 12;
    f->w = width - 4;
    f->h = 12;
    f->max = GC_MAX;            /* func_146203_f(100) */
    tf_focus(f, 1);
    tf_set_text(f, s, n);
}

void gui_chat_run(struct client_player *p, const struct chat_op *op, int *close, int *wake)
{
    struct gui_chat *g = &p->chat;
    int shift = (op->mods & 1) != 0, ctrl = (op->mods & 2) != 0;
    /* the scaled screen the oracle's 854 by 480 display makes (GUI scale 2) */
    const int width = GC_SCREEN_W, height = GC_SCREEN_H;

    *close = *wake = 0;
    if (op->has_clip)
    {
        memcpy(p->chat_clip.s, op->clip, (size_t)op->clip_len * sizeof op->clip[0]);
        p->chat_clip.n = op->clip_len;
    }
    /* GuiScreen.handleInput: the Mouse queue, then the Keyboard queue */
    for (int i = 0; i < op->nev; ++i)
    {
        const struct chat_ev *e = &op->ev[i];
        if (e->kind == CHAT_EV_PRESS)
        {
            int x = e->x * width / GC_DISPLAY_W;
            int y = height - e->y * height / GC_DISPLAY_H - 1;
            /* GuiChat.mouseClicked: chatLinks is on; the component under
             * the pointer is the tape's (GuiNewChat.func_146236_a) */
            if (e->button == 0 && op->has_comp)
            {
                if (shift)
                {
                    uint16_t t[GC_MAX];
                    int n = gui_chat_utf16(op->comp_text, t, GC_MAX);
                    tf_write(&g->f, t, n);
                }
                else
                {
                    /* SUGGEST_COMMAND */
                    uint16_t t[GC_MAX];
                    int n = gui_chat_utf16(op->comp_value, t, GC_MAX);
                    tf_set_text(&g->f, t, n);
                }
                continue;
            }
            tf_click(&g->f, x, y, e->button);
            /* GuiScreen.mouseClicked: GuiSleepMP's Leave Bed button */
            if (e->button == 0 && g->sleep && x >= width / 2 - 100 && y >= height - 40 && x < width / 2 + 100 &&
                y < height - 20)
                *wake = 1;
        }
        else if (e->kind == CHAT_EV_WHEEL)
        {
            int d = e->code;
            if (d != 0)
            {
                if (d > 1) d = 1;
                if (d < -1) d = -1;
                if (!shift) d *= 7;
                fx_scroll(p, d);
            }
        }
    }
    for (int i = 0; i < op->nev; ++i)
    {
        const struct chat_ev *e = &op->ev[i];
        if (e->kind != CHAT_EV_KEY) continue;
        int c = 0, w = 0;
        chat_key(p, e->ch, e->code, shift, ctrl, &c, &w);
        /* displayGuiScreen(null): GuiChat.onGuiClosed resets the scroll
         * (once: a later close finds no screen up) */
        if (c && !*close) fx_scroll(p, CHAT_FX_RESET);
        *close |= c;
        *wake |= w;
    }
}

void gui_chat_update(struct client_player *p)
{
    ++p->chat.f.counter;
}

void gui_chat_tab_reply(struct client_player *p, const char *const *items, int n)
{
    struct gui_chat *g = &p->chat;
    struct gui_text_field *f = &g->f;

    if (!g->waiting) return;
    g->completing = 0;
    g->ntab = 0;
    for (int i = 0; i < n; ++i)
    {
        if (!items[i][0] || g->ntab >= GC_TABS) continue;
        g->tab_len[g->ntab] = (uint8_t)gui_chat_utf16(items[i], g->tab[g->ntab], GC_TAB_LEN);
        ++g->ntab;
    }
    /* StringUtils.getCommonPrefix over every string (the empty ones too) */
    uint16_t pre[GC_TAB_LEN];
    int plen = 0;
    if (n > 0)
    {
        plen = gui_chat_utf16(items[0], pre, GC_TAB_LEN);
        for (int i = 1; i < n && plen > 0; ++i)
        {
            uint16_t s[GC_TAB_LEN];
            int m = gui_chat_utf16(items[i], s, GC_TAB_LEN), k = 0;
            while (k < plen && k < m && s[k] == pre[k]) ++k;
            plen = k;
        }
    }
    int start = tf_word(f, -1, f->cursor, 0);
    /* the word from its start to the text's end, against the prefix,
     * ignoring case */
    int wlen = f->len - start, same = wlen == plen;
    for (int i = 0; same && i < plen; ++i)
    {
        uint16_t a = f->text[start + i], b = pre[i];
        if (a < 128 && a >= 'A' && a <= 'Z') a = (uint16_t)(a + 32);
        if (b < 128 && b >= 'A' && b <= 'Z') b = (uint16_t)(b + 32);
        same = a == b;
    }
    if (plen > 0 && !same)
    {
        tf_delete(f, start - f->cursor);
        tf_write(f, pre, plen);
    }
    else if (g->ntab > 0)
    {
        g->completing = 1;
        chat_tab(p);
    }
}

void gui_chat_draw_state(const struct client_player *p, struct gui_chat_draw *d)
{
    const struct gui_text_field *f = &p->chat.f;
    const uint16_t *v = f->text + f->scroll;
    int vis = trim_count(v, f->len - f->scroll, f->w, 0);

    d->vis_len = vis;
    gui_chat_utf8(v, vis, d->vis);
    d->cursor = f->cursor - f->scroll;
    d->sel = f->sel - f->scroll;
    d->on = d->cursor >= 0 && d->cursor <= vis;
    gui_chat_utf8(v, d->on ? d->cursor : vis, d->pre);
    gui_chat_utf8(v + (d->on ? d->cursor : vis), d->on ? vis - d->cursor : 0, d->post);
    int sel = d->sel > vis ? vis : d->sel;
    gui_chat_utf8(v, sel < 0 ? 0 : sel, d->sel_pre);
    d->blink_on = f->focused && f->counter / 6 % 2 == 0 && d->on;
    d->bar = f->cursor < f->len || f->len >= f->max;
    d->x = f->x;
    d->y = f->y;
    d->w = f->w;
}
