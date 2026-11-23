/* GuiNewChat's message and line lists; see chat.h. */
#define _POSIX_C_SOURCE 200809L
#include "chat.h"
#include "jmath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "chat: allocation failed\n"); exit(1); }
    return p;
}

int chat_width(float w)
{
    return mh_floor_float(w * (float)(320 - 40) + (float)40);
}

int chat_height(float h)
{
    return mh_floor_float(h * (float)(180 - 20) + (float)20);
}

static char *cat3(const char *a, const char *b, const char *c)
{
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char *r = xalloc(la + lb + lc + 1);
    memcpy(r, a, la);
    memcpy(r + la, b, lb);
    memcpy(r + la + lb, c, lc + 1);
    return r;
}

/* EnumChatFormatting.getTextWithoutFormattingCodes: every section sign with
 * the code after it, case-insensitively 0-9, a-f, k-o, r. */
static char *strip_codes(const char *s)
{
    size_t n;
    uint32_t *cp = font_utf8_decode(s, &n);
    uint32_t *out = xalloc((n + 1) * sizeof *out);
    size_t k = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (cp[i] == FONT_SECTION && i + 1 < n)
        {
            uint32_t c = cp[i + 1];
            if (c >= 'A' && c <= 'Z') c += 32;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'k' && c <= 'o') || c == 'r')
            {
                ++i;
                continue;
            }
        }
        out[k++] = cp[i];
    }
    char *r = font_utf8_encode(out, k);
    free(cp);
    free(out);
    return r;
}

/* substring by code points, [a, b) */
static char *cp_sub(const char *s, size_t a, size_t b)
{
    size_t n;
    uint32_t *cp = font_utf8_decode(s, &n);
    if (b > n) b = n;
    if (a > b) a = b;
    char *r = font_utf8_encode(cp + a, b - a);
    free(cp);
    return r;
}

static size_t cp_len(const char *s)
{
    size_t n;
    free(font_utf8_decode(s, &n));
    return n;
}

/* String.lastIndexOf(" "), in code points */
static long cp_last_space(const char *s)
{
    size_t n;
    uint32_t *cp = font_utf8_decode(s, &n);
    long r = -1;
    for (size_t i = 0; i < n; ++i)
        if (cp[i] == ' ') r = (long)i;
    free(cp);
    return r;
}

struct piece { const char *fmt; char *text; int part; };

/* func_146237_a's split; the lines, and each line's pieces when pieces is
 * not NULL */
static char **split_impl(const struct font_metrics *f, const struct chat_message *m, int width,
                         int colours, int *nlines, struct chat_piece *pieces, int max, int *npieces)
{
    /* var9: the components still to place, a split's rest going in after the
     * one it came from with that one's style */
    int cap = m->nparts + 8, nq = m->nparts, np = 0;
    struct piece *q = xalloc((size_t)cap * sizeof *q);
    for (int i = 0; i < m->nparts; ++i) { q[i].fmt = m->fmt[i]; q[i].text = strdup(m->text[i]); q[i].part = i; }
    int lcap = 4, nl = 0;
    char **lines = xalloc((size_t)lcap * sizeof *lines);
    char *line = strdup("\302\247r"); /* the line's own empty root, then its reset */
    int used = 0;
    for (int i = 0; i < nq; ++i)
    {
        char *raw = cat3(q[i].fmt, q[i].text, "");
        char *v12 = colours ? raw : strip_codes(raw);
        if (!colours) free(raw);
        int w13 = font_string_width(f, v12);
        char *seg = v12;
        int split = 0;
        if (used + w13 > width)
        {
            char *v16 = font_trim_to_width(f, v12, width - used);
            char *v17 = cp_len(v16) < cp_len(v12) ? cp_sub(v12, cp_len(v16), (size_t)-1) : NULL;
            if (v17 && v17[0])
            {
                long sp = cp_last_space(v16);
                if (sp >= 0)
                {
                    char *head = cp_sub(v12, 0, (size_t)sp);
                    if (font_string_width(f, head) > 0)
                    {
                        free(v16);
                        free(v17);
                        v16 = head;
                        v17 = cp_sub(v12, (size_t)sp, (size_t)-1);
                    }
                    else free(head);
                }
                if (nq + 1 > cap) { cap *= 2; q = realloc(q, (size_t)cap * sizeof *q); if (!q) exit(1); }
                memmove(q + i + 2, q + i + 1, (size_t)(nq - i - 1) * sizeof *q);
                q[i + 1].fmt = q[i].fmt;
                q[i + 1].text = v17;
                q[i + 1].part = q[i].part;
                ++nq;
                v17 = NULL;
            }
            free(v17);
            w13 = font_string_width(f, v16);
            seg = v16;
            split = 1;
        }
        if (used + w13 <= width)
        {
            used += w13;
            if (pieces && np < max)
                pieces[np++] = (struct chat_piece){nl, q[i].part, strdup(seg)};
            char *next = cat3(line, q[i].fmt, seg);
            free(line);
            line = cat3(next, "\302\247r", "");
            free(next);
        }
        else split = 1;
        if (seg != v12) free(seg);
        free(v12);
        if (split)
        {
            if (nl + 2 > lcap) { lcap *= 2; lines = realloc(lines, (size_t)lcap * sizeof *lines); if (!lines) exit(1); }
            lines[nl++] = line;
            line = strdup("\302\247r");
            used = 0;
        }
    }
    if (nl + 1 > lcap) { lcap += 1; lines = realloc(lines, (size_t)lcap * sizeof *lines); if (!lines) exit(1); }
    lines[nl++] = line;
    for (int i = 0; i < nq; ++i) free(q[i].text);
    free(q);
    *nlines = nl;
    if (npieces) *npieces = np;
    return lines;
}

char **chat_split(const struct font_metrics *f, const struct chat_message *m, int width,
                  int colours, int *nlines)
{
    return split_impl(f, m, width, colours, nlines, NULL, 0, NULL);
}

void chat_split_pieces(const struct font_metrics *f, const struct chat_message *m, int width, int colours,
                       struct chat_piece *out, int max, int *npieces, int *nlines)
{
    char **lines = split_impl(f, m, width, colours, nlines, out, max, npieces);
    for (int i = 0; i < *nlines; ++i) free(lines[i]);
    free(lines);
}

void chat_message_set(struct chat_message *m, int counter, int id, int n,
                      const char *const *fmt, const char *const *text)
{
    memset(m, 0, sizeof *m);
    m->counter = counter;
    m->id = id;
    if (n > CHAT_PARTS) n = CHAT_PARTS;
    m->nparts = n;
    for (int i = 0; i < n; ++i) { m->fmt[i] = strdup(fmt[i]); m->text[i] = strdup(text[i]); }
}

void chat_message_set_click(struct chat_message *m, int i, const char *value)
{
    if (i < 0 || i >= m->nparts) return;
    free(m->click[i]);
    m->click[i] = value ? strdup(value) : NULL;
}

void chat_message_set_hover(struct chat_message *m, int i, const char *value)
{
    if (i < 0 || i >= m->nparts) return;
    free(m->hover[i]);
    m->hover[i] = value ? strdup(value) : NULL;
}

static void message_free(struct chat_message *m)
{
    for (int i = 0; i < m->nparts; ++i)
    {
        free(m->fmt[i]); free(m->text[i]); free(m->click[i]); free(m->hover[i]);
        m->click[i] = m->hover[i] = NULL;
    }
    m->nparts = 0;
}

void chat_remove_id(struct chat *c, int id)
{
    for (int i = 0; i < c->nmsg; ++i)
        if (c->msg[i].id == id)
        {
            message_free(&c->msg[i]);
            memmove(c->msg + i, c->msg + i + 1, (size_t)(c->nmsg - i - 1) * sizeof *c->msg);
            --c->nmsg;
            return;
        }
}

void chat_add(struct chat *c, const struct font_metrics *f, struct chat_message *m,
              int width, int colours)
{
    int n = 0;
    char **lines = chat_split(f, m, width, colours, &n);
    /* each line goes in at the front: the message's last line ends up first */
    for (int i = 0; i < n; ++i)
    {
        if (c->nline == CHAT_MAX) { free(c->line[CHAT_MAX - 1].text); --c->nline; }
        memmove(c->line + 1, c->line, (size_t)c->nline * sizeof *c->line);
        c->line[0] = (struct chat_line){m->counter, m->id, lines[i]};
        ++c->nline;
    }
    free(lines);
    if (c->nmsg == CHAT_MAX) { message_free(&c->msg[CHAT_MAX - 1]); --c->nmsg; }
    memmove(c->msg + 1, c->msg, (size_t)c->nmsg * sizeof *c->msg);
    c->msg[0] = *m;
    ++c->nmsg;
    memset(m, 0, sizeof *m);
}

void chat_push_message(struct chat *c, struct chat_message *m)
{
    if (c->nmsg == CHAT_MAX) { message_free(&c->msg[CHAT_MAX - 1]); --c->nmsg; }
    memmove(c->msg + 1, c->msg, (size_t)c->nmsg * sizeof *c->msg);
    c->msg[0] = *m;
    ++c->nmsg;
    memset(m, 0, sizeof *m);
}

void chat_free(struct chat *c)
{
    for (int i = 0; i < c->nmsg; ++i) message_free(&c->msg[i]);
    for (int i = 0; i < c->nline; ++i) free(c->line[i].text);
    c->nmsg = c->nline = 0;
}
