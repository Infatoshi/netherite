/* The server's half of the chat screen (chatcmd.h). Strings are UTF-8: every
 * rule here that Java applies per char (trim, split on a space, the ASCII
 * patterns of PlayerSelector) only ever tests ASCII, which UTF-8 never
 * hides inside a longer character. */
#include "chatcmd.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chatcomp.h"
#include "chatmsg.h"
#include "envstack.h"
#include "gui_chat.h"
#include "jmath.h"
#include "jorder.h"
#include "player.h"
#include "world.h"

#define CM_NAME "Player"   /* the oracle's player, chatmsg.c's */

/* ------------------------------------------------------------ strings */

#define WORDS 64
#define WORD_LEN 304

/* String.split(" ", limit): limit 0 drops the trailing empty strings,
 * -1 keeps them; a string with no space is itself */
static int jsplit(const char *s, int keep_trailing, char (*out)[WORD_LEN], int max)
{
    int n = 0;
    const char *p = s;

    for (;;)
    {
        const char *q = strchr(p, ' ');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (n < max)
        {
            if (len >= WORD_LEN) len = WORD_LEN - 1;
            memcpy(out[n], p, len);
            out[n][len] = 0;
            ++n;
        }
        if (!q) break;
        p = q + 1;
    }
    if (!keep_trailing)
        while (n > 1 && out[n - 1][0] == 0) --n;
    return n;
}

/* Character.toUpperCase and toLowerCase over the characters an ASCII name
 * can meet ignoring case: ASCII, the dotless i, the long s, the Kelvin sign,
 * the dotted capital I */
static uint32_t jupper(uint32_t c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c == 0x131) return 'I';
    if (c == 0x17f) return 'S';
    return c;
}

static uint32_t jlower(uint32_t c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c == 0x212a) return 'k';
    if (c == 0x130) return 'i';
    return c;
}

/* String.regionMatches(true, ...): one char pair */
static int fold_eq(uint16_t a, uint16_t b)
{
    if (a == b) return 1;
    uint32_t ua = jupper(a), ub = jupper(b);
    return ua == ub || jlower(ua) == jlower(ub);
}

/* CommandBase.doesStringStartWith(prefix, s): s starts with prefix,
 * ignoring case */
static int starts_with_ic(const char *prefix, const char *s)
{
    uint16_t a[WORD_LEN], b[WORD_LEN];
    int na = gui_chat_utf16(prefix, a, WORD_LEN), nb = gui_chat_utf16(s, b, WORD_LEN);

    if (na > nb) return 0;
    for (int i = 0; i < na; ++i)
        if (!fold_eq(b[i], a[i])) return 0;
    return 1;
}

/* String.equalsIgnoreCase */
static int equals_ic(const char *x, const char *y)
{
    uint16_t a[WORD_LEN], b[WORD_LEN];
    int na = gui_chat_utf16(x, a, WORD_LEN), nb = gui_chat_utf16(y, b, WORD_LEN);

    if (na != nb) return 0;
    for (int i = 0; i < na; ++i)
        if (!fold_eq(a[i], b[i])) return 0;
    return 1;
}

/* Integer.parseInt: 1 and *v, or 0 (a NumberFormatException) */
static int jparse_int(const char *s, int *v)
{
    int i = 0, neg = 0;
    int64_t x = 0;

    if (s[0] == '-' || s[0] == '+')
    {
        neg = s[0] == '-';
        i = 1;
        if (!s[1]) return 0;
    }
    if (!s[i]) return 0;
    for (; s[i]; ++i)
    {
        if (s[i] < '0' || s[i] > '9') return 0;
        x = x * 10 + (s[i] - '0');
        if (x > (int64_t)INT_MAX + 1) return 0;
    }
    if (neg) x = -x;
    if (x > INT_MAX || x < INT_MIN) return 0;
    *v = (int)x;
    return 1;
}

/* MathHelper.parseIntWithDefault */
static int parse_int_def(const char *s, int def)
{
    int v;
    return jparse_int(s, &v) ? v : def;
}

/* String.hashCode over the UTF-16 units */
static int32_t jhash(const char *s)
{
    uint16_t u[WORD_LEN];
    int n = gui_chat_utf16(s, u, WORD_LEN);
    uint32_t h = 0;
    for (int i = 0; i < n; ++i) h = 31u * h + u[i];
    return (int32_t)h;
}

/* ----------------------------------------------------- the command map */

enum
{
    C_TIME, C_GAMEMODE, C_DIFFICULTY, C_DEFAULTGAMEMODE, C_KILL, C_TOGGLEDOWNFALL, C_WEATHER, C_XP, C_TP,
    C_GIVE, C_EFFECT, C_ENCHANT, C_ME, C_SEED, C_HELP, C_DEBUG, C_TELL, C_SAY, C_SPAWNPOINT,
    C_SETWORLDSPAWN, C_GAMERULE, C_CLEAR, C_TESTFOR, C_SPREADPLAYERS, C_PLAYSOUND, C_SCOREBOARD,
    C_ACHIEVEMENT, C_SUMMON, C_SETBLOCK, C_TESTFORBLOCK, C_TELLRAW, C_COUNT
};

/* ServerCommandManager's constructor, in order (an integrated server: none
 * of the dedicated server's), each getCommandName and getCommandUsage */
static const struct { const char *name, *usage; } CMD[C_COUNT] = {
    {"time", "commands.time.usage"}, {"gamemode", "commands.gamemode.usage"},
    {"difficulty", "commands.difficulty.usage"}, {"defaultgamemode", "commands.defaultgamemode.usage"},
    {"kill", "commands.kill.usage"}, {"toggledownfall", "commands.downfall.usage"},
    {"weather", "commands.weather.usage"}, {"xp", "commands.xp.usage"}, {"tp", "commands.tp.usage"},
    {"give", "commands.give.usage"}, {"effect", "commands.effect.usage"}, {"enchant", "commands.enchant.usage"},
    {"me", "commands.me.usage"}, {"seed", "commands.seed.usage"}, {"help", "commands.help.usage"},
    {"debug", "commands.debug.usage"}, {"tell", "commands.message.usage"}, {"say", "commands.say.usage"},
    {"spawnpoint", "commands.spawnpoint.usage"}, {"setworldspawn", "commands.setworldspawn.usage"},
    {"gamerule", "commands.gamerule.usage"}, {"clear", "commands.clear.usage"},
    {"testfor", "commands.testfor.usage"}, {"spreadplayers", "commands.spreadplayers.usage"},
    {"playsound", "commands.playsound.usage"}, {"scoreboard", "commands.scoreboard.usage"},
    {"achievement", "commands.achievement.usage"}, {"summon", "commands.summon.usage"},
    {"setblock", "commands.setblock.usage"}, {"testforblock", "commands.testforblock.usage"},
    {"tellraw", "commands.tellraw.usage"},
};

/* CommandHandler.commandMap's keys as registerCommand puts them: each name,
 * then its aliases (help's ?, tell's w and msg), with the command */
static const struct { const char *key; int cmd; } KEYS[] = {
    {"time", C_TIME}, {"gamemode", C_GAMEMODE}, {"difficulty", C_DIFFICULTY},
    {"defaultgamemode", C_DEFAULTGAMEMODE}, {"kill", C_KILL}, {"toggledownfall", C_TOGGLEDOWNFALL},
    {"weather", C_WEATHER}, {"xp", C_XP}, {"tp", C_TP}, {"give", C_GIVE}, {"effect", C_EFFECT},
    {"enchant", C_ENCHANT}, {"me", C_ME}, {"seed", C_SEED}, {"help", C_HELP}, {"?", C_HELP},
    {"debug", C_DEBUG}, {"tell", C_TELL}, {"w", C_TELL}, {"msg", C_TELL}, {"say", C_SAY},
    {"spawnpoint", C_SPAWNPOINT}, {"setworldspawn", C_SETWORLDSPAWN}, {"gamerule", C_GAMERULE},
    {"clear", C_CLEAR}, {"testfor", C_TESTFOR}, {"spreadplayers", C_SPREADPLAYERS},
    {"playsound", C_PLAYSOUND}, {"scoreboard", C_SCOREBOARD}, {"achievement", C_ACHIEVEMENT},
    {"summon", C_SUMMON}, {"setblock", C_SETBLOCK}, {"testforblock", C_TESTFORBLOCK}, {"tellraw", C_TELLRAW},
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))

/* commandMap.get: HashMap's equals, case and all */
static int cmd_lookup(const char *name)
{
    for (int i = 0; i < NKEYS; ++i)
        if (!strcmp(KEYS[i].key, name)) return KEYS[i].cmd;
    return -1;
}

/* The map's iteration order (jorder.h): every key put from empty, so the
 * keys by bin of the 64-bin table, each bin in insertion order. */
static void key_order(int *order)
{
    int cap = jord_cap(NKEYS), n = 0;
    for (int b = 0; b < cap; ++b)
        for (int i = 0; i < NKEYS; ++i)
            if ((int)(jord_spread(jhash(KEYS[i].key)) & (uint32_t)(cap - 1)) == b) order[n++] = i;
}

/* ICommand.canCommandSenderUseCommand for the player: EntityPlayerMP's
 * rule (seed on a server that is not dedicated, tell, help and me; the rest
 * needs an op, which the owner of a world without commands is not);
 * CommandShowSeed adds isSinglePlayer */
static int can_use(int cmd)
{
    return cmd == C_TELL || cmd == C_HELP || cmd == C_ME || cmd == C_SEED;
}

/* --------------------------------------------------------------- output */

/* a CommandException: its key and String.valueOf of each argument */
struct cmd_ex
{
    int thrown;
    int generic;                 /* a RuntimeException: commands.generic.exception */
    const char *key;
    int nargs;
    char args[2][WORD_LEN];
};

static void ex_throw(struct cmd_ex *e, const char *key, int nargs, const char *a0, const char *a1)
{
    e->thrown = 1;
    e->key = key;
    e->nargs = nargs;
    if (nargs > 0) snprintf(e->args[0], WORD_LEN, "%s", a0);
    if (nargs > 1) snprintf(e->args[1], WORD_LEN, "%s", a1);
}

static void ex_int(struct cmd_ex *e, const char *key, int a, int b)
{
    char x[16], y[16];
    snprintf(x, sizeof x, "%d", a);
    snprintf(y, sizeof y, "%d", b);
    ex_throw(e, key, 2, x, y);
}

static void send_s02(struct chat_comp *t, int c)
{
    chat_s02_send(s2c_out(), t, c);
}

/* ICommandSender.addChatMessage(red translation of the exception) */
static void send_ex(const struct cmd_ex *e)
{
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    int c = chat_translation(t, e->generic ? "commands.generic.exception" : e->key);
    if (!e->generic)
        for (int i = 0; i < e->nargs; ++i) chat_arg_raw(t, c, e->args[i]);
    chat_set_color(t, c, CF_RED);
    send_s02(t, c);
}

/* ------------------------------------------------------ PlayerSelector */

/* \w: [a-zA-Z_0-9] */
static int is_w(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

struct sel_args
{
    int n;
    char key[16][WORD_LEN];
    char val[16][WORD_LEN];
};

static const char *sel_get(const struct sel_args *a, const char *key)
{
    for (int i = 0; i < a->n; ++i)
        if (!strcmp(a->key[i], key)) return a->val[i];
    return NULL;
}

/* HashMap.put: a key again replaces its value */
static void sel_put(struct sel_args *a, const char *key, const char *val, size_t vlen)
{
    int i = 0;
    while (i < a->n && strcmp(a->key[i], key)) ++i;
    if (i == a->n)
    {
        if (a->n >= 16) return;
        snprintf(a->key[a->n++], WORD_LEN, "%s", key);
    }
    if (vlen >= WORD_LEN) vlen = WORD_LEN - 1;
    memcpy(a->val[i], val, vlen);
    a->val[i][vlen] = 0;
}

/* tokenPattern ^@([parf])(?:\[([\w=,!-]*)\])?$: the type, and the bracket's
 * text (*args NULL without brackets) */
static int sel_token(const char *s, char *type, const char **args, size_t *nargs)
{
    if (s[0] != '@' || !s[1] || !strchr("parf", s[1])) return 0;
    *type = s[1];
    *args = NULL;
    *nargs = 0;
    if (!s[2]) return 1;
    if (s[2] != '[') return 0;
    size_t n = strlen(s);
    if (s[n - 1] != ']') return 0;
    for (size_t i = 3; i < n - 1; ++i)
        if (!is_w(s[i]) && s[i] != '=' && s[i] != ',' && s[i] != '!' && s[i] != '-') return 0;
    *args = s + 3;
    *nargs = n - 4;
    return 1;
}

/* [-!]?[\w-]* then $ or ",": its end past the separator, -1 no match */
static long sel_value(const char *s, size_t n, size_t pos, size_t *vend)
{
    size_t i = pos;
    if (i < n && (s[i] == '-' || s[i] == '!')) ++i;
    while (i < n && (is_w(s[i]) || s[i] == '-')) ++i;
    *vend = i;
    if (i == n) return (long)i;
    if (s[i] == ',') return (long)i + 1;
    return -1;
}

/* getArgumentMap */
static void sel_arg_map(const char *s, size_t n, struct sel_args *a)
{
    static const char *const POS[4] = {"x", "y", "z", "r"};
    a->n = 0;
    if (s == NULL) return;
    long last = -1;
    size_t pos = 0;
    int k = 0;
    /* intListPattern \G([-!]?[\w-]*)(?:$|,), find after find */
    for (;;)
    {
        size_t vend;
        long end = sel_value(s, n, pos, &vend);
        if (end < 0) break;
        if (k < 4 && vend > pos) sel_put(a, POS[k], s + pos, vend - pos);
        ++k;
        last = end;
        if (vend == pos && (size_t)end == pos) break;   /* an empty match at the end: the next find fails */
        pos = (size_t)end;
        if (pos > n) break;
    }
    if (last < (long)n)
    {
        /* keyValueListPattern \G(\w+)=([-!]?[\w-]*)(?:$|,) */
        pos = last < 0 ? 0 : (size_t)last;
        while (pos < n)
        {
            size_t k0 = pos;
            while (pos < n && is_w(s[pos])) ++pos;
            if (pos == k0 || pos >= n || s[pos] != '=') break;
            char key[WORD_LEN];
            size_t kl = pos - k0 < WORD_LEN - 1 ? pos - k0 : WORD_LEN - 1;
            memcpy(key, s + k0, kl);
            key[kl] = 0;
            size_t vs = pos + 1, vend;
            long end = sel_value(s, n, vs, &vend);
            if (end < 0) break;
            sel_put(a, key, s + vs, vend - vs);
            pos = (size_t)end;
            if ((size_t)end == n) break;
        }
    }
}

/* matchPlayers over the one player: -1 null (not a token, or @f), else
 * the count found (0 or 1); a RuntimeException sets e->generic */
static int sel_match(const struct server_player *p, const char *s, struct cmd_ex *e)
{
    char type;
    const char *args;
    size_t nargs;
    if (!sel_token(s, &type, &args, &nargs)) return -1;
    struct sel_args *a ENV_LOCAL = envstack_take(sizeof *a);
    sel_arg_map(args, nargs, a);

    int rm = 0, r = 0, lm = 0, l = INT_MAX, c = type == 'a' ? 0 : 1, m = -1;
    int cx = mh_floor(p->e.pos_x), cy = mh_floor(p->e.pos_y + 0.5), cz = mh_floor(p->e.pos_z);
    int has_score = 0;
    const char *v;
    for (int i = 0; i < a->n; ++i)
        if (!strncmp(a->key[i], "score_", 6) && strlen(a->key[i]) > 6) has_score = 1;
    if ((v = sel_get(a, "rm"))) rm = parse_int_def(v, rm);
    if ((v = sel_get(a, "r"))) r = parse_int_def(v, r);
    if ((v = sel_get(a, "lm"))) lm = parse_int_def(v, lm);
    if ((v = sel_get(a, "l"))) l = parse_int_def(v, l);
    if ((v = sel_get(a, "x"))) cx = parse_int_def(v, cx);
    if ((v = sel_get(a, "y"))) cy = parse_int_def(v, cy);
    if ((v = sel_get(a, "z"))) cz = parse_int_def(v, cz);
    if ((v = sel_get(a, "m"))) m = parse_int_def(v, m);
    if ((v = sel_get(a, "c"))) c = parse_int_def(v, c);
    const char *team = sel_get(a, "team"), *name = sel_get(a, "name");

    if (type != 'p' && type != 'a' && type != 'r') return -1;
    int count = type == 'r' ? 0 : c;

    /* ServerConfigurationManager.findPlayers; the world filter (the
     * sender's own, with rm, r, x, y or z) holds the one player */
    int found = 1;
    int not_name = name && name[0] == '!', not_team = team && team[0] == '!';
    if (not_name) ++name;
    if (not_team) ++team;
    if (name && not_name == equals_ic(name, CM_NAME)) found = 0;
    /* no team: the registered name "" */
    if (found && team && not_team == (team[0] == 0)) found = 0;
    if (found && (rm > 0 || r > 0))
    {
        int px = mh_floor(p->e.pos_x), py = mh_floor(p->e.pos_y + 0.5), pz = mh_floor(p->e.pos_z);
        float dx = (float)(int32_t)((uint32_t)cx - (uint32_t)px), dy = (float)(int32_t)((uint32_t)cy - (uint32_t)py),
              dz = (float)(int32_t)((uint32_t)cz - (uint32_t)pz);
        float d = dx * dx + dy * dy + dz * dz;
        int rm2 = (int32_t)((uint32_t)rm * (uint32_t)rm), r2 = (int32_t)((uint32_t)r * (uint32_t)r);
        if ((rm > 0 && d < (float)rm2) || (r > 0 && d > (float)r2)) found = 0;
    }
    /* func_96457_a: a score argument names an objective no scoreboard has */
    if (found && has_score) found = 0;
    if (found && !(m == -1 || m == 0)) found = 0;
    if (found && !((lm <= 0 || p->sv.xp_level >= lm) && p->sv.xp_level <= l)) found = 0;
    (void)count;

    if (type == 'r')
    {
        /* Collections.shuffle (one player: no draw), subList(0, min(c, size)) */
        int k = c < found ? c : found;
        if (k < 0)
        {
            e->thrown = 1;
            e->generic = 1;
            return 0;
        }
        return k;
    }
    return found;
}

/* matchesMultiplePlayers */
static int sel_multi(const char *s)
{
    char type;
    const char *args;
    size_t nargs;
    if (!sel_token(s, &type, &args, &nargs)) return 0;
    struct sel_args *a ENV_LOCAL = envstack_take(sizeof *a);
    sel_arg_map(args, nargs, a);
    int c = type == 'a' ? 0 : 1;
    const char *v = sel_get(a, "c");
    if (v) c = parse_int_def(v, c);
    return c != 1;
}

/* ------------------------------------------------------------ commands */

/* func_145748_c_ of the player, as a component of tree t */
static int player_name(struct chat_comp *t)
{
    return chatmsg_player_name(t);
}

/* CommandHelp.processCommand */
static void cmd_help(struct server_player *p, char (*args)[WORD_LEN], int nargs, struct cmd_ex *e)
{
    (void)p;
    /* getSortedPossibleCommands: the commands the player may use, by name */
    static const int USABLE[] = {C_HELP, C_ME, C_SEED, C_TELL};
    const int n = 4, per = 7, pages = (n - 1) / per;
    int page = 0;

    if (nargs > 0)
    {
        int v;
        struct cmd_ex num = {0};
        if (!jparse_int(args[0], &v)) ex_throw(&num, "commands.generic.num.invalid", 1, args[0], NULL);
        else if (v < 1) ex_int(&num, "commands.generic.num.tooSmall", v, 1);
        else if (v > pages + 1) ex_int(&num, "commands.generic.num.tooBig", v, pages + 1);
        if (num.thrown)
        {
            int c = cmd_lookup(args[0]);
            if (c >= 0) ex_throw(e, CMD[c].usage, 0, NULL, NULL);
            else if (parse_int_def(args[0], -1) != -1) *e = num;
            else ex_throw(e, "commands.generic.notFound", 0, NULL, NULL);
            return;
        }
        page = v - 1;
    }
    int end = (page + 1) * per < n ? (page + 1) * per : n;
    {
        struct chat_comp *t ENV_LOCAL = chat_comp_new();
        char a0[16], a1[16];
        snprintf(a0, sizeof a0, "%d", page + 1);
        snprintf(a1, sizeof a1, "%d", pages + 1);
        int c = chat_translation(t, "commands.help.header");
        chat_arg_raw(t, c, a0);
        chat_arg_raw(t, c, a1);
        chat_set_color(t, c, CF_DARK_GREEN);
        send_s02(t, c);
    }
    for (int i = page * per; i < end; ++i)
    {
        struct chat_comp *t ENV_LOCAL = chat_comp_new();
        char v[64];
        snprintf(v, sizeof v, "/%s ", CMD[USABLE[i]].name);
        int c = chat_translation(t, CMD[USABLE[i]].usage);
        chat_set_click(t, c, CC_CLICK_SUGGEST_COMMAND, v);
        send_s02(t, c);
    }
    if (page == 0)
    {
        struct chat_comp *t ENV_LOCAL = chat_comp_new();
        int c = chat_translation(t, "commands.help.footer");
        chat_set_color(t, c, CF_GREEN);
        send_s02(t, c);
    }
}

/* CommandEmote.processCommand */
static void cmd_me(struct server_player *p, char (*args)[WORD_LEN], int nargs, struct cmd_ex *e)
{
    if (nargs <= 0)
    {
        ex_throw(e, "commands.me.usage", 0, NULL, NULL);
        return;
    }
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    /* func_147176_a(sender, args, 0, canCommandSenderUseCommand(1, "me")),
     * which is true: every word a selector may stand for */
    int root = chat_text(t, "");
    for (int i = 0; i < nargs; ++i)
    {
        if (i > 0) chat_append_text(t, root, " ");
        int w;
        int k = sel_match(p, args[i], e);
        if (e->thrown) return;
        if (k > 0)
        {
            /* func_150869_b: joinNiceString of the matched names */
            w = chat_text(t, "");
            chat_append(t, w, player_name(t));
        }
        else if (k == 0 || sel_token(args[i], &(char){0}, &(const char *){NULL}, &(size_t){0}))
        {
            /* a token that matches nobody (hasArguments) */
            ex_throw(e, "commands.generic.player.notFound", 0, NULL, NULL);
            return;
        }
        else w = chat_text(t, args[i]);
        chat_append(t, root, w);
    }
    int c = chat_translation(t, "chat.type.emote");
    chat_arg(t, c, player_name(t));
    chat_arg(t, c, root);
    /* func_148539_a: every player's S02 */
    send_s02(t, c);
}

/* CommandMessage.processCommand: the one player is always the sender */
static void cmd_tell(struct server_player *p, char (*args)[WORD_LEN], int nargs, struct cmd_ex *e)
{
    if (nargs < 2)
    {
        ex_throw(e, "commands.message.usage", 0, NULL, NULL);
        return;
    }
    /* getPlayer: matchOnePlayer, then the name ignoring case */
    int k = sel_match(p, args[0], e);
    if (e->thrown) return;
    if (k != 1 && !equals_ic(args[0], CM_NAME))
    {
        ex_throw(e, "commands.generic.player.notFound", 0, NULL, NULL);
        return;
    }
    ex_throw(e, "commands.message.sameTarget", 0, NULL, NULL);
}

/* CommandShowSeed.processCommand */
static void cmd_seed(struct server_player *p)
{
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    char s[32];
    snprintf(s, sizeof s, "%lld", (long long)(p->e.world ? p->e.world->seed : 0));
    int c = chat_translation(t, "commands.seed.success");
    chat_arg_raw(t, c, s);
    send_s02(t, c);
}

static void process(struct server_player *p, int cmd, char (*args)[WORD_LEN], int nargs, struct cmd_ex *e)
{
    switch (cmd)
    {
    case C_HELP: cmd_help(p, args, nargs, e); break;
    case C_ME: cmd_me(p, args, nargs, e); break;
    case C_TELL: cmd_tell(p, args, nargs, e); break;
    case C_SEED: cmd_seed(p); break;
    default: break;
    }
}

/* CommandHandler.executeCommand */
static void execute(struct server_player *p, const char *line)
{
    char (*w)[WORD_LEN] ENV_LOCAL = envstack_take(WORDS * sizeof *w);
    char s[WORD_LEN];
    const char *a = line;
    size_t n;

    while (*a && (unsigned char)*a <= 32) ++a;
    n = strlen(a);
    while (n > 0 && (unsigned char)a[n - 1] <= 32) --n;
    if (n >= WORD_LEN) n = WORD_LEN - 1;
    memcpy(s, a, n);
    s[n] = 0;
    const char *body = s[0] == '/' ? s + 1 : s;
    int nw = jsplit(body, 0, w, WORDS);
    int cmd = cmd_lookup(w[0]);
    char (*args)[WORD_LEN] = w + 1;
    int nargs = nw - 1;
    struct cmd_ex e = {0};

    int uidx = -1;
    if (cmd == C_TELL)
        for (int i = 0; i < nargs; ++i)
            if (i == 0 && sel_multi(args[i])) { uidx = i; break; }

    if (cmd < 0)
    {
        ex_throw(&e, "commands.generic.notFound", 0, NULL, NULL);
        send_ex(&e);
        return;
    }
    if (!can_use(cmd))
    {
        ex_throw(&e, "commands.generic.permission", 0, NULL, NULL);
        send_ex(&e);
        return;
    }
    if (uidx > -1)
    {
        int k = sel_match(p, args[uidx], &e);
        if (e.thrown || k < 0)
        {
            /* a RuntimeException (@r's subList), or matchPlayers' null (@f):
             * the loop's NullPointerException */
            e.thrown = e.generic = 1;
            send_ex(&e);
            return;
        }
        char keep[WORD_LEN];
        memcpy(keep, args[uidx], WORD_LEN);
        for (int i = 0; i < k; ++i)
        {
            snprintf(args[uidx], WORD_LEN, "%s", CM_NAME);
            struct cmd_ex x = {0};
            process(p, cmd, args, nargs, &x);
            if (x.thrown) send_ex(&x);
        }
        memcpy(args[uidx], keep, WORD_LEN);
        return;
    }
    process(p, cmd, args, nargs, &e);
    if (e.thrown) send_ex(&e);
}

/* ----------------------------------------------------- processChatMessage */

void chatcmd_message(struct server_player *p, const char *text)
{
    /* func_143004_u: the idle clock (the wall clock, pinned) */
    /* StringUtils.normalizeSpace: trimmed, each run of spaces one space (the
     * text field lets no other whitespace in) */
    char s[WORD_LEN];
    size_t k = 0;
    const char *a = text;
    while (*a && (unsigned char)*a <= 32) ++a;
    for (; *a && k < WORD_LEN - 1; ++a)
    {
        if (*a == ' ' && (k == 0 || s[k - 1] == ' ')) continue;
        s[k++] = *a;
    }
    while (k > 0 && (unsigned char)s[k - 1] <= 32) --k;
    s[k] = 0;

    if (s[0] == '/') execute(p, s);
    else
    {
        struct chat_comp *t ENV_LOCAL = chat_comp_new();
        int c = chat_translation(t, "chat.type.text");
        chat_arg(t, c, player_name(t));
        chat_arg_raw(t, c, s);
        /* func_148544_a(component, false): every player's S02 */
        send_s02(t, c);
    }
    p->chat_spam += 20;
    if (p->chat_spam > 200) p->chat_unmodelled = "the chat spam kick (disconnect.spam)";
}

void chatcmd_network_tick(struct server_player *p)
{
    if (p->chat_spam > 0) --p->chat_spam;
}

const char *chatcmd_unmodelled(const struct server_player *p)
{
    return p->chat_unmodelled;
}

/* ----------------------------------------------------- processTabComplete */

struct tab_list
{
    int n;
    char s[GC_TABS][WORD_LEN];
};

static void tab_add(struct tab_list *l, const char *s)
{
    if (l->n >= GC_TABS) return;
    size_t n = strlen(s);
    if (n >= WORD_LEN) n = WORD_LEN - 1;
    memcpy(l->s[l->n], s, n);
    l->s[l->n++][n] = 0;
}

/* getListOfStringsMatchingLastWord */
static void match_last(struct tab_list *l, char (*args)[WORD_LEN], int nargs, const char *const *opts, int nopts)
{
    const char *last = args[nargs - 1];
    for (int i = 0; i < nopts; ++i)
        if (starts_with_ic(last, opts[i])) tab_add(l, opts[i]);
}

static const char *const PLAYERS[] = {CM_NAME};

/* ICommand.addTabCompletionOptions; 0 when it reads a registry's names,
 * which are not modelled */
static int command_tabs(struct server_player *p, int cmd, char (*args)[WORD_LEN], int nargs, struct tab_list *l)
{
    (void)p;
    static const char *const SETADD[] = {"set", "add"}, *const DAYNIGHT[] = {"day", "night"},
                             *const MODES[] = {"survival", "creative", "adventure"},
                             *const DIFF[] = {"peaceful", "easy", "normal", "hard"},
                             *const WEATHER[] = {"clear", "rain", "thunder"}, *const DEBUG[] = {"start", "stop"},
                             *const TF[] = {"true", "false"}, *const GIVE[] = {"give"},
                             *const HANDLING[] = {"replace", "destroy", "keep"},
                             /* GameRules' TreeMap keys */
                             *const RULES[] = {"commandBlockOutput", "doDaylightCycle", "doFireTick", "doMobLoot",
                                               "doMobSpawning", "doTileDrops", "keepInventory", "mobGriefing",
                                               "naturalRegeneration"},
                             *const SB[] = {"objectives", "players", "teams"},
                             *const SB_OBJ[] = {"list", "add", "remove", "setdisplay"},
                             *const SB_SLOT[] = {"list", "sidebar", "belowName"},
                             *const SB_PL[] = {"set", "add", "remove", "reset", "list"},
                             *const SB_TEAM[] = {"add", "remove", "join", "leave", "empty", "list", "option"},
                             *const SB_OPT[] = {"color", "friendlyfire", "seeFriendlyInvisibles"},
                             /* EnumChatFormatting.getValidValues(true, false) */
                             *const COLORS[] = {"black", "dark_blue", "dark_green", "dark_aqua", "dark_red",
                                                "dark_purple", "gold", "gray", "dark_gray", "blue", "green", "aqua",
                                                "red", "light_purple", "yellow", "white", "reset"};
#define ML(a) match_last(l, args, nargs, a, (int)(sizeof a / sizeof a[0]))
    switch (cmd)
    {
    case C_TIME:
        if (nargs == 1) ML(SETADD);
        else if (nargs == 2 && !strcmp(args[0], "set")) ML(DAYNIGHT);
        return 1;
    case C_GAMEMODE:
    case C_DEFAULTGAMEMODE:
        if (nargs == 1) ML(MODES);
        else if (nargs == 2) ML(PLAYERS);
        return 1;
    case C_DIFFICULTY:
        if (nargs == 1) ML(DIFF);
        return 1;
    case C_WEATHER:
        if (nargs == 1) ML(WEATHER);
        return 1;
    case C_XP:
        if (nargs == 2) ML(PLAYERS);
        return 1;
    case C_TP:
    case C_SPAWNPOINT:
        if (nargs == 1 || nargs == 2) ML(PLAYERS);
        return 1;
    case C_GIVE:
    case C_CLEAR:
        if (nargs == 1) ML(PLAYERS);
        else if (nargs == 2) return 0;   /* Item.itemRegistry's keys */
        return 1;
    case C_EFFECT:
    case C_ENCHANT:
    case C_TELLRAW:
        if (nargs == 1) ML(PLAYERS);
        return 1;
    case C_ME:
    case C_TELL:
        ML(PLAYERS);
        return 1;
    case C_SAY:
        if (nargs >= 1) ML(PLAYERS);
        return 1;
    case C_DEBUG:
        if (nargs == 1) ML(DEBUG);
        return 1;
    case C_GAMERULE:
        if (nargs == 1) ML(RULES);
        else if (nargs == 2) ML(TF);
        return 1;
    case C_ACHIEVEMENT:
        if (nargs == 1) ML(GIVE);
        else if (nargs == 2) return 0;   /* StatList.allStats' ids */
        else if (nargs == 3) ML(PLAYERS);
        return 1;
    case C_SUMMON:
        if (nargs == 1) return 0;        /* EntityList's names */
        return 1;
    case C_SETBLOCK:
        if (nargs == 4) return 0;        /* Block.blockRegistry's keys */
        if (nargs == 6) ML(HANDLING);
        return 1;
    case C_TESTFORBLOCK:
        if (nargs == 4) return 0;
        return 1;
    case C_SCOREBOARD:
        /* the scoreboard has no objective and no team: no command makes one
         * in a world without commands */
        if (nargs == 1) { ML(SB); return 1; }
        if (equals_ic(args[0], "objectives"))
        {
            if (nargs == 2) ML(SB_OBJ);
            else if (equals_ic(args[1], "add")) { if (nargs == 4) return 0; }   /* the criteria's names */
            else if (equals_ic(args[1], "setdisplay") && nargs == 3) ML(SB_SLOT);
        }
        else if (equals_ic(args[0], "players"))
        {
            if (nargs == 2) ML(SB_PL);
            else if ((equals_ic(args[1], "set") || equals_ic(args[1], "add") || equals_ic(args[1], "remove")) && nargs == 3)
                ML(PLAYERS);
        }
        else if (equals_ic(args[0], "teams"))
        {
            if (nargs == 2) ML(SB_TEAM);
            else if (equals_ic(args[1], "join")) { if (nargs >= 4) ML(PLAYERS); }
            else if (equals_ic(args[1], "leave")) ML(PLAYERS);
            else if (equals_ic(args[1], "option"))
            {
                if (nargs == 4) ML(SB_OPT);
                else if (nargs == 5)
                {
                    if (equals_ic(args[3], "color")) ML(COLORS);
                    else if (equals_ic(args[3], "friendlyfire") || equals_ic(args[3], "seeFriendlyInvisibles")) ML(TF);
                }
            }
        }
        return 1;
    default:
        return 1;
    }
#undef ML
}

void chatcmd_tab(struct server_player *p, const char *text)
{
    struct tab_list *l ENV_LOCAL = envstack_zeroed(sizeof *l);
    char (*w)[WORD_LEN] ENV_LOCAL = envstack_take(WORDS * sizeof *w);

    /* MinecraftServer.getPossibleCompletions */
    if (text[0] == '/')
    {
        const char *body = text + 1;
        int flag = strchr(body, ' ') == NULL;
        int nw = jsplit(body, 1, w, WORDS);
        struct tab_list *got ENV_LOCAL = envstack_zeroed(sizeof *got);
        if (nw == 1)
        {
            /* getPossibleCommands: the map's keys in its order */
            int order[NKEYS];
            key_order(order);
            for (int i = 0; i < NKEYS; ++i)
            {
                int k = order[i];
                if (starts_with_ic(w[0], KEYS[k].key) && can_use(KEYS[k].cmd)) tab_add(got, KEYS[k].key);
            }
        }
        else
        {
            int cmd = cmd_lookup(w[0]);
            if (cmd >= 0 && !command_tabs(p, cmd, w + 1, nw - 1, got))
                p->chat_unmodelled = "a tab completion over a registry's names";
        }
        for (int i = 0; i < got->n; ++i)
        {
            char s[WORD_LEN + 1];
            snprintf(s, sizeof s, "%s%s", flag ? "/" : "", got->s[i]);
            tab_add(l, s);
        }
    }
    else
    {
        int nw = jsplit(text, 1, w, WORDS);
        if (starts_with_ic(w[nw - 1], CM_NAME)) tab_add(l, CM_NAME);
    }

    /* the S3A: its strings out of line, NUL after each */
    char *side ENV_LOCAL = envstack_take(GC_TABS * 48);
    size_t k = 0;
    int n = 0;
    for (int i = 0; i < l->n; ++i)
    {
        size_t m = strlen(l->s[i]) + 1;
        if (k + m > GC_TABS * 48 || k + m > S2C_SIDE_MAX_PKT) break;
        memcpy(side + k, l->s[i], m);
        k += m;
        ++n;
    }
    struct s2c_queue *q = s2c_out();
    struct s2c_pkt *pkt = s2c_add(q);
    pkt->kind = PK_S3A;
    pkt->i0 = n;
    if (k > 0) memcpy(s2c_side_new(q, pkt, k), side, k);
}
