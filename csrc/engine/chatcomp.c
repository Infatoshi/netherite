/* The server's chat components and the S02 that carries one (chatcomp.h). */
#include "chatcomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "envstack.h"
#include "survival.h"
#include "lang.h"
#include "tape.h"

/* ---------------------------------------------------- EnumChatFormatting */

static const char CF_CODE[CF_COUNT] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    'k', 'l', 'm', 'n', 'o', 'r',
};

/* getFriendlyName: name().toLowerCase() */
static const char *const CF_NAME[CF_COUNT] = {
    "black", "dark_blue", "dark_green", "dark_aqua", "dark_red", "dark_purple", "gold",
    "gray", "dark_gray", "blue", "green", "aqua", "red", "light_purple", "yellow", "white",
    "obfuscated", "bold", "strikethrough", "underline", "italic", "reset",
};

char chat_fmt_code(int f)
{
    return CF_CODE[f];
}

const char *chat_fmt_friendly(int f)
{
    return CF_NAME[f];
}

/* the controlString: "§" + code, the section sign in UTF-8 */
int chat_fmt_string(int f, char *out)
{
    out[0] = (char)0xc2;
    out[1] = (char)0xa7;
    out[2] = CF_CODE[f];
    out[3] = 0;
    return 3;
}

/* ---------------------------------------------------- the component tree */

static void cc_fail(const char *what)
{
    fprintf(stderr, "chatcomp: %s\n", what);
    abort();
}

static int16_t pool_add(struct chat_comp *t, const char *s)
{
    size_t n = strlen(s) + 1;
    if ((size_t)t->npool + n > CC_POOL) cc_fail("the string pool is full");
    memcpy(&t->pool[t->npool], s, n);
    int16_t off = (int16_t)t->npool;
    t->npool += (int)n;
    return off;
}

void chat_comp_init(struct chat_comp *t)
{
    t->ncomp = t->nstyle = t->npool = 0;
}

struct chat_comp *chat_comp_take(void)
{
    struct chat_comp *t = envstack_take(sizeof *t);
    chat_comp_init(t);
    return t;
}

static int new_comp(struct chat_comp *t, int kind, const char *s)
{
    if (t->ncomp >= CC_MAX_COMP) cc_fail("too many components");
    int c = t->ncomp++;
    struct cc_comp *k = &t->comp[c];
    memset(k, 0, sizeof *k);
    k->kind = (uint8_t)kind;
    k->style = -1;
    k->first_sib = k->last_sib = k->next_sib = -1;
    k->str = pool_add(t, s);
    return c;
}

/* new ChatStyle(): every field null */
static int new_style(struct chat_comp *t)
{
    if (t->nstyle >= CC_MAX_STYLE) cc_fail("too many styles");
    int s = t->nstyle++;
    struct cc_style *st = &t->style[s];
    st->color = st->bold = st->italic = st->underlined = st->strikethrough = st->obfuscated = -1;
    st->click = CC_CLICK_NONE;
    st->hover = CC_HOVER_NONE;
    st->click_value = -1;
    st->hover_value = -1;
    st->parent = -1;
    return s;
}

int chat_text(struct chat_comp *t, const char *text)
{
    return new_comp(t, CC_TEXT, text);
}

int chat_translation(struct chat_comp *t, const char *key)
{
    return new_comp(t, CC_TRANSLATION, key);
}

/* A component's style that must exist already: every sibling got one when
 * appendSibling ran (getChatStyle().setParentStyle), so the parenting
 * walks below never make one (and never recurse). */
static struct cc_style *style_made(struct chat_comp *t, int c)
{
    if (t->comp[c].style < 0) cc_fail("a sibling without a style");
    return &t->style[t->comp[c].style];
}

int chat_style_of(struct chat_comp *t, int c)
{
    struct cc_comp *k = &t->comp[c];
    if (k->style < 0)
    {
        int16_t st = (int16_t)new_style(t);
        k = &t->comp[c];
        k->style = st;
        for (int i = k->first_sib; i >= 0; i = t->comp[i].next_sib) style_made(t, i)->parent = k->style;
    }
    return k->style;
}

/* the constructor's loop: a component argument's style is parented to the
 * translation's (getChatStyle, made here if need be) */
void chat_arg(struct chat_comp *t, int tr, int c)
{
    struct cc_comp *k = &t->comp[tr];
    if (k->narg >= CC_MAX_ARGS) cc_fail("too many arguments");
    k->arg[k->narg] = (int16_t)c;
    k->arg_raw[k->narg] = -1;
    ++k->narg;
    int s = chat_style_of(t, c);
    t->style[s].parent = (int16_t)chat_style_of(t, tr);
}

void chat_arg_raw(struct chat_comp *t, int tr, const char *value)
{
    struct cc_comp *k = &t->comp[tr];
    if (k->narg >= CC_MAX_ARGS) cc_fail("too many arguments");
    k->arg[k->narg] = -1;
    k->arg_raw[k->narg] = pool_add(t, value);
    ++t->comp[tr].narg;
}

void chat_set_style(struct chat_comp *t, int c, int style)
{
    struct cc_comp *k = &t->comp[c];
    k->style = (int16_t)style;
    for (int i = k->first_sib; i >= 0; i = t->comp[i].next_sib) style_made(t, i)->parent = (int16_t)style;
    /* ChatComponentTranslation.setChatStyle: the component arguments too
     * (its children are made from the style at iteration time) */
    if (k->kind == CC_TRANSLATION)
        for (int i = 0; i < k->narg; ++i)
            if (k->arg[i] >= 0) t->style[chat_style_of(t, k->arg[i])].parent = (int16_t)style;
}

void chat_append(struct chat_comp *t, int c, int sib)
{
    int s = chat_style_of(t, sib);
    t->style[s].parent = (int16_t)chat_style_of(t, c);
    struct cc_comp *k = &t->comp[c];
    if (k->last_sib >= 0) t->comp[k->last_sib].next_sib = (int16_t)sib;
    else k->first_sib = (int16_t)sib;
    k->last_sib = (int16_t)sib;
    ++k->nsib;
}

void chat_append_text(struct chat_comp *t, int c, const char *text)
{
    chat_append(t, c, chat_text(t, text));
}

void chat_set_color(struct chat_comp *t, int c, int color)
{
    t->style[chat_style_of(t, c)].color = (int8_t)color;
}

void chat_set_click(struct chat_comp *t, int c, int action, const char *value)
{
    struct cc_style *st = &t->style[chat_style_of(t, c)];
    st->click = (int8_t)action;
    st->click_value = pool_add(t, value);
}

void chat_set_hover(struct chat_comp *t, int c, int action, int value)
{
    struct cc_style *st = &t->style[chat_style_of(t, c)];
    st->hover = (int8_t)action;
    st->hover_value = (int16_t)value;
}

/* createCopy: a new component of the same text or key and arguments, its
 * style a shallow copy (every own field and the parent). The game's copies
 * (StatBase.func_150951_e's statName) have no siblings or component
 * arguments; a deeper copy is not needed and stops the program. */
int chat_copy(struct chat_comp *t, int c)
{
    const struct cc_comp *k = &t->comp[c];
    if (k->nsib > 0) cc_fail("a copy of a component with siblings");
    for (int i = 0; i < k->narg; ++i)
        if (k->arg[i] >= 0) cc_fail("a copy of a component argument");
    int16_t str = k->str;
    int kind = k->kind, narg = k->narg;
    int n = new_comp(t, kind, "");
    t->npool -= 1;              /* the empty string: the copy shares the source's */
    struct cc_comp *d = &t->comp[n];
    d->str = str;
    d->narg = (int8_t)narg;
    for (int i = 0; i < narg; ++i)
    {
        d->arg[i] = -1;
        d->arg_raw[i] = t->comp[c].arg_raw[i];
    }
    int from = chat_style_of(t, c);
    int s = new_style(t);
    t->style[s] = t->style[from];
    chat_set_style(t, n, s);
    return n;
}

int chat_style_empty(const struct chat_comp *t, int style)
{
    if (style < 0) return 1;
    const struct cc_style *s = &t->style[style];
    return s->bold < 0 && s->italic < 0 && s->strikethrough < 0 && s->underlined < 0 && s->obfuscated < 0 &&
           s->color < 0 && s->click == CC_CLICK_NONE && s->hover == CC_HOVER_NONE;
}

/* ChatStyle's getters: the own field, else the parent's, the root answering
 * null or false. A chain longer than the slots is a cycle (Java would
 * overflow its stack). */
static int resolve(const struct chat_comp *t, int style, size_t field)
{
    for (int n = 0; style >= 0; ++n)
    {
        if (n > CC_MAX_STYLE) cc_fail("a style parent cycle");
        int8_t v = *((const int8_t *)&t->style[style] + field);
        if (v >= 0) return v;
        style = t->style[style].parent;
    }
    return -1;
}

void chat_style_code(const struct chat_comp *t, int style, char *out)
{
    int n = 0;
    int color = resolve(t, style, offsetof(struct cc_style, color));
    if (color >= 0) n += chat_fmt_string(color, out + n);
    if (resolve(t, style, offsetof(struct cc_style, bold)) == 1) n += chat_fmt_string(CF_BOLD, out + n);
    if (resolve(t, style, offsetof(struct cc_style, italic)) == 1) n += chat_fmt_string(CF_ITALIC, out + n);
    if (resolve(t, style, offsetof(struct cc_style, underlined)) == 1) n += chat_fmt_string(CF_UNDERLINE, out + n);
    if (resolve(t, style, offsetof(struct cc_style, obfuscated)) == 1) n += chat_fmt_string(CF_OBFUSCATED, out + n);
    if (resolve(t, style, offsetof(struct cc_style, strikethrough)) == 1)
        n += chat_fmt_string(CF_STRIKETHROUGH, out + n);
    out[n] = 0;
}

/* ------------------------------------------------------------ the writer */

struct wbuf
{
    char *p;
    size_t n, cap;
    int over;
};

static void put(struct wbuf *w, const char *s, size_t n)
{
    if (w->n + n + 1 > w->cap) { w->over = 1; return; }
    memcpy(w->p + w->n, s, n);
    w->n += n;
    w->p[w->n] = 0;
}

static void puts_(struct wbuf *w, const char *s)
{
    put(w, s, strlen(s));
}

/* Gson's JsonWriter.string with htmlSafe (GsonBuilder's default): quotes,
 * backslashes and control characters escaped, and < > & = ' and the
 * two line separators as \u escapes. */
static void put_json_string(struct wbuf *w, const char *s)
{
    put(w, "\"", 1);
    for (const unsigned char *c = (const unsigned char *)s; *c; ++c)
    {
        char esc[8];
        const char *r = NULL;
        switch (*c)
        {
            case '"': r = "\\\""; break;
            case '\\': r = "\\\\"; break;
            case '\t': r = "\\t"; break;
            case '\b': r = "\\b"; break;
            case '\n': r = "\\n"; break;
            case '\r': r = "\\r"; break;
            case '\f': r = "\\f"; break;
            case '<': case '>': case '&': case '=': case '\'':
                snprintf(esc, sizeof esc, "\\u%04x", *c);
                r = esc;
                break;
            default:
                if (*c < 0x20) { snprintf(esc, sizeof esc, "\\u%04x", *c); r = esc; }
                else if (*c == 0xe2 && c[1] == 0x80 && (c[2] == 0xa8 || c[2] == 0xa9))
                {
                    r = c[2] == 0xa8 ? "\\u2028" : "\\u2029";
                    c += 2;
                }
                break;
        }
        if (r) puts_(w, r);
        else put(w, (const char *)c, 1);
    }
    put(w, "\"", 1);
}

/* The serializer's work list: a literal, a pool string (a JSON string) or a
 * component to expand. A component expands into its own list, pushed in
 * reverse so it is written in order. */
enum { SJ_LIT, SJ_STR, SJ_COMP };
struct sj_task { int8_t kind; int16_t v; const char *lit; };
#define SJ_STACK (4 * CC_MAX_COMP + 64)
#define SJ_SEQ (2 * CC_MAX_COMP + 40)

static void sj_seq(struct sj_task *seq, int *n, int kind, int v, const char *lit)
{
    if (*n >= SJ_SEQ) cc_fail("a component's serialization is too long");
    seq[*n].kind = (int8_t)kind;
    seq[*n].v = (int16_t)v;
    seq[*n].lit = lit;
    ++*n;
}

/* IChatComponent.Serializer.serialize over one component: the list its
 * JSON is made of (ChatStyle.Serializer's keys, extra, then text, or
 * translate and with). */
static int sj_expand(const struct chat_comp *t, int c, struct sj_task *seq)
{
    const struct cc_comp *k = &t->comp[c];
    int n = 0;

    if (k->kind == CC_TEXT && chat_style_empty(t, k->style) && k->nsib == 0)
    {
        sj_seq(seq, &n, SJ_STR, k->str, NULL);
        return n;
    }

    sj_seq(seq, &n, SJ_LIT, 0, "{");
    const char *comma = "";
    if (!chat_style_empty(t, k->style))
    {
        const struct cc_style *s = &t->style[k->style];
        static const char *const bools[5][2] = {
            {"\"bold\":false", "\"bold\":true"}, {"\"italic\":false", "\"italic\":true"},
            {"\"underlined\":false", "\"underlined\":true"},
            {"\"strikethrough\":false", "\"strikethrough\":true"},
            {"\"obfuscated\":false", "\"obfuscated\":true"},
        };
        const int8_t v[5] = {s->bold, s->italic, s->underlined, s->strikethrough, s->obfuscated};
        for (int i = 0; i < 5; ++i)
            if (v[i] >= 0)
            {
                sj_seq(seq, &n, SJ_LIT, 0, comma);
                sj_seq(seq, &n, SJ_LIT, 0, bools[i][v[i]]);
                comma = ",";
            }
        if (s->color >= 0)
        {
            /* Gson's EnumTypeAdapterFactory: the constant's lower-case name */
            sj_seq(seq, &n, SJ_LIT, 0, comma);
            sj_seq(seq, &n, SJ_LIT, 0, "\"color\":\"");
            sj_seq(seq, &n, SJ_LIT, 0, chat_fmt_friendly(s->color));
            sj_seq(seq, &n, SJ_LIT, 0, "\"");
            comma = ",";
        }
        if (s->click == CC_CLICK_SUGGEST_COMMAND)
        {
            sj_seq(seq, &n, SJ_LIT, 0, comma);
            sj_seq(seq, &n, SJ_LIT, 0, "\"clickEvent\":{\"action\":\"suggest_command\",\"value\":");
            sj_seq(seq, &n, SJ_STR, s->click_value, NULL);
            sj_seq(seq, &n, SJ_LIT, 0, "}");
            comma = ",";
        }
        if (s->hover == CC_HOVER_SHOW_ACHIEVEMENT)
        {
            sj_seq(seq, &n, SJ_LIT, 0, comma);
            sj_seq(seq, &n, SJ_LIT, 0, "\"hoverEvent\":{\"action\":\"show_achievement\",\"value\":");
            sj_seq(seq, &n, SJ_COMP, s->hover_value, NULL);
            sj_seq(seq, &n, SJ_LIT, 0, "}");
            comma = ",";
        }
    }
    if (k->nsib > 0)
    {
        sj_seq(seq, &n, SJ_LIT, 0, comma);
        sj_seq(seq, &n, SJ_LIT, 0, "\"extra\":[");
        for (int i = k->first_sib; i >= 0; i = t->comp[i].next_sib)
        {
            if (i != k->first_sib) sj_seq(seq, &n, SJ_LIT, 0, ",");
            sj_seq(seq, &n, SJ_COMP, i, NULL);
        }
        sj_seq(seq, &n, SJ_LIT, 0, "]");
        comma = ",";
    }
    sj_seq(seq, &n, SJ_LIT, 0, comma);
    if (k->kind == CC_TEXT)
    {
        sj_seq(seq, &n, SJ_LIT, 0, "\"text\":");
        sj_seq(seq, &n, SJ_STR, k->str, NULL);
    }
    else
    {
        sj_seq(seq, &n, SJ_LIT, 0, "\"translate\":");
        sj_seq(seq, &n, SJ_STR, k->str, NULL);
        if (k->narg > 0)
        {
            sj_seq(seq, &n, SJ_LIT, 0, ",\"with\":[");
            for (int i = 0; i < k->narg; ++i)
            {
                if (i) sj_seq(seq, &n, SJ_LIT, 0, ",");
                if (k->arg[i] >= 0) sj_seq(seq, &n, SJ_COMP, k->arg[i], NULL);
                else sj_seq(seq, &n, SJ_STR, k->arg_raw[i], NULL);
            }
            sj_seq(seq, &n, SJ_LIT, 0, "]");
        }
    }
    sj_seq(seq, &n, SJ_LIT, 0, "}");
    return n;
}

int chat_json(const struct chat_comp *t, int c, char *out, size_t cap)
{
    struct wbuf w = {out, 0, cap, 0};
    struct sj_task *stack ENV_LOCAL = envstack_take(SJ_STACK * sizeof *stack);
    struct sj_task *seq ENV_LOCAL = envstack_take(SJ_SEQ * sizeof *seq);
    int sp = 0;

    if (cap == 0) return -1;
    out[0] = 0;
    stack[sp++] = (struct sj_task){SJ_COMP, (int16_t)c, NULL};
    while (sp > 0)
    {
        struct sj_task k = stack[--sp];
        if (k.kind == SJ_LIT) puts_(&w, k.lit);
        else if (k.kind == SJ_STR) put_json_string(&w, &t->pool[k.v]);
        else
        {
            int n = sj_expand(t, k.v, seq);
            if (sp + n > SJ_STACK) cc_fail("the serializer's stack is full");
            for (int i = n - 1; i >= 0; --i) stack[sp++] = seq[i];
        }
    }
    return w.over ? -1 : (int)w.n;
}

/* ------------------------------------------------------------- the parts */

/* The walk's work list: a component to iterate, or a part to write (its
 * text a pool string or a span of a translation's format string, and the
 * style its formatting code resolves). */
enum { PW_COMP, PW_POOL, PW_SPAN };
struct pw_task { int8_t kind; int16_t style; int16_t v; const char *span; int16_t len; };
#define PW_STACK (4 * CC_MAX_COMP + 64)
#define PW_SEQ (2 * CC_MAX_COMP + 24)

static void pw_seq(struct pw_task *seq, int *n, int kind, int style, int v, const char *span, int len)
{
    if (*n >= PW_SEQ) cc_fail("a component's parts are too many");
    seq[*n] = (struct pw_task){(int8_t)kind, (int16_t)style, (int16_t)v, span, (int16_t)len};
    ++*n;
}

/* One component's iterator(): a text is itself then its siblings; a
 * translation is its children (ChatComponentTranslation.initializeFromFormat
 * over the translated string: the spans between placeholders and a "%%" as
 * texts parented to its style, each %s or %n$s its argument, a component
 * as itself, any other Object as a text of its String.valueOf) then its
 * siblings. Only %s is supported, as there. */
static int pw_expand(const struct chat_comp *t, int c, struct pw_task *seq)
{
    const struct cc_comp *k = &t->comp[c];
    int n = 0;

    if (k->kind == CC_TEXT) pw_seq(seq, &n, PW_POOL, k->style, k->str, NULL, 0);
    else
    {
        const char *f = chat_translate(&t->pool[k->str]);
        int len = (int)strlen(f), pos = 0, next = 0;
        /* stringVariablePattern, %(?:(\d+)\$)?([A-Za-z%]|$), from pos */
        for (int i = 0; i <= len; ++i)
        {
            if (i == len || f[i] != '%') continue;
            int j = i + 1, idx = -1;
            if (f[j] >= '0' && f[j] <= '9')
            {
                int d = j, v = 0;
                while (f[d] >= '0' && f[d] <= '9') v = v * 10 + (f[d++] - '0');
                if (f[d] == '$') { j = d + 1; idx = v - 1; }
            }
            char conv = f[j];
            int is_end = j == len;
            if (!is_end && !((conv >= 'A' && conv <= 'Z') || (conv >= 'a' && conv <= 'z') || conv == '%'))
                continue;
            int end = is_end ? j : j + 1;
            if (i > pos) pw_seq(seq, &n, PW_SPAN, k->style, 0, f + pos, i - pos);
            if (!is_end && conv == '%' && end - i == 2) pw_seq(seq, &n, PW_SPAN, k->style, 0, "%", 1);
            else
            {
                if (is_end || conv != 's') cc_fail("an unsupported format");
                int a = idx >= 0 ? idx : next++;
                if (a >= k->narg) cc_fail("a format argument past the arguments");
                if (k->arg[a] >= 0) pw_seq(seq, &n, PW_COMP, 0, k->arg[a], NULL, 0);
                else pw_seq(seq, &n, PW_POOL, k->style, k->arg_raw[a], NULL, 0);
            }
            pos = end;
            i = end - 1;
        }
        if (pos < len) pw_seq(seq, &n, PW_SPAN, k->style, 0, f + pos, len - pos);
    }
    for (int i = k->first_sib; i >= 0; i = t->comp[i].next_sib) pw_seq(seq, &n, PW_COMP, 0, i, NULL, 0);
    return n;
}

int chat_parts(const struct chat_comp *t, int c, char *out, size_t cap, size_t *used)
{
    struct wbuf w = {out, 0, cap, 0};
    struct pw_task *stack ENV_LOCAL = envstack_take(PW_STACK * sizeof *stack);
    struct pw_task *seq ENV_LOCAL = envstack_take(PW_SEQ * sizeof *seq);
    int sp = 0, nparts = 0;

    stack[sp++] = (struct pw_task){PW_COMP, 0, (int16_t)c, NULL, 0};
    while (sp > 0)
    {
        struct pw_task k = stack[--sp];
        if (k.kind == PW_COMP)
        {
            int n = pw_expand(t, k.v, seq);
            if (sp + n > PW_STACK) cc_fail("the parts walk's stack is full");
            for (int i = n - 1; i >= 0; --i) stack[sp++] = seq[i];
            continue;
        }
        char code[24];
        chat_style_code(t, k.style, code);
        put(&w, code, strlen(code) + 1);
        if (k.kind == PW_POOL) put(&w, &t->pool[k.v], strlen(&t->pool[k.v]) + 1);
        else
        {
            put(&w, k.span, (size_t)k.len);
            put(&w, "", 1);
        }
        /* ChatStyle.getChatClickEvent through the parents: the drawn
         * chat's click (GuiChat.mouseClicked) */
        const char *click = "";
        for (int st = k.style, n = 0; st >= 0 && n <= CC_MAX_STYLE; st = t->style[st].parent, ++n)
            if (t->style[st].click != CC_CLICK_NONE)
            {
                click = &t->pool[t->style[st].click_value];
                break;
            }
        put(&w, click, strlen(click) + 1);
        /* getChatHoverEvent through the parents: SHOW_ACHIEVEMENT's statId
         * (GuiChat.drawScreen's tooltip) */
        const char *hover = "";
        for (int st = k.style, n = 0; st >= 0 && n <= CC_MAX_STYLE; st = t->style[st].parent, ++n)
            if (t->style[st].hover != CC_HOVER_NONE)
            {
                hover = &t->pool[t->comp[t->style[st].hover_value].str];
                break;
            }
        put(&w, hover, strlen(hover) + 1);
        ++nparts;
    }
    *used = w.n;
    return w.over ? -1 : nparts;
}

/* -------------------------------------------------------- the en_US lang */

/* assets/minecraft/lang/en_US.lang, the keys the server's chat lines use
 * (StringTranslate's map: a later line wins, so entity.Arrow.name is
 * "arrow"). The achievements' names are survival.c's. */
static const char *const CHAT_KEYS[] = {
    "chat.type.achievement",
    "chat.type.text",
    "chat.type.emote",
    "commands.generic.notFound",
    "commands.generic.permission",
    "commands.generic.usage",
    "commands.generic.player.notFound",
    "commands.generic.num.invalid",
    "commands.generic.num.tooSmall",
    "commands.generic.num.tooBig",
    "commands.generic.exception",
    "commands.help.header",
    "commands.help.footer",
    "commands.seed.success",
    "commands.message.sameTarget",
    "commands.message.display.incoming",
    "commands.message.display.outgoing",
    "commands.time.usage",
    "commands.gamemode.usage",
    "commands.difficulty.usage",
    "commands.defaultgamemode.usage",
    "commands.kill.usage",
    "commands.downfall.usage",
    "commands.weather.usage",
    "commands.xp.usage",
    "commands.tp.usage",
    "commands.give.usage",
    "commands.effect.usage",
    "commands.enchant.usage",
    "commands.me.usage",
    "commands.seed.usage",
    "commands.help.usage",
    "commands.debug.usage",
    "commands.message.usage",
    "commands.say.usage",
    "commands.spawnpoint.usage",
    "commands.setworldspawn.usage",
    "commands.gamerule.usage",
    "commands.clear.usage",
    "commands.testfor.usage",
    "commands.spreadplayers.usage",
    "commands.playsound.usage",
    "commands.scoreboard.usage",
    "commands.achievement.usage",
    "commands.summon.usage",
    "commands.setblock.usage",
    "commands.testforblock.usage",
    "commands.tellraw.usage",
    "tile.bed.occupied",
    "tile.bed.noSleep",
    "tile.bed.notSafe",
    "tile.bed.notValid",
    "build.tooHigh",
    "death.fell.accident.ladder",
    "death.fell.accident.vines",
    "death.fell.accident.water",
    "death.fell.accident.generic",
    "death.fell.killer",
    "death.fell.assist",
    "death.fell.assist.item",
    "death.fell.finish",
    "death.fell.finish.item",
    "death.attack.inFire",
    "death.attack.inFire.player",
    "death.attack.onFire",
    "death.attack.onFire.player",
    "death.attack.lava",
    "death.attack.lava.player",
    "death.attack.inWall",
    "death.attack.drown",
    "death.attack.drown.player",
    "death.attack.starve",
    "death.attack.cactus",
    "death.attack.cactus.player",
    "death.attack.generic",
    "death.attack.explosion",
    "death.attack.explosion.player",
    "death.attack.magic",
    "death.attack.wither",
    "death.attack.anvil",
    "death.attack.fallingBlock",
    "death.attack.mob",
    "death.attack.player",
    "death.attack.player.item",
    "death.attack.arrow",
    "death.attack.arrow.item",
    "death.attack.fireball",
    "death.attack.fireball.item",
    "death.attack.thrown",
    "death.attack.thrown.item",
    "death.attack.indirectMagic",
    "death.attack.indirectMagic.item",
    "death.attack.thorns",
    "death.attack.fall",
    "death.attack.outOfWorld",
    "entity.Arrow.name",
    "entity.Snowball.name",
    "entity.Fireball.name",
    "entity.SmallFireball.name",
    "entity.Creeper.name",
    "entity.Skeleton.name",
    "entity.Spider.name",
    "entity.Zombie.name",
    "entity.Slime.name",
    "entity.Ghast.name",
    "entity.PigZombie.name",
    "entity.Enderman.name",
    "entity.Silverfish.name",
    "entity.CaveSpider.name",
    "entity.Blaze.name",
    "entity.LavaSlime.name",
    "entity.MushroomCow.name",
    "entity.Villager.name",
    "entity.VillagerGolem.name",
    "entity.EnderDragon.name",
    "entity.Witch.name",
    "entity.Pig.name",
    "entity.Sheep.name",
    "entity.Cow.name",
    "entity.Chicken.name",
    "entity.Squid.name",
    "entity.Bat.name",
    "entity.generic.name",
};

const char *chat_translate(const char *key)
{
    for (size_t i = 0; i < sizeof CHAT_KEYS / sizeof CHAT_KEYS[0]; ++i)
        if (!strcmp(CHAT_KEYS[i], key)) return lang_text(key);
    const char *a = surv_ach_lang(key);
    return a ? a : key;
}

int chat_can_translate(const char *key)
{
    return chat_translate(key) != key;
}

/* ------------------------------------------------------------------ S02 */

#define S02_SIDE 1536

void chat_s02_send(struct s2c_queue *q, const struct chat_comp *t, int c)
{
    char buf[S02_SIDE];
    int n = chat_json(t, c, buf, sizeof buf);
    if (n < 0) cc_fail("an S02's JSON is too long");
    size_t used = 0;
    int nparts = chat_parts(t, c, buf + n + 1, sizeof buf - (size_t)n - 1, &used);
    if (nparts < 0) cc_fail("an S02's parts are too long");
    size_t bytes = (size_t)n + 1 + used;
    if (bytes > S2C_SIDE_MAX_PKT) cc_fail("an S02 over the packet's side part");

    struct s2c_pkt *pkt = s2c_add(q);
    pkt->kind = PK_S02;
    pkt->i0 = nparts;
    memcpy(s2c_side_new(q, pkt, bytes), buf, bytes);
}

const char *chat_s02_json(const struct s2c_queue *q, const struct s2c_pkt *pkt)
{
    return s2c_side(q, pkt);
}

int chat_s02_check(const struct jval *row, const struct s2c_queue *q, char *why, size_t n)
{
    const struct jval *want = json_get(row, "s02");
    int nwant = want ? json_len(want) : 0, k = 0;

    for (int i = 0; q && i < q->n; ++i)
    {
        const struct s2c_pkt *pkt = &q->q[i];
        if (pkt->kind != PK_S02) continue;
        const char *got = chat_s02_json(q, pkt);
        if (k >= nwant)
        {
            snprintf(why, n, "s02[%d] want none got %s", k, got);
            return 1;
        }
        const struct jval *w = json_at(want, k);
        const char *wj = json_str(json_at(w, 0));
        if (!wj || strcmp(wj, got))
        {
            snprintf(why, n, "s02[%d] want %s got %s", k, wj ? wj : "?", got);
            return 1;
        }
        const struct jval *parts = json_at(w, 1);
        const char *s = got + strlen(got) + 1;
        if (json_len(parts) != pkt->i0)
        {
            snprintf(why, n, "s02[%d] %s: want %d parts got %d", k, got, json_len(parts), pkt->i0);
            return 1;
        }
        for (int j = 0; j < pkt->i0; ++j)
        {
            const char *code = s, *text = s + strlen(s) + 1;
            s = text + strlen(text) + 1;
            s += strlen(s) + 1;   /* the part's click value and hover, which the row does not carry */
            s += strlen(s) + 1;
            const char *wc = json_str(json_at(json_at(parts, j), 0));
            const char *wt = json_str(json_at(json_at(parts, j), 1));
            if (!wc || !wt || strcmp(wc, code) || strcmp(wt, text))
            {
                snprintf(why, n, "s02[%d] part %d want [%s|%s] got [%s|%s]", k, j, wc ? wc : "?",
                         wt ? wt : "?", code, text);
                return 1;
            }
        }
        ++k;
    }
    if (k < nwant)
    {
        snprintf(why, n, "s02[%d] want %s got none", k, json_str(json_at(json_at(want, k), 0)));
        return 1;
    }
    return 0;
}
