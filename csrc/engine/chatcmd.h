/* The server's half of the chat screen: NetHandlerPlayServer.processChatMessage
 * (a line to every player, or a command) and processTabComplete (the S3A
 * of MinecraftServer.getPossibleCompletions), with the ServerCommandManager
 * of the integrated server as a player without cheats meets it: every
 * command name is known (the commands.generic.notFound line for the rest),
 * only tell (w, msg), me, help (?) and seed are allowed (the
 * commands.generic.permission line for the others: the owner is no op while
 * the world allows no commands), PlayerSelector's @p, @a, @r and @f over the
 * one player. */
#ifndef NETHERITE_CHATCMD_H
#define NETHERITE_CHATCMD_H

struct server_player;

/* processChatMessage over a C01's text (UTF-8). */
void chatcmd_message(struct server_player *p, const char *text);

/* processTabComplete over a C14's text (UTF-8): the S3A. */
void chatcmd_tab(struct server_player *p, const char *text);

/* onNetworkTick's chatSpamThresholdCount decay. */
void chatcmd_network_tick(struct server_player *p);

/* What the last call reached that is not modelled (NULL none): the spam
 * kick, a completion over a registry's names. */
const char *chatcmd_unmodelled(const struct server_player *p);

#endif
