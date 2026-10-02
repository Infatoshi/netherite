/* A snapshot session: the load, the start and the tick pair every consumer of
 * a recording runs, so the playable client (play/play.c) and the replay gate
 * (tests/test_snapshots.c) cannot drift apart.
 *
 * session_open takes a loaded snapshot and its tape header and builds the two
 * players, the whole-server replay (a checkpoint start's own save resolved),
 * the client's world, the survival module and the containers exactly as the
 * join left them. session_tick runs one row: the header's setup entries, the
 * client player's tick with the packets the last server tick sent, then the
 * whole server's tick and the server player's (NetworkManager order). An
 * older movement snapshot (not a whole-server one) runs the two players
 * alone. The caller owns the rows: which to run, what to compare. */
#ifndef NETHERITE_SESSION_H
#define NETHERITE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "det.h"
#include "item_entity.h"
#include "player.h"
#include "survival.h"
#include "jorder.h"

struct jval;
struct snapshot;
struct serverreplay;
struct world;

/* The consumer's own work at fixed points of a row; any may be NULL. */
struct session_hooks {
    void *ctx;
    /* serverreplay_sync_client_chunks' per-chunk callback */
    void (*on_chunk)(void *ctx, char kind, int cx, int cz);
    /* after an S07 gave the client an empty world of dimension dim */
    void (*world_changed)(void *ctx, int dim);
    /* after the client's packets and chunks, before the client player's
     * tick; paused is Minecraft.isGamePaused */
    void (*before_client_tick)(void *ctx, int paused);
    /* after the whole server's world tick, before the server player's */
    void (*after_world_tick)(void *ctx);
    /* right after the server player's tick */
    void (*after_player_tick)(void *ctx);
};

typedef int (*session_dev_apply_fn)(struct serverreplay *sr, struct server_player *sp, const struct jval *cmd,
                                    char *error, int error_size);
typedef void (*session_dev_end_fn)(struct serverreplay *sr);

#define SESSION_DEV_MAX 16384

struct session {
    struct snapshot *s;
    const struct jval *hdr;         /* the tape header the rows belong to */
    struct client_player cp;
    struct server_player sp;
    struct serverreplay *sr;        /* the whole server (NULL on a movement snapshot) */
    struct world *client_world;     /* the client's own world (whole server only) */
    int server_rows;                /* the rows carry the whole server's w and d */
    int no_gen;                     /* the manifest's noChunkGeneration for the overworld */
    /* a movement snapshot's own det streams and item world */
    det_state det;
    ie_world iew;
    /* the server player the next world tick reads: the position the
     * previous tick's processPlayer left */
    double sv_px, sv_py, sv_pz;
    int carry_c16, was_paused;
    /* GameSettings.renderDistanceChunks as the last row's opts left it (0:
     * no row has set it, the server keeps its own) */
    int client_rd;
    /* WorldClient's own light keeping: its previousActiveChunkSet (the
     * active chunks whose relight checks ran since the set last moved) and
     * the activeChunkSet table's length (clear() keeps the largest) */
    int cw_prev[128][2];
    int cw_nprev, cw_active_cap;
    struct jord_window_memo cw_memo;   /* the last active-square order (jord_ccp_window_memo) */
    /* the tape's start.spawnerPin: a spawner's display mob turns its head
     * by Det.PIN_SPAWNER's draw (play) */
    int spawner_pin;
    /* Java's getTickCounter inside row t is t + tc_offset (stats.json's
     * tickCounter is the counter the snapshot's row started from) */
    int64_t tc_offset;
    /* the header's setup entries, then the extra ones (play-dev's sidecar),
     * merged by tick with the header's first */
    const struct jval *setups;
    int hdr_rd;                     /* the header's options.rd (4 without): session_client_blocks' default */
    int next_setup;
    const struct jval *const *extra;
    int nextra, next_extra;
    /* Dev entries due at a paused row wait for the next server tick */
    const struct jval *dev_pending[SESSION_DEV_MAX];
    int ndev_pending;
    /* the dev ops (dev.c), set by a NETHERITE_DEV build; NULL refuses a Dev entry */
    session_dev_apply_fn dev_apply;
    session_dev_end_fn dev_end;
    /* a start's difficulty (the env pool's pool_config.difficulty): D + 1
     * sets difficulty D at the head of the next server tick, where a Dev
     * difficulty entry at that tick runs (MinecraftServer.func_147139_a);
     * 0 keeps the world's */
    int start_difficulty;
    struct session_hooks hooks;
    char err[512];
};

/* 0 when a build may run this tape: a Dev entry needs a dev tape (and never a
 * play-mode header), a dev snapshot needs a dev tape, and a product build
 * (dev_build 0) refuses a dev tape outright. */
int session_refuse_dev(const struct jval *hdr, const struct jval *manifest, int dev_build);

/* Everything between a loaded snapshot and its first row. s stays the
 * caller's and must outlive the session; dir is the snapshot's directory
 * (the checkpoint and join-save resolution reads it). Set hooks, dev_apply
 * and extra before the first session_tick. 0 with err on failure. */
int session_open(struct session *ss, struct snapshot *s, const char *dir, const struct jval *hdr);
/* The same for a snapshot that is not the recording's own start (a keyframe,
 * tests/segments.sh): dir is the snapshot's directory (its files and save
 * overlay), rec_dir the recording's (a checkpoint start resolves beside it). */
int session_open_at(struct session *ss, struct snapshot *s, const char *dir, const char *rec_dir, const struct jval *hdr);

/* A tape row's act, as the oracle wrote it. The number of malformed parts
 * (the first one described in err); the act holds what did parse. */
int session_parse_act(const struct jval *row, struct act *act, char *err, size_t err_size);

/* One row at tick t (t >= the snapshot's tick). 0 with err when a setup
 * entry cannot run. */
int session_tick(struct session *ss, int64_t t, const struct act *act);

/* GameSettings.renderDistanceChunks now: the last row's opts rd, else the
 * header's options rd (4 when it names none) */
int session_client_rd(const struct session *ss);

void session_close(struct session *ss);

#endif
