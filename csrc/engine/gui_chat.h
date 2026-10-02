/* The chat screen's client half: GuiChat (and GuiSleepMP, which extends it)
 * over its GuiTextField, and GuiNewChat's sent-message history, as one
 * tick's batch of raw keyboard and mouse events runs through them
 * (GuiScreen.handleInput: every mouse event, then every key). The text is
 * Java's: UTF-16 units, every index a char index. What the screen sends
 * (the C01 of a message, the C14 of a tab completion) goes to the client's
 * chat outbox; what only the drawn chat changes (its scroll, the tab
 * completion's list line) goes to the frame's (struct chat_fx). The batch
 * is the tape's ["chat", mods, px, py, clip, comp, events] op
 * (oracle/harness/netherite/oracle/ChatInput.java). */
#ifndef NETHERITE_GUI_CHAT_H
#define NETHERITE_GUI_CHAT_H

#include <stdint.h>

#define GC_MAX 100          /* GuiChat.initGui's func_146203_f(100) */
#define GC_HIST 64          /* the sent history's newest kept */
#define GC_TABS 64          /* a completion list's entries kept */
#define GC_TAB_LEN 40       /* UTF-16 units of one entry */
#define GC_OUT 8            /* C01s and C14s one tick may send */
#define GC_OUT_LEN 304      /* UTF-8 bytes of one (100 units and the NUL) */
/* The oracle's display (854 by 480) and the scaled screen its GUI scale 2
 * makes: the chat screen's layout and its event coordinates. */
#define GC_DISPLAY_W 854
#define GC_DISPLAY_H 480
#define GC_SCREEN_W 427
#define GC_SCREEN_H 240

/* GuiTextField, as GuiChat.initGui makes it: at (4, height - 12), width -
 * 4 by 12, no background, focused, 100 long, enabled. */
struct gui_text_field
{
    uint16_t text[GC_MAX];      /* field_146216_j */
    int len, max;               /* its length, field_146217_k */
    int counter;                /* field_146214_l: the cursor's blink clock */
    int focused;                /* field_146213_o */
    int scroll;                 /* field_146225_q: the first char drawn */
    int cursor;                 /* field_146224_r */
    int sel;                    /* field_146223_s: the selection's other end */
    int x, y, w, h;             /* field_146209_f, field_146210_g, field_146218_h, field_146219_i */
};

/* One tick's chat batch (the tape op). */
#define CHAT_OP_EV 128
enum { CHAT_EV_KEY, CHAT_EV_PRESS, CHAT_EV_RELEASE, CHAT_EV_WHEEL };
struct chat_ev
{
    int8_t kind;
    int8_t button;
    uint16_t ch;                /* a key's char (Keyboard.getEventCharacter) */
    int32_t code;               /* a key's LWJGL code, a wheel's turn */
    int32_t x, y;               /* a press or release, display pixels, y up */
};
struct chat_op
{
    int mods;                   /* 1 shift, 2 control, as the poll left them */
    int px, py;                 /* Mouse.getX, getY */
    int has_clip;               /* the clipboard at the batch's start */
    uint16_t clip[GC_MAX];      /* its first GC_MAX allowed units (all a paste can take) */
    int clip_len;
    int has_comp;               /* the SUGGEST_COMMAND component a left press finds */
    char comp_value[GC_OUT_LEN];
    char comp_text[GC_OUT_LEN]; /* its getUnformattedTextForChat, UTF-8 */
    int nev;
    struct chat_ev ev[CHAT_OP_EV];
};

/* What the screen sends, in send order: C01PacketChatMessage (0) and
 * C14PacketTabComplete (1), each its string in UTF-8. */
struct chat_out
{
    int n;
    int kind[GC_OUT];
    char text[GC_OUT][GC_OUT_LEN];
};

/* The drawn chat's side of the batch, in order: GuiNewChat.func_146229_b's
 * scroll turns (CHAT_FX_RESET for resetScroll). The tab completion's list
 * line (func_146234_a with id 1) joins the client's S02 lines
 * (client_player.s02_*), in the order the pump and the batch print. */
#define CHAT_FX_RESET INT32_MIN
#define CHAT_FX_MAX 16
struct chat_fx
{
    int nscroll;
    int32_t scroll[CHAT_FX_MAX];
};

/* GuiNewChat.field_146248_g: n messages sent in all, the newest GC_HIST
 * kept at s[i % GC_HIST]. */
struct chat_sent
{
    int n;
    uint16_t s[GC_HIST][GC_MAX];
    uint8_t len[GC_HIST];
};

struct gui_chat
{
    int up;                     /* the GuiChat object this state is (a closed one keeps it) */
    int sleep;                  /* it is a GuiSleepMP */
    struct gui_text_field f;    /* field_146415_a */
    uint16_t saved[GC_MAX];     /* field_146410_g: the line the history left */
    int saved_len;
    int hist_idx;               /* field_146416_h */
    int completing;             /* field_146417_i */
    int waiting;                /* field_146414_r: a C14 is out */
    int ntab, tab_idx;          /* field_146412_t, field_146413_s */
    uint16_t tab[GC_TABS][GC_TAB_LEN];
    uint8_t tab_len[GC_TABS];
};

/* GuiScreen.getClipboardString and setClipboardString's system clipboard,
 * as far as a paste can read it (its first GC_MAX allowed chars). */
struct chat_clip
{
    uint16_t s[GC_MAX];
    int n;
};

struct client_player;
struct client_out;

/* GuiChat.initGui over PRESET (UTF-8: "" for key.chat, "/" for
 * key.command), SLEEP for GuiSleepMP, at the scaled screen's WIDTH and
 * HEIGHT. */
void gui_chat_open(struct client_player *p, const char *preset, int sleep, int width, int height);

/* GuiScreen.handleInput over the open chat screen with the tick's batch:
 * the history, the text, the tab completion and what they send; a key that
 * closes the screen (Enter, Escape) sets *close, and the rest of the batch
 * still runs on the closed GuiChat, as vanilla's loop does. GuiSleepMP's
 * Escape and Leave Bed set *wake. */
void gui_chat_run(struct client_player *p, const struct chat_op *op, int *close, int *wake);

/* GuiChat.updateScreen: the text field's blink clock. */
void gui_chat_update(struct client_player *p);

/* NetHandlerPlayClient.handleTabComplete with a GuiChat up:
 * func_146406_a over the S3A's N strings (UTF-8). */
void gui_chat_tab_reply(struct client_player *p, const char *const *items, int n);

/* GuiTextField.drawTextBox's inputs for the frame: the visible text (from
 * scroll, trimmed to the field's width) and its pieces around the cursor
 * and the selection (UTF-8), the offsets, whether the cursor is drawn now
 * (the blink) and as a bar (func_146208_g). */
struct gui_chat_draw
{
    char vis[GC_OUT_LEN];       /* the visible text */
    char pre[GC_OUT_LEN];       /* its chars before the cursor (the cursor on it) */
    char post[GC_OUT_LEN];      /* and from the cursor on */
    char sel_pre[GC_OUT_LEN];   /* its chars before the selection's end */
    int vis_len;                /* chars */
    int cursor, sel;            /* offsets into vis, as drawTextBox computes them */
    int on;                     /* var5: the cursor is on the visible run */
    int blink_on, bar;
    int x, y, w;                /* the field */
};
void gui_chat_draw_state(const struct client_player *p, struct gui_chat_draw *d);

/* FontRenderer.getStringWidth over UTF-16 units (FONT_CW, font_cw.h). */
int gui_chat_width(const uint16_t *s, int n);

/* UTF-8 to UTF-16 units (at most cap) and back (out at least 3 * n + 1). */
int gui_chat_utf16(const char *s, uint16_t *out, int cap);
/* The same, keeping only ChatAllowedCharacters' chars (a paste's filter). */
int gui_chat_utf16_allowed(const char *s, uint16_t *out, int cap);
int gui_chat_utf8(const uint16_t *s, int n, char *out);

#endif
