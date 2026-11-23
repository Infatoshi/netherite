/* GuiNewChat's messages and drawn lines. A message is its chat component
 * flattened the way func_146237_a iterates it: parts, each its style's
 * formatting code and its own text. The drawn lines are what func_146237_a
 * splits a message into at the chat's width, newest first, each carrying
 * the GuiIngame update counter the message arrived at (the 200-tick fade
 * counts from it). raster_hud.c draws them (func_146230_a). */
#ifndef NETHERITE_CHAT_H
#define NETHERITE_CHAT_H

#include "font.h"

#define CHAT_MAX 100 /* both lists keep their newest 100 */
#define CHAT_PARTS 128   /* a /me of fifty words is a hundred parts */

struct chat_message {
    int counter, id, nparts;
    char *fmt[CHAT_PARTS], *text[CHAT_PARTS];
    char *click[CHAT_PARTS];    /* the part's SUGGEST_COMMAND value, NULL none */
    char *hover[CHAT_PARTS];    /* its SHOW_ACHIEVEMENT statId, NULL none */
};

/* One piece of a drawn line: the ChatComponentText func_146237_a made (its
 * text with its formatting code in front, as the line holds it) and the
 * message part it came from. */
struct chat_piece {
    int line, part;
    char *text;
};

struct chat_line {
    int counter, id;
    char *text; /* getFormattedText of the line's component */
};

struct chat {
    struct chat_message msg[CHAT_MAX]; /* field_146252_h, newest first */
    int nmsg;
    struct chat_line line[CHAT_MAX];   /* field_146253_i, newest first */
    int nline;
};

/* GuiNewChat.func_146228_f / func_146246_g / func_146232_i: the chat's width,
 * height and line count in GUI pixels from the settings. */
int chat_width(float chat_width);
int chat_height(float height);

/* func_146237_a over one message: its lines at WIDTH (the chat width over
 * the scale, floored), top to bottom, malloc'd; COLOURS is chatColours. */
char **chat_split(const struct font_metrics *f, const struct chat_message *m, int width,
                  int colours, int *nlines);

/* GuiNewChat.func_146234_a: a new message at COUNTER, its lines split and put
 * in front, both lists capped at 100. The chat takes over m's strings. */
void chat_add(struct chat *c, const struct font_metrics *f, struct chat_message *m,
              int width, int colours);

/* The same split, as pieces: each drawn line's (top to bottom) pieces in
 * order into out (malloc'd texts, at most max), *npieces of them. */
void chat_split_pieces(const struct font_metrics *f, const struct chat_message *m, int width, int colours,
                       struct chat_piece *out, int max, int *npieces, int *nlines);

/* A message out of plain parts (copied): n parts of formatting code and text. */
void chat_message_set(struct chat_message *m, int counter, int id, int n,
                      const char *const *fmt, const char *const *text);
/* Part i's click value and hover (copied). */
void chat_message_set_click(struct chat_message *m, int i, const char *value);
void chat_message_set_hover(struct chat_message *m, int i, const char *value);

/* GuiNewChat.func_146242_c over the message list: the first message with
 * the id leaves (its lines with it). */
void chat_remove_id(struct chat *c, int id);

/* The message alone in front of the list (a client that re-splits at draw
 * time, as raster_hud does, keeps no lines); the chat takes m's strings. */
void chat_push_message(struct chat *c, struct chat_message *m);

void chat_free(struct chat *c);

#endif
