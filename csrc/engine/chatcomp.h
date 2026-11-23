/* The server's chat components (net.minecraft.util: IChatComponent,
 * ChatComponentStyle, ChatComponentText, ChatComponentTranslation, ChatStyle,
 * EnumChatFormatting) and the S02PacketChat that carries one.
 *
 * A component tree is built into one fixed struct chat_comp: components and
 * styles are slots in its arrays, the strings live in its pool, and a style's
 * parent is another style slot, the way the Java objects share ChatStyle
 * instances (StatBase.func_150955_j gives the "[" text its translation's
 * own style). Two things are read off a tree, both without recursion:
 *   - its wire form, IChatComponent.Serializer.func_150696_a (Gson's JSON,
 *     what S02PacketChat.writePacketData sends), and
 *   - the parts GuiNewChat.func_146237_a iterates it into: every component
 *     of IChatComponent.iterator() (a translation's children from
 *     initializeFromFormat over the en_US string) with its style's
 *     getFormattingCode (the parent chain resolved, as createDeepCopy does)
 *     and its getUnformattedTextForChat.
 * chat_s02_send queues both on the server's packet queue as one S02; the
 * row check compares them with the oracle's "s02" row field. */
#ifndef NETHERITE_CHATCOMP_H
#define NETHERITE_CHATCOMP_H

#include <stddef.h>
#include <stdint.h>

/* EnumChatFormatting, in its declaration order. */
enum
{
    CF_BLACK, CF_DARK_BLUE, CF_DARK_GREEN, CF_DARK_AQUA, CF_DARK_RED, CF_DARK_PURPLE, CF_GOLD,
    CF_GRAY, CF_DARK_GRAY, CF_BLUE, CF_GREEN, CF_AQUA, CF_RED, CF_LIGHT_PURPLE, CF_YELLOW, CF_WHITE,
    CF_OBFUSCATED, CF_BOLD, CF_STRIKETHROUGH, CF_UNDERLINE, CF_ITALIC, CF_RESET, CF_COUNT
};

/* EnumChatFormatting.getFormattingCode, getFriendlyName (the lower-case
 * name Gson's EnumTypeAdapterFactory writes) and toString (the section sign
 * and the code, UTF-8, into out, at least 4 bytes; the length). Its
 * getTextWithoutFormattingCodes is chat.c's strip_codes (the chat's
 * colours-off path). */
char chat_fmt_code(int f);
const char *chat_fmt_friendly(int f);
int chat_fmt_string(int f, char *out);

/* ClickEvent.Action and HoverEvent.Action, the ones this game's components
 * carry: SUGGEST_COMMAND (EntityPlayer.func_145748_c_) and SHOW_ACHIEVEMENT
 * (StatBase.func_150951_e). */
enum { CC_CLICK_NONE, CC_CLICK_SUGGEST_COMMAND };
enum { CC_HOVER_NONE, CC_HOVER_SHOW_ACHIEVEMENT };

#define CC_MAX_COMP 160
#define CC_MAX_STYLE 160
#define CC_MAX_ARGS 4
#define CC_POOL 2048

/* ChatStyle: each Boolean is -1 (null), 0 or 1; color -1 is null; parent -1
 * is null (the root style's answers). */
struct cc_style
{
    int8_t color, bold, italic, underlined, strikethrough, obfuscated;
    int8_t click, hover;
    int16_t click_value;        /* pool offset of the ClickEvent's value */
    int16_t hover_value;        /* the HoverEvent's component */
    int16_t parent;
};

/* One component: ChatComponentText (text) or ChatComponentTranslation (key
 * and formatArgs). style -1 is a style not made yet (getChatStyle makes an
 * empty one). An argument is a component, or (raw) a plain Object whose
 * String.valueOf is the pool string. The siblings are a list: first_sib,
 * then each one's next_sib, -1 ending it. */
enum { CC_TEXT, CC_TRANSLATION };
struct cc_comp
{
    uint8_t kind;
    int8_t narg;
    int16_t style;
    int16_t str;                /* the text, or the translation key */
    int16_t nsib, first_sib, last_sib, next_sib;
    int16_t arg[CC_MAX_ARGS];
    int16_t arg_raw[CC_MAX_ARGS];   /* >= 0: the raw argument's pool string */
};

struct chat_comp
{
    int ncomp, nstyle, npool;
    struct cc_comp comp[CC_MAX_COMP];
    struct cc_style style[CC_MAX_STYLE];
    char pool[CC_POOL];
};

/* An empty tree. A tree is large (the chat's longest line, a /me of fifty
 * words, is a hundred components): take it from the env's scratch stack
 * (chat_comp_new) rather than a stack frame. */
void chat_comp_init(struct chat_comp *t);
#define chat_comp_new() chat_comp_take()
struct chat_comp *chat_comp_take(void);
/* new ChatComponentText(text): the component's slot. */
int chat_text(struct chat_comp *t, const char *text);
/* new ChatComponentTranslation(key, args...): the arguments set after it
 * with chat_arg / chat_arg_raw, in order (the constructor parents each
 * component argument's style to the translation's). */
int chat_translation(struct chat_comp *t, const char *key);
void chat_arg(struct chat_comp *t, int tr, int c);
void chat_arg_raw(struct chat_comp *t, int tr, const char *value);
/* ChatComponentStyle.getChatStyle: the component's style slot, made empty
 * the first time (its siblings parented to it). */
int chat_style_of(struct chat_comp *t, int c);
/* ChatComponentStyle.setChatStyle (ChatComponentTranslation's also
 * re-parents its component arguments). */
void chat_set_style(struct chat_comp *t, int c, int style);
/* ChatComponentStyle.appendSibling and appendText. */
void chat_append(struct chat_comp *t, int c, int sib);
void chat_append_text(struct chat_comp *t, int c, const char *text);
/* ChatStyle's setters on a component's style. */
void chat_set_color(struct chat_comp *t, int c, int color);
void chat_set_click(struct chat_comp *t, int c, int action, const char *value);
void chat_set_hover(struct chat_comp *t, int c, int action, int value);
/* ChatComponentText.createCopy (the style shallow-copied, the siblings
 * copied) and ChatComponentTranslation.createCopy (the arguments copied
 * when they are components): the copy's slot. */
int chat_copy(struct chat_comp *t, int c);

/* ChatStyle.isEmpty over a style slot (-1, a style not made, is empty). */
int chat_style_empty(const struct chat_comp *t, int style);
/* ChatStyle.getFormattingCode of the style's deep copy: the colour and the
 * bold, italic, underline, obfuscated, strikethrough codes the parent chain
 * resolves (ChatStyle's getters), into out (at least 16 bytes). */
void chat_style_code(const struct chat_comp *t, int style, char *out);

/* IChatComponent.Serializer.func_150696_a of component c into out (cap
 * bytes); the length, or -1 when it does not fit. */
int chat_json(const struct chat_comp *t, int c, char *out, size_t cap);

/* The parts GuiNewChat iterates component c into (IChatComponent.iterator
 * over the en_US strings): each part's formatting code, text, click value
 * (its style's SUGGEST_COMMAND through the parents, "" none) and hover (its
 * SHOW_ACHIEVEMENT's statId, "" none), written
 * NUL-terminated one after the other into out; the part count, or -1 when
 * out is too small. */
int chat_parts(const struct chat_comp *t, int c, char *out, size_t cap, size_t *used);

/* StatCollector.translateToLocal over the en_US strings the server's chat
 * lines use (the key itself when missing) and StatCollector.canTranslate. */
const char *chat_translate(const char *key);
int chat_can_translate(const char *key);

/* The S02's out-of-line part: the JSON, then the parts (formatting code,
 * text, click value and hover each), all NUL-terminated; the packet's i0 is
 * the part count. */
struct s2c_queue;
struct s2c_pkt;
/* NetHandlerPlayServer.sendPacket(new S02PacketChat(component c)) on the
 * server's queue q: the packet, its side part. */
void chat_s02_send(struct s2c_queue *q, const struct chat_comp *t, int c);
/* A queued S02's JSON, and its parts one after the other. */
const char *chat_s02_json(const struct s2c_queue *q, const struct s2c_pkt *pkt);

/* The row check: a tape row's "s02" field (the S02s the oracle's server
 * tick sent, each [json, [[code, text], ...]]; absent is none) against the
 * S02s on queue q, in order. 0 when they agree, else 1 with the first
 * difference in why. */
struct jval;
int chat_s02_check(const struct jval *row, const struct s2c_queue *q, char *why, size_t n);

#endif
